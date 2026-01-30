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

#include "googlesql/resolved_ast/resolved_ast.h"

#include <cstdint>
#include <memory>
#include <set>
#include <string>
#include <utility>
#include <vector>

#include "googlesql/base/testing/status_matchers.h"
#include "googlesql/public/analyzer.h"
#include "googlesql/public/analyzer_output.h"
#include "googlesql/public/builtin_function_options.h"
#include "googlesql/public/catalog.h"
#include "googlesql/public/function.h"
#include "googlesql/public/function_signature.h"
#include "googlesql/public/id_string.h"
#include "googlesql/public/options.pb.h"
#include "googlesql/public/simple_catalog.h"
#include "googlesql/public/type.h"
#include "googlesql/public/type.pb.h"
#include "googlesql/public/value.h"
#include "googlesql/public/value.pb.h"
#include "googlesql/resolved_ast/make_node_vector.h"
#include "googlesql/resolved_ast/resolved_ast.pb.h"
#include "googlesql/resolved_ast/resolved_node.h"
#include "googlesql/resolved_ast/resolved_node_kind.h"
#include "googlesql/resolved_ast/resolved_node_kind.pb.h"
#include "googlesql/resolved_ast/serialization.pb.h"
#include "googlesql/resolved_ast/test_utils.h"
#include "googlesql/resolved_ast/validator.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "absl/base/log_severity.h"
#include "googlesql/base/check.h"
#include "absl/log/log.h"
#include "absl/status/status.h"
#include "absl/strings/str_cat.h"
#include "googlesql/base/status.h"

namespace googlesql {

using ::googlesql::testing::MakeSelect1Stmt;
using ::testing::ContainerEq;
using ::testing::ElementsAre;
using ::testing::HasSubstr;
using ::testing::NotNull;
using ::testing::UnorderedElementsAre;
using ::testing::UnorderedElementsAreArray;
using ::absl_testing::StatusIs;

class ResolvedASTTest : public ::testing::Test {
 public:
  // Make a new ResolvedColumn with int32 type.
  ResolvedColumn MakeColumn() {
    return ResolvedColumn(
        ++column_id_, googlesql::IdString::MakeGlobal("MakeColumn"),
        googlesql::IdString::MakeGlobal("C"), types::Int32Type());
  }

 private:
  int column_id_ = 1000;
};

static const SimpleTable* t1 = new SimpleTable("T1");
static const SimpleTable* t2 = new SimpleTable("T2");

static std::unique_ptr<const ResolvedJoinScan> MakeJoin(
    ResolvedJoinScan::JoinType type = ResolvedJoinScan::INNER) {
  auto node = MakeResolvedJoinScan(
      {} /* column_list */, type,
      MakeResolvedTableScan({} /* column_list */, t1, nullptr /* systime */),
      MakeResolvedTableScan({} /* column_list */, t2, nullptr /* systime */),
      nullptr /* join_condition */);
  ABSL_LOG(INFO) << "Made " << node->DebugString();
  return node;
}

static TypeFactory* type_factory = new TypeFactory;

static std::unique_ptr<const ResolvedLiteral> MakeIntLiteral(int value) {
  return MakeResolvedLiteral(Value::Int64(value));
}

static const ResolvedTableScan* AsTableScan(const ResolvedScan* scan) {
  return static_cast<const ResolvedTableScan*>(scan);
}

TEST_F(ResolvedASTTest, Misc) {
  // Test calling various getters and setters for scalars, nodes and vectors.
  // Heap checker checks for correct ownership transfer and cleanup.
  TypeFactory type_factory;
  const FunctionSignature signature(
      {type_factory.get_bool(), {}, nullptr /* context */});
  std::unique_ptr<const Function> function(
      new Function("fn_name", "group", Function::SCALAR, {signature}));

  const ResolvedColumn select_column(10, googlesql::IdString::MakeGlobal("T"),
                                     googlesql::IdString::MakeGlobal("C"),
                                     type_factory.get_int32());
  auto project = MakeResolvedProjectScan(
      {select_column},
      MakeNodeVector(MakeResolvedComputedColumn(
          select_column, MakeIntLiteral(4))) /* expr_list */,
      MakeJoin() /* input_scan */);
  ABSL_LOG(INFO) << project->DebugString();
  EXPECT_EQ(RESOLVED_PROJECT_SCAN, project->node_kind());
  EXPECT_EQ(1, project->expr_list_size());
  EXPECT_EQ(select_column, project->expr_list(0)->column());
  EXPECT_EQ(RESOLVED_LITERAL, project->expr_list(0)->expr()->node_kind());
  EXPECT_EQ(RESOLVED_JOIN_SCAN, project->input_scan()->node_kind());

  auto new_scan = MakeResolvedTableScan({}, t1, nullptr, "t1");
  EXPECT_EQ(t1, new_scan->table());
  EXPECT_EQ("t1", new_scan->alias());
  new_scan->set_table(t2);
  EXPECT_EQ("t1", new_scan->alias());
  EXPECT_EQ(t2, new_scan->table());

  project->set_input_scan(std::move(new_scan));
  project->add_expr_list(
      MakeResolvedComputedColumn(MakeColumn(), MakeIntLiteral(-1234)));
  ABSL_LOG(INFO) << project->DebugString();

  EXPECT_EQ("ProjectScan", project->node_kind_string());
  EXPECT_EQ("ProjectScan", ResolvedNodeKindToString(RESOLVED_PROJECT_SCAN));

  EXPECT_EQ(1, project->column_list_size());
  std::vector<ResolvedColumn>* column_list = project->mutable_column_list();
  column_list->push_back((*column_list)[0]);
  EXPECT_EQ(2, project->column_list_size());
  EXPECT_EQ(select_column, project->column_list(0));
  EXPECT_EQ(select_column, project->column_list(1));
}

TEST_F(ResolvedASTTest, DebugStringWithNullInTree) {
  // Even though trees with nullptr AST nodes in them are not valid, we still
  // want to be able to get a debug string without crashing that indicates the
  // location of the null ast node within the tree.
  TypeFactory type_factory;
  const Type* int64_type = type_factory.get_int64();
  Function function("test", "test_group", Function::SCALAR);
  FunctionArgumentTypeList function_arg_types = {
      FunctionArgumentType(int64_type), FunctionArgumentType(int64_type)};
  FunctionSignature sig(FunctionArgumentType(int64_type), function_arg_types,
                        static_cast<int64_t>(1234));

  std::vector<std::unique_ptr<ResolvedExpr>> args;
  args.push_back(MakeResolvedLiteral(Value::Int64(1)));
  args.push_back(nullptr);

  std::unique_ptr<ResolvedFunctionCall> call = MakeResolvedFunctionCall(
      type_factory.get_int64(), &function, sig, std::move(args),
      ResolvedFunctionCall::DEFAULT_ERROR_MODE);
  EXPECT_EQ(R"(
FunctionCall(test_group:test(INT64, INT64) -> INT64)
+-Literal(type=INT64, value=1)
+-<nullptr AST node>
)",
            absl::StrCat("\n", call->DebugString()));
}

TEST_F(ResolvedASTTest, DebugStringWithNullInTree2) {
  IdStringPool pool;
  std::unique_ptr<ResolvedQueryStmt> query_stmt = MakeSelect1Stmt(pool);
  const_cast<ResolvedComputedColumn*>(
      query_stmt->query()->GetAs<ResolvedProjectScan>()->expr_list(0))
      ->release_expr();

  EXPECT_EQ(R"(
QueryStmt
+-output_column_list=
| +-tbl.x#1 AS x [INT64]
+-query=
  +-ProjectScan
    +-column_list=[tbl.x#1]
    +-expr_list=
    | +-x#1 := <nullptr AST node>
    +-input_scan=
      +-SingleRowScan
)",
            absl::StrCat("\n", query_stmt->DebugString()));
}

