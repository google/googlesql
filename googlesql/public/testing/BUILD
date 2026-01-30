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
load("@rules_cc//cc:cc_test.bzl", "cc_test")

package(
    default_visibility = ["//visibility:public"],
)

cc_library(
    name = "test_case_options_util",
    testonly = 1,
    srcs = ["test_case_options_util.cc"],
    hdrs = ["test_case_options_util.h"],
    deps = [
        "//googlesql/base:status",
        "//googlesql/common:options_utils",
        "//googlesql/public:language_options",
        "//googlesql/public:options_cc_proto",
        "@com_google_absl//absl/status:statusor",
        "@com_google_absl//absl/strings",
        "@com_google_file_based_test_driver//file_based_test_driver:test_case_options",
    ],
)

cc_library(
    name = "error_matchers",
    testonly = 1,
    hdrs = ["error_matchers.h"],
    deps = [
        "//googlesql/base/testing:status_matchers",
        "@com_google_absl//absl/status",
        "@com_google_absl//absl/status:statusor",
        "@com_google_absl//absl/strings",
        "@com_google_googletest//:gtest",
    ],
)

cc_test(
    name = "error_matchers_test",
    srcs = ["error_matchers_test.cc"],
    deps = [
        ":error_matchers",
        "//googlesql/base/testing:googlesql_gtest_main",
        "@com_google_absl//absl/status",
        "@com_google_absl//absl/strings:string_view",
    ],
)
