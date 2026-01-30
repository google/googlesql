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

load("@com_github_grpc_grpc//bazel:cc_grpc_library.bzl", "cc_grpc_library")
load("@com_google_protobuf//bazel:java_proto_library.bzl", "java_proto_library")
load("@com_google_protobuf//bazel:proto_library.bzl", "proto_library")
load("@io_grpc_grpc_java//:java_grpc_library.bzl", "java_grpc_library")
load("@rules_cc//cc:cc_binary.bzl", "cc_binary")
load("@rules_cc//cc:cc_library.bzl", "cc_library")
load("@rules_cc//cc:cc_test.bzl", "cc_test")

# GoogleSQL Server
package(default_visibility = ["//googlesql/base:googlesql_implementation"])

cc_library(
    name = "local_service",
    srcs = ["local_service.cc"],
    hdrs = [
        "local_service.h",
        "state.h",
    ],
    deps = [
        ":local_service_cc_proto",
        "//googlesql/base:map_util",
        "//googlesql/base:ret_check",
        "//googlesql/base:status",
        "//googlesql/common:errors",
        "//googlesql/common:proto_helper",
        "//googlesql/parser:parse_tree_serializer",
        "//googlesql/proto:options_cc_proto",
        "//googlesql/proto:simple_catalog_cc_proto",
        "//googlesql/public:analyzer",
        "//googlesql/public:builtin_function",
        "//googlesql/public:builtin_function_options",
        "//googlesql/public:evaluator",
        "//googlesql/public:formatter_options",
        "//googlesql/public:function",
        "//googlesql/public:id_string",
        "//googlesql/public:language_options",
        "//googlesql/public:lenient_formatter",
        "//googlesql/public:parse_resume_location",
        "//googlesql/public:parse_resume_location_cc_proto",
        "//googlesql/public:simple_catalog",
        "//googlesql/public:simple_table_cc_proto",
        "//googlesql/public:sql_formatter",
        "//googlesql/public:templated_sql_tvf",  # buildcleaner: keep
        "//googlesql/public:type",
        "//googlesql/public:type_cc_proto",
        "//googlesql/public:value",
        "//googlesql/public:value_cc_proto",
        "//googlesql/resolved_ast",
        "//googlesql/resolved_ast:sql_builder",
        "@com_google_absl//absl/base:core_headers",
        "@com_google_absl//absl/cleanup",
        "@com_google_absl//absl/container:flat_hash_map",
        "@com_google_absl//absl/container:flat_hash_set",
        "@com_google_absl//absl/functional:bind_front",
        "@com_google_absl//absl/status",
        "@com_google_absl//absl/status:statusor",
        "@com_google_absl//absl/strings",
        "@com_google_absl//absl/synchronization",
        "@com_google_absl//absl/types:optional",
        "@com_google_protobuf//:protobuf",
    ],
)

cc_test(
    name = "local_service_test",
    srcs = ["local_service_test.cc"],
    data = [
        "//googlesql/testdata:test_schema_proto",
    ],
    tags = ["requires-net:loopback"],
    deps = [
        ":local_service",
        "//googlesql/base",
        "//googlesql/base:path",
        "//googlesql/base:status",
        "//googlesql/base/testing:googlesql_gtest_main",
        "//googlesql/base/testing:status_matchers",
        "//googlesql/common:status_payload_utils",
        "//googlesql/common/testing:proto_matchers",
        "//googlesql/common/testing:testing_proto_util",
        "//googlesql/proto:function_cc_proto",
        "//googlesql/proto:simple_catalog_cc_proto",
        "//googlesql/public:formatter_options_cc_proto",
        "//googlesql/public:parse_resume_location_cc_proto",
        "//googlesql/public:simple_catalog",
        "//googlesql/public:simple_table_cc_proto",
        "//googlesql/public:type",
        "//googlesql/public:type_cc_proto",
        "//googlesql/public:value",
        "//googlesql/public:value_cc_proto",
        "//googlesql/public/functions:date_time_util",
        "//googlesql/resolved_ast:resolved_ast_cc_proto",
        "//googlesql/testdata:test_proto3_cc_proto",
        "//googlesql/testdata:test_schema_cc_proto",
        "@com_google_absl//absl/strings",
        "@com_google_protobuf//:protobuf",
    ],
)

