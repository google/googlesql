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

load("@com_google_protobuf//bazel:java_proto_library.bzl", "java_proto_library")
load("@com_google_protobuf//bazel:proto_library.bzl", "proto_library")
load("@rules_python//python:proto.bzl", "py_proto_library")

package(default_visibility = ["//googlesql/base:googlesql_implementation"])

proto_library(
    name = "property_graph_proto",
    srcs = ["property_graph.proto"],
)

cc_proto_library(
    name = "property_graph_cc_proto",
    deps = [":property_graph_proto"],
)

java_proto_library(
    name = "property_graph_java_proto",
    deps = [":property_graph_proto"],
)

py_proto_library(
    name = "property_graph_py_proto",
    deps = [":property_graph_proto"],
)