TEST_F(ResolvedASTTest, DebugStringPrintAccessedWithNullInTree2) {
  IdStringPool pool;
  std::unique_ptr<ResolvedQueryStmt> query_stmt = MakeSelect1Stmt(pool);
  const_cast<ResolvedComputedColumn*>(
      query_stmt->query()->GetAs<ResolvedProjectScan>()->expr_list(0))
      ->release_expr();

  EXPECT_EQ(
      R"(
QueryStmt
+-output_column_list{ }=
| +-tbl.x#1 AS x [INT64]
+-query{*}=
  +-ProjectScan
    +-column_list{ }=[tbl.x#1]
    +-expr_list{*}=
    | +-x#1 := <nullptr AST node>
    +-input_scan{ }=
      +-SingleRowScan
)",
      absl::StrCat("\n", query_stmt->DebugString({.print_accessed = true})));
}

TEST_F(ResolvedASTTest, DebugStringPrintCreatedColumnsInTableScan) {
  SimpleColumn column("bar", "baz", types::Int64Type());
  SimpleTable table("bar", {&column}, false, 123);
  SimpleCatalog catalog("foo");
  catalog.AddTable(&table);
  TypeFactory factory;
  std::unique_ptr<const AnalyzerOutput> output;
  AnalyzerOptions options = AnalyzerOptions();
  options.set_record_parse_locations(true);
  GOOGLESQL_ASSERT_OK(googlesql::AnalyzeStatement("select baz from bar;", options,
                                        &catalog, &factory, &output));
  const ResolvedStatement* query_stmt = output->resolved_statement();

  EXPECT_THAT(query_stmt->DebugString({.print_created_columns = true}),
              R"(QueryStmt
+-output_column_list=
| +-bar.baz#1 AS baz [INT64]
+-query=
  +-ProjectScan
    +-parse_location=0-19
    +-column_list=[bar.baz#1]
    +-input_scan=
      +-TableScan(parse_location=16-19, column_list{c}=[bar.baz#1], table=bar, column_index_list=[0])
)");
}

TEST_F(ResolvedASTTest,
       DebugStringPrintCreatedColumnsInArrayScanAndComputedColumn) {
  TypeFactory factory;
  SimpleCatalog catalog("catalog", &factory);
  catalog.AddBuiltinFunctions(BuiltinFunctionOptions::AllReleasedFunctions());
  AnalyzerOptions options;
  std::unique_ptr<const AnalyzerOutput> output;
  GOOGLESQL_ASSERT_OK(AnalyzeStatement(
      "SELECT *, numbers+1 FROM UNNEST ([10,20,30]) as numbers;", options,
      &catalog, &factory, &output));
  const ResolvedStatement* query_stmt = output->resolved_statement();

  EXPECT_THAT(query_stmt->DebugString({.print_created_columns = true}),
              R"(QueryStmt
+-output_column_list=
| +-$array.numbers#1 AS numbers [INT64]
| +-$query.$col2#2 AS `$col2` [INT64]
+-query=
  +-ProjectScan
    +-column_list=[$array.numbers#1, $query.$col2#2]
    +-expr_list=
    | +-$col2#2{c} :=
    |   +-FunctionCall(GoogleSQL:$add(INT64, INT64) -> INT64)
    |     +-ColumnRef(type=INT64, column=$array.numbers#1)
    |     +-Literal(type=INT64, value=1)
    +-input_scan=
      +-ArrayScan
        +-column_list=[$array.numbers#1]
        +-array_expr_list=
        | +-Literal(type=ARRAY<INT64>, value=[10, 20, 30])
        +-element_column_list{c}=[$array.numbers#1]
)");

  // Test with both print_created_columns and print_accessed enabled.
  EXPECT_THAT(query_stmt->DebugString(
                  {.print_accessed = true, .print_created_columns = true}),
              HasSubstr("+-element_column_list{c}{ }=[$array.numbers#1]"));
}

TEST_F(ResolvedASTTest, DebugStringPrintColumnHolderAsCreated) {
  TypeFactory factory;
  SimpleCatalog catalog("catalog", &factory);
  catalog.AddBuiltinFunctions(BuiltinFunctionOptions::AllReleasedFunctions());
  LanguageOptions language_options;
  language_options.EnableLanguageFeature(FEATURE_UNNEST_AND_FLATTEN_ARRAYS);
  AnalyzerOptions options(std::move(language_options));
  std::unique_ptr<const AnalyzerOutput> output;
  GOOGLESQL_ASSERT_OK(AnalyzeStatement(
      "SELECT FLATTEN(numbers.n) FROM (SELECT [STRUCT(1 as n)] as numbers);",
      options, &catalog, &factory, &output));
  const ResolvedStatement* query_stmt = output->resolved_statement();

  EXPECT_THAT(query_stmt->DebugString(
                  {.print_accessed = true, .print_created_columns = true}),
              HasSubstr(R"(+-array_offset_column{c}{*}=
    |         |           |         +-ColumnHolder(column{*}=$offset.injected#5))"));
}

TEST_F(ResolvedASTTest, DebugStringAnnotationsFunctionCall) {
  TypeFactory type_factory;

  std::unique_ptr<ResolvedFunctionCall> call =
      googlesql::testing::WrapInFunctionCall(
          &type_factory, MakeResolvedLiteral(Value::Int64(1)),
          MakeResolvedLiteral(Value::Int64(2)),
          MakeResolvedLiteral(Value::Int64(3)));
  EXPECT_EQ(
      R"(
FunctionCall(test_group:test(INT64, INT64, INT64) -> INT64) (test - call root)
+-Literal(type=INT64, value=1) (test - arg 0)
+-Literal(type=INT64, value=2) (test - arg 1)
+-Literal(type=INT64, value=3)
)",
      absl::StrCat(
          absl::StrCat("\n", call->DebugString(ResolvedNode::DebugStringConfig{
                                 {{call->argument_list(0), "(test - arg 0)"},
                                  {call->argument_list(1), "(test - arg 1)"},
                                  {call.get(), "(test - call root)"}},
                                 false}))));
}

TEST_F(ResolvedASTTest, DebugStringAnnotationsAndPrintAccessedFunctionCall) {
  TypeFactory type_factory;

  std::unique_ptr<ResolvedFunctionCall> call =
      googlesql::testing::WrapInFunctionCall(
          &type_factory, MakeResolvedLiteral(Value::Int64(1)),
          MakeResolvedLiteral(Value::Int64(2)),
          MakeResolvedLiteral(Value::Int64(3)));
  EXPECT_EQ(
      R"(
FunctionCall(test_group:test(INT64, INT64, INT64) -> INT64) (test - call root)
+-Literal(type{*}=INT64, value{ }=1) (test - arg 0)
+-Literal(type{*}=INT64, value{ }=2) (test - arg 1)
+-Literal(type{*}=INT64, value{ }=3)
)",
      absl::StrCat(absl::StrCat(
          "\n", call->DebugString(
                    {.annotations{{call->argument_list(0), "(test - arg 0)"},
                                  {call->argument_list(1), "(test - arg 1)"},
                                  {call.get(), "(test - call root)"}},
                     .print_accessed = true}))));
}

TEST_F(ResolvedASTTest, QueryStmtDebugString) {
  IdStringPool pool;

  std::unique_ptr<ResolvedQueryStmt> stmt = testing::MakeSelect1Stmt(pool);
  EXPECT_EQ(
      R"(
QueryStmt (test - query)
+-output_column_list=
| +-tbl.x#1 AS x [INT64] (test - output col)
+-query=
  +-ProjectScan
    +-column_list=[tbl.x#1]
    +-expr_list=
    | +-x#1 := Literal(type=INT64, value=1)
    +-input_scan=
      +-SingleRowScan
)",
      absl::StrCat("\n",
                   stmt->DebugString(ResolvedNode::DebugStringConfig{
                       {{stmt.get(), "(test - query)"},
                        {stmt->output_column_list(0), "(test - output col)"}},
                       false})));
}

TEST_F(ResolvedASTTest, ReleaseAndSet) {
  TypeFactory type_factory;
  const FunctionSignature signature(
      {type_factory.get_bool(), {}, nullptr /* context */});
  std::unique_ptr<const Function> function(
      new Function("fn_name", "group", Function::SCALAR, {signature}));

  const ResolvedColumn select_column(10, googlesql::IdString::MakeGlobal("T"),
                                     googlesql::IdString::MakeGlobal("C"),
                                     type_factory.get_int32());
  std::vector<std::unique_ptr<const ResolvedComputedColumn>> expr_list;
  expr_list.push_back(
      MakeResolvedComputedColumn(select_column, MakeIntLiteral(4)));
  auto project = MakeResolvedProjectScan({select_column}, std::move(expr_list),
                                         MakeJoin() /* input_scan */);

  // test Node Pointer interface.
  const ResolvedScan* scan_ptr = project->input_scan();
  EXPECT_NE(nullptr, project->input_scan());
  // We take the pointer.
  std::unique_ptr<const ResolvedScan> scan = project->release_input_scan();
  EXPECT_EQ(scan_ptr, scan.get());
  EXPECT_EQ(nullptr, project->input_scan());

  // Give back the pointer.
  project->set_input_scan(std::move(scan));
  scan_ptr = project->input_scan();
  EXPECT_EQ(scan_ptr, project->input_scan());

  // Test node vector interface.
  EXPECT_EQ(1, project->expr_list_size());

  project->add_expr_list(
      MakeResolvedComputedColumn(select_column, MakeIntLiteral(4)));
  EXPECT_EQ(2, project->expr_list_size());

  // We take the pointers.
  std::vector<std::unique_ptr<const ResolvedComputedColumn>> new_expr_list =
      project->release_expr_list();
  EXPECT_EQ(0, project->expr_list_size());
  EXPECT_EQ(2, new_expr_list.size());

  // Give them back.
  project->set_expr_list(std::move(new_expr_list));
  EXPECT_EQ(2, project->expr_list_size());
}

TEST_F(ResolvedASTTest, GetAs) {
  std::unique_ptr<const ResolvedNode> node = MakeJoin();
  EXPECT_EQ(RESOLVED_JOIN_SCAN, node->node_kind());
  EXPECT_EQ("JoinScan", node->node_kind_string());

  const ResolvedJoinScan* join_scan = node->GetAs<ResolvedJoinScan>();
  EXPECT_EQ(RESOLVED_JOIN_SCAN, join_scan->node_kind());

  // This bug is caught at compile time because there is no valid static_cast
  // from JoinScan to ArrayScan.
  //   join_scan->GetAs<ResolvedArrayScan>();

  const ResolvedScan* scan = join_scan;  // Implicit up cast.
  EXPECT_EQ("JoinScan", scan->node_kind_string());
  EXPECT_EQ(ResolvedJoinScan::TYPE, scan->node_kind());
}

TEST_F(ResolvedASTTest, GetAsResolvedNode) {
  std::unique_ptr<const ResolvedNode> node = MakeJoin();
  EXPECT_EQ(RESOLVED_JOIN_SCAN, node->node_kind());
  EXPECT_EQ("JoinScan", node->node_kind_string());

  std::unique_ptr<const ResolvedJoinScan> join =
      GetAsResolvedNode<ResolvedJoinScan>(std::move(node));
  EXPECT_EQ(RESOLVED_JOIN_SCAN, join->node_kind());
  EXPECT_EQ("JoinScan", join->node_kind_string());
}

TEST_F(ResolvedASTTest, CheckFieldsAccessed) {
  // Fields get marked as accessed when we read them to call DebugString.
  // Note that calling node()->DebugString() marks node as accesssed but
  // node's children are still unaccessed.
  std::unique_ptr<const ResolvedJoinScan> node = MakeJoin();
  AsTableScan(node->left_scan())->table()->FullName();
  AsTableScan(node->right_scan())->table()->FullName();
  node->join_type();
  GOOGLESQL_EXPECT_OK(node->CheckFieldsAccessed());

  EXPECT_EQ(RESOLVED_JOIN_SCAN, node->node_kind());
  EXPECT_EQ(RESOLVED_TABLE_SCAN, node->left_scan()->node_kind());
  EXPECT_EQ(RESOLVED_TABLE_SCAN, node->right_scan()->node_kind());

  node = MakeJoin();
  AsTableScan(node->left_scan())->table()->FullName();
  node->join_type();
  EXPECT_EQ(
      R"(Unimplemented feature (ResolvedJoinScan::right_scan not accessed)
JoinScan (*** This node has unaccessed field ***)
+-left_scan{*}=
| +-TableScan(table{*}=T1)
+-right_scan{ }=
  +-TableScan(table{ }=T2)
)",
      node->CheckFieldsAccessed().message());
  // DebugString does not count as accessing the members inside.
  node->right_scan()->DebugString();
  EXPECT_EQ(R"(Unimplemented feature (ResolvedTableScan::table not accessed)
JoinScan
+-left_scan{*}=
| +-TableScan(table{*}=T1)
+-right_scan{*}=
  +-TableScan(table{ }=T2) (*** This node has unaccessed field ***)
)",
            node->CheckFieldsAccessed().message());
  AsTableScan(node->right_scan())->table()->FullName();
  GOOGLESQL_EXPECT_OK(node->CheckFieldsAccessed());

  // Unaccessed type, but it has the default value.
  node = MakeJoin();
  AsTableScan(node->left_scan())->table()->FullName();
  AsTableScan(node->right_scan())->table()->FullName();
  GOOGLESQL_EXPECT_OK(node->CheckFieldsAccessed());

  // Unaccessed type, with a non-default value.
  node = MakeJoin(ResolvedJoinScan::LEFT);
  AsTableScan(node->left_scan())->table()->FullName();
  AsTableScan(node->right_scan())->table()->FullName();
  EXPECT_EQ(
      R"(Unimplemented feature (ResolvedJoinScan::join_type not accessed and has non-default value)
JoinScan (*** This node has unaccessed field ***)
+-join_type{ }=LEFT
+-left_scan{*}=
| +-TableScan(table{*}=T1)
+-right_scan{*}=
  +-TableScan(table{*}=T2)
)",
      node->CheckFieldsAccessed().message());

  // Make sure MarkFieldsAccessed() works.
  node = MakeJoin();
  AsTableScan(node->left_scan())->table()->FullName();
  node->right_scan();
  EXPECT_EQ(R"(Unimplemented feature (ResolvedTableScan::table not accessed)
JoinScan
+-left_scan{*}=
| +-TableScan(table{*}=T1)
+-right_scan{*}=
  +-TableScan(table{ }=T2) (*** This node has unaccessed field ***)
)",
            node->CheckFieldsAccessed().message());
  node->right_scan()->MarkFieldsAccessed();
  GOOGLESQL_EXPECT_OK(node->CheckFieldsAccessed());
}

// Test CheckFieldsAccessed on a vector field.
TEST_F(ResolvedASTTest, CheckVectorFieldsAccessed) {
  TypeFactory type_factory;
  auto node = MakeResolvedProjectScan();

  // Run three iterations.
  // 0: Empty vector, testing expr_list_size.  Also add elements.
  // 1: Test expr_list(i).
  // 2: Test expr_list().
  for (int i = 0; i < 3; ++i) {
    SCOPED_TRACE(absl::StrCat("CheckVectorFieldsAccessed pass ", i));

    // Reset, and then mark scalar fields as accessed.
    node->ClearFieldsAccessed();
    node->input_scan();

    if (i != 0) {
      // Reading _size doesn't mark field as accessed if the
      // vector is non-empty.
      EXPECT_EQ(3, node->expr_list_size());
    }

    if (i == 0) {
      EXPECT_EQ(
          R"(Unimplemented feature (ResolvedProjectScan::expr_list not accessed)
ProjectScan (*** This node has unaccessed field ***)
)",
          node->CheckFieldsAccessed().message());

      // If the vector is empty, reading _size marks it accessed.
      EXPECT_EQ(0, node->expr_list_size());

      GOOGLESQL_EXPECT_OK(node->CheckFieldsAccessed());

      node->add_expr_list(MakeResolvedComputedColumn(
          MakeColumn(), MakeResolvedLiteral(Value::Bool(true))));
      node->add_expr_list(MakeResolvedComputedColumn(
          MakeColumn(), MakeResolvedLiteral(Value::Bool(false))));
      node->add_expr_list(MakeResolvedComputedColumn(
          MakeColumn(), MakeResolvedLiteral(Value::Bool(true))));
      // Note: Mutating or adding elements doesn't reset accessed bits anywhere,
      // but the new elements won't be accessed yet.
      // Now the vector is accessed, but some of its elements aren't.
      EXPECT_EQ(
          R"(Unimplemented feature (ResolvedComputedColumn::expr not accessed)
ProjectScan
+-expr_list{*}=
  +-C#1001 := Literal(type{ }=BOOL, value{ }=true) (*** This node has unaccessed field ***)
  +-C#1002 := Literal(type{ }=BOOL, value{ }=false)
  +-C#1003 := Literal(type{ }=BOOL, value{ }=true)
)",
          node->CheckFieldsAccessed().message());
    } else if (i == 1) {
      EXPECT_EQ(
          R"(Unimplemented feature (ResolvedProjectScan::expr_list not accessed)
ProjectScan (*** This node has unaccessed field ***)
+-expr_list{ }=
  +-C#1001 := Literal(type{ }=BOOL, value{ }=true)
  +-C#1002 := Literal(type{ }=BOOL, value{ }=false)
  +-C#1003 := Literal(type{ }=BOOL, value{ }=true)
)",
          node->CheckFieldsAccessed().message());

      // Access one of the vector elements, and its bool value.
      static_cast<const ResolvedLiteral*>(node->expr_list(1)->expr())->value();

      // Now the vector is accessed, but some of its elements aren't.
      EXPECT_EQ(
          R"(Unimplemented feature (ResolvedComputedColumn::expr not accessed)
ProjectScan
+-expr_list{*}=
  +-C#1001 := Literal(type{ }=BOOL, value{ }=true) (*** This node has unaccessed field ***)
  +-C#1002 := Literal(type{ }=BOOL, value{*}=false)
  +-C#1003 := Literal(type{ }=BOOL, value{ }=true)
)",
          node->CheckFieldsAccessed().message());
    } else /* i == 2 */ {
      EXPECT_EQ(
          R"(Unimplemented feature (ResolvedProjectScan::expr_list not accessed)
ProjectScan (*** This node has unaccessed field ***)
+-expr_list{ }=
  +-C#1001 := Literal(type{ }=BOOL, value{ }=true)
  +-C#1002 := Literal(type{ }=BOOL, value{ }=false)
  +-C#1003 := Literal(type{ }=BOOL, value{ }=true)
)",
          node->CheckFieldsAccessed().message());

      EXPECT_EQ(2, i);
      // Access the whole vector, and element 1's bool value.
      static_cast<const ResolvedLiteral*>(node->expr_list()[1]->expr())->value();
    // Now the vector is accessed, but some of its elements aren't.
      EXPECT_EQ(
          R"(Unimplemented feature (ResolvedComputedColumn::expr not accessed)
ProjectScan
+-expr_list{*}=
  +-C#1001 := Literal(type{ }=BOOL, value{ }=true) (*** This node has unaccessed field ***)
  +-C#1002 := Literal(type{ }=BOOL, value{*}=false)
  +-C#1003 := Literal(type{ }=BOOL, value{ }=true)
)",
          node->CheckFieldsAccessed().message());
    }

    // We get an OK result once we access all vector elements.
    for (int j = 0; j < node->expr_list_size(); ++j) {
      EXPECT_FALSE(node->CheckFieldsAccessed().ok());
      static_cast<const ResolvedLiteral*>(node->expr_list(j)->expr())->value();
    }
    GOOGLESQL_EXPECT_OK(node->CheckFieldsAccessed());

    // Make sure MarkFieldsAccessed() on vector works.
    node->ClearFieldsAccessed();
    node->MarkFieldsAccessed();
    GOOGLESQL_EXPECT_OK(node->CheckFieldsAccessed());
  }
}

