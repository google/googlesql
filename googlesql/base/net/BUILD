#
# Copyright 2020 Google LLC
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

load("@rules_cc//cc:cc_library.bzl", "cc_library")
load("@rules_cc//cc:cc_test.bzl", "cc_test")

licenses(["notice"])

package(default_visibility = ["//googlesql/base:googlesql_implementation"])

cc_library(
    name = "public_suffix",
    hdrs = ["public_suffix.h"],
    deps = [
        ":public_suffix_oss",
    ],
)

cc_library(
    name = "idn",
    hdrs = ["idn.h"],
    deps = [
        ":idn_oss",
    ],
)

cc_library(
    name = "ipaddress",
    hdrs = ["ipaddress.h"],
    deps = [
        ":ipaddress_oss",
    ],
)

py_binary(
    name = "simple_embedder",
    srcs = ["simple_embedder.py"],
    deps = [
        "@io_abseil_py//absl:app",
    ],
)

genrule(
    name = "gen_public_suffix_data",
    srcs = ["@org_publicsuffix//:public_suffix_list.dat"],
    outs = ["public_suffix_list_data.h"],
    cmd = "$(location :simple_embedder)" +
          " $(location @org_publicsuffix//:public_suffix_list.dat)" +
          " $(location public_suffix_list_data.h)" +
          " googlesql::internal" +
          " kPublicSuffixListData",
    tools = [":simple_embedder"],
)

cc_library(
    name = "public_suffix_oss",
    srcs = [
        "public_suffix_list_data.h",
        "public_suffix_oss.cc",
    ],
    hdrs = ["public_suffix_oss.h"],
    deps = [
        "//googlesql/base:logging",
        "@com_google_absl//absl/container:flat_hash_map",
        "@com_google_absl//absl/container:flat_hash_set",
        "@com_google_absl//absl/container:inlined_vector",
        "@com_google_absl//absl/functional:function_ref",
        "@com_google_absl//absl/strings",
        "@com_google_absl//absl/strings:str_format",
        "@icu",
    ],
)

genrule(
    name = "gen_public_suffix_test_psl",
    srcs = ["@org_publicsuffix//:test_psl.txt"],
    outs = ["public_suffix_test_psl.h"],
    cmd = "$(location :simple_embedder)" +
          " $(location @org_publicsuffix//:test_psl.txt)" +
          " $(location public_suffix_test_psl.h)" +
          " googlesql::internal" +
          " kTestPsl",
    tools = [":simple_embedder"],
)

cc_test(
    name = "public_suffix_oss_test",
    srcs = [
        "public_suffix_oss_test.cc",
        "public_suffix_test_psl.h",
    ],
    deps = [
        ":idn_oss",
        ":public_suffix_oss",
        "//googlesql/base/testing:googlesql_gtest_main",
        "@com_google_absl//absl/container:flat_hash_set",
        "@com_google_absl//absl/strings",
    ],
)

cc_library(
    name = "idn_oss",
    srcs = [
        "idn_oss.cc",
    ],
    hdrs = ["idn_oss.h"],
    deps = [
        "//googlesql/base:logging",
        "@com_google_absl//absl/strings",
        "@com_google_absl//absl/strings:str_format",
        "@icu",
    ],
)

cc_test(
    name = "idn_oss_test",
    srcs = [
        "idn_oss_test.cc",
    ],
    deps = [
        ":idn_oss",
        "//googlesql/base/testing:googlesql_gtest_main",
        "@com_google_absl//absl/strings",
    ],
)

cc_library(
    name = "ipaddress_oss",
    srcs = [
        "ipaddress_oss.cc",
    ],
    hdrs = ["ipaddress_oss.h"],
    deps = [
        "//googlesql/base:endian",
        "//googlesql/base:logging",
        "@com_google_absl//absl/base:config",
        "@com_google_absl//absl/base:core_headers",
        "@com_google_absl//absl/numeric:int128",
        "@com_google_absl//absl/status",
        "@com_google_absl//absl/status:statusor",
        "@com_google_absl//absl/strings",
        "@com_google_absl//absl/strings:str_format",
    ],
)

cc_test(
    name = "ipaddress_oss_test",
    srcs = [
        "ipaddress_oss_test.cc",
    ],
    deps = [
        ":ipaddress_oss",
        "//googlesql/base:endian",
        "//googlesql/base:logging",
        "//googlesql/base/testing:googlesql_gtest_main",
        "@com_google_absl//absl/container:fixed_array",
        "@com_google_absl//absl/numeric:int128",
        "@com_google_absl//absl/status",
        "@com_google_absl//absl/strings",
        "@com_google_absl//absl/strings:str_format",
    ],
)
