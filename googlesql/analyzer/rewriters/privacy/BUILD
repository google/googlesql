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

# Contains helper classes for privacy rewriters.
package(
    default_visibility = ["//googlesql/base:googlesql_implementation"],
)

cc_library(
    name = "privacy_utility",
    srcs = ["privacy_utility.cc"],
    hdrs = ["privacy_utility.h"],
    deps = [
        "//googlesql/analyzer:name_scope",
        "//googlesql/analyzer:resolver",
        "//googlesql/base:check",
        "//googlesql/base:ret_check",
        "//googlesql/base:status",
        "//googlesql/common:errors",
        "//googlesql/parser:parse_tree",
        "//googlesql/proto:anon_output_with_report_cc_proto",
        "//googlesql/proto:internal_error_location_cc_proto",
        "//googlesql/public:builtin_function_cc_proto",
        "//googlesql/public:catalog",
        "//googlesql/public:function",
        "//googlesql/public:function_cc_proto",
        "//googlesql/public:options_cc_proto",
        "//googlesql/public:type_cc_proto",
        "//googlesql/public/functions:differential_privacy_cc_proto",
        "//googlesql/resolved_ast",
        "//googlesql/resolved_ast:resolved_ast_enums_cc_proto",
        "//googlesql/resolved_ast:resolved_node_kind_cc_proto",
        "@com_google_absl//absl/memory",
        "@com_google_absl//absl/status",
        "@com_google_absl//absl/status:statusor",
        "@com_google_absl//absl/strings:string_view",
    ],
)

cc_library(
    name = "approx_count_distinct_utility",
    srcs = ["approx_count_distinct_utility.cc"],
    hdrs = ["approx_count_distinct_utility.h"],
    deps = [
        ":privacy_utility",
        "//googlesql/analyzer:resolver",
        "//googlesql/base:check",
        "//googlesql/base:ret_check",
        "//googlesql/base:status",
        "//googlesql/parser:parse_tree",
        "//googlesql/proto:anon_output_with_report_cc_proto",
        "//googlesql/proto:internal_error_location_cc_proto",
        "//googlesql/public:analyzer_options",
        "//googlesql/public:builtin_function_cc_proto",
        "//googlesql/public:catalog",
        "//googlesql/public:function",
        "//googlesql/public:function_cc_proto",
        "//googlesql/public:function_headers",
        "//googlesql/public:id_string",
        "//googlesql/public:options_cc_proto",
        "//googlesql/public:rewriter_interface",
        "//googlesql/public:type",
        "//googlesql/public:type_cc_proto",
        "//googlesql/public:value",
        "//googlesql/public/functions:differential_privacy_cc_proto",
        "//googlesql/public/types",
        "//googlesql/resolved_ast",
        "//googlesql/resolved_ast:column_factory",
        "//googlesql/resolved_ast:comparator",
        "//googlesql/resolved_ast:resolved_ast_enums_cc_proto",
        "//googlesql/resolved_ast:resolved_node_kind_cc_proto",
        "//googlesql/resolved_ast:rewrite_utils",
        "@com_google_absl//absl/status:statusor",
        "@com_google_absl//absl/strings:string_view",
        "@com_google_absl//absl/types:span",
    ],
)