// Test CheckFieldsAccessed on a optional-to-access field with children.
TEST_F(ResolvedASTTest, CheckOptionalFieldsAccessed) {
  std::unique_ptr<ResolvedTableScan> scan(
      MakeResolvedTableScan({} /* column_list */, t1, nullptr));

  EXPECT_FALSE(scan->CheckFieldsAccessed().ok());
  scan->table();
  GOOGLESQL_EXPECT_OK(scan->CheckFieldsAccessed());

  auto hint_uptr =
      MakeResolvedOption(/*qualifier=*/"foo", "key", MakeIntLiteral(7));
  const ResolvedOption* hint = hint_uptr.get();
  // Add a hint_list entry, which is an ignorable field.
  scan->add_hint_list(std::move(hint_uptr));

  // It is okay to ignore the hint_list, even though its own fields
  // have not been accessed.
  EXPECT_FALSE(hint->CheckFieldsAccessed().ok());
  EXPECT_FALSE(hint->CheckFieldsAccessed().ok());
  GOOGLESQL_EXPECT_OK(scan->CheckFieldsAccessed());
  EXPECT_FALSE(hint->CheckFieldsAccessed().ok());

  // Accessing hint_list_size doesn't count as making it accessed if
  // the list is non-empty.
  EXPECT_EQ(1, scan->hint_list_size());
  EXPECT_FALSE(hint->CheckFieldsAccessed().ok());
  GOOGLESQL_EXPECT_OK(scan->CheckFieldsAccessed());

  // Accessing an element of hint_list makes it accessed, but now we should
  // also be accessing its children, which we haven't done yet.
  EXPECT_EQ(hint, scan->hint_list(0));
  EXPECT_FALSE(hint->CheckFieldsAccessed().ok());
  EXPECT_FALSE(scan->CheckFieldsAccessed().ok());

  // Access the children on the ResolvedOption.  Since value is ignorable,
  // we don't have to look at its contents to make the hint okay.
  hint->qualifier();
  hint->name();

  GOOGLESQL_EXPECT_OK(hint->CheckFieldsAccessed());
  GOOGLESQL_EXPECT_OK(scan->CheckFieldsAccessed());

  // Once we fetch the value, we are required to look inside at its contents.
  const ResolvedExpr* value_expr = hint->value();
  EXPECT_FALSE(value_expr->CheckFieldsAccessed().ok());
  EXPECT_FALSE(hint->CheckFieldsAccessed().ok());
  EXPECT_FALSE(scan->CheckFieldsAccessed().ok());

  EXPECT_EQ(RESOLVED_LITERAL, value_expr->node_kind());
  value_expr->GetAs<ResolvedLiteral>()->value();

  GOOGLESQL_EXPECT_OK(value_expr->CheckFieldsAccessed());
  GOOGLESQL_EXPECT_OK(hint->CheckFieldsAccessed());
  GOOGLESQL_EXPECT_OK(scan->CheckFieldsAccessed());

  scan->ClearFieldsAccessed();
  EXPECT_FALSE(value_expr->CheckFieldsAccessed().ok());
  EXPECT_FALSE(hint->CheckFieldsAccessed().ok());
  EXPECT_FALSE(scan->CheckFieldsAccessed().ok());
}

