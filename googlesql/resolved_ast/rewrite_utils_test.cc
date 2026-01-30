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

#include "googlesql/resolved_ast/rewrite_utils.h"

#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "googlesql/base/atomic_sequence_num.h"
#include "googlesql/base/testing/status_matchers.h"
#include "googlesql/public/analyzer_options.h"
#include "googlesql/public/builtin_function_options.h"
#include "googlesql/public/function.h"
#include "googlesql/public/function_signature.h"
#include "googlesql/public/id_string.h"
#include "googlesql/public/numeric_value.h"
#include "googlesql/public/options.pb.h"
#include "googlesql/public/proto/type_annotation.pb.h"
#include "googlesql/public/simple_catalog.h"
#include "googlesql/public/types/annotation.h"
#include "googlesql/public/types/simple_type.h"
#include "googlesql/public/types/type.h"
#include "googlesql/public/types/type_factory.h"
#include "googlesql/public/value.h"
#include "googlesql/resolved_ast/column_factory.h"
#include "googlesql/resolved_ast/resolved_ast.h"
#include "googlesql/resolved_ast/resolved_ast_builder.h"
#include "googlesql/resolved_ast/resolved_column.h"
#include "googlesql/resolved_ast/serialization.pb.h"
#include "googlesql/resolved_ast/test_utils.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/ascii.h"
#include "absl/strings/match.h"
#include "absl/types/span.h"
#include "googlesql/base/ret_check.h"

namespace googlesql {
namespace {

using ::googlesql::MakeResolvedTVFScan;
using ::testing::HasSubstr;
using ::testing::IsEmpty;
using ::testing::Not;
using ::testing::SizeIs;
using ::testing::UnorderedElementsAre;
using ::testing::Values;
using ::absl_testing::IsOk;
using ::absl_testing::StatusIs;

TEST(RewriteUtilsTest, CopyAndReplaceColumns) {
  googlesql_base::SequenceNumber sequence;
  ColumnFactory factory(0, &sequence);
  SimpleTable table("tab", {{"col", types::Int64Type()}});
  std::unique_ptr<ResolvedScan> input = MakeResolvedTableScan(
      {factory.MakeCol("t", "c", types::Int64Type())}, &table, nullptr);
  EXPECT_EQ(input->column_list(0).column_id(), 1);

  // Copy 'input' several times. The first time a new column is allocated but
  // subsequent copies will use the column already populated in 'map'.
  ColumnReplacementMap map;
  for (int i = 0; i < 5; ++i) {
    GOOGLESQL_ASSERT_OK_AND_ASSIGN(std::unique_ptr<ResolvedScan> output,
                         CopyResolvedASTAndRemapColumns(*input, factory, map));
    EXPECT_EQ(output->column_list(0).column_id(), 2);
    EXPECT_EQ(map.size(), 1);
  }

  // Repeat the experiment but feed the output of each iteration into the
  // input of the next. In this case we should get a new column each iteration
  // with a incremented column_id.
  map = {};
  for (int i = 1; i < 5; ++i) {
    GOOGLESQL_ASSERT_OK_AND_ASSIGN(std::unique_ptr<ResolvedScan> output,
                         CopyResolvedASTAndRemapColumns(*input, factory, map));
    // 2 columns for setup and first loop plus 1 for each iteration of this loop
    EXPECT_EQ(output->column_list(0).column_id(), i + 2);
    EXPECT_EQ(map.size(), i);
    input = std::move(output);
  }
}

TEST(RewriteUtilsTest, ShallowCopyAndReplaceAllColumns) {
  googlesql_base::SequenceNumber sequence;
  ColumnFactory factory(0, &sequence);
  SimpleTable table("tab", {{"col", types::Int64Type()}});
  std::unique_ptr<const ResolvedTableScan> input = MakeResolvedTableScan(
      {factory.MakeCol("t", "c", types::Int64Type())}, &table, nullptr);
  EXPECT_EQ(input->column_list(0).column_id(), 1);

  // Shallow copy `input` several times, feeding the output of each iteration
  // into the input of the next. In this case we should get a new column each
  // iteration with a incremented column_id.
  ColumnReplacementMap map;
  for (int i = 1; i < 5; ++i) {
    GOOGLESQL_ASSERT_OK_AND_ASSIGN(input,
                         RemapAllColumns(std::move(input), factory, map));

    EXPECT_EQ(input->column_list(0).column_id(), i + 1);
    EXPECT_EQ(map.size(), i);
  }
}

TEST(RewriteUtilsTest, ShallowCopyAndReplaceSpecifiedColumns) {
  googlesql_base::SequenceNumber sequence;
  ColumnFactory factory(0, &sequence);
  SimpleTable table("tab", {{"col", types::Int64Type()}});
  std::unique_ptr<const ResolvedTableScan> input =
      MakeResolvedTableScan({factory.MakeCol("t", "c1", types::Int64Type()),
                             factory.MakeCol("t", "c2", types::StringType())},
                            &table, nullptr);
  EXPECT_EQ(input->column_list(0).column_id(), 1);

  // Rewrite `input` several times. When the map is empty, it should be a no-op.
  ColumnReplacementMap map;
  for (int i = 0; i < 5; ++i) {
    GOOGLESQL_ASSERT_OK_AND_ASSIGN(input, RemapSpecifiedColumns(std::move(input), map));
    EXPECT_EQ(input->column_list(0).column_id(), 1);
    EXPECT_EQ(map.size(), 0);
  }

  map[input->column_list(0)] = factory.MakeCol("t", "c3", types::Int64Type());
  // Repeat the experiment with a non-empty map. Only the first iteration
  // should be a no-op.
  for (int i = 1; i < 5; ++i) {
    GOOGLESQL_ASSERT_OK_AND_ASSIGN(std::unique_ptr<const ResolvedTableScan> output,
                         RemapSpecifiedColumns(std::move(input), map));
    // The first column should be replaced with a new column once.
    EXPECT_EQ(output->column_list(0).column_id(), 3);
    EXPECT_EQ(map.size(), 1);
    input = std::move(output);
  }
}

TEST(RewriteUtilsTest, RemapSpecifiedColumnsInColumnList) {
  IdStringPool id_string_pool;
  ColumnFactory factory(0, id_string_pool,
                        std::make_unique<googlesql_base::SequenceNumber>());
  ResolvedColumnList columns;
  columns.emplace_back(factory.MakeCol("t", "c1", types::Int64Type()));
  columns.emplace_back(factory.MakeCol("t", "c2", types::StringType()));
  EXPECT_EQ(columns[0].column_id(), 1);
  EXPECT_EQ(columns[0].name(), "c1");
  EXPECT_EQ(columns[1].column_id(), 2);

  ColumnReplacementMap map;
  map[columns[0]] = factory.MakeCol("t", "c3", types::DoubleType());
  ResolvedColumnList output = RemapColumnList(columns, map);
  EXPECT_EQ(output[0].column_id(), 3);
  EXPECT_EQ(output[0].name(), "c3");
  EXPECT_EQ(output[1].column_id(), 2);
}

TEST(RewriteUtilsTest, RemoveUnusedColumnRefs) {
  const Type* type = types::BoolType();
  googlesql_base::SequenceNumber sequence;
  IdStringPool id_string_pool;
  ColumnFactory factory(0, &id_string_pool, &sequence);
  ResolvedColumn cola = factory.MakeCol("table", "cola", type);
  ResolvedColumn colb = factory.MakeCol("table", "colb", type);
  std::vector<std::unique_ptr<const ResolvedColumnRef>> column_refs;
  column_refs.emplace_back(
      MakeResolvedColumnRef(type, cola, /*is_correlated=*/true));
  column_refs.emplace_back(
      MakeResolvedColumnRef(type, colb, /*is_correlated=*/false));

  std::unique_ptr<const ResolvedScan> single_row_scan =
      MakeResolvedSingleRowScan(/*column_list=*/{});
  GOOGLESQL_ASSERT_OK(RemoveUnusedColumnRefs(*single_row_scan, column_refs));
  EXPECT_THAT(column_refs, IsEmpty());

  std::unique_ptr<const ResolvedExpr> filter_expr =
      MakeResolvedColumnRef(type, colb, /*is_correlated=*/true);
  std::unique_ptr<const ResolvedScan> filter_scan =
      MakeResolvedFilterScan(/*column_list=*/{},
                             /*input_scan=*/std::move(single_row_scan),
                             /*filter_expr=*/std::move(filter_expr));

  column_refs.emplace_back(
      MakeResolvedColumnRef(type, cola, /*is_correlated=*/true));
  column_refs.emplace_back(
      MakeResolvedColumnRef(type, colb, /*is_correlated=*/false));
  GOOGLESQL_ASSERT_OK(RemoveUnusedColumnRefs(*filter_scan, column_refs));
  EXPECT_THAT(column_refs, SizeIs(1));
  EXPECT_EQ(column_refs.front()->column(), colb);
}

TEST(RewriteUtilsTest, SortUniqueColumnRefs) {
  const Type* type = types::StringType();
  googlesql_base::SequenceNumber sequence;
  ColumnFactory factory(0, &sequence);
  ResolvedColumn cola = factory.MakeCol("table", "cola", type);
  ResolvedColumn colb = factory.MakeCol("table", "colb", type);
  ResolvedColumn colc = factory.MakeCol("table", "colc", type);

  bool kCorrelated = true;
  std::vector<std::unique_ptr<const ResolvedColumnRef>> column_refs;
  column_refs.emplace_back(MakeResolvedColumnRef(type, colb, kCorrelated));
  column_refs.emplace_back(MakeResolvedColumnRef(type, cola, kCorrelated));
  column_refs.emplace_back(MakeResolvedColumnRef(type, cola, kCorrelated));
  column_refs.emplace_back(MakeResolvedColumnRef(type, colb, kCorrelated));
  column_refs.emplace_back(MakeResolvedColumnRef(type, colc, kCorrelated));
  column_refs.emplace_back(MakeResolvedColumnRef(type, colc, !kCorrelated));
  SortUniqueColumnRefs(column_refs);

  ASSERT_EQ(column_refs.size(), 4);
  EXPECT_EQ(column_refs[0]->column(), cola);
  EXPECT_EQ(column_refs[1]->column(), colb);
  EXPECT_EQ(column_refs[2]->column(), colc);
  EXPECT_EQ(column_refs[3]->column(), colc);
  EXPECT_FALSE(column_refs[2]->is_correlated());
  EXPECT_TRUE(column_refs[3]->is_correlated());
}

class GetCorrelatedColumnSetTest : public ::testing::Test {
 public:
  GetCorrelatedColumnSetTest()
      : column_factory_(0, &sequence_),
        col_a_(column_factory_.MakeCol("table", "a", types::Int64Type())),
        col_b_(column_factory_.MakeCol("table", "b", types::Int64Type())),
        col_c_(column_factory_.MakeCol("table", "c", types::Int64Type())) {}

