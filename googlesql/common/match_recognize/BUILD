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

load("@com_google_protobuf//bazel:proto_library.bzl", "proto_library")
load("@rules_cc//cc:cc_library.bzl", "cc_library")
load("@rules_cc//cc:cc_test.bzl", "cc_test")

package(
    default_visibility = ["//googlesql/base:googlesql_implementation"],
)

cc_library(
    name = "nfa",
    srcs = ["nfa.cc"],
    hdrs = ["nfa.h"],
    deps = [
        "//googlesql/base:check",
        "//googlesql/base:ret_check",
        "//googlesql/base:status",
        "@com_google_absl//absl/base:nullability",
        "@com_google_absl//absl/container:flat_hash_set",
        "@com_google_absl//absl/memory",
        "@com_google_absl//absl/status",
        "@com_google_absl//absl/strings",
        "@com_google_absl//absl/strings:str_format",
        "@com_google_absl//absl/types:span",
    ],
)

cc_test(
    name = "nfa_test",
    srcs = ["nfa_test.cc"],
    deps = [
        ":nfa",
        "//googlesql/base/testing:googlesql_gtest_main",
        "//googlesql/base/testing:status_matchers",
        "@com_google_absl//absl/container:flat_hash_set",
        "@com_google_absl//absl/status",
        "@com_google_absl//absl/strings",
    ],
)

cc_library(
    name = "nfa_builder",
    srcs = ["nfa_builder.cc"],
    hdrs = ["nfa_builder.h"],
    deps = [
        ":epsilon_remover",
        ":nfa",
        "//googlesql/base:ret_check",
        "//googlesql/base:status",
        "//googlesql/public:coercer",
        "//googlesql/public:value",
        "//googlesql/resolved_ast",
        "//googlesql/resolved_ast:resolved_ast_enums_cc_proto",
        "@com_google_absl//absl/container:flat_hash_map",
        "@com_google_absl//absl/status",
        "@com_google_absl//absl/status:statusor",
        "@com_google_absl//absl/strings",
        "@com_google_absl//absl/time",
    ],
)

cc_test(
    name = "nfa_builder_test",
    srcs = ["nfa_builder_test.cc"],
    deps = [
        ":nfa",
        ":nfa_builder",
        ":nfa_matchers",
        ":test_pattern_resolver",
        "//googlesql/base/testing:googlesql_gtest_main",
        "//googlesql/base/testing:status_matchers",
        "//googlesql/public:analyzer_options",
        "//googlesql/public:value",
        "//googlesql/public/types",
        "//googlesql/resolved_ast",
        "@com_google_absl//absl/status",
        "@com_google_absl//absl/status:statusor",
        "@com_google_absl//absl/strings",
        "@com_google_absl//absl/strings:string_view",
    ],
)

cc_library(
    name = "nfa_matchers",
    testonly = 1,
    srcs = ["nfa_matchers.cc"],
    hdrs = ["nfa_matchers.h"],
    deps = [
        ":nfa",
        "//googlesql/base:check",
        "@com_google_absl//absl/container:flat_hash_map",
        "@com_google_absl//absl/strings",
        "@com_google_googletest//:gtest",
    ],
)

cc_test(
    name = "nfa_matchers_test",
    srcs = ["nfa_matchers_test.cc"],
    deps = [
        ":nfa",
        ":nfa_matchers",
        "//googlesql/base:status",
        "//googlesql/base/testing:googlesql_gtest_main",
        "//googlesql/base/testing:status_matchers",
        "@com_google_absl//absl/status:statusor",
    ],
)

cc_library(
    name = "epsilon_remover",
    srcs = ["epsilon_remover.cc"],
    hdrs = ["epsilon_remover.h"],
    deps = [
        ":nfa",
        "//googlesql/base:ret_check",
        "//googlesql/base:status",
        "@com_google_absl//absl/container:flat_hash_map",
        "@com_google_absl//absl/container:flat_hash_set",
        "@com_google_absl//absl/status",
        "@com_google_absl//absl/status:statusor",
        "@com_google_absl//absl/strings",
    ],
)

cc_test(
    name = "epsilon_remover_test",
    srcs = ["epsilon_remover_test.cc"],
    deps = [
        ":epsilon_remover",
        ":nfa",
        ":nfa_matchers",
        "//googlesql/base/testing:googlesql_gtest_main",
        "//googlesql/base/testing:status_matchers",
        "@com_google_absl//absl/status",
    ],
)

cc_library(
    name = "test_pattern_resolver",
    testonly = 1,
    srcs = ["test_pattern_resolver.cc"],
    hdrs = ["test_pattern_resolver.h"],
    deps = [
        "//googlesql/base:case",
        "//googlesql/base:status",
        "//googlesql/public:analyzer",
        "//googlesql/public:analyzer_options",
        "//googlesql/public:analyzer_output",
        "//googlesql/public:builtin_function_options",
        "//googlesql/public:options_cc_proto",
        "//googlesql/public:simple_catalog",
        "//googlesql/public/types",
        "//googlesql/resolved_ast",
        "//googlesql/resolved_ast:resolved_ast_enums_cc_proto",
        "@com_google_absl//absl/container:btree",
        "@com_google_absl//absl/log",
        "@com_google_absl//absl/status",
        "@com_google_absl//absl/status:statusor",
        "@com_google_absl//absl/strings",
        "@com_google_absl//absl/strings:string_view",
        "@com_googlesource_code_re2//:re2",
    ],
)

