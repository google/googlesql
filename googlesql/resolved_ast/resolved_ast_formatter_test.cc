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

#include "googlesql/resolved_ast/resolved_ast_formatter.h"

#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "googlesql/base/testing/status_matchers.h"
#include "googlesql/common/utf_util.h"
#include "googlesql/public/id_string.h"
#include "googlesql/public/types/type.h"
#include "googlesql/public/types/type_factory.h"
#include "googlesql/public/value.h"
#include "googlesql/resolved_ast/resolved_ast.h"
#include "googlesql/resolved_ast/resolved_column.h"
#include "googlesql/resolved_ast/resolved_node.h"
#include "googlesql/resolved_ast/resolved_node_kind.pb.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "absl/strings/match.h"

namespace googlesql {
namespace {

using ::testing::ElementsAre;

TEST(ResolvedASTFormatterTest, TestNodeViewProperties) {
  TypeFactory type_factory;
  std::unique_ptr<ResolvedLiteral> literal =
      MakeResolvedLiteral(type_factory.get_int64(), Value::Int64(42));

  ResolvedASTNodeView node_view(literal.get());

  EXPECT_FALSE(node_view.is_null());
  EXPECT_EQ(node_view.node(), literal.get());
  EXPECT_EQ(node_view.node_kind(), RESOLVED_LITERAL);
  EXPECT_EQ(node_view.node_name(), "Literal");
  EXPECT_EQ(node_view.pipe_input_scan(), nullptr);
}

TEST(ResolvedASTFormatterTest, TestFieldViewIteration) {
  TypeFactory type_factory;
  std::unique_ptr<ResolvedLiteral> literal =
      MakeResolvedLiteral(type_factory.get_int64(), Value::Int64(42));

  ResolvedASTNodeView node_view(literal.get());

  std::vector<std::string> field_names;
  std::vector<std::string> field_values;

  for (int i = 0; i < node_view.num_fields(); i++) {
    const ResolvedASTFieldView field = node_view.field(i);
    field_names.push_back(std::string(field.name()));
    field_values.push_back(std::string(field.scalar_value()));
    EXPECT_TRUE(field.is_scalar());
  }

  EXPECT_THAT(field_names, ElementsAre("type", "value"));
  EXPECT_THAT(field_values, ElementsAre("INT64", "42"));
}

TEST(ResolvedASTFormatterTest, TestGetLinearPipelineChain) {
  TypeFactory type_factory;
  ResolvedColumn col(1, IdString::MakeGlobal("t"), IdString::MakeGlobal("c"),
                     type_factory.get_int64());

  std::unique_ptr<ResolvedTableScan> table_scan = MakeResolvedTableScan(
      {col}, /*table=*/nullptr, /*for_system_time_expr=*/nullptr);
  std::unique_ptr<ResolvedFilterScan> filter_scan = MakeResolvedFilterScan(
      {col}, std::move(table_scan),
      MakeResolvedLiteral(type_factory.get_bool(), Value::Bool(true)));

  ResolvedASTNodeView filter_view(filter_scan.get());
  std::vector<ResolvedASTNodeView> chain = filter_view.GetLinearPipelineChain();

  ASSERT_EQ(chain.size(), 2);
  EXPECT_EQ(chain[0].node_kind(), RESOLVED_TABLE_SCAN);
  EXPECT_EQ(chain[1].node_kind(), RESOLVED_FILTER_SCAN);
}

TEST(ResolvedASTFormatterTest, TestLinearModeRendering) {
  TypeFactory type_factory;
  ResolvedColumn col(1, IdString::MakeGlobal("t"), IdString::MakeGlobal("c"),
                     type_factory.get_int64());

  std::unique_ptr<ResolvedTableScan> table_scan = MakeResolvedTableScan(
      {col}, /*table=*/nullptr, /*for_system_time_expr=*/nullptr);
  std::unique_ptr<ResolvedFilterScan> filter_scan = MakeResolvedFilterScan(
      {col}, std::move(table_scan),
      MakeResolvedLiteral(type_factory.get_bool(), Value::Bool(true)));

  ResolvedNode::DebugStringConfig config{.linear_mode = true};
  std::string formatted = RenderResolvedAST(filter_scan.get(), config);

  EXPECT_TRUE(absl::StrContains(formatted, "|> FilterScan"));
}

TEST(ResolvedASTFormatterTest, TestOmitPipeInputScanField) {
  TypeFactory type_factory;
  ResolvedColumn col(1, IdString::MakeGlobal("t"), IdString::MakeGlobal("c"),
                     type_factory.get_int64());

  std::unique_ptr<ResolvedTableScan> table_scan = MakeResolvedTableScan(
      {col}, /*table=*/nullptr, /*for_system_time_expr=*/nullptr);
  std::unique_ptr<ResolvedFilterScan> filter_scan = MakeResolvedFilterScan(
      {col}, std::move(table_scan),
      MakeResolvedLiteral(type_factory.get_bool(), Value::Bool(true)));

  // Case 1: omit_pipe_input_scan_field = true (default)
  {
    ResolvedNode::DebugStringConfig config{.linear_mode = true,
                                           .omit_pipe_input_scan_field = true};
    std::string formatted = RenderResolvedAST(filter_scan.get(), config);

    EXPECT_FALSE(absl::StrContains(formatted, "input_scan="));
    EXPECT_FALSE(absl::StrContains(formatted, "<pipe_input>"));
  }

  // Case 2: omit_pipe_input_scan_field = false
  {
    ResolvedNode::DebugStringConfig config{.linear_mode = true,
                                           .omit_pipe_input_scan_field = false};
    std::string formatted = RenderResolvedAST(filter_scan.get(), config);

    EXPECT_TRUE(absl::StrContains(formatted, "input_scan=<pipe_input>"));
  }
}

TEST(ResolvedASTFormatterTest, TestNullHandling) {
  ResolvedASTNodeView null_node_view(nullptr);

  EXPECT_TRUE(null_node_view.is_null());
  EXPECT_EQ(null_node_view.node(), nullptr);
  EXPECT_EQ(null_node_view.node_kind(), std::nullopt);
  EXPECT_EQ(null_node_view.node_name(), "<nullptr AST node>");
  EXPECT_EQ(null_node_view.pipe_input_scan(), nullptr);

  EXPECT_EQ(null_node_view.num_fields(), 0);
}

std::unique_ptr<ResolvedFilterScan> MakeSampleFilterScan(
    TypeFactory* type_factory) {
  ResolvedColumn col(1, IdString::MakeGlobal("t"), IdString::MakeGlobal("c"),
                     type_factory->get_int64());
  std::unique_ptr<ResolvedTableScan> table_scan = MakeResolvedTableScan(
      {col}, /*table=*/nullptr, /*for_system_time_expr=*/nullptr);
  return MakeResolvedFilterScan(
      {col}, std::move(table_scan),
      MakeResolvedLiteral(type_factory->get_bool(), Value::Bool(true)));
}

TEST(ResolvedASTFormatterTest, MaxLengthDefaultAndLarger) {
  TypeFactory type_factory;
  std::unique_ptr<ResolvedFilterScan> filter_scan =
      MakeSampleFilterScan(&type_factory);

  const std::string full = filter_scan->DebugString();
  ASSERT_FALSE(full.empty());
  ASSERT_FALSE(absl::EndsWith(full, "..."));

  EXPECT_EQ(filter_scan->DebugString({}), full);
  EXPECT_EQ(
      RenderResolvedAST(filter_scan.get(), ResolvedNode::DebugStringConfig{}),
      full);
  EXPECT_EQ(filter_scan->DebugString(
                {.max_length = static_cast<int32_t>(full.size())}),
            full);
  EXPECT_EQ(filter_scan->DebugString(
                {.max_length = static_cast<int32_t>(full.size()) + 100}),
            full);
}

TEST(ResolvedASTFormatterTest, MaxLengthTruncatesWithEllipsis) {
  TypeFactory type_factory;
  std::unique_ptr<ResolvedFilterScan> filter_scan =
      MakeSampleFilterScan(&type_factory);

  const std::string full = filter_scan->DebugString();
  ASSERT_GT(full.size(), 25);

  for (int32_t max_length : {static_cast<int32_t>(full.size()) - 1, 20, 4}) {
    const std::string result =
        filter_scan->DebugString({.max_length = max_length});
    EXPECT_EQ(result, PrettyTruncateUTF8(full, max_length));
    EXPECT_EQ(result.size(), max_length);
    EXPECT_TRUE(absl::EndsWith(result, "..."));
  }
}

}  // namespace
}  // namespace googlesql