 protected:
  googlesql_base::SequenceNumber sequence_;
  ColumnFactory column_factory_;
  ResolvedColumn col_a_;
  ResolvedColumn col_b_;
  ResolvedColumn col_c_;
};

TEST_F(GetCorrelatedColumnSetTest, NoColumnRefs) {
  std::unique_ptr<const ResolvedScan> scan =
      MakeResolvedSingleRowScan(/*column_list=*/{});
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(auto correlated_columns, GetCorrelatedColumnSet(*scan));
  EXPECT_THAT(correlated_columns, IsEmpty());
}

TEST_F(GetCorrelatedColumnSetTest, OnlyUncorrelatedRefs) {
  std::unique_ptr<const ResolvedExpr> ref1 =
      MakeResolvedColumnRef(col_a_.type(), col_a_, /*is_correlated=*/false);
  std::unique_ptr<const ResolvedExpr> ref2 =
      MakeResolvedColumnRef(col_b_.type(), col_b_, /*is_correlated=*/false);

  ResolvedColumn computed_col1 =
      column_factory_.MakeCol("t", "x", col_a_.type());
  ResolvedColumn computed_col2 =
      column_factory_.MakeCol("t", "y", col_b_.type());
  std::vector<std::unique_ptr<const ResolvedComputedColumn>> expr_list;
  expr_list.push_back(
      MakeResolvedComputedColumn(computed_col1, std::move(ref1)));
  expr_list.push_back(
      MakeResolvedComputedColumn(computed_col2, std::move(ref2)));

  auto project_scan = MakeResolvedProjectScan(
      {computed_col1, computed_col2}, std::move(expr_list),
      MakeResolvedSingleRowScan(/*column_list=*/{}));

  GOOGLESQL_ASSERT_OK_AND_ASSIGN(auto correlated_columns,
                       GetCorrelatedColumnSet(*project_scan));
  EXPECT_THAT(correlated_columns, IsEmpty());
}

TEST_F(GetCorrelatedColumnSetTest, OnlyCorrelatedRefs) {
  std::unique_ptr<const ResolvedExpr> ref1 =
      MakeResolvedColumnRef(col_a_.type(), col_a_, /*is_correlated=*/true);
  std::unique_ptr<const ResolvedExpr> ref2 =
      MakeResolvedColumnRef(col_b_.type(), col_b_, /*is_correlated=*/true);

  ResolvedColumn computed_col1 =
      column_factory_.MakeCol("t", "x", col_a_.type());
  ResolvedColumn computed_col2 =
      column_factory_.MakeCol("t", "y", col_b_.type());
  std::vector<std::unique_ptr<const ResolvedComputedColumn>> expr_list;
  expr_list.push_back(
      MakeResolvedComputedColumn(computed_col1, std::move(ref1)));
  expr_list.push_back(
      MakeResolvedComputedColumn(computed_col2, std::move(ref2)));

  auto project_scan = MakeResolvedProjectScan(
      {computed_col1, computed_col2}, std::move(expr_list),
      MakeResolvedSingleRowScan(/*column_list=*/{}));

  GOOGLESQL_ASSERT_OK_AND_ASSIGN(auto correlated_columns,
                       GetCorrelatedColumnSet(*project_scan));
  EXPECT_THAT(correlated_columns, UnorderedElementsAre(col_a_, col_b_));
}

TEST_F(GetCorrelatedColumnSetTest, MixedCorrelatedAndUncorrelatedRefs) {
  std::unique_ptr<const ResolvedExpr> ref1 =
      MakeResolvedColumnRef(col_a_.type(), col_a_, /*is_correlated=*/true);
  std::unique_ptr<const ResolvedExpr> ref2 =
      MakeResolvedColumnRef(col_b_.type(), col_b_, /*is_correlated=*/false);

  ResolvedColumn computed_col1 =
      column_factory_.MakeCol("t", "x", col_a_.type());
  ResolvedColumn computed_col2 =
      column_factory_.MakeCol("t", "y", col_b_.type());
  std::vector<std::unique_ptr<const ResolvedComputedColumn>> expr_list;
  expr_list.push_back(
      MakeResolvedComputedColumn(computed_col1, std::move(ref1)));
  expr_list.push_back(
      MakeResolvedComputedColumn(computed_col2, std::move(ref2)));

  auto project_scan = MakeResolvedProjectScan(
      {computed_col1, computed_col2}, std::move(expr_list),
      MakeResolvedSingleRowScan(/*column_list=*/{}));

  GOOGLESQL_ASSERT_OK_AND_ASSIGN(auto correlated_columns,
                       GetCorrelatedColumnSet(*project_scan));
  EXPECT_THAT(correlated_columns, UnorderedElementsAre(col_a_));
}

TEST(RewriteUtilsTest, SafePreconditionWithIferrorOverride) {
  SimpleCatalog catalog("test_catalog");
  catalog.AddBuiltinFunctions(BuiltinFunctionOptions::AllReleasedFunctions());
  AnalyzerOptions analyzer_options;

  GOOGLESQL_EXPECT_OK(
      CheckCatalogSupportsSafeMode("whatever", analyzer_options, catalog));

  // If we remove IFEROR from the catalog, we should fail the precondition
  // checks.
  auto is_iferror = [](const Function* fn) {
    return googlesql_base::CaseEqual(fn->Name(), "iferror");
  };
  std::vector<std::unique_ptr<const Function>> removed;
  catalog.RemoveFunctions(is_iferror, removed);
  ASSERT_EQ(removed.size(), 1);

  EXPECT_THAT(
      CheckCatalogSupportsSafeMode("whatever", analyzer_options, catalog),
      Not(IsOk()));

  // Adding the function back to the catalog should still work.
  const Function* iferror = removed.back().get();
  catalog.AddFunction(iferror);
  GOOGLESQL_EXPECT_OK(
      CheckCatalogSupportsSafeMode("whatever", analyzer_options, catalog));

  // Replacing iferror with an identical copy should still satisfy the
  // preconditions.
  Function iferror_copy(iferror->Name(), iferror->GetGroup(), iferror->mode(),
                        iferror->signatures(), iferror->function_options());
  ASSERT_EQ(catalog.RemoveFunctions(is_iferror), 1);
  catalog.AddFunction(&iferror_copy);
  GOOGLESQL_EXPECT_OK(
      CheckCatalogSupportsSafeMode("whatever", analyzer_options, catalog));

  // Replacing iferror with a non-builtin group copy should not satisfy the
  // preconditions.
  Function non_builtin_iferror(iferror->Name(), /*group=*/"non-builtin",
                               iferror->mode(), iferror->signatures(),
                               iferror->function_options());
  ASSERT_EQ(catalog.RemoveFunctions(is_iferror), 1);
  catalog.AddFunction(&non_builtin_iferror);
  EXPECT_THAT(
      CheckCatalogSupportsSafeMode("whatever", analyzer_options, catalog),
      Not(IsOk()));
}

TEST(RewriteUtilsTest, SafePreconditionWithIferrorLookupFailure) {
  class ErrorThrowingCatalog : public SimpleCatalog {
   public:
    ErrorThrowingCatalog() : SimpleCatalog("error_throwing_catalog") {
      AddBuiltinFunctions(BuiltinFunctionOptions::AllReleasedFunctions());
    }
    absl::Status GetFunction(
        const std::string& name, const Function** function,
        const FindOptions& options = FindOptions()) override {
      GOOGLESQL_RET_CHECK_FAIL() << "fail-for-test";
    }
  };
  ErrorThrowingCatalog catalog;
  AnalyzerOptions analyzer_options;
  EXPECT_THAT(
      CheckCatalogSupportsSafeMode("whatever", analyzer_options, catalog),
      StatusIs(absl::StatusCode::kInternal));
}

static AnalyzerOptions MakeAnalyzerOptions() {
  AnalyzerOptions options;
  options.mutable_language()->SetSupportsAllStatementKinds();
  options.mutable_language()->EnableLanguageFeature(
      LanguageFeature::FEATURE_COLLATION_SUPPORT);
  options.mutable_language()->EnableLanguageFeature(
      LanguageFeature::FEATURE_ANNOTATION_FRAMEWORK);
  return options;
}

class FunctionCallBuilderTest : public ::testing::Test {
 public:
  FunctionCallBuilderTest()
      : analyzer_options_(MakeAnalyzerOptions()),
        catalog_("function_builder_catalog"),
        fn_builder_(analyzer_options_, catalog_, type_factory_) {
    catalog_.AddBuiltinFunctions(
        BuiltinFunctionOptions(analyzer_options_.language()));
  }