cc_test(
    name = "test_pattern_resolver_test",
    srcs = ["test_pattern_resolver_test.cc"],
    deps = [
        ":test_pattern_resolver",
        "//googlesql/base/testing:googlesql_gtest_main",
        "//googlesql/base/testing:status_matchers",
        "//googlesql/public:analyzer_options",
        "//googlesql/public:value",
        "//googlesql/public/types",
        "//googlesql/resolved_ast",
        "//googlesql/resolved_ast:resolved_ast_enums_cc_proto",
        "@com_google_absl//absl/status",
        "@com_google_absl//absl/strings:string_view",
    ],
)

cc_library(
    name = "nfa_match_partition",
    srcs = ["nfa_match_partition.cc"],
    hdrs = ["nfa_match_partition.h"],
    deps = [
        ":compiled_nfa",
        ":edge_tracker",
        ":nfa",
        ":row_edge_list",
        "//googlesql/base:check",
        "//googlesql/base:ret_check",
        "//googlesql/base:status",
        "//googlesql/public/functions/match_recognize:match_partition",
        "@com_google_absl//absl/base:core_headers",
        "@com_google_absl//absl/status",
        "@com_google_absl//absl/status:statusor",
        "@com_google_absl//absl/strings",
    ],
)

proto_library(
    name = "match_test_result_proto",
    testonly = 1,
    srcs = ["match_test_result.proto"],
)

cc_proto_library(
    name = "match_test_result_cc_proto",
    testonly = 1,
    deps = [":match_test_result_proto"],
)

cc_library(
    name = "compiled_nfa",
    srcs = ["compiled_nfa.cc"],
    hdrs = ["compiled_nfa.h"],
    deps = [
        ":nfa",
        "//googlesql/base:ret_check",
        "//googlesql/base:status",
        "//googlesql/public/functions/match_recognize:compiled_pattern_cc_proto",
        "@com_google_absl//absl/memory",
        "@com_google_absl//absl/status",
        "@com_google_absl//absl/status:statusor",
        "@com_google_absl//absl/strings",
        "@com_google_absl//absl/types:span",
    ],
)

cc_test(
    name = "compiled_nfa_test",
    srcs = ["compiled_nfa_test.cc"],
    deps = [
        ":compiled_nfa",
        ":edge_matchers",
        ":nfa",
        "//googlesql/base/testing:googlesql_gtest_main",
        "//googlesql/base/testing:status_matchers",
        "//googlesql/public/functions/match_recognize:compiled_pattern_cc_proto",
        "@com_google_absl//absl/status",
    ],
)

cc_library(
    name = "row_edge_list",
    srcs = ["row_edge_list.cc"],
    hdrs = ["row_edge_list.h"],
    deps = [
        ":compiled_nfa",
        ":nfa",
        "//googlesql/base:check",
        "@com_google_absl//absl/base:nullability",
        "@com_google_absl//absl/strings",
        "@com_google_absl//absl/types:span",
    ],
)

cc_test(
    name = "row_edge_list_test",
    srcs = ["row_edge_list_test.cc"],
    deps = [
        ":compiled_nfa",
        ":edge_matchers",
        ":nfa",
        ":row_edge_list",
        "//googlesql/base/testing:googlesql_gtest_main",
        "//googlesql/base/testing:status_matchers",
        "@com_google_absl//absl/log",
    ],
)

cc_library(
    name = "test_matcher",
    testonly = 1,
    srcs = ["test_matcher.cc"],
    hdrs = ["test_matcher.h"],
    deps = [
        ":match_test_result_cc_proto",
        ":test_pattern_resolver",
        "//googlesql/base:check",
        "//googlesql/base:status",
        "//googlesql/public:analyzer_options",
        "//googlesql/public:value",
        "//googlesql/public/functions/match_recognize:compiled_pattern",
        "//googlesql/public/functions/match_recognize:compiled_pattern_cc_proto",
        "//googlesql/public/functions/match_recognize:match_partition",
        "//googlesql/public/types",
        "//googlesql/resolved_ast",
        "//googlesql/resolved_ast:resolved_ast_enums_cc_proto",
        "@com_google_absl//absl/container:flat_hash_map",
        "@com_google_absl//absl/status",
        "@com_google_absl//absl/status:statusor",
        "@com_google_absl//absl/strings",
        "@com_google_absl//absl/strings:string_view",
        "@com_google_absl//absl/types:span",
    ],
)

cc_library(
    name = "edge_tracker",
    srcs = ["edge_tracker.cc"],
    hdrs = ["edge_tracker.h"],
    deps = [
        ":compiled_nfa",
        ":nfa",
        ":row_edge_list",
        "@com_google_absl//absl/functional:any_invocable",
    ],
)

cc_library(
    name = "edge_matchers",
    testonly = 1,
    srcs = ["edge_matchers.cc"],
    hdrs = ["edge_matchers.h"],
    deps = [
        ":compiled_nfa",
        ":nfa",
        "@com_google_absl//absl/strings",
        "@com_google_googletest//:gtest",
    ],
)

cc_test(
    name = "edge_tracker_test",
    srcs = ["edge_tracker_test.cc"],
    deps = [
        ":compiled_nfa",
        ":edge_matchers",
        ":edge_tracker",
        ":nfa",
        ":row_edge_list",
        "//googlesql/base/testing:googlesql_gtest_main",
        "//googlesql/base/testing:status_matchers",
    ],
)
