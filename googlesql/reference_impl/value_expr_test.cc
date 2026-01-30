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

// Tests for ValueExprs not covered by other tests.

#include <cstdint>
#include <limits>
#include <memory>
#include <optional>
#include <ostream>
#include <string>
#include <utility>
#include <vector>

#include "googlesql/base/logging.h"
#include "googlesql/common/graph_element_utils.h"
#include "googlesql/common/internal_value.h"
#include "googlesql/common/status_payload_utils.h"
#include "googlesql/base/testing/status_matchers.h"
#include "googlesql/compliance/functions_testlib.h"
#include "googlesql/compliance/functions_testlib_common.h"
#include "googlesql/public/builtin_function.h"
#include "googlesql/public/builtin_function_options.h"
#include "googlesql/public/catalog.h"
#include "googlesql/public/function.h"
#include "googlesql/public/function_signature.h"
#include "googlesql/public/id_string.h"
#include "googlesql/public/json_value.h"
#include "googlesql/public/language_options.h"
#include "googlesql/public/options.pb.h"
#include "googlesql/public/property_graph.h"
#include "googlesql/public/proto_util.h"
#include "googlesql/public/simple_catalog.h"
#include "googlesql/public/simple_property_graph.h"
#include "googlesql/public/type.h"
#include "googlesql/public/type.pb.h"
#include "googlesql/public/types/graph_element_type.h"
#include "googlesql/public/types/timestamp_util.h"
#include "googlesql/public/types/type_factory.h"
#include "googlesql/public/value.h"
#include "googlesql/reference_impl/evaluation.h"
#include "googlesql/reference_impl/function.h"
#include "googlesql/reference_impl/operator.h"
#include "googlesql/reference_impl/test_relational_op.h"
#include "googlesql/reference_impl/tuple.h"
#include "googlesql/reference_impl/tuple_test_util.h"
#include "googlesql/reference_impl/type_helpers.h"
#include "googlesql/reference_impl/variable_generator.h"
#include "googlesql/reference_impl/variable_id.h"
#include "googlesql/resolved_ast/resolved_ast.h"
#include "googlesql/resolved_ast/resolved_ast_builder.h"
#include "googlesql/resolved_ast/resolved_column.h"
#include "googlesql/resolved_ast/resolved_node_kind.pb.h"
#include "googlesql/testdata/test_schema.pb.h"
#include "googlesql/testing/test_function.h"
#include "googlesql/testing/test_value.h"
#include "googlesql/testing/using_test_value.cc"
#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "absl/container/flat_hash_map.h"
#include "absl/container/flat_hash_set.h"
#include "absl/flags/flag.h"
#include "absl/memory/memory.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/cord.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/string_view.h"
#include "absl/types/span.h"
#include "google/protobuf/io/coded_stream.h"
#include "google/protobuf/io/zero_copy_stream_impl_lite.h"
#include "google/protobuf/wire_format_lite.h"
#include "googlesql/base/ret_check.h"
#include "googlesql/base/status_macros.h"

namespace googlesql {
namespace {

static const auto DEFAULT_ERROR_MODE =
    ResolvedFunctionCallBase::DEFAULT_ERROR_MODE;

using SharedProtoState = TupleSlot::SharedProtoState;

using ::std::nullopt;

using ::google::protobuf::internal::WireFormatLite;

using ::testing::_;
using ::testing::AnyOf;
using ::testing::Bool;
using ::testing::ElementsAre;
using ::testing::Eq;
using ::testing::HasSubstr;
using ::testing::IsNull;
using ::testing::Not;
using ::testing::Pointee;
using ::testing::Property;
using ::testing::TestWithParam;
using ::testing::UnorderedElementsAre;
using ::testing::ValuesIn;
using ::testing::WithParamInterface;
using ::absl_testing::IsOkAndHolds;
using ::absl_testing::StatusIs;

// For readability.
std::vector<const TupleSchema*> EmptyParamsSchemas() { return {}; }
std::vector<const TupleData*> EmptyParams() { return {}; }

// For convenience.
absl::StatusOr<Value> EvalExpr(const ValueExpr& expr,
                               absl::Span<const TupleData* const> params,
                               EvaluationContext* context = nullptr) {
  EvaluationContext empty_context((EvaluationOptions()));
  if (context == nullptr) {
    context = &empty_context;
  }
  TupleSlot slot;
  absl::Status status;
  if (!expr.EvalSimple(params, context, &slot, &status)) {
    return status;
  }
  if (!TupleSlot::ShouldStoreSharedProtoStateFor(slot.value().type_kind())) {
    EXPECT_THAT(*slot.mutable_shared_proto_state(), IsNull());
  }
  return slot.value();
}

std::unique_ptr<ScalarFunctionBody> CreateFunction(
    FunctionKind kind, const Type* output_type,
    absl::Span<const std::unique_ptr<AlgebraArg>> arguments = {}) {
  LanguageOptions language_options;
  language_options.EnableMaximumLanguageFeaturesForDevelopment();
  return BuiltinScalarFunction::CreateValidated(kind, language_options,
                                                output_type, arguments)
      .value();
}

// -------------------------------------------------------
// Scalar functions
// -------------------------------------------------------

struct NaryFunctionTemplate {
 public:
  FunctionKind kind;
  QueryParamsWithResult params;