  AnalyzerOptions analyzer_options_;
  SimpleCatalog catalog_;
  TypeFactory type_factory_;
  FunctionCallBuilder fn_builder_;
};

TEST_F(FunctionCallBuilderTest, LikeTest) {
  std::unique_ptr<ResolvedExpr> input = MakeResolvedLiteral(
      types::StringType(), Value::String("bar"), /*has_explicit_type=*/true);
  ASSERT_NE(input, nullptr);
  std::unique_ptr<ResolvedExpr> pattern = MakeResolvedLiteral(
      types::StringType(), Value::String("%r"), /*has_explicit_type=*/true);
  ASSERT_NE(pattern, nullptr);

  GOOGLESQL_ASSERT_OK_AND_ASSIGN(std::unique_ptr<const ResolvedExpr> like_fn,
                       fn_builder_.Like(std::move(input), std::move(pattern)));
  EXPECT_EQ(like_fn->DebugString(), absl::StripLeadingAsciiWhitespace(R"(
FunctionCall(GoogleSQL:$like(STRING, STRING) -> BOOL)
+-Literal(type=STRING, value="bar", has_explicit_type=TRUE)
+-Literal(type=STRING, value="%r", has_explicit_type=TRUE)
)"));
}

static absl::StatusOr<std::unique_ptr<const ResolvedExpr>> MakeArrayOfStrings(
    FunctionCallBuilder& fn_builder, absl::Span<const std::string> strings) {
  std::vector<std::unique_ptr<const ResolvedExpr>> make_array_args;
  for (const std::string& str : strings) {
    make_array_args.emplace_back(MakeResolvedLiteral(
        types::StringType(), Value::String(str), /*has_explicit_type=*/true));
  }

  const Type* type = make_array_args[0]->type();
  return fn_builder.MakeArray(type, std::move(make_array_args));
}

TEST_F(FunctionCallBuilderTest, MakeArray) {
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(std::unique_ptr<const ResolvedExpr> make_arr_fn,
                       MakeArrayOfStrings(fn_builder_, {"foo", "bar"}));
  EXPECT_EQ(make_arr_fn->DebugString(), absl::StripLeadingAsciiWhitespace(R"(
FunctionCall(GoogleSQL:$make_array(repeated(2) STRING) -> ARRAY<STRING>)
+-Literal(type=STRING, value="foo", has_explicit_type=TRUE)
+-Literal(type=STRING, value="bar", has_explicit_type=TRUE)
)"));
}

TEST_F(FunctionCallBuilderTest, MakeArrayWithAnnotation) {
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(std::vector<std::unique_ptr<const ResolvedExpr>> args,
                       testing::BuildResolvedLiteralsWithCollationForTest(
                           {{"foo", "und:ci"}, {"bar", "und:ci"}},
                           analyzer_options_, catalog_, type_factory_));
  const Type* type = args[0]->type();
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(std::unique_ptr<const ResolvedExpr> make_arr_fn,
                       fn_builder_.MakeArray(type, std::move(args)));

  EXPECT_EQ(make_arr_fn->DebugString(), absl::StripLeadingAsciiWhitespace(R"(
FunctionCall(GoogleSQL:$make_array(repeated(2) STRING) -> ARRAY<STRING>)
+-type_annotation_map=<{Collation:"und:ci"}>
+-FunctionCall(GoogleSQL:collate(STRING, STRING) -> STRING)
| +-type_annotation_map={Collation:"und:ci"}
| +-Literal(type=STRING, value="foo", has_explicit_type=TRUE)
| +-Literal(type=STRING, value="und:ci", preserve_in_literal_remover=TRUE)
+-FunctionCall(GoogleSQL:collate(STRING, STRING) -> STRING)
  +-type_annotation_map={Collation:"und:ci"}
  +-Literal(type=STRING, value="bar", has_explicit_type=TRUE)
  +-Literal(type=STRING, value="und:ci", preserve_in_literal_remover=TRUE)
)"));
}

TEST_F(FunctionCallBuilderTest, MakeArrayWithMixedAnnotation) {
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(std::vector<std::unique_ptr<const ResolvedExpr>> args,
                       testing::BuildResolvedLiteralsWithCollationForTest(
                           {{"foo", "und:ci"}, {"bar", "binary"}},
                           analyzer_options_, catalog_, type_factory_));

  const Type* type = args[0]->type();
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(std::unique_ptr<const ResolvedExpr> make_arr_fn,
                       fn_builder_.MakeArray(type, std::move(args)));

  EXPECT_EQ(make_arr_fn->DebugString(), absl::StripLeadingAsciiWhitespace(R"(
FunctionCall(GoogleSQL:$make_array(repeated(2) STRING) -> ARRAY<STRING>)
+-FunctionCall(GoogleSQL:collate(STRING, STRING) -> STRING)
| +-type_annotation_map={Collation:"und:ci"}
| +-Literal(type=STRING, value="foo", has_explicit_type=TRUE)
| +-Literal(type=STRING, value="und:ci", preserve_in_literal_remover=TRUE)
+-FunctionCall(GoogleSQL:collate(STRING, STRING) -> STRING)
  +-type_annotation_map={Collation:"binary"}
  +-Literal(type=STRING, value="bar", has_explicit_type=TRUE)
  +-Literal(type=STRING, value="binary", preserve_in_literal_remover=TRUE)
)"));
}

TEST_F(FunctionCallBuilderTest, ArrayConcat) {
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(std::unique_ptr<const ResolvedExpr> just_foo_array,
                       MakeArrayOfStrings(fn_builder_, {"foo"}));
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(std::unique_ptr<const ResolvedExpr> bar_baz_array,
                       MakeArrayOfStrings(fn_builder_, {"bar", "baz"}));
  std::vector<std::unique_ptr<const ResolvedExpr>> args;
  args.emplace_back(std::move(just_foo_array));
  args.emplace_back(std::move(bar_baz_array));
  const ArrayType* array_type;
  GOOGLESQL_ASSERT_OK(type_factory_.MakeArrayType(types::StringType(), &array_type));
  args.emplace_back(
      MakeResolvedLiteral(array_type, Value::EmptyArray(array_type)));
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(std::unique_ptr<const ResolvedExpr> array_concat_fn,
                       fn_builder_.ArrayConcat(std::move(args)));

  EXPECT_EQ(array_concat_fn->DebugString(),
            absl::StripLeadingAsciiWhitespace(R"(
FunctionCall(GoogleSQL:array_concat(ARRAY<STRING>, repeated(2) ARRAY<STRING>) -> ARRAY<STRING>)
+-FunctionCall(GoogleSQL:$make_array(repeated(1) STRING) -> ARRAY<STRING>)
| +-Literal(type=STRING, value="foo", has_explicit_type=TRUE)
+-FunctionCall(GoogleSQL:$make_array(repeated(2) STRING) -> ARRAY<STRING>)
| +-Literal(type=STRING, value="bar", has_explicit_type=TRUE)
| +-Literal(type=STRING, value="baz", has_explicit_type=TRUE)
+-Literal(type=ARRAY<STRING>, value=[])
)"));
}

TEST_F(FunctionCallBuilderTest, CaseNoValueElseTest) {
  std::vector<std::unique_ptr<const ResolvedExpr>> condition_args;
  std::vector<std::unique_ptr<const ResolvedExpr>> result_args;

  condition_args.push_back(MakeResolvedLiteral(
      types::BoolType(), Value::Bool(true), /*has_explicit_type=*/true));
  result_args.push_back(MakeResolvedLiteral(
      types::StringType(), Value::String("foo"), /*has_explicit_type=*/true));
  condition_args.push_back(MakeResolvedLiteral(
      types::BoolType(), Value::Bool(false), /*has_explicit_type=*/true));
  result_args.push_back(MakeResolvedLiteral(
      types::StringType(), Value::String("bar"), /*has_explicit_type=*/true));

  std::unique_ptr<ResolvedExpr> else_result = MakeResolvedLiteral(
      types::StringType(), Value::String("baz"), /*has_explicit_type=*/true);

  GOOGLESQL_ASSERT_OK_AND_ASSIGN(
      std::unique_ptr<const ResolvedExpr> case_fn,
      fn_builder_.CaseNoValue(std::move(condition_args), std::move(result_args),
                              std::move(else_result)));
  EXPECT_EQ(case_fn->DebugString(), absl::StripLeadingAsciiWhitespace(R"(
FunctionCall(GoogleSQL:$case_no_value(repeated(2) BOOL, repeated(2) STRING, STRING) -> STRING)
+-Literal(type=BOOL, value=true, has_explicit_type=TRUE)
+-Literal(type=STRING, value="foo", has_explicit_type=TRUE)
+-Literal(type=BOOL, value=false, has_explicit_type=TRUE)
+-Literal(type=STRING, value="bar", has_explicit_type=TRUE)
+-Literal(type=STRING, value="baz", has_explicit_type=TRUE)
)"));
}

TEST_F(FunctionCallBuilderTest, NotTest) {
  std::unique_ptr<ResolvedExpr> input = MakeResolvedLiteral(
      types::BoolType(), Value::Bool(true), /*has_explicit_type=*/true);
  ASSERT_NE(input, nullptr);

  GOOGLESQL_ASSERT_OK_AND_ASSIGN(std::unique_ptr<const ResolvedExpr> not_fn,
                       fn_builder_.Not(std::move(input)));
  EXPECT_EQ(not_fn->DebugString(), absl::StripLeadingAsciiWhitespace(R"(
FunctionCall(GoogleSQL:$not(BOOL) -> BOOL)
+-Literal(type=BOOL, value=true, has_explicit_type=TRUE)
)"));
}

TEST_F(FunctionCallBuilderTest, EqualTest) {
  std::unique_ptr<ResolvedExpr> input =
      MakeResolvedLiteral(types::StringType(), Value::StringValue("true"),
                          /*has_explicit_type=*/true);
  std::unique_ptr<ResolvedExpr> input2 =
      MakeResolvedLiteral(types::StringType(), Value::StringValue("false"),
                          /*has_explicit_type=*/true);

  GOOGLESQL_ASSERT_OK_AND_ASSIGN(std::unique_ptr<const ResolvedExpr> equal_fn,
                       fn_builder_.Equal(std::move(input), std::move(input2)));
  EXPECT_EQ(equal_fn->DebugString(), absl::StripLeadingAsciiWhitespace(R"(
FunctionCall(GoogleSQL:$equal(STRING, STRING) -> BOOL)
+-Literal(type=STRING, value="true", has_explicit_type=TRUE)
+-Literal(type=STRING, value="false", has_explicit_type=TRUE)
)"));
}

TEST_F(FunctionCallBuilderTest, EqualArgumentTypeMismatchTest) {
  std::unique_ptr<ResolvedExpr> input = MakeResolvedLiteral(
      types::BoolType(), Value::Bool(true), /*has_explicit_type=*/true);
  std::unique_ptr<ResolvedExpr> input2 =
      MakeResolvedLiteral(types::StringType(), Value::StringValue("true"),
                          /*has_explicit_type=*/true);

  EXPECT_THAT(fn_builder_.Equal(std::move(input), std::move(input2)),
              StatusIs(absl::StatusCode::kInternal));
}

TEST_F(FunctionCallBuilderTest, EqualArgumentTypeDoesNotSupportEqualityTest) {
  // TokenList type does not support equality.
  std::unique_ptr<ResolvedExpr> input =
      MakeResolvedLiteral(types::TokenListType(), Value::NullTokenList(),
                          /*has_explicit_type=*/true);
  std::unique_ptr<ResolvedExpr> input2 =
      MakeResolvedLiteral(types::TokenListType(), Value::NullTokenList(),
                          /*has_explicit_type=*/true);

  EXPECT_THAT(fn_builder_.Equal(std::move(input), std::move(input2)),
              StatusIs(absl::StatusCode::kInternal));
}

TEST_F(FunctionCallBuilderTest, GreaterOrEqualForStringsTest) {
  std::unique_ptr<ResolvedExpr> input1 =
      MakeResolvedLiteral(types::StringType(), Value::StringValue("string1"),
                          /*has_explicit_type=*/true);
  std::unique_ptr<ResolvedExpr> input2 =
      MakeResolvedLiteral(types::StringType(), Value::StringValue("string2"),
                          /*has_explicit_type=*/true);

  GOOGLESQL_ASSERT_OK_AND_ASSIGN(
      std::unique_ptr<const ResolvedExpr> function,
      fn_builder_.GreaterOrEqual(std::move(input1), std::move(input2)));
  EXPECT_EQ(function->DebugString(), absl::StripLeadingAsciiWhitespace(R"(
FunctionCall(GoogleSQL:$greater_or_equal(STRING, STRING) -> BOOL)
+-Literal(type=STRING, value="string1", has_explicit_type=TRUE)
+-Literal(type=STRING, value="string2", has_explicit_type=TRUE)
)"));
}

TEST_F(FunctionCallBuilderTest, GreaterOrEqualForInt64Test) {
  std::unique_ptr<ResolvedExpr> input1 =
      MakeResolvedLiteral(types::Int64Type(), Value::Int64(1),
                          /*has_explicit_type=*/true);
  std::unique_ptr<ResolvedExpr> input2 =
      MakeResolvedLiteral(types::Int64Type(), Value::Int64(2),
                          /*has_explicit_type=*/true);

  GOOGLESQL_ASSERT_OK_AND_ASSIGN(
      std::unique_ptr<const ResolvedExpr> function,
      fn_builder_.GreaterOrEqual(std::move(input1), std::move(input2)));
  EXPECT_EQ(function->DebugString(), absl::StripLeadingAsciiWhitespace(R"(
FunctionCall(GoogleSQL:$greater_or_equal(INT64, INT64) -> BOOL)
+-Literal(type=INT64, value=1, has_explicit_type=TRUE)
+-Literal(type=INT64, value=2, has_explicit_type=TRUE)
)"));
}

TEST_F(FunctionCallBuilderTest,
       GreaterOrEqualRefusesToCompareStringsAndIntegers) {
  std::unique_ptr<ResolvedExpr> input1 = MakeResolvedLiteral(
      types::StringType(), Value::StringValue("some_string"),
      /*has_explicit_type=*/true);
  std::unique_ptr<ResolvedExpr> input2 =
      MakeResolvedLiteral(types::Int64Type(), Value::Int64(2),
                          /*has_explicit_type=*/true);

  EXPECT_THAT(fn_builder_.GreaterOrEqual(std::move(input1), std::move(input2)),
              StatusIs(absl::StatusCode::kInternal));
}

TEST_F(FunctionCallBuilderTest, GreaterOrEqualRefusesToCompareInt32AndInt64) {
  std::unique_ptr<ResolvedExpr> input1 =
      MakeResolvedLiteral(types::Int32Type(), Value::Int32(1),
                          /*has_explicit_type=*/true);
  std::unique_ptr<ResolvedExpr> input2 =
      MakeResolvedLiteral(types::Int64Type(), Value::Int64(2),
                          /*has_explicit_type=*/true);

  EXPECT_THAT(fn_builder_.GreaterOrEqual(std::move(input1), std::move(input2)),
              StatusIs(absl::StatusCode::kInternal));
}

TEST_F(FunctionCallBuilderTest,
       GreaterOrEqualCanCompareSignedAndUnsignedInt64) {
  std::unique_ptr<ResolvedExpr> input1 =
      MakeResolvedLiteral(types::Uint64Type(), Value::Uint64(1),
                          /*has_explicit_type=*/true);
  std::unique_ptr<ResolvedExpr> input2 =
      MakeResolvedLiteral(types::Int64Type(), Value::Int64(2),
                          /*has_explicit_type=*/true);

  GOOGLESQL_ASSERT_OK_AND_ASSIGN(
      std::unique_ptr<const ResolvedExpr> function,
      fn_builder_.GreaterOrEqual(std::move(input1), std::move(input2)));
  EXPECT_EQ(function->DebugString(), absl::StripLeadingAsciiWhitespace(R"(
FunctionCall(GoogleSQL:$greater_or_equal(UINT64, INT64) -> BOOL)
+-Literal(type=UINT64, value=1, has_explicit_type=TRUE)
+-Literal(type=INT64, value=2, has_explicit_type=TRUE)
)"));
}

TEST_F(FunctionCallBuilderTest, GreaterOrEqualTypeDoesNotSupportOrdering) {
  std::unique_ptr<ResolvedExpr> input1 = MakeResolvedLiteral(
      types::JsonType(), Value::NullJson(), /*has_explicit_type=*/true);
  std::unique_ptr<ResolvedExpr> input2 = MakeResolvedLiteral(
      types::JsonType(), Value::NullJson(), /*has_explicit_type=*/true);

  EXPECT_THAT(fn_builder_.GreaterOrEqual(std::move(input1), std::move(input2)),
              StatusIs(absl::StatusCode::kInternal));
}

TEST_F(FunctionCallBuilderTest, SubtractionTestForInt64) {
  // Int64 - Int64 is a built-in signature type in the `SimpleCatalog`.
  std::unique_ptr<ResolvedExpr> input1 =
      MakeResolvedLiteral(types::Int64Type(), Value::Int64(1),
                          /*has_explicit_type=*/true);
  std::unique_ptr<ResolvedExpr> input2 =
      MakeResolvedLiteral(types::Int64Type(), Value::Int64(2),
                          /*has_explicit_type=*/true);

  GOOGLESQL_ASSERT_OK_AND_ASSIGN(
      std::unique_ptr<const ResolvedExpr> function,
      fn_builder_.Subtract(std::move(input1), std::move(input2)));
  EXPECT_EQ(function->DebugString(), absl::StripLeadingAsciiWhitespace(R"(
FunctionCall(GoogleSQL:$subtract(INT64, INT64) -> INT64)
+-Literal(type=INT64, value=1, has_explicit_type=TRUE)
+-Literal(type=INT64, value=2, has_explicit_type=TRUE)
)"));
}

TEST_F(FunctionCallBuilderTest, SubtractionTestForDateMinusInt64) {
  // Datetime - Int64 is a built-in signature type in the `SimpleCatalog`.
  std::unique_ptr<ResolvedExpr> input1 = MakeResolvedLiteral(
      types::DateType(), Value::Date(/*3rd Nov. 1992*/ 8342),
      /*has_explicit_type=*/true);

  std::unique_ptr<ResolvedExpr> input2 =
      MakeResolvedLiteral(types::Int64Type(), Value::Int64(10),
                          /*has_explicit_type=*/true);

  GOOGLESQL_ASSERT_OK_AND_ASSIGN(
      std::unique_ptr<const ResolvedExpr> function,
      fn_builder_.Subtract(std::move(input1), std::move(input2)));
  EXPECT_EQ(function->DebugString(), absl::StripLeadingAsciiWhitespace(R"(
FunctionCall(GoogleSQL:$subtract(DATE, INT64) -> DATE)
+-Literal(type=DATE, value=1992-11-03, has_explicit_type=TRUE)
+-Literal(type=INT64, value=10, has_explicit_type=TRUE)
)"));
}

TEST_F(FunctionCallBuilderTest,
       SubtractRefusesToWorkOnSignedAndUnsignedIntegers) {
  // Int64 - Uint64 is *not* a built-in function in `SimpleCatalog`.
  std::unique_ptr<ResolvedExpr> input1 =
      MakeResolvedLiteral(types::Int64Type(), Value::Int64(1),
                          /*has_explicit_type=*/true);

  std::unique_ptr<ResolvedExpr> input2 =
      MakeResolvedLiteral(types::Uint64Type(), Value::Uint64(1),
                          /*has_explicit_type=*/true);

  EXPECT_THAT(fn_builder_.Subtract(std::move(input1), std::move(input2)),
              StatusIs(absl::StatusCode::kInternal));
}

TEST_F(FunctionCallBuilderTest, SafeSubtractionTestForInt64) {
  // Int64 - Int64 is a built-in signature type in the `SimpleCatalog`.
  std::unique_ptr<ResolvedExpr> input1 =
      MakeResolvedLiteral(types::Int64Type(), Value::Int64(1),
                          /*has_explicit_type=*/true);
  std::unique_ptr<ResolvedExpr> input2 =
      MakeResolvedLiteral(types::Int64Type(), Value::Int64(2),
                          /*has_explicit_type=*/true);

  GOOGLESQL_ASSERT_OK_AND_ASSIGN(
      std::unique_ptr<const ResolvedExpr> function,
      fn_builder_.SafeSubtract(std::move(input1), std::move(input2)));
  EXPECT_EQ(function->DebugString(), absl::StripLeadingAsciiWhitespace(R"(
FunctionCall(GoogleSQL:safe_subtract(INT64, INT64) -> INT64)
+-Literal(type=INT64, value=1, has_explicit_type=TRUE)
+-Literal(type=INT64, value=2, has_explicit_type=TRUE)
)"));
}

TEST_F(FunctionCallBuilderTest,
       SafeSubtractRefusesToWorkOnSignedAndUnsignedIntegers) {
  // Int64 - Uint64 is *not* a built-in function in `SimpleCatalog`.
  std::unique_ptr<ResolvedExpr> input1 =
      MakeResolvedLiteral(types::Int64Type(), Value::Int64(1),
                          /*has_explicit_type=*/true);

  std::unique_ptr<ResolvedExpr> input2 =
      MakeResolvedLiteral(types::Uint64Type(), Value::Uint64(1),
                          /*has_explicit_type=*/true);

  EXPECT_THAT(fn_builder_.SafeSubtract(std::move(input1), std::move(input2)),
              StatusIs(absl::StatusCode::kInternal));
}

TEST_F(FunctionCallBuilderTest, AndTest) {
  std::vector<std::unique_ptr<const ResolvedExpr>> expressions;
  std::unique_ptr<ResolvedExpr> input = MakeResolvedLiteral(
      types::BoolType(), Value::Bool(true), /*has_explicit_type=*/true);
  std::unique_ptr<ResolvedExpr> input2 = MakeResolvedLiteral(
      types::BoolType(), Value::Bool(false), /*has_explicit_type=*/true);
  expressions.push_back(std::move(input));
  expressions.push_back(std::move(input2));

  GOOGLESQL_ASSERT_OK_AND_ASSIGN(std::unique_ptr<const ResolvedExpr> and_fn,
                       fn_builder_.And(std::move(expressions)));
  EXPECT_EQ(and_fn->DebugString(), absl::StripLeadingAsciiWhitespace(R"(
FunctionCall(GoogleSQL:$and(BOOL, repeated(1) BOOL) -> BOOL)
+-Literal(type=BOOL, value=true, has_explicit_type=TRUE)
+-Literal(type=BOOL, value=false, has_explicit_type=TRUE)
)"));
}

TEST_F(FunctionCallBuilderTest, OrTest) {
  std::vector<std::unique_ptr<const ResolvedExpr>> expressions;
  std::unique_ptr<ResolvedExpr> input = MakeResolvedLiteral(
      types::BoolType(), Value::Bool(true), /*has_explicit_type=*/true);
  std::unique_ptr<ResolvedExpr> input2 = MakeResolvedLiteral(
      types::BoolType(), Value::Bool(false), /*has_explicit_type=*/true);
  expressions.push_back(std::move(input));
  expressions.push_back(std::move(input2));

  GOOGLESQL_ASSERT_OK_AND_ASSIGN(std::unique_ptr<const ResolvedExpr> or_fn,
                       fn_builder_.Or(std::move(expressions)));
  EXPECT_EQ(or_fn->DebugString(), absl::StripLeadingAsciiWhitespace(R"(
FunctionCall(GoogleSQL:$or(BOOL, repeated(1) BOOL) -> BOOL)
+-Literal(type=BOOL, value=true, has_explicit_type=TRUE)
+-Literal(type=BOOL, value=false, has_explicit_type=TRUE)
)"));
}

TEST_F(FunctionCallBuilderTest, AndTooFewExpressionsTest) {
  std::vector<std::unique_ptr<const ResolvedExpr>> expressions;
  std::unique_ptr<ResolvedExpr> input = MakeResolvedLiteral(
      types::BoolType(), Value::Bool(true), /*has_explicit_type=*/true);
  expressions.push_back(std::move(input));

  EXPECT_THAT(fn_builder_.And(std::move(expressions)),
              StatusIs(absl::StatusCode::kInternal));
}

TEST_F(FunctionCallBuilderTest, AndInvalidExpressionsTest) {
  std::vector<std::unique_ptr<const ResolvedExpr>> expressions;
  std::unique_ptr<ResolvedExpr> input = MakeResolvedLiteral(
      types::BoolType(), Value::Bool(true), /*has_explicit_type=*/true);
  std::unique_ptr<ResolvedExpr> input2 = MakeResolvedLiteral(
      types::Int64Type(), Value::Int64(1), /*has_explicit_type=*/true);
  expressions.push_back(std::move(input));
  expressions.push_back(std::move(input2));

  EXPECT_THAT(fn_builder_.And(std::move(expressions)),
              StatusIs(absl::StatusCode::kInternal));
}

TEST_F(FunctionCallBuilderTest, ErrorFunctionWithCollationTest) {
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(std::unique_ptr<const ResolvedLiteral> a1,
                       ResolvedLiteralBuilder()
                           .set_value(Value::String("a"))
                           .set_type(types::StringType())
                           .Build());
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(std::unique_ptr<const ResolvedLiteral> a2,
                       ResolvedLiteralBuilder()
                           .set_value(Value::String("A"))
                           .set_type(types::StringType())
                           .Build());
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(
      std::unique_ptr<const ResolvedExpr> expr1,
      testing::MakeCollateCallForTest(
          std::move(a1), "und:ci", analyzer_options_, catalog_, type_factory_));

  GOOGLESQL_ASSERT_OK_AND_ASSIGN(
      std::unique_ptr<const ResolvedExpr> expr2,
      testing::MakeCollateCallForTest(
          std::move(a2), "und:cs", analyzer_options_, catalog_, type_factory_));

  GOOGLESQL_ASSERT_OK_AND_ASSIGN(std::unique_ptr<const ResolvedExpr> resolved_fn1,
                       fn_builder_.Error(std::move(expr1)));
  EXPECT_EQ(resolved_fn1->DebugString(), absl::StripLeadingAsciiWhitespace(R"(
FunctionCall(GoogleSQL:error(STRING) -> INT64)
+-FunctionCall(GoogleSQL:collate(STRING, STRING) -> STRING)
  +-type_annotation_map={Collation:"und:ci"}
  +-Literal(type=STRING, value="a")
  +-Literal(type=STRING, value="und:ci", preserve_in_literal_remover=TRUE)
)"));

  GOOGLESQL_ASSERT_OK_AND_ASSIGN(
      std::unique_ptr<const ResolvedExpr> resolved_fn2,
      fn_builder_.Error(std::move(expr2), types::StringType()));
  EXPECT_EQ(resolved_fn2->DebugString(), absl::StripLeadingAsciiWhitespace(R"(
FunctionCall(GoogleSQL:error(STRING) -> STRING)
+-FunctionCall(GoogleSQL:collate(STRING, STRING) -> STRING)
  +-type_annotation_map={Collation:"und:cs"}
  +-Literal(type=STRING, value="A")
  +-Literal(type=STRING, value="und:cs", preserve_in_literal_remover=TRUE)
)"));
}

TEST_F(FunctionCallBuilderTest, NotEqualWithSameCollationTest) {
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(std::unique_ptr<const ResolvedLiteral> a1,
                       ResolvedLiteralBuilder()
                           .set_value(Value::String("a"))
                           .set_type(types::StringType())
                           .Build());
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(std::unique_ptr<const ResolvedLiteral> a2,
                       ResolvedLiteralBuilder()
                           .set_value(Value::String("A"))
                           .set_type(types::StringType())
                           .Build());

  GOOGLESQL_ASSERT_OK_AND_ASSIGN(
      std::unique_ptr<const ResolvedExpr> expr1,
      testing::MakeCollateCallForTest(
          std::move(a1), "und:ci", analyzer_options_, catalog_, type_factory_));
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(
      std::unique_ptr<const ResolvedExpr> expr2,
      testing::MakeCollateCallForTest(
          std::move(a2), "und:ci", analyzer_options_, catalog_, type_factory_));

  GOOGLESQL_ASSERT_OK_AND_ASSIGN(
      std::unique_ptr<const ResolvedFunctionCall> resolved_fn,
      fn_builder_.NotEqual(std::move(expr1), std::move(expr2)));

  EXPECT_EQ(resolved_fn->DebugString(), absl::StripLeadingAsciiWhitespace(R"(
FunctionCall(GoogleSQL:$not_equal(STRING, STRING) -> BOOL)
+-FunctionCall(GoogleSQL:collate(STRING, STRING) -> STRING)
| +-type_annotation_map={Collation:"und:ci"}
| +-Literal(type=STRING, value="a")
| +-Literal(type=STRING, value="und:ci", preserve_in_literal_remover=TRUE)
+-FunctionCall(GoogleSQL:collate(STRING, STRING) -> STRING)
| +-type_annotation_map={Collation:"und:ci"}
| +-Literal(type=STRING, value="A")
| +-Literal(type=STRING, value="und:ci", preserve_in_literal_remover=TRUE)
+-collation_list=[und:ci]
)"));
}

TEST_F(FunctionCallBuilderTest, NotEqualWithMixedCollationTest) {
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(std::unique_ptr<const ResolvedLiteral> a1,
                       ResolvedLiteralBuilder()
                           .set_value(Value::String("a"))
                           .set_type(types::StringType())
                           .Build());
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(std::unique_ptr<const ResolvedLiteral> a2,
                       ResolvedLiteralBuilder()
                           .set_value(Value::String("A"))
                           .set_type(types::StringType())
                           .Build());

  GOOGLESQL_ASSERT_OK_AND_ASSIGN(
      std::unique_ptr<const ResolvedExpr> expr1,
      testing::MakeCollateCallForTest(
          std::move(a1), "und:ci", analyzer_options_, catalog_, type_factory_));
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(
      std::unique_ptr<const ResolvedExpr> expr2,
      testing::MakeCollateCallForTest(
          std::move(a2), "und:cs", analyzer_options_, catalog_, type_factory_));

  ASSERT_THAT(fn_builder_.NotEqual(std::move(expr1), std::move(expr2)),
              StatusIs(absl::StatusCode::kInvalidArgument,
                       HasSubstr("Collation conflict")));
}

TEST_F(FunctionCallBuilderTest, LeastWithSameCollationTest) {
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(std::vector<std::unique_ptr<const ResolvedExpr>> args,
                       testing::BuildResolvedLiteralsWithCollationForTest(
                           {{"foo", "und:ci"}, {"bar", "und:ci"}},
                           analyzer_options_, catalog_, type_factory_));
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(std::unique_ptr<const ResolvedExpr> resolved_fn,
                       fn_builder_.Least(std::move(args)));

  EXPECT_EQ(resolved_fn->DebugString(), absl::StripLeadingAsciiWhitespace(R"(
FunctionCall(GoogleSQL:least(repeated(2) STRING) -> STRING)
+-type_annotation_map={Collation:"und:ci"}
+-FunctionCall(GoogleSQL:collate(STRING, STRING) -> STRING)
| +-type_annotation_map={Collation:"und:ci"}
| +-Literal(type=STRING, value="foo", has_explicit_type=TRUE)
| +-Literal(type=STRING, value="und:ci", preserve_in_literal_remover=TRUE)
+-FunctionCall(GoogleSQL:collate(STRING, STRING) -> STRING)
| +-type_annotation_map={Collation:"und:ci"}
| +-Literal(type=STRING, value="bar", has_explicit_type=TRUE)
| +-Literal(type=STRING, value="und:ci", preserve_in_literal_remover=TRUE)
+-collation_list=[und:ci]
)"));
}

TEST_F(FunctionCallBuilderTest, LeastWithMixedCollationTest) {
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(std::vector<std::unique_ptr<const ResolvedExpr>> args,
                       testing::BuildResolvedLiteralsWithCollationForTest(
                           {{"foo", "und:ci"}, {"FOO", "binary"}},
                           analyzer_options_, catalog_, type_factory_));
  ASSERT_THAT(fn_builder_.Least(std::move(args)),
              StatusIs(absl::StatusCode::kInvalidArgument,
                       HasSubstr("Collation conflict")));
}

TEST_F(FunctionCallBuilderTest, GreatestWithSameCollationTest) {
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(
      std::vector<std::unique_ptr<const ResolvedExpr>> args,
      testing::BuildResolvedLiteralsWithCollationForTest(
          {{"foo", "und:ci"}, {"FOO", "und:ci"}, {"BaR", "und:ci"}},
          analyzer_options_, catalog_, type_factory_));
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(std::unique_ptr<const ResolvedExpr> resolved_fn,
                       fn_builder_.Greatest(std::move(args)));

  EXPECT_EQ(resolved_fn->DebugString(), absl::StripLeadingAsciiWhitespace(R"(
FunctionCall(GoogleSQL:greatest(repeated(3) STRING) -> STRING)
+-type_annotation_map={Collation:"und:ci"}
+-FunctionCall(GoogleSQL:collate(STRING, STRING) -> STRING)
| +-type_annotation_map={Collation:"und:ci"}
| +-Literal(type=STRING, value="foo", has_explicit_type=TRUE)
| +-Literal(type=STRING, value="und:ci", preserve_in_literal_remover=TRUE)
+-FunctionCall(GoogleSQL:collate(STRING, STRING) -> STRING)
| +-type_annotation_map={Collation:"und:ci"}
| +-Literal(type=STRING, value="FOO", has_explicit_type=TRUE)
| +-Literal(type=STRING, value="und:ci", preserve_in_literal_remover=TRUE)
+-FunctionCall(GoogleSQL:collate(STRING, STRING) -> STRING)
| +-type_annotation_map={Collation:"und:ci"}
| +-Literal(type=STRING, value="BaR", has_explicit_type=TRUE)
| +-Literal(type=STRING, value="und:ci", preserve_in_literal_remover=TRUE)
+-collation_list=[und:ci]
)"));
}

TEST_F(FunctionCallBuilderTest, GreatestWithMixedCollationTest) {
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(std::vector<std::unique_ptr<const ResolvedExpr>> args,
                       testing::BuildResolvedLiteralsWithCollationForTest(
                           {{"foo", "und:ci"}, {"FOO", "binary"}},
                           analyzer_options_, catalog_, type_factory_));
  ASSERT_THAT(fn_builder_.Greatest(std::move(args)),
              StatusIs(absl::StatusCode::kInvalidArgument,
                       HasSubstr("Collation conflict")));
}

TEST_F(FunctionCallBuilderTest, CoalesceWithCommonSuperTypeTest) {
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(std::unique_ptr<const ResolvedLiteral> a1,
                       ResolvedLiteralBuilder()
                           .set_value(Value::Int64(1))
                           .set_type(types::Int64Type())
                           .Build());
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(std::unique_ptr<const ResolvedLiteral> a2,
                       ResolvedLiteralBuilder()
                           .set_value(Value::Int32(2))
                           .set_type(types::Int32Type())
                           .Build());
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(std::unique_ptr<const ResolvedLiteral> a3,
                       ResolvedLiteralBuilder()
                           .set_value(Value::Uint32(300))
                           .set_type(types::Uint32Type())
                           .Build());

  std::vector<std::unique_ptr<const ResolvedExpr>> args;
  args.push_back(std::move(a1));
  args.push_back(std::move(a2));
  args.push_back(std::move(a3));

  GOOGLESQL_ASSERT_OK_AND_ASSIGN(std::unique_ptr<const ResolvedExpr> resolved_fn,
                       fn_builder_.Coalesce(std::move(args)));

  EXPECT_EQ(resolved_fn->DebugString(), absl::StripLeadingAsciiWhitespace(R"(
FunctionCall(GoogleSQL:coalesce(repeated(3) INT64) -> INT64)
+-Literal(type=INT64, value=1)
+-Literal(type=INT32, value=2)
+-Literal(type=UINT32, value=300)
)"));
}

TEST_F(FunctionCallBuilderTest, CoalesceWithSameCollationTest) {
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(
      std::vector<std::unique_ptr<const ResolvedExpr>> args,
      testing::BuildResolvedLiteralsWithCollationForTest(
          {{"foo", "und:ci"}, {"FOO", "und:ci"}, {"BaR", "und:ci"}},
          analyzer_options_, catalog_, type_factory_));
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(std::unique_ptr<const ResolvedExpr> resolved_fn,
                       fn_builder_.Coalesce(std::move(args)));

  EXPECT_EQ(resolved_fn->DebugString(), absl::StripLeadingAsciiWhitespace(R"(
FunctionCall(GoogleSQL:coalesce(repeated(3) STRING) -> STRING)
+-type_annotation_map={Collation:"und:ci"}
+-FunctionCall(GoogleSQL:collate(STRING, STRING) -> STRING)
| +-type_annotation_map={Collation:"und:ci"}
| +-Literal(type=STRING, value="foo", has_explicit_type=TRUE)
| +-Literal(type=STRING, value="und:ci", preserve_in_literal_remover=TRUE)
+-FunctionCall(GoogleSQL:collate(STRING, STRING) -> STRING)
| +-type_annotation_map={Collation:"und:ci"}
| +-Literal(type=STRING, value="FOO", has_explicit_type=TRUE)
| +-Literal(type=STRING, value="und:ci", preserve_in_literal_remover=TRUE)
+-FunctionCall(GoogleSQL:collate(STRING, STRING) -> STRING)
  +-type_annotation_map={Collation:"und:ci"}
  +-Literal(type=STRING, value="BaR", has_explicit_type=TRUE)
  +-Literal(type=STRING, value="und:ci", preserve_in_literal_remover=TRUE)
)"));
}

TEST_F(FunctionCallBuilderTest, CoalesceWithMixedCollationTest) {
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(std::vector<std::unique_ptr<const ResolvedExpr>> args,
                       testing::BuildResolvedLiteralsWithCollationForTest(
                           {{"foo", "und:ci"}, {"FOO", "binary"}},
                           analyzer_options_, catalog_, type_factory_));
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(std::unique_ptr<const ResolvedExpr> resolved_fn,
                       fn_builder_.Coalesce(std::move(args)));

  EXPECT_EQ(resolved_fn->DebugString(), absl::StripLeadingAsciiWhitespace(R"(
FunctionCall(GoogleSQL:coalesce(repeated(2) STRING) -> STRING)
+-FunctionCall(GoogleSQL:collate(STRING, STRING) -> STRING)
| +-type_annotation_map={Collation:"und:ci"}
| +-Literal(type=STRING, value="foo", has_explicit_type=TRUE)
| +-Literal(type=STRING, value="und:ci", preserve_in_literal_remover=TRUE)
+-FunctionCall(GoogleSQL:collate(STRING, STRING) -> STRING)
  +-type_annotation_map={Collation:"binary"}
  +-Literal(type=STRING, value="FOO", has_explicit_type=TRUE)
  +-Literal(type=STRING, value="binary", preserve_in_literal_remover=TRUE)
)"));
}

TEST_F(FunctionCallBuilderTest, LessWithSameCollationTest) {
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(std::unique_ptr<const ResolvedLiteral> a1,
                       ResolvedLiteralBuilder()
                           .set_value(Value::String("a"))
                           .set_type(types::StringType())
                           .Build());
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(std::unique_ptr<const ResolvedLiteral> a2,
                       ResolvedLiteralBuilder()
                           .set_value(Value::String("A"))
                           .set_type(types::StringType())
                           .Build());

  GOOGLESQL_ASSERT_OK_AND_ASSIGN(
      std::unique_ptr<const ResolvedExpr> expr1,
      testing::MakeCollateCallForTest(
          std::move(a1), "und:ci", analyzer_options_, catalog_, type_factory_));
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(
      std::unique_ptr<const ResolvedExpr> expr2,
      testing::MakeCollateCallForTest(
          std::move(a2), "und:ci", analyzer_options_, catalog_, type_factory_));

  GOOGLESQL_ASSERT_OK_AND_ASSIGN(std::unique_ptr<const ResolvedFunctionCall> resolved_fn,
                       fn_builder_.Less(std::move(expr1), std::move(expr2)));

  EXPECT_EQ(resolved_fn->DebugString(), absl::StripLeadingAsciiWhitespace(R"(
FunctionCall(GoogleSQL:$less(STRING, STRING) -> BOOL)
+-FunctionCall(GoogleSQL:collate(STRING, STRING) -> STRING)
| +-type_annotation_map={Collation:"und:ci"}
| +-Literal(type=STRING, value="a")
| +-Literal(type=STRING, value="und:ci", preserve_in_literal_remover=TRUE)
+-FunctionCall(GoogleSQL:collate(STRING, STRING) -> STRING)
| +-type_annotation_map={Collation:"und:ci"}
| +-Literal(type=STRING, value="A")
| +-Literal(type=STRING, value="und:ci", preserve_in_literal_remover=TRUE)
+-collation_list=[und:ci]
)"));
}

TEST_F(FunctionCallBuilderTest, ArrayLengthWithSameCollationTest) {
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(std::vector<std::unique_ptr<const ResolvedExpr>> args,
                       testing::BuildResolvedLiteralsWithCollationForTest(
                           {{"foo", "und:ci"}, {"bar", "und:ci"}},
                           analyzer_options_, catalog_, type_factory_));
  const Type* type = args[0]->type();
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(std::unique_ptr<const ResolvedExpr> array_expr,
                       fn_builder_.MakeArray(type, std::move(args)));
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(std::unique_ptr<const ResolvedFunctionCall> resolved_fn,
                       fn_builder_.ArrayLength(std::move(array_expr)));

  EXPECT_EQ(resolved_fn->DebugString(), absl::StripLeadingAsciiWhitespace(R"(
FunctionCall(GoogleSQL:array_length(ARRAY<STRING>) -> INT64)
+-FunctionCall(GoogleSQL:$make_array(repeated(2) STRING) -> ARRAY<STRING>)
  +-type_annotation_map=<{Collation:"und:ci"}>
  +-FunctionCall(GoogleSQL:collate(STRING, STRING) -> STRING)
  | +-type_annotation_map={Collation:"und:ci"}
  | +-Literal(type=STRING, value="foo", has_explicit_type=TRUE)
  | +-Literal(type=STRING, value="und:ci", preserve_in_literal_remover=TRUE)
  +-FunctionCall(GoogleSQL:collate(STRING, STRING) -> STRING)
    +-type_annotation_map={Collation:"und:ci"}
    +-Literal(type=STRING, value="bar", has_explicit_type=TRUE)
    +-Literal(type=STRING, value="und:ci", preserve_in_literal_remover=TRUE)
)"));
}

TEST_F(FunctionCallBuilderTest, ArrayAtOffsetTest) {
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(std::vector<std::unique_ptr<const ResolvedExpr>> args,
                       testing::BuildResolvedLiteralsWithCollationForTest(
                           {{"foo", "und:ci"}, {"bar", "und:ci"}},
                           analyzer_options_, catalog_, type_factory_));
  const Type* type = args[0]->type();
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(std::unique_ptr<const ResolvedExpr> array_expr,
                       fn_builder_.MakeArray(type, std::move(args)));
  std::unique_ptr<const ResolvedExpr> offset_expr =
      MakeResolvedLiteral(Value::Int64(0));
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(
      std::unique_ptr<const ResolvedFunctionCall> resolved_fn,
      fn_builder_.ArrayAtOffset(std::move(array_expr), std::move(offset_expr)));

  EXPECT_EQ(resolved_fn->DebugString(), absl::StripLeadingAsciiWhitespace(R"(
FunctionCall(GoogleSQL:$array_at_offset(ARRAY<STRING>, INT64) -> STRING)
+-type_annotation_map={Collation:"und:ci"}
+-FunctionCall(GoogleSQL:$make_array(repeated(2) STRING) -> ARRAY<STRING>)
| +-type_annotation_map=<{Collation:"und:ci"}>
| +-FunctionCall(GoogleSQL:collate(STRING, STRING) -> STRING)
| | +-type_annotation_map={Collation:"und:ci"}
| | +-Literal(type=STRING, value="foo", has_explicit_type=TRUE)
| | +-Literal(type=STRING, value="und:ci", preserve_in_literal_remover=TRUE)
| +-FunctionCall(GoogleSQL:collate(STRING, STRING) -> STRING)
|   +-type_annotation_map={Collation:"und:ci"}
|   +-Literal(type=STRING, value="bar", has_explicit_type=TRUE)
|   +-Literal(type=STRING, value="und:ci", preserve_in_literal_remover=TRUE)
+-Literal(type=INT64, value=0)
)"));
}

TEST_F(FunctionCallBuilderTest, ArraySliceTest) {
  std::vector<std::unique_ptr<const ResolvedExpr>> args;
  args.push_back(MakeResolvedLiteral(Value::Int64(100)));
  args.push_back(MakeResolvedLiteral(Value::Int64(101)));
  args.push_back(MakeResolvedLiteral(Value::Int64(102)));
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(
      std::unique_ptr<const ResolvedExpr> array_expr,
      fn_builder_.MakeArray(types::Int64Type(), std::move(args)));
  std::unique_ptr<const ResolvedExpr> start_offset_expr =
      MakeResolvedLiteral(Value::Int64(1));
  std::unique_ptr<const ResolvedExpr> end_offset_expr =
      MakeResolvedLiteral(Value::Int64(2));
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(std::unique_ptr<const ResolvedFunctionCall> resolved_fn,
                       fn_builder_.ArraySlice(std::move(array_expr),
                                              std::move(start_offset_expr),
                                              std::move(end_offset_expr)));

  EXPECT_EQ(resolved_fn->DebugString(), absl::StripLeadingAsciiWhitespace(R"(
FunctionCall(GoogleSQL:array_slice(ARRAY<INT64> array_to_slice, INT64 start_offset, INT64 end_offset) -> ARRAY<INT64>)
+-FunctionCall(GoogleSQL:$make_array(repeated(3) INT64) -> ARRAY<INT64>)
| +-Literal(type=INT64, value=100)
| +-Literal(type=INT64, value=101)
| +-Literal(type=INT64, value=102)
+-Literal(type=INT64, value=1)
+-Literal(type=INT64, value=2)
)"));
}

TEST_F(FunctionCallBuilderTest, ArraySliceWithSameCollationTest) {
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(std::vector<std::unique_ptr<const ResolvedExpr>> args,
                       testing::BuildResolvedLiteralsWithCollationForTest(
                           {{"foo", "und:ci"}, {"bar", "und:ci"}},
                           analyzer_options_, catalog_, type_factory_));
  const Type* type = args[0]->type();
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(std::unique_ptr<const ResolvedExpr> array_expr,
                       fn_builder_.MakeArray(type, std::move(args)));
  std::unique_ptr<const ResolvedExpr> start_offset_expr =
      MakeResolvedLiteral(Value::Int64(0));
  std::unique_ptr<const ResolvedExpr> end_offset_expr =
      MakeResolvedLiteral(Value::Int64(1));
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(std::unique_ptr<const ResolvedFunctionCall> resolved_fn,
                       fn_builder_.ArraySlice(std::move(array_expr),
                                              std::move(start_offset_expr),
                                              std::move(end_offset_expr)));

  EXPECT_EQ(resolved_fn->DebugString(), absl::StripLeadingAsciiWhitespace(R"(
FunctionCall(GoogleSQL:array_slice(ARRAY<STRING> array_to_slice, INT64 start_offset, INT64 end_offset) -> ARRAY<STRING>)
+-type_annotation_map=<{Collation:"und:ci"}>
+-FunctionCall(GoogleSQL:$make_array(repeated(2) STRING) -> ARRAY<STRING>)
| +-type_annotation_map=<{Collation:"und:ci"}>
| +-FunctionCall(GoogleSQL:collate(STRING, STRING) -> STRING)
| | +-type_annotation_map={Collation:"und:ci"}
| | +-Literal(type=STRING, value="foo", has_explicit_type=TRUE)
| | +-Literal(type=STRING, value="und:ci", preserve_in_literal_remover=TRUE)
| +-FunctionCall(GoogleSQL:collate(STRING, STRING) -> STRING)
|   +-type_annotation_map={Collation:"und:ci"}
|   +-Literal(type=STRING, value="bar", has_explicit_type=TRUE)
|   +-Literal(type=STRING, value="und:ci", preserve_in_literal_remover=TRUE)
+-Literal(type=INT64, value=0)
+-Literal(type=INT64, value=1)
)"));
}

TEST_F(FunctionCallBuilderTest, ArraySliceWithMixedCollationTest) {
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(std::vector<std::unique_ptr<const ResolvedExpr>> args,
                       testing::BuildResolvedLiteralsWithCollationForTest(
                           {{"foo", "und:ci"}, {"FOO", "binary"}},
                           analyzer_options_, catalog_, type_factory_));
  const Type* type = args[0]->type();
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(std::unique_ptr<const ResolvedExpr> array_expr,
                       fn_builder_.MakeArray(type, std::move(args)));
  std::unique_ptr<const ResolvedExpr> start_offset_expr =
      MakeResolvedLiteral(Value::Int64(0));
  std::unique_ptr<const ResolvedExpr> end_offset_expr =
      MakeResolvedLiteral(Value::Int64(1));
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(std::unique_ptr<const ResolvedFunctionCall> resolved_fn,
                       fn_builder_.ArraySlice(std::move(array_expr),
                                              std::move(start_offset_expr),
                                              std::move(end_offset_expr)));

  EXPECT_EQ(resolved_fn->DebugString(), absl::StripLeadingAsciiWhitespace(R"(
FunctionCall(GoogleSQL:array_slice(ARRAY<STRING> array_to_slice, INT64 start_offset, INT64 end_offset) -> ARRAY<STRING>)
+-FunctionCall(GoogleSQL:$make_array(repeated(2) STRING) -> ARRAY<STRING>)
| +-FunctionCall(GoogleSQL:collate(STRING, STRING) -> STRING)
| | +-type_annotation_map={Collation:"und:ci"}
| | +-Literal(type=STRING, value="foo", has_explicit_type=TRUE)
| | +-Literal(type=STRING, value="und:ci", preserve_in_literal_remover=TRUE)
| +-FunctionCall(GoogleSQL:collate(STRING, STRING) -> STRING)
|   +-type_annotation_map={Collation:"binary"}
|   +-Literal(type=STRING, value="FOO", has_explicit_type=TRUE)
|   +-Literal(type=STRING, value="binary", preserve_in_literal_remover=TRUE)
+-Literal(type=INT64, value=0)
+-Literal(type=INT64, value=1)
)"));
}

TEST_F(FunctionCallBuilderTest, ArraySliceWithInvalidArgumentsTest) {
  std::vector<std::unique_ptr<const ResolvedExpr>> args;
  args.push_back(MakeResolvedLiteral(Value::Int64(100)));
  args.push_back(MakeResolvedLiteral(Value::Int64(101)));
  args.push_back(MakeResolvedLiteral(Value::Int64(102)));
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(
      std::unique_ptr<const ResolvedExpr> array_expr,
      fn_builder_.MakeArray(types::Int64Type(), std::move(args)));
  std::unique_ptr<const ResolvedExpr> start_offset_expr =
      MakeResolvedLiteral(Value::Int64(1));
  std::unique_ptr<const ResolvedExpr> end_offset_expr =
      MakeResolvedLiteral(Value::String("foo"));
  EXPECT_THAT(fn_builder_.ArraySlice(std::move(array_expr),
                                     std::move(start_offset_expr),
                                     std::move(end_offset_expr)),
              StatusIs(absl::StatusCode::kInternal));
}

TEST_F(FunctionCallBuilderTest, ModInt64Test) {
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(
      std::unique_ptr<const ResolvedFunctionCall> resolved_fn,
      fn_builder_.Mod(/*dividend_expr=*/MakeResolvedLiteral(Value::Int64(1)),
                      /*divisor_expr=*/MakeResolvedLiteral(Value::Int64(2))));

  EXPECT_EQ(resolved_fn->DebugString(), absl::StripLeadingAsciiWhitespace(R"(
FunctionCall(GoogleSQL:mod(INT64, INT64) -> INT64)
+-Literal(type=INT64, value=1)
+-Literal(type=INT64, value=2)
)"));
}

TEST_F(FunctionCallBuilderTest, ModUint64Test) {
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(
      std::unique_ptr<const ResolvedFunctionCall> resolved_fn,
      fn_builder_.Mod(/*dividend_expr=*/MakeResolvedLiteral(Value::Uint64(1)),
                      /*divisor_expr=*/MakeResolvedLiteral(Value::Uint64(2))));

  EXPECT_EQ(resolved_fn->DebugString(), absl::StripLeadingAsciiWhitespace(R"(
FunctionCall(GoogleSQL:mod(UINT64, UINT64) -> UINT64)
+-Literal(type=UINT64, value=1)
+-Literal(type=UINT64, value=2)
)"));
}

// The catalog does not have the FN_MOD_NUMERIC signature.
TEST_F(FunctionCallBuilderTest, ModNumericNoSignatureTest) {
  EXPECT_THAT(
      fn_builder_.Mod(/*dividend_expr=*/MakeResolvedLiteral(
                          Value::Numeric(NumericValue(1))),
                      /*divisor_expr=*/MakeResolvedLiteral(
                          Value::Numeric(NumericValue(2)))),
      StatusIs(
          absl::StatusCode::kInvalidArgument,
          ::testing::HasSubstr(
              "The provided catalog does not have the FN_MOD_NUMERIC "
              "signature. Did you forget to enable FEATURE_NUMERIC_TYPE?")));
}

// The new catalog has the FN_MOD_NUMERIC signature.
TEST_F(FunctionCallBuilderTest, ModNumericTest) {
  AnalyzerOptions analyzer_options;
  analyzer_options.mutable_language()->EnableLanguageFeature(
      FEATURE_NUMERIC_TYPE);
  SimpleCatalog catalog("mod_numeric_builder_catalog");
  catalog.AddBuiltinFunctions(
      BuiltinFunctionOptions(analyzer_options.language()));
  FunctionCallBuilder fn_builder(analyzer_options, catalog, type_factory_);

  GOOGLESQL_ASSERT_OK_AND_ASSIGN(std::unique_ptr<const ResolvedFunctionCall> resolved_fn,
                       fn_builder.Mod(/*dividend_expr=*/MakeResolvedLiteral(
                                          Value::Numeric(NumericValue(1))),
                                      /*divisor_expr=*/MakeResolvedLiteral(
                                          Value::Numeric(NumericValue(2)))));

  EXPECT_EQ(resolved_fn->DebugString(), absl::StripLeadingAsciiWhitespace(R"(
FunctionCall(GoogleSQL:mod(NUMERIC, NUMERIC) -> NUMERIC)
+-Literal(type=NUMERIC, value=1)
+-Literal(type=NUMERIC, value=2)
)"));
}

// The catalog does not have the FN_MOD_BIGNUMERIC signature.
TEST_F(FunctionCallBuilderTest, ModBigNumericNoSignatureTest) {
  EXPECT_THAT(
      fn_builder_.Mod(/*dividend_expr=*/MakeResolvedLiteral(
                          Value::BigNumeric(BigNumericValue(1))),
                      /*divisor_expr=*/MakeResolvedLiteral(
                          Value::BigNumeric(BigNumericValue(2)))),
      StatusIs(
          absl::StatusCode::kInvalidArgument,
          ::testing::HasSubstr(
              "The provided catalog does not have the FN_MOD_BIGNUMERIC "
              "signature. Did you forget to enable FEATURE_BIGNUMERIC_TYPE?")));
}

// The new catalog has the FN_MOD_BIGNUMERIC signature.
TEST_F(FunctionCallBuilderTest, ModBigNumericTest) {
  AnalyzerOptions analyzer_options;
  analyzer_options.mutable_language()->EnableLanguageFeature(
      FEATURE_BIGNUMERIC_TYPE);
  SimpleCatalog catalog("mod_big_numeric_builder_catalog");
  catalog.AddBuiltinFunctions(
      BuiltinFunctionOptions(analyzer_options.language()));
  FunctionCallBuilder fn_builder(analyzer_options, catalog, type_factory_);

  GOOGLESQL_ASSERT_OK_AND_ASSIGN(
      std::unique_ptr<const ResolvedFunctionCall> resolved_fn,
      fn_builder.Mod(/*dividend_expr=*/MakeResolvedLiteral(
                         Value::BigNumeric(BigNumericValue(1))),
                     /*divisor_expr=*/MakeResolvedLiteral(
                         Value::BigNumeric(BigNumericValue(2)))));

  EXPECT_EQ(resolved_fn->DebugString(), absl::StripLeadingAsciiWhitespace(R"(
FunctionCall(GoogleSQL:mod(BIGNUMERIC, BIGNUMERIC) -> BIGNUMERIC)
+-Literal(type=BIGNUMERIC, value=1)
+-Literal(type=BIGNUMERIC, value=2)
)"));
}

TEST_F(FunctionCallBuilderTest, ModInvalidInputTypeTest) {
  EXPECT_THAT(
      fn_builder_.Mod(/*dividend_expr=*/MakeResolvedLiteral(Value::String("a")),
                      /*divisor_expr=*/MakeResolvedLiteral(Value::String("b"))),
      StatusIs(absl::StatusCode::kInvalidArgument,
               ::testing::HasSubstr("Unsupported input type for mod: STRING")));
}

TEST_F(FunctionCallBuilderTest, CountInt32Test) {
  auto column =
      MakeResolvedColumnRef(types::Int32Type(), ResolvedColumn(), false);

  GOOGLESQL_ASSERT_OK_AND_ASSIGN(std::unique_ptr<const ResolvedExpr> function,
                       fn_builder_.Count(std::move(column)));
  EXPECT_EQ(function->DebugString(), absl::StripLeadingAsciiWhitespace(R"(
AggregateFunctionCall(GoogleSQL:count(INT32) -> INT64)
+-ColumnRef(type=INT32, column=.#-1)
)"));
}

TEST_F(FunctionCallBuilderTest, CountInt64Test) {
  auto column =
      MakeResolvedColumnRef(types::Int64Type(), ResolvedColumn(), false);

  GOOGLESQL_ASSERT_OK_AND_ASSIGN(std::unique_ptr<const ResolvedExpr> function,
                       fn_builder_.Count(std::move(column)));
  EXPECT_EQ(function->DebugString(), absl::StripLeadingAsciiWhitespace(R"(
AggregateFunctionCall(GoogleSQL:count(INT64) -> INT64)
+-ColumnRef(type=INT64, column=.#-1)
)"));
}

TEST_F(FunctionCallBuilderTest, CountUInt32Test) {
  auto column =
      MakeResolvedColumnRef(types::Uint32Type(), ResolvedColumn(), false);

  GOOGLESQL_ASSERT_OK_AND_ASSIGN(std::unique_ptr<const ResolvedExpr> function,
                       fn_builder_.Count(std::move(column)));
  EXPECT_EQ(function->DebugString(), absl::StripLeadingAsciiWhitespace(R"(
AggregateFunctionCall(GoogleSQL:count(UINT32) -> INT64)
+-ColumnRef(type=UINT32, column=.#-1)
)"));
}

TEST_F(FunctionCallBuilderTest, CountUInt64Test) {
  auto column =
      MakeResolvedColumnRef(types::Uint64Type(), ResolvedColumn(), false);

  GOOGLESQL_ASSERT_OK_AND_ASSIGN(std::unique_ptr<const ResolvedExpr> function,
                       fn_builder_.Count(std::move(column)));
  EXPECT_EQ(function->DebugString(), absl::StripLeadingAsciiWhitespace(R"(
AggregateFunctionCall(GoogleSQL:count(UINT64) -> INT64)
+-ColumnRef(type=UINT64, column=.#-1)
)"));
}

TEST_F(FunctionCallBuilderTest, CountStringTest) {
  auto column =
      MakeResolvedColumnRef(types::StringType(), ResolvedColumn(), false);

  GOOGLESQL_ASSERT_OK_AND_ASSIGN(std::unique_ptr<const ResolvedExpr> function,
                       fn_builder_.Count(std::move(column)));
  EXPECT_EQ(function->DebugString(), absl::StripLeadingAsciiWhitespace(R"(
AggregateFunctionCall(GoogleSQL:count(STRING) -> INT64)
+-ColumnRef(type=STRING, column=.#-1)
)"));
}

TEST_F(FunctionCallBuilderTest, CountFloatTest) {
  auto column =
      MakeResolvedColumnRef(types::FloatType(), ResolvedColumn(), false);

  GOOGLESQL_ASSERT_OK_AND_ASSIGN(std::unique_ptr<const ResolvedExpr> function,
                       fn_builder_.Count(std::move(column)));
  EXPECT_EQ(function->DebugString(), absl::StripLeadingAsciiWhitespace(R"(
AggregateFunctionCall(GoogleSQL:count(FLOAT) -> INT64)
+-ColumnRef(type=FLOAT, column=.#-1)
)"));
}

TEST_F(FunctionCallBuilderTest, CountDoubleTest) {
  auto column =
      MakeResolvedColumnRef(types::DoubleType(), ResolvedColumn(), false);

  GOOGLESQL_ASSERT_OK_AND_ASSIGN(std::unique_ptr<const ResolvedExpr> function,
                       fn_builder_.Count(std::move(column)));
  EXPECT_EQ(function->DebugString(), absl::StripLeadingAsciiWhitespace(R"(
AggregateFunctionCall(GoogleSQL:count(DOUBLE) -> INT64)
+-ColumnRef(type=DOUBLE, column=.#-1)
)"));
}

TEST_F(FunctionCallBuilderTest, CountDistinctInt32Test) {
  auto column =
      MakeResolvedColumnRef(types::Int32Type(), ResolvedColumn(), false);

  GOOGLESQL_ASSERT_OK_AND_ASSIGN(
      std::unique_ptr<const ResolvedExpr> function,
      fn_builder_.Count(std::move(column), /*is_distinct=*/true));
  EXPECT_EQ(function->DebugString(), absl::StripLeadingAsciiWhitespace(R"(
AggregateFunctionCall(GoogleSQL:count(INT32) -> INT64)
+-ColumnRef(type=INT32, column=.#-1)
+-distinct=TRUE
)"));
}

TEST_F(FunctionCallBuilderTest, CountDistinctInt64Test) {
  auto column =
      MakeResolvedColumnRef(types::Int64Type(), ResolvedColumn(), false);

  GOOGLESQL_ASSERT_OK_AND_ASSIGN(
      std::unique_ptr<const ResolvedExpr> function,
      fn_builder_.Count(std::move(column), /*is_distinct=*/true));
  EXPECT_EQ(function->DebugString(), absl::StripLeadingAsciiWhitespace(R"(
AggregateFunctionCall(GoogleSQL:count(INT64) -> INT64)
+-ColumnRef(type=INT64, column=.#-1)
+-distinct=TRUE
)"));
}

TEST_F(FunctionCallBuilderTest, CountDistinctUInt32Test) {
  auto column =
      MakeResolvedColumnRef(types::Uint32Type(), ResolvedColumn(), false);

  GOOGLESQL_ASSERT_OK_AND_ASSIGN(
      std::unique_ptr<const ResolvedExpr> function,
      fn_builder_.Count(std::move(column), /*is_distinct=*/true));
  EXPECT_EQ(function->DebugString(), absl::StripLeadingAsciiWhitespace(R"(
AggregateFunctionCall(GoogleSQL:count(UINT32) -> INT64)
+-ColumnRef(type=UINT32, column=.#-1)
+-distinct=TRUE
)"));
}

TEST_F(FunctionCallBuilderTest, CountDistinctUInt64Test) {
  auto column =
      MakeResolvedColumnRef(types::Uint64Type(), ResolvedColumn(), false);

  GOOGLESQL_ASSERT_OK_AND_ASSIGN(
      std::unique_ptr<const ResolvedExpr> function,
      fn_builder_.Count(std::move(column), /*is_distinct=*/true));
  EXPECT_EQ(function->DebugString(), absl::StripLeadingAsciiWhitespace(R"(
AggregateFunctionCall(GoogleSQL:count(UINT64) -> INT64)
+-ColumnRef(type=UINT64, column=.#-1)
+-distinct=TRUE
)"));
}

TEST_F(FunctionCallBuilderTest, CountDistinctStringTest) {
  auto column =
      MakeResolvedColumnRef(types::StringType(), ResolvedColumn(), false);

  GOOGLESQL_ASSERT_OK_AND_ASSIGN(
      std::unique_ptr<const ResolvedExpr> function,
      fn_builder_.Count(std::move(column), /*is_distinct=*/true));
  EXPECT_EQ(function->DebugString(), absl::StripLeadingAsciiWhitespace(R"(
AggregateFunctionCall(GoogleSQL:count(STRING) -> INT64)
+-ColumnRef(type=STRING, column=.#-1)
+-distinct=TRUE
)"));
}

TEST_F(FunctionCallBuilderTest, CountDistinctFloatTest) {
  auto column =
      MakeResolvedColumnRef(types::FloatType(), ResolvedColumn(), false);

  GOOGLESQL_ASSERT_OK_AND_ASSIGN(
      std::unique_ptr<const ResolvedExpr> function,
      fn_builder_.Count(std::move(column), /*is_distinct=*/true));
  EXPECT_EQ(function->DebugString(), absl::StripLeadingAsciiWhitespace(R"(
AggregateFunctionCall(GoogleSQL:count(FLOAT) -> INT64)
+-ColumnRef(type=FLOAT, column=.#-1)
+-distinct=TRUE
)"));
}

TEST_F(FunctionCallBuilderTest, CountDistinctDoubleTest) {
  auto column =
      MakeResolvedColumnRef(types::DoubleType(), ResolvedColumn(), false);

  GOOGLESQL_ASSERT_OK_AND_ASSIGN(
      std::unique_ptr<const ResolvedExpr> function,
      fn_builder_.Count(std::move(column), /*is_distinct=*/true));
  EXPECT_EQ(function->DebugString(), absl::StripLeadingAsciiWhitespace(R"(
AggregateFunctionCall(GoogleSQL:count(DOUBLE) -> INT64)
+-ColumnRef(type=DOUBLE, column=.#-1)
+-distinct=TRUE
)"));
}

TEST_F(FunctionCallBuilderTest, CountDistinctGetStructFieldTest) {
  std::unique_ptr<ResolvedExpr> column_ref =
      MakeResolvedColumnRef(types::Int64Type(), ResolvedColumn(), false);
  auto column = MakeResolvedGetStructField(types::Int64Type(),
                                           std::move(column_ref), 0, true);
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(
      std::unique_ptr<const ResolvedExpr> function,
      fn_builder_.Count(std::move(column), /*is_distinct=*/true));
  EXPECT_EQ(function->DebugString(), absl::StripLeadingAsciiWhitespace(R"(
AggregateFunctionCall(GoogleSQL:count(INT64) -> INT64)
+-GetStructField
| +-type=INT64
| +-expr=
| | +-ColumnRef(type=INT64, column=.#-1)
| +-field_idx=0
| +-field_expr_is_positional=TRUE
+-distinct=TRUE
)"));
}

TEST_F(FunctionCallBuilderTest, CountDistinctGetProtoTest) {
  std::unique_ptr<ResolvedExpr> column_ref =
      MakeResolvedColumnRef(types::Int64Type(), ResolvedColumn(), false);
  auto field_descriptor =
      ResolvedColumnProto::descriptor()->FindFieldByName("column_id");
  auto column = MakeResolvedGetProtoField(
      types::Int64Type(), std::move(column_ref), field_descriptor,
      googlesql::Value(),
      /*get_has_bit=*/false, FieldFormat::DEFAULT_FORMAT,
      /*return_default_value_when_unset=*/false);
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(
      std::unique_ptr<const ResolvedExpr> function,
      fn_builder_.Count(std::move(column), /*is_distinct=*/true));
  EXPECT_EQ(function->DebugString(), absl::StripLeadingAsciiWhitespace(R"(
AggregateFunctionCall(GoogleSQL:count(INT64) -> INT64)
+-GetProtoField
| +-type=INT64
| +-expr=
| | +-ColumnRef(type=INT64, column=.#-1)
| +-field_descriptor=column_id
+-distinct=TRUE
)"));
}

TEST_F(FunctionCallBuilderTest, ConcatString) {
  std::vector<std::unique_ptr<const ResolvedExpr>> args;
  args.push_back(MakeResolvedLiteral(Value::String("a")));
  args.push_back(MakeResolvedLiteral(Value::String("b")));
  args.push_back(MakeResolvedLiteral(Value::String("c")));

  GOOGLESQL_ASSERT_OK_AND_ASSIGN(std::unique_ptr<const ResolvedExpr> function,
                       fn_builder_.Concat(std::move(args)));
  EXPECT_EQ(function->DebugString(), absl::StripLeadingAsciiWhitespace(R"(
FunctionCall(GoogleSQL:concat(STRING, repeated(2) STRING) -> STRING)
+-Literal(type=STRING, value="a")
+-Literal(type=STRING, value="b")
+-Literal(type=STRING, value="c")
)"));
}

TEST_F(FunctionCallBuilderTest, ConcatStringWithCollation) {
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(
      std::vector<std::unique_ptr<const ResolvedExpr>> args,
      testing::BuildResolvedLiteralsWithCollationForTest(
          {{"foo", "und:ci"}, {"bar", "und:ci"}, {"bar", "und:ci"}},
          analyzer_options_, catalog_, type_factory_));

  GOOGLESQL_ASSERT_OK_AND_ASSIGN(std::unique_ptr<const ResolvedExpr> function,
                       fn_builder_.Concat(std::move(args)));
  EXPECT_EQ(function->DebugString(), absl::StripLeadingAsciiWhitespace(R"(
FunctionCall(GoogleSQL:concat(STRING, repeated(2) STRING) -> STRING)
+-type_annotation_map={Collation:"und:ci"}
+-FunctionCall(GoogleSQL:collate(STRING, STRING) -> STRING)
| +-type_annotation_map={Collation:"und:ci"}
| +-Literal(type=STRING, value="foo", has_explicit_type=TRUE)
| +-Literal(type=STRING, value="und:ci", preserve_in_literal_remover=TRUE)
+-FunctionCall(GoogleSQL:collate(STRING, STRING) -> STRING)
| +-type_annotation_map={Collation:"und:ci"}
| +-Literal(type=STRING, value="bar", has_explicit_type=TRUE)
| +-Literal(type=STRING, value="und:ci", preserve_in_literal_remover=TRUE)
+-FunctionCall(GoogleSQL:collate(STRING, STRING) -> STRING)
  +-type_annotation_map={Collation:"und:ci"}
  +-Literal(type=STRING, value="bar", has_explicit_type=TRUE)
  +-Literal(type=STRING, value="und:ci", preserve_in_literal_remover=TRUE)
)"));
}

TEST_F(FunctionCallBuilderTest, ConcatBytes) {
  std::vector<std::unique_ptr<const ResolvedExpr>> args;
  args.push_back(MakeResolvedLiteral(Value::Bytes("a")));
  args.push_back(MakeResolvedLiteral(Value::Bytes("b")));
  args.push_back(MakeResolvedLiteral(Value::Bytes("c")));

  GOOGLESQL_ASSERT_OK_AND_ASSIGN(std::unique_ptr<const ResolvedExpr> function,
                       fn_builder_.Concat(std::move(args)));
  EXPECT_EQ(function->DebugString(), absl::StripLeadingAsciiWhitespace(R"(
FunctionCall(GoogleSQL:concat(BYTES, repeated(2) BYTES) -> BYTES)
+-Literal(type=BYTES, value=b"a")
+-Literal(type=BYTES, value=b"b")
+-Literal(type=BYTES, value=b"c")
)"));
}

TEST_F(FunctionCallBuilderTest, ConcatInvalidInputType) {
  std::vector<std::unique_ptr<const ResolvedExpr>> args;
  args.push_back(MakeResolvedLiteral(Value::Numeric(NumericValue(1))));
  args.push_back(MakeResolvedLiteral(Value::Numeric(NumericValue(2))));
  EXPECT_THAT(fn_builder_.Concat(std::move(args)),
              StatusIs(absl::StatusCode::kInternal,
                       HasSubstr("Invalid element type: NUMERIC")));
}

TEST_F(FunctionCallBuilderTest, ConcatEmptyArgList) {
  std::vector<std::unique_ptr<const ResolvedExpr>> args;
  EXPECT_THAT(
      fn_builder_.Concat(std::move(args)),
      StatusIs(absl::StatusCode::kInternal, HasSubstr("!elements.empty() ")));
}

TEST_F(FunctionCallBuilderTest, IsNotNull) {
  auto column =
      MakeResolvedColumnRef(types::Int64Type(), ResolvedColumn(), false);

  GOOGLESQL_ASSERT_OK_AND_ASSIGN(std::unique_ptr<const ResolvedExpr> function,
                       fn_builder_.IsNotNull(std::move(column)));
  EXPECT_EQ(function->DebugString(), absl::StripLeadingAsciiWhitespace(R"(
FunctionCall(GoogleSQL:$not(BOOL) -> BOOL)
+-FunctionCall(GoogleSQL:$is_null(INT64) -> BOOL)
  +-ColumnRef(type=INT64, column=.#-1)
)"));
}

TEST_F(FunctionCallBuilderTest, ArrayToString) {
  const ArrayType* array_of_strings;
  GOOGLESQL_ASSERT_OK(
      type_factory_.MakeArrayType(types::StringType(), &array_of_strings));
  auto array = MakeResolvedColumnRef(array_of_strings, ResolvedColumn(), false);
  auto delimiter = MakeResolvedLiteral(Value::String("delimiter"));

  GOOGLESQL_ASSERT_OK_AND_ASSIGN(
      std::unique_ptr<const ResolvedExpr> function,
      fn_builder_.ArrayToString(std::move(array), std::move(delimiter)));
  EXPECT_EQ(function->DebugString(), absl::StripLeadingAsciiWhitespace(R"(
FunctionCall(GoogleSQL:array_to_string(ARRAY<STRING>, STRING) -> STRING)
+-ColumnRef(type=ARRAY<STRING>, column=.#-1)
+-Literal(type=STRING, value="delimiter")
)"));
}

TEST_F(FunctionCallBuilderTest, ArrayAgg) {
  auto element =
      MakeResolvedColumnRef(types::StringType(), ResolvedColumn(), false);

  GOOGLESQL_ASSERT_OK_AND_ASSIGN(std::unique_ptr<const ResolvedExpr> function,
                       fn_builder_.ArrayAgg(std::move(element), nullptr));
  EXPECT_EQ(function->DebugString(), absl::StripLeadingAsciiWhitespace(R"(
AggregateFunctionCall(GoogleSQL:array_agg(STRING) -> ARRAY<STRING>)
+-ColumnRef(type=STRING, column=.#-1)
)"));
}

TEST_F(FunctionCallBuilderTest, ArrayAggWithHaving) {
  ResolvedColumn col;
  auto element = MakeResolvedColumnRef(types::StringType(), col, false);
  auto having = MakeResolvedColumnRef(types::StringType(), col, false);

  GOOGLESQL_ASSERT_OK_AND_ASSIGN(
      std::unique_ptr<const ResolvedExpr> function,
      fn_builder_.ArrayAgg(std::move(element), std::move(having),
                           ResolvedAggregateHavingModifier::MAX));
  EXPECT_EQ(function->DebugString(), absl::StripLeadingAsciiWhitespace(R"(
AggregateFunctionCall(GoogleSQL:array_agg(STRING) -> ARRAY<STRING>)
+-ColumnRef(type=STRING, column=.#-1)
+-having_modifier=
  +-AggregateHavingModifier
    +-kind=MAX
    +-having_expr=
      +-ColumnRef(type=STRING, column=.#-1)
)"));
}

// FEATURE_ANALYTIC_FUNCTIONS is required to load the IS_FIRST function in the
// catalog.
TEST_F(FunctionCallBuilderTest, IsFirstKCatalogMissingFn) {
  std::unique_ptr<ResolvedExpr> input =
      MakeResolvedLiteral(types::Int64Type(), Value::Int64(1),
                          /*has_explicit_type=*/true);

  EXPECT_THAT(
      fn_builder_.IsFirstK(std::move(input)),
      StatusIs(absl::StatusCode::kNotFound,
               ::testing::HasSubstr("Function not found: is_first not found in "
                                    "catalog function_builder_catalog")));
}

TEST_F(FunctionCallBuilderTest, IsFirstKInt64Literal) {
  AnalyzerOptions analyzer_options;
  analyzer_options.mutable_language()->EnableLanguageFeature(
      FEATURE_ANALYTIC_FUNCTIONS);
  SimpleCatalog catalog("is_first_k_builder_catalog");
  catalog.AddBuiltinFunctions(
      BuiltinFunctionOptions(analyzer_options.language()));
  FunctionCallBuilder fn_builder(analyzer_options, catalog, type_factory_);
  std::unique_ptr<ResolvedExpr> input =
      MakeResolvedLiteral(types::Int64Type(), Value::Int64(1),
                          /*has_explicit_type=*/true);

  GOOGLESQL_ASSERT_OK_AND_ASSIGN(std::unique_ptr<const ResolvedExpr> function,
                       fn_builder.IsFirstK(std::move(input)));
  EXPECT_EQ(function->DebugString(), absl::StripLeadingAsciiWhitespace(R"(
AnalyticFunctionCall(GoogleSQL:is_first(INT64) -> BOOL)
+-Literal(type=INT64, value=1, has_explicit_type=TRUE)
)"));
}

// IS_FIRST(k) requires the argument to be a literal or a parameter.
TEST_F(FunctionCallBuilderTest, IsFirstKInt64ColumnRef) {
  AnalyzerOptions analyzer_options;
  analyzer_options.mutable_language()->EnableLanguageFeature(
      FEATURE_ANALYTIC_FUNCTIONS);
  SimpleCatalog catalog("is_first_k_builder_catalog");
  catalog.AddBuiltinFunctions(
      BuiltinFunctionOptions(analyzer_options.language()));
  FunctionCallBuilder fn_builder(analyzer_options, catalog, type_factory_);
  std::unique_ptr<ResolvedExpr> input =
      MakeResolvedColumnRef(types::Int64Type(), ResolvedColumn(), false);

  EXPECT_THAT(fn_builder.IsFirstK(std::move(input)),
              StatusIs(absl::StatusCode::kInternal));
}

// IS_FIRST(k) requires the argument to be INT64.
TEST_F(FunctionCallBuilderTest, IsFirstKWrongType) {
  AnalyzerOptions analyzer_options;
  analyzer_options.mutable_language()->EnableLanguageFeature(
      FEATURE_ANALYTIC_FUNCTIONS);
  SimpleCatalog catalog("is_first_k_builder_catalog");
  catalog.AddBuiltinFunctions(
      BuiltinFunctionOptions(analyzer_options.language()));
  FunctionCallBuilder fn_builder(analyzer_options, catalog, type_factory_);
  std::unique_ptr<ResolvedExpr> input =
      MakeResolvedLiteral(types::StringType(), Value::String("abc"));

  EXPECT_THAT(fn_builder.IsFirstK(std::move(input)),
              StatusIs(absl::StatusCode::kInternal));
}

TEST_F(FunctionCallBuilderTest, MakeNullIfEmptyArray) {
  googlesql_base::SequenceNumber sequence;
  ColumnFactory column_factory(10, &sequence);
  const ArrayType* array_of_strings;
  GOOGLESQL_ASSERT_OK(
      type_factory_.MakeArrayType(types::StringType(), &array_of_strings));
  auto array = MakeResolvedColumnRef(array_of_strings, ResolvedColumn(), false);
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(
      std::unique_ptr<const ResolvedExpr> function,
      fn_builder_.MakeNullIfEmptyArray(column_factory, std::move(array)));
  EXPECT_EQ(function->DebugString(), absl::StripLeadingAsciiWhitespace(R"(
WithExpr
+-type=ARRAY<STRING>
+-assignment_list=
| +-$out#11 := ColumnRef(type=ARRAY<STRING>, column=.#-1)
+-expr=
  +-FunctionCall(GoogleSQL:if(BOOL, ARRAY<STRING>, ARRAY<STRING>) -> ARRAY<STRING>)
    +-FunctionCall(GoogleSQL:$greater_or_equal(INT64, INT64) -> BOOL)
    | +-FunctionCall(GoogleSQL:array_length(ARRAY<STRING>) -> INT64)
    | | +-ColumnRef(type=ARRAY<STRING>, column=null_if_empty_array.$out#11)
    | +-Literal(type=INT64, value=1)
    +-ColumnRef(type=ARRAY<STRING>, column=null_if_empty_array.$out#11)
    +-Literal(type=ARRAY<STRING>, value=NULL)
)"));
}

TEST_F(FunctionCallBuilderTest, AreEqualGraphElements) {
  TypeFactory type_factory;
  const GraphElementType* graph_node_type1 = nullptr;
  const GraphElementType* graph_node_type2 = nullptr;
  // super type.
  GOOGLESQL_ASSERT_OK(type_factory.MakeGraphElementType(
      {"graph_node_type"}, GraphElementType::kNode,
      {{"p1", types::Int64Type()}, {"p2", types::StringType()}},
      &graph_node_type1));
  GOOGLESQL_ASSERT_OK(type_factory.MakeGraphElementType(
      {"graph_node_type"}, GraphElementType::kNode,
      {{"p1", types::Int64Type()}}, &graph_node_type2));

  // lhs is super type.
  std::vector<std::unique_ptr<const ResolvedExpr>> args;
  args.emplace_back(MakeResolvedColumnRef(graph_node_type1, ResolvedColumn(),
                                          /*is_correlated=*/false));
  args.emplace_back(MakeResolvedColumnRef(graph_node_type2, ResolvedColumn(),
                                          /*is_correlated=*/false));
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(std::unique_ptr<const ResolvedFunctionCall> function,
                       fn_builder_.AreEqualGraphElements(std::move(args)));
  EXPECT_EQ(function->DebugString(), absl::StripLeadingAsciiWhitespace(R"(
FunctionCall(GoogleSQL:$equal(GRAPH_NODE(graph_node_type)<p1 INT64, p2 STRING>, GRAPH_NODE(graph_node_type)<p1 INT64, p2 STRING>) -> BOOL)
+-ColumnRef(type=GRAPH_NODE(graph_node_type)<p1 INT64, p2 STRING>, column=.#-1)
+-Cast(GRAPH_NODE(graph_node_type)<p1 INT64> -> GRAPH_NODE(graph_node_type)<p1 INT64, p2 STRING>)
  +-ColumnRef(type=GRAPH_NODE(graph_node_type)<p1 INT64>, column=.#-1)
)"));

  // rhs is super type.
  args.clear();
  args.emplace_back(MakeResolvedColumnRef(graph_node_type2, ResolvedColumn(),
                                          /*is_correlated=*/false));
  args.emplace_back(MakeResolvedColumnRef(graph_node_type1, ResolvedColumn(),
                                          /*is_correlated=*/false));
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(function,
                       fn_builder_.AreEqualGraphElements(std::move(args)));
  EXPECT_EQ(function->DebugString(), absl::StripLeadingAsciiWhitespace(R"(
FunctionCall(GoogleSQL:$equal(GRAPH_NODE(graph_node_type)<p1 INT64, p2 STRING>, GRAPH_NODE(graph_node_type)<p1 INT64, p2 STRING>) -> BOOL)
+-Cast(GRAPH_NODE(graph_node_type)<p1 INT64> -> GRAPH_NODE(graph_node_type)<p1 INT64, p2 STRING>)
| +-ColumnRef(type=GRAPH_NODE(graph_node_type)<p1 INT64>, column=.#-1)
+-ColumnRef(type=GRAPH_NODE(graph_node_type)<p1 INT64, p2 STRING>, column=.#-1)
)"));

  // lhs and rhs are the same type.
  args.clear();
  args.emplace_back(MakeResolvedColumnRef(graph_node_type1, ResolvedColumn(),
                                          /*is_correlated=*/false));
  args.emplace_back(MakeResolvedColumnRef(graph_node_type1, ResolvedColumn(),
                                          /*is_correlated=*/false));
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(function,
                       fn_builder_.AreEqualGraphElements(std::move(args)));
  EXPECT_EQ(function->DebugString(), absl::StripLeadingAsciiWhitespace(R"(
FunctionCall(GoogleSQL:$equal(GRAPH_NODE(graph_node_type)<p1 INT64, p2 STRING>, GRAPH_NODE(graph_node_type)<p1 INT64, p2 STRING>) -> BOOL)
+-ColumnRef(type=GRAPH_NODE(graph_node_type)<p1 INT64, p2 STRING>, column=.#-1)
+-ColumnRef(type=GRAPH_NODE(graph_node_type)<p1 INT64, p2 STRING>, column=.#-1)
)"));
}

TEST_F(FunctionCallBuilderTest,
       IsNotDistinctFromArgumentTypeMismatchReturnsError) {
  std::unique_ptr<ResolvedExpr> left =
      MakeResolvedLiteral(types::Int64Type(), Value::Int64(1),
                          /*has_explicit_type=*/true);
  std::unique_ptr<ResolvedExpr> right =
      MakeResolvedLiteral(types::StringType(), Value::String("test"),
                          /*has_explicit_type=*/true);

  EXPECT_THAT(fn_builder_.IsNotDistinctFrom(std::move(left), std::move(right)),
              StatusIs(absl::StatusCode::kInternal,
                       ::testing::HasSubstr("Inconsistent types")));
}

TEST_F(FunctionCallBuilderTest, IsNotDistinctFrom) {
  std::unique_ptr<ResolvedExpr> left =
      MakeResolvedLiteral(types::Int64Type(), Value::Int64(1),
                          /*has_explicit_type=*/true);
  std::unique_ptr<ResolvedExpr> right =
      MakeResolvedLiteral(types::Int64Type(), Value::Int64(2),
                          /*has_explicit_type=*/true);

  GOOGLESQL_ASSERT_OK_AND_ASSIGN(
      std::unique_ptr<const ResolvedExpr> function,
      fn_builder_.IsNotDistinctFrom(std::move(left), std::move(right)));
  EXPECT_EQ(function->DebugString(), absl::StripLeadingAsciiWhitespace(R"(
FunctionCall(GoogleSQL:$is_not_distinct_from(INT64, INT64) -> BOOL)
+-Literal(type=INT64, value=1, has_explicit_type=TRUE)
+-Literal(type=INT64, value=2, has_explicit_type=TRUE)
)"));
}

TEST_F(FunctionCallBuilderTest, HllInitUnsupportedTypeTest) {
  auto column =
      MakeResolvedColumnRef(types::TimeType(), ResolvedColumn(), false);
  // HLL_COUNT.INIT does not support TimeType.
  ASSERT_THAT(fn_builder_.HllInit(std::move(column)),
              StatusIs(absl::StatusCode::kInternal));
}

TEST_F(FunctionCallBuilderTest, HllInitInt32Test) {
  auto column =
      MakeResolvedColumnRef(types::Int32Type(), ResolvedColumn(), false);
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(std::unique_ptr<const ResolvedExpr> function,
                       fn_builder_.HllInit(std::move(column)));
  EXPECT_EQ(function->DebugString(), absl::StripLeadingAsciiWhitespace(R"(
AggregateFunctionCall(GoogleSQL:hll_count.init(INT32) -> BYTES)
+-ColumnRef(type=INT32, column=.#-1)
)"));
}

TEST_F(FunctionCallBuilderTest, HllInitInt64Test) {
  auto column =
      MakeResolvedColumnRef(types::Int64Type(), ResolvedColumn(), false);
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(std::unique_ptr<const ResolvedExpr> function,
                       fn_builder_.HllInit(std::move(column)));
  EXPECT_EQ(function->DebugString(), absl::StripLeadingAsciiWhitespace(R"(
AggregateFunctionCall(GoogleSQL:hll_count.init(INT64) -> BYTES)
+-ColumnRef(type=INT64, column=.#-1)
)"));
}

TEST_F(FunctionCallBuilderTest, HllInitUint64Test) {
  auto column =
      MakeResolvedColumnRef(types::Uint64Type(), ResolvedColumn(), false);
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(std::unique_ptr<const ResolvedExpr> function,
                       fn_builder_.HllInit(std::move(column)));
  EXPECT_EQ(function->DebugString(), absl::StripLeadingAsciiWhitespace(R"(
AggregateFunctionCall(GoogleSQL:hll_count.init(UINT64) -> BYTES)
+-ColumnRef(type=UINT64, column=.#-1)
)"));
}

TEST_F(FunctionCallBuilderTest, HllInitStringTest) {
  auto column =
      MakeResolvedColumnRef(types::StringType(), ResolvedColumn(), false);
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(std::unique_ptr<const ResolvedExpr> function,
                       fn_builder_.HllInit(std::move(column)));
  EXPECT_EQ(function->DebugString(), absl::StripLeadingAsciiWhitespace(R"(
AggregateFunctionCall(GoogleSQL:hll_count.init(STRING) -> BYTES)
+-ColumnRef(type=STRING, column=.#-1)
)"));
}

TEST_F(FunctionCallBuilderTest, HllInitBytesTest) {
  auto column =
      MakeResolvedColumnRef(types::BytesType(), ResolvedColumn(), false);
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(std::unique_ptr<const ResolvedExpr> function,
                       fn_builder_.HllInit(std::move(column)));
  EXPECT_EQ(function->DebugString(), absl::StripLeadingAsciiWhitespace(R"(
AggregateFunctionCall(GoogleSQL:hll_count.init(BYTES) -> BYTES)
+-ColumnRef(type=BYTES, column=.#-1)
)"));
}

TEST_F(FunctionCallBuilderTest, HllMergePartialBytesTest) {
  auto column =
      MakeResolvedColumnRef(types::BytesType(), ResolvedColumn(), false);
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(std::unique_ptr<const ResolvedExpr> function,
                       fn_builder_.HllMergePartial(std::move(column)));
  EXPECT_EQ(function->DebugString(), absl::StripLeadingAsciiWhitespace(R"(
AggregateFunctionCall(GoogleSQL:hll_count.merge_partial(BYTES) -> BYTES)
+-ColumnRef(type=BYTES, column=.#-1)
)"));
}

TEST_F(FunctionCallBuilderTest, HllMergePartialInt64TypeTest) {
  auto column =
      MakeResolvedColumnRef(types::Int64Type(), ResolvedColumn(), false);
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(std::unique_ptr<const ResolvedExpr> function,
                       fn_builder_.HllMergePartial(std::move(column)));
  EXPECT_EQ(function->DebugString(), absl::StripLeadingAsciiWhitespace(R"(
AggregateFunctionCall(GoogleSQL:hll_count.merge_partial(INT64) -> BYTES)
+-ColumnRef(type=INT64, column=.#-1)
)"));
}

TEST_F(FunctionCallBuilderTest, HllMergeBytesTest) {
  auto column =
      MakeResolvedColumnRef(types::BytesType(), ResolvedColumn(), false);
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(std::unique_ptr<const ResolvedExpr> function,
                       fn_builder_.HllMerge(std::move(column)));
  EXPECT_EQ(function->DebugString(), absl::StripLeadingAsciiWhitespace(R"(
AggregateFunctionCall(GoogleSQL:hll_count.merge(BYTES) -> INT64)
+-ColumnRef(type=BYTES, column=.#-1)
)"));
}

TEST_F(FunctionCallBuilderTest, HllMergeInt64TypeTest) {
  auto column =
      MakeResolvedColumnRef(types::Int64Type(), ResolvedColumn(), false);
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(std::unique_ptr<const ResolvedExpr> function,
                       fn_builder_.HllMerge(std::move(column)));
  EXPECT_EQ(function->DebugString(), absl::StripLeadingAsciiWhitespace(R"(
AggregateFunctionCall(GoogleSQL:hll_count.merge(INT64) -> INT64)
+-ColumnRef(type=INT64, column=.#-1)
)"));
}

TEST_F(FunctionCallBuilderTest, HllExtractBytesTest) {
  auto column =
      MakeResolvedColumnRef(types::BytesType(), ResolvedColumn(), false);
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(std::unique_ptr<const ResolvedExpr> function,
                       fn_builder_.HllExtract(std::move(column)));
  EXPECT_EQ(function->DebugString(), absl::StripLeadingAsciiWhitespace(R"(
FunctionCall(GoogleSQL:hll_count.extract(BYTES) -> INT64)
+-ColumnRef(type=BYTES, column=.#-1)
)"));
}

TEST_F(FunctionCallBuilderTest, HllExtractInt64TypeTest) {
  auto column =
      MakeResolvedColumnRef(types::Int64Type(), ResolvedColumn(), false);
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(std::unique_ptr<const ResolvedExpr> function,
                       fn_builder_.HllExtract(std::move(column)));
  EXPECT_EQ(function->DebugString(), absl::StripLeadingAsciiWhitespace(R"(
FunctionCall(GoogleSQL:hll_count.extract(INT64) -> INT64)
+-ColumnRef(type=INT64, column=.#-1)
)"));
}

class ValidateArgumentsDoNotContainCorrelationTest : public ::testing::Test {
 protected:
  ValidateArgumentsDoNotContainCorrelationTest()
      : column_factory_(0, &pool_, &seq_), catalog_("test_catalog") {}

  void SetUp() override {
    catalog_.AddBuiltinFunctions(
        BuiltinFunctionOptions::AllReleasedFunctions());
    int64_type_ = type_factory_.get_int64();
    bool_type_ = type_factory_.get_bool();

    dummy_tvf_scan_ = MakeResolvedTVFScan(
        /*column_list=*/{},
        /*tvf=*/nullptr,
        /*signature=*/nullptr,
        /*argument_list=*/{},
        /*column_index_list=*/{},
        /*alias=*/"tvf1",
        /*function_call_signature=*/nullptr);
  }

  TypeFactory type_factory_;
  IdStringPool pool_;
  googlesql_base::SequenceNumber seq_;
  ColumnFactory column_factory_;
  SimpleCatalog catalog_;
  AnalyzerOptions analyzer_options_;
  const Type* int64_type_;
  const Type* bool_type_;
  std::unique_ptr<ResolvedTVFScan> dummy_tvf_scan_;
};

TEST_F(ValidateArgumentsDoNotContainCorrelationTest,
       ArgumentWithExternalCorrelation) {
  // An argument that CONTAINS a correlated column reference to outside the
  // expression. Conceptual SQL:
  // ... MY_FUNC(..., outer_table.a, ...) ...
  ResolvedColumn outer_column =
      column_factory_.MakeCol("outer_table", "a", int64_type_);
  auto correlated_expr =
      MakeResolvedColumnRef(int64_type_, outer_column, /*is_correlated=*/true);

  EXPECT_THAT(
      ValidateArgumentsDoNotContainCorrelation(dummy_tvf_scan_.get(), "MY_FUNC",
                                               {correlated_expr.get()}),
      StatusIs(
          absl::StatusCode::kUnimplemented,
          HasSubstr(
              "MY_FUNC arguments must not contain correlated references to "
              "columns "
              "defined outside the scope of the argument expression itself")));
}

TEST_F(ValidateArgumentsDoNotContainCorrelationTest,
       ArgumentWithInternalCorrelation) {
  // An argument containing a subquery with a correlated column reference,
  // but the correlation is *within* the argument's expression, not external.
  // Conceptual SQL:
  // ... MY_FUNC(..., (SELECT y.b FROM y WHERE y.a = outer_table.a), ...) ...
  // This is fine because the correlation is self-contained within the subquery.

  ResolvedColumn y_a = column_factory_.MakeCol("y", "a", int64_type_);
  ResolvedColumn y_b = column_factory_.MakeCol("y", "b", int64_type_);
  ResolvedColumn inner_outer_a =
      column_factory_.MakeCol("outer_table", "a", int64_type_);

  std::vector<std::unique_ptr<const ResolvedColumnRef>> parameter_list;
  parameter_list.push_back(MakeResolvedColumnRef(int64_type_, inner_outer_a,
                                                 /*is_correlated=*/false));
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(
      auto filter_expr,
      FunctionCallBuilder(analyzer_options_, catalog_, type_factory_)
          .Equal(
              MakeResolvedColumnRef(int64_type_, y_a, /*is_correlated=*/false),
              MakeResolvedColumnRef(int64_type_, inner_outer_a,
                                    /*is_correlated=*/true)));

  auto subquery = MakeResolvedSubqueryExpr(
      int64_type_, ResolvedSubqueryExpr::SCALAR,
      /*parameter_list=*/std::move(parameter_list),
      /*in_expr=*/nullptr,
      MakeResolvedProjectScan(
          {y_b}, {},
          MakeResolvedFilterScan(
              {y_a, y_b},
              MakeResolvedTableScan({y_a, y_b}, /*table=*/nullptr,
                                    /*for_system_time_expr=*/nullptr),
              std::move(filter_expr))));

  GOOGLESQL_EXPECT_OK(ValidateArgumentsDoNotContainCorrelation(
      dummy_tvf_scan_.get(), "MY_FUNC", {subquery.get()}));
}

TEST_F(ValidateArgumentsDoNotContainCorrelationTest,
       ArgumentsWithoutCorrelation) {
  // Arguments that do NOT contain any externally correlated column refs.
  // Conceptual SQL:
  // ... MY_FUNC(..., local_table.b, 42, ...) ...
  ResolvedColumn local_column =
      column_factory_.MakeCol("local_table", "b", int64_type_);
  auto non_correlated_expr =
      MakeResolvedColumnRef(int64_type_, local_column, /*is_correlated=*/false);
  auto literal_expr = MakeResolvedLiteral(Value::Int64(42));

  GOOGLESQL_EXPECT_OK(ValidateArgumentsDoNotContainCorrelation(
      dummy_tvf_scan_.get(), "MY_FUNC",
      {non_correlated_expr.get(), literal_expr.get()}));
}

TEST_F(ValidateArgumentsDoNotContainCorrelationTest,
       ArgumentWithNonCorrelatedFunctionCall) {
  // Tests that a complex argument built using FunctionCallBuilder,
  // containing various function calls and only non-externally-correlated
  // column references, is correctly identified as not having external
  // correlations. Conceptual SQL:
  // ... MY_FUNC(..., local_table.b > 0 AND local_table.c < 0, ...) ...
  ResolvedColumn col_b =
      column_factory_.MakeCol("local_table", "b", int64_type_);
  ResolvedColumn col_c =
      column_factory_.MakeCol("local_table", "c", int64_type_);

  FunctionCallBuilder fn_builder(analyzer_options_, catalog_, type_factory_);

  GOOGLESQL_ASSERT_OK_AND_ASSIGN(
      auto greater_expr,
      fn_builder.Greater(MakeResolvedColumnRef(int64_type_, col_b, false),
                         MakeResolvedLiteral(Value::Int64(0))));

  GOOGLESQL_ASSERT_OK_AND_ASSIGN(
      auto less_expr,
      fn_builder.Less(MakeResolvedColumnRef(int64_type_, col_c, false),
                      MakeResolvedLiteral(Value::Int64(0))));

  std::vector<std::unique_ptr<const ResolvedExpr>> and_args;
  and_args.push_back(std::move(greater_expr));
  and_args.push_back(std::move(less_expr));

  GOOGLESQL_ASSERT_OK_AND_ASSIGN(auto expr, fn_builder.And(std::move(and_args)));

  GOOGLESQL_EXPECT_OK(ValidateArgumentsDoNotContainCorrelation(dummy_tvf_scan_.get(),
                                                     "MY_FUNC", {expr.get()}));
}

}  // namespace
}  // namespace googlesql
