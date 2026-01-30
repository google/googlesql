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
    name = "proto_matchers",
    testonly = 1,
    hdrs = ["proto_matchers.h"],
    deps = ["//googlesql/base/testing:proto_matchers"],
)

cc_library(
    name = "testing_proto_util",
    testonly = 1,
    hdrs = ["testing_proto_util.h"],
    deps = [
        "//googlesql/base:path",
        "@com_google_absl//absl/base:core_headers",
        "@com_google_absl//absl/strings:cord",
        "@com_google_googletest//:gtest",
        "@com_google_protobuf//:protobuf",
    ],
)

proto_library(
    name = "status_payload_matchers_test_proto",
    testonly = 1,
    srcs = ["status_payload_matchers_test.proto"],
)

cc_proto_library(
    name = "status_payload_matchers_test_cc_proto",
    testonly = 1,
    deps = [":status_payload_matchers_test_proto"],
)

cc_library(
    name = "status_payload_matchers",
    testonly = 1,
    srcs = [],
    hdrs = ["status_payload_matchers.h"],
    deps = [
        ":status_payload_matchers_oss",
    ],
)

cc_library(
    name = "status_payload_matchers_oss",
    testonly = 1,
    srcs = [],
    hdrs = ["status_payload_matchers_oss.h"],
    deps = [
        "//googlesql/base/testing:status_matchers",
        "//googlesql/common:status_payload_utils",
        "@com_google_absl//absl/status",
        "@com_google_absl//absl/status:statusor",
        "@com_google_absl//absl/strings",
        "@com_google_protobuf//:protobuf",
    ],
)

cc_test(
    name = "status_payload_matchers_oss_test",
    size = "small",
    srcs = ["status_payload_matchers_oss_test.cc"],
    deps = [
        ":status_payload_matchers_oss",
        ":status_payload_matchers_test_cc_proto",
        "//googlesql/base/testing:googlesql_gtest_main",
        "//googlesql/base/testing:proto_matchers_oss",
        "//googlesql/common:status_payload_utils",
        "@com_google_absl//absl/status",
        "@com_google_absl//absl/status:statusor",
        "@com_google_absl//absl/strings",
    ],
)
