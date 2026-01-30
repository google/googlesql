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

load("@com_google_protobuf//bazel:java_proto_library.bzl", "java_proto_library")
load("@com_google_protobuf//bazel:proto_library.bzl", "proto_library")
load("@rules_cc//cc:cc_test.bzl", "cc_test")
load("@rules_python//python:proto.bzl", "py_proto_library")

package(default_visibility = ["//googlesql/base:googlesql_implementation"])

filegroup(
    name = "type_annotation_filegroup",
    srcs = [
        "type_annotation.proto",
        "wire_format_annotation.proto",
    ],
)

proto_library(
    name = "logging_proto",
    srcs = ["logging.proto"],
    deps = ["@com_google_protobuf//:duration_proto"],
)

cc_proto_library(
    name = "logging_cc_proto",
    deps = [":logging_proto"],
)

java_proto_library(
    name = "logging_java_proto",
    deps = ["logging_proto"],
)

proto_library(
    name = "type_annotation_proto",
    srcs = [
        "type_annotation.proto",
        "wire_format_annotation.proto",
    ],
    features = ["-proto_dynamic_mode_static_link"],
    deps = ["@com_google_protobuf//:descriptor_proto"],
)

cc_proto_library(
    name = "type_annotation_cc_proto",
    deps = [":type_annotation_proto"],
)

java_proto_library(
    name = "type_annotation_java_proto",
    deps = [":type_annotation_proto"],
)

exports_files([
    "wire_format_annotation.proto",
    "type_annotation.proto",
])
