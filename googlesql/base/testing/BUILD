#
# Copyright 2018 Google LLC
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
load("@rules_cc//cc:cc_test.bzl", "cc_test")

licenses(["notice"])

package(default_visibility = ["//googlesql/base:googlesql_implementation"])

# A drop in replacement for gtest_main that parsers absl flags
cc_library(
    name = "googlesql_gtest_main",
    testonly = 1,
    srcs = ["googlesql_gtest_main.cc"],
    deps = [
        "@com_google_absl//absl/flags:flag",
        "@com_google_absl//absl/flags:parse",
        "@com_google_absl//absl/log:initialize",
        "@com_google_googletest//:gtest",
    ],
)

cc_test(
    name = "googlesql_gtest_main_test",
    testonly = 1,
    srcs = ["googlesql_gtest_main_test.cc"],
    args = ["--test_flag_value=good"],
    deps = [
        ":googlesql_gtest_main",
        "@com_google_absl//absl/flags:flag",
        "@com_google_googletest//:gtest",
    ],
)

cc_library(
    name = "status_matchers",
    testonly = 1,
    srcs = ["status_matchers.cc"],
    hdrs = ["status_matchers.h"],
    deps = [
        "//googlesql/base:source_location",
        "//googlesql/base:status",
        "@com_google_absl//absl/status",
        "@com_google_absl//absl/status:status_matchers",
        "@com_google_absl//absl/status:statusor",
        "@com_google_absl//absl/strings",
        "@com_google_absl//absl/strings:string_view",
        "@com_google_googletest//:gtest",
    ],
)

cc_test(
    name = "status_matchers_test",
    size = "small",
    srcs = ["status_matchers_test.cc"],
    deps = [
        ":status_matchers",
        "//googlesql/base:status",
        "//googlesql/base/testing:googlesql_gtest_main",
        "@com_google_absl//absl/status",
        "@com_google_absl//absl/status:statusor",
        "@com_google_googletest//:gtest",
    ],
)

cc_library(
    name = "proto_matchers",
    testonly = 1,
    hdrs = ["proto_matchers.h"],
    deps = [
        ":proto_matchers_oss",
    ],
)

cc_library(
    name = "proto_matchers_oss",
    testonly = 1,
    hdrs = ["proto_matchers_oss.h"],
    deps = [
        "//googlesql/base:logging",
        "@com_google_absl//absl/strings",
        "@com_google_googletest//:gtest",
        "@com_google_protobuf//:protobuf",
    ],
)

cc_test(
    name = "proto_matchers_oss_test",
    size = "small",
    srcs = ["proto_matchers_oss_test.cc"],
    deps = [
        ":proto_matchers_oss",
        "//googlesql/base:test_payload_cc_proto",
        "//googlesql/base/testing:googlesql_gtest_main",
        "@com_google_absl//absl/strings",
        "@com_google_googletest//:gtest",
    ],
)