// This is a minimal test for the Validator based on the old test for the
// removed CheckValid method, which just checked for missing required fields.
// We get lots of coverage for successful Validator calls from resolver tests,
// but not much for unsuccessful validations.
TEST_F(ResolvedASTTest, Validator) {
  Validator validator;
  TypeFactory type_factory;
  auto bad_literal = MakeResolvedLiteral(nullptr, Value::Int64(4));
  auto good_literal =
      MakeResolvedLiteral(type_factory.get_int32(), Value::Int32(5));

  SimpleColumn column1("table", "col1", types::Int64Type());
  SimpleColumn column2("table", "col2", types::Int64Type());
  SimpleTable table("table", {&column1, &column2}, false, 123);

  auto table_scan_uptr = MakeResolvedTableScan({}, &table, nullptr);
  ResolvedTableScan* table_scan = table_scan_uptr.get();
  auto filter_scan_uptr = MakeResolvedFilterScan(
      {}, std::move(table_scan_uptr),
      MakeResolvedLiteral(type_factory.get_bool(), Value::Bool(true)));
  ResolvedFilterScan* filter_scan = filter_scan_uptr.get();
  const FunctionSignature signature(
      {FunctionArgumentType(type_factory.get_bool(),
                            FunctionArgumentType::REQUIRED, 1),
       {},
       nullptr /* context */});
  std::unique_ptr<const Function> function(
      new Function("fn_name", "group", Function::SCALAR, {signature}));

  auto bad_where =
      MakeResolvedFunctionCall(nullptr, function.get(), signature, {},
                               ResolvedFunctionCallBase::DEFAULT_ERROR_MODE);

  auto bad_where_2 = MakeResolvedFunctionCall(
      signature.result_type().type(), nullptr, signature, {},
      ResolvedFunctionCallBase::DEFAULT_ERROR_MODE);

  auto good_where = MakeResolvedFunctionCall(
      signature.result_type().type(), function.get(), signature, {},
      ResolvedFunctionCallBase::DEFAULT_ERROR_MODE);

  auto project_uptr =
      MakeResolvedProjectScan({} /* column_list */, {} /* expr_list */,
                              std::move(filter_scan_uptr) /* input_scan */);
  ResolvedProjectScan* project = project_uptr.get();
  auto limit_offset_uptr = MakeResolvedLimitOffsetScan(
      {} /* column_list */, std::move(project_uptr) /* input_scan */,
      MakeResolvedLiteral(type_factory.get_int64(),
                          Value::NullInt64()) /* limit */,
      nullptr /* offset */);
  ResolvedLimitOffsetScan* limit_offset = limit_offset_uptr.get();

  auto stmt_uptr = MakeResolvedQueryStmt({} /* output_column_list */,
                                         false /* is_value_table */,
                                         std::move(limit_offset_uptr));
  ResolvedQueryStmt* stmt = stmt_uptr.get();
  EXPECT_THAT(validator.ValidateResolvedStatement(stmt),
              StatusIs(absl::StatusCode::kInternal,
                       HasSubstr("Unexpected literal with null value")));

  limit_offset->set_limit(
      MakeResolvedLiteral(type_factory.get_int64(), Value::Int64(1)));
  const ResolvedColumn resolved_column1(
      1, googlesql::IdString::MakeGlobal("Table"),
      googlesql::IdString::MakeGlobal("col1"), type_factory.get_int32());
  stmt->add_output_column_list(
      MakeResolvedOutputColumn("col1", resolved_column1));
  const ResolvedColumn resolved_column2(
      2, googlesql::IdString::MakeGlobal("Table"),
      googlesql::IdString::MakeGlobal("col2"), type_factory.get_int32());
  EXPECT_THAT(validator.ValidateResolvedStatement(stmt),
              StatusIs(absl::StatusCode::kInternal,
                       HasSubstr("Incorrect reference to column "
                                 "Table.col1#1")));

  project->set_column_list({resolved_column1});
  limit_offset->set_column_list({resolved_column1});
  EXPECT_THAT(validator.ValidateResolvedStatement(stmt),
              StatusIs(absl::StatusCode::kInternal,
                       HasSubstr("Column list contains column Table.col1#1 not "
                                 "visible in scan node\n"
                                 "ProjectScan")));

  filter_scan->set_column_list({resolved_column1});
  EXPECT_THAT(validator.ValidateResolvedStatement(stmt),
              StatusIs(absl::StatusCode::kInternal,
                       HasSubstr("Column list contains column Table.col1#1 not "
                                 "visible in scan node\n"
                                 "FilterScan")));

  table_scan->set_column_list({resolved_column2});
  table_scan->set_column_index_list({1});
  EXPECT_THAT(validator.ValidateResolvedStatement(stmt),
              StatusIs(absl::StatusCode::kInternal,
                       HasSubstr("Column list contains column Table.col1#1 not "
                                 "visible in scan node\n"
                                 "FilterScan")));

  table_scan->add_column_list(resolved_column1);
  table_scan->add_column_index_list(0);
  GOOGLESQL_EXPECT_OK(validator.ValidateResolvedStatement(stmt));

  const ResolvedColumn resolved_column3(
      3, googlesql::IdString::MakeGlobal("$query"),
      googlesql::IdString::MakeGlobal("col3"), type_factory.get_int32());
  project->add_expr_list(
      MakeResolvedComputedColumn(resolved_column3, std::move(good_literal)));
  GOOGLESQL_EXPECT_OK(validator.ValidateResolvedStatement(stmt));

  filter_scan->set_filter_expr(std::move(bad_where));
  EXPECT_THAT(
      validator.ValidateResolvedStatement(stmt),
      StatusIs(absl::StatusCode::kInternal,
               HasSubstr("ResolvedExpr does not have a Type:\nFunctionCall")));

  filter_scan->set_filter_expr(std::move(bad_where_2));

  EXPECT_THAT(
      validator.ValidateResolvedStatement(stmt),
      StatusIs(
          absl::StatusCode::kInternal,
          HasSubstr(
              "ResolvedFunctionCall does not have a Function:\nFunctionCall")));

  filter_scan->set_filter_expr(std::move(good_where));
  GOOGLESQL_EXPECT_OK(validator.ValidateResolvedStatement(stmt));

  project->add_expr_list(
      MakeResolvedComputedColumn(MakeColumn(), std::move(bad_literal)));
  EXPECT_THAT(
      validator.ValidateResolvedStatement(stmt),
      StatusIs(
          absl::StatusCode::kInternal,
          HasSubstr("ResolvedExpr does not have a Type:\nLiteral(value=4)")));
}

