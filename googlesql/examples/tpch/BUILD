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
    name = "execute_all_queries_test",
    args = ["--catalog=tpch"],
    sql_file = ":all_queries.sql",
)

execute_query_test(
    name = "execute_all_pipe_queries_test",
    args = ["--catalog=tpch"],
    sql_file = ":all_pipe_queries.sql",
)

execute_query_test(
    name = "execute_all_queries_with_graph_test",
    args = ["--catalog=tpch_graph"],
    sql_file = ":all_queries.sql",
)

execute_query_test(
    name = "execute_describe_test",
    args = ["--catalog=tpch"],
    sql_file = ":describe.sql",
)

execute_query_test(
    name = "execute_describe_with_graph_test",
    args = ["--catalog=tpch_graph"],
    sql_file = ":describe.sql",
)
