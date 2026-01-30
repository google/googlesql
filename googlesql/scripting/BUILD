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

load("@bazel_skylib//:bzl_library.bzl", "bzl_library")
load("@com_google_protobuf//bazel:java_proto_library.bzl", "java_proto_library")
load("@com_google_protobuf//bazel:proto_library.bzl", "proto_library")
load("@rules_cc//cc:cc_library.bzl", "cc_library")
load("@rules_cc//cc:cc_test.bzl", "cc_test")

package(
    default_visibility = ["//googlesql/base:googlesql_implementation"],
)

cc_library(
    name = "control_flow_graph",
    srcs = ["control_flow_graph.cc"],
    hdrs = ["control_flow_graph.h"],
    deps = [
        ":script_segment",
        "//googlesql/base:check",
        "//googlesql/base:map_util",
        "//googlesql/base:ret_check",
        "//googlesql/base:status",
        "//googlesql/parser",
        "//googlesql/public:id_string",
        "//googlesql/public:parse_location",
        "@com_google_absl//absl/container:flat_hash_map",
        "@com_google_absl//absl/container:flat_hash_set",
        "@com_google_absl//absl/memory",
        "@com_google_absl//absl/status",
        "@com_google_absl//absl/status:statusor",
        "@com_google_absl//absl/strings",
    ],
)

cc_library(
    name = "parsed_script",
    srcs = ["parsed_script.cc"],
    hdrs = [
        "parsed_script.h",
    ],
    deps = [
        ":control_flow_graph",
        ":error_helpers",
        ":type_aliases",
        "//googlesql/base:check",
        "//googlesql/base:map_util",
        "//googlesql/base:status",
        "//googlesql/base:strings",
        "//googlesql/common:errors",
        "//googlesql/parser",
        "//googlesql/public:error_helpers",
        "//googlesql/public:id_string",
        "//googlesql/public:options_cc_proto",
        "//googlesql/public:parse_location",
        "//googlesql/public:type",
        "@com_google_absl//absl/algorithm:container",
        "@com_google_absl//absl/base:core_headers",
        "@com_google_absl//absl/container:btree",
        "@com_google_absl//absl/container:flat_hash_map",
        "@com_google_absl//absl/flags:flag",
        "@com_google_absl//absl/memory",
        "@com_google_absl//absl/status",
        "@com_google_absl//absl/status:statusor",
        "@com_google_absl//absl/strings",
        "@com_google_absl//absl/types:variant",
    ],
)

cc_test(
    name = "parsed_script_test",
    srcs = ["parsed_script_test.cc"],
    deps = [
        ":control_flow_graph",
        ":parsed_script",
        ":type_aliases",
        "//googlesql/base:source_location",
        "//googlesql/base/testing:googlesql_gtest_main",
        "//googlesql/base/testing:status_matchers",
        "//googlesql/parser",
        "//googlesql/public:error_helpers",
        "//googlesql/public:id_string",
        "//googlesql/public:language_options",
        "//googlesql/public:options_cc_proto",
        "//googlesql/public:parse_location",
        "@com_google_absl//absl/status",
        "@com_google_absl//absl/status:statusor",
        "@com_google_absl//absl/strings",
        "@com_google_absl//absl/types:span",
        "@com_google_absl//absl/types:variant",
    ],
)

cc_library(
    name = "script_segment",
    srcs = ["script_segment.cc"],
    hdrs = ["script_segment.h"],
    deps = [
        "//googlesql/parser",
        "//googlesql/public:parse_location",
        "//googlesql/public:parse_resume_location",
        "@com_google_absl//absl/strings",
    ],
)

cc_library(
    name = "parse_helpers",
    srcs = ["parse_helpers.cc"],
    hdrs = ["parse_helpers.h"],
    deps = [
        ":parsed_script",
        "//googlesql/base:status",
        "//googlesql/common:errors",
        "//googlesql/parser",
        "//googlesql/public:error_helpers",
        "//googlesql/public:options_cc_proto",
        "@com_google_absl//absl/base",
        "@com_google_absl//absl/base:core_headers",
        "@com_google_absl//absl/status:statusor",
        "@com_google_absl//absl/strings",
    ],
)

