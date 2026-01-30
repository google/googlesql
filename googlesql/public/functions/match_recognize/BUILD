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
    default_visibility = ["//visibility:public"],
)

cc_library(
    name = "compiled_pattern",
    srcs = ["compiled_pattern.cc"],
    hdrs = ["compiled_pattern.h"],
    deps = [
        ":compiled_pattern_cc_proto",
        ":match_partition",
        "//googlesql/base:ret_check",
        "//googlesql/base:status",
        "//googlesql/common/match_recognize:compiled_nfa",
        "//googlesql/common/match_recognize:nfa",
        "//googlesql/common/match_recognize:nfa_builder",
        "//googlesql/common/match_recognize:nfa_match_partition",
        "//googlesql/public:value",
        "//googlesql/resolved_ast",
        "//googlesql/resolved_ast:resolved_ast_enums_cc_proto",
        "//googlesql/resolved_ast:resolved_node_kind_cc_proto",
        "@com_google_absl//absl/status",
        "@com_google_absl//absl/status:statusor",
        "@com_google_absl//absl/strings",
    ],
)

cc_library(
    name = "match_partition",
    hdrs = ["match_partition.h"],
    deps = [
        "//googlesql/public:value",
        "@com_google_absl//absl/status:statusor",
        "@com_google_absl//absl/strings:string_view",
    ],
)

proto_library(
    name = "compiled_pattern_proto",
    srcs = ["compiled_pattern.proto"],
    deps = ["//googlesql/resolved_ast:resolved_ast_enums_proto"],
)

cc_proto_library(
    name = "compiled_pattern_cc_proto",
    deps = [":compiled_pattern_proto"],
)

cc_test(
    name = "compiled_pattern_test",
    srcs = ["compiled_pattern_test.cc"],
    deps = [
        ":match_partition",
        "//googlesql/base:check",
        "//googlesql/base:source_location",
        "//googlesql/base/testing:googlesql_gtest_main",
        "//googlesql/base/testing:status_matchers",
        "//googlesql/common/match_recognize:match_test_result_cc_proto",
        "//googlesql/common/match_recognize:test_matcher",
        "//googlesql/common/testing:proto_matchers",
        "//googlesql/public:value",
        "//googlesql/resolved_ast:resolved_ast_enums_cc_proto",
        "@com_google_absl//absl/container:flat_hash_map",
        "@com_google_absl//absl/log",
        "@com_google_absl//absl/status",
        "@com_google_absl//absl/status:statusor",
        "@com_google_absl//absl/strings",
        "@com_google_absl//absl/strings:string_view",
        "@com_google_absl//absl/types:span",
    ],
)
