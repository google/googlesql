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
#   GoogleSQL ResolvedAST Java library.

load("@rules_java//java:defs.bzl", "java_library")
load("//googlesql/resolved_ast:build_rules.bzl", "gen_resolved_ast_files")

package(
    default_visibility = ["//visibility:public"],
)

gen_resolved_ast_files(
    name = "run_gen_resolved_ast_java",
    srcs = [
        "ResolvedNodes.java.template",
        "RewritingVisitor.java.template",
    ],
    outs = [
        "ResolvedNodes.java",
        "RewritingVisitor.java",
    ],
)

java_library(
    name = "resolvedast",
    srcs = [
        "AbstractDeserializationHelper.java",
        "DebugStrings.java",
        "DeserializationHelper.java",
        "ResolvedColumn.java",
        "ResolvedNode.java",
        "ResolvedNodes.java",
        "RewritingVisitor.java",
    ],
    deps = [
        "//googlesql/proto:function_java_proto",
        "//googlesql/public:annotation_java_proto",
        "//googlesql/public:type_modifiers_java_proto",
        "//googlesql/public:type_parameters_java_proto",
        "//googlesql/public:type_proto_java_proto",
        "//googlesql/public/proto:type_annotation_java_proto",
        "//googlesql/resolved_ast:resolved_ast_enums_java_proto",
        "//googlesql/resolved_ast:resolved_ast_java_proto",
        "//googlesql/resolved_ast:resolved_node_kind_java_proto",
        "//googlesql/resolved_ast:serialization_java_proto",
        "//java/com/google/googlesql:client",
        "//java/com/google/googlesql:debug_printable_node",
        "//java/com/google/googlesql:property_graph",
        "//java/com/google/googlesql:types",
        "@com_google_protobuf//:protobuf_java",
        "@maven//:com_google_code_findbugs_jsr305",
        "@maven//:com_google_errorprone_error_prone_annotations",
        "@maven//:com_google_guava_guava",
    ],
)

java_library(
    name = "resolvedast_no_simple_catalog",
    srcs = [
        "AbstractDeserializationHelper.java",
        "DebugStrings.java",
        "ResolvedColumn.java",
        "ResolvedNode.java",
        "ResolvedNodes.java",
        "RewritingVisitor.java",
    ],
    deps = [
        "//googlesql/proto:function_java_proto",
        "//googlesql/public:annotation_java_proto",
        "//googlesql/public:type_modifiers_java_proto",
        "//googlesql/public:type_parameters_java_proto",
        "//googlesql/public:type_proto_java_proto",
        "//googlesql/public/proto:type_annotation_java_proto",
        "//googlesql/resolved_ast:resolved_ast_enums_java_proto",
        "//googlesql/resolved_ast:resolved_ast_java_proto",
        "//googlesql/resolved_ast:resolved_node_kind_java_proto",
        "//googlesql/resolved_ast:serialization_java_proto",
        "//java/com/google/googlesql:catalog",
        "//java/com/google/googlesql:debug_printable_node",
        "//java/com/google/googlesql:property_graph",
        "//java/com/google/googlesql:types",
        "@com_google_protobuf//:protobuf_java",
        "@maven//:com_google_code_findbugs_jsr305",
        "@maven//:com_google_errorprone_error_prone_annotations",
        "@maven//:com_google_guava_guava",
    ],
)