TEST_F(ResolvedASTTest, GetChildNodes) {
  // One child.
  {
    auto table1_scan = MakeResolvedTableScan({}, t1, nullptr);
    std::vector<const ResolvedNode*> expected_child_nodes = {table1_scan.get()};
    auto filter_scan = MakeResolvedFilterScan({}, std::move(table1_scan),
                                              nullptr /* filter_expr */);
    std::vector<const ResolvedNode*> child_nodes;
    filter_scan->GetChildNodes(&child_nodes);
    EXPECT_THAT(child_nodes, ContainerEq(expected_child_nodes));
  }

  // Multiple children.
  {
    auto table1_scan = MakeResolvedTableScan({}, t1, nullptr);
    auto table2_scan = MakeResolvedTableScan({}, t2, nullptr);
    std::vector<const ResolvedNode*> expected_child_nodes = {table1_scan.get(),
                                                             table2_scan.get()};

    auto join_scan = MakeResolvedJoinScan(
        {} /* column_list */, ResolvedJoinScan::INNER, std::move(table1_scan),
        std::move(table2_scan), nullptr /* join_condition */);
    std::vector<const ResolvedNode*> child_nodes;
    join_scan->GetChildNodes(&child_nodes);
    EXPECT_THAT(child_nodes, UnorderedElementsAreArray(expected_child_nodes));

    auto table2_scan2 = MakeResolvedTableScan({}, t2, nullptr);
    expected_child_nodes = {join_scan.get(), table2_scan2.get()};
    auto join_scan2 = MakeResolvedJoinScan(
        {} /* column_list */, ResolvedJoinScan::INNER, std::move(join_scan),
        std::move(table2_scan2), nullptr /* join_condition */);

    // Check that grandchildren aren't returned. Also, check that the vector is
    // cleared before the children are added.
    child_nodes = {nullptr};
    join_scan2->GetChildNodes(&child_nodes);
    EXPECT_THAT(child_nodes, UnorderedElementsAreArray(expected_child_nodes));
  }

  // Children in a vector.
  {
    auto input_item1 =
        MakeResolvedSetOperationItem(MakeResolvedTableScan({}, t1, nullptr));
    auto input_item2 =
        MakeResolvedSetOperationItem(MakeResolvedTableScan({}, t2, nullptr));
    std::vector<const ResolvedNode*> expected_child_nodes = {input_item1.get(),
                                                             input_item2.get()};
    auto union_all = MakeResolvedSetOperationScan(
        {} /* column_list */, ResolvedSetOperationScan::UNION_ALL,
        MakeNodeVector(std::move(input_item1), std::move(input_item2)));
    std::vector<const ResolvedNode*> child_nodes;
    union_all->GetChildNodes(&child_nodes);
    EXPECT_THAT(child_nodes, UnorderedElementsAreArray(expected_child_nodes));
  }
}

