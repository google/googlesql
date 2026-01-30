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

# Description:
#   GoogleSQL AST Java library.

load("@rules_java//java:defs.bzl", "java_library")

package(
    default_visibility = ["//googlesql/base:googlesql_implementation"],
)

genrule(
    name = "gen_java",
    srcs = ["ASTNodes.java.template"],
    outs = ["ASTNodes.java"],
    cmd = "$(location //googlesql/parser:gen_parse_tree) $(OUTS) $(SRCS)",
    tools = ["//googlesql/parser:gen_parse_tree"],
)

genrule(
    name = "gen_parse_tree_visitor",
    srcs = ["ParseTreeVisitor.java.template"],
    outs = ["ParseTreeVisitor.java"],
    cmd = "$(location //googlesql/parser:gen_parse_tree) $(OUTS) $(SRCS)",
    tools = ["//googlesql/parser:gen_parse_tree"],
)

java_library(
    name = "parser",
    srcs = [
        "ASTNode.java",
        "ASTNodes.java",
        "ParseTreeVisitor.java",
    ],
    deps = [
        "//googlesql/parser:ast_enums_java_proto",
        "//googlesql/parser:ast_java_proto",
        "//googlesql/public:type_proto_java_proto",
        "//java/com/google/googlesql:client",
        "//java/com/google/googlesql:debug_printable_node",
        "@maven//:com_google_guava_guava",
        "@maven//:javax_annotation_javax_annotation_api",
    ],
)
