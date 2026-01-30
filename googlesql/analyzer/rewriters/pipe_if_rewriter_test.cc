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

#include "googlesql/analyzer/rewriters/pipe_if_rewriter.h"

#include <memory>
#include <utility>
#include <vector>

#include "googlesql/base/testing/status_matchers.h"
#include "googlesql/public/analyzer_options.h"
#include "googlesql/public/analyzer_output_properties.h"
#include "googlesql/public/id_string.h"
#include "googlesql/public/simple_catalog.h"
#include "googlesql/public/types/type.h"
#include "googlesql/public/types/type_factory.h"
#include "googlesql/resolved_ast/resolved_ast.h"
#include "googlesql/resolved_ast/resolved_column.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "absl/status/statusor.h"

namespace googlesql {

// This tests the code that adds ProjectScans in the PipeIf rewriter.  This is
// useful because the case where column_lists don't match never occurs with
// Resolved ASTs produced by the analyzer.
TEST(PipeIfRewriter, Rewrite) {
  IdStringPool pool;
  ResolvedColumn column1(1, pool.Make("t"), pool.Make("col1"),
                         types::Int64Type());
  ResolvedColumn column2(2, pool.Make("t"), pool.Make("col2"),
                         types::Int64Type());
  ResolvedColumn column3(3, pool.Make("t"), pool.Make("col3"),
                         types::Int64Type());

  ResolvedColumnList all_columns = {column1, column2, column3};
  ResolvedColumnList subset_columns = {column2};

  AnalyzerOptions analyzer_options;
  SimpleCatalog catalog("root");
  TypeFactory type_factory;
  AnalyzerOutputProperties properties;

  const Rewriter* rewriter = GetPipeIfRewriter();

  // Run twice - with unpruned and pruned column_lists on the IfScan.
  for (int iter = 0; iter < 2; ++iter) {
    auto if_case = MakeResolvedPipeIfCase(
        MakeResolvedLiteral(Value::Bool(true)), "(subpipeline)",
        MakeResolvedSubpipeline(MakeResolvedSubpipelineInputScan(all_columns)));

    std::vector<std::unique_ptr<const ResolvedPipeIfCase>> if_cases;
    if_cases.push_back(std::move(if_case));

    auto scan =
        MakeResolvedPipeIfScan(iter == 0 ? all_columns : subset_columns,
                               MakeResolvedSingleRowScan(all_columns),
                               /*selected_case=*/0, std::move(if_cases));

    // The difference in input trees is just in the outer column_list on
    // the ResolvedPipeIfScan.  The second one prunes it to one column.
    if (iter == 0) {
      EXPECT_EQ(R"X(PipeIfScan
+-column_list=t.[col1#1, col2#2, col3#3]
+-input_scan=
| +-SingleRowScan(column_list=t.[col1#1, col2#2, col3#3])
+-selected_case=0
+-if_case_list=
  +-PipeIfCase
    +-condition=
    | +-Literal(type=BOOL, value=true)
    +-subpipeline_sql="(subpipeline)"
    +-subpipeline=
      +-Subpipeline
        +-scan=
          +-SubpipelineInputScan(column_list=t.[col1#1, col2#2, col3#3])
)X",
                scan->DebugString());
    } else {
      EXPECT_EQ(R"X(PipeIfScan
+-column_list=[t.col2#2]
+-input_scan=
| +-SingleRowScan(column_list=t.[col1#1, col2#2, col3#3])
+-selected_case=0
+-if_case_list=
  +-PipeIfCase
    +-condition=
    | +-Literal(type=BOOL, value=true)
    +-subpipeline_sql="(subpipeline)"
    +-subpipeline=
      +-Subpipeline
        +-scan=
          +-SubpipelineInputScan(column_list=t.[col1#1, col2#2, col3#3])
)X",
                scan->DebugString());
    }

    auto result = rewriter->Rewrite(analyzer_options, std::move(scan), catalog,
                                    type_factory, properties);
    GOOGLESQL_ASSERT_OK(result);

    if (iter == 0) {
      // When the column_lists match (the normal case), the rewrite just
      // extracts the scan from the subpipeline.
      EXPECT_EQ("SingleRowScan(column_list=t.[col1#1, col2#2, col3#3])\n",
                result.value()->DebugString());
    } else {
      // When the column_lists don't match, the rewrite adds a ProjectScan
      // to produce the expected column_list.
      EXPECT_EQ(R"(ProjectScan
+-column_list=[t.col2#2]
+-input_scan=
  +-SingleRowScan(column_list=t.[col1#1, col2#2, col3#3])
)",
                result.value()->DebugString());
    }
  }
}

}  // namespace googlesql
