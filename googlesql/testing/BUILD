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

load("@rules_cc//cc:cc_binary.bzl", "cc_binary")
load("@rules_cc//cc:cc_library.bzl", "cc_library")
load("@rules_cc//cc:cc_test.bzl", "cc_test")

package(default_visibility = ["//googlesql/base:googlesql_implementation"])

cc_library(
    name = "sql_types_test",
    testonly = 1,
    srcs = ["sql_types_test.cc"],
    hdrs = ["sql_types_test.h"],
    deps = [
        "//googlesql/base:check",
        "//googlesql/base:map_util",
        "//googlesql/base/testing:googlesql_gtest_main",
        "//googlesql/public:civil_time",
        "//googlesql/public:coercer",
        "//googlesql/public:function",
        "//googlesql/public:language_options",
        "//googlesql/public:numeric_value",
        "//googlesql/public:options_cc_proto",
        "//googlesql/public:type",
        "//googlesql/public:type_cc_proto",
        "//googlesql/public:value",
        "//googlesql/public/types",
        "//googlesql/testdata:test_schema_cc_proto",
        "@com_google_absl//absl/container:flat_hash_map",
        "@com_google_absl//absl/strings:cord",
        "@com_google_protobuf//:protobuf",
    ],
)

cc_library(
    name = "test_module_contents_fetcher",
    testonly = 1,
    srcs = ["test_module_contents_fetcher.cc"],
    hdrs = ["test_module_contents_fetcher.h"],
    deps = [
        "//googlesql/base:map_util",
        "//googlesql/base:ret_check",
        "//googlesql/base:status",
        "//googlesql/public:file_module_contents_fetcher",
        "//googlesql/public:module_contents_fetcher",
        "@com_google_absl//absl/container:flat_hash_map",
        "@com_google_absl//absl/status",
        "@com_google_absl//absl/strings",
        "@com_google_protobuf//:protobuf",
    ],
)

cc_library(
    name = "test_catalog",
    testonly = 1,
    srcs = ["test_catalog.cc"],
    hdrs = ["test_catalog.h"],
    deps = [
        "//googlesql/base:map_util",
        "//googlesql/base:status",
        "//googlesql/public:catalog",
        "//googlesql/public:function",
        "//googlesql/public:simple_catalog",
        "//googlesql/public:type",
        "//googlesql/public/types",
        "@com_google_absl//absl/container:node_hash_map",
        "@com_google_absl//absl/status",
        "@com_google_absl//absl/strings",
    ],
)

cc_test(
    name = "test_catalog_test",
    size = "small",
    srcs = ["test_catalog_test.cc"],
    deps = [
        ":test_catalog",
        "//googlesql/base/testing:googlesql_gtest_main",
        "//googlesql/base/testing:status_matchers",
        "//googlesql/public:catalog",
        "//googlesql/public:function",
        "//googlesql/public:simple_catalog",
        "//googlesql/public:type",
        "@com_google_absl//absl/status",
    ],
)

cc_library(
    name = "test_value",
    testonly = 1,
    srcs = ["test_value.cc"],
    hdrs = [
        "test_value.h",
        "using_test_value.cc",
    ],
    deps = [
        "//googlesql/base:check",
        "//googlesql/base:status",
        "//googlesql/common:float_margin",
        "//googlesql/common:internal_value",
        "//googlesql/compliance:test_driver",
        "//googlesql/public:json_value",
        "//googlesql/public:numeric_value",
        "//googlesql/public:timestamp_picos_value",
        "//googlesql/public:type",
        "//googlesql/public:value",
        "//googlesql/public/types",
        "@com_google_absl//absl/status:statusor",
        "@com_google_absl//absl/strings",
        "@com_google_absl//absl/types:span",
        "@com_google_googletest//:gtest",
        "@com_google_protobuf//:protobuf",
    ],
)

cc_library(
    name = "test_function",
    testonly = 1,
    srcs = ["test_function.cc"],
    hdrs = ["test_function.h"],
    deps = [
        ":test_value",
        "//googlesql/base",
        "//googlesql/base:check",
        "//googlesql/base:map_util",
        "//googlesql/base:status",
        "//googlesql/common:float_margin",
        "//googlesql/public:options_cc_proto",
        "//googlesql/public:type",
        "//googlesql/public:type_cc_proto",
        "//googlesql/public:value",
        "//googlesql/public/types",
        "@com_google_absl//absl/base:core_headers",
        "@com_google_absl//absl/status",
        "@com_google_absl//absl/status:statusor",
        "@com_google_absl//absl/strings",
        "@com_google_absl//absl/types:span",
    ],
)

cc_library(
    name = "type_util",
    testonly = 1,
    srcs = ["type_util.cc"],
    hdrs = ["type_util.h"],
    deps = [
        "//googlesql/base:check",
        "//googlesql/compliance:test_util",
        "//googlesql/public:type",
        "//googlesql/public:type_cc_proto",
        "//googlesql/public/types",
        "//googlesql/testdata:test_proto3_cc_proto",
        "//googlesql/testdata:test_schema_cc_proto",
        "@com_google_absl//absl/strings",
        "@com_google_absl//absl/strings:string_view",
        "@com_google_googleapis//google/type:date_cc_proto",
        "@com_google_googleapis//google/type:latlng_cc_proto",
        "@com_google_googleapis//google/type:timeofday_cc_proto",
        "@com_google_protobuf//:protobuf",
    ],
)

cc_test(
    name = "type_util_test",
    size = "small",
    srcs = ["type_util_test.cc"],
    deps = [
        ":sql_types_test",
        ":type_util",
        "//googlesql/base/testing:googlesql_gtest_main",
        "//googlesql/base/testing:status_matchers",
        "//googlesql/public:type",
    ],
)