cc_library(
    name = "error_helpers",
    srcs = ["error_helpers.cc"],
    hdrs = ["error_helpers.h"],
    deps = [
        ":script_segment",
        "//googlesql/base:status",
        "//googlesql/common:errors",
        "//googlesql/common:status_payload_utils",
        "//googlesql/parser",
        "//googlesql/proto:script_exception_cc_proto",
        "//googlesql/public:error_location_cc_proto",
        "@com_google_absl//absl/status:statusor",
        "@com_google_absl//absl/strings",
    ],
)

cc_library(
    name = "stack_frame",
    hdrs = ["stack_frame.h"],
    deps = [
        ":parsed_script",
        "//googlesql/parser",
    ],
)

cc_library(
    name = "type_aliases",
    hdrs = ["type_aliases.h"],
    deps = [
        "//googlesql/public:id_string",
        "//googlesql/public:value",
    ],
)

cc_library(
    name = "script_executor",
    srcs = [
        "script_executor.cc",
        "script_executor_impl.cc",
        "script_executor_impl.h",
        "serialization_helpers.cc",
        "serialization_helpers.h",
    ],
    hdrs = [
        "script_executor.h",
    ],
    deps = [
        ":control_flow_graph",
        ":error_helpers",
        ":parsed_script",
        ":procedure_extension_cc_proto",
        ":script_executor_state_cc_proto",
        ":script_segment",
        ":stack_frame",
        ":type_aliases",
        ":variable_cc_proto",
        "//googlesql/base",
        "//googlesql/base:flat_set",
        "//googlesql/base:map_util",
        "//googlesql/base:ret_check",
        "//googlesql/base:status",
        "//googlesql/common:errors",
        "//googlesql/common:status_payload_utils",
        "//googlesql/parser",
        "//googlesql/proto:script_exception_cc_proto",
        "//googlesql/public:analyzer",
        "//googlesql/public:analyzer_options",
        "//googlesql/public:coercer",
        "//googlesql/public:error_helpers",
        "//googlesql/public:evaluator",
        "//googlesql/public:evaluator_table_iterator",
        "//googlesql/public:function",
        "//googlesql/public:id_string",
        "//googlesql/public:language_options",
        "//googlesql/public:options_cc_proto",
        "//googlesql/public:parse_location",
        "//googlesql/public:signature_match_result",
        "//googlesql/public:time_zone_util",
        "//googlesql/public:type",
        "//googlesql/public:value",
        "//googlesql/public/types",
        "@com_google_absl//absl/container:flat_hash_map",
        "@com_google_absl//absl/container:flat_hash_set",
        "@com_google_absl//absl/memory",
        "@com_google_absl//absl/status",
        "@com_google_absl//absl/status:statusor",
        "@com_google_absl//absl/strings",
        "@com_google_absl//absl/time",
        "@com_google_absl//absl/types:span",
        "@com_google_protobuf//:protobuf",
    ],
)

cc_test(
    name = "error_helpers_test",
    srcs = ["error_helpers_test.cc"],
    deps = [
        ":error_helpers",
        ":script_segment",
        "//googlesql/base:status",
        "//googlesql/base/testing:googlesql_gtest_main",
        "//googlesql/base/testing:status_matchers",
        "//googlesql/common:errors",
        "//googlesql/common:status_payload_utils",
        "//googlesql/parser",
        "//googlesql/proto:internal_error_location_cc_proto",
        "//googlesql/public:error_helpers",
        "//googlesql/public:options_cc_proto",
        "//googlesql/public:parse_location",
        "@com_google_absl//absl/strings",
    ],
)

proto_library(
    name = "script_executor_state_proto",
    srcs = ["script_executor_state.proto"],
    deps = [
        ":procedure_extension_proto",
        ":variable_proto",
        "//googlesql/proto:function_proto",
        "//googlesql/proto:script_exception_proto",
        "@com_google_protobuf//:any_proto",
    ],
)

cc_proto_library(
    name = "script_executor_state_cc_proto",
    deps = [":script_executor_state_proto"],
)

proto_library(
    name = "procedure_extension_proto",
    srcs = ["procedure_extension.proto"],
)

cc_proto_library(
    name = "procedure_extension_cc_proto",
    deps = [":procedure_extension_proto"],
)

proto_library(
    name = "variable_proto",
    srcs = ["variable.proto"],
    deps = [
        "//googlesql/public:type_parameters_proto",
        "//googlesql/public:type_proto",
        "//googlesql/public:value_proto",
    ],
)

cc_proto_library(
    name = "variable_cc_proto",
    deps = [":variable_proto"],
)

java_proto_library(
    name = "variable_java_proto",
    deps = [":variable_proto"],
)
