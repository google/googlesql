#
# Copyright 2019 Google LLC
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#     http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.
#

load("//googlesql/tools/execute_query:build_rules.bzl", "execute_query_test")

execute_query_test(
    name = "execute_pipe_examples_test",
    args = ["--catalog=tpch"],
    sql_file = "pipe_examples.sql",
)

execute_query_test(
    name = "execute_pipe_pivot_test",
    args = ["--catalog=tpch"],
    sql_file = "pipe_pivot.sql",
)

execute_query_test(
    name = "execute_walkthrough_7day_test",
    args = [
        "--catalog=tpch",
        # This test depends on REWRITE_INLINE_SQL_TVFS, which is marked in_development=true, and
        # thus may return incorrect results, and needs to be manually enabled.
        # TODO: b/202167428 - Remove arg once this rewriter is out of development.
        "--enabled_ast_rewrites=ALL_MINUS_DEV,+INLINE_SQL_TVFS",
    ],
    sql_file = "walkthrough_7day.sql",
)
