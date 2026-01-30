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

load("@rules_cc//cc:cc_library.bzl", "cc_library")

package(default_visibility = ["//googlesql/base:googlesql_implementation"])

cc_library(
    name = "collation",
    srcs = ["collation.cc"],
    hdrs = ["collation.h"],
    deps = [
        ":default_annotation",
        "//googlesql/base:ret_check",
        "//googlesql/base:status",
        "//googlesql/common:errors",
        "//googlesql/parser:parse_tree",
        "//googlesql/public:builtin_function_cc_proto",
        "//googlesql/public:function",
        "//googlesql/public:function_cc_proto",
        "//googlesql/public:type",
        "//googlesql/public/types",
        "//googlesql/resolved_ast",
        "//googlesql/resolved_ast:resolved_node_kind_cc_proto",
        "@com_google_absl//absl/status",
        "@com_google_absl//absl/status:statusor",
        "@com_google_absl//absl/strings",
    ],
)

cc_library(
    name = "default_annotation",
    srcs = ["default_annotation_spec.cc"],
    hdrs = ["default_annotation_spec.h"],
    deps = [
        "//googlesql/base:ret_check",
        "//googlesql/base:status",
        "//googlesql/common:errors",
        "//googlesql/common:type_and_argument_kind_visitor",
        "//googlesql/public:builtin_function_cc_proto",
        "//googlesql/public:function_cc_proto",
        "//googlesql/public:function_headers",
        "//googlesql/public/types",
        "//googlesql/resolved_ast",
        "@com_google_absl//absl/container:flat_hash_map",
        "@com_google_absl//absl/status",
        "@com_google_absl//absl/status:statusor",
        "@com_google_absl//absl/strings",
        "@com_google_absl//absl/types:span",
    ],
)