TEST_F(ResolvedASTTest, GetTreeDepth) {
  // Leaf node.
  {
    auto table1_scan = MakeResolvedTableScan({}, t1, nullptr);
    EXPECT_EQ(table1_scan->GetTreeDepth(), 1);
  }

  // Node with one child
  {
    auto table1_scan = MakeResolvedTableScan({}, t1, nullptr);
    auto filter_scan = MakeResolvedFilterScan({}, std::move(table1_scan),
                                              nullptr /* filter_expr */);
    EXPECT_EQ(filter_scan->GetTreeDepth(), 2);
  }

  // Multiple children - first child is deeper.
  {
    auto table1_scan = MakeResolvedTableScan({}, t1, nullptr);
    auto table2_scan = MakeResolvedTableScan({}, t2, nullptr);
    auto filter_scan = MakeResolvedFilterScan({}, std::move(table2_scan),
                                              nullptr /* filter_expr */);
    auto join_scan = MakeResolvedJoinScan(
        {} /* column_list */, ResolvedJoinScan::INNER, std::move(filter_scan),
        std::move(table1_scan), nullptr /* join_condition */);
    EXPECT_EQ(join_scan->GetTreeDepth(), 3);
  }

  // Multiple children - second child is deeper.
  {
    auto table1_scan = MakeResolvedTableScan({}, t1, nullptr);
    auto table2_scan = MakeResolvedTableScan({}, t2, nullptr);
    auto filter_scan = MakeResolvedFilterScan({}, std::move(table2_scan),
                                              nullptr /* filter_expr */);
    auto join_scan = MakeResolvedJoinScan(
        {} /* column_list */, ResolvedJoinScan::INNER, std::move(table1_scan),
        std::move(filter_scan), nullptr /* join_condition */);
    EXPECT_EQ(join_scan->GetTreeDepth(), 3);
  }

  // Join tree - join condition is deeper than both scan trees.
  {
    TypeFactory type_factory;
    const FunctionSignature signature1(
        {type_factory.get_bool(),
         {type_factory.get_bool(), type_factory.get_bool()},
         nullptr /* context */});
    std::unique_ptr<const Function> function1(
        new Function("fn_and", "group", Function::SCALAR, {signature1}));
    const FunctionSignature signature2(
        {type_factory.get_bool(),
         {type_factory.get_int32(), type_factory.get_int64()},
         nullptr /* context */});
    std::unique_ptr<const Function> function2(
        new Function("fn_equals", "group", Function::SCALAR, {signature2}));

    auto table1_scan = MakeResolvedTableScan({}, t1, nullptr);
    auto table2_scan = MakeResolvedTableScan({}, t2, nullptr);
    auto filter_scan = MakeResolvedFilterScan({}, std::move(table2_scan),
                                              nullptr /* filter_expr */);
    const ResolvedColumn c1 = MakeColumn();
    auto func_comparison = MakeResolvedFunctionCall(
        signature2.result_type().type(), function2.get(), signature2,
        MakeNodeVector(
            MakeResolvedColumnRef(c1.type(), c1, false /* is_correlated */),
            MakeIntLiteral(6)),
        ResolvedFunctionCallBase::DEFAULT_ERROR_MODE);
    auto func_and = MakeResolvedFunctionCall(
        signature1.result_type().type(), function1.get(), signature1,
        MakeNodeVector(
            std::move(func_comparison),
            MakeResolvedLiteral(type_factory.get_bool(), Value::Bool(true))),
        ResolvedFunctionCallBase::DEFAULT_ERROR_MODE);
    auto join_scan = MakeResolvedJoinScan(
        {} /* column_list */, ResolvedJoinScan::INNER, std::move(filter_scan),
        std::move(table1_scan), std::move(func_and));
    EXPECT_EQ(join_scan->GetTreeDepth(), 4);
  }
}

TEST_F(ResolvedASTTest, GetDescendantsWithKinds) {
  // Build up a tree that looks like
  //   u1:SetOperation
  //     j1:Join
  //       s1:TableScan
  //       j2:Join
  //         s2:TableScan
  //         s3:TableScan
  //     s4:TableScan
  auto s1_uptr = MakeResolvedTableScan({}, t1, nullptr);
  auto s2_uptr = MakeResolvedTableScan({}, t1, nullptr);
  auto s3_uptr = MakeResolvedTableScan({}, t1, nullptr);
  auto s4_uptr = MakeResolvedTableScan({}, t1, nullptr);
  const ResolvedTableScan* s1 = s1_uptr.get();
  const ResolvedTableScan* s2 = s2_uptr.get();
  const ResolvedTableScan* s3 = s3_uptr.get();
  const ResolvedTableScan* s4 = s4_uptr.get();
  auto join2 = MakeResolvedJoinScan(
      {} /* column_list */, ResolvedJoinScan::INNER, std::move(s2_uptr),
      std::move(s3_uptr), nullptr /* join_condition */);
  auto join1 = MakeResolvedJoinScan(
      {} /* column_list */, ResolvedJoinScan::INNER, std::move(s1_uptr),
      std::move(join2), nullptr /* join_condition */);
  const ResolvedJoinScan* j1 = join1.get();
  auto u1 = MakeResolvedSetOperationScan(
      {} /* column_list */, ResolvedSetOperationScan::UNION_ALL,
      MakeNodeVector(MakeResolvedSetOperationItem(std::move(join1)),
                     MakeResolvedSetOperationItem(std::move(s4_uptr))));

  ABSL_LOG(INFO) << "Built tree:\n" << u1->DebugString();

  std::vector<const ResolvedNode*> found_nodes;

  // Empty set of node kinds.
  u1->GetDescendantsWithKinds({}, &found_nodes);
  EXPECT_THAT(found_nodes, ElementsAre());

  // Node kind that doesn't exist.
  u1->GetDescendantsWithKinds({RESOLVED_PROJECT_SCAN}, &found_nodes);
  EXPECT_THAT(found_nodes, ElementsAre());

  // Look up the root node.
  u1->GetDescendantsWithKinds({RESOLVED_SET_OPERATION_SCAN}, &found_nodes);
  EXPECT_THAT(found_nodes, ElementsAre(u1.get()));

  // Look up table scans.
  u1->GetDescendantsWithKinds({RESOLVED_TABLE_SCAN}, &found_nodes);

  EXPECT_THAT(found_nodes, UnorderedElementsAre(s4, s1, s2, s3));

  // Look up join.  We get the outermost join but not the one underneath it.
  u1->GetDescendantsWithKinds({RESOLVED_JOIN_SCAN}, &found_nodes);
  EXPECT_THAT(found_nodes, ElementsAre(j1));

  // Look up joins and table scans.  We get the outermost join and the
  // one table scan that isn't under it.
  u1->GetDescendantsWithKinds(
      {RESOLVED_JOIN_SCAN, RESOLVED_TABLE_SCAN, RESOLVED_LITERAL},
      &found_nodes);
  EXPECT_THAT(found_nodes, ElementsAre(j1, s4));
}

TEST_F(ResolvedASTTest, IsSuperclassType) {
  std::unique_ptr<const ResolvedNode> join = MakeJoin();
  EXPECT_TRUE(join->IsScan());
  EXPECT_FALSE(join->IsExpression());
  EXPECT_FALSE(join->IsStatement());

  std::unique_ptr<const ResolvedNode> literal = MakeIntLiteral(6);
  EXPECT_FALSE(literal->IsScan());
  EXPECT_TRUE(literal->IsExpression());
  EXPECT_FALSE(literal->IsStatement());

  auto commit = MakeResolvedCommitStmt();
  EXPECT_FALSE(commit->IsScan());
  EXPECT_FALSE(commit->IsExpression());
  EXPECT_TRUE(commit->IsStatement());
}