cc_test(
    name = "local_service_benchmark",
    srcs = ["local_service_benchmark.cc"],
    deps = [
        ":local_service",
        ":local_service_cc_proto",
        "//googlesql/base",
        "//googlesql/base:status",
        "//googlesql/base/testing:status_matchers",
        "@com_github_google_benchmark//:benchmark_main",
        "@com_google_absl//absl/base",
        "@com_google_absl//absl/status",
    ],
)

proto_library(
    name = "local_service_proto",
    srcs = ["local_service.proto"],
    deps = [
        "//googlesql/parser:parse_tree_proto",
        "//googlesql/proto:function_proto",
        "//googlesql/proto:options_proto",
        "//googlesql/proto:simple_catalog_proto",
        "//googlesql/public:formatter_options_proto",
        "//googlesql/public:options_proto",
        "//googlesql/public:parse_resume_location_proto",
        "//googlesql/public:simple_table_proto",
        "//googlesql/public:type_proto",
        "//googlesql/public:value_proto",
        "//googlesql/resolved_ast:resolved_ast_proto",
        "@com_google_protobuf//:descriptor_proto",
        "@com_google_protobuf//:empty_proto",
    ],
)

cc_proto_library(
    name = "local_service_cc_proto",
    deps = [":local_service_proto"],
)

cc_grpc_library(
    name = "local_service_cc_grpc",
    srcs = [":local_service_proto"],
    grpc_only = True,
    deps = [":local_service_cc_proto"],
)

cc_library(
    name = "local_service_grpc",
    srcs = ["local_service_grpc.cc"],
    hdrs = ["local_service_grpc.h"],
    deps = [
        ":local_service",
        ":local_service_cc_grpc",
        ":local_service_cc_proto",
        "//googlesql/base:status",
        "//googlesql/proto:options_cc_proto",
        "//googlesql/public:parse_resume_location_cc_proto",
        "//googlesql/public:simple_table_cc_proto",
    ],
)

cc_test(
    name = "local_service_grpc_test",
    srcs = ["local_service_grpc_test.cc"],
    tags = ["requires-net:loopback"],
    deps = [
        ":local_service",
        ":local_service_cc_grpc",
        ":local_service_cc_proto",
        ":local_service_grpc",
        "//googlesql/base/testing:googlesql_gtest_main",
        "//googlesql/base/testing:status_matchers",
        "//googlesql/proto:options_cc_proto",
        "//googlesql/public:type_cc_proto",
        "//googlesql/public:value_cc_proto",
        "@com_github_grpc_grpc//:grpc++",
    ],
)

java_proto_library(
    name = "local_service_java_proto",
    deps = [":local_service_proto"],
)

java_grpc_library(
    name = "local_service_java_grpc",
    srcs = [":local_service_proto"],
    deps = [
        ":local_service_java_proto",
        "//googlesql/proto:options_java_proto",
        "//googlesql/public:parse_resume_location_java_proto",
        "//googlesql/public:simple_table_java_proto",
    ],
)

cc_library(
    name = "local_service_jni",
    srcs = ["local_service_jni.cc"],
    hdrs = ["local_service_jni.h"],
    linkstatic = 1,
    deps = [
        ":local_service_grpc",
        "@bazel_tools//tools/jdk:jni",
        "@com_github_grpc_grpc//:grpc++",
    ],
    alwayslink = 1,
)

cc_binary(
    name = "liblocal_service_jni.so",
    linkshared = 1,
    deps = [":local_service_jni"],
)

cc_binary(
    name = "liblocal_service_jni.dylib",
    linkshared = 1,
    deps = [":local_service_jni"],
)
