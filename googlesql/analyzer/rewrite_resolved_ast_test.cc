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

#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "googlesql/base/atomic_sequence_num.h"
#include "googlesql/base/testing/status_matchers.h"  
#include "googlesql/public/analyzer.h"
#include "googlesql/public/analyzer_options.h"
#include "googlesql/public/analyzer_output.h"
#include "googlesql/public/builtin_function_options.h"
#include "googlesql/public/options.pb.h"
#include "googlesql/public/rewriter_interface.h"
#include "googlesql/public/simple_catalog.h"
#include "googlesql/public/type.h"
#include "googlesql/public/types/type_factory.h"
#include "googlesql/resolved_ast/resolved_ast.h"
#include "googlesql/resolved_ast/resolved_node.h"
#include "googlesql/testdata/test_schema.pb.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "absl/flags/declare.h"
#include "absl/flags/flag.h"
#include "googlesql/base/check.h"
#include "absl/status/status.h"
#include "absl/strings/string_view.h"

ABSL_DECLARE_FLAG(double, googlesql_stack_usage_proportion_warning);

namespace googlesql {

using ::testing::Eq;
using ::testing::NotNull;

TEST(RewriteResolvedAstTest, RewriterDoesNotConflictWithExpressionColumnNames) {
  // The map function rewriters use a variable called "m" and a variable called
  // "k" with AnalyzeSubstitute to produce the rewrites. We want to verify that
  // these variables don't conflict with an expression column in the analyzer
  // options.
  AnalyzerOptions options;
  options.mutable_language()->EnableMaximumLanguageFeatures();
  GOOGLESQL_CHECK_OK(options.AddExpressionColumn("k", types::Int64Type()));

  TypeFactory types;
  const Type* map_type;
  GOOGLESQL_CHECK_OK(types.MakeProtoType(
      googlesql_test::MessageWithMapField::descriptor(), &map_type));
  GOOGLESQL_CHECK_OK(options.AddExpressionColumn("mapproto", map_type));

  SimpleCatalog catalog("catalog", &types);
  catalog.AddBuiltinFunctions(BuiltinFunctionOptions::AllReleasedFunctions());

  std::unique_ptr<const AnalyzerOutput> output;
  auto status = googlesql::AnalyzeExpression(
      "mapproto.string_int32_map[SAFE_KEY('foo')] + k", options, &catalog,
      &types, &output);
  EXPECT_TRUE(status.ok()) << status;
  if (output == nullptr) return;

  // Before we fixed the bug, this expression would not have analyzed correctly.
  // After, note that we can still see the user-provided resolved expression
  // columns in the appropriate places in the AST, but these names do not
  // conflict with the internal variable names for the map function rewrite
  // rules.
  EXPECT_THAT(output->resolved_expr()->DebugString(),
              R"sql(FunctionCall(GoogleSQL:$add(INT64, INT64) -> INT64)
+-Cast(INT32 -> INT64)
| +-SubqueryExpr
|   +-type=INT32
|   +-subquery_type=SCALAR
|   +-subquery=
|     +-ProjectScan
|       +-column_list=[$expr_subquery.$col1#6]
|       +-expr_list=
|       | +-$col1#6 :=
|       |   +-FunctionCall(GoogleSQL:$case_no_value(repeated(2) BOOL, repeated(2) INT32, INT32) -> INT32)
|       |     +-FunctionCall(GoogleSQL:$is_null(ARRAY<PROTO<googlesql_test.MessageWithMapField.StringInt32MapEntry>>) -> BOOL)
|       |     | +-ColumnRef(type=ARRAY<PROTO<googlesql_test.MessageWithMapField.StringInt32MapEntry>>, column=$subquery1.m#2)
|       |     +-Literal(type=INT32, value=NULL)
|       |     +-FunctionCall(GoogleSQL:$is_null(STRING) -> BOOL)
|       |     | +-ColumnRef(type=STRING, column=$subquery1.k#1)
|       |     +-Literal(type=INT32, value=NULL)
|       |     +-SubqueryExpr
|       |       +-type=INT32
|       |       +-subquery_type=SCALAR
|       |       +-parameter_list=
|       |       | +-ColumnRef(type=STRING, column=$subquery1.k#1)
|       |       | +-ColumnRef(type=ARRAY<PROTO<googlesql_test.MessageWithMapField.StringInt32MapEntry>>, column=$subquery1.m#2)
|       |       +-subquery=
|       |         +-LimitOffsetScan
|       |           +-column_list=[$expr_subquery.value#5]
|       |           +-input_scan=
|       |           | +-OrderByScan
|       |           |   +-column_list=[$expr_subquery.value#5]
|       |           |   +-is_ordered=TRUE
|       |           |   +-input_scan=
|       |           |   | +-ProjectScan
|       |           |   |   +-column_list=[$array.elem#3, $array_offset.offset#4, $expr_subquery.value#5]
|       |           |   |   +-expr_list=
|       |           |   |   | +-value#5 :=
|       |           |   |   |   +-GetProtoField
|       |           |   |   |     +-type=INT32
|       |           |   |   |     +-expr=
|       |           |   |   |     | +-ColumnRef(type=PROTO<googlesql_test.MessageWithMapField.StringInt32MapEntry>, column=$array.elem#3)
|       |           |   |   |     +-field_descriptor=value
|       |           |   |   |     +-default_value=0
|       |           |   |   +-input_scan=
|       |           |   |     +-FilterScan
|       |           |   |       +-column_list=[$array.elem#3, $array_offset.offset#4]
|       |           |   |       +-input_scan=
|       |           |   |       | +-ArrayScan
|       |           |   |       |   +-column_list=[$array.elem#3, $array_offset.offset#4]
|       |           |   |       |   +-array_expr_list=
|       |           |   |       |   | +-ColumnRef(type=ARRAY<PROTO<googlesql_test.MessageWithMapField.StringInt32MapEntry>>, column=$subquery1.m#2, is_correlated=TRUE)
|       |           |   |       |   +-element_column_list=[$array.elem#3]
|       |           |   |       |   +-array_offset_column=
|       |           |   |       |     +-ColumnHolder(column=$array_offset.offset#4)
|       |           |   |       +-filter_expr=
|       |           |   |         +-FunctionCall(GoogleSQL:$equal(STRING, STRING) -> BOOL)
|       |           |   |           +-GetProtoField
|       |           |   |           | +-type=STRING
|       |           |   |           | +-expr=
|       |           |   |           | | +-ColumnRef(type=PROTO<googlesql_test.MessageWithMapField.StringInt32MapEntry>, column=$array.elem#3)
|       |           |   |           | +-field_descriptor=key
|       |           |   |           | +-default_value=""
|       |           |   |           +-ColumnRef(type=STRING, column=$subquery1.k#1, is_correlated=TRUE)
|       |           |   +-order_by_item_list=
|       |           |     +-OrderByItem
|       |           |       +-column_ref=
|       |           |       | +-ColumnRef(type=INT64, column=$array_offset.offset#4)
|       |           |       +-is_descending=TRUE
|       |           +-limit=
|       |             +-Literal(type=INT64, value=1)
|       +-input_scan=
|         +-ProjectScan
|           +-column_list=$subquery1.[k#1, m#2]
|           +-expr_list=
|           | +-k#1 := Literal(type=STRING, value="foo")
|           | +-m#2 :=
|           |   +-GetProtoField
|           |     +-type=ARRAY<PROTO<googlesql_test.MessageWithMapField.StringInt32MapEntry>>
|           |     +-expr=
|           |     | +-ExpressionColumn(type=PROTO<googlesql_test.MessageWithMapField>, name="mapproto")
|           |     +-field_descriptor=string_int32_map
|           |     +-default_value=[]
|           +-input_scan=
|             +-SingleRowScan
+-ExpressionColumn(type=INT64, name="k")
)sql");
}

TEST(RewriteResolvedAstTest, RewriterWarnsComplextyJustOnce) {
  double old_value =
      absl::GetFlag(FLAGS_googlesql_stack_usage_proportion_warning);
  // Always warn.
  absl::SetFlag(&FLAGS_googlesql_stack_usage_proportion_warning, 0.0);
  AnalyzerOptions options;
  options.mutable_language()->EnableMaximumLanguageFeatures();
  GOOGLESQL_CHECK_OK(options.AddExpressionColumn("k", types::Int64Type()));

  TypeFactory types;
  const Type* map_type;
  GOOGLESQL_CHECK_OK(types.MakeProtoType(
      googlesql_test::MessageWithMapField::descriptor(), &map_type));
  GOOGLESQL_CHECK_OK(options.AddExpressionColumn("mapproto", map_type));

  SimpleCatalog catalog("catalog", &types);
  catalog.AddBuiltinFunctions(BuiltinFunctionOptions::AllReleasedFunctions());

  std::unique_ptr<const AnalyzerOutput> output;
  auto status = googlesql::AnalyzeExpression(
      "mapproto.string_int32_map[SAFE_KEY('foo')] + k", options, &catalog,
      &types, &output);
  GOOGLESQL_EXPECT_OK(status);

  EXPECT_THAT(output->deprecation_warnings(),
              ::testing::ElementsAre(absl_testing::StatusIs(
                  absl::StatusCode::kResourceExhausted)));
  absl::SetFlag(&FLAGS_googlesql_stack_usage_proportion_warning, old_value);
}

TEST(RewriteResolvedAstTest, SequenceNumberUnchangedIfNoRewritersApplied) {
  TypeFactory types;
  SimpleCatalog catalog("catalog", &types);
  catalog.AddBuiltinFunctions(BuiltinFunctionOptions::AllReleasedFunctions());

  auto table = std::make_unique<SimpleTable>(
      "T",
      std::vector<SimpleTable::NameAndType>{{"string_col", types.get_string()},
                                            {"int64_col", types.get_int64()}});
  catalog.AddOwnedTable(std::move(table));

  googlesql_base::SequenceNumber sequence_number;
  AnalyzerOptions options;
  options.set_column_id_sequence_number(&sequence_number);
  options.mutable_language()->DisableAllLanguageFeatures();
  // Disable TYPEOF rewriting and expect that no rewriters are applied.
  options.disable_rewrite(ResolvedASTRewrite::REWRITE_TYPEOF_FUNCTION);

  std::unique_ptr<const AnalyzerOutput> output;
  GOOGLESQL_ASSERT_OK(AnalyzeStatement(R"sql(SELECT TYPEOF(int64_col) FROM T)sql",
                             options, &catalog, &types, &output));
  ASSERT_THAT(output->resolved_statement(), NotNull());

  // One ID for each table column and one for the query's output column.
  EXPECT_THAT(output->max_column_id(), Eq(3));
  EXPECT_THAT(sequence_number.GetNext(), Eq(4));
}

// Same as above, but `REWRITE_TYPEOF_FUNCTION` is enabled.
TEST(RewriteResolvedAstTest,
     SequenceNumberIncrementedIfBuiltinRewriterApplied) {
  TypeFactory types;
  SimpleCatalog catalog("catalog", &types);
  catalog.AddBuiltinFunctions(BuiltinFunctionOptions::AllReleasedFunctions());

  auto table = std::make_unique<SimpleTable>(
      "T",
      std::vector<SimpleTable::NameAndType>{{"string_col", types.get_string()},
                                            {"int64_col", types.get_int64()}});
  catalog.AddOwnedTable(std::move(table));

  googlesql_base::SequenceNumber sequence_number;
  AnalyzerOptions options;
  options.set_column_id_sequence_number(&sequence_number);
  options.mutable_language()->DisableAllLanguageFeatures();
  options.enable_rewrite(ResolvedASTRewrite::REWRITE_TYPEOF_FUNCTION);

  std::unique_ptr<const AnalyzerOutput> output;
  GOOGLESQL_ASSERT_OK(AnalyzeStatement(R"sql(SELECT TYPEOF(int64_col) FROM T)sql",
                             options, &catalog, &types, &output));
  ASSERT_THAT(output->resolved_statement(), NotNull());

  // One ID for each table column and one for the query's output column. Note
  // that this is unchanged from the previous test case. The rewriter for
  // TYPEOF did not need to generate any new IDs.
  EXPECT_THAT(output->max_column_id(), Eq(3));

  // Whenever a rewriter is applied, the sequence number is always incremented
  // by one even though it is not necessary. This is because the framework does
  // not have any way to track the current value of the max column ID. It must
  // call `GetNext()` to get the next value, which mutates the sequence number.
  EXPECT_THAT(sequence_number.GetNext(), Eq(5));
}

namespace {

class NoopRewriter : public Rewriter {
 public:
  absl::StatusOr<std::unique_ptr<const ResolvedNode>> Rewrite(
      const AnalyzerOptions& options, std::unique_ptr<const ResolvedNode> input,
      Catalog& catalog, TypeFactory& type_factory,
      AnalyzerOutputProperties& output_properties) const override {
    return input;
  }

  std::string Name() const override { return "NoopRewriter"; }
};

}  // namespace

TEST(RewriteResolvedAstTest,
     SequenceNumberIncrementedIfLeadingRewriterApplied) {
  TypeFactory types;
  SimpleCatalog catalog("catalog", &types);

  auto table = std::make_unique<SimpleTable>(
      "T",
      std::vector<SimpleTable::NameAndType>{{"string_col", types.get_string()},
                                            {"int64_col", types.get_int64()}});
  catalog.AddOwnedTable(std::move(table));

  googlesql_base::SequenceNumber sequence_number;
  AnalyzerOptions options;
  options.set_column_id_sequence_number(&sequence_number);
  options.mutable_language()->DisableAllLanguageFeatures();
  options.add_leading_rewriter(std::make_shared<NoopRewriter>());

  std::unique_ptr<const AnalyzerOutput> output;
  GOOGLESQL_ASSERT_OK(AnalyzeStatement(R"sql(SELECT * FROM T)sql", options, &catalog,
                             &types, &output));
  ASSERT_THAT(output->resolved_statement(), NotNull());

  // One ID for each table column and one for the query's output column.
  EXPECT_THAT(output->max_column_id(), Eq(2));

  // Whenever a rewriter is applied, the sequence number is always incremented
  // by one even though it is not necessary. This is because the framework does
  // not have any way to track the current value of the max column ID. It must
  // call `GetNext()` to get the next value, which mutates the sequence number.
  EXPECT_THAT(sequence_number.GetNext(), Eq(4));
}

TEST(RewriteResolvedAstTest,
     SequenceNumberIncrementedIfTrailingRewriterApplied) {
  TypeFactory types;
  SimpleCatalog catalog("catalog", &types);

  auto table = std::make_unique<SimpleTable>(
      "T",
      std::vector<SimpleTable::NameAndType>{{"string_col", types.get_string()},
                                            {"int64_col", types.get_int64()}});
  catalog.AddOwnedTable(std::move(table));

  googlesql_base::SequenceNumber sequence_number;
  AnalyzerOptions options;
  options.set_column_id_sequence_number(&sequence_number);
  options.mutable_language()->DisableAllLanguageFeatures();
  options.add_trailing_rewriter(std::make_shared<NoopRewriter>());

  std::unique_ptr<const AnalyzerOutput> output;
  GOOGLESQL_ASSERT_OK(AnalyzeStatement(R"sql(SELECT * FROM T)sql", options, &catalog,
                             &types, &output));
  ASSERT_THAT(output->resolved_statement(), NotNull());

  // One ID for each table column and one for the query's output column.
  EXPECT_THAT(output->max_column_id(), Eq(2));

  // Whenever a rewriter is applied, the sequence number is always incremented
  // by one even though it is not necessary. This is because the framework does
  // not have any way to track the current value of the max column ID. It must
  // call `GetNext()` to get the next value, which mutates the sequence number.
  EXPECT_THAT(sequence_number.GetNext(), Eq(4));
}

}  // namespace googlesql