TEST_F(ResolvedASTTest, GetDescendantsSatisfying) {
  // Build up a tree that looks like
  //   q1:QueryStatement
  //     p1:Project
  //       e1:Literal
  //       e2:Literal
  //         s1:TableScan
  auto s1_uptr = MakeResolvedTableScan({} /* column_list */, t1, nullptr);
  std::unique_ptr<const ResolvedLiteral> e1_uptr = MakeIntLiteral(6);
  std::unique_ptr<const ResolvedLiteral> e2_uptr = MakeIntLiteral(7);
  const ResolvedTableScan* s1 = s1_uptr.get();
  const ResolvedLiteral* e1 = e1_uptr.get();
  const ResolvedLiteral* e2 = e2_uptr.get();
  auto p1_uptr = MakeResolvedProjectScan(
      {} /* column_list */,
      MakeNodeVector(
          MakeResolvedComputedColumn(MakeColumn(), std::move(e1_uptr)),
          MakeResolvedComputedColumn(MakeColumn(), std::move(e2_uptr))),
      std::move(s1_uptr));
  const ResolvedProjectScan* p1 = p1_uptr.get();
  auto q1_uptr =
      MakeResolvedQueryStmt({} /* output_column_list */,
                            false /* is_value_table */, std::move(p1_uptr));
  const ResolvedQueryStmt* q1 = q1_uptr.get();

  ABSL_LOG(INFO) << "Built tree:\n" << q1->DebugString();

  std::vector<const ResolvedNode*> found_nodes;

  // Empty set of node kinds (by pointing below the initial Statement).
  s1->GetDescendantsSatisfying(&ResolvedNode::IsStatement, &found_nodes);
  EXPECT_THAT(found_nodes, ElementsAre());

  q1->GetDescendantsSatisfying(&ResolvedNode::IsStatement, &found_nodes);
  EXPECT_THAT(found_nodes, ElementsAre(q1));

  q1->GetDescendantsSatisfying(&ResolvedNode::IsExpression, &found_nodes);
  EXPECT_THAT(found_nodes, ElementsAre(e1, e2));

  q1->GetDescendantsSatisfying(&ResolvedNode::IsScan, &found_nodes);
  EXPECT_THAT(found_nodes, ElementsAre(p1, s1));
}

TEST_F(ResolvedASTTest, TableSerialization) {
  SimpleColumn column("bar", "baz", types::Int64Type());
  SimpleTable table("bar", {&column}, false, 123);
  SimpleCatalog catalog("foo");
  catalog.AddTable(&table);
  TypeFactory factory;
  std::unique_ptr<const AnalyzerOutput> output;
  AnalyzerOptions options = AnalyzerOptions();
  options.set_record_parse_locations(true);
  GOOGLESQL_ASSERT_OK(googlesql::AnalyzeStatement("select baz from bar;", options,
                                        &catalog, &factory, &output));
  AnyResolvedStatementProto proto;
  FileDescriptorSetMap map;
  GOOGLESQL_ASSERT_OK(output->resolved_statement()->SaveTo(&map, &proto));
  EXPECT_TRUE(proto.has_resolved_query_stmt_node());
  const ResolvedQueryStmtProto& query = proto.resolved_query_stmt_node();
  EXPECT_TRUE(query.query().has_resolved_project_scan_node());
  const ResolvedProjectScanProto& scan =
      query.query().resolved_project_scan_node();
  EXPECT_TRUE(scan.input_scan().has_resolved_table_scan_node());
  const ResolvedTableScanProto& table_scan =
      scan.input_scan().resolved_table_scan_node();
  const TableRefProto& table_ref = table_scan.table();
  EXPECT_EQ("bar", table_ref.name());
  EXPECT_EQ(123, table_ref.serialization_id());
  EXPECT_TRUE(table_scan.has_parent());
  EXPECT_TRUE(table_scan.parent().has_parent());
  const ResolvedNodeProto& resolved_node = table_scan.parent().parent();
  EXPECT_TRUE(resolved_node.has_parse_location_range());
  const ParseLocationRangeProto& parse_location_range =
      resolved_node.parse_location_range();
  EXPECT_EQ(16, parse_location_range.start());
  EXPECT_EQ(19, parse_location_range.end());
}

TEST_F(ResolvedASTTest, TableDeserialization) {
  SimpleColumn column("bar" /* table_name */, "baz" /* name */,
                      types::Int64Type());
  SimpleTable table("bar", {&column}, false /* takes_ownership */,
                    123 /* id */);
  SimpleCatalog catalog("foo");
  catalog.AddTable(&table);
  TypeFactory factory;
  std::unique_ptr<const AnalyzerOutput> output;
  GOOGLESQL_ASSERT_OK(googlesql::AnalyzeStatement(
      "select baz from bar;", AnalyzerOptions(), &catalog, &factory, &output));
  AnyResolvedNodeProto proto;
  FileDescriptorSetMap map;
  GOOGLESQL_ASSERT_OK(output->resolved_statement()->SaveTo(&map, &proto));

  std::vector<const google::protobuf::DescriptorPool*> pools;
  for (const auto& entry : map) pools.push_back(entry.first);

  IdStringPool string_pool;
  ResolvedNode::RestoreParams restore_params(pools, &catalog, &factory,
                                             &string_pool);

  auto ast =
      std::move(ResolvedNode::RestoreFrom(proto, restore_params).value());
  const auto* query = ast->GetAs<ResolvedQueryStmt>();
  const auto* project = query->query()->GetAs<ResolvedProjectScan>();
  const auto* table_scan = project->input_scan()->GetAs<ResolvedTableScan>();
  const Table* got_table = table_scan->table();

  EXPECT_EQ(table.Name(), got_table->Name());
  EXPECT_EQ(table.GetSerializationId(), got_table->GetSerializationId());
}

TEST_F(ResolvedASTTest, FieldDescriptorSerialization) {
  SimpleCatalog catalog("foo");
  TypeFactory factory;
  const Type* type;
  GOOGLESQL_ASSERT_OK(factory.MakeProtoType(ValueProto::descriptor(), &type));
  catalog.AddType("GoogleSQLValueProto", type);
  std::unique_ptr<const AnalyzerOutput> output;
  GOOGLESQL_ASSERT_OK(googlesql::AnalyzeStatement(
      "select as GoogleSQLValueProto 1 int32_value;", AnalyzerOptions(),
      &catalog, &factory, &output));
  AnyResolvedStatementProto proto;
  FileDescriptorSetMap map;
  GOOGLESQL_ASSERT_OK(output->resolved_statement()->SaveTo(&map, &proto));
  EXPECT_TRUE(proto.has_resolved_query_stmt_node());
  const ResolvedQueryStmtProto& query = proto.resolved_query_stmt_node();
  EXPECT_TRUE(query.query().has_resolved_project_scan_node());
  const ResolvedProjectScanProto& scan =
      query.query().resolved_project_scan_node();
  ASSERT_EQ(1, scan.expr_list_size());
  EXPECT_TRUE(scan.expr_list(0).expr().has_resolved_make_proto_node());
  const ResolvedMakeProtoProto& make_proto_node =
      scan.expr_list(0).expr().resolved_make_proto_node();
  ASSERT_EQ(1, make_proto_node.field_list_size());
  const FieldDescriptorRefProto& field_ref =
      make_proto_node.field_list(0).field_descriptor();
  EXPECT_EQ("googlesql.ValueProto", field_ref.containing_proto().proto_name());
  const google::protobuf::FieldDescriptor* field =
      ValueProto::descriptor()->FindFieldByNumber(field_ref.number());
  EXPECT_EQ("int32_value", field->name());
}

