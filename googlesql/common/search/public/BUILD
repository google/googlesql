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

package(default_visibility = ["//googlesql/base:googlesql_implementation"])

cc_library(
    name = "token_list_util",
    srcs = ["token_list_util.cc"],
    hdrs = ["token_list_util.h"],
    deps = [
        "//googlesql/public:simple_token_list",
        "//googlesql/public:strings",
        "@com_google_absl//absl/base:no_destructor",
        "@com_google_absl//absl/functional:any_invocable",
        "@com_google_absl//absl/strings",
        "@com_google_absl//absl/strings:str_format",
        "@com_google_absl//absl/strings:string_view",
        "@com_google_absl//absl/types:span",
    ],
)

cc_test(
    name = "token_list_util_test",
    srcs = ["token_list_util_test.cc"],
    deps = [
        ":token_list_util",
        "//googlesql/base/testing:googlesql_gtest_main",
        "//googlesql/public:simple_token_list",
        "@com_google_absl//absl/strings",
    ],
)