  NaryFunctionTemplate(FunctionKind kind,
                       absl::Span<const ValueConstructor> arguments,
                       const ValueConstructor& result,
                       absl::string_view error_message)
      : kind(kind), params(arguments, result, error_message) {}
  NaryFunctionTemplate(FunctionKind kind,
                       absl::Span<const ValueConstructor> arguments,
                       const ValueConstructor& result)
      : kind(kind), params(arguments, result) {}
  NaryFunctionTemplate(FunctionKind kind, const QueryParamsWithResult& params)
      : kind(kind), params(params) {}
};

std::ostream& operator<<(std::ostream& out, const NaryFunctionTemplate& t) {
  std::vector<std::unique_ptr<ValueExpr>> arguments;
  arguments.reserve(t.params.num_params());
  std::vector<std::unique_ptr<AlgebraArg>> algebra_args;
  algebra_args.reserve(t.params.num_params());
  for (int i = 0; i < t.params.num_params(); ++i) {
    arguments.push_back(ConstExpr::Create(t.params.param(i)).value());
    algebra_args.emplace_back(std::make_unique<ExprArg>(
        ConstExpr::Create(t.params.param(i)).value()));
  }
  auto fct_op =
      ScalarFunctionCallExpr::Create(
          CreateFunction(t.kind, t.params.result().type(), algebra_args),
          std::move(arguments))
          .value();
  out << fct_op->DebugString() << " == ";
  if (t.params.required_features().empty()) {
    out << t.params.result().DebugString(/*verbose=*/true);
  } else {
    out << "(";
    const QueryParamsWithResult::ResultMap& result_map = t.params.results();
    for (auto iter = result_map.begin(); iter != result_map.end(); ++iter) {
      if (iter != result_map.begin()) {
        out << ", ";
      }
      out << iter->first << ":"
          << iter->second.result.DebugString(/*verbose=*/true);
    }
    out << ")";
  }
  return out;
}

std::vector<NaryFunctionTemplate> GetFunctionTemplates(
    FunctionKind kind, absl::Span<const QueryParamsWithResult> tests) {
  std::vector<NaryFunctionTemplate> templates;
  for (const auto& t : tests) {
    templates.emplace_back(kind, t);
  }
  return templates;
}

// Returns only those function templates that have non-null arguments or no
// arguments. This method is used to filter out null templates for Cast()
// because the cast implementation would cast any null input to any output type
// (it relies on the function signatures to prevent these casts in SQL queries).
std::vector<QueryParamsWithResult> NonNullArguments(
    absl::Span<const QueryParamsWithResult> tests) {
  std::vector<QueryParamsWithResult> result;
  for (const auto& t : tests) {
    bool nulls_only = true;
    for (const auto& value : t.params()) {
      if (!value.is_null()) {
        nulls_only = false;
        break;
      }
    }
    if (!t.params().empty() && nulls_only) continue;
    result.push_back(t);
  }
  return result;
}

typedef TestWithParam<NaryFunctionTemplate> NaryFunctionTemplateTest;

TEST_P(NaryFunctionTemplateTest, NaryFunctionTest) {
  const NaryFunctionTemplate& t = GetParam();
  for (const auto& each : t.params.results()) {
    LanguageOptions language_options;
    for (const auto& feature : each.first) {
      language_options.EnableLanguageFeature(feature);
    }
    EvaluationContext context((EvaluationOptions()));
    context.SetLanguageOptions(language_options);

    const Type* first_argument_type;
    bool mismatched_types_other_than_int64_or_uint64 = false;
    std::vector<std::unique_ptr<AlgebraArg>> arguments;
    for (int i = 0; i < t.params.num_params(); ++i) {
      GOOGLESQL_ASSERT_OK_AND_ASSIGN(auto arg, ConstExpr::Create(t.params.param(i)));
      arguments.push_back(std::make_unique<ExprArg>(std::move(arg)));

      const Type* arg_type = arguments.back()->value_expr()->output_type();
      if (i == 0) {
        first_argument_type = arg_type;
      } else if (!first_argument_type->Equals(arg_type)) {
        if ((first_argument_type->kind() != TYPE_INT64 &&
             first_argument_type->kind() != TYPE_UINT64) ||
            (arg_type->kind() != TYPE_INT64 &&
             arg_type->kind() != TYPE_UINT64)) {
          mismatched_types_other_than_int64_or_uint64 = true;
        }
      }
    }
    GOOGLESQL_ASSERT_OK_AND_ASSIGN(
        auto function_body,
        BuiltinScalarFunction::CreateValidated(
            t.kind, language_options, t.params.result().type(), arguments));
    GOOGLESQL_ASSERT_OK_AND_ASSIGN(
        auto fct, ScalarFunctionCallExpr::Create(std::move(function_body),
                                                 std::move(arguments)));
    const absl::StatusOr<Value> function_value =
        EvalExpr(*fct, EmptyParams(), &context);

    if (each.second.status.ok()) {
      if (mismatched_types_other_than_int64_or_uint64) {
        // Some of the compliance tests cover coercion cases for the comparison
        // operators. Since no scalar functions exist for direct comparisons of
        // e.g. NUMERIC with INT64, the reference implementation will return an
        // undefined result.
        continue;
      }
      EXPECT_THAT(function_value, IsOkAndHolds(each.second.result));
    } else {
      EXPECT_FALSE(function_value.status().ok());
      if (each.second.status.code() != absl::StatusCode::kUnknown) {
        EXPECT_THAT(
            function_value,
            StatusIs(each.second.status.code(),
                     HasSubstr(std::string(each.second.status.message()))));
      }
    }
  }
}

INSTANTIATE_TEST_SUITE_P(
    UnaryMinus, NaryFunctionTemplateTest,
    ValuesIn(GetFunctionTemplates(FunctionKind::kUnaryMinus,
                                  GetFunctionTestsUnaryMinus())));

INSTANTIATE_TEST_SUITE_P(Add, NaryFunctionTemplateTest,
                         ValuesIn(GetFunctionTemplates(FunctionKind::kAdd,
                                                       GetFunctionTestsAdd())));

INSTANTIATE_TEST_SUITE_P(
    Subtract, NaryFunctionTemplateTest,
    ValuesIn(GetFunctionTemplates(FunctionKind::kSubtract,
                                  GetFunctionTestsSubtract())));

// TODO: replace individual CAST tests by a wholesale test once datetime
// and bool support is in place.
INSTANTIATE_TEST_SUITE_P(CastBool, NaryFunctionTemplateTest,
                         ValuesIn(GetFunctionTemplates(
                             FunctionKind::kCast,
                             NonNullArguments(GetFunctionTestsCastBool()))));

INSTANTIATE_TEST_SUITE_P(
    CastNumeric, NaryFunctionTemplateTest,
    ValuesIn(GetFunctionTemplates(FunctionKind::kCast,
                                  GetFunctionTestsCastNumeric())));

INSTANTIATE_TEST_SUITE_P(
    CastString, NaryFunctionTemplateTest,
    ValuesIn(GetFunctionTemplates(FunctionKind::kCast,
                                  GetFunctionTestsCastString())));

INSTANTIATE_TEST_SUITE_P(And, NaryFunctionTemplateTest,
                         ValuesIn(GetFunctionTemplates(FunctionKind::kAnd,
                                                       GetFunctionTestsAnd())));

INSTANTIATE_TEST_SUITE_P(Or, NaryFunctionTemplateTest,
                         ValuesIn(GetFunctionTemplates(FunctionKind::kOr,
                                                       GetFunctionTestsOr())));

INSTANTIATE_TEST_SUITE_P(Not, NaryFunctionTemplateTest,
                         ValuesIn(GetFunctionTemplates(FunctionKind::kNot,
                                                       GetFunctionTestsNot())));

INSTANTIATE_TEST_SUITE_P(Equal, NaryFunctionTemplateTest,
                         ValuesIn(GetFunctionTemplates(
                             FunctionKind::kEqual, GetFunctionTestsEqual())));

INSTANTIATE_TEST_SUITE_P(Less, NaryFunctionTemplateTest,
                         ValuesIn(GetFunctionTemplates(
                             FunctionKind::kLess, GetFunctionTestsLess())));

INSTANTIATE_TEST_SUITE_P(
    LessOrEqual, NaryFunctionTemplateTest,
    ValuesIn(GetFunctionTemplates(FunctionKind::kLessOrEqual,
                                  GetFunctionTestsLessOrEqual())));

INSTANTIATE_TEST_SUITE_P(IsNull, NaryFunctionTemplateTest,
                         ValuesIn(GetFunctionTemplates(
                             FunctionKind::kIsNull, GetFunctionTestsIsNull())));

INSTANTIATE_TEST_SUITE_P(
    ArrayAtOffset, NaryFunctionTemplateTest,
    ValuesIn(GetFunctionTemplates(FunctionKind::kArrayAtOffset,
                                  GetFunctionTestsAtOffset())));

INSTANTIATE_TEST_SUITE_P(
    SafeArrayAtOffset, NaryFunctionTemplateTest,
    ValuesIn(GetFunctionTemplates(FunctionKind::kSafeArrayAtOffset,
                                  GetFunctionTestsSafeAtOffset())));

INSTANTIATE_TEST_SUITE_P(Least, NaryFunctionTemplateTest,
                         ValuesIn(GetFunctionTemplates(
                             FunctionKind::kLeast, GetFunctionTestsLeast())));

INSTANTIATE_TEST_SUITE_P(
    Greatest, NaryFunctionTemplateTest,
    ValuesIn(GetFunctionTemplates(FunctionKind::kGreatest,
                                  GetFunctionTestsGreatest())));

class EvalTest : public ::testing::Test {
 protected:
  void SetUp() override {
    TypeFactory* type_factory = test_values::static_type_factory();
    GOOGLESQL_ASSERT_OK(type_factory->MakeProtoType(
        googlesql_test::KitchenSinkPB::descriptor(), &proto_type_));
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
};

TEST_F(EvalTest, NewStructExpr) {
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(auto one_expr, ConstExpr::Create(Int64(1)));
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(auto foo_expr, ConstExpr::Create(String("foo")));

  std::vector<std::unique_ptr<ExprArg>> args;
  args.push_back(std::make_unique<ExprArg>(std::move(one_expr)));
  args.push_back(std::make_unique<ExprArg>(std::move(foo_expr)));

  GOOGLESQL_ASSERT_OK_AND_ASSIGN(
      auto struct_op,
      NewStructExpr::Create(
          MakeStructType({{"a", Int64Type()}, {"b", StringType()}}),
          std::move(args)));
  GOOGLESQL_ASSERT_OK(struct_op->SetSchemasForEvaluation(EmptyParamsSchemas()));
  EXPECT_EQ(
      "NewStructExpr(\n"
      "+-type: STRUCT<a INT64, b STRING>,\n"
      "+-0 a: ConstExpr(1),\n"
      "+-1 b: ConstExpr(\"foo\"))",
      struct_op->DebugString());
  Value result = Struct({"a", "b"}, {Int64(1), String("foo")});
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(auto value, EvalExpr(*struct_op, EmptyParams()));
  EXPECT_TRUE(result.Equals(value));

  // Test that we can't create a struct that is too large.
  EvaluationOptions value_size_options;
  value_size_options.max_value_byte_size = 1;
  EvaluationContext value_size_context(value_size_options);
  EXPECT_THAT(EvalExpr(*struct_op, EmptyParams(), &value_size_context),
              StatusIs(absl::StatusCode::kOutOfRange,
                       HasSubstr("Cannot construct struct Value larger than")));
}

TEST_F(EvalTest, NewArrayExpr) {
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(auto one_expr, ConstExpr::Create(Int64(1)));
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(auto null_expr, ConstExpr::Create(NullInt64()));

  std::vector<std::unique_ptr<ValueExpr>> args;
  args.push_back(std::move(one_expr));
  args.push_back(std::move(null_expr));

  GOOGLESQL_ASSERT_OK_AND_ASSIGN(auto array_op,
                       NewArrayExpr::Create(Int64ArrayType(), std::move(args)));
  EXPECT_EQ(
      "NewArrayExpr(\n"
      "  type: ARRAY<INT64>,\n"
      "  ConstExpr(Int64(1)),\n"
      "  ConstExpr(Int64(NULL)))",
      array_op->DebugString(true));
  EXPECT_EQ("NewArrayExpr(ConstExpr(1), ConstExpr(NULL))",
            array_op->DebugString());
  Value expected_result = Array({Int64(1), NullInt64()});

  EvaluationContext context((EvaluationOptions()));
  TupleSlot slot;
  absl::Status status;
  ASSERT_TRUE(array_op->EvalSimple(EmptyParams(), &context, &slot, &status))
      << status;
  EXPECT_EQ(expected_result, slot.value());
  EXPECT_EQ(slot.mutable_shared_proto_state()->get(), nullptr);

  // Test that we can't create an array that is too large.
  EvaluationOptions value_size_options;
  value_size_options.max_value_byte_size = 1;
  EvaluationContext value_size_context(value_size_options);
  EXPECT_FALSE(
      array_op->EvalSimple(EmptyParams(), &value_size_context, &slot, &status));
  EXPECT_THAT(status,
              StatusIs(absl::StatusCode::kOutOfRange,
                       HasSubstr("Cannot construct array Value larger than")));
}

SimpleGraphNodeTable MakeSimpleGraphNodeTable() {
  return SimpleGraphNodeTable(
      "node_table1", std::vector<std::string>{"graph0"},
      /*input_table=*/nullptr, /*key_cols=*/{},
      /*labels=*/absl::flat_hash_set<const GraphElementLabel*>(),
      /*property_definitions=*/{});
}

SimpleGraphEdgeTable MakeSimpleGraphEdgeTable(
    const GraphNodeTable* source_node_table,
    const GraphNodeTable* dest_node_table) {
  return SimpleGraphEdgeTable(
      "edge_table1", std::vector<std::string>{"graph0"},
      /*input_table=*/nullptr, /*key_cols=*/{},
      /*labels=*/absl::flat_hash_set<const GraphElementLabel*>(),
      /*property_definitions=*/{},
      std::make_unique<const SimpleGraphNodeTableReference>(
          source_node_table, /*edge_table_columns=*/std::vector<int>{},
          /*node_table_columns=*/std::vector<int>{}),
      std::make_unique<const SimpleGraphNodeTableReference>(
          dest_node_table, /*edge_table_columns=*/std::vector<int>{},
          /*node_table_columns=*/std::vector<int>{}));
}

absl::StatusOr<std::vector<std::unique_ptr<ValueExpr>>> MakeValueExprs(
    absl::Span<const Value> values) {
  std::vector<std::unique_ptr<ValueExpr>> exprs;
  exprs.reserve(values.size());
  for (const auto& val : values) {
    GOOGLESQL_ASSIGN_OR_RETURN(auto expr, ConstExpr::Create(val));
    exprs.push_back(std::move(expr));
  }
  return exprs;
}

absl::StatusOr<std::unique_ptr<NewGraphElementExpr>> MakeGraphElementOp(
    std::vector<Value> key_vals, const GraphElementTable* element_table,
    std::vector<Value> src_node_key_vals = {},
    std::vector<Value> dst_node_key_vals = {}) {
  std::vector<PropertyType> property_types;
  property_types.emplace_back("a", Int64Type());
  property_types.emplace_back("b", StringType());
  property_types.emplace_back("c", DateType());

  GOOGLESQL_ASSIGN_OR_RETURN(auto one_expr, ConstExpr::Create(Int64(1)));
  GOOGLESQL_ASSIGN_OR_RETURN(auto foo_expr, ConstExpr::Create(String("foo")));
  GOOGLESQL_ASSIGN_OR_RETURN(auto null_date_expr, ConstExpr::Create(Value::NullDate()));

  std::vector<NewGraphElementExpr::Property> properties;
  properties.push_back({.name = "a", .definition = std::move(one_expr)});
  properties.push_back({.name = "b", .definition = std::move(foo_expr)});
  properties.push_back({.name = "c", .definition = std::move(null_date_expr)});

  GOOGLESQL_ASSIGN_OR_RETURN(std::vector<std::unique_ptr<ValueExpr>> key_exprs,
                   MakeValueExprs(key_vals));
  std::vector<std::unique_ptr<ValueExpr>> src_node_key_exprs,
      dst_node_key_exprs;

  if (!src_node_key_vals.empty()) {
    GOOGLESQL_ASSIGN_OR_RETURN(src_node_key_exprs, MakeValueExprs(src_node_key_vals));
  }
  if (!dst_node_key_vals.empty()) {
    GOOGLESQL_ASSIGN_OR_RETURN(dst_node_key_exprs, MakeValueExprs(dst_node_key_vals));
  }

  GOOGLESQL_ASSIGN_OR_RETURN(
      std::unique_ptr<NewGraphElementExpr> new_graph_element_op,
      NewGraphElementExpr::Create(
          MakeGraphElementType({"graph0"},
                               !src_node_key_vals.empty()
                                   ? GraphElementType::ElementKind::kEdge
                                   : GraphElementType::ElementKind::kNode,
                               property_types),
          element_table, std::move(key_exprs), std::move(properties),
          /*dynamic_property_expr=*/nullptr,
          /*dynamic_label_expr=*/nullptr,
          std::move(src_node_key_exprs), std::move(dst_node_key_exprs)));
  return new_graph_element_op;
}

TEST_F(EvalTest, NewGraphNodeExpr) {
  SimpleGraphNodeTable element_table = MakeSimpleGraphNodeTable();
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(
      std::unique_ptr<NewGraphElementExpr> new_graph_element_op,
      MakeGraphElementOp({Value::Int64(1), Value::String("key")},
                         &element_table));

  GOOGLESQL_ASSERT_OK(
      new_graph_element_op->SetSchemasForEvaluation(EmptyParamsSchemas()));
  EXPECT_EQ(new_graph_element_op->DebugString(),
            "NewGraphElementExpr(\n"
            "  +-type: GRAPH_NODE(graph0)<a INT64, b STRING, c DATE>\n"
            "  +-table: graph0.node_table1\n"
            "  +-key: (\n"
            "  +- ConstExpr(1),\n"
            "  +- ConstExpr(\"key\"),\n"
            "  +-)\n"
            "  +-static_properties: (\n"
            "  +- a: ConstExpr(1),\n"
            "  +- b: ConstExpr(\"foo\"),\n"
            "  +- c: ConstExpr(NULL),\n"
            "  +-)");

  GOOGLESQL_ASSERT_OK_AND_ASSIGN(Value graph_element_value,
                       EvalExpr(*new_graph_element_op, EmptyParams()));
  EXPECT_FALSE(graph_element_value.is_null());
  EXPECT_EQ(graph_element_value.DebugString(), "{a:1, b:\"foo\", c:NULL}");
  EXPECT_EQ(graph_element_value.type(), new_graph_element_op->output_type());
  EXPECT_EQ(graph_element_value.GetIdentifier(),
            "[STRING]graph0.node_table1[INT64]1[STRING]key");

  // Test that we can't create an element that is too large.
  EvaluationOptions value_size_options;
  value_size_options.max_value_byte_size = 1;
  EvaluationContext value_size_context(value_size_options);
  EXPECT_THAT(
      EvalExpr(*new_graph_element_op, EmptyParams(), &value_size_context),
      StatusIs(absl::StatusCode::kOutOfRange,
               HasSubstr("Cannot construct graph element Value larger than ")));
}

TEST_F(EvalTest, NewGraphEdgeExpr) {
  SimpleGraphNodeTable node_table = MakeSimpleGraphNodeTable();
  SimpleGraphEdgeTable edge_table =
      MakeSimpleGraphEdgeTable(&node_table, &node_table);
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(
      std::unique_ptr<NewGraphElementExpr> edge_op,
      MakeGraphElementOp({Value::Int64(1), Value::String("ek")}, &edge_table,
                         {Value::Int64(2), Value::String("nk2")},
                         {Value::Int64(3), Value::String("nk3")}));

  GOOGLESQL_ASSERT_OK(edge_op->SetSchemasForEvaluation(EmptyParamsSchemas()));
  EXPECT_EQ(edge_op->DebugString(),
            "NewGraphElementExpr(\n"
            "  +-type: GRAPH_EDGE(graph0)<a INT64, b STRING, c DATE>\n"
            "  +-table: graph0.edge_table1\n"
            "  +-key: (\n"
            "  +- ConstExpr(1),\n"
            "  +- ConstExpr(\"ek\"),\n"
            "  +-)\n"
            "  +-src_node_key: (\n"
            "  +- ConstExpr(2),\n"
            "  +- ConstExpr(\"nk2\"),\n"
            "  +-)\n"
            "  +-dst_node_key: (\n"
            "  +- ConstExpr(3),\n"
            "  +- ConstExpr(\"nk3\"),\n"
            "  +-)\n"
            "  +-static_properties: (\n"
            "  +- a: ConstExpr(1),\n"
            "  +- b: ConstExpr(\"foo\"),\n"
            "  +- c: ConstExpr(NULL),\n"
            "  +-)");

  GOOGLESQL_ASSERT_OK_AND_ASSIGN(Value edge_value, EvalExpr(*edge_op, EmptyParams()));
  EXPECT_FALSE(edge_value.is_null());
  EXPECT_EQ(edge_value.DebugString(), "{a:1, b:\"foo\", c:NULL}");
  EXPECT_EQ(edge_value.type(), edge_op->output_type());

  GOOGLESQL_ASSERT_OK_AND_ASSIGN(
      std::unique_ptr<NewGraphElementExpr> src_node_op,
      MakeGraphElementOp({Value::Int64(2), Value::String("nk2")}, &node_table));
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(Value src_node_value,
                       EvalExpr(*src_node_op, EmptyParams()));

  GOOGLESQL_ASSERT_OK_AND_ASSIGN(
      std::unique_ptr<NewGraphElementExpr> reverse_edge_op,
      MakeGraphElementOp({Value::Int64(1), Value::String("key")}, &edge_table,
                         {Value::Int64(3), Value::String("nk3")},
                         {Value::Int64(2), Value::String("nk2")}));
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(Value reverse_edge_value,
                       EvalExpr(*reverse_edge_op, EmptyParams()));
  EXPECT_EQ(edge_value.GetIdentifier(),
            "[STRING]graph0.edge_table1[INT64]1[STRING]ek[INT64]2[STRING]nk2["
            "INT64]3[STRING]nk3");
  EXPECT_NE(edge_value.GetIdentifier(), reverse_edge_value.GetIdentifier());
  EXPECT_EQ(edge_value.GetSourceNodeIdentifier(),
            src_node_value.GetIdentifier());
  EXPECT_NE(edge_value.GetDestNodeIdentifier(), src_node_value.GetIdentifier());
  EXPECT_EQ(reverse_edge_value.GetDestNodeIdentifier(),
            src_node_value.GetIdentifier());
  EXPECT_NE(reverse_edge_value.GetSourceNodeIdentifier(),
            src_node_value.GetIdentifier());
}

enum class DynamicLabelValueKindInTest {
  kStatic,
  kSingle,
  kSingleEmptyValue,
  kArrayWithNullValue,
  kEmptyArray,
  kNullArray,
  kArrayWithEmptyValue,
};

absl::StatusOr<std::unique_ptr<NewGraphElementExpr>> MakeDynamicGraphElementOp(
    absl::Span<const Value> key_vals, const GraphElementTable* element_table,
    DynamicLabelValueKindInTest dynamic_label_kind,
    std::vector<Value::Property>& dynamic_properties,
    std::vector<Value> src_node_key_vals = {},
    std::vector<Value> dst_node_key_vals = {}) {
  std::vector<PropertyType> property_types;
  property_types.emplace_back("a", Int64Type());
  property_types.emplace_back("b", StringType());
  property_types.emplace_back("c", DateType());

  GOOGLESQL_ASSIGN_OR_RETURN(auto one_expr, ConstExpr::Create(Int64(1)));
  GOOGLESQL_ASSIGN_OR_RETURN(auto foo_expr, ConstExpr::Create(String("foo")));
  GOOGLESQL_ASSIGN_OR_RETURN(auto null_date_expr, ConstExpr::Create(Value::NullDate()));

  std::vector<NewGraphElementExpr::Property> static_properties;
  static_properties.push_back({.name = "a", .definition = std::move(one_expr)});
  static_properties.push_back({.name = "b", .definition = std::move(foo_expr)});
  static_properties.push_back(
      {.name = "c", .definition = std::move(null_date_expr)});

  GOOGLESQL_ASSIGN_OR_RETURN(std::vector<std::unique_ptr<ValueExpr>> key_exprs,
                   MakeValueExprs(key_vals));
  std::vector<std::unique_ptr<ValueExpr>> src_node_key_exprs,
      dst_node_key_exprs;

  if (!src_node_key_vals.empty()) {
    GOOGLESQL_ASSIGN_OR_RETURN(src_node_key_exprs, MakeValueExprs(src_node_key_vals));
  }
  if (!dst_node_key_vals.empty()) {
    GOOGLESQL_ASSIGN_OR_RETURN(dst_node_key_exprs, MakeValueExprs(dst_node_key_vals));
  }

  GOOGLESQL_ASSIGN_OR_RETURN(JSONValue json_value,
                   MakePropertiesJsonValue(absl::MakeSpan(dynamic_properties),
                                           LanguageOptions::MaximumFeatures()));
  GOOGLESQL_ASSIGN_OR_RETURN(std::unique_ptr<ValueExpr> dynamic_property_expr,
                   ConstExpr::Create(Json(std::move(json_value))));
  std::unique_ptr<ValueExpr> dynamic_label_expr;
  if (dynamic_label_kind == DynamicLabelValueKindInTest::kSingle) {
    GOOGLESQL_ASSIGN_OR_RETURN(dynamic_label_expr,
                     ConstExpr::Create(String("dynamic_label1")));
  } else if (dynamic_label_kind ==
             DynamicLabelValueKindInTest::kSingleEmptyValue) {
    GOOGLESQL_ASSIGN_OR_RETURN(dynamic_label_expr, ConstExpr::Create(String("")));
  } else if (dynamic_label_kind ==
             DynamicLabelValueKindInTest::kArrayWithNullValue) {
    std::vector<Value> value_vector = {String("dynamic_label1"),
                                       String("dynamic_label2"),
                                       Value::NullString()};
    GOOGLESQL_ASSIGN_OR_RETURN(dynamic_label_expr, ConstExpr::Create(Value::Array(
                                             StringArrayType(), value_vector)));
  } else if (dynamic_label_kind == DynamicLabelValueKindInTest::kEmptyArray) {
    GOOGLESQL_ASSIGN_OR_RETURN(dynamic_label_expr,
                     ConstExpr::Create(Value::EmptyArray(StringArrayType())));
  } else if (dynamic_label_kind == DynamicLabelValueKindInTest::kNullArray) {
    GOOGLESQL_ASSIGN_OR_RETURN(dynamic_label_expr,
                     ConstExpr::Create(Value::Null(StringArrayType())));
  } else if (dynamic_label_kind ==
             DynamicLabelValueKindInTest::kArrayWithEmptyValue) {
    std::vector<Value> value_vector = {String("dynamic_label1"),
                                       String("dynamic_label2"), String("")};
    GOOGLESQL_ASSIGN_OR_RETURN(dynamic_label_expr, ConstExpr::Create(Value::Array(
                                             StringArrayType(), value_vector)));
  }

  GOOGLESQL_ASSIGN_OR_RETURN(
      std::unique_ptr<NewGraphElementExpr> new_graph_element_op,
      NewGraphElementExpr::Create(
          MakeDynamicGraphElementType(
              {"graph0"},
              !src_node_key_vals.empty() ? GraphElementType::ElementKind::kEdge
                                         : GraphElementType::ElementKind::kNode,
              property_types),
          element_table, std::move(key_exprs), std::move(static_properties),
          std::move(dynamic_property_expr), std::move(dynamic_label_expr),
          std::move(src_node_key_exprs), std::move(dst_node_key_exprs)));
  return new_graph_element_op;
}

TEST_F(EvalTest, DynamicNewGraphNodeExpr) {
  SimpleGraphNodeTable element_table = MakeSimpleGraphNodeTable();
  std::vector<Value> keys = {Value::Int64(1), Value::String("key")};
  Value p1_value = Value::Bool(true);
  Value p2_value = Value::Double(3.14);
  std::vector<Value::Property> dynamic_properties = {{"p1", p1_value},
                                                     {"p2", p2_value}};
  JSONValue p1_json(p1_value.bool_value());
  JSONValue p2_json(p2_value.double_value());
  const Value p1_json_value = Value::Json(std::move(p1_json));
  const Value p2_json_value = Value::Json(std::move(p2_json));

  GOOGLESQL_ASSERT_OK_AND_ASSIGN(
      std::unique_ptr<NewGraphElementExpr> new_graph_element_op1,
      MakeDynamicGraphElementOp(keys, &element_table,
                                DynamicLabelValueKindInTest::kSingle,
                                dynamic_properties));
  GOOGLESQL_ASSERT_OK(
      new_graph_element_op1->SetSchemasForEvaluation(EmptyParamsSchemas()));
  EXPECT_EQ(
      "NewGraphElementExpr(\n"
      "  +-type: GRAPH_NODE(graph0)<a INT64, b STRING, c DATE, DYNAMIC>\n"
      "  +-table: graph0.node_table1\n"
      "  +-key: (\n"
      "  +- ConstExpr(1),\n"
      "  +- ConstExpr(\"key\"),\n"
      "  +-)\n"
      "  +-static_properties: (\n"
      "  +- a: ConstExpr(1),\n"
      "  +- b: ConstExpr(\"foo\"),\n"
      "  +- c: ConstExpr(NULL),\n"
      "  +-)\n"
      "  +-dynamic_property: ConstExpr({\"p1\":true,\"p2\":3.14})\n"
      "  +-dynamic_label: ConstExpr(\"dynamic_label1\")",
      new_graph_element_op1->DebugString());

  GOOGLESQL_ASSERT_OK_AND_ASSIGN(Value graph_element_value1,
                       EvalExpr(*new_graph_element_op1, EmptyParams()));
  EXPECT_EQ(graph_element_value1.DebugString(), "{a:1, b:\"foo\", c:NULL}");
  EXPECT_THAT(graph_element_value1.GetLabels(), ElementsAre("dynamic_label1"));
  EXPECT_THAT(graph_element_value1.FindPropertyByName("p1"),
              IsOkAndHolds(Eq(p1_json_value)));
  EXPECT_THAT(graph_element_value1.FindPropertyByName("p2"),
              IsOkAndHolds(Eq(p2_json_value)));

  GOOGLESQL_ASSERT_OK_AND_ASSIGN(
      std::unique_ptr<NewGraphElementExpr> new_graph_element_op2,
      MakeDynamicGraphElementOp(keys, &element_table,
                                DynamicLabelValueKindInTest::kStatic,
                                dynamic_properties));
  GOOGLESQL_ASSERT_OK(
      new_graph_element_op2->SetSchemasForEvaluation(EmptyParamsSchemas()));
  EXPECT_EQ(
      "NewGraphElementExpr(\n"
      "  +-type: GRAPH_NODE(graph0)<a INT64, b STRING, c DATE, DYNAMIC>\n"
      "  +-table: graph0.node_table1\n"
      "  +-key: (\n"
      "  +- ConstExpr(1),\n"
      "  +- ConstExpr(\"key\"),\n"
      "  +-)\n"
      "  +-static_properties: (\n"
      "  +- a: ConstExpr(1),\n"
      "  +- b: ConstExpr(\"foo\"),\n"
      "  +- c: ConstExpr(NULL),\n"
      "  +-)\n"
      "  +-dynamic_property: ConstExpr({\"p1\":true,\"p2\":3.14})",
      new_graph_element_op2->DebugString());

  GOOGLESQL_ASSERT_OK_AND_ASSIGN(Value graph_element_value2,
                       EvalExpr(*new_graph_element_op2, EmptyParams()));
  EXPECT_EQ(graph_element_value2.DebugString(), "{a:1, b:\"foo\", c:NULL}");
  EXPECT_THAT(graph_element_value2.GetLabels(), ::testing::IsEmpty());

  GOOGLESQL_ASSERT_OK_AND_ASSIGN(
      std::unique_ptr<NewGraphElementExpr> new_graph_element_op3,
      MakeDynamicGraphElementOp(
          keys, &element_table,
          DynamicLabelValueKindInTest::kArrayWithNullValue,
          dynamic_properties));
  GOOGLESQL_ASSERT_OK(
      new_graph_element_op3->SetSchemasForEvaluation(EmptyParamsSchemas()));
  EXPECT_EQ(
      "NewGraphElementExpr(\n"
      "  +-type: GRAPH_NODE(graph0)<a INT64, b STRING, c DATE, DYNAMIC>\n"
      "  +-table: graph0.node_table1\n"
      "  +-key: (\n"
      "  +- ConstExpr(1),\n"
      "  +- ConstExpr(\"key\"),\n"
      "  +-)\n"
      "  +-static_properties: (\n"
      "  +- a: ConstExpr(1),\n"
      "  +- b: ConstExpr(\"foo\"),\n"
      "  +- c: ConstExpr(NULL),\n"
      "  +-)\n"
      "  +-dynamic_property: ConstExpr({\"p1\":true,\"p2\":3.14})\n"
      "  +-dynamic_label: ConstExpr([\"dynamic_label1\", \"dynamic_label2\", "
      "NULL])",
      new_graph_element_op3->DebugString());

  GOOGLESQL_ASSERT_OK_AND_ASSIGN(Value graph_element_value3,
                       EvalExpr(*new_graph_element_op3, EmptyParams()));
  EXPECT_EQ(graph_element_value3.DebugString(), "{a:1, b:\"foo\", c:NULL}");
  // The NULL label is ignored after eval.
  EXPECT_THAT(graph_element_value3.GetLabels(),
              ElementsAre("dynamic_label1", "dynamic_label2"));
  EXPECT_THAT(graph_element_value3.FindPropertyByName("p1"),
              IsOkAndHolds(Eq(p1_json_value)));
  EXPECT_THAT(graph_element_value3.FindPropertyByName("p2"),
              IsOkAndHolds(Eq(p2_json_value)));

  GOOGLESQL_ASSERT_OK_AND_ASSIGN(
      std::unique_ptr<NewGraphElementExpr> new_graph_element_op4,
      MakeDynamicGraphElementOp(keys, &element_table,
                                DynamicLabelValueKindInTest::kEmptyArray,
                                dynamic_properties));
  GOOGLESQL_ASSERT_OK(
      new_graph_element_op4->SetSchemasForEvaluation(EmptyParamsSchemas()));
  EXPECT_EQ(new_graph_element_op4->DebugString(),
            "NewGraphElementExpr(\n"
            "  +-type: GRAPH_NODE(graph0)<a INT64, b STRING, c DATE, "
            "DYNAMIC>\n"
            "  +-table: graph0.node_table1\n"
            "  +-key: (\n"
            "  +- ConstExpr(1),\n"
            "  +- ConstExpr(\"key\"),\n"
            "  +-)\n"
            "  +-static_properties: (\n"
            "  +- a: ConstExpr(1),\n"
            "  +- b: ConstExpr(\"foo\"),\n"
            "  +- c: ConstExpr(NULL),\n"
            "  +-)\n"
            "  +-dynamic_property: ConstExpr({\"p1\":true,\"p2\":3.14})\n"
            "  +-dynamic_label: ConstExpr([])");
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(Value graph_element_value4,
                       EvalExpr(*new_graph_element_op4, EmptyParams()));
  EXPECT_EQ(graph_element_value4.DebugString(), "{a:1, b:\"foo\", c:NULL}");
  EXPECT_THAT(graph_element_value4.GetLabels(), testing::IsEmpty());
  EXPECT_THAT(graph_element_value4.FindPropertyByName("p1"),
              IsOkAndHolds(Eq(p1_json_value)));
  EXPECT_THAT(graph_element_value4.FindPropertyByName("p2"),
              IsOkAndHolds(Eq(p2_json_value)));

  GOOGLESQL_ASSERT_OK_AND_ASSIGN(
      std::unique_ptr<NewGraphElementExpr> new_graph_element_op5,
      MakeDynamicGraphElementOp(keys, &element_table,
                                DynamicLabelValueKindInTest::kNullArray,
                                dynamic_properties));
  GOOGLESQL_ASSERT_OK(
      new_graph_element_op5->SetSchemasForEvaluation(EmptyParamsSchemas()));
  EXPECT_EQ(new_graph_element_op5->DebugString(),
            "NewGraphElementExpr(\n"
            "  +-type: GRAPH_NODE(graph0)<a INT64, b STRING, c DATE, "
            "DYNAMIC>\n"
            "  +-table: graph0.node_table1\n"
            "  +-key: (\n"
            "  +- ConstExpr(1),\n"
            "  +- ConstExpr(\"key\"),\n"
            "  +-)\n"
            "  +-static_properties: (\n"
            "  +- a: ConstExpr(1),\n"
            "  +- b: ConstExpr(\"foo\"),\n"
            "  +- c: ConstExpr(NULL),\n"
            "  +-)\n"
            "  +-dynamic_property: ConstExpr({\"p1\":true,\"p2\":3.14})\n"
            "  +-dynamic_label: ConstExpr(NULL)");
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(Value graph_element_value5,
                       EvalExpr(*new_graph_element_op5, EmptyParams()));
  EXPECT_EQ(graph_element_value5.DebugString(), "{a:1, b:\"foo\", c:NULL}");
  EXPECT_THAT(graph_element_value5.GetLabels(), testing::IsEmpty());
  EXPECT_THAT(graph_element_value5.FindPropertyByName("p1"),
              IsOkAndHolds(Eq(p1_json_value)));
  EXPECT_THAT(graph_element_value5.FindPropertyByName("p2"),
              IsOkAndHolds(Eq(p2_json_value)));

  GOOGLESQL_ASSERT_OK_AND_ASSIGN(
      std::unique_ptr<NewGraphElementExpr> new_graph_element_op6,
      MakeDynamicGraphElementOp(keys, &element_table,
                                DynamicLabelValueKindInTest::kSingleEmptyValue,
                                dynamic_properties));
  GOOGLESQL_ASSERT_OK(
      new_graph_element_op6->SetSchemasForEvaluation(EmptyParamsSchemas()));
  EXPECT_EQ(
      "NewGraphElementExpr(\n"
      "  +-type: GRAPH_NODE(graph0)<a INT64, b STRING, c DATE, DYNAMIC>\n"
      "  +-table: graph0.node_table1\n"
      "  +-key: (\n"
      "  +- ConstExpr(1),\n"
      "  +- ConstExpr(\"key\"),\n"
      "  +-)\n"
      "  +-static_properties: (\n"
      "  +- a: ConstExpr(1),\n"
      "  +- b: ConstExpr(\"foo\"),\n"
      "  +- c: ConstExpr(NULL),\n"
      "  +-)\n"
      "  +-dynamic_property: ConstExpr({\"p1\":true,\"p2\":3.14})\n"
      "  +-dynamic_label: ConstExpr(\"\")",
      new_graph_element_op6->DebugString());

  GOOGLESQL_ASSERT_OK_AND_ASSIGN(Value graph_element_value6,
                       EvalExpr(*new_graph_element_op6, EmptyParams()));
  EXPECT_EQ(graph_element_value6.DebugString(), "{a:1, b:\"foo\", c:NULL}");
  EXPECT_TRUE(graph_element_value6.GetLabels().empty());
  EXPECT_THAT(graph_element_value6.FindPropertyByName("p1"),
              IsOkAndHolds(Eq(p1_json_value)));
  EXPECT_THAT(graph_element_value6.FindPropertyByName("p2"),
              IsOkAndHolds(Eq(p2_json_value)));

  GOOGLESQL_ASSERT_OK_AND_ASSIGN(
      std::unique_ptr<NewGraphElementExpr> new_graph_element_op7,
      MakeDynamicGraphElementOp(
          keys, &element_table,
          DynamicLabelValueKindInTest::kArrayWithEmptyValue,
          dynamic_properties));

  GOOGLESQL_ASSERT_OK(
      new_graph_element_op7->SetSchemasForEvaluation(EmptyParamsSchemas()));
  EXPECT_EQ(
      "NewGraphElementExpr(\n"
      "  +-type: GRAPH_NODE(graph0)<a INT64, b STRING, c DATE, DYNAMIC>\n"
      "  +-table: graph0.node_table1\n"
      "  +-key: (\n"
      "  +- ConstExpr(1),\n"
      "  +- ConstExpr(\"key\"),\n"
      "  +-)\n"
      "  +-static_properties: (\n"
      "  +- a: ConstExpr(1),\n"
      "  +- b: ConstExpr(\"foo\"),\n"
      "  +- c: ConstExpr(NULL),\n"
      "  +-)\n"
      "  +-dynamic_property: ConstExpr({\"p1\":true,\"p2\":3.14})\n"
      "  +-dynamic_label: ConstExpr([\"dynamic_label1\", \"dynamic_label2\", "
      "\"\"])",
      new_graph_element_op7->DebugString());

  GOOGLESQL_ASSERT_OK_AND_ASSIGN(Value graph_element_value7,
                       EvalExpr(*new_graph_element_op7, EmptyParams()));
  EXPECT_EQ(graph_element_value7.DebugString(), "{a:1, b:\"foo\", c:NULL}");
  EXPECT_THAT(graph_element_value7.GetLabels(),
              ElementsAre("dynamic_label1", "dynamic_label2"));
  EXPECT_THAT(graph_element_value7.FindPropertyByName("p1"),
              IsOkAndHolds(Eq(p1_json_value)));
  EXPECT_THAT(graph_element_value7.FindPropertyByName("p2"),
              IsOkAndHolds(Eq(p2_json_value)));
}

TEST_F(EvalTest, DynamicNewGraphEdgeExpr) {
  SimpleGraphNodeTable node_table = MakeSimpleGraphNodeTable();
  SimpleGraphEdgeTable edge_table =
      MakeSimpleGraphEdgeTable(&node_table, &node_table);
  std::vector<Value> keys = {Value::Int64(1), Value::String("ek")};
  std::vector<Value> src_node_keys = {Value::Int64(2), Value::String("nk2")};
  std::vector<Value> dst_node_keys = {Value::Int64(3), Value::String("nk3")};
  Value p1_value = Value::Bool(true);
  Value p2_value = Value::Double(3.14);
  std::vector<Value::Property> dynamic_properties = {{"p1", p1_value},
                                                     {"p2", p2_value}};
  JSONValue p1_json(p1_value.bool_value());
  JSONValue p2_json(p2_value.double_value());
  const Value p1_json_value = Value::Json(std::move(p1_json));
  const Value p2_json_value = Value::Json(std::move(p2_json));

  GOOGLESQL_ASSERT_OK_AND_ASSIGN(
      std::unique_ptr<NewGraphElementExpr> new_graph_element_op1,
      MakeDynamicGraphElementOp(
          keys, &edge_table, DynamicLabelValueKindInTest::kSingle,
          dynamic_properties, src_node_keys, dst_node_keys));
  GOOGLESQL_ASSERT_OK(
      new_graph_element_op1->SetSchemasForEvaluation(EmptyParamsSchemas()));
  EXPECT_EQ(
      "NewGraphElementExpr(\n"
      "  +-type: GRAPH_EDGE(graph0)<a INT64, b STRING, c DATE, DYNAMIC>\n"
      "  +-table: graph0.edge_table1\n"
      "  +-key: (\n"
      "  +- ConstExpr(1),\n"
      "  +- ConstExpr(\"ek\"),\n"
      "  +-)\n"
      "  +-src_node_key: (\n"
      "  +- ConstExpr(2),\n"
      "  +- ConstExpr(\"nk2\"),\n"
      "  +-)\n"
      "  +-dst_node_key: (\n"
      "  +- ConstExpr(3),\n"
      "  +- ConstExpr(\"nk3\"),\n"
      "  +-)\n"
      "  +-static_properties: (\n"
      "  +- a: ConstExpr(1),\n"
      "  +- b: ConstExpr(\"foo\"),\n"
      "  +- c: ConstExpr(NULL),\n"
      "  +-)\n"
      "  +-dynamic_property: ConstExpr({\"p1\":true,\"p2\":3.14})\n"
      "  +-dynamic_label: ConstExpr(\"dynamic_label1\")",
      new_graph_element_op1->DebugString());

  GOOGLESQL_ASSERT_OK_AND_ASSIGN(Value graph_element_value1,
                       EvalExpr(*new_graph_element_op1, EmptyParams()));
  EXPECT_EQ(graph_element_value1.DebugString(), "{a:1, b:\"foo\", c:NULL}");
  EXPECT_THAT(graph_element_value1.GetLabels(), ElementsAre("dynamic_label1"));
  EXPECT_THAT(graph_element_value1.FindPropertyByName("p1"),
              IsOkAndHolds(Eq(p1_json_value)));
  EXPECT_THAT(graph_element_value1.FindPropertyByName("p2"),
              IsOkAndHolds(Eq(p2_json_value)));

  GOOGLESQL_ASSERT_OK_AND_ASSIGN(
      std::unique_ptr<NewGraphElementExpr> new_graph_element_op2,
      MakeDynamicGraphElementOp(
          keys, &edge_table, DynamicLabelValueKindInTest::kStatic,
          dynamic_properties, src_node_keys, dst_node_keys));
  GOOGLESQL_ASSERT_OK(
      new_graph_element_op2->SetSchemasForEvaluation(EmptyParamsSchemas()));
  EXPECT_EQ(
      "NewGraphElementExpr(\n"
      "  +-type: GRAPH_EDGE(graph0)<a INT64, b STRING, c DATE, DYNAMIC>\n"
      "  +-table: graph0.edge_table1\n"
      "  +-key: (\n"
      "  +- ConstExpr(1),\n"
      "  +- ConstExpr(\"ek\"),\n"
      "  +-)\n"
      "  +-src_node_key: (\n"
      "  +- ConstExpr(2),\n"
      "  +- ConstExpr(\"nk2\"),\n"
      "  +-)\n"
      "  +-dst_node_key: (\n"
      "  +- ConstExpr(3),\n"
      "  +- ConstExpr(\"nk3\"),\n"
      "  +-)\n"
      "  +-static_properties: (\n"
      "  +- a: ConstExpr(1),\n"
      "  +- b: ConstExpr(\"foo\"),\n"
      "  +- c: ConstExpr(NULL),\n"
      "  +-)\n"
      "  +-dynamic_property: ConstExpr({\"p1\":true,\"p2\":3.14})",
      new_graph_element_op2->DebugString());

  GOOGLESQL_ASSERT_OK_AND_ASSIGN(Value graph_element_value2,
                       EvalExpr(*new_graph_element_op2, EmptyParams()));
  EXPECT_EQ(graph_element_value2.DebugString(), "{a:1, b:\"foo\", c:NULL}");
  EXPECT_THAT(graph_element_value2.GetLabels(), ::testing::IsEmpty());
}

TEST_F(EvalTest, GraphElementPropertyAccessTest) {
  SimpleGraphNodeTable element_table = MakeSimpleGraphNodeTable();
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(
      std::unique_ptr<NewGraphElementExpr> new_graph_element_op,
      MakeGraphElementOp({Value::Int64(1)}, &element_table));

  // Attempt a property access on the new element.
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(std::unique_ptr<GraphGetElementPropertyExpr>
                           graph_element_property_access_op,
                       GraphGetElementPropertyExpr::Create(
                           StringType(), "b", std::move(new_graph_element_op)));

  EXPECT_EQ(graph_element_property_access_op->DebugString(),
            "GraphGetElementPropertyExpr(b, NewGraphElementExpr(\n"
            "  +-type: GRAPH_NODE(graph0)<a INT64, b STRING, c DATE>\n"
            "  +-table: graph0.node_table1\n"
            "  +-key: (\n"
            "  +- ConstExpr(1),\n"
            "  +-)\n"
            "  +-static_properties: (\n"
            "  +- a: ConstExpr(1),\n"
            "  +- b: ConstExpr(\"foo\"),\n"
            "  +- c: ConstExpr(NULL),\n"
            "  +-))");

  GOOGLESQL_ASSERT_OK_AND_ASSIGN(
      Value property_access_value,
      EvalExpr(*graph_element_property_access_op, EmptyParams()));
  EXPECT_EQ(property_access_value.DebugString(), "\"foo\"");
  EXPECT_TRUE(property_access_value.Equals(String("foo")));
}

TEST_F(EvalTest, NullGraphElementPropertyAccessTest) {
  SimpleGraphNodeTable element_table = MakeSimpleGraphNodeTable();
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(
      std::unique_ptr<NewGraphElementExpr> new_graph_element_op,
      MakeGraphElementOp({Value::Int64(1)}, &element_table));

  GOOGLESQL_ASSERT_OK_AND_ASSIGN(
      std::unique_ptr<ConstExpr> null_graph_element_op,
      ConstExpr::Create(Value::Null(new_graph_element_op->output_type())));

  GOOGLESQL_ASSERT_OK_AND_ASSIGN(
      std::unique_ptr<GraphGetElementPropertyExpr>
          graph_element_property_access_op,
      GraphGetElementPropertyExpr::Create(StringType(), "b",
                                          std::move(null_graph_element_op)));

  EXPECT_EQ(graph_element_property_access_op->DebugString(),
            "GraphGetElementPropertyExpr(b, ConstExpr(NULL))");

  GOOGLESQL_ASSERT_OK_AND_ASSIGN(
      Value property_access_value,
      EvalExpr(*graph_element_property_access_op, EmptyParams()));
  EXPECT_TRUE(property_access_value.is_null());
  EXPECT_TRUE(property_access_value.Equals(Value::NullString()));
}

TEST_F(EvalTest, FieldValueExpr) {
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(auto struct_expr,
                       ConstExpr::Create(Struct(
                           {{"foo", GetProtoValue(1)}, {"bar", Int64(0)}})));
  const TupleSlot& slot = struct_expr->slot_test_only();
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(auto field_op,
                       FieldValueExpr::Create(0, std::move(struct_expr)));
  EXPECT_EQ(
      "FieldValueExpr(0:foo, "
      "ConstExpr({foo:{int64_key_1: 1 int64_key_2: 10}, bar:0}))",
      field_op->DebugString());

  EvaluationContext context((EvaluationOptions()));
  TupleSlot result;
  absl::Status status;
  ASSERT_TRUE(field_op->EvalSimple(EmptyParams(), &context, &result, &status))
      << status;
  EXPECT_EQ(GetProtoValue(1), result.value());
  EXPECT_EQ(result.mutable_shared_proto_state()->get(),
            slot.mutable_shared_proto_state()->get());

  // Null struct as input.
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(
      auto null_struct_expr,
      ConstExpr::Create(Value::Null(MakeStructType({{"foo", Int64Type()}}))));
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(auto field_op_null,
                       FieldValueExpr::Create(0, std::move(null_struct_expr)));
  EXPECT_THAT(EvalExpr(*field_op_null, EmptyParams()),
              IsOkAndHolds(NullInt64()));
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

TEST_F(EvalTest, IfExpr) {
  // Use division by zero to force an error if the wrong branch gets evaluated.
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(auto true_expr, ConstExpr::Create(values::Bool(true)));
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(auto one_expr, ConstExpr::Create(Int64(1)));
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(auto if_op_true,
                       IfExpr::Create(std::move(true_expr), std::move(one_expr),
                                      DivByZeroErrorExpr()));
  EXPECT_EQ(
      "IfExpr(\n"
      "+-condition: ConstExpr(true),\n"
      "+-true_value: ConstExpr(1),\n"
      "+-false_value: Div(ConstExpr(1), ConstExpr(0)))",
      if_op_true->DebugString());
  EXPECT_THAT(EvalExpr(*if_op_true, EmptyParams()), IsOkAndHolds(Int64(1)));

  GOOGLESQL_ASSERT_OK_AND_ASSIGN(auto false_expr, ConstExpr::Create(values::Bool(false)));
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(auto one_expr2, ConstExpr::Create(Int64(1)));
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(auto if_op_false, IfExpr::Create(std::move(false_expr),
                                                        DivByZeroErrorExpr(),
                                                        std::move(one_expr2)));
  EXPECT_THAT(EvalExpr(*if_op_false, EmptyParams()), IsOkAndHolds(Int64(1)));

  GOOGLESQL_ASSERT_OK_AND_ASSIGN(auto null_expr, ConstExpr::Create(NullBool()));
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(auto one_expr3, ConstExpr::Create(Int64(1)));
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(auto if_op_null, IfExpr::Create(std::move(null_expr),
                                                       DivByZeroErrorExpr(),
                                                       std::move(one_expr3)));
  EXPECT_THAT(EvalExpr(*if_op_null, EmptyParams()), IsOkAndHolds(Int64(1)));
}

class EvalTestWithMemoryAccounting
    : public EvalTest,
      public WithParamInterface</*enable_memory_accounting=*/bool> {};

INSTANTIATE_TEST_SUITE_P(EvalTestWithMemoryAccountingSuite,
                         EvalTestWithMemoryAccounting, Bool());

TEST_P(EvalTestWithMemoryAccounting,
       ScalarFnCallExprArgsMemoryFootprintIsConstrainedWhenAccountingEnabled) {
  constexpr int64_t kMaxIntermediateByteSize = 12 * 1024;  // 12KB
  const bool enable_memory_accounting = GetParam();

  TypeFactory type_factory;

  constexpr int64_t kNumArrayConcatArgs = 4;
  constexpr int64_t kSplitArgSize = 4 * 1024;  // 4KB

  std::vector<std::unique_ptr<ValueExpr>> array_concat_args;
  array_concat_args.reserve(kNumArrayConcatArgs);
  // Generate 3 SPLIT function expressions to generate 4 arrays of 4096
  // chars/bytes each. That is to say a total of 16384 chars/bytes.
  for (int array_concat_arg_count = 0;
       array_concat_arg_count < kNumArrayConcatArgs; ++array_concat_arg_count) {
    GOOGLESQL_ASSERT_OK_AND_ASSIGN(
        std::unique_ptr<ValueExpr> input,
        ConstExpr::Create(String(std::string(kSplitArgSize, 'a'))));

    std::vector<std::unique_ptr<ValueExpr>> split_args;
    split_args.reserve(2);
    split_args.push_back(std::move(input));
    GOOGLESQL_ASSERT_OK_AND_ASSIGN(split_args.emplace_back(),
                         ConstExpr::Create(String("")));

    GOOGLESQL_ASSERT_OK_AND_ASSIGN(array_concat_args.emplace_back(),
                         ScalarFunctionCallExpr::Create(
                             CreateFunction(FunctionKind::kSplit, StringType()),
                             std::move(split_args), DEFAULT_ERROR_MODE));
  }

  const Type* string_array_type;
  GOOGLESQL_ASSERT_OK(type_factory.MakeArrayType(StringType(), &string_array_type));

  GOOGLESQL_ASSERT_OK_AND_ASSIGN(
      auto array_concat_expr,
      ScalarFunctionCallExpr::Create(
          CreateFunction(FunctionKind::kArrayConcat, string_array_type),
          std::move(array_concat_args), DEFAULT_ERROR_MODE));

  EvaluationOptions options;
  options.max_intermediate_byte_size = kMaxIntermediateByteSize;
  options.enable_function_args_memory_accounting = enable_memory_accounting;
  EvaluationContext memory_context(options);
  ASSERT_THAT(memory_context.memory_accountant()->remaining_bytes(),
              Eq(kMaxIntermediateByteSize));

  const absl::StatusOr<Value> result =
      EvalExpr(*array_concat_expr, EmptyParams(), &memory_context);

  if (enable_memory_accounting) {
    EXPECT_THAT(result, StatusIs(absl::StatusCode::kResourceExhausted));
  } else {
    GOOGLESQL_EXPECT_OK(result);
  }
  EXPECT_THAT(memory_context.memory_accountant()->remaining_bytes(),
              Eq(kMaxIntermediateByteSize));
}

TEST_F(EvalTest, WithExpr) {
  VariableId a("a"), x("x"), y("y");

  GOOGLESQL_ASSERT_OK_AND_ASSIGN(auto deref_x, DerefExpr::Create(x, Int64Type()));
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(auto deref_y, DerefExpr::Create(y, Int64Type()));

  std::vector<std::unique_ptr<ValueExpr>> args;
  args.push_back(std::move(deref_x));
  args.push_back(std::move(deref_y));

  GOOGLESQL_ASSERT_OK_AND_ASSIGN(auto body,
                       ScalarFunctionCallExpr::Create(
                           CreateFunction(FunctionKind::kAdd, Int64Type()),
                           std::move(args), DEFAULT_ERROR_MODE));

  GOOGLESQL_ASSERT_OK_AND_ASSIGN(auto deref_a, DerefExpr::Create(a, Int64Type()));
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(auto one, ConstExpr::Create(Int64(1)));

  std::vector<std::unique_ptr<ValueExpr>> a_plus_one_args;
  a_plus_one_args.push_back(std::move(deref_a));
  a_plus_one_args.push_back(std::move(one));

  GOOGLESQL_ASSERT_OK_AND_ASSIGN(auto a_plus_one,
                       ScalarFunctionCallExpr::Create(
                           CreateFunction(FunctionKind::kAdd, Int64Type()),
                           std::move(a_plus_one_args), DEFAULT_ERROR_MODE));

  GOOGLESQL_ASSERT_OK_AND_ASSIGN(auto deref_x_again, DerefExpr::Create(x, Int64Type()));

  std::vector<std::unique_ptr<ExprArg>> let_assign;
  let_assign.push_back(std::make_unique<ExprArg>(x, std::move(a_plus_one)));
  let_assign.push_back(std::make_unique<ExprArg>(y, std::move(deref_x_again)));

  GOOGLESQL_ASSERT_OK_AND_ASSIGN(
      auto let, WithExpr::Create(std::move(let_assign), std::move(body)));
  EXPECT_EQ(
      "WithExpr(\n"
      "+-assign: {\n"
      "| +-$x := Add($a, ConstExpr(1)),\n"
      "| +-$y := $x},\n"
      "+-body: Add($x, $y))",
      let->DebugString());

  const TupleSchema params_schema({a});
  const TupleData params_data = CreateTestTupleData({Int64(5)});
  GOOGLESQL_ASSERT_OK(let->SetSchemasForEvaluation({&params_schema}));

  EXPECT_THAT(EvalExpr(*let, {&params_data}),
              IsOkAndHolds(Int64(12)));  // (a+1) + (a+1)

  // Check that we get an error if the memory bound is too low. This is
  // particularly important if one of the variables holds an array that
  // represents a WITH table.
  EvaluationOptions options;
  options.max_intermediate_byte_size = 1;
  EvaluationContext memory_context(options);
  EXPECT_THAT(EvalExpr(*let, {&params_data}, &memory_context),
              StatusIs(absl::StatusCode::kResourceExhausted));
}

TEST_F(EvalTest, ArrayAtOffsetNonDeterminism) {
  VariableId arr("arr"), pos("pos");

  GOOGLESQL_ASSERT_OK_AND_ASSIGN(auto deref_arr,
                       DerefExpr::Create(arr, Int64ArrayType()));
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(auto deref_pos, DerefExpr::Create(pos, Int64Type()));

  std::vector<std::unique_ptr<ValueExpr>> args;
  args.push_back(std::move(deref_arr));
  args.push_back(std::move(deref_pos));

  GOOGLESQL_ASSERT_OK_AND_ASSIGN(
      auto fct, ScalarFunctionCallExpr::Create(
                    CreateFunction(FunctionKind::kArrayAtOffset, Int64Type()),
                    std::move(args)));
  const TupleSchema params_schema({arr, pos});
  GOOGLESQL_ASSERT_OK(fct->SetSchemasForEvaluation({&params_schema}));

  EvaluationContext context1((EvaluationOptions()));
  // [7,8][offset(1)] == 8, specified order.
  const TupleData params_data1 =
      CreateTestTupleData({Array({Int64(7), Int64(8)}), Int64(1)});
  EXPECT_THAT(EvalExpr(*fct, {&params_data1}, &context1),
              IsOkAndHolds(Int64(8)));
  EXPECT_TRUE(context1.IsDeterministicOutput());

  // [7,8][offset(1)] IN (7, 8), unspecified order.
  EvaluationContext context2((EvaluationOptions()));
  const TupleData params_data2 = CreateTestTupleData(
      {Array({Int64(7), Int64(8)}, kIgnoresOrder), Int64(1)});
  EXPECT_THAT(EvalExpr(*fct, {&params_data2}, &context2),
              IsOkAndHolds(AnyOf(Int64(7), Int64(8))));
  EXPECT_FALSE(context2.IsDeterministicOutput());
}

TEST_F(EvalTest, CodePointsToStringBytesNonDeterminism) {
  for (FunctionKind kind :
       {FunctionKind::kCodePointsToBytes, FunctionKind::kCodePointsToString}) {
    const Type* output_type = BytesType();
    if (kind == FunctionKind::kCodePointsToString) {
      output_type = StringType();
    }

    VariableId arr("arr");

    GOOGLESQL_ASSERT_OK_AND_ASSIGN(auto deref_arr,
                         DerefExpr::Create(arr, Int64ArrayType()));

    std::vector<std::unique_ptr<ValueExpr>> args;
    args.push_back(std::move(deref_arr));

    GOOGLESQL_ASSERT_OK_AND_ASSIGN(
        auto fct, ScalarFunctionCallExpr::Create(
                      CreateFunction(kind, output_type), std::move(args)));
    const TupleSchema params_schema({arr});
    GOOGLESQL_ASSERT_OK(fct->SetSchemasForEvaluation({&params_schema}));

    {
      // Deterministic order (single element).

      EvaluationContext context((EvaluationOptions()));
      const TupleData params_data =
          CreateTestTupleData({Array({Int64(70)}, kIgnoresOrder)});

      if (kind == FunctionKind::kCodePointsToBytes) {
        EXPECT_THAT(EvalExpr(*fct, {&params_data}, &context),
                    IsOkAndHolds(Bytes("F")));
      } else {
        EXPECT_THAT(EvalExpr(*fct, {&params_data}, &context),
                    IsOkAndHolds(String("F")));
      }
      EXPECT_TRUE(context.IsDeterministicOutput());
    }

    {
      // Non-deterministic order (multiple elements).

      EvaluationContext context((EvaluationOptions()));
      const TupleData params_data = CreateTestTupleData(
          {Array({Int64(70), Int64(111), Int64(111)}, kIgnoresOrder)});
      if (kind == FunctionKind::kCodePointsToBytes) {
        EXPECT_THAT(EvalExpr(*fct, {&params_data}, &context),
                    IsOkAndHolds(Bytes("Foo")));
      } else {
        EXPECT_THAT(EvalExpr(*fct, {&params_data}, &context),
                    IsOkAndHolds(String("Foo")));
      }
      EXPECT_FALSE(context.IsDeterministicOutput());
    }

    {
      // Deterministic order (multiple ordered elements).

      EvaluationContext context((EvaluationOptions()));
      const TupleData params_data =
          CreateTestTupleData({Array({Int64(70), Int64(111), Int64(111)})});
      if (kind == FunctionKind::kCodePointsToBytes) {
        EXPECT_THAT(EvalExpr(*fct, {&params_data}, &context),
                    IsOkAndHolds(Bytes("Foo")));
      } else {
        EXPECT_THAT(EvalExpr(*fct, {&params_data}, &context),
                    IsOkAndHolds(String("Foo")));
      }
      EXPECT_TRUE(context.IsDeterministicOutput());
    }
  }
}

TEST_F(EvalTest, ArrayReverseNonDeterminism) {
  VariableId arr("arr");

  GOOGLESQL_ASSERT_OK_AND_ASSIGN(auto deref_arr,
                       DerefExpr::Create(arr, Int64ArrayType()));

  std::vector<std::unique_ptr<ValueExpr>> args;
  args.push_back(std::move(deref_arr));

  GOOGLESQL_ASSERT_OK_AND_ASSIGN(auto fct, ScalarFunctionCallExpr::Create(
                                     CreateFunction(FunctionKind::kArrayReverse,
                                                    Int64ArrayType()),
                                     std::move(args)));
  const TupleSchema params_schema({arr});
  GOOGLESQL_ASSERT_OK(fct->SetSchemasForEvaluation({&params_schema}));

  {
    // Deterministic order (single element).

    EvaluationContext context((EvaluationOptions()));
    const TupleData params_data =
        CreateTestTupleData({Array({Int64(70)}, kIgnoresOrder)});
    EXPECT_THAT(EvalExpr(*fct, {&params_data}, &context),
                IsOkAndHolds(values::Int64Array({70})));
    EXPECT_TRUE(context.IsDeterministicOutput());
  }

  {
    // Non-deterministic order (multiple elements).

    EvaluationContext context((EvaluationOptions()));
    const TupleData params_data = CreateTestTupleData(
        {Array({Int64(70), Int64(111), Int64(112)}, kIgnoresOrder)});
    EXPECT_THAT(EvalExpr(*fct, {&params_data}, &context),
                IsOkAndHolds(values::Int64Array({112, 111, 70})));
    EXPECT_FALSE(context.IsDeterministicOutput());
  }

  {
    // Deterministic order (multiple ordered elements).

    EvaluationContext context((EvaluationOptions()));
    const TupleData params_data =
        CreateTestTupleData({Array({Int64(70), Int64(111), Int64(112)})});
    EXPECT_THAT(EvalExpr(*fct, {&params_data}, &context),
                IsOkAndHolds(values::Int64Array({112, 111, 70})));
    EXPECT_TRUE(context.IsDeterministicOutput());
  }
}

TEST_F(EvalTest, SingleValueExpr) {
  VariableId a("a");
  auto input0 =
      absl::WrapUnique(new TestRelationalOp({a}, {},
                                            /*preserves_order=*/true));
  std::vector<std::vector<const SharedProtoState*>> shared_states1;
  auto input1 = absl::WrapUnique(new TestRelationalOp(
      {a}, CreateTestTupleDatas({{GetProtoValue(1)}}, &shared_states1),
      /*preserves_order=*/true));
  auto input2 = absl::WrapUnique(
      new TestRelationalOp({a}, CreateTestTupleDatas({{Int64(1)}, {Int64(2)}}),
                           /*preserves_order=*/true));

  GOOGLESQL_ASSERT_OK_AND_ASSIGN(auto deref_a0, DerefExpr::Create(a, Int64Type()));
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(auto deref_a1, DerefExpr::Create(a, Int64Type()));
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(auto deref_a2, DerefExpr::Create(a, Int64Type()));

  // Empty.
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(
      auto element0,
      SingleValueExpr::Create(std::move(deref_a0), std::move(input0)));
  GOOGLESQL_ASSERT_OK(element0->SetSchemasForEvaluation(EmptyParamsSchemas()));
  EXPECT_EQ(
      "SingleValueExpr(\n"
      "+-value: $a,\n"
      "+-input: TestRelationalOp)",
      element0->DebugString());
  EXPECT_THAT(EvalExpr(*element0, EmptyParams()), IsOkAndHolds(NullInt64()));

  // Singleton.
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(
      auto element1,
      SingleValueExpr::Create(std::move(deref_a1), std::move(input1)));
  GOOGLESQL_ASSERT_OK(element1->SetSchemasForEvaluation(EmptyParamsSchemas()));
  EXPECT_EQ(
      "SingleValueExpr(\n"
      "+-value: $a,\n"
      "+-input: TestRelationalOp)",
      element1->DebugString());
  EvaluationContext context((EvaluationOptions()));
  TupleSlot result;
  absl::Status status;
  ASSERT_TRUE(element1->EvalSimple(EmptyParams(), &context, &result, &status))
      << status;
  EXPECT_EQ(result.value(), GetProtoValue(1));
  const std::shared_ptr<SharedProtoState> result_shared_state =
      *result.mutable_shared_proto_state();
  EXPECT_THAT(result_shared_state, Pointee(Eq(nullopt)));
  EXPECT_THAT(result_shared_state, HasRawPointer(shared_states1[0][0]));

  // More than one element.
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(
      auto element2,
      SingleValueExpr::Create(std::move(deref_a2), std::move(input2)));
  GOOGLESQL_ASSERT_OK(element2->SetSchemasForEvaluation(EmptyParamsSchemas()));
  EXPECT_EQ(
      "SingleValueExpr(\n"
      "+-value: $a,\n"
      "+-input: TestRelationalOp)",
      element2->DebugString());
  EXPECT_THAT(EvalExpr(*element2, EmptyParams()),
              StatusIs(absl::StatusCode::kOutOfRange, "More than one element"));
}

TEST_F(EvalTest, ExistsExpr) {
  VariableId a("a");
  auto input0 =
      absl::WrapUnique(new TestRelationalOp({a}, {}, /*preserves_order=*/true));
  auto input1 = absl::WrapUnique(
      new TestRelationalOp({a}, CreateTestTupleDatas({{Int64(1)}}),
                           /*preserves_order=*/true));
  auto input2 = absl::WrapUnique(
      new TestRelationalOp({a}, CreateTestTupleDatas({{Int64(1)}, {Int64(2)}}),
                           /*preserves_order=*/true));

  // Empty.
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(auto exists0, ExistsExpr::Create(std::move(input0)));
  GOOGLESQL_ASSERT_OK(exists0->SetSchemasForEvaluation(EmptyParamsSchemas()));
  EXPECT_EQ(
      "ExistsExpr(\n"
      "+-input: TestRelationalOp)",
      exists0->DebugString());
  EXPECT_THAT(EvalExpr(*exists0, EmptyParams()),
              IsOkAndHolds(values::Bool(false)));

  // Singleton.
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(auto exists1, ExistsExpr::Create(std::move(input1)));
  GOOGLESQL_ASSERT_OK(exists1->SetSchemasForEvaluation(EmptyParamsSchemas()));
  EXPECT_EQ(
      "ExistsExpr(\n"
      "+-input: TestRelationalOp)",
      exists1->DebugString());
  EXPECT_THAT(EvalExpr(*exists1, EmptyParams()),
              IsOkAndHolds(values::Bool(true)));

  // More than one element.
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(auto exists2, ExistsExpr::Create(std::move(input2)));
  GOOGLESQL_ASSERT_OK(exists2->SetSchemasForEvaluation(EmptyParamsSchemas()));
  EXPECT_EQ(
      "ExistsExpr(\n"
      "+-input: TestRelationalOp)",
      exists2->DebugString());
  EvaluationContext context((EvaluationOptions()));
  EXPECT_THAT(EvalExpr(*exists2, EmptyParams()),
              IsOkAndHolds(values::Bool(true)));
}

class ExistsExprErrorNonDeterminismTest
    : public ::testing::TestWithParam<bool> {};

TEST_P(ExistsExprErrorNonDeterminismTest, ExistsExprErrorNonDeterminism) {
  bool skip_nondeterminism_checking = GetParam();

  // Create an input with 2 rows, the second produces an error to ensure that
  // EXISTS() signals non-determinism and doesn't just return true after the
  // first row.
  VariableId a("a");
  auto input = absl::WrapUnique(
      new TestRelationalOp({a}, CreateTestTupleDatas({{Int64(1)}, {Int64(0)}}),
                           /*preserves_order=*/true));

  GOOGLESQL_ASSERT_OK_AND_ASSIGN(auto one_expr, ConstExpr::Create(Value::Int64(1)));
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(auto deref_a, DerefExpr::Create(a, Int64Type()));
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(auto function_kind,
                       BuiltinFunctionCatalog::GetKindByName("div"));
  std::vector<std::unique_ptr<ValueExpr>> args;
  args.push_back(std::move(one_expr));
  args.push_back(std::move(deref_a));
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(
      auto div_expr, BuiltinScalarFunction::CreateCall(
                         function_kind, LanguageOptions(), types::DoubleType(),
                         ConvertValueExprsToAlgebraArgs(std::move(args))));
  std::vector<std::unique_ptr<ExprArg>> exprs;
  exprs.push_back(std::make_unique<ExprArg>(std::move(div_expr)));
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(auto compute,
                       ComputeOp::Create(std::move(exprs), std::move(input)));

  GOOGLESQL_ASSERT_OK_AND_ASSIGN(auto exists, ExistsExpr::Create(std::move(compute)));
  GOOGLESQL_ASSERT_OK(exists->SetSchemasForEvaluation(EmptyParamsSchemas()));

  EvaluationContext context((EvaluationOptions{
      .return_early_from_exists_subquery = skip_nondeterminism_checking}));
  EXPECT_THAT(EvalExpr(*exists, EmptyParams(), &context),
              IsOkAndHolds(values::Bool(true)));
  EXPECT_EQ(context.IsDeterministicOutput(), skip_nondeterminism_checking);
}

INSTANTIATE_TEST_SUITE_P(ExistsExprErrorNonDeterminismTest,
                         ExistsExprErrorNonDeterminismTest,
                         ::testing::Bool()  // skip_nondeterminism_checking
);

TEST_F(EvalTest, DerefExprDuplicateIds) {
  const VariableId v("v");
  const VariableId w("w");
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(auto e, DerefExpr::Create(v, Int64Type()));

  const TupleSchema schema1({v, w});
  const TupleSchema schema2({v});
  const TupleSchema schema3({v});

  EXPECT_THAT(e->SetSchemasForEvaluation({&schema1, &schema2, &schema3}),
              StatusIs(absl::StatusCode::kInternal,
                       HasSubstr("Duplicate name detected: v")));
}

TEST_F(EvalTest, DerefExprNameNotFound) {
  const VariableId v("v");
  const VariableId w("w");
  const VariableId x("x");
  const VariableId y("y");
  const VariableId z("z");
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(auto e, DerefExpr::Create(v, Int64Type()));

  const TupleSchema schema1({w, x});
  const TupleSchema schema2({y, z});

  EXPECT_THAT(
      e->SetSchemasForEvaluation({&schema1, &schema2}),
      StatusIs(absl::StatusCode::kInternal, HasSubstr("Missing name: v")));
}

TEST_F(EvalTest, MultiStmtExpr) {
  VariableId x("x"), y("y"), cte1("cte1");
  TypeFactory type_factory;

  // Statement 1: SELECT 1 AS x, 'a' AS y
  // This creates a temporary result (a CTE) that can be referenced by later
  // statements.
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(auto one, ConstExpr::Create(Int64(1)));
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(auto enumerate_op, EnumerateOp::Create(std::move(one)));

  GOOGLESQL_ASSERT_OK_AND_ASSIGN(auto const_1, ConstExpr::Create(Int64(1)));
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(auto const_a, ConstExpr::Create(String("a")));
  std::vector<std::unique_ptr<ExprArg>> compute_args;
  compute_args.push_back(std::make_unique<ExprArg>(x, std::move(const_1)));
  compute_args.push_back(std::make_unique<ExprArg>(y, std::move(const_a)));
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(
      auto compute_op,
      ComputeOp::Create(std::move(compute_args), std::move(enumerate_op)));

  const StructType* cte_row_type =
      MakeStructType({{"x", Int64Type()}, {"y", StringType()}});
  const ArrayType* cte_array_type;
  GOOGLESQL_ASSERT_OK(type_factory.MakeArrayType(cte_row_type, &cte_array_type));
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(auto deref_x_for_nest,
                       DerefExpr::Create(x, Int64Type()));
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(auto deref_y_for_nest,
                       DerefExpr::Create(y, StringType()));
  std::vector<std::unique_ptr<ExprArg>> nest_fields;
  nest_fields.push_back(std::make_unique<ExprArg>(std::move(deref_x_for_nest)));
  nest_fields.push_back(std::make_unique<ExprArg>(std::move(deref_y_for_nest)));
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(
      auto struct_for_nest,
      NewStructExpr::Create(cte_row_type, std::move(nest_fields)));
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(
      auto nest_expr,
      ArrayNestExpr::Create(cte_array_type, std::move(struct_for_nest),
                            std::move(compute_op), /*is_with_table=*/true));
  auto stmt1_arg = std::make_unique<ExprArg>(cte1, std::move(nest_expr));

  // Statement 2: SELECT * FROM <cte1>
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(auto deref_cte1,
                       DerefExpr::Create(cte1, cte_array_type));
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(
      auto array_scan_op,
      ArrayScanOp::Create(VariableId(), VariableId(), {{x, 0}, {y, 1}},
                          std::move(deref_cte1)));

  const StructType* select_row_type =
      MakeStructType({{"x", Int64Type()}, {"y", StringType()}});
  const ArrayType* select_array_type;
  GOOGLESQL_ASSERT_OK(type_factory.MakeArrayType(select_row_type, &select_array_type));
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(auto deref_x_for_select,
                       DerefExpr::Create(x, Int64Type()));
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(auto deref_y_for_select,
                       DerefExpr::Create(y, StringType()));
  std::vector<std::unique_ptr<ExprArg>> select_fields;
  select_fields.push_back(
      std::make_unique<ExprArg>(std::move(deref_x_for_select)));
  select_fields.push_back(
      std::make_unique<ExprArg>(std::move(deref_y_for_select)));
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(
      auto struct_for_select,
      NewStructExpr::Create(select_row_type, std::move(select_fields)));
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(
      auto select_nest_expr,
      ArrayNestExpr::Create(select_array_type, std::move(struct_for_select),
                            std::move(array_scan_op), /*is_with_table=*/false));
  auto stmt2_arg = std::make_unique<ExprArg>(std::move(select_nest_expr));

  // Create and evaluate the MultiStmtExpr
  std::vector<std::unique_ptr<ExprArg>> multi_stmt_args;
  multi_stmt_args.push_back(std::move(stmt1_arg));
  multi_stmt_args.push_back(std::move(stmt2_arg));
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(auto multi_stmt_expr,
                       MultiStmtExpr::Create(std::move(multi_stmt_args)));

  GOOGLESQL_ASSERT_OK(multi_stmt_expr->SetSchemasForEvaluation(EmptyParamsSchemas()));

  EvaluationContext context((EvaluationOptions()));
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(std::vector<StmtResult> results,
                       multi_stmt_expr->EvalMulti(EmptyParams(), &context));

  // Verify the results
  ASSERT_EQ(results.size(), 2);

  Value expected_row = Struct({{"x", Int64(1)}, {"y", String("a")}});
  Value expected_result = Array({expected_row});

  // Result 1: CreateWithEntryStmt
  GOOGLESQL_ASSERT_OK(results[0].value);
  EXPECT_EQ(results[0].value.value(), expected_result);

  // Result 2: SELECT
  GOOGLESQL_ASSERT_OK(results[1].value);
  EXPECT_EQ(results[1].value.value(), expected_result);
}

TEST_F(EvalTest, MultiStmtExpr_FailingCTECausesCascadingFailure) {
  VariableId x("x"), cte1("cte1");
  TypeFactory type_factory;

  // Statement 1: SELECT 1/0 AS x
  // This fails to create a temporary result (a CTE) that can be referenced by
  // later statements, and subsequent statements will fail due to cascading
  // failures.
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(auto one, ConstExpr::Create(Int64(1)));
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(auto zero, ConstExpr::Create(Int64(0)));
  std::vector<std::unique_ptr<ValueExpr>> div_args;
  div_args.push_back(std::move(one));
  div_args.push_back(std::move(zero));
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(auto div_by_zero,
                       ScalarFunctionCallExpr::Create(
                           CreateFunction(FunctionKind::kDiv, Int64Type()),
                           std::move(div_args)));

  GOOGLESQL_ASSERT_OK_AND_ASSIGN(auto enumerate_op_one, ConstExpr::Create(Int64(1)));
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(auto enumerate_op,
                       EnumerateOp::Create(std::move(enumerate_op_one)));

  std::vector<std::unique_ptr<ExprArg>> compute_args;
  compute_args.push_back(std::make_unique<ExprArg>(x, std::move(div_by_zero)));
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(
      auto compute_op,
      ComputeOp::Create(std::move(compute_args), std::move(enumerate_op)));

  const StructType* cte_row_type = MakeStructType({{"x", Int64Type()}});
  const ArrayType* cte_array_type;
  GOOGLESQL_ASSERT_OK(type_factory.MakeArrayType(cte_row_type, &cte_array_type));
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(auto deref_x_for_nest,
                       DerefExpr::Create(x, Int64Type()));
  std::vector<std::unique_ptr<ExprArg>> nest_fields;
  nest_fields.push_back(std::make_unique<ExprArg>(std::move(deref_x_for_nest)));
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(
      auto struct_for_nest,
      NewStructExpr::Create(cte_row_type, std::move(nest_fields)));
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(
      auto nest_expr,
      ArrayNestExpr::Create(cte_array_type, std::move(struct_for_nest),
                            std::move(compute_op), /*is_with_table=*/true));
  auto stmt1_arg = std::make_unique<ExprArg>(cte1, std::move(nest_expr));

  // Statement 2: SELECT * FROM <cte1>
  // This statement should fail because its prerequisite (statement 1) fails.
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(auto deref_cte1,
                       DerefExpr::Create(cte1, cte_array_type));
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(auto array_scan_op,
                       ArrayScanOp::Create(VariableId(), VariableId(), {{x, 0}},
                                           std::move(deref_cte1)));

  const StructType* select_row_type = MakeStructType({{"x", Int64Type()}});
  const ArrayType* select_array_type;
  GOOGLESQL_ASSERT_OK(type_factory.MakeArrayType(select_row_type, &select_array_type));
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(auto deref_x_for_select,
                       DerefExpr::Create(x, Int64Type()));
  std::vector<std::unique_ptr<ExprArg>> select_fields;
  select_fields.push_back(
      std::make_unique<ExprArg>(std::move(deref_x_for_select)));
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(
      auto struct_for_select,
      NewStructExpr::Create(select_row_type, std::move(select_fields)));
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(
      auto select_nest_expr,
      ArrayNestExpr::Create(select_array_type, std::move(struct_for_select),
                            std::move(array_scan_op), /*is_with_table=*/false));
  auto stmt2_arg = std::make_unique<ExprArg>(std::move(select_nest_expr));

  // Create and evaluate the MultiStmtExpr
  std::vector<std::unique_ptr<ExprArg>> multi_stmt_args;
  multi_stmt_args.push_back(std::move(stmt1_arg));
  multi_stmt_args.push_back(std::move(stmt2_arg));
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(auto multi_stmt_expr,
                       MultiStmtExpr::Create(std::move(multi_stmt_args)));

  GOOGLESQL_ASSERT_OK(multi_stmt_expr->SetSchemasForEvaluation(EmptyParamsSchemas()));

  EvaluationContext context((EvaluationOptions()));
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(std::vector<StmtResult> results,
                       multi_stmt_expr->EvalMulti(EmptyParams(), &context));

  // Verify the results
  ASSERT_EQ(results.size(), 2);

  // Statement 1: CreateWithEntryStmt (fails)
  EXPECT_THAT(results[0].value, StatusIs(absl::StatusCode::kOutOfRange,
                                         HasSubstr("division by zero")));

  // Statement 2: SELECT (fails due to cascading failure)
  EXPECT_THAT(
      results[1].value,
      StatusIs(absl::StatusCode::kOutOfRange,
               HasSubstr("Prerequisite CreateWithEntryStmt failed, "
                         "statement index: 0 error: division by zero")));
}

TEST_F(EvalTest, MultiStmtExpr_EvalWithSingleStatement) {
  // Eval on a MultiStmtExpr with a single statement fails as well.
  TypeFactory type_factory;
  const StructType* row_type = MakeStructType({{"x", Int64Type()}});
  const ArrayType* array_type;
  GOOGLESQL_ASSERT_OK(type_factory.MakeArrayType(row_type, &array_type));

  // Statement: SELECT 1 AS x;
  Value row = Struct({{"x", Int64(1)}});
  Value array_val = Array({row});

  GOOGLESQL_ASSERT_OK_AND_ASSIGN(auto const_expr, ConstExpr::Create(array_val));
  auto stmt_arg = std::make_unique<ExprArg>(std::move(const_expr));

  std::vector<std::unique_ptr<ExprArg>> multi_stmt_args;
  multi_stmt_args.push_back(std::move(stmt_arg));
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(auto multi_stmt_expr,
                       MultiStmtExpr::Create(std::move(multi_stmt_args)));

  GOOGLESQL_ASSERT_OK(multi_stmt_expr->SetSchemasForEvaluation(EmptyParamsSchemas()));

  EvaluationContext context((EvaluationOptions()));

  EXPECT_THAT(EvalExpr(*multi_stmt_expr, EmptyParams(), &context),
              StatusIs(absl::StatusCode::kOutOfRange,
                       HasSubstr("Multiple statements are not supported in "
                                 "Eval. Use EvalMulti instead")));
}

TEST_F(EvalTest, MultiStmtExpr_EvalWithMultipleStatements) {
  // Eval on a MultiStmtExpr with multiple statements fails.
  TypeFactory type_factory;
  const StructType* row_type = MakeStructType({{"x", Int64Type()}});
  const ArrayType* array_type;
  GOOGLESQL_ASSERT_OK(type_factory.MakeArrayType(row_type, &array_type));

  Value row = Struct({{"x", Int64(1)}});
  Value array_val = Array({row});

  // Statement 1: SELECT 1 AS x;
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(auto const_expr1, ConstExpr::Create(array_val));
  auto stmt_arg1 = std::make_unique<ExprArg>(std::move(const_expr1));

  // Statement 2: SELECT 1 AS x;
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(auto const_expr2, ConstExpr::Create(array_val));
  auto stmt_arg2 = std::make_unique<ExprArg>(std::move(const_expr2));

  std::vector<std::unique_ptr<ExprArg>> multi_stmt_args;
  multi_stmt_args.push_back(std::move(stmt_arg1));
  multi_stmt_args.push_back(std::move(stmt_arg2));
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(auto multi_stmt_expr,
                       MultiStmtExpr::Create(std::move(multi_stmt_args)));

  GOOGLESQL_ASSERT_OK(multi_stmt_expr->SetSchemasForEvaluation(EmptyParamsSchemas()));

  EvaluationContext context((EvaluationOptions()));
  EXPECT_THAT(EvalExpr(*multi_stmt_expr, EmptyParams(), &context),
              StatusIs(absl::StatusCode::kOutOfRange,
                       "Multiple statements are not supported in Eval. Use "
                       "EvalMulti instead"));
}

TEST_F(EvalTest, EvalMultiSuccess) {
  // EvalMulti by default just calls EvalSimple and returns a single result.
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(auto const_expr, ConstExpr::Create(Int64(42)));
  GOOGLESQL_ASSERT_OK(const_expr->SetSchemasForEvaluation(EmptyParamsSchemas()));

  EvaluationContext context((EvaluationOptions()));
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(std::vector<StmtResult> results,
                       const_expr->EvalMulti(EmptyParams(), &context));

  ASSERT_EQ(results.size(), 1);
  GOOGLESQL_ASSERT_OK(results[0].value);
  EXPECT_EQ(results[0].value.value(), Int64(42));
}

TEST_F(EvalTest, EvalMultiFailure) {
  // EvalMulti by default just calls EvalSimple and returns the error.
  auto div_by_zero_expr = DivByZeroErrorExpr();
  GOOGLESQL_ASSERT_OK(div_by_zero_expr->SetSchemasForEvaluation(EmptyParamsSchemas()));

  EvaluationContext context((EvaluationOptions()));
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(std::vector<StmtResult> results,
                       div_by_zero_expr->EvalMulti(EmptyParams(), &context));

  ASSERT_EQ(results.size(), 1);
  EXPECT_THAT(results[0].value, StatusIs(absl::StatusCode::kOutOfRange,
                                         HasSubstr("division by zero")));
}

class DMLValueExprEvalTest : public EvalTest {
 public:
  DMLValueExprEvalTest() {
    GOOGLESQL_CHECK_OK(table_.SetPrimaryKey({0}));
    absl::flat_hash_map<std::string, const Type*> types_ignored;
    GOOGLESQL_CHECK_OK(GetBuiltinFunctionsAndTypes(
        BuiltinFunctionOptions::AllReleasedFunctions(), *type_factory(),
        functions_, types_ignored));
  }

  const Table* table() { return &table_; }

  const Function* function(absl::string_view name) {
    return functions_[name].get();
  }

  absl::StatusOr<std::unique_ptr<DMLInsertValueExpr>>
  InsertStmtToInsertExprConverter(const ResolvedInsertStmt* stmt,
                                  const ArrayType* table_array_type) {
    GOOGLESQL_ASSIGN_OR_RETURN(
        const StructType* primary_key_type,
        CreatePrimaryKeyType(stmt->table_scan()->table(), type_factory()));
    GOOGLESQL_ASSIGN_OR_RETURN(const StructType* dml_output_type,
                     CreateDMLOutputType(table_array_type, type_factory()));
    // Create a ColumnToVariableMapping.
    auto column_to_variable_mapping = std::make_unique<ColumnToVariableMapping>(
        std::make_unique<VariableGenerator>());

    // Build a ResolvedScanMap and a ResolvedExprMap from the AST.
    auto resolved_scan_map = std::make_unique<ResolvedScanMap>();
    GOOGLESQL_ASSIGN_OR_RETURN(
        std::unique_ptr<ValueExpr> table_as_array_expr,
        TableAsArrayExpr::Create(stmt->table_scan()->table()->Name(),
                                 table_array_type));
    GOOGLESQL_ASSIGN_OR_RETURN(
        std::unique_ptr<RelationalOp> relation_op,
        ArrayScanOp::Create(
            /*element=*/VariableId(),
            /*position=*/VariableId(),
            {{column_to_variable_mapping->GetVariableNameFromColumn(
                  stmt->table_scan()->column_list()[0]),
              0},
             {column_to_variable_mapping->GetVariableNameFromColumn(
                  stmt->table_scan()->column_list()[1]),
              1}},
            std::move(table_as_array_expr)));
    (*resolved_scan_map)[stmt->table_scan()] = std::move(relation_op);
    auto resolved_expr_map = std::make_unique<ResolvedExprMap>();
    for (const auto& value : stmt->row_list(0)->value_list()) {
      GOOGLESQL_ASSIGN_OR_RETURN(
          std::unique_ptr<ValueExpr> const_expr,
          ConstExpr::Create(value->value()->GetAs<ResolvedLiteral>()->value()));
      (*resolved_expr_map)[value->value()] = std::move(const_expr);
    }
    if (stmt->row_list_size() == 2) {
      for (const auto& value : stmt->row_list(1)->value_list()) {
        GOOGLESQL_ASSIGN_OR_RETURN(
            std::unique_ptr<ValueExpr> const_expr,
            ConstExpr::Create(
                value->value()->GetAs<ResolvedLiteral>()->value()));
        (*resolved_expr_map)[value->value()] = std::move(const_expr);
      }
    }

    auto column_expr_map = std::make_unique<ColumnExprMap>();

    // Create the DMLInsertValueExpr to be tested.
    return DMLInsertValueExpr::Create(
        stmt->table_scan()->table(), table_array_type,
        /*returning_array_type=*/nullptr, primary_key_type, dml_output_type,
        stmt, &stmt->table_scan()->column_list(),
        /*returning_column_values=*/nullptr,
        std::move(column_to_variable_mapping), std::move(resolved_scan_map),
        std::move(resolved_expr_map), std::move(column_expr_map));
  }

 private:
  SimpleTable table_{"test_table",
                     {{"int_val", Int64Type()}, {"str_val", StringType()}}};
  absl::flat_hash_map<std::string, std::unique_ptr<Function>> functions_;
};

TEST_F(DMLValueExprEvalTest, DMLInsertValueExpr) {
  // Build a resolved AST for inserting a new row (3, "three") into the table.
  std::unique_ptr<ResolvedTableScan> table_scan = MakeResolvedTableScan(
      {ResolvedColumn{1, googlesql::IdString::MakeGlobal("test_table"),
                      googlesql::IdString::MakeGlobal("int_val"), Int64Type()},
       ResolvedColumn{2, googlesql::IdString::MakeGlobal("test_table"),
                      googlesql::IdString::MakeGlobal("str_val"),
                      StringType()}},
      table(), /*for_system_time_expr=*/nullptr);
  std::vector<std::unique_ptr<const ResolvedDMLValue>> row_values;
  row_values.push_back(MakeResolvedDMLValue(
      MakeResolvedLiteral(Int64Type(), Int64(3), /*has_explicit_type=*/
                          true, /*float_literal_id=*/0)));
  row_values.push_back(MakeResolvedDMLValue(
      MakeResolvedLiteral(StringType(), String("three"), /*has_explicit_type=*/
                          true, /*float_literal_id=*/0)));
  std::vector<std::unique_ptr<const ResolvedInsertRow>> row_list;
  row_list.push_back(MakeResolvedInsertRow(std::move(row_values)));
  std::unique_ptr<ResolvedInsertStmt> stmt = MakeResolvedInsertStmt(
      std::move(table_scan), ResolvedInsertStmt::OR_ERROR,
      /*assert_rows_modified=*/nullptr, /*returning=*/nullptr,
      {ResolvedColumn{1, googlesql::IdString::MakeGlobal("test_table"),
                      googlesql::IdString::MakeGlobal("int_val"), Int64Type()},
       ResolvedColumn{2, googlesql::IdString::MakeGlobal("test_table"),
                      googlesql::IdString::MakeGlobal("str_val"),
                      StringType()}},
      /*query_parameter_list=*/{}, /*query=*/nullptr,
      /*query_output_column_list=*/{}, std::move(row_list),
      /*on_conflict_clause=*/nullptr);

  // Create output types.
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(
      const ArrayType* table_array_type,
      CreateTableArrayType(stmt->table_scan()->column_list(),
                           stmt->table_scan()->table()->IsValueTable(),
                           type_factory()));

  GOOGLESQL_ASSERT_OK_AND_ASSIGN(
      std::unique_ptr<DMLInsertValueExpr> expr,
      InsertStmtToInsertExprConverter(stmt.get(), table_array_type));

  // Evaluate and check.
  TupleSlot result;
  absl::Status status;
  // the case for return_all_rows_for_dml = true is covered by the reference
  // implementation compliance tests
  EvaluationOptions options{};
  options.return_all_rows_for_dml = false;
  EvaluationContext context{options};
  GOOGLESQL_CHECK_OK(context.AddTableAsArray(
      "test_table", /*is_value_table=*/false,
      Value::Array(table_array_type,
                   {Value::Struct(table_array_type->element_type()->AsStruct(),
                                  {Int64(1), String("one")}),
                    Value::Struct(table_array_type->element_type()->AsStruct(),
                                  {Int64(2), NullString()}),
                    Value::Struct(table_array_type->element_type()->AsStruct(),
                                  {Int64(4), NullString()})}),
      LanguageOptions{}));
  GOOGLESQL_ASSERT_OK(expr->SetSchemasForEvaluation({}));
  EXPECT_TRUE(expr->EvalSimple({}, &context, &result, &status));
  GOOGLESQL_ASSERT_OK(status);
  EXPECT_EQ(result.value().field(0).int64_value(), 1);
  EXPECT_THAT(result.value().field(1).elements(),
              UnorderedElementsAre(Property(
                  &Value::fields, ElementsAre(Int64(3), String("three")))));
}

TEST_F(DMLValueExprEvalTest, DMLInsertOrIgnoreValueExpr) {
  const ResolvedColumnList& column_list = {
      ResolvedColumn{1, IdString::MakeGlobal("test_table"),
                     IdString::MakeGlobal("int_val"), Int64Type()},
      ResolvedColumn{2, IdString::MakeGlobal("test_table"),
                     googlesql::IdString::MakeGlobal("str_val"), StringType()}};
  auto table_scan = MakeResolvedTableScan(column_list, table(),
                                          /*for_system_time_expr=*/nullptr);

  // Build a resolved AST for inserting a row with PK (1), that already exists.
  std::vector<std::unique_ptr<const ResolvedDMLValue>> row_values;
  row_values.push_back(
      MakeResolvedDMLValue(MakeResolvedLiteral(Int64Type(), Int64(1))));
  row_values.push_back(MakeResolvedDMLValue(
      MakeResolvedLiteral(StringType(), String("one again"))));
  std::vector<std::unique_ptr<const ResolvedInsertRow>> row_list;
  row_list.push_back(MakeResolvedInsertRow(std::move(row_values)));

  GOOGLESQL_ASSERT_OK_AND_ASSIGN(auto stmt,
                       ResolvedInsertStmtBuilder()
                           .set_table_scan(std::move(table_scan))
                           .set_insert_mode(ResolvedInsertStmt::OR_IGNORE)
                           .set_row_list(std::move(row_list))
                           .set_insert_column_list(column_list)
                           .Build());

  // Create output types.
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(
      const ArrayType* table_array_type,
      CreateTableArrayType(stmt->table_scan()->column_list(),
                           stmt->table_scan()->table()->IsValueTable(),
                           type_factory()));

  GOOGLESQL_ASSERT_OK_AND_ASSIGN(
      std::unique_ptr<DMLInsertValueExpr> expr,
      InsertStmtToInsertExprConverter(stmt.get(), table_array_type));

  // Evaluate and check.
  TupleSlot result;
  absl::Status status;
  // the case for return_all_rows_for_dml = true is covered by the reference
  // implementation compliance tests
  EvaluationOptions options{};
  options.return_all_rows_for_dml = false;
  EvaluationContext context{options};
  GOOGLESQL_ASSERT_OK(context.AddTableAsArray(
      "test_table", /*is_value_table=*/false,
      Value::Array(table_array_type,
                   {Value::Struct(table_array_type->element_type()->AsStruct(),
                                  {Int64(1), String("one")}),
                    Value::Struct(table_array_type->element_type()->AsStruct(),
                                  {Int64(2), NullString()}),
                    Value::Struct(table_array_type->element_type()->AsStruct(),
                                  {Int64(4), NullString()})}),
      LanguageOptions{}));
  GOOGLESQL_ASSERT_OK(expr->SetSchemasForEvaluation({}));
  EXPECT_TRUE(expr->EvalSimple({}, &context, &result, &status));
  GOOGLESQL_ASSERT_OK(status);
  // No rows are returned
  EXPECT_EQ(result.value().field(0).int64_value(), 0);
  EXPECT_TRUE(result.value().field(1).empty());
}

TEST_F(DMLValueExprEvalTest, DMLInsertOrIgnoreValueExprReturnAllInsertRows) {
  const ResolvedColumnList& column_list = {
      ResolvedColumn{1, IdString::MakeGlobal("test_table"),
                     IdString::MakeGlobal("int_val"), Int64Type()},
      ResolvedColumn{2, IdString::MakeGlobal("test_table"),
                     googlesql::IdString::MakeGlobal("str_val"), StringType()}};
  auto table_scan = MakeResolvedTableScan(column_list, table(),
                                          /*for_system_time_expr=*/nullptr);

  // Build a resolved AST for inserting:
  // (1) a row with PK: 1, that already exists.
  std::vector<std::unique_ptr<const ResolvedDMLValue>> insert_row1;
  insert_row1.push_back(
      MakeResolvedDMLValue(MakeResolvedLiteral(Int64Type(), Int64(1))));
  insert_row1.push_back(MakeResolvedDMLValue(
      MakeResolvedLiteral(StringType(), String("one again"))));
  // (2) a row with PK: 10 that does not exist.
  std::vector<std::unique_ptr<const ResolvedDMLValue>> insert_row2;
  insert_row2.push_back(
      MakeResolvedDMLValue(MakeResolvedLiteral(Int64Type(), Int64(10))));
  insert_row2.push_back(
      MakeResolvedDMLValue(MakeResolvedLiteral(StringType(), String("ten"))));
  std::vector<std::unique_ptr<const ResolvedInsertRow>> row_list;
  row_list.push_back(MakeResolvedInsertRow(std::move(insert_row1)));
  row_list.push_back(MakeResolvedInsertRow(std::move(insert_row2)));

  GOOGLESQL_ASSERT_OK_AND_ASSIGN(auto stmt,
                       ResolvedInsertStmtBuilder()
                           .set_table_scan(std::move(table_scan))
                           .set_insert_mode(ResolvedInsertStmt::OR_IGNORE)
                           .set_row_list(std::move(row_list))
                           .set_insert_column_list(column_list)
                           .Build());

  // Create output types.
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(
      const ArrayType* table_array_type,
      CreateTableArrayType(stmt->table_scan()->column_list(),
                           stmt->table_scan()->table()->IsValueTable(),
                           type_factory()));

  GOOGLESQL_ASSERT_OK_AND_ASSIGN(
      std::unique_ptr<DMLInsertValueExpr> expr,
      InsertStmtToInsertExprConverter(stmt.get(), table_array_type));

  // Evaluate and check.
  TupleSlot result;
  absl::Status status;
  // the case for return_all_rows_for_dml = true is covered by the reference
  // implementation compliance tests
  EvaluationOptions options{};
  options.return_all_insert_rows_insert_ignore_dml = true;
  options.return_all_rows_for_dml = false;
  EvaluationContext context{options};
  GOOGLESQL_ASSERT_OK(context.AddTableAsArray(
      "test_table", /*is_value_table=*/false,
      Value::Array(table_array_type,
                   {Value::Struct(table_array_type->element_type()->AsStruct(),
                                  {Int64(1), String("one")}),
                    Value::Struct(table_array_type->element_type()->AsStruct(),
                                  {Int64(2), NullString()}),
                    Value::Struct(table_array_type->element_type()->AsStruct(),
                                  {Int64(4), NullString()})}),
      LanguageOptions{}));
  GOOGLESQL_ASSERT_OK(expr->SetSchemasForEvaluation({}));
  EXPECT_TRUE(expr->EvalSimple({}, &context, &result, &status));
  GOOGLESQL_ASSERT_OK(status);
  // 1st struct field is the number of rows modified.
  // The count accounts only for the new row inserted.
  EXPECT_EQ(result.value().field(0).int64_value(), 1);
  // 2nd struct field returns the insert rows, both the new and the ignored
  // row.
  ASSERT_FALSE(result.value().field(1).empty());
  EXPECT_EQ(result.value().field(1).elements().size(), 2);
  EXPECT_EQ(result.value().field(1).elements().at(0).field(0).int64_value(), 1);
  EXPECT_EQ(result.value().field(1).elements().at(0).field(1).string_value(),
            "one again");
  EXPECT_EQ(result.value().field(1).elements().at(1).field(0).int64_value(),
            10);
  EXPECT_EQ(result.value().field(1).elements().at(1).field(1).string_value(),
            "ten");
}

TEST_F(DMLValueExprEvalTest,
       DMLInsertValueExprSetsPrimaryKeyValuesToNullWhenDisallowed) {
  // Build a resolved AST for inserting a new row (3, "three") into the table.
  std::unique_ptr<ResolvedTableScan> table_scan = MakeResolvedTableScan(
      {ResolvedColumn{1, googlesql::IdString::MakeGlobal("test_table"),
                      googlesql::IdString::MakeGlobal("int_val"), Int64Type()},
       ResolvedColumn{2, googlesql::IdString::MakeGlobal("test_table"),
                      googlesql::IdString::MakeGlobal("str_val"),
                      StringType()}},
      table(), /*for_system_time_expr=*/nullptr);
  std::vector<std::unique_ptr<const ResolvedDMLValue>> row_values;
  row_values.push_back(MakeResolvedDMLValue(
      MakeResolvedLiteral(Int64Type(), NullInt64(), /*has_explicit_type=*/
                          true, /*float_literal_id=*/0)));
  row_values.push_back(MakeResolvedDMLValue(
      MakeResolvedLiteral(StringType(), String("three"), /*has_explicit_type=*/
                          true, /*float_literal_id=*/0)));
  std::vector<std::unique_ptr<const ResolvedInsertRow>> row_list;
  row_list.push_back(MakeResolvedInsertRow(std::move(row_values)));
  std::unique_ptr<ResolvedInsertStmt> stmt = MakeResolvedInsertStmt(
      std::move(table_scan), ResolvedInsertStmt::OR_ERROR,
      /*assert_rows_modified=*/nullptr, /*returning=*/nullptr,
      {ResolvedColumn{1, googlesql::IdString::MakeGlobal("test_table"),
                      googlesql::IdString::MakeGlobal("int_val"), Int64Type()},
       ResolvedColumn{2, googlesql::IdString::MakeGlobal("test_table"),
                      googlesql::IdString::MakeGlobal("str_val"),
                      StringType()}},
      /*query_parameter_list=*/{}, /*query=*/nullptr,
      /*query_output_column_list=*/{}, std::move(row_list),
      /*on_conflict_clause=*/nullptr);

  // Create output types.
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(
      const ArrayType* table_array_type,
      CreateTableArrayType(stmt->table_scan()->column_list(),
                           stmt->table_scan()->table()->IsValueTable(),
                           type_factory()));
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(
      const StructType* primary_key_type,
      CreatePrimaryKeyType(stmt->table_scan()->table(), type_factory()));
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(const StructType* dml_output_type,
                       CreateDMLOutputType(table_array_type, type_factory()));

  // Create a ColumnToVariableMapping.
  auto column_to_variable_mapping = std::make_unique<ColumnToVariableMapping>(
      std::make_unique<VariableGenerator>());

  // Build a ResolvedScanMap and a ResolvedExprMap from the AST.
  auto resolved_scan_map = std::make_unique<ResolvedScanMap>();
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(
      std::unique_ptr<ValueExpr> table_as_array_expr,
      TableAsArrayExpr::Create(stmt->table_scan()->table()->Name(),
                               table_array_type));
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(
      std::unique_ptr<RelationalOp> relation_op,
      ArrayScanOp::Create(
          /*element=*/VariableId(),
          /*position=*/VariableId(),
          {std::make_pair(column_to_variable_mapping->GetVariableNameFromColumn(
                              stmt->table_scan()->column_list()[0]),
                          0),
           std::make_pair(column_to_variable_mapping->GetVariableNameFromColumn(
                              stmt->table_scan()->column_list()[1]),
                          1)},
          std::move(table_as_array_expr)));
  (*resolved_scan_map)[stmt->table_scan()] = std::move(relation_op);
  auto resolved_expr_map = std::make_unique<ResolvedExprMap>();
  for (const auto& value : stmt->row_list(0)->value_list()) {
    GOOGLESQL_ASSERT_OK_AND_ASSIGN(
        std::unique_ptr<ValueExpr> const_expr,
        ConstExpr::Create(value->value()->GetAs<ResolvedLiteral>()->value()));
    (*resolved_expr_map)[value->value()] = std::move(const_expr);
  }

  auto column_expr_map = std::make_unique<ColumnExprMap>();

  // Create the DMLInsertValueExpr to be tested.
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(
      std::unique_ptr<DMLInsertValueExpr> expr,
      DMLInsertValueExpr::Create(
          stmt->table_scan()->table(), table_array_type,
          /*returning_array_type=*/nullptr, primary_key_type, dml_output_type,
          stmt.get(), &stmt->table_scan()->column_list(),
          /*returning_column_values=*/nullptr,
          std::move(column_to_variable_mapping), std::move(resolved_scan_map),
          std::move(resolved_expr_map), std::move(column_expr_map)));

  // Evaluate and check.
  TupleSlot result;
  absl::Status status;
  // the case for return_all_rows_for_dml = true is covered by the reference
  // implementation compliance tests
  EvaluationOptions options{};
  options.return_all_rows_for_dml = false;
  EvaluationContext context{options};
  LanguageOptions language_options;
  language_options.EnableLanguageFeature(FEATURE_DISALLOW_NULL_PRIMARY_KEYS);
  context.SetLanguageOptions(language_options);
  GOOGLESQL_CHECK_OK(context.AddTableAsArray(
      "test_table", /*is_value_table=*/false,
      Value::Array(table_array_type,
                   {Value::Struct(table_array_type->element_type()->AsStruct(),
                                  {Int64(1), String("one")}),
                    Value::Struct(table_array_type->element_type()->AsStruct(),
                                  {Int64(2), NullString()}),
                    Value::Struct(table_array_type->element_type()->AsStruct(),
                                  {Int64(4), NullString()})}),
      language_options));
  GOOGLESQL_ASSERT_OK(expr->SetSchemasForEvaluation({}));
  EXPECT_FALSE(expr->EvalSimple({}, &context, &result, &status));
  EXPECT_THAT(
      status,
      StatusIs(absl::StatusCode::kOutOfRange,
               HasSubstr("INSERT a NULL value into a primary key column")));
}

TEST_F(DMLValueExprEvalTest, DMLDeleteValueExpr) {
  // Build a resolved AST for deleting rows where str_val is null from the
  // table.
  std::unique_ptr<ResolvedTableScan> table_scan = MakeResolvedTableScan(
      {ResolvedColumn{1, googlesql::IdString::MakeGlobal("test_table"),
                      googlesql::IdString::MakeGlobal("int_val"), Int64Type()},
       ResolvedColumn{2, googlesql::IdString::MakeGlobal("test_table"),
                      googlesql::IdString::MakeGlobal("str_val"),
                      StringType()}},
      table(), /*for_system_time_expr=*/nullptr);
  std::vector<std::unique_ptr<ResolvedColumnRef>> resolved_column_refs;
  resolved_column_refs.push_back(MakeResolvedColumnRef(
      table_scan->column_list(1).type(), table_scan->column_list(1),
      /*is_correlated=*/false));
  std::unique_ptr<ResolvedDeleteStmt> stmt = MakeResolvedDeleteStmt(
      std::move(table_scan), /*assert_rows_modified=*/nullptr,
      /*returning=*/nullptr,
      /*array_offset_column=*/nullptr, /*where_expr=*/
      MakeResolvedFunctionCall(BoolType(), function("$is_null"),
                               *function("$is_null")->GetSignature(0),
                               std::move(resolved_column_refs),
                               ResolvedFunctionCall::DEFAULT_ERROR_MODE));

  // Create output types.
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(
      const ArrayType* table_array_type,
      CreateTableArrayType(stmt->table_scan()->column_list(),
                           stmt->table_scan()->table()->IsValueTable(),
                           type_factory()));
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(
      const StructType* primary_key_type,
      CreatePrimaryKeyType(stmt->table_scan()->table(), type_factory()));
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(const StructType* dml_output_type,
                       CreateDMLOutputType(table_array_type, type_factory()));

  // Create a ColumnToVariableMapping.
  auto column_to_variable_mapping = std::make_unique<ColumnToVariableMapping>(
      std::make_unique<VariableGenerator>());

  // Build a ResolvedScanMap and a ResolvedExprMap from the AST.
  auto resolved_scan_map = std::make_unique<ResolvedScanMap>();
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(
      std::unique_ptr<ValueExpr> table_as_array_expr,
      TableAsArrayExpr::Create(stmt->table_scan()->table()->Name(),
                               table_array_type));
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(
      std::unique_ptr<RelationalOp> relation_op,
      ArrayScanOp::Create(
          /*element=*/VariableId(),
          /*position=*/VariableId(),
          {std::make_pair(column_to_variable_mapping->GetVariableNameFromColumn(
                              stmt->table_scan()->column_list()[0]),
                          0),
           std::make_pair(column_to_variable_mapping->GetVariableNameFromColumn(
                              stmt->table_scan()->column_list()[1]),
                          1)},
          std::move(table_as_array_expr)));
  (*resolved_scan_map)[stmt->table_scan()] = std::move(relation_op);
  auto resolved_expr_map = std::make_unique<ResolvedExprMap>();
  std::vector<std::unique_ptr<ValueExpr>> arguments{};
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(
      std::unique_ptr<DerefExpr> arg_expr,
      DerefExpr::Create(column_to_variable_mapping->GetVariableNameFromColumn(
                            stmt->where_expr()
                                ->GetAs<ResolvedFunctionCall>()
                                ->argument_list(0)
                                ->GetAs<ResolvedColumnRef>()
                                ->column()),
                        stmt->where_expr()
                            ->GetAs<ResolvedFunctionCall>()
                            ->argument_list(0)
                            ->GetAs<ResolvedColumnRef>()
                            ->column()
                            .type()));
  arguments.push_back(std::move(arg_expr));
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(
      FunctionKind function_kind,
      BuiltinFunctionCatalog::GetKindByName(stmt->where_expr()
                                                ->GetAs<ResolvedFunctionCall>()
                                                ->function()
                                                ->Name()));
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(
      std::unique_ptr<ValueExpr> function_call_expr,
      BuiltinScalarFunction::CreateCall(
          function_kind, LanguageOptions{},
          stmt->where_expr()->GetAs<ResolvedFunctionCall>()->type(),
          ConvertValueExprsToAlgebraArgs(std::move(arguments)),
          stmt->where_expr()->GetAs<ResolvedFunctionCall>()->error_mode()));
  (*resolved_expr_map)[stmt->where_expr()] = std::move(function_call_expr);

  // Create the DMLDeleteValueExpr to be tested.
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(
      std::unique_ptr<DMLDeleteValueExpr> expr,
      DMLDeleteValueExpr::Create(
          stmt->table_scan()->table(), table_array_type,
          /*returning_array_type=*/nullptr, primary_key_type, dml_output_type,
          stmt.get(), &stmt->table_scan()->column_list(),
          /*returning_column_values=*/nullptr,
          std::move(column_to_variable_mapping), std::move(resolved_scan_map),
          std::move(resolved_expr_map)));

  // Evaluate and check.
  TupleSlot result;
  absl::Status status;
  // the case for return_all_rows_for_dml = true is covered by the reference
  // implementation compliance tests
  EvaluationOptions options{};
  options.return_all_rows_for_dml = false;
  EvaluationContext context{options};
  GOOGLESQL_CHECK_OK(context.AddTableAsArray(
      "test_table", /*is_value_table=*/false,
      Value::Array(table_array_type,
                   {Value::Struct(table_array_type->element_type()->AsStruct(),
                                  {Int64(1), String("one")}),
                    Value::Struct(table_array_type->element_type()->AsStruct(),
                                  {Int64(2), NullString()}),
                    Value::Struct(table_array_type->element_type()->AsStruct(),
                                  {Int64(4), NullString()})}),
      LanguageOptions{}));
  GOOGLESQL_ASSERT_OK(expr->SetSchemasForEvaluation({}));
  EXPECT_TRUE(expr->EvalSimple({}, &context, &result, &status));
  GOOGLESQL_ASSERT_OK(status);
  EXPECT_EQ(result.value().field(0).int64_value(), 2);
  EXPECT_THAT(
      result.value().field(1).elements(),
      UnorderedElementsAre(
          Property(&Value::fields, ElementsAre(Int64(2), NullString())),
          Property(&Value::fields, ElementsAre(Int64(4), NullString()))));
}

TEST_F(DMLValueExprEvalTest, DMLUpdateValueExpr) {
  // Build a resolved AST for updating str_val from null to 'unknown' in the
  // table.
  std::unique_ptr<const ResolvedTableScan> table_scan = MakeResolvedTableScan(
      {ResolvedColumn{1, googlesql::IdString::MakeGlobal("test_table"),
                      googlesql::IdString::MakeGlobal("int_val"), Int64Type()},
       ResolvedColumn{2, googlesql::IdString::MakeGlobal("test_table"),
                      googlesql::IdString::MakeGlobal("str_val"),
                      StringType()}},
      table(), /*for_system_time_expr=*/nullptr);
  std::vector<std::unique_ptr<const ResolvedUpdateItem>> update_item_list;
  update_item_list.push_back(MakeResolvedUpdateItem(
      MakeResolvedColumnRef(table_scan->column_list(1).type(),
                            table_scan->column_list(1),
                            /*is_correlated=*/false),
      MakeResolvedDMLValue(MakeResolvedLiteral(
          StringType(), String("unknown"), /*has_explicit_type=*/
          true, /*float_literal_id=*/0)),
      /*element_column=*/nullptr, /*array_update_list=*/{}, /*delete_list=*/{},
      /*update_list=*/{}, /*insert_list=*/{}));
  std::vector<std::unique_ptr<ResolvedColumnRef>> resolved_column_refs;
  resolved_column_refs.push_back(MakeResolvedColumnRef(
      table_scan->column_list(1).type(), table_scan->column_list(1),
      /*is_correlated=*/false));
  std::unique_ptr<ResolvedUpdateStmt> stmt = MakeResolvedUpdateStmt(
      std::move(table_scan), /*assert_rows_modified=*/nullptr,
      /*returning=*/nullptr, /*array_offset_column=*/nullptr,
      /*where_expr=*/
      MakeResolvedFunctionCall(BoolType(), function("$is_null"),
                               *function("$is_null")->GetSignature(0),
                               std::move(resolved_column_refs),
                               ResolvedFunctionCall::DEFAULT_ERROR_MODE),
      /*update_item_list=*/std::move(update_item_list), /*from_scan=*/nullptr);

  // Create output types.
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(
      const ArrayType* table_array_type,
      CreateTableArrayType(stmt->table_scan()->column_list(),
                           stmt->table_scan()->table()->IsValueTable(),
                           type_factory()));
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(
      const StructType* primary_key_type,
      CreatePrimaryKeyType(stmt->table_scan()->table(), type_factory()));
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(const StructType* dml_output_type,
                       CreateDMLOutputType(table_array_type, type_factory()));

  // Create a ColumnToVariableMapping.
  auto column_to_variable_mapping = std::make_unique<ColumnToVariableMapping>(
      std::make_unique<VariableGenerator>());

  // Build a ResolvedScanMap and a ResolvedExprMap from the AST.
  auto resolved_scan_map = std::make_unique<ResolvedScanMap>();
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(
      std::unique_ptr<ValueExpr> table_as_array_expr,
      TableAsArrayExpr::Create(stmt->table_scan()->table()->Name(),
                               table_array_type));
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(
      std::unique_ptr<RelationalOp> relation_op,
      ArrayScanOp::Create(
          /*element=*/VariableId(),
          /*position=*/VariableId(),
          {std::make_pair(column_to_variable_mapping->GetVariableNameFromColumn(
                              stmt->table_scan()->column_list()[0]),
                          0),
           std::make_pair(column_to_variable_mapping->GetVariableNameFromColumn(
                              stmt->table_scan()->column_list()[1]),
                          1)},
          std::move(table_as_array_expr)));
  (*resolved_scan_map)[stmt->table_scan()] = std::move(relation_op);
  auto resolved_expr_map = std::make_unique<ResolvedExprMap>();
  std::vector<std::unique_ptr<ValueExpr>> arguments{};
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(
      std::unique_ptr<DerefExpr> arg_expr,
      DerefExpr::Create(column_to_variable_mapping->GetVariableNameFromColumn(
                            stmt->where_expr()
                                ->GetAs<ResolvedFunctionCall>()
                                ->argument_list(0)
                                ->GetAs<ResolvedColumnRef>()
                                ->column()),
                        stmt->where_expr()
                            ->GetAs<ResolvedFunctionCall>()
                            ->argument_list(0)
                            ->GetAs<ResolvedColumnRef>()
                            ->column()
                            .type()));
  arguments.push_back(std::move(arg_expr));
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(
      FunctionKind function_kind,
      BuiltinFunctionCatalog::GetKindByName(stmt->where_expr()
                                                ->GetAs<ResolvedFunctionCall>()
                                                ->function()
                                                ->Name()));
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(
      std::unique_ptr<ValueExpr> function_call_expr,
      BuiltinScalarFunction::CreateCall(
          function_kind, LanguageOptions{},
          stmt->where_expr()->GetAs<ResolvedFunctionCall>()->type(),
          ConvertValueExprsToAlgebraArgs(std::move(arguments)),
          stmt->where_expr()->GetAs<ResolvedFunctionCall>()->error_mode()));
  (*resolved_expr_map)[stmt->where_expr()] = std::move(function_call_expr);
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(
      std::unique_ptr<DerefExpr> target_expr,
      DerefExpr::Create(column_to_variable_mapping->GetVariableNameFromColumn(
                            stmt->update_item_list(0)
                                ->target()
                                ->GetAs<ResolvedColumnRef>()
                                ->column()),
                        stmt->update_item_list(0)
                            ->target()
                            ->GetAs<ResolvedColumnRef>()
                            ->column()
                            .type()));
  (*resolved_expr_map)[stmt->update_item_list(0)->target()] =
      std::move(target_expr);

  GOOGLESQL_ASSERT_OK_AND_ASSIGN(std::unique_ptr<ConstExpr> set_value_expr,
                       ConstExpr::Create(stmt->update_item_list(0)
                                             ->set_value()
                                             ->value()
                                             ->GetAs<ResolvedLiteral>()
                                             ->value()));
  (*resolved_expr_map)[stmt->update_item_list(0)->set_value()->value()] =
      std::move(set_value_expr);
  // Touch the getter to pass CheckFieldsAccessed().
  ASSERT_EQ(stmt->update_item_list(0)->element_column(), nullptr);

  auto column_expr_map = std::make_unique<ColumnExprMap>();

  // Create the DMLUpdateValueExpr to be tested.
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(
      std::unique_ptr<DMLUpdateValueExpr> expr,
      DMLUpdateValueExpr::Create(
          stmt->table_scan()->table(), table_array_type,
          /*returning_array_type=*/nullptr, primary_key_type, dml_output_type,
          stmt.get(), &stmt->table_scan()->column_list(),
          /*returning_column_values=*/nullptr,
          std::move(column_to_variable_mapping), std::move(resolved_scan_map),
          std::move(resolved_expr_map), std::move(column_expr_map)));

  // Evaluate and check.
  TupleSlot result;
  absl::Status status;
  // the case for return_all_rows_for_dml = true is covered by the reference
  // implementation compliance tests
  EvaluationOptions options{};
  options.return_all_rows_for_dml = false;
  EvaluationContext context{options};
  GOOGLESQL_CHECK_OK(context.AddTableAsArray(
      "test_table", /*is_value_table=*/false,
      Value::Array(table_array_type,
                   {Value::Struct(table_array_type->element_type()->AsStruct(),
                                  {Int64(1), String("one")}),
                    Value::Struct(table_array_type->element_type()->AsStruct(),
                                  {Int64(2), NullString()}),
                    Value::Struct(table_array_type->element_type()->AsStruct(),
                                  {Int64(4), NullString()})}),
      LanguageOptions{}));
  GOOGLESQL_ASSERT_OK(expr->SetSchemasForEvaluation({}));
  EXPECT_TRUE(expr->EvalSimple({}, &context, &result, &status));
  GOOGLESQL_ASSERT_OK(status);
  EXPECT_EQ(result.value().field(0).int64_value(), 2);
  EXPECT_THAT(
      result.value().field(1).elements(),
      UnorderedElementsAre(
          Property(&Value::fields, ElementsAre(Int64(2), String("unknown"))),
          Property(&Value::fields, ElementsAre(Int64(4), String("unknown")))));
}

TEST_F(DMLValueExprEvalTest,
       DMLUpdateValueExprModifiesPrimaryKeyWhenDisallowed) {
  // Build a resolved AST for updating str_val from null to 'unknown' in the
  // table.
  std::unique_ptr<const ResolvedTableScan> table_scan = MakeResolvedTableScan(
      {ResolvedColumn{1, googlesql::IdString::MakeGlobal("test_table"),
                      googlesql::IdString::MakeGlobal("int_val"), Int64Type()},
       ResolvedColumn{2, googlesql::IdString::MakeGlobal("test_table"),
                      googlesql::IdString::MakeGlobal("str_val"),
                      StringType()}},
      table(), /*for_system_time_expr=*/nullptr);
  std::vector<std::unique_ptr<const ResolvedUpdateItem>> update_item_list;
  update_item_list.push_back(MakeResolvedUpdateItem(
      MakeResolvedColumnRef(table_scan->column_list(0).type(),
                            table_scan->column_list(0),
                            /*is_correlated=*/false),
      MakeResolvedDMLValue(MakeResolvedLiteral(Int64Type(),
                                               Int64(0), /*has_explicit_type=*/
                                               true, /*float_literal_id=*/0)),
      /*element_column=*/nullptr, /*update_item_element_list=*/{},
      /*delete_list=*/{}, /*update_list=*/{}, /*insert_list=*/{}));
  std::vector<std::unique_ptr<ResolvedColumnRef>> resolved_column_refs;
  resolved_column_refs.push_back(MakeResolvedColumnRef(
      table_scan->column_list(1).type(), table_scan->column_list(1),
      /*is_correlated=*/false));
  std::unique_ptr<ResolvedUpdateStmt> stmt = MakeResolvedUpdateStmt(
      std::move(table_scan), /*assert_rows_modified=*/nullptr,
      /*returning=*/nullptr, /*array_offset_column=*/nullptr,
      /*where_expr=*/
      MakeResolvedFunctionCall(BoolType(), function("$is_null"),
                               *function("$is_null")->GetSignature(0),
                               std::move(resolved_column_refs),
                               ResolvedFunctionCall::DEFAULT_ERROR_MODE),
      /*update_item_list=*/std::move(update_item_list), /*from_scan=*/nullptr);

  // Create output types.
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(
      const ArrayType* table_array_type,
      CreateTableArrayType(stmt->table_scan()->column_list(),
                           stmt->table_scan()->table()->IsValueTable(),
                           type_factory()));
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(
      const StructType* primary_key_type,
      CreatePrimaryKeyType(stmt->table_scan()->table(), type_factory()));
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(const StructType* dml_output_type,
                       CreateDMLOutputType(table_array_type, type_factory()));

  // Create a ColumnToVariableMapping.
  auto column_to_variable_mapping = std::make_unique<ColumnToVariableMapping>(
      std::make_unique<VariableGenerator>());

  // Build a ResolvedScanMap and a ResolvedExprMap from the AST.
  auto resolved_scan_map = std::make_unique<ResolvedScanMap>();
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(
      std::unique_ptr<ValueExpr> table_as_array_expr,
      TableAsArrayExpr::Create(stmt->table_scan()->table()->Name(),
                               table_array_type));
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(
      std::unique_ptr<RelationalOp> relation_op,
      ArrayScanOp::Create(
          /*element=*/VariableId(),
          /*position=*/VariableId(),
          {std::make_pair(column_to_variable_mapping->GetVariableNameFromColumn(
                              stmt->table_scan()->column_list()[0]),
                          0),
           std::make_pair(column_to_variable_mapping->GetVariableNameFromColumn(
                              stmt->table_scan()->column_list()[1]),
                          1)},
          std::move(table_as_array_expr)));
  (*resolved_scan_map)[stmt->table_scan()] = std::move(relation_op);
  auto resolved_expr_map = std::make_unique<ResolvedExprMap>();
  std::vector<std::unique_ptr<ValueExpr>> arguments{};
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(
      std::unique_ptr<DerefExpr> arg_expr,
      DerefExpr::Create(column_to_variable_mapping->GetVariableNameFromColumn(
                            stmt->where_expr()
                                ->GetAs<ResolvedFunctionCall>()
                                ->argument_list(0)
                                ->GetAs<ResolvedColumnRef>()
                                ->column()),
                        stmt->where_expr()
                            ->GetAs<ResolvedFunctionCall>()
                            ->argument_list(0)
                            ->GetAs<ResolvedColumnRef>()
                            ->column()
                            .type()));
  arguments.push_back(std::move(arg_expr));
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(
      FunctionKind function_kind,
      BuiltinFunctionCatalog::GetKindByName(stmt->where_expr()
                                                ->GetAs<ResolvedFunctionCall>()
                                                ->function()
                                                ->Name()));
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(
      std::unique_ptr<ValueExpr> function_call_expr,
      BuiltinScalarFunction::CreateCall(
          function_kind, LanguageOptions{},
          stmt->where_expr()->GetAs<ResolvedFunctionCall>()->type(),
          ConvertValueExprsToAlgebraArgs(std::move(arguments)),
          stmt->where_expr()->GetAs<ResolvedFunctionCall>()->error_mode()));
  (*resolved_expr_map)[stmt->where_expr()] = std::move(function_call_expr);
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(
      std::unique_ptr<DerefExpr> target_expr,
      DerefExpr::Create(column_to_variable_mapping->GetVariableNameFromColumn(
                            stmt->update_item_list(0)
                                ->target()
                                ->GetAs<ResolvedColumnRef>()
                                ->column()),
                        stmt->update_item_list(0)
                            ->target()
                            ->GetAs<ResolvedColumnRef>()
                            ->column()
                            .type()));
  (*resolved_expr_map)[stmt->update_item_list(0)->target()] =
      std::move(target_expr);

  GOOGLESQL_ASSERT_OK_AND_ASSIGN(std::unique_ptr<ConstExpr> set_value_expr,
                       ConstExpr::Create(stmt->update_item_list(0)
                                             ->set_value()
                                             ->value()
                                             ->GetAs<ResolvedLiteral>()
                                             ->value()));
  (*resolved_expr_map)[stmt->update_item_list(0)->set_value()->value()] =
      std::move(set_value_expr);
  // Touch the getter to pass CheckFieldsAccessed().
  ASSERT_EQ(stmt->update_item_list(0)->element_column(), nullptr);

  auto column_expr_map = std::make_unique<ColumnExprMap>();

  // Create the DMLUpdateValueExpr to be tested.
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(
      std::unique_ptr<DMLUpdateValueExpr> expr,
      DMLUpdateValueExpr::Create(
          stmt->table_scan()->table(), table_array_type,
          /*returning_array_type=*/nullptr, primary_key_type, dml_output_type,
          stmt.get(), &stmt->table_scan()->column_list(),
          /*returning_column_values=*/nullptr,
          std::move(column_to_variable_mapping), std::move(resolved_scan_map),
          std::move(resolved_expr_map), std::move(column_expr_map)));

  // Evaluate and check.
  TupleSlot result;
  absl::Status status;
  // the case for return_all_rows_for_dml = true is covered by the reference
  // implementation compliance tests
  EvaluationOptions options{};
  options.return_all_rows_for_dml = false;
  EvaluationContext context{options};
  LanguageOptions language_options;
  language_options.EnableLanguageFeature(FEATURE_DISALLOW_PRIMARY_KEY_UPDATES);
  context.SetLanguageOptions(language_options);
  GOOGLESQL_CHECK_OK(context.AddTableAsArray(
      "test_table", /*is_value_table=*/false,
      Value::Array(table_array_type,
                   {Value::Struct(table_array_type->element_type()->AsStruct(),
                                  {Int64(1), String("one")}),
                    Value::Struct(table_array_type->element_type()->AsStruct(),
                                  {Int64(2), NullString()}),
                    Value::Struct(table_array_type->element_type()->AsStruct(),
                                  {Int64(4), NullString()})}),
      language_options));
  GOOGLESQL_ASSERT_OK(expr->SetSchemasForEvaluation({}));
  EXPECT_FALSE(expr->EvalSimple({}, &context, &result, &status));
  EXPECT_THAT(status, StatusIs(absl::StatusCode::kOutOfRange,
                               HasSubstr("modify a primary key column")));
}

TEST_F(DMLValueExprEvalTest,
       DMLUpdateValueExprSetsPrimaryKeyValuesToNullWhenDisallowed) {
  // Build a resolved AST for updating str_val from null to 'unknown' in the
  // table.
  std::unique_ptr<const ResolvedTableScan> table_scan = MakeResolvedTableScan(
      {ResolvedColumn{1, googlesql::IdString::MakeGlobal("test_table"),
                      googlesql::IdString::MakeGlobal("int_val"), Int64Type()},
       ResolvedColumn{2, googlesql::IdString::MakeGlobal("test_table"),
                      googlesql::IdString::MakeGlobal("str_val"),
                      StringType()}},
      table(), /*for_system_time_expr=*/nullptr);
  std::vector<std::unique_ptr<const ResolvedUpdateItem>> update_item_list;
  update_item_list.push_back(MakeResolvedUpdateItem(
      MakeResolvedColumnRef(table_scan->column_list(0).type(),
                            table_scan->column_list(0),
                            /*is_correlated=*/false),
      MakeResolvedDMLValue(
          MakeResolvedLiteral(Int64Type(), NullInt64(), /*has_explicit_type=*/
                              true, /*float_literal_id=*/0)),
      /*element_column=*/nullptr, /*array_update_list=*/{}, /*delete_list=*/{},
      /*update_list=*/{}, /*insert_list=*/{}));
  std::vector<std::unique_ptr<ResolvedColumnRef>> resolved_column_refs;
  resolved_column_refs.push_back(MakeResolvedColumnRef(
      table_scan->column_list(1).type(), table_scan->column_list(1),
      /*is_correlated=*/false));
  std::unique_ptr<ResolvedUpdateStmt> stmt = MakeResolvedUpdateStmt(
      std::move(table_scan), /*assert_rows_modified=*/nullptr,
      /*returning=*/nullptr, /*array_offset_column=*/nullptr,
      /*where_expr=*/
      MakeResolvedFunctionCall(BoolType(), function("$is_null"),
                               *function("$is_null")->GetSignature(0),
                               std::move(resolved_column_refs),
                               ResolvedFunctionCall::DEFAULT_ERROR_MODE),
      /*update_item_list=*/std::move(update_item_list), /*from_scan=*/nullptr);

  // Create output types.
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(
      const ArrayType* table_array_type,
      CreateTableArrayType(stmt->table_scan()->column_list(),
                           stmt->table_scan()->table()->IsValueTable(),
                           type_factory()));
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(
      const StructType* primary_key_type,
      CreatePrimaryKeyType(stmt->table_scan()->table(), type_factory()));
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(const StructType* dml_output_type,
                       CreateDMLOutputType(table_array_type, type_factory()));

  // Create a ColumnToVariableMapping.
  auto column_to_variable_mapping = std::make_unique<ColumnToVariableMapping>(
      std::make_unique<VariableGenerator>());

  // Build a ResolvedScanMap and a ResolvedExprMap from the AST.
  auto resolved_scan_map = std::make_unique<ResolvedScanMap>();
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(
      std::unique_ptr<ValueExpr> table_as_array_expr,
      TableAsArrayExpr::Create(stmt->table_scan()->table()->Name(),
                               table_array_type));
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(
      std::unique_ptr<RelationalOp> relation_op,
      ArrayScanOp::Create(
          /*element=*/VariableId(),
          /*position=*/VariableId(),
          {std::make_pair(column_to_variable_mapping->GetVariableNameFromColumn(
                              stmt->table_scan()->column_list()[0]),
                          0),
           std::make_pair(column_to_variable_mapping->GetVariableNameFromColumn(
                              stmt->table_scan()->column_list()[1]),
                          1)},
          std::move(table_as_array_expr)));
  (*resolved_scan_map)[stmt->table_scan()] = std::move(relation_op);
  auto resolved_expr_map = std::make_unique<ResolvedExprMap>();
  std::vector<std::unique_ptr<ValueExpr>> arguments{};
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(
      std::unique_ptr<DerefExpr> arg_expr,
      DerefExpr::Create(column_to_variable_mapping->GetVariableNameFromColumn(
                            stmt->where_expr()
                                ->GetAs<ResolvedFunctionCall>()
                                ->argument_list(0)
                                ->GetAs<ResolvedColumnRef>()
                                ->column()),
                        stmt->where_expr()
                            ->GetAs<ResolvedFunctionCall>()
                            ->argument_list(0)
                            ->GetAs<ResolvedColumnRef>()
                            ->column()
                            .type()));
  arguments.push_back(std::move(arg_expr));
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(
      FunctionKind function_kind,
      BuiltinFunctionCatalog::GetKindByName(stmt->where_expr()
                                                ->GetAs<ResolvedFunctionCall>()
                                                ->function()
                                                ->Name()));
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(
      std::unique_ptr<ValueExpr> function_call_expr,
      BuiltinScalarFunction::CreateCall(
          function_kind, LanguageOptions{},
          stmt->where_expr()->GetAs<ResolvedFunctionCall>()->type(),
          ConvertValueExprsToAlgebraArgs(std::move(arguments)),
          stmt->where_expr()->GetAs<ResolvedFunctionCall>()->error_mode()));
  (*resolved_expr_map)[stmt->where_expr()] = std::move(function_call_expr);
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(
      std::unique_ptr<DerefExpr> target_expr,
      DerefExpr::Create(column_to_variable_mapping->GetVariableNameFromColumn(
                            stmt->update_item_list(0)
                                ->target()
                                ->GetAs<ResolvedColumnRef>()
                                ->column()),
                        stmt->update_item_list(0)
                            ->target()
                            ->GetAs<ResolvedColumnRef>()
                            ->column()
                            .type()));
  (*resolved_expr_map)[stmt->update_item_list(0)->target()] =
      std::move(target_expr);

  GOOGLESQL_ASSERT_OK_AND_ASSIGN(std::unique_ptr<ConstExpr> set_value_expr,
                       ConstExpr::Create(stmt->update_item_list(0)
                                             ->set_value()
                                             ->value()
                                             ->GetAs<ResolvedLiteral>()
                                             ->value()));
  (*resolved_expr_map)[stmt->update_item_list(0)->set_value()->value()] =
      std::move(set_value_expr);
  // Touch the getter to pass CheckFieldsAccessed().
  ASSERT_EQ(stmt->update_item_list(0)->element_column(), nullptr);

  auto column_expr_map = std::make_unique<ColumnExprMap>();

  // Create the DMLUpdateValueExpr to be tested.
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(
      std::unique_ptr<DMLUpdateValueExpr> expr,
      DMLUpdateValueExpr::Create(
          stmt->table_scan()->table(), table_array_type,
          /*returning_array_type=*/nullptr, primary_key_type, dml_output_type,
          stmt.get(), &stmt->table_scan()->column_list(),
          /*returning_column_values=*/nullptr,
          std::move(column_to_variable_mapping), std::move(resolved_scan_map),
          std::move(resolved_expr_map), std::move(column_expr_map)));

  // Evaluate and check.
  TupleSlot result;
  absl::Status status;
  // the case for return_all_rows_for_dml = true is covered by the reference
  // implementation compliance tests
  EvaluationOptions options{};
  options.return_all_rows_for_dml = false;
  EvaluationContext context{options};
  LanguageOptions language_options;
  language_options.EnableLanguageFeature(FEATURE_DISALLOW_NULL_PRIMARY_KEYS);
  context.SetLanguageOptions(language_options);
  GOOGLESQL_CHECK_OK(context.AddTableAsArray(
      "test_table", /*is_value_table=*/false,
      Value::Array(table_array_type,
                   {Value::Struct(table_array_type->element_type()->AsStruct(),
                                  {Int64(1), String("one")}),
                    Value::Struct(table_array_type->element_type()->AsStruct(),
                                  {Int64(2), NullString()}),
                    Value::Struct(table_array_type->element_type()->AsStruct(),
                                  {Int64(4), NullString()})}),
      language_options));
  GOOGLESQL_ASSERT_OK(expr->SetSchemasForEvaluation({}));
  EXPECT_FALSE(expr->EvalSimple({}, &context, &result, &status));
  EXPECT_THAT(status, StatusIs(absl::StatusCode::kOutOfRange,
                               HasSubstr("set a primary key column to NULL")));
}

// TODO: Many of the tests that use this fixture should probably be
// moved to public/proto_util_test.cc.
class ProtoEvalTest : public ::testing::Test {
 public:
  ProtoEvalTest() : type_factory_(test_values::static_type_factory()) {}
  ~ProtoEvalTest() override = default;

  const ProtoType* MakeProtoType(const google::protobuf::Message* msg) {
    const ProtoType* proto_type;
    GOOGLESQL_CHECK_OK(type_factory_->MakeProtoType(msg->GetDescriptor(), &proto_type));
    return proto_type;
  }

  // Reads 'field_name' of 'msg' using a GetProtoFieldExpr. Crashes on error.
  Value GetProtoFieldOrDie(const google::protobuf::Message* msg,
                           absl::string_view field_name) {
    return GetProtoField(msg, field_name).value();
  }

  // Reads 'field_name' of 'proto_value' using a GetProtoFieldExpr. Crashes on
  // error.
  Value GetProtoFieldOrDie(const Value& proto_value,
                           absl::string_view field_name) {
    return GetProtoField(proto_value, field_name).value();
  }

  // Reads 'field_name' of 'msg' using a GetProtoFieldExpr.
  absl::StatusOr<Value> GetProtoField(const google::protobuf::Message* msg,
                                      absl::string_view field_name) {
    const ProtoType* proto_type = MakeProtoType(msg);
    absl::Cord bytes;
    ABSL_CHECK(msg->SerializePartialToCord(&bytes));
    return GetProtoField(Value::Proto(proto_type, bytes), field_name);
  }

  // Reads 'field_name' of 'proto_value' using a GetProtoFieldExpr.
  absl::StatusOr<Value> GetProtoField(const Value& proto_value,
                                      absl::string_view field_name) {
    TupleSlot proto_slot;
    proto_slot.SetValue(proto_value);
    EvaluationContext context((EvaluationOptions()));
    GOOGLESQL_ASSIGN_OR_RETURN(TupleSlot output_slot,
                     EvalGetProtoFieldExpr(proto_slot, field_name,
                                           /*get_has_bit=*/false, &context));
    return output_slot.value();
  }

  // Checks presence of 'field_name' in 'msg' using a GetProtoFieldExpr. Crashes
  // on error.
  Value HasProtoFieldOrDie(const google::protobuf::Message* msg,
                           absl::string_view field_name) {
    const ProtoType* proto_type = MakeProtoType(msg);
    absl::Cord bytes;
    ABSL_CHECK(msg->SerializePartialToCord(&bytes));
    return HasProtoFieldOrDie(Value::Proto(proto_type, bytes), field_name);
  }

  // Checks presence of 'field_name' in 'proto_value' using a
  // GetProtoFieldExpr. Crashes on error.
  Value HasProtoFieldOrDie(const Value& proto_value,
                           absl::string_view field_name) {
    TupleSlot proto_slot;
    proto_slot.SetValue(proto_value);
    EvaluationContext context((EvaluationOptions()));
    return EvalGetProtoFieldExpr(proto_slot, field_name, /*get_has_bit=*/true,
                                 &context)
        .value()
        .value();
  }

  // Creates a GetProtoFieldExpr that either accesses or checks the presence of
  // 'field_name'. Invokes it on 'proto_slot' by passing 'proto_slot' in
  // parameter 'p'.
  absl::StatusOr<TupleSlot> EvalGetProtoFieldExpr(const TupleSlot& proto_slot,
                                                  absl::string_view field_name,
                                                  bool get_has_bit,
                                                  EvaluationContext* context) {
    const google::protobuf::FieldDescriptor* field_descr =
        proto_slot.value().type()->AsProto()->descriptor()->FindFieldByName(
            field_name);
    GOOGLESQL_RET_CHECK(field_descr != nullptr);
    const Type* field_type;
    Value default_value;
    GOOGLESQL_RET_CHECK_OK(GetProtoFieldTypeAndDefault(
        {}, field_descr, /*catalog_name_path=*/{}, type_factory_, &field_type,
        &default_value));

    ProtoFieldAccessInfo access_info;
    access_info.field_info.descriptor = field_descr;
    access_info.field_info.type = field_type;
    access_info.field_info.format = ProtoType::GetFormatAnnotation(field_descr);
    access_info.field_info.get_has_bit = get_has_bit;
    access_info.field_info.default_value = default_value;

    std::vector<absl::StatusOr<TupleSlot>> output_slots;
    GOOGLESQL_RETURN_IF_ERROR(EvalGetProtoFieldExprs({std::make_pair(&access_info, 1)},
                                           {proto_slot}, context,
                                           &output_slots));
    GOOGLESQL_RET_CHECK_EQ(output_slots.size(), 1);
    return output_slots[0];
  }

  // Creates a single FieldRegistry for 'infos'. For each (ProtoFieldAccessInfo
  // info, int count) pair, there is a ProtoFieldReader wrapped by 'count'
  // GetProtoFieldExprs. For each TupleSlot in 'proto_slots', evaluates all of
  // the ProtoFieldExprs and appends the results to 'output_slots'. Tests that
  // GetProtoFieldExpr::Eval() only reads from the proto when it should. The
  // actual proto value is represented by a parameter.
  absl::Status EvalGetProtoFieldExprs(
      std::vector<std::pair<const ProtoFieldAccessInfo*, int>> infos,
      absl::Span<const TupleSlot> proto_slots, EvaluationContext* context,
      std::vector<absl::StatusOr<TupleSlot>>* output_slots) {
    GOOGLESQL_RET_CHECK(!proto_slots.empty());

    context->set_populate_last_get_field_value_call_read_fields_from_proto_map(
        true);

    // Build the GetProtoFieldExprs.
    VariableId p("p");
    std::unique_ptr<ProtoFieldRegistry> registry;
    std::vector<std::unique_ptr<ProtoFieldReader>> field_readers;
    std::vector<std::unique_ptr<GetProtoFieldExpr>> exprs;
    GOOGLESQL_RETURN_IF_ERROR(CreateGetProtoFieldExprs(
        infos, p, proto_slots[0].value().type()->AsProto(), context, &registry,
        &field_readers, &exprs));

    // Evaluate the GetProtoFieldExprs as described in the method comment.
    for (const TupleSlot& proto_slot : proto_slots) {
      const TupleData params({proto_slot});
      for (int expr_idx = 0; expr_idx < exprs.size(); ++expr_idx) {
        const GetProtoFieldExpr& expr = *exprs[expr_idx];

        TupleSlot output_slot;
        absl::Status status;
        if (!expr.EvalSimple({&params}, context, &output_slot, &status)) {
          return status;
        }
        if (TupleSlot::ShouldStoreSharedProtoStateFor(
                output_slot.value().type_kind())) {
          EXPECT_EQ(proto_slot.mutable_shared_proto_state()->get(),
                    output_slot.mutable_shared_proto_state()->get());
        }
        output_slots->push_back(output_slot);

        bool expect_to_read_fields;
        if (proto_slot.value().is_null()) {
          // We can't read fields from a NULL proto.
          expect_to_read_fields = false;
        } else if (context->options().store_proto_field_value_maps) {
          // We should only read from the proto the first time.
          expect_to_read_fields = (expr_idx == 0);
        } else {
          // We have to read fields from non-NULL protos every time.
          expect_to_read_fields = true;
        }

        const bool read_fields =
            context->last_get_field_value_call_read_fields_from_proto(
                expr.field_reader());
        EXPECT_EQ(read_fields, expect_to_read_fields)
            << "proto_value:\n"
            << proto_slot.value() << "\nexpr_idx = " << expr_idx;
      }
    }

    return absl::OkStatus();
  }

  // Creates a single ProtoFieldAccessInfo for 'infos', and for each
  // (ProtoFieldAccessInfo info, int count) pair, creates a ProtoFieldReader
  // wrapped by 'count' GetProtoFieldExprs.
  absl::Status CreateGetProtoFieldExprs(
      std::vector<std::pair<const ProtoFieldAccessInfo*, int>> infos,
      const VariableId& variable_id, const ProtoType* type,
      EvaluationContext* context, std::unique_ptr<ProtoFieldRegistry>* registry,
      std::vector<std::unique_ptr<ProtoFieldReader>>* field_readers,
      std::vector<std::unique_ptr<GetProtoFieldExpr>>* exprs) {
    *registry = std::make_unique<ProtoFieldRegistry>();

    for (const auto& entry : infos) {
      const ProtoFieldAccessInfo* info = entry.first;
      const int count = entry.second;

      field_readers->push_back(
          std::make_unique<ProtoFieldReader>(*info, registry->get()));
      ProtoFieldReader* field_reader = field_readers->back().get();

      for (int i = 0; i < count; ++i) {
        GOOGLESQL_ASSIGN_OR_RETURN(auto deref_expr, DerefExpr::Create(variable_id, type));
        GOOGLESQL_ASSIGN_OR_RETURN(
            auto get_proto_field_expr,
            GetProtoFieldExpr::Create(std::move(deref_expr), field_reader));

        const TupleSchema params_schema({variable_id});
        GOOGLESQL_RETURN_IF_ERROR(
            get_proto_field_expr->SetSchemasForEvaluation({&params_schema}));

        std::string field_name(info->field_info.descriptor->name());
        if (info->field_info.get_has_bit) {
          field_name = absl::StrCat("has_", field_name);
        }
        EXPECT_EQ(get_proto_field_expr->DebugString(),
                  absl::StrCat("GetProtoFieldExpr(", field_name, ", $p)"));

        exprs->push_back(std::move(get_proto_field_expr));
      }
    }

    return absl::OkStatus();
  }

  absl::Status MakeProto(absl::Span<const std::pair<std::string, Value>> fields,
                         google::protobuf::Message* out) {
    std::vector<MakeProtoFunction::FieldAndFormat> field_and_formats;
    std::vector<Value> values;
    std::vector<std::unique_ptr<ValueExpr>> arguments;
    for (const auto& p : fields) {
      const std::string& field_name = p.first;
      const Value& value = p.second;
      const auto descr = out->GetDescriptor()->FindFieldByName(field_name);
      ABSL_CHECK(descr != nullptr)
          << "No field '" << field_name << "' in proto of type "
          << out->GetDescriptor()->full_name();
      field_and_formats.emplace_back(descr,
                                     ProtoType::GetFormatAnnotation(descr));
      values.push_back(value);
      GOOGLESQL_ASSIGN_OR_RETURN(auto const_value, ConstExpr::Create(value));
      arguments.push_back(std::move(const_value));
    }
    GOOGLESQL_ASSIGN_OR_RETURN(auto fct_op,
                     ScalarFunctionCallExpr::Create(
                         std::make_unique<MakeProtoFunction>(MakeProtoType(out),
                                                             field_and_formats),
                         std::move(arguments)));
    GOOGLESQL_ASSIGN_OR_RETURN(Value result, EvalExpr(*fct_op, EmptyParams()));
    ABSL_CHECK(result.type()->IsProto());
    out->Clear();
    if (!result.is_null()) {
      EXPECT_TRUE(out->ParsePartialFromCord(result.ToCord()))
          << result.FullDebugString();
    }
    return absl::OkStatus();
  }

  Value MakeProtoValue(absl::Span<const std::pair<std::string, Value>> fields,
                       const google::protobuf::Message& msg) {
    std::unique_ptr<google::protobuf::Message> tmp(msg.New());
    GOOGLESQL_CHECK_OK(MakeProto(fields, tmp.get()));
    absl::Cord bytes;
    ABSL_CHECK(tmp->SerializePartialToCord(&bytes));
    return Value::Proto(MakeProtoType(&msg), bytes);
  }

  // 'out' is cleared first.
  std::string FormatProto(
      absl::Span<const std::pair<std::string, Value>> fields,
      google::protobuf::Message* out) {
    absl::Status result = MakeProto(fields, out);
    if (result.ok()) {
      return out->ShortDebugString();
    } else {
      return googlesql::internal::StatusToString(result);
    }
  }

  // 'out' is cleared first.
  std::string FormatProto(const std::string& field_name, const Value& value,
                          google::protobuf::Message* out) {
    return FormatProto({{field_name, value}}, out);
  }

 protected:
  TypeFactory* type_factory_;
};

TEST_F(ProtoEvalTest, CreatePrimitiveProtoFields) {
  googlesql_test::KitchenSinkPB p;
  // int32
  EXPECT_EQ("int32_val: -5", FormatProto("int32_val", Int32(-5), &p));
  EXPECT_EQ("", FormatProto("int32_val", NullInt32(), &p));
  EXPECT_THAT(FormatProto("int32_val", Uint32(0), &p),
              HasSubstr("out_of_range"));
  EXPECT_THAT(FormatProto("int32_val", Array({Int32(0)}), &p),
              HasSubstr("out_of_range"));
  EXPECT_THAT(
      FormatProto("int32_val", Value::EmptyArray(types::Int32ArrayType()), &p),
      HasSubstr("out_of_range"));
  // uint32
  EXPECT_EQ("uint32_val: 5", FormatProto("uint32_val", Uint32(5), &p));
  EXPECT_EQ("", FormatProto("uint32_val", NullUint32(), &p));
  EXPECT_THAT(FormatProto("uint32_val", Int32(0), &p),
              HasSubstr("out_of_range"));

  // int64
  EXPECT_EQ("int64_val: -5", FormatProto("int64_val", Int64(-5), &p));
  EXPECT_EQ("", FormatProto("int64_val", NullInt64(), &p));
  EXPECT_THAT(FormatProto("int64_val", Int32(0), &p),
              HasSubstr("out_of_range"));

  // uint64
  EXPECT_EQ("uint64_val: 5", FormatProto("uint64_val", Uint64(5), &p));
  EXPECT_EQ("", FormatProto("uint64_val", NullUint64(), &p));
  EXPECT_THAT(FormatProto("uint64_val", Int32(0), &p),
              HasSubstr("out_of_range"));

  // bool
  EXPECT_EQ("bool_val: true", FormatProto("bool_val", True(), &p));
  EXPECT_EQ("", FormatProto("bool_val", NullBool(), &p));
  EXPECT_THAT(FormatProto("bool_val", Int32(1), &p), HasSubstr("out_of_range"));

  // float
  EXPECT_EQ("float_val: 0.3", FormatProto("float_val", Float(0.3), &p));
  EXPECT_EQ("", FormatProto("float_val", NullFloat(), &p));
  EXPECT_THAT(FormatProto("float_val", Double(0.3), &p),
              HasSubstr("out_of_range"));

  // double
  EXPECT_EQ("double_val: 0.5", FormatProto("double_val", Double(0.5), &p));
  EXPECT_EQ("", FormatProto("double_val", NullDouble(), &p));
  EXPECT_THAT(FormatProto("double_val", Float(0.5), &p),
              HasSubstr("out_of_range"));

  // string
  EXPECT_EQ("string_val: \"@\"", FormatProto("string_val", String("@"), &p));
  EXPECT_EQ("", FormatProto("string_val", NullString(), &p));
  EXPECT_THAT(FormatProto("string_val", Bytes("@"), &p),
              HasSubstr("out_of_range"));

  // bytes
  EXPECT_EQ("bytes_val: \"@\"", FormatProto("bytes_val", Bytes("@"), &p));
  EXPECT_EQ("", FormatProto("bytes_val", NullBytes(), &p));
  EXPECT_THAT(FormatProto("bytes_val", String("@"), &p),
              HasSubstr("out_of_range"));

  // fixed32_val
  EXPECT_EQ("fixed32_val: 5", FormatProto("fixed32_val", Uint32(5), &p));
  EXPECT_EQ("", FormatProto("fixed32_val", NullUint32(), &p));
  EXPECT_THAT(FormatProto("fixed32_val", Int32(0), &p),
              HasSubstr("out_of_range"));

  // fixed64_val
  EXPECT_EQ("fixed64_val: 5", FormatProto("fixed64_val", Uint64(5), &p));
  EXPECT_EQ("", FormatProto("fixed64_val", NullUint64(), &p));
  EXPECT_THAT(FormatProto("fixed64_val", Int64(0), &p),
              HasSubstr("out_of_range"));

  // sfixed32_val
  EXPECT_EQ("sfixed32_val: -5", FormatProto("sfixed32_val", Int32(-5), &p));
  EXPECT_EQ("", FormatProto("sfixed32_val", NullInt32(), &p));
  EXPECT_THAT(FormatProto("sfixed32_val", Uint32(0), &p),
              HasSubstr("out_of_range"));

  // sfixed64_val
  EXPECT_EQ("sfixed64_val: -5", FormatProto("sfixed64_val", Int64(-5), &p));
  EXPECT_EQ("", FormatProto("sfixed64_val", NullInt64(), &p));
  EXPECT_THAT(FormatProto("sfixed64_val", Uint64(0), &p),
              HasSubstr("out_of_range"));

  // sint32_val
  EXPECT_EQ("sint32_val: -5", FormatProto("sint32_val", Int32(-5), &p));
  EXPECT_EQ("", FormatProto("sint32_val", NullInt32(), &p));
  EXPECT_THAT(FormatProto("sint32_val", Uint32(0), &p),
              HasSubstr("out_of_range"));

  // sint64_val
  EXPECT_EQ("sint64_val: -5", FormatProto("sint64_val", Int64(-5), &p));
  EXPECT_EQ("", FormatProto("sint64_val", NullInt64(), &p));
  EXPECT_THAT(FormatProto("sint64_val", Uint64(0), &p),
              HasSubstr("out_of_range"));

  // Required field.
  EXPECT_EQ("int64_key_1: -5", FormatProto("int64_key_1", Int64(-5), &p));
  EXPECT_THAT(FormatProto("int64_key_1", NullInt64(), &p),
              HasSubstr("required"));

  // Several fields.
  EXPECT_EQ("int32_val: 1 uint32_val: 2 int64_val: 3 uint64_val: 4",
            FormatProto(
                {
                    {"int32_val", Int32(1)},
                    {"uint32_val", Uint32(2)},
                    {"int64_val", Int64(3)},
                    {"uint64_val", Uint64(4)},
                },
                &p));
}

TEST_F(ProtoEvalTest, CreateRepeatedProtoFields) {
  googlesql_test::KitchenSinkPB p;

  // repeated_int32_val
  EXPECT_EQ(
      "repeated_int32_val: -5 repeated_int32_val: -7",
      FormatProto("repeated_int32_val", Array({Int32(-5), Int32(-7)}), &p));
  EXPECT_EQ("", FormatProto("repeated_int32_val",
                            Value::EmptyArray(types::Int32ArrayType()), &p));
  EXPECT_EQ("", FormatProto("repeated_int32_val",
                            Value::Null(types::Int32ArrayType()), &p));
  EXPECT_THAT(FormatProto("repeated_int32_val", Array({Uint32(0)}), &p),
              HasSubstr("out_of_range"));
  EXPECT_THAT(FormatProto("repeated_int32_val", Int32(0), &p),
              HasSubstr("out_of_range"));

  // repeated_uint32_val
  EXPECT_EQ(
      "repeated_uint32_val: 5 repeated_uint32_val: 7",
      FormatProto("repeated_uint32_val", Array({Uint32(5), Uint32(7)}), &p));
  EXPECT_EQ("", FormatProto("repeated_uint32_val",
                            Value::EmptyArray(types::Uint32ArrayType()), &p));
  EXPECT_EQ("", FormatProto("repeated_uint32_val",
                            Value::Null(types::Uint32ArrayType()), &p));
  EXPECT_THAT(FormatProto("repeated_uint32_val", Array({Int32(0)}), &p),
              HasSubstr("out_of_range"));

  // repeated_int64_val
  EXPECT_EQ(
      "repeated_int64_val: -5 repeated_int64_val: -7",
      FormatProto("repeated_int64_val", Array({Int64(-5), Int64(-7)}), &p));
  EXPECT_EQ("", FormatProto("repeated_int64_val",
                            Value::EmptyArray(types::Int64ArrayType()), &p));
  EXPECT_EQ("", FormatProto("repeated_int64_val",
                            Value::Null(types::Int64ArrayType()), &p));
  EXPECT_THAT(FormatProto("repeated_int64_val", Array({Uint64(0)}), &p),
              HasSubstr("out_of_range"));

  // repeated_uint64_val
  EXPECT_EQ(
      "repeated_uint64_val: 5 repeated_uint64_val: 7",
      FormatProto("repeated_uint64_val", Array({Uint64(5), Uint64(7)}), &p));
  EXPECT_EQ("", FormatProto("repeated_uint64_val",
                            Value::EmptyArray(types::Uint64ArrayType()), &p));
  EXPECT_EQ("", FormatProto("repeated_uint64_val",
                            Value::Null(types::Uint64ArrayType()), &p));
  EXPECT_THAT(FormatProto("repeated_uint64_val", Array({Int64(0)}), &p),
              HasSubstr("out_of_range"));

  // repeated_bool_val
  EXPECT_EQ("repeated_bool_val: true repeated_bool_val: false",
            FormatProto("repeated_bool_val", Array({True(), False()}), &p));
  EXPECT_EQ("", FormatProto("repeated_bool_val",
                            Value::EmptyArray(types::BoolArrayType()), &p));
  EXPECT_EQ("", FormatProto("repeated_bool_val",
                            Value::Null(types::BoolArrayType()), &p));
  EXPECT_THAT(FormatProto("repeated_bool_val", Array({Int64(0)}), &p),
              HasSubstr("out_of_range"));

  // repeated_float_val
  EXPECT_EQ(
      "repeated_float_val: 0.3 repeated_float_val: 0.5",
      FormatProto("repeated_float_val", Array({Float(0.3), Float(0.5)}), &p));
  EXPECT_EQ("", FormatProto("repeated_float_val",
                            Value::EmptyArray(types::FloatArrayType()), &p));
  EXPECT_EQ("", FormatProto("repeated_float_val",
                            Value::Null(types::FloatArrayType()), &p));
  EXPECT_THAT(FormatProto("repeated_float_val", Array({Double(0)}), &p),
              HasSubstr("out_of_range"));

  // repeated_double_val
  EXPECT_EQ("repeated_double_val: 0.3 repeated_double_val: 0.5",
            FormatProto("repeated_double_val",
                        Array({Double(0.3), Double(0.5)}), &p));
  EXPECT_EQ("", FormatProto("repeated_double_val",
                            Value::EmptyArray(types::DoubleArrayType()), &p));
  EXPECT_EQ("", FormatProto("repeated_double_val",
                            Value::Null(types::DoubleArrayType()), &p));
  EXPECT_THAT(FormatProto("repeated_double_val", Array({Float(0)}), &p),
              HasSubstr("out_of_range"));

  // repeated_string_val
  EXPECT_EQ(
      "repeated_string_val: \"@\" repeated_string_val: \"\"",
      FormatProto("repeated_string_val", Array({String("@"), String("")}), &p));
  EXPECT_EQ("", FormatProto("repeated_string_val",
                            Value::EmptyArray(types::StringArrayType()), &p));
  EXPECT_EQ("", FormatProto("repeated_string_val",
                            Value::Null(types::StringArrayType()), &p));
  EXPECT_THAT(FormatProto("repeated_string_val", Array({Bytes("")}), &p),
              HasSubstr("out_of_range"));

  // repeated_bytes_val
  EXPECT_EQ(
      "repeated_bytes_val: \"@\" repeated_bytes_val: \"\"",
      FormatProto("repeated_bytes_val", Array({Bytes("@"), Bytes("")}), &p));
  EXPECT_EQ("", FormatProto("repeated_bytes_val",
                            Value::EmptyArray(types::BytesArrayType()), &p));
  EXPECT_EQ("", FormatProto("repeated_bytes_val",
                            Value::Null(types::BytesArrayType()), &p));
  EXPECT_THAT(FormatProto("repeated_bytes_val", Array({String("")}), &p),
              HasSubstr("out_of_range"));

  // repeated_fixed32_val
  EXPECT_EQ(
      "repeated_fixed32_val: 5 repeated_fixed32_val: 7",
      FormatProto("repeated_fixed32_val", Array({Uint32(5), Uint32(7)}), &p));
  EXPECT_EQ("", FormatProto("repeated_fixed32_val",
                            Value::EmptyArray(types::Uint32ArrayType()), &p));
  EXPECT_EQ("", FormatProto("repeated_fixed32_val",
                            Value::Null(types::Uint32ArrayType()), &p));
  EXPECT_THAT(FormatProto("repeated_fixed32_val", Array({Int32(0)}), &p),
              HasSubstr("out_of_range"));

  // repeated_fixed64_val
  EXPECT_EQ(
      "repeated_fixed64_val: 5 repeated_fixed64_val: 7",
      FormatProto("repeated_fixed64_val", Array({Uint64(5), Uint64(7)}), &p));
  EXPECT_EQ("", FormatProto("repeated_fixed64_val",
                            Value::EmptyArray(types::Uint64ArrayType()), &p));
  EXPECT_EQ("", FormatProto("repeated_fixed64_val",
                            Value::Null(types::Uint64ArrayType()), &p));
  EXPECT_THAT(FormatProto("repeated_fixed64_val", Array({Int64(0)}), &p),
              HasSubstr("out_of_range"));

  // repeated_sfixed32_val
  EXPECT_EQ(
      "repeated_sfixed32_val: -5 repeated_sfixed32_val: -7",
      FormatProto("repeated_sfixed32_val", Array({Int32(-5), Int32(-7)}), &p));
  EXPECT_EQ("", FormatProto("repeated_sfixed32_val",
                            Value::EmptyArray(types::Int32ArrayType()), &p));
  EXPECT_EQ("", FormatProto("repeated_sfixed32_val",
                            Value::Null(types::Int32ArrayType()), &p));
  EXPECT_THAT(FormatProto("repeated_sfixed32_val", Array({Uint32(0)}), &p),
              HasSubstr("out_of_range"));

  // repeated_sfixed64_val
  EXPECT_EQ(
      "repeated_sfixed64_val: -5 repeated_sfixed64_val: -7",
      FormatProto("repeated_sfixed64_val", Array({Int64(-5), Int64(-7)}), &p));
  EXPECT_EQ("", FormatProto("repeated_sfixed64_val",
                            Value::EmptyArray(types::Int64ArrayType()), &p));
  EXPECT_EQ("", FormatProto("repeated_sfixed64_val",
                            Value::Null(types::Int64ArrayType()), &p));
  EXPECT_THAT(FormatProto("repeated_sfixed64_val", Array({Uint64(0)}), &p),
              HasSubstr("out_of_range"));

  // repeated_sint32_val
  EXPECT_EQ(
      "repeated_sint32_val: -5 repeated_sint32_val: -7",
      FormatProto("repeated_sint32_val", Array({Int32(-5), Int32(-7)}), &p));
  EXPECT_EQ("", FormatProto("repeated_sint32_val",
                            Value::EmptyArray(types::Int32ArrayType()), &p));
  EXPECT_EQ("", FormatProto("repeated_sint32_val",
                            Value::Null(types::Int32ArrayType()), &p));
  EXPECT_THAT(FormatProto("repeated_sint32_val", Array({Uint32(0)}), &p),
              HasSubstr("out_of_range"));

  // repeated_sint64_val
  EXPECT_EQ(
      "repeated_sint64_val: -5 repeated_sint64_val: -7",
      FormatProto("repeated_sint64_val", Array({Int64(-5), Int64(-7)}), &p));
  EXPECT_EQ("", FormatProto("repeated_sint64_val",
                            Value::EmptyArray(types::Int64ArrayType()), &p));
  EXPECT_EQ("", FormatProto("repeated_sint64_val",
                            Value::Null(types::Int64ArrayType()), &p));
  EXPECT_THAT(FormatProto("repeated_sint64_val", Array({Uint64(0)}), &p),
              HasSubstr("out_of_range"));
}

TEST_F(ProtoEvalTest, CreateNestedProtoFields) {
  googlesql_test::KitchenSinkPB p;
  googlesql_test::KitchenSinkPB::Nested nested_pb;
  Value nested_value1 = MakeProtoValue({{"nested_int64", Int64(5)}}, nested_pb);
  Value nested_value2 = MakeProtoValue({{"nested_int64", Int64(7)}}, nested_pb);

  // nested_value
  EXPECT_EQ("nested_value { nested_int64: 5 }",
            FormatProto("nested_value", nested_value1, &p));
  EXPECT_THAT(FormatProto("nested_value", Uint32(0), &p),
              HasSubstr("out_of_range"));

  // nested_repeated_value
  EXPECT_EQ(
      "nested_repeated_value { nested_int64: 5 } "
      "nested_repeated_value { nested_int64: 7 }",
      FormatProto("nested_repeated_value",
                  Array({nested_value1, nested_value2}), &p));
  EXPECT_THAT(FormatProto("nested_repeated_value", Array({Uint32(0)}), &p),
              HasSubstr("out_of_range"));

  // repeated_holder
  googlesql_test::RepeatedHolderPB holder_pb;
  googlesql_test::TestExtraPB extra_pb;
  Value holder_value = MakeProtoValue(
      {{"repeated_field",
        Array({MakeProtoValue({{"int32_val1", Int32(5)}}, extra_pb)})}},
      holder_pb);
  EXPECT_EQ(
      "repeated_holder { repeated_field { int32_val1: 5 } } "
      "repeated_holder { repeated_field { int32_val1: 5 } }",
      FormatProto("repeated_holder", Array({holder_value, holder_value}), &p));
  EXPECT_EQ(
      "", FormatProto(
              "repeated_holder",
              Value::EmptyArray(Array({holder_value}).type()->AsArray()), &p));
  EXPECT_EQ("", FormatProto(
                    "repeated_holder",
                    Value::Null(Array({holder_value}).type()->AsArray()), &p));
  EXPECT_THAT(FormatProto("repeated_holder", Array({Uint32(0)}), &p),
              HasSubstr("out_of_range"));

  // optional_group
  googlesql_test::KitchenSinkPB_OptionalGroup group_pb;
  Value group_value = MakeProtoValue({{"int64_val", Int64(5)}}, group_pb);
  EXPECT_EQ("optional_group { int64_val: 5 }",
            FormatProto("optional_group", group_value, &p));
  EXPECT_THAT(FormatProto("optional_group", Uint32(0), &p),
              HasSubstr("out_of_range"));

  // nested_repeated_group
  googlesql_test::KitchenSinkPB_NestedRepeatedGroup nested_group_pb;
  Value nested_group1 = MakeProtoValue({{"id", Int64(5)}}, nested_group_pb);
  Value nested_group2 = MakeProtoValue({{"id", Int64(7)}}, nested_group_pb);
  EXPECT_EQ(
      "nested_repeated_group { id: 5 } "
      "nested_repeated_group { id: 7 }",
      FormatProto("nested_repeated_group",
                  Array({nested_group1, nested_group2}), &p));
  EXPECT_EQ(
      "", FormatProto(
              "nested_repeated_group",
              Value::EmptyArray(Array({nested_group1}).type()->AsArray()), &p));
  EXPECT_EQ("", FormatProto(
                    "nested_repeated_group",
                    Value::Null(Array({nested_group1}).type()->AsArray()), &p));
  EXPECT_THAT(FormatProto("nested_repeated_group", Array({Uint32(0)}), &p),
              HasSubstr("out_of_range"));
}

TEST_F(ProtoEvalTest, CreateEnumProtoFields) {
  googlesql_test::KitchenSinkPB p;
  const EnumType* enum_type;
  GOOGLESQL_CHECK_OK(type_factory_->MakeEnumType(googlesql_test::TestEnum_descriptor(),
                                       &enum_type));

  // test_enum
  EXPECT_EQ("test_enum: TESTENUM1",
            FormatProto("test_enum", Value::Enum(enum_type, 1), &p));
  EXPECT_EQ("", FormatProto("test_enum", Value::Null(enum_type), &p));
  EXPECT_THAT(FormatProto("test_enum", Int32(1), &p),
              HasSubstr("out_of_range"));

  // repeated_test_enum
  EXPECT_EQ(
      "repeated_test_enum: TESTENUM0 repeated_test_enum: TESTENUM1",
      FormatProto("repeated_test_enum",
                  Array({Value::Enum(enum_type, 0), Value::Enum(enum_type, 1)}),
                  &p));
  const ArrayType* enum_array_type;
  GOOGLESQL_CHECK_OK(type_factory_->MakeArrayType(enum_type, &enum_array_type));
  EXPECT_EQ("", FormatProto("repeated_test_enum",
                            Value::EmptyArray(enum_array_type), &p));
  EXPECT_EQ(
      "", FormatProto("repeated_test_enum", Value::Null(enum_array_type), &p));
  EXPECT_THAT(FormatProto("repeated_test_enum", Array({Int32(0)}), &p),
              HasSubstr("out_of_range"));
}

// Note: the Write methods in proto_util.cc do not check proto annotations,
// i.e., they allow storing integers in timestamp and date fields as well as
// timestamps in integer fields. However, GoogleSQL analyzer will reject such
// use. The test below purposefully does not attempt to exercise such scenarios.
TEST_F(ProtoEvalTest, CreateDateTimeProtoFields) {
  googlesql_test::KitchenSinkPB p;
  // date
  EXPECT_EQ("date: 5", FormatProto("date", Date(5), &p));
  EXPECT_EQ("", FormatProto("date", NullDate(), &p));
  EXPECT_THAT(FormatProto("date", String(""), &p), HasSubstr("out_of_range"));

  // date64
  EXPECT_EQ("date64: 5", FormatProto("date64", Date(5), &p));
  EXPECT_EQ("", FormatProto("date64", NullDate(), &p));
  EXPECT_THAT(FormatProto("date64", String(""), &p), HasSubstr("out_of_range"));

  // TODO: add tests for date_decimal once implemented.

  // timestamp_seconds
  EXPECT_EQ(
      "timestamp_seconds: 5",
      FormatProto("timestamp_seconds", TimestampFromUnixMicros(5000000), &p));
  EXPECT_EQ("", FormatProto("timestamp_seconds", NullTimestamp(), &p));
  EXPECT_THAT(FormatProto("timestamp_seconds", String(""), &p),
              HasSubstr("out_of_range"));

  EXPECT_EQ("timestamp_seconds_format: 5",
            FormatProto("timestamp_seconds_format",
                        TimestampFromUnixMicros(5000000), &p));
  EXPECT_EQ("", FormatProto("timestamp_seconds_format", NullTimestamp(), &p));
  EXPECT_THAT(FormatProto("timestamp_seconds_format", String(""), &p),
              HasSubstr("out_of_range"));

  // timestamp_millis
  EXPECT_EQ("timestamp_millis: 5",
            FormatProto("timestamp_millis", TimestampFromUnixMicros(5000), &p));
  EXPECT_EQ("", FormatProto("timestamp_millis", NullTimestamp(), &p));
  EXPECT_THAT(FormatProto("timestamp_millis", String(""), &p),
              HasSubstr("out_of_range"));

  EXPECT_EQ("timestamp_millis_format: 5",
            FormatProto("timestamp_millis_format",
                        TimestampFromUnixMicros(5000), &p));
  EXPECT_EQ("", FormatProto("timestamp_millis_format", NullTimestamp(), &p));
  EXPECT_THAT(FormatProto("timestamp_millis_format", String(""), &p),
              HasSubstr("out_of_range"));

  // timestamp_micros
  EXPECT_EQ("timestamp_micros: 5",
            FormatProto("timestamp_micros", TimestampFromUnixMicros(5), &p));
  EXPECT_EQ("", FormatProto("timestamp_micros", NullTimestamp(), &p));
  EXPECT_THAT(FormatProto("timestamp_micros", Uint32(1), &p),
              HasSubstr("out_of_range"));

  EXPECT_EQ(
      "timestamp_micros_format: 5",
      FormatProto("timestamp_micros_format", TimestampFromUnixMicros(5), &p));
  EXPECT_EQ("", FormatProto("timestamp_micros_format", NullTimestamp(), &p));
  EXPECT_THAT(FormatProto("timestamp_micros_format", Uint32(1), &p),
              HasSubstr("out_of_range"));

  // repeated_date
  EXPECT_EQ("repeated_date: 5 repeated_date: 7",
            FormatProto("repeated_date", Array({Date(5), Date(7)}), &p));
  EXPECT_THAT(FormatProto("repeated_date", Array({String("")}), &p),
              HasSubstr("out_of_range"));

  // repeated_date64
  EXPECT_EQ("repeated_date64: 5 repeated_date64: 7",
            FormatProto("repeated_date64", Array({Date(5), Date(7)}), &p));
  EXPECT_THAT(FormatProto("repeated_date64", Array({String("")}), &p),
              HasSubstr("out_of_range"));

  // repeated_timestamp_seconds
  EXPECT_EQ("repeated_timestamp_seconds: 5 repeated_timestamp_seconds: 7",
            FormatProto("repeated_timestamp_seconds",
                        Array({TimestampFromUnixMicros(5000000),
                               TimestampFromUnixMicros(7000000)}),
                        &p));
  EXPECT_THAT(
      FormatProto("repeated_timestamp_seconds", Array({String("")}), &p),
      HasSubstr("out_of_range"));

  // repeated_timestamp_millis
  EXPECT_EQ("repeated_timestamp_millis: 5 repeated_timestamp_millis: 7",
            FormatProto("repeated_timestamp_millis",
                        Array({TimestampFromUnixMicros(5000),
                               TimestampFromUnixMicros(7000)}),
                        &p));
  EXPECT_THAT(FormatProto("repeated_timestamp_millis", Array({String("")}), &p),
              HasSubstr("out_of_range"));

  // repeated_timestamp_micros
  EXPECT_EQ(
      "repeated_timestamp_micros: 5 repeated_timestamp_micros: 7",
      FormatProto(
          "repeated_timestamp_micros",
          Array({TimestampFromUnixMicros(5), TimestampFromUnixMicros(7)}), &p));
  EXPECT_THAT(FormatProto("repeated_timestamp_micros", Array({String("")}), &p),
              HasSubstr("out_of_range"));
}

TEST_F(ProtoEvalTest, GetProtoFieldExprPrimitiveProtoFields) {
  googlesql_test::KitchenSinkPB p;

  // int32_val: default 77
  EXPECT_EQ(Value::Int32(77), GetProtoFieldOrDie(&p, "int32_val"));
  EXPECT_EQ(Value::Bool(false), HasProtoFieldOrDie(&p, "int32_val"));
  p.set_int32_val(123);
  EXPECT_EQ(Value::Int32(123), GetProtoFieldOrDie(&p, "int32_val"));
  EXPECT_EQ(Value::Bool(true), HasProtoFieldOrDie(&p, "int32_val"));

  // uint32_val: default 777
  EXPECT_EQ(Value::Uint32(777), GetProtoFieldOrDie(&p, "uint32_val"));
  EXPECT_EQ(Value::Bool(false), HasProtoFieldOrDie(&p, "uint32_val"));
  p.set_uint32_val(124);
  EXPECT_EQ(Value::Uint32(124), GetProtoFieldOrDie(&p, "uint32_val"));
  EXPECT_EQ(Value::Bool(true), HasProtoFieldOrDie(&p, "uint32_val"));

  // int64_val: default 0
  EXPECT_EQ(Value::Int64(0), GetProtoFieldOrDie(&p, "int64_val"));
  EXPECT_EQ(Value::Bool(false), HasProtoFieldOrDie(&p, "int64_val"));
  p.set_int64_val(-125);
  EXPECT_EQ(Value::Int64(-125), GetProtoFieldOrDie(&p, "int64_val"));
  EXPECT_EQ(Value::Bool(true), HasProtoFieldOrDie(&p, "int64_val"));

  // uint64_val: default 0
  EXPECT_EQ(Value::Uint64(0), GetProtoFieldOrDie(&p, "uint64_val"));
  EXPECT_EQ(Value::Bool(false), HasProtoFieldOrDie(&p, "uint64_val"));
  p.set_uint64_val(125);
  EXPECT_EQ(Value::Uint64(125), GetProtoFieldOrDie(&p, "uint64_val"));
  EXPECT_EQ(Value::Bool(true), HasProtoFieldOrDie(&p, "uint64_val"));

  // string_val: default "default_name"
  EXPECT_EQ(Value::String("default_name"),
            GetProtoFieldOrDie(&p, "string_val"));
  EXPECT_EQ(Value::Bool(false), HasProtoFieldOrDie(&p, "string_val"));
  p.set_string_val("foo");
  EXPECT_EQ(Value::String("foo"), GetProtoFieldOrDie(&p, "string_val"));
  EXPECT_EQ(Value::Bool(true), HasProtoFieldOrDie(&p, "string_val"));

  EXPECT_EQ(Value::Float(0), GetProtoFieldOrDie(&p, "float_val"));
  EXPECT_EQ(Value::Bool(false), HasProtoFieldOrDie(&p, "float_val"));
  p.set_float_val(125.5);
  EXPECT_EQ(Value::Float(125.5), GetProtoFieldOrDie(&p, "float_val"));

  EXPECT_EQ(Value::Double(0), GetProtoFieldOrDie(&p, "double_val"));
  EXPECT_EQ(Value::Bool(false), HasProtoFieldOrDie(&p, "double_val"));
  p.set_double_val(125.7);
  EXPECT_EQ(Value::Double(125.7), GetProtoFieldOrDie(&p, "double_val"));
  EXPECT_EQ(Value::Bool(true), HasProtoFieldOrDie(&p, "double_val"));

  EXPECT_EQ(Value::Bytes(""), GetProtoFieldOrDie(&p, "bytes_val"));
  EXPECT_EQ(Value::Bool(false), HasProtoFieldOrDie(&p, "bytes_val"));
  p.set_bytes_val("bar");
  EXPECT_EQ(Value::Bytes("bar"), GetProtoFieldOrDie(&p, "bytes_val"));
  EXPECT_EQ(Value::Bool(true), HasProtoFieldOrDie(&p, "bytes_val"));

  EXPECT_EQ(Value::Bool(false), GetProtoFieldOrDie(&p, "bool_val"));
  EXPECT_EQ(Value::Bool(false), HasProtoFieldOrDie(&p, "bool_val"));
  p.set_bool_val(true);
  EXPECT_EQ(Value::Bool(true), GetProtoFieldOrDie(&p, "bool_val"));
  EXPECT_EQ(Value::Bool(true), HasProtoFieldOrDie(&p, "bool_val"));

  EXPECT_EQ(Value::Uint32(0), GetProtoFieldOrDie(&p, "fixed32_val"));
  EXPECT_EQ(Value::Bool(false), HasProtoFieldOrDie(&p, "fixed32_val"));
  p.set_fixed32_val(129);
  EXPECT_EQ(Value::Uint32(129), GetProtoFieldOrDie(&p, "fixed32_val"));
  EXPECT_EQ(Value::Bool(true), HasProtoFieldOrDie(&p, "fixed32_val"));

  EXPECT_EQ(Value::Uint64(0), GetProtoFieldOrDie(&p, "fixed64_val"));
  EXPECT_EQ(Value::Bool(false), HasProtoFieldOrDie(&p, "fixed64_val"));
  p.set_fixed64_val(129);
  EXPECT_EQ(Value::Uint64(129), GetProtoFieldOrDie(&p, "fixed64_val"));
  EXPECT_EQ(Value::Bool(true), HasProtoFieldOrDie(&p, "fixed64_val"));

  EXPECT_EQ(Value::Int32(0), GetProtoFieldOrDie(&p, "sint32_val"));
  EXPECT_EQ(Value::Bool(false), HasProtoFieldOrDie(&p, "sint32_val"));
  p.set_sint32_val(-135);
  EXPECT_EQ(Value::Int32(-135), GetProtoFieldOrDie(&p, "sint32_val"));
  EXPECT_EQ(Value::Bool(true), HasProtoFieldOrDie(&p, "sint32_val"));

  EXPECT_EQ(Value::Int64(0), GetProtoFieldOrDie(&p, "sint64_val"));
  EXPECT_EQ(Value::Bool(false), HasProtoFieldOrDie(&p, "sint64_val"));
  p.set_sint64_val(-135);
  EXPECT_EQ(Value::Int64(-135), GetProtoFieldOrDie(&p, "sint64_val"));
  EXPECT_EQ(Value::Bool(true), HasProtoFieldOrDie(&p, "sint64_val"));
}

TEST_F(ProtoEvalTest, GetProtoFieldExprRepeatedProtoFields) {
  googlesql_test::KitchenSinkPB p;

  // Repeated fields.
  EXPECT_EQ(Int32Array({}), GetProtoFieldOrDie(&p, "repeated_int32_val"));
  p.add_repeated_int32_val(140);
  p.add_repeated_int32_val(141);
  EXPECT_EQ(Int32Array({140, 141}),
            GetProtoFieldOrDie(&p, "repeated_int32_val"));

  EXPECT_EQ(Uint32Array({}), GetProtoFieldOrDie(&p, "repeated_uint32_val"));
  p.add_repeated_uint32_val(150);
  p.add_repeated_uint32_val(151);
  EXPECT_EQ(Uint32Array({150, 151}),
            GetProtoFieldOrDie(&p, "repeated_uint32_val"));

  EXPECT_EQ(Int64Array({}), GetProtoFieldOrDie(&p, "repeated_int64_val"));
  p.add_repeated_int64_val(160);
  p.add_repeated_int64_val(161);
  EXPECT_EQ(Int64Array({160, 161}),
            GetProtoFieldOrDie(&p, "repeated_int64_val"));

  EXPECT_EQ(Uint64Array({}), GetProtoFieldOrDie(&p, "repeated_uint64_val"));
  p.add_repeated_uint64_val(170);
  p.add_repeated_uint64_val(171);
  EXPECT_EQ(Uint64Array({170, 171}),
            GetProtoFieldOrDie(&p, "repeated_uint64_val"));

  EXPECT_EQ(StringArray(std::vector<std::string>()),
            GetProtoFieldOrDie(&p, "repeated_string_val"));
  p.add_repeated_string_val("s1");
  p.add_repeated_string_val("s2");
  EXPECT_EQ(StringArray({"s1", "s2"}),
            GetProtoFieldOrDie(&p, "repeated_string_val"));

  EXPECT_EQ(FloatArray({}), GetProtoFieldOrDie(&p, "repeated_float_val"));
  p.add_repeated_float_val(80.5);
  p.add_repeated_float_val(81.5);
  EXPECT_EQ(FloatArray({80.5, 81.5}),
            GetProtoFieldOrDie(&p, "repeated_float_val"));

  EXPECT_EQ(DoubleArray({}), GetProtoFieldOrDie(&p, "repeated_double_val"));
  p.add_repeated_double_val(8.5);
  p.add_repeated_double_val(9.5);
  EXPECT_EQ(DoubleArray({8.5, 9.5}),
            GetProtoFieldOrDie(&p, "repeated_double_val"));

  EXPECT_EQ(BytesArray(std::vector<std::string>()),
            GetProtoFieldOrDie(&p, "repeated_bytes_val"));
  p.add_repeated_bytes_val("b1");
  p.add_repeated_bytes_val("b2");
  EXPECT_EQ(BytesArray({"b1", "b2"}),
            GetProtoFieldOrDie(&p, "repeated_bytes_val"));

  EXPECT_EQ(BoolArray({}), GetProtoFieldOrDie(&p, "repeated_bool_val"));
  p.add_repeated_bool_val(true);
  p.add_repeated_bool_val(false);
  EXPECT_EQ(BoolArray({true, false}),
            GetProtoFieldOrDie(&p, "repeated_bool_val"));

  EXPECT_EQ(Uint32Array({}), GetProtoFieldOrDie(&p, "repeated_fixed32_val"));
  p.add_repeated_fixed32_val(160);
  p.add_repeated_fixed32_val(161);
  EXPECT_EQ(Uint32Array({160, 161}),
            GetProtoFieldOrDie(&p, "repeated_fixed32_val"));

  EXPECT_EQ(Uint64Array({}), GetProtoFieldOrDie(&p, "repeated_fixed64_val"));
  p.add_repeated_fixed64_val(170);
  p.add_repeated_fixed64_val(171);
  EXPECT_EQ(Uint64Array({170, 171}),
            GetProtoFieldOrDie(&p, "repeated_fixed64_val"));

  EXPECT_EQ(Int32Array({}), GetProtoFieldOrDie(&p, "repeated_sfixed32_val"));
  p.add_repeated_sfixed32_val(170);
  p.add_repeated_sfixed32_val(171);
  EXPECT_EQ(Int32Array({170, 171}),
            GetProtoFieldOrDie(&p, "repeated_sfixed32_val"));

  EXPECT_EQ(Int64Array({}), GetProtoFieldOrDie(&p, "repeated_sfixed64_val"));
  p.add_repeated_sfixed64_val(180);
  p.add_repeated_sfixed64_val(181);
  EXPECT_EQ(Int64Array({180, 181}),
            GetProtoFieldOrDie(&p, "repeated_sfixed64_val"));

  EXPECT_EQ(Int32Array({}), GetProtoFieldOrDie(&p, "repeated_sint32_val"));
  p.add_repeated_sint32_val(190);
  p.add_repeated_sint32_val(191);
  EXPECT_EQ(Int32Array({190, 191}),
            GetProtoFieldOrDie(&p, "repeated_sint32_val"));

  EXPECT_EQ(Int64Array({}), GetProtoFieldOrDie(&p, "repeated_sint64_val"));
  p.add_repeated_sint64_val(200);
  p.add_repeated_sint64_val(201);
  EXPECT_EQ(Int64Array({200, 201}),
            GetProtoFieldOrDie(&p, "repeated_sint64_val"));
}

TEST_F(ProtoEvalTest, GetProtoFieldExprPackedProtoFields) {
  googlesql_test::KitchenSinkPB p;

  EXPECT_EQ(Int32Array({}), GetProtoFieldOrDie(&p, "repeated_int32_packed"));
  p.add_repeated_int32_packed(140);
  p.add_repeated_int32_packed(141);
  EXPECT_EQ(Int32Array({140, 141}),
            GetProtoFieldOrDie(&p, "repeated_int32_packed"));

  EXPECT_EQ(Uint32Array({}), GetProtoFieldOrDie(&p, "repeated_uint32_packed"));
  p.add_repeated_uint32_packed(150);
  p.add_repeated_uint32_packed(151);
  EXPECT_EQ(Uint32Array({150, 151}),
            GetProtoFieldOrDie(&p, "repeated_uint32_packed"));

  EXPECT_EQ(Int64Array({}), GetProtoFieldOrDie(&p, "repeated_int64_packed"));
  p.add_repeated_int64_packed(160);
  p.add_repeated_int64_packed(161);
  EXPECT_EQ(Int64Array({160, 161}),
            GetProtoFieldOrDie(&p, "repeated_int64_packed"));

  EXPECT_EQ(Uint64Array({}), GetProtoFieldOrDie(&p, "repeated_uint64_packed"));
  p.add_repeated_uint64_packed(170);
  p.add_repeated_uint64_packed(171);
  EXPECT_EQ(Uint64Array({170, 171}),
            GetProtoFieldOrDie(&p, "repeated_uint64_packed"));

  EXPECT_EQ(FloatArray({}), GetProtoFieldOrDie(&p, "repeated_float_packed"));
  p.add_repeated_float_packed(80.5);
  p.add_repeated_float_packed(81.5);
  EXPECT_EQ(FloatArray({80.5, 81.5}),
            GetProtoFieldOrDie(&p, "repeated_float_packed"));

  EXPECT_EQ(DoubleArray({}), GetProtoFieldOrDie(&p, "repeated_double_packed"));
  p.add_repeated_double_packed(8.5);
  p.add_repeated_double_packed(9.5);
  EXPECT_EQ(DoubleArray({8.5, 9.5}),
            GetProtoFieldOrDie(&p, "repeated_double_packed"));

  EXPECT_EQ(BoolArray({}), GetProtoFieldOrDie(&p, "repeated_bool_packed"));
  p.add_repeated_bool_packed(true);
  p.add_repeated_bool_packed(false);
  EXPECT_EQ(BoolArray({true, false}),
            GetProtoFieldOrDie(&p, "repeated_bool_packed"));

  EXPECT_EQ(Uint32Array({}), GetProtoFieldOrDie(&p, "repeated_fixed32_packed"));
  p.add_repeated_fixed32_packed(160);
  p.add_repeated_fixed32_packed(161);
  EXPECT_EQ(Uint32Array({160, 161}),
            GetProtoFieldOrDie(&p, "repeated_fixed32_packed"));

  EXPECT_EQ(Uint64Array({}), GetProtoFieldOrDie(&p, "repeated_fixed64_packed"));
  p.add_repeated_fixed64_packed(170);
  p.add_repeated_fixed64_packed(171);
  EXPECT_EQ(Uint64Array({170, 171}),
            GetProtoFieldOrDie(&p, "repeated_fixed64_packed"));

  EXPECT_EQ(Int32Array({}), GetProtoFieldOrDie(&p, "repeated_sfixed32_packed"));
  p.add_repeated_sfixed32_packed(170);
  p.add_repeated_sfixed32_packed(171);
  EXPECT_EQ(Int32Array({170, 171}),
            GetProtoFieldOrDie(&p, "repeated_sfixed32_packed"));

  EXPECT_EQ(Int64Array({}), GetProtoFieldOrDie(&p, "repeated_sfixed64_packed"));
  p.add_repeated_sfixed64_packed(180);
  p.add_repeated_sfixed64_packed(181);
  EXPECT_EQ(Int64Array({180, 181}),
            GetProtoFieldOrDie(&p, "repeated_sfixed64_packed"));

  EXPECT_EQ(Int32Array({}), GetProtoFieldOrDie(&p, "repeated_sint32_packed"));
  p.add_repeated_sint32_packed(190);
  p.add_repeated_sint32_packed(191);
  EXPECT_EQ(Int32Array({190, 191}),
            GetProtoFieldOrDie(&p, "repeated_sint32_packed"));

  EXPECT_EQ(Int64Array({}), GetProtoFieldOrDie(&p, "repeated_sint64_packed"));
  p.add_repeated_sint64_packed(200);
  p.add_repeated_sint64_packed(201);
  EXPECT_EQ(Int64Array({200, 201}),
            GetProtoFieldOrDie(&p, "repeated_sint64_packed"));

  const EnumType* enum_type;
  GOOGLESQL_CHECK_OK(type_factory_->MakeEnumType(googlesql_test::TestEnum_descriptor(),
                                       &enum_type));
  const ArrayType* enum_array_type;
  GOOGLESQL_CHECK_OK(type_factory_->MakeArrayType(enum_type, &enum_array_type));
  EXPECT_EQ(Value::EmptyArray(enum_array_type),
            GetProtoFieldOrDie(&p, "repeated_enum_packed"));
  p.add_repeated_enum_packed(googlesql_test::TESTENUM2);
  p.add_repeated_enum_packed(googlesql_test::TESTENUM1);
  EXPECT_EQ(
      Value::Array(enum_array_type, {Value::Enum(enum_type, "TESTENUM2"),
                                     Value::Enum(enum_type, "TESTENUM1")}),
      GetProtoFieldOrDie(&p, "repeated_enum_packed"));
}

TEST_F(ProtoEvalTest, GetProtoFieldExprProtosEnums) {
  googlesql_test::KitchenSinkPB p;

  googlesql_test::KitchenSinkPB::Nested nested_tmp;
  const ProtoType* nested_type = MakeProtoType(&nested_tmp);
  EXPECT_EQ(Value::Null(nested_type), GetProtoFieldOrDie(&p, "nested_value"));
  EXPECT_EQ(Value::Bool(false), HasProtoFieldOrDie(&p, "nested_value"));
  EXPECT_EQ(Value::NullInt64(),
            GetProtoFieldOrDie(GetProtoFieldOrDie(&p, "nested_value"),
                               "nested_int64"));
  EXPECT_EQ(Value::NullBool(),
            HasProtoFieldOrDie(GetProtoFieldOrDie(&p, "nested_value"),
                               "nested_int64"));
  googlesql_test::KitchenSinkPB::Nested* n = p.mutable_nested_value();
  // Default: 88.
  EXPECT_EQ(Value::Int64(88), GetProtoFieldOrDie(n, "nested_int64"));
  n->add_nested_repeated_int64(300);
  n->add_nested_repeated_int64(301);
  ASSERT_TRUE(nested_tmp.ParseFromCord(
      GetProtoFieldOrDie(&p, "nested_value").ToCord()));
  EXPECT_EQ(absl::StrCat(*n), absl::StrCat(nested_tmp));
  EXPECT_EQ(Value::Bool(true), HasProtoFieldOrDie(&p, "nested_value"));

  const ArrayType* nested_array_type;
  GOOGLESQL_CHECK_OK(type_factory_->MakeArrayType(nested_type, &nested_array_type));
  EXPECT_EQ(Value::Array(nested_array_type, {}),
            GetProtoFieldOrDie(&p, "nested_repeated_value"));
  p.add_nested_repeated_value()->CopyFrom(*n);
  p.add_nested_repeated_value()->CopyFrom(*n);
  absl::Cord bytes_3145;
  ABSL_CHECK(n->SerializePartialToCord(&bytes_3145));
  absl::Cord nested_bytes = bytes_3145;
  EXPECT_EQ(Value::Array(nested_array_type,
                         {Value::Proto(nested_type, nested_bytes),
                          Value::Proto(nested_type, nested_bytes)}),
            GetProtoFieldOrDie(&p, "nested_repeated_value"));

  const EnumType* enum_type;
  GOOGLESQL_CHECK_OK(type_factory_->MakeEnumType(googlesql_test::TestEnum_descriptor(),
                                       &enum_type));
  EXPECT_EQ(Value::Enum(enum_type, "TESTENUM0"),
            GetProtoFieldOrDie(&p, "test_enum"));  // Default value.
  EXPECT_EQ(Value::Bool(false), HasProtoFieldOrDie(&p, "test_enum"));
  p.set_test_enum(googlesql_test::TESTENUM1);
  EXPECT_EQ(Value::Enum(enum_type, "TESTENUM1"),
            GetProtoFieldOrDie(&p, "test_enum"));
  EXPECT_EQ(Value::Bool(true), HasProtoFieldOrDie(&p, "test_enum"));

  const ArrayType* enum_array_type;
  GOOGLESQL_CHECK_OK(type_factory_->MakeArrayType(enum_type, &enum_array_type));
  EXPECT_EQ(Value::EmptyArray(enum_array_type),
            GetProtoFieldOrDie(&p, "repeated_test_enum"));
  p.add_repeated_test_enum(googlesql_test::TESTENUM2);
  p.add_repeated_test_enum(googlesql_test::TESTENUM1);
  EXPECT_EQ(
      Value::Array(enum_array_type, {Value::Enum(enum_type, "TESTENUM2"),
                                     Value::Enum(enum_type, "TESTENUM1")}),
      GetProtoFieldOrDie(&p, "repeated_test_enum"));

  // optional_group
  googlesql_test::KitchenSinkPB::OptionalGroup group_tmp;
  const ProtoType* group_type = MakeProtoType(&group_tmp);
  EXPECT_EQ(Value::Null(group_type), GetProtoFieldOrDie(&p, "optional_group"));
  EXPECT_EQ(Value::Bool(false), HasProtoFieldOrDie(&p, "optional_group"));
  EXPECT_EQ(Value::NullInt64(),
            GetProtoFieldOrDie(GetProtoFieldOrDie(&p, "optional_group"),
                               "int64_val"));
  googlesql_test::KitchenSinkPB::OptionalGroup* g = p.mutable_optional_group();
  g->set_int64_val(500);
  g->add_optionalgroupnested()->set_int64_val(510);
  ASSERT_TRUE(group_tmp.ParseFromCord(
      GetProtoFieldOrDie(&p, "optional_group").ToCord()));
  EXPECT_EQ(absl::StrCat(*g), absl::StrCat(group_tmp));
  EXPECT_EQ(Value::Bool(true), HasProtoFieldOrDie(&p, "optional_group"));

  // nested_repeated_group
  googlesql_test::KitchenSinkPB::NestedRepeatedGroup repeated_group_tmp;
  const ProtoType* repeated_group_type = MakeProtoType(&repeated_group_tmp);
  const ArrayType* repeated_group_array_type;
  GOOGLESQL_CHECK_OK(type_factory_->MakeArrayType(repeated_group_type,
                                        &repeated_group_array_type));
  EXPECT_EQ(Value::Array(repeated_group_array_type, {}),
            GetProtoFieldOrDie(&p, "nested_repeated_group"));
  p.add_nested_repeated_group()->set_id(600);
  p.add_nested_repeated_group()->add_nestedrepeatedgroupnested()->set_id(610);

  absl::Cord bytes_3200;
  ABSL_CHECK(p.nested_repeated_group(0).SerializePartialToCord(&bytes_3200));
  absl::Cord group_bytes0 = bytes_3200;
  absl::Cord bytes_3201;
  ABSL_CHECK(p.nested_repeated_group(1).SerializePartialToCord(&bytes_3201));
  absl::Cord group_bytes1 = bytes_3201;
  EXPECT_EQ(Value::Array(repeated_group_array_type,
                         {Value::Proto(repeated_group_type, group_bytes0),
                          Value::Proto(repeated_group_type, group_bytes1)}),
            GetProtoFieldOrDie(&p, "nested_repeated_group"));
}

TEST_F(ProtoEvalTest, GetProtoFieldExprDateTime) {
  googlesql_test::KitchenSinkPB p;
  EXPECT_EQ(Value::Date(0), GetProtoFieldOrDie(&p, "date"));
  EXPECT_EQ(Value::Bool(false), HasProtoFieldOrDie(&p, "date"));
  p.set_date(types::kDateMax);
  EXPECT_EQ(Value::Bool(true), HasProtoFieldOrDie(&p, "date"));
  EXPECT_EQ(Value::Date(types::kDateMax), GetProtoFieldOrDie(&p, "date"));
  p.set_date(types::kDateMin);
  EXPECT_EQ(Value::Date(types::kDateMin), GetProtoFieldOrDie(&p, "date"));
  // Out-of-bounds date value produces an error.
  p.set_date(types::kDateMax + 1);
  EXPECT_THAT(GetProtoField(&p, "date"),
              StatusIs(absl::StatusCode::kOutOfRange,
                       HasSubstr("Corrupted protocol buffer")));
  p.set_date(types::kDateMin - 1);
  EXPECT_THAT(GetProtoField(&p, "date"),
              StatusIs(absl::StatusCode::kOutOfRange,
                       HasSubstr("Corrupted protocol buffer")));

  EXPECT_EQ(Value::Date(0), GetProtoFieldOrDie(&p, "date64"));
  EXPECT_EQ(Value::Bool(false), HasProtoFieldOrDie(&p, "date64"));
  p.set_date64(102031);
  EXPECT_EQ(Value::Date(102031), GetProtoFieldOrDie(&p, "date64"));
  EXPECT_EQ(Value::Bool(true), HasProtoFieldOrDie(&p, "date64"));

  EXPECT_EQ(Value::TimestampFromUnixMicros(0),
            GetProtoFieldOrDie(&p, "timestamp_seconds"));
  EXPECT_EQ(Value::Bool(false), HasProtoFieldOrDie(&p, "timestamp_seconds"));
  p.set_timestamp_seconds(types::kTimestampSecondsMin);
  EXPECT_EQ(Value::Bool(true), HasProtoFieldOrDie(&p, "timestamp_seconds"));
  EXPECT_EQ(
      Value::TimestampFromUnixMicros(types::kTimestampSecondsMin * 1000000),
      GetProtoFieldOrDie(&p, "timestamp_seconds"));
  p.set_timestamp_seconds(types::kTimestampSecondsMax);
  EXPECT_EQ(
      Value::TimestampFromUnixMicros(types::kTimestampSecondsMax * 1000000),
      GetProtoFieldOrDie(&p, "timestamp_seconds"));
  // Out-of-bounds timestamp seconds value produces an error.
  p.set_timestamp_seconds(types::kTimestampMin / 1000000 - 1);
  EXPECT_FALSE(GetProtoField(&p, "timestamp_seconds").ok());
  p.set_timestamp_seconds(types::kTimestampMax / 1000000 + 1);
  EXPECT_FALSE(GetProtoField(&p, "timestamp_seconds").ok());

  EXPECT_EQ(Value::TimestampFromUnixMicros(0),
            GetProtoFieldOrDie(&p, "timestamp_millis"));
  EXPECT_EQ(Value::Bool(false), HasProtoFieldOrDie(&p, "timestamp_millis"));
  p.set_timestamp_millis(types::kTimestampMillisMin);
  EXPECT_EQ(Value::Bool(true), HasProtoFieldOrDie(&p, "timestamp_millis"));
  EXPECT_EQ(Value::TimestampFromUnixMicros(types::kTimestampMillisMin * 1000),
            GetProtoFieldOrDie(&p, "timestamp_millis"));
  p.set_timestamp_millis(types::kTimestampMillisMax);
  EXPECT_EQ(Value::TimestampFromUnixMicros(types::kTimestampMillisMax * 1000),
            GetProtoFieldOrDie(&p, "timestamp_millis"));
  // Out-of-bounds timestamp millis value produces an error.
  p.set_timestamp_millis(types::kTimestampMin / 1000 - 1);
  EXPECT_FALSE(GetProtoField(&p, "timestamp_millis").ok());
  p.set_timestamp_millis(types::kTimestampMax / 1000 + 1);
  EXPECT_FALSE(GetProtoField(&p, "timestamp_millis").ok());

  EXPECT_EQ(Value::TimestampFromUnixMicros(0),
            GetProtoFieldOrDie(&p, "timestamp_micros"));
  EXPECT_EQ(Value::Bool(false), HasProtoFieldOrDie(&p, "timestamp_micros"));
  p.set_timestamp_micros(types::kTimestampMicrosMin);
  EXPECT_EQ(Value::Bool(true), HasProtoFieldOrDie(&p, "timestamp_micros"));
  EXPECT_EQ(Value::TimestampFromUnixMicros(types::kTimestampMicrosMin),
            GetProtoFieldOrDie(&p, "timestamp_micros"));
  p.set_timestamp_micros(types::kTimestampMicrosMax);
  EXPECT_EQ(Value::TimestampFromUnixMicros(types::kTimestampMicrosMax),
            GetProtoFieldOrDie(&p, "timestamp_micros"));
  // Out-of-bounds timestamp value produces an error.
  p.set_timestamp_micros(types::kTimestampMin - 1);
  EXPECT_FALSE(GetProtoField(&p, "timestamp_micros").ok());
  p.set_timestamp_micros(types::kTimestampMax + 1);
  EXPECT_FALSE(GetProtoField(&p, "timestamp_micros").ok());
}

TEST_F(ProtoEvalTest, GetProtoFieldExprDefaultAnnotations) {
  googlesql_test::KitchenSinkPB p;
  EXPECT_EQ(Value::Int64(0), GetProtoFieldOrDie(&p, "int_with_no_default"));
  EXPECT_EQ(Value::Int64(17), GetProtoFieldOrDie(&p, "int_with_default"));
  EXPECT_EQ(Value::NullInt64(),
            GetProtoFieldOrDie(&p, "int_with_no_default_nullable"));
  EXPECT_EQ(Value::NullInt64(),
            GetProtoFieldOrDie(&p, "int_with_default_nullable"));
  EXPECT_EQ(Value::Bool(false), HasProtoFieldOrDie(&p, "int_with_no_default"));
  EXPECT_EQ(Value::Bool(false), HasProtoFieldOrDie(&p, "int_with_default"));
  EXPECT_EQ(Value::Bool(false),
            HasProtoFieldOrDie(&p, "int_with_no_default_nullable"));
  EXPECT_EQ(Value::Bool(false),
            HasProtoFieldOrDie(&p, "int_with_default_nullable"));
  // Field starting with "has_" works fine assuming the method access is
  // disambiguated properly from the field access.
  EXPECT_EQ(Value::String(""), GetProtoFieldOrDie(&p, "has_confusing_name"));
  EXPECT_EQ(Value::Bool(false), HasProtoFieldOrDie(&p, "has_confusing_name"));
  p.set_has_confusing_name("no");
  EXPECT_EQ(Value::String("no"), GetProtoFieldOrDie(&p, "has_confusing_name"));
  EXPECT_EQ(Value::Bool(true), HasProtoFieldOrDie(&p, "has_confusing_name"));
}

TEST_F(ProtoEvalTest, GetProtoFieldExprLastFieldOccurrence) {
  // The latest value with the given tag is the one than matters.
  googlesql_test::KitchenSinkPB p;

  p.set_int32_val(5);
  absl::Cord bytes_3312;
  ABSL_CHECK(p.SerializePartialToCord(&bytes_3312));
  absl::Cord bytes1 = bytes_3312;
  p.set_int32_val(7);
  absl::Cord bytes_3314;
  ABSL_CHECK(p.SerializePartialToCord(&bytes_3314));
  absl::Cord bytes2 = bytes_3314;
  absl::Cord bytes;
  bytes.Append(bytes1);
  bytes.Append(bytes2);
  Value proto_value = Value::Proto(MakeProtoType(&p), bytes);
  EXPECT_EQ(Value::Int32(7), GetProtoFieldOrDie(proto_value, "int32_val"));
  // Proto API has the same behavior.
  p.Clear();
  ASSERT_TRUE(p.ParsePartialFromCord(proto_value.ToCord()));
  EXPECT_EQ(7, p.int32_val());
}

TEST_F(ProtoEvalTest, GetProtoFieldExprOutOfBoundsInt32) {
  googlesql_test::KitchenSinkPB p;
  // Append int32_val field with value 1. Streams are scoped to be closed
  // correctly.
  google::protobuf::io::CordOutputStream cord_stream1;
  {
    google::protobuf::io::CodedOutputStream out(&cord_stream1);
    out.WriteVarint32(WireFormatLite::MakeTag(3 /* tag of int32_val */,
                                              WireFormatLite::WIRETYPE_VARINT));
    out.WriteVarint32(5);
    // Append another int32_val field which has
    // std::numeric_limits<int64>::max() value on the wire.
    out.WriteVarint32(WireFormatLite::MakeTag(3 /* tag of int32_val */,
                                              WireFormatLite::WIRETYPE_VARINT));
    out.WriteVarint64(std::numeric_limits<int64_t>::max());
  }
  absl::Cord bytes = cord_stream1.Consume();
  // int64 value is truncated to int32 by WireFormatLite and is returned as -1
  // (yes, this is weird).
  Value proto_value = Value::Proto(MakeProtoType(&p), bytes);
  EXPECT_EQ("{int32_val: -1}", proto_value.ShortDebugString());
  EXPECT_EQ(Value::Int32(-1), GetProtoFieldOrDie(proto_value, "int32_val"));
  // Append garbage at the end. This causes a corruption error.
  google::protobuf::io::CordOutputStream cord_stream2;
  {
    google::protobuf::io::CodedOutputStream out(&cord_stream2);
    out.WriteVarint32(
        WireFormatLite::MakeTag(150776, WireFormatLite::WIRETYPE_END_GROUP));
  }
  bytes.Append(cord_stream2.Consume());
  EXPECT_EQ(15, bytes.size());
  proto_value = Value::Proto(MakeProtoType(&p), bytes);
  EXPECT_THAT(GetProtoField(proto_value, "int32_val"),
              StatusIs(absl::StatusCode::kOutOfRange,
                       HasSubstr("Corrupted protocol buffer")));
}

TEST_F(ProtoEvalTest, GetProtoFieldExprCorruptedProto) {
  googlesql_test::KitchenSinkPB p;
  google::protobuf::io::CordOutputStream cord_stream;
  {
    google::protobuf::io::CodedOutputStream out(&cord_stream);
    out.WriteVarint32(WireFormatLite::MakeTag(
        150776, WireFormatLite::WIRETYPE_END_GROUP));  // garbage
  }

  // Proto2 API won't parse the value.
  absl::Cord bytes = cord_stream.Consume();
  ASSERT_FALSE(p.ParsePartialFromCord(bytes));
  Value proto_value = Value::Proto(MakeProtoType(&p), bytes);
  EXPECT_EQ("{<unparseable>}", proto_value.ShortDebugString());
  // Returns an error.
  EXPECT_THAT(GetProtoField(proto_value, "int32_val"),
              StatusIs(absl::StatusCode::kOutOfRange,
                       HasSubstr("Corrupted protocol buffer")));
}

TEST_F(ProtoEvalTest, GetProtoFieldExprsMultipleFieldsMultipleRows) {
  // Create two ProtoFieldInfos. For the first one, create a single
  // ProtoFieldReader used by two GetProtoFieldExpr nodes. For the second one,
  // create a second ProtoFieldReader (and a corresponding GetProtoFieldExpr
  // node). Evaluate all three GetProtoFieldExpr nodes on three protos to
  // ensure the ProtoFieldValueMap is used in the appropriate ways.
  for (bool use_shared_states : {false, true}) {
    ABSL_LOG(INFO) << "use_shared_states: " << use_shared_states;
    googlesql_test::KitchenSinkPB p1;
    p1.set_int64_key_1(1);
    p1.set_int64_key_2(2);
    googlesql_test::KitchenSinkPB_Nested* nested1 = p1.mutable_nested_value();
    nested1->set_nested_int64(3);

    googlesql_test::KitchenSinkPB p2;
    p2.set_int64_key_1(10);
    p2.set_int64_key_2(20);
    googlesql_test::KitchenSinkPB_Nested* nested2 = p2.mutable_nested_value();
    nested2->set_nested_int64(30);

    googlesql_test::KitchenSinkPB p3;
    p3.set_int64_key_1(100);
    p3.set_int64_key_2(200);
    googlesql_test::KitchenSinkPB_Nested* nested3 = p3.mutable_nested_value();
    nested3->set_nested_int64(300);

    absl::Cord bytes_3409;
    ABSL_CHECK(p1.SerializeToCord(&bytes_3409));
    absl::Cord bytes1 = bytes_3409;
    absl::Cord bytes_3410;
    ABSL_CHECK(p2.SerializeToCord(&bytes_3410));
    absl::Cord bytes2 = bytes_3410;
    absl::Cord bytes_3411;
    ABSL_CHECK(p3.SerializeToCord(&bytes_3411));
    absl::Cord bytes3 = bytes_3411;

    absl::Cord bytes_3413;
    ABSL_CHECK(nested1->SerializeToCord(&bytes_3413));
    absl::Cord nested_bytes1 = bytes_3413;
    absl::Cord bytes_3414;
    ABSL_CHECK(nested2->SerializeToCord(&bytes_3414));
    absl::Cord nested_bytes2 = bytes_3414;
    absl::Cord bytes_3415;
    ABSL_CHECK(nested3->SerializeToCord(&bytes_3415));
    absl::Cord nested_bytes3 = bytes_3415;

    const std::vector<Value> v = {Value::Proto(MakeProtoType(&p1), bytes1),
                                  Value::Proto(MakeProtoType(&p2), bytes2),
                                  Value::Proto(MakeProtoType(&p3), bytes3)};

    const Value nested_value1 =
        Value::Proto(MakeProtoType(nested1), nested_bytes1);
    const Value nested_value2 =
        Value::Proto(MakeProtoType(nested2), nested_bytes2);
    const Value nested_value3 =
        Value::Proto(MakeProtoType(nested3), nested_bytes3);

    TupleSlot s1;
    s1.SetValue(v[0]);

    TupleSlot s2;
    s2.SetValue(v[1]);

    TupleSlot s3;
    s3.SetValue(v[2]);

    ProtoFieldAccessInfo access_info1;
    ProtoFieldInfo* info1 = &access_info1.field_info;
    info1->descriptor = p1.GetDescriptor()->FindFieldByName("int64_key_1");
    GOOGLESQL_ASSERT_OK(GetProtoFieldTypeAndDefault(
        {}, info1->descriptor, /*catalog_name_path=*/{}, type_factory_,
        &info1->type, &info1->default_value));
    info1->format = ProtoType::GetFormatAnnotation(info1->descriptor);

    ProtoFieldAccessInfo access_info2;
    ProtoFieldInfo* info2 = &access_info2.field_info;
    info2->descriptor = p2.GetDescriptor()->FindFieldByName("nested_value");
    GOOGLESQL_ASSERT_OK(GetProtoFieldTypeAndDefault(
        {}, info2->descriptor, /*catalog_name_path=*/{}, type_factory_,
        &info2->type, &info2->default_value));
    info2->format = ProtoType::GetFormatAnnotation(info2->descriptor);

    EvaluationOptions options;
    options.store_proto_field_value_maps = use_shared_states;

    EvaluationContext context(options);
    std::vector<absl::StatusOr<TupleSlot>> output_slots;
    GOOGLESQL_ASSERT_OK(EvalGetProtoFieldExprs(
        {std::make_pair(&access_info1, 2), std::make_pair(&access_info2, 1)},
        {s1, s2, s3}, &context, &output_slots));
    ASSERT_EQ(output_slots.size(), (2 + 1) * 3);

    // First proto.
    // (The shared state is checked by EvalGetProtoFieldExprs()).
    EXPECT_THAT(output_slots[0], IsOkAndHolds(IsTupleSlotWith(Int64(1), _)));
    EXPECT_THAT(output_slots[1], IsOkAndHolds(IsTupleSlotWith(Int64(1), _)));
    EXPECT_THAT(output_slots[2],
                IsOkAndHolds(IsTupleSlotWith(nested_value1, _)));

    // Second proto.
    EXPECT_THAT(output_slots[3], IsOkAndHolds(IsTupleSlotWith(Int64(10), _)));
    EXPECT_THAT(output_slots[4], IsOkAndHolds(IsTupleSlotWith(Int64(10), _)));
    EXPECT_THAT(output_slots[5],
                IsOkAndHolds(IsTupleSlotWith(nested_value2, _)));

    // Third proto.
    EXPECT_THAT(output_slots[6], IsOkAndHolds(IsTupleSlotWith(Int64(100), _)));
    EXPECT_THAT(output_slots[7], IsOkAndHolds(IsTupleSlotWith(Int64(100), _)));
    EXPECT_THAT(output_slots[8],
                IsOkAndHolds(IsTupleSlotWith(nested_value3, _)));

    if (use_shared_states) {
      ASSERT_THAT(s1, IsTupleSlotWith(v[0], Not(IsNull())));
      ASSERT_THAT(s2, IsTupleSlotWith(v[1], Not(IsNull())));
      ASSERT_THAT(s3, IsTupleSlotWith(v[2], Not(IsNull())));

      std::vector<const SharedProtoState*> shared_states = {
          s1.mutable_shared_proto_state()->get(),
          s2.mutable_shared_proto_state()->get(),
          s3.mutable_shared_proto_state()->get()};

      for (int i = 0; i < 3; ++i) {
        const SharedProtoState& shared_state = *shared_states[i];
        ASSERT_TRUE(shared_state.has_value());
        ASSERT_EQ(shared_state.value().size(), 1);
        const auto& entry = *shared_state.value().begin();
        EXPECT_EQ(entry.first.proto_rep, InternalValue::GetProtoRep(v[i]));
        const ProtoFieldValueList& value_list = *entry.second;

        const int first_field_values[] = {1, 10, 100};
        const int first_field_value = first_field_values[i];

        googlesql_test::KitchenSinkPB_Nested nested_msg;
        nested_msg.set_nested_int64(3 * first_field_value);
        absl::Cord bytes;
        ABSL_CHECK(nested_msg.SerializeToCord(&bytes));
        absl::Cord nested_bytes = bytes;
        const Value nested_value =
            Value::Proto(MakeProtoType(&nested_msg), nested_bytes);

        EXPECT_THAT(value_list,
                    ElementsAre(IsOkAndHolds(Int64(first_field_value)),
                                IsOkAndHolds(nested_value)));
      }
    } else {
      EXPECT_THAT(s1, IsTupleSlotWith(v[0], Pointee(Eq(nullopt))));
      EXPECT_THAT(s2, IsTupleSlotWith(v[1], Pointee(Eq(nullopt))));
      EXPECT_THAT(s3, IsTupleSlotWith(v[2], Pointee(Eq(nullopt))));
    }
  }
}

}  // namespace
}  // namespace googlesql