TEST_F(ResolvedASTTest, FieldDescriptorDeserialization) {
  SimpleCatalog catalog("foo");
  TypeFactory factory;
  const Type* type;
  GOOGLESQL_ASSERT_OK(factory.MakeProtoType(ValueProto::descriptor(), &type));
  catalog.AddType("GoogleSQLValueProto", type);
  std::unique_ptr<const AnalyzerOutput> output;
  GOOGLESQL_ASSERT_OK(googlesql::AnalyzeStatement(
      "select as GoogleSQLValueProto 1 int32_value;", AnalyzerOptions(),
      &catalog, &factory, &output));

  AnyResolvedNodeProto proto;
  FileDescriptorSetMap map;
  GOOGLESQL_ASSERT_OK(output->resolved_statement()->SaveTo(&map, &proto));

  std::vector<const google::protobuf::DescriptorPool*> pools;
  for (const auto& entry : map) pools.push_back(entry.first);

  IdStringPool string_pool;
  ResolvedNode::RestoreParams restore_params(pools, &catalog, &factory,
                                             &string_pool);

  auto ast =
      std::move(ResolvedNode::RestoreFrom(proto, restore_params).value());
  const auto* query_stmt = ast->GetAs<ResolvedQueryStmt>();
  const auto* project = query_stmt->query()->GetAs<ResolvedProjectScan>();
  const auto* make_proto =
      project->expr_list()[0]->expr()->GetAs<ResolvedMakeProto>();
  const auto& field_ref = make_proto->field_list()[0];

  EXPECT_EQ("int32_value", field_ref->field_descriptor()->name());
}

TEST_F(ResolvedASTTest, NestedCatalogSerialization) {
  SimpleCatalog catalog("foo");
  SimpleCatalog child_catalog("bar");
  catalog.AddCatalog("bar", &child_catalog);

  TypeFactory factory;
  SimpleTable table("bar.Table",
                    {new SimpleColumn("bar.Table", "col", factory.get_int32())},
                    true /* takes_ownership */, 123 /* id */);
  child_catalog.AddTable("Table", &table);

  std::unique_ptr<const AnalyzerOutput> output;
  GOOGLESQL_ASSERT_OK(googlesql::AnalyzeStatement("select * from bar.Table;",
                                        AnalyzerOptions(), &catalog, &factory,
                                        &output));

  AnyResolvedNodeProto proto;
  FileDescriptorSetMap map;
  GOOGLESQL_ASSERT_OK(output->resolved_statement()->SaveTo(&map, &proto));

  std::vector<const google::protobuf::DescriptorPool*> pools;
  for (const auto& entry : map) pools.push_back(entry.first);

  IdStringPool string_pool;
  ResolvedNode::RestoreParams restore_params(pools, &catalog, &factory,
                                             &string_pool);

  auto ast =
      std::move(ResolvedNode::RestoreFrom(proto, restore_params).value());
  const auto* query_stmt = ast->GetAs<ResolvedQueryStmt>();
  const auto* table_scan = query_stmt->query()
                               ->GetAs<ResolvedProjectScan>()
                               ->input_scan()
                               ->GetAs<ResolvedTableScan>();
  EXPECT_EQ(table_scan->table()->GetSerializationId(),
            table.GetSerializationId());
}

TEST_F(ResolvedASTTest, ConstantSerialization) {
  std::unique_ptr<SimpleConstant> constant;
  GOOGLESQL_CHECK_OK(SimpleConstant::Create({"bar", "baz"}, Value::Int64(3323),
                                  &constant));
  const std::string constant_fullname = constant->FullName();
  SimpleCatalog outer_catalog("foo");
  SimpleCatalog inner_catalog("bar");
  outer_catalog.AddCatalog(&inner_catalog);
  inner_catalog.AddOwnedConstant(std::move(constant));
  TypeFactory factory;
  std::unique_ptr<const AnalyzerOutput> output;
  GOOGLESQL_ASSERT_OK(googlesql::AnalyzeStatement("select bar.baz;", AnalyzerOptions(),
                                        &outer_catalog, &factory, &output));
  AnyResolvedStatementProto proto;
  FileDescriptorSetMap map;
  GOOGLESQL_ASSERT_OK(output->resolved_statement()->SaveTo(&map, &proto));

  ASSERT_TRUE(proto.has_resolved_query_stmt_node()) << absl::StrCat(proto);
  const ResolvedQueryStmtProto& query = proto.resolved_query_stmt_node();
  ASSERT_TRUE(query.query().has_resolved_project_scan_node())
      << absl::StrCat(query);
  const ResolvedProjectScanProto& scan =
      query.query().resolved_project_scan_node();
  ASSERT_EQ(scan.expr_list_size(), 1) << absl::StrCat(scan);
  const ResolvedComputedColumnProto& column = scan.expr_list(0);
  ASSERT_TRUE(column.has_expr()) << absl::StrCat(column);
  const ResolvedConstantProto& resolved_constant =
      column.expr().resolved_constant_node();
  ASSERT_TRUE(resolved_constant.has_constant())
      << absl::StrCat(resolved_constant);
  const ConstantRefProto& constant_ref = resolved_constant.constant();
  EXPECT_EQ(constant_ref.name(), constant_fullname);
}

TEST_F(ResolvedASTTest, ConstantDeserialization) {
  std::unique_ptr<SimpleConstant> constant;
  GOOGLESQL_CHECK_OK(SimpleConstant::Create({"bar", "baz"}, Value::Int64(3323),
                                  &constant));
  const std::string constant_fullname = constant->FullName();
  SimpleCatalog outer_catalog("foo");
  SimpleCatalog inner_catalog("bar");
  outer_catalog.AddCatalog(&inner_catalog);
  inner_catalog.AddOwnedConstant(std::move(constant));
  TypeFactory factory;
  std::unique_ptr<const AnalyzerOutput> output;
  GOOGLESQL_ASSERT_OK(googlesql::AnalyzeStatement("select bar.baz;", AnalyzerOptions(),
                                        &outer_catalog, &factory, &output));
  AnyResolvedNodeProto proto;
  FileDescriptorSetMap map;
  GOOGLESQL_ASSERT_OK(output->resolved_statement()->SaveTo(&map, &proto));

  std::vector<const google::protobuf::DescriptorPool*> pools;
  for (const auto& entry : map) pools.push_back(entry.first);

  IdStringPool string_pool;
  ResolvedNode::RestoreParams restore_params(pools, &outer_catalog, &factory,
                                             &string_pool);

  auto ast =
      std::move(ResolvedNode::RestoreFrom(proto, restore_params).value());
  const auto* const query = ast->GetAs<ResolvedQueryStmt>();
  const auto* const project = query->query()->GetAs<ResolvedProjectScan>();
  ASSERT_EQ(project->expr_list_size(), 1) << project->DebugString();
  const ResolvedComputedColumn* const column = project->expr_list(0);
  auto* const resolved_constant =
      dynamic_cast<const ResolvedConstant*>(column->expr());
  ASSERT_THAT(resolved_constant, NotNull()) << column->DebugString();
  const Constant* const deserialized_constant = resolved_constant->constant();
  ASSERT_THAT(deserialized_constant, NotNull())
      << resolved_constant->DebugString();
  EXPECT_EQ(deserialized_constant->FullName(), constant_fullname);
}

// TODO: Remove this test case once we have analyzer tests that
// actually produce the field.
TEST_F(ResolvedASTTest, ResolvedFunctionArgumentAlias) {
  // Checks the default values.
  {
    std::unique_ptr<ResolvedFunctionArgument> function_argument =
        MakeResolvedFunctionArgument();
    EXPECT_EQ(function_argument->argument_alias(), "");
  }
  // The alias field is correctly populated and printed.
  {
    std::unique_ptr<ResolvedFunctionArgument> function_argument =
        MakeResolvedFunctionArgument();
    function_argument->set_argument_alias("alias");
    EXPECT_EQ(function_argument->DebugString(),
              "FunctionArgument(argument_alias=\"alias\")\n");
  }
}

}  // namespace googlesql
