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
load("@rules_cc//cc:cc_library.bzl", "cc_library")
load("@rules_cc//cc:cc_test.bzl", "cc_test")

package(default_visibility = ["//visibility:public"])

cc_library(
    name = "util",
    srcs = ["util.cc"],
    hdrs = ["util.h"],
    deps = [
        "//googlesql/base:status",
        "//googlesql/common:utf_util",
        "@com_google_absl//absl/base:core_headers",
        "@com_google_absl//absl/numeric:int128",
        "@com_google_absl//absl/status",
        "@com_google_absl//absl/strings",
    ],
)

cc_test(
    name = "util_test",
    size = "small",
    srcs = ["util_test.cc"],
    deps = [
        ":util",
        "//googlesql/base:status",
        "//googlesql/base/testing:googlesql_gtest_main",
        "//googlesql/base/testing:status_matchers",
        "@com_google_absl//absl/status",
    ],
)

cc_library(
    name = "convert_internal",
    hdrs = ["convert_internal.h"],
    deps = ["//googlesql/base"],
)

cc_test(
    name = "convert_internal_test",
    size = "small",
    srcs = ["convert_internal_test.cc"],
    deps = [
        ":convert_internal",
        "//googlesql/base/testing:googlesql_gtest_main",
    ],
)

cc_library(
    name = "compression",
    srcs = ["compression.cc"],
    hdrs = ["compression.h"],
    deps = [
        "//googlesql/base:check",
        "//googlesql/base:status",
        "//googlesql/common:utf_util",
        "@com_google_absl//absl/memory",
        "@com_google_absl//absl/status",
        "@com_google_absl//absl/status:statusor",
        "@com_google_absl//absl/strings",
        "@com_google_absl//absl/strings:string_view",
    ],
)

cc_test(
    name = "compression_test",
    size = "small",
    srcs = ["compression_test.cc"],
    deps = [
        ":compression",
        "//googlesql/base/testing:googlesql_gtest_main",
        "//googlesql/base/testing:status_matchers",
        "@com_google_absl//absl/status",
    ],
)

cc_library(
    name = "convert",
    srcs = ["convert.cc"],
    hdrs = ["convert.h"],
    deps = [
        ":convert_internal",
        ":util",
        "//googlesql/base",
        "//googlesql/base:status",
        "//googlesql/public:numeric_value",
        "@com_google_absl//absl/base:core_headers",
        "@com_google_absl//absl/status:statusor",
        "@com_google_absl//absl/strings",
    ],
)

cc_test(
    name = "convert_test",
    size = "small",
    srcs = ["convert_test.cc"],
    deps = [
        ":convert",
        "//googlesql/base:status",
        "//googlesql/base/testing:googlesql_gtest_main",
        "//googlesql/base/testing:status_matchers",
        "//googlesql/compliance:functions_testlib",
        "//googlesql/public:numeric_value",
        "//googlesql/public:type_cc_proto",
        "//googlesql/public:value",
        "//googlesql/testing:test_function",
        "@com_google_absl//absl/status",
        "@com_google_absl//absl/strings",
    ],
)

cc_library(
    name = "convert_proto",
    srcs = ["convert_proto.cc"],
    hdrs = ["convert_proto.h"],
    deps = [
        "//googlesql/base:source_location",
        "//googlesql/base:status",
        "@com_google_absl//absl/strings",
        "@com_google_absl//absl/strings:cord",
        "@com_google_protobuf//:protobuf",
    ],
)

cc_test(
    name = "convert_proto_test",
    size = "small",
    srcs = ["convert_proto_test.cc"],
    deps = [
        ":convert_proto",
        "//googlesql/base/testing:googlesql_gtest_main",
        "//googlesql/base/testing:status_matchers",
        "//googlesql/testdata:test_schema_cc_proto",
    ],
)

cc_library(
    name = "convert_string",
    srcs = ["convert_string.cc"],
    hdrs = ["convert_string.h"],
    deps = [
        ":util",
        "//googlesql/base:case",
        "//googlesql/base:status",
        "//googlesql/common:string_util",
        "//googlesql/public:numeric_value",
        "@com_google_absl//absl/base:core_headers",
        "@com_google_absl//absl/status:statusor",
        "@com_google_absl//absl/strings",
    ],
)

cc_test(
    name = "convert_string_test",
    size = "small",
    srcs = ["convert_string_test.cc"],
    deps = [
        ":convert_string",
        "//googlesql/base:status",
        "//googlesql/base/testing:googlesql_gtest_main",
        "//googlesql/base/testing:status_matchers",
        "//googlesql/compliance:functions_testlib",
        "//googlesql/public:type_cc_proto",
        "//googlesql/public:value",
        "//googlesql/testing:test_function",
        "@com_google_absl//absl/status",
        "@com_google_absl//absl/strings",
    ],
)

cc_library(
    name = "convert_string_with_format",
    srcs = ["convert_string_with_format.cc"],
    hdrs = ["convert_string_with_format.h"],
    deps = [
        ":convert",
        ":format_max_output_width",
        ":string_format",
        ":util",
        "//googlesql/base:ret_check",
        "//googlesql/base:status",
        "//googlesql/public:numeric_value",
        "//googlesql/public:value",
        "@com_google_absl//absl/container:flat_hash_map",
        "@com_google_absl//absl/flags:flag",
        "@com_google_absl//absl/status",
        "@com_google_absl//absl/status:statusor",
        "@com_google_absl//absl/strings",
        "@com_google_absl//absl/strings:str_format",
        "@com_googlesource_code_re2//:re2",
    ],
)

cc_test(
    name = "convert_string_with_format_test",
    size = "small",
    srcs = ["convert_string_with_format_test.cc"],
    deps = [
        ":convert_string",
        ":convert_string_with_format",
        ":format_max_output_width",
        "//googlesql/base:status",
        "//googlesql/base/testing:googlesql_gtest_main",
        "//googlesql/base/testing:status_matchers",
        "//googlesql/compliance:functions_testlib",
        "//googlesql/public:numeric_value",
        "//googlesql/public:options_cc_proto",
        "//googlesql/public:type_cc_proto",
        "//googlesql/public:value",
        "//googlesql/testing:test_function",
        "@com_google_absl//absl/flags:flag",
        "@com_google_absl//absl/strings",
    ],
)

cc_library(
    name = "format_max_output_width",
    srcs = ["format_max_output_width.cc"],
    hdrs = ["format_max_output_width.h"],
    deps = [
        "@com_google_absl//absl/flags:flag",
    ],
)

cc_library(
    name = "input_format_string_max_width",
    srcs = ["input_format_string_max_width.cc"],
    hdrs = ["input_format_string_max_width.h"],
    deps = ["@com_google_absl//absl/flags:flag"],
)

cc_library(
    name = "string_format",
    srcs = ["string_format.cc"],
    hdrs = ["string_format.h"],
    deps = [
        ":convert_proto",
        ":format_max_output_width",
        "//googlesql/base",
        "//googlesql/base:ret_check",
        "//googlesql/base:status",
        "//googlesql/common:utf_util",
        "//googlesql/public:json_value",
        "//googlesql/public:numeric_value",
        "//googlesql/public:options_cc_proto",
        "//googlesql/public:strings",
        "//googlesql/public:type",
        "//googlesql/public:type_cc_proto",
        "//googlesql/public:value",
        "//googlesql/public/types",
        "@com_google_absl//absl/base:core_headers",
        "@com_google_absl//absl/flags:flag",
        "@com_google_absl//absl/status",
        "@com_google_absl//absl/strings",
        "@com_google_absl//absl/strings:cord",
        "@com_google_absl//absl/strings:str_format",
        "@com_google_absl//absl/types:span",
        "@com_google_protobuf//:protobuf",
        "@com_googlesource_code_re2//:re2",
        "@icu//:common",
    ],
)

cc_test(
    name = "format_test",
    size = "medium",
    srcs = ["format_test.cc"],
    shard_count = 6,
    deps = [
        ":format_max_output_width",
        ":string_format",
        "//googlesql/base",
        "//googlesql/base:map_util",
        "//googlesql/base:status",
        "//googlesql/base/testing:googlesql_gtest_main",
        "//googlesql/base/testing:status_matchers",
        "//googlesql/compliance:functions_testlib",
        "//googlesql/public:civil_time",
        "//googlesql/public:value",
        "//googlesql/public/types",
        "//googlesql/testdata:test_schema_cc_proto",
        "//googlesql/testing:test_function",
        "//googlesql/testing:test_value",
        "@com_google_absl//absl/base",
        "@com_google_absl//absl/cleanup",
        "@com_google_absl//absl/flags:flag",
        "@com_google_absl//absl/functional:bind_front",
        "@com_google_absl//absl/random",
        "@com_google_absl//absl/status",
        "@com_google_absl//absl/status:statusor",
        "@com_google_absl//absl/strings",
        "@com_google_absl//absl/types:span",
    ],
)

cc_test(
    name = "string_format_test",
    size = "small",
    srcs = ["string_format_test.cc"],
    deps = [
        ":string_format",
        "//googlesql/base",
        "//googlesql/base/testing:googlesql_gtest_main",
        "//googlesql/base/testing:status_matchers",
        "//googlesql/common/testing:testing_proto_util",
        "//googlesql/public:options_cc_proto",
        "//googlesql/public:value",
        "//googlesql/public/types",
        "//googlesql/testdata:test_schema_cc_proto",
        "@com_google_absl//absl/status",
        "@com_google_absl//absl/strings",
        "@com_google_absl//absl/types:span",
    ],
)

cc_library(
    name = "arithmetics_internal",
    hdrs = ["arithmetics_internal.h"],
    deps = [
        "//googlesql/base",
    ],
)

cc_test(
    name = "arithmetics_internal_test",
    size = "small",
    srcs = ["arithmetics_internal_test.cc"],
    deps = [
        ":arithmetics_internal",
        "//googlesql/base/testing:googlesql_gtest_main",
    ],
)

cc_library(
    name = "arithmetics",
    hdrs = ["arithmetics.h"],
    deps = [
        ":arithmetics_internal",
        ":convert",
        ":util",
        "//googlesql/base:status",
        "//googlesql/public:numeric_value",
        "@com_google_absl//absl/base:core_headers",
        "@com_google_absl//absl/status:statusor",
        "@com_google_absl//absl/strings",
    ],
)

cc_test(
    name = "arithmetics_test",
    size = "small",
    srcs = ["arithmetics_test.cc"],
    deps = [
        ":arithmetics",
        "//googlesql/base",
        "//googlesql/base:status",
        "//googlesql/base/testing:googlesql_gtest_main",
        "//googlesql/base/testing:status_matchers",
        "//googlesql/compliance:functions_testlib",
        "//googlesql/public:options_cc_proto",
        "//googlesql/public:type",
        "//googlesql/public:type_cc_proto",
        "//googlesql/public:value",
        "//googlesql/testing:test_function",
        "@com_google_absl//absl/numeric:int128",
        "@com_google_absl//absl/status",
    ],
)

cc_library(
    name = "bitwise",
    srcs = ["bitwise.cc"],
    hdrs = ["bitwise.h"],
    deps = [
        ":bitwise_agg_mode_cc_proto",
        ":util",
        "//googlesql/base:bits",
        "//googlesql/base:status",
        "@com_google_absl//absl/base",
        "@com_google_absl//absl/base:core_headers",
        "@com_google_absl//absl/strings",
    ],
)

cc_test(
    name = "bitwise_test",
    size = "small",
    srcs = ["bitwise_test.cc"],
    deps = [
        ":bitwise",
        ":bitwise_agg_mode_cc_proto",
        "//googlesql/base",
        "//googlesql/base:status",
        "//googlesql/base/testing:googlesql_gtest_main",
        "//googlesql/compliance:functions_testlib",
        "//googlesql/public:type_cc_proto",
        "//googlesql/public:value",
        "//googlesql/testing:test_function",
    ],
)

cc_library(
    name = "bitcast",
    hdrs = ["bitcast.h"],
    deps = [
        "//googlesql/base:status",
        "@com_google_absl//absl/base",
    ],
)

cc_test(
    name = "bitcast_test",
    size = "small",
    srcs = ["bitcast_test.cc"],
    deps = [
        ":bitcast",
        "//googlesql/base:status",
        "//googlesql/base/testing:googlesql_gtest_main",
    ],
)

cc_library(
    name = "comparison",
    hdrs = ["comparison.h"],
)

cc_test(
    name = "comparison_test",
    size = "small",
    srcs = ["comparison_test.cc"],
    deps = [
        ":comparison",
        "//googlesql/base/testing:googlesql_gtest_main",
    ],
)

cc_library(
    name = "date_time_util_internal",
    srcs = ["date_time_util_internal.cc"],
    hdrs = ["date_time_util_internal.h"],
    deps = [
        "//googlesql/base",
        "@com_google_absl//absl/time",
    ],
)

cc_test(
    name = "date_time_util_internal_test",
    size = "small",
    srcs = ["date_time_util_internal_test.cc"],
    deps = [
        ":date_time_util_internal",
        "//googlesql/base/testing:googlesql_gtest_main",
        "@com_google_absl//absl/time",
    ],
)

cc_library(
    name = "date_time_util",
    srcs = ["date_time_util.cc"],
    hdrs = ["date_time_util.h"],
    deps = [
        ":arithmetics",
        ":date_time_util_internal",
        ":datetime_cc_proto",
        "//googlesql/base",
        "//googlesql/base:mathutil",
        "//googlesql/base:ret_check",
        "//googlesql/base:status",
        "//googlesql/base:time_proto_util",
        "//googlesql/common:errors",
        "//googlesql/public:civil_time",
        "//googlesql/public:interval_value",
        "//googlesql/public:pico_time",
        "//googlesql/public:time_zone_util",
        "//googlesql/public/proto:type_annotation_cc_proto",
        "//googlesql/public/types:timestamp_util",
        "@com_google_absl//absl/base:core_headers",
        "@com_google_absl//absl/base:no_destructor",
        "@com_google_absl//absl/container:flat_hash_map",
        "@com_google_absl//absl/numeric:int128",
        "@com_google_absl//absl/status",
        "@com_google_absl//absl/status:statusor",
        "@com_google_absl//absl/strings",
        "@com_google_absl//absl/strings:str_format",
        "@com_google_absl//absl/time",
        "@com_google_googleapis//google/type:date_cc_proto",
        "@com_google_protobuf//:protobuf",
    ],
)

cc_library(
    name = "parse_date_time_utils",
    srcs = ["parse_date_time_utils.cc"],
    hdrs = ["parse_date_time_utils.h"],
    deps = [
        ":date_time_util",
        "@com_google_absl//absl/base:core_headers",
        "@com_google_absl//absl/time",
    ],
)

cc_library(
    name = "parse_date_time",
    srcs = ["parse_date_time.cc"],
    hdrs = ["parse_date_time.h"],
    deps = [
        ":date_time_util",
        ":date_time_util_internal",
        ":datetime_cc_proto",
        ":parse_date_time_utils",
        "//googlesql/base",
        "//googlesql/base:ret_check",
        "//googlesql/base:status",
        "//googlesql/common:errors",
        "//googlesql/public:civil_time",
        "//googlesql/public:pico_time",
        "//googlesql/public:strings",
        "//googlesql/public/types:timestamp_util",
        "@com_google_absl//absl/base:nullability",
        "@com_google_absl//absl/status",
        "@com_google_absl//absl/strings",
        "@com_google_absl//absl/strings:str_format",
        "@com_google_absl//absl/time",
    ],
)

cc_library(
    name = "cast_date_time",
    srcs = ["cast_date_time.cc"],
    hdrs = ["cast_date_time.h"],
    deps = [
        ":date_time_util",
        ":datetime_cc_proto",
        ":input_format_string_max_width",
        ":parse_date_time_utils",
        "//googlesql/base",
        "//googlesql/base:general_trie",
        "//googlesql/base:map_util",
        "//googlesql/base:mathutil",
        "//googlesql/base:ret_check",
        "//googlesql/base:status",
        "//googlesql/common:errors",
        "//googlesql/common:utf_util",
        "//googlesql/public:civil_time",
        "//googlesql/public:pico_time",
        "//googlesql/public:strings",
        "//googlesql/public:type",
        "//googlesql/public:type_cc_proto",
        "@com_google_absl//absl/base:core_headers",
        "@com_google_absl//absl/container:flat_hash_map",
        "@com_google_absl//absl/flags:flag",
        "@com_google_absl//absl/status",
        "@com_google_absl//absl/status:statusor",
        "@com_google_absl//absl/strings",
        "@com_google_absl//absl/strings:str_format",
        "@com_google_absl//absl/time",
        "@com_google_absl//absl/types:span",
        "@icu//:headers",
    ],
)

cc_library(
    name = "math",
    srcs = ["math.cc"],
    hdrs = ["math.h"],
    deps = [
        ":arithmetics",
        ":rounding_mode_cc_proto",
        ":util",
        "//googlesql/base:status",
        "//googlesql/common:multiprecision_int",
        "//googlesql/public:numeric_value",
        "@com_google_absl//absl/base:core_headers",
        "@com_google_absl//absl/status",
        "@com_google_absl//absl/status:statusor",
        "@com_google_absl//absl/strings",
        "@com_google_absl//absl/strings:str_format",
    ],
)

cc_test(
    name = "math_test",
    size = "small",
    srcs = ["math_test.cc"],
    deps = [
        ":math",
        ":rounding_mode_cc_proto",
        "//googlesql/base",
        "//googlesql/base:status",
        "//googlesql/base/testing:googlesql_gtest_main",
        "//googlesql/base/testing:status_matchers",
        "//googlesql/common:float_margin",
        "//googlesql/compliance:functions_testlib",
        "//googlesql/public:numeric_value",
        "//googlesql/public:type",
        "//googlesql/public:type_cc_proto",
        "//googlesql/public:value",
        "//googlesql/testing:test_function",
        "@com_google_absl//absl/status",
        "@com_google_absl//absl/strings",
    ],
)

cc_library(
    name = "net",
    srcs = ["net.cc"],
    hdrs = ["net.h"],
    deps = [
        ":util",
        "//googlesql/base:status",
        "//googlesql/base/net:idn",
        "//googlesql/base/net:ipaddress",
        "//googlesql/base/net:public_suffix",
        "@com_google_absl//absl/base:core_headers",
        "@com_google_absl//absl/functional:function_ref",
        "@com_google_absl//absl/status",
        "@com_google_absl//absl/strings",
    ],
)

cc_test(
    name = "net_test",
    size = "small",
    srcs = ["net_test.cc"],
    deps = [
        ":net",
        "//googlesql/base:status",
        "//googlesql/base/testing:googlesql_gtest_main",
        "//googlesql/base/testing:status_matchers",
        "//googlesql/compliance:functions_testlib",
        "//googlesql/public:type",
        "//googlesql/public:value",
        "//googlesql/testing:test_function",
        "@com_google_absl//absl/status",
        "@com_google_absl//absl/strings:string_view",
        "@com_googlesource_code_re2//:re2",
    ],
)

cc_library(
    name = "numeric",
    srcs = ["numeric.cc"],
    hdrs = ["numeric.h"],
    deps = [
        ":util",
        "//googlesql/base",
        "//googlesql/public:numeric_value",
        "@com_google_absl//absl/status",
        "@com_google_absl//absl/strings",
    ],
)

cc_test(
    name = "numeric_test",
    size = "small",
    srcs = ["numeric_test.cc"],
    deps = [
        ":numeric",
        "//googlesql/base/testing:googlesql_gtest_main",
        "//googlesql/base/testing:status_matchers",
        "//googlesql/compliance:functions_testlib",
        "//googlesql/public:numeric_value",
        "//googlesql/public:value",
        "//googlesql/testing:test_function",
        "@com_google_absl//absl/status",
    ],
)

cc_library(
    name = "string",
    srcs = ["string.cc"],
    hdrs = ["string.h"],
    deps = [
        ":normalize_mode_cc_proto",
        ":util",
        "//googlesql/base",
        "//googlesql/base:check",
        "//googlesql/base:map_util",
        "//googlesql/base:status",
        "//googlesql/base:stl_util",
        "//googlesql/base:strings",
        "//googlesql/common:utf_util",
        "//googlesql/public:strings",
        "@com_google_absl//absl/algorithm:container",
        "@com_google_absl//absl/base",
        "@com_google_absl//absl/base:core_headers",
        "@com_google_absl//absl/base:no_destructor",
        "@com_google_absl//absl/container:flat_hash_map",
        "@com_google_absl//absl/container:flat_hash_set",
        "@com_google_absl//absl/status",
        "@com_google_absl//absl/strings",
        "@com_google_absl//absl/strings:str_format",
        "@com_google_absl//absl/types:span",
    ],
)

cc_library(
    name = "string_with_collation",
    srcs = ["string_with_collation.cc"],
    hdrs = ["string_with_collation.h"],
    deps = [
        ":like",
        ":string",
        ":util",
        "//googlesql/base:check",
        "//googlesql/base:status",
        "//googlesql/common:utf_util",
        "//googlesql/public:collator_lite",
        "//googlesql/public:type_cc_proto",
        "@com_google_absl//absl/base:core_headers",
        "@com_google_absl//absl/memory",
        "@com_google_absl//absl/status",
        "@com_google_absl//absl/status:statusor",
        "@com_google_absl//absl/strings",
        "@com_googlesource_code_re2//:re2",
    ],
)

cc_library(
    name = "uuid",
    srcs = ["uuid.cc"],
    hdrs = ["uuid.h"],
    deps = [
        "//googlesql/base:status",
        "//googlesql/public:uuid_value",
        "@com_google_absl//absl/random",
        "@com_google_absl//absl/random:bit_gen_ref",
        "@com_google_absl//absl/random:distributions",
        "@com_google_absl//absl/strings:string_view",
    ],
)

cc_test(
    name = "uuid_test",
    size = "small",
    srcs = ["uuid_test.cc"],
    deps = [
        ":uuid",
        "//googlesql/base",
        "//googlesql/base/testing:googlesql_gtest_main",
        "@com_google_absl//absl/random",
        "@com_google_absl//absl/random:mock_distributions",
        "@com_google_absl//absl/random:mocking_bit_gen",
    ],
)

cc_test(
    name = "string_test",
    size = "small",
    srcs = ["string_test.cc"],
    deps = [
        ":normalize_mode_cc_proto",
        ":string",
        "//googlesql/base",
        "//googlesql/base/testing:googlesql_gtest_main",
        "//googlesql/base/testing:status_matchers",
        "//googlesql/compliance:functions_testlib",
        "//googlesql/public:type",
        "//googlesql/public:type_cc_proto",
        "//googlesql/public:value",
        "//googlesql/testing:test_function",
        "@com_google_absl//absl/status",
        "@com_google_absl//absl/strings",
    ],
)

cc_test(
    name = "string_with_collation_test",
    size = "small",
    srcs = ["string_with_collation_test.cc"],
    deps = [
        ":string_with_collation",
        "//googlesql/base/testing:googlesql_gtest_main",
        "//googlesql/base/testing:status_matchers",
        "//googlesql/compliance:functions_testlib",
        "//googlesql/public:collator",  # buildcleaner: keep
        "//googlesql/public:type_cc_proto",
        "//googlesql/public:value",
        "//googlesql/public/types",
        "//googlesql/testing:test_function",
        "@com_google_absl//absl/status",
        "@com_google_absl//absl/strings",
    ],
)

cc_library(
    name = "regexp",
    srcs = ["regexp.cc"],
    hdrs = ["regexp.h"],
    deps = [
        ":string",
        ":util",
        "//googlesql/base",
        "//googlesql/base:ret_check",
        "//googlesql/base:status",
        "//googlesql/base:strings",
        "//googlesql/common:utf_util",
        "//googlesql/public:options_cc_proto",
        "//googlesql/public:type_cc_proto",
        "//googlesql/public:value",
        "//googlesql/public/types",
        "@com_google_absl//absl/base:core_headers",
        "@com_google_absl//absl/container:flat_hash_set",
        "@com_google_absl//absl/memory",
        "@com_google_absl//absl/status",
        "@com_google_absl//absl/status:statusor",
        "@com_google_absl//absl/strings",
        "@com_google_absl//absl/types:span",
        "@com_googlesource_code_re2//:re2",
        "@icu//:headers",
    ],
)

cc_test(
    name = "regexp_test",
    size = "small",
    srcs = ["regexp_test.cc"],
    deps = [
        ":regexp",
        "//googlesql/base/testing:googlesql_gtest_main",
        "//googlesql/base/testing:status_matchers",
        "//googlesql/compliance:functions_testlib",
        "//googlesql/public:type",
        "//googlesql/public:type_cc_proto",
        "//googlesql/public:value",
        "//googlesql/testing:test_function",
        "@com_google_absl//absl/status",
        "@com_google_absl//absl/status:statusor",
        "@com_google_absl//absl/strings:string_view",
        "@com_googlesource_code_re2//:re2",
    ],
)

proto_library(
    name = "datetime_proto",
    srcs = ["datetime.proto"],
)

cc_proto_library(
    name = "datetime_cc_proto",
    deps = ["datetime_proto"],
)

java_proto_library(
    name = "datetime_java_proto",
    deps = [":datetime_proto"],
)

proto_library(
    name = "normalize_mode_proto",
    srcs = ["normalize_mode.proto"],
)

cc_proto_library(
    name = "normalize_mode_cc_proto",
    deps = ["normalize_mode_proto"],
)

java_proto_library(
    name = "normalize_mode_java_proto",
    deps = [":normalize_mode_proto"],
)

proto_library(
    name = "rounding_mode_proto",
    srcs = ["rounding_mode.proto"],
    deps = [
        "//googlesql/public:type_proto",
    ],
)

cc_proto_library(
    name = "rounding_mode_cc_proto",
    deps = ["rounding_mode_proto"],
)

java_proto_library(
    name = "rounding_mode_java_proto",
    deps = [":rounding_mode_proto"],
)

proto_library(
    name = "array_find_mode_proto",
    srcs = ["array_find_mode.proto"],
    deps = ["//googlesql/public:type_proto"],
)

java_proto_library(
    name = "array_find_mode_java_proto",
    deps = [":array_find_mode_proto"],
)

cc_proto_library(
    name = "array_find_mode_cc_proto",
    deps = [":array_find_mode_proto"],
)

proto_library(
    name = "bitwise_agg_mode_proto",
    srcs = ["bitwise_agg_mode.proto"],
    deps = ["//googlesql/public:type_proto"],
)

java_proto_library(
    name = "bitwise_agg_mode_java_proto",
    deps = [":bitwise_agg_mode_proto"],
)

cc_proto_library(
    name = "bitwise_agg_mode_cc_proto",
    deps = [":bitwise_agg_mode_proto"],
)

proto_library(
    name = "unsupported_fields_proto",
    srcs = ["unsupported_fields.proto"],
    deps = ["//googlesql/public:type_proto"],
)

cc_proto_library(
    name = "unsupported_fields_cc_proto",
    deps = [":unsupported_fields_proto"],
)

java_proto_library(
    name = "unsupported_fields_java_proto",
    deps = [":unsupported_fields_proto"],
)

proto_library(
    name = "differential_privacy_proto",
    srcs = ["differential_privacy.proto"],
    deps = [
        "//googlesql/proto:anon_output_with_report_proto",
        "//googlesql/public:type_proto",
    ],
)

java_proto_library(
    name = "differential_privacy_java_proto",
    deps = [":differential_privacy_proto"],
)

cc_proto_library(
    name = "differential_privacy_cc_proto",
    deps = [":differential_privacy_proto"],
)

proto_library(
    name = "array_zip_mode_proto",
    srcs = ["array_zip_mode.proto"],
    deps = ["//googlesql/public:type_proto"],
)

cc_proto_library(
    name = "array_zip_mode_cc_proto",
    deps = [":array_zip_mode_proto"],
)

java_proto_library(
    name = "array_zip_mode_java_proto",
    deps = [":array_zip_mode_proto"],
)

proto_library(
    name = "range_sessionize_mode_proto",
    srcs = ["range_sessionize_mode.proto"],
    deps = ["//googlesql/public:type_proto"],
)

java_proto_library(
    name = "range_sessionize_mode_java_proto",
    deps = [":range_sessionize_mode_proto"],
)

cc_proto_library(
    name = "range_sessionize_mode_cc_proto",
    deps = [":range_sessionize_mode_proto"],
)

cc_library(
    name = "json_internal",
    srcs = ["json_internal.cc"],
    hdrs = ["json_internal.h"],
    deps = [
        "//googlesql/base:case",
        "//googlesql/base:check",
        "//googlesql/base:status",
        "//googlesql/base:strings",
        "//googlesql/common:json_parser",
        "//googlesql/common:json_util",
        "//googlesql/common:thread_stack",
        "@com_google_absl//absl/base:core_headers",
        "@com_google_absl//absl/memory",
        "@com_google_absl//absl/status",
        "@com_google_absl//absl/status:statusor",
        "@com_google_absl//absl/strings",
        "@com_googlesource_code_re2//:re2",
    ],
)

cc_library(
    name = "json",
    srcs = ["json.cc"],
    hdrs = ["json.h"],
    deps = [
        ":convert",
        ":convert_string",
        ":json_format",
        ":json_internal",
        ":to_json",
        "//googlesql/base:lossless_convert",
        "//googlesql/base:ret_check",
        "//googlesql/base:status",
        "//googlesql/base:strings",
        "//googlesql/common:errors",
        "//googlesql/common:json_util",
        "//googlesql/public:json_value",
        "//googlesql/public:language_options",
        "//googlesql/public:numeric_value",
        "//googlesql/public:value",
        "@com_google_absl//absl/base:core_headers",
        "@com_google_absl//absl/container:btree",
        "@com_google_absl//absl/container:flat_hash_set",
        "@com_google_absl//absl/container:inlined_vector",
        "@com_google_absl//absl/functional:function_ref",
        "@com_google_absl//absl/memory",
        "@com_google_absl//absl/status",
        "@com_google_absl//absl/status:statusor",
        "@com_google_absl//absl/strings",
        "@com_google_absl//absl/types:span",
        "@com_googlesource_code_re2//:re2",
    ],
)

cc_test(
    name = "json_test",
    srcs = ["json_test.cc"],
    deps = [
        ":json",
        ":json_internal",
        "//googlesql/base",
        "//googlesql/base/testing:googlesql_gtest_main",
        "//googlesql/base/testing:status_matchers",
        "//googlesql/compliance:functions_testlib",
        "//googlesql/public:json_value",
        "//googlesql/public:language_options",
        "//googlesql/public:numeric_value",
        "//googlesql/public:options_cc_proto",
        "//googlesql/public:value",
        "//googlesql/public/types",
        "//googlesql/testing:test_function",
        "@com_google_absl//absl/status",
        "@com_google_absl//absl/status:statusor",
        "@com_google_absl//absl/strings",
        "@com_google_absl//absl/strings:str_format",
        "@com_google_absl//absl/types:span",
    ],
)

cc_library(
    name = "distance",
    srcs = ["distance.cc"],
    hdrs = ["distance.h"],
    deps = [
        ":arithmetics",
        ":math",
        "//googlesql/base:edit_distance",
        "//googlesql/base:ret_check",
        "//googlesql/base:status",
        "//googlesql/public:value",
        "//googlesql/public/types",
        "@com_google_absl//absl/container:btree",
        "@com_google_absl//absl/container:flat_hash_map",
        "@com_google_absl//absl/functional:function_ref",
        "@com_google_absl//absl/status",
        "@com_google_absl//absl/status:statusor",
        "@com_google_absl//absl/strings",
        "@com_google_absl//absl/types:span",
        "@icu//:headers",
    ],
)

cc_test(
    name = "distance_test",
    srcs = ["distance_test.cc"],
    deps = [
        ":distance",
        "//googlesql/base:check",
        "//googlesql/base/testing:googlesql_gtest_main",
        "//googlesql/base/testing:status_matchers",
        "//googlesql/public:value",
        "//googlesql/public/types",
        "@com_google_absl//absl/log",
        "@com_google_absl//absl/random",
        "@com_google_absl//absl/random:bit_gen_ref",
        "@com_google_absl//absl/random:distributions",
        "@com_google_absl//absl/status",
        "@com_google_absl//absl/strings",
        "@com_google_absl//absl/types:span",
    ],
)

cc_library(
    name = "generate_array",
    hdrs = ["generate_array.h"],
    deps = [
        ":arithmetics",
        ":date_time_util",
        ":datetime_cc_proto",
        "//googlesql/base:source_location",
        "//googlesql/base:status",
        "//googlesql/public:numeric_value",
        "//googlesql/public:pico_time",
        "@com_google_absl//absl/status",
        "@com_google_absl//absl/status:statusor",
        "@com_google_absl//absl/time",
    ],
)

cc_test(
    name = "generate_array_test",
    srcs = ["generate_array_test.cc"],
    deps = [
        ":generate_array",
        "//googlesql/base:status",
        "//googlesql/base/testing:googlesql_gtest_main",
        "//googlesql/base/testing:status_matchers",
        "//googlesql/common:float_margin",
        "//googlesql/common:internal_value",
        "//googlesql/compliance:functions_testlib",
        "//googlesql/public:numeric_value",
        "//googlesql/public:type_cc_proto",
        "//googlesql/public:value",
        "//googlesql/testing:test_function",
        "@com_google_absl//absl/strings",
    ],
)

cc_library(
    name = "common_proto",
    srcs = ["common_proto.cc"],
    hdrs = ["common_proto.h"],
    deps = [
        ":date_time_util",
        "//googlesql/base:status",
        "//googlesql/common:errors",
        "//googlesql/common:utf_util",
        "//googlesql/public:civil_time",
        "@com_google_absl//absl/status:statusor",
        "@com_google_absl//absl/strings",
        "@com_google_absl//absl/strings:cord",
        "@com_google_googleapis//google/type:latlng_cc_proto",
        "@com_google_googleapis//google/type:timeofday_cc_proto",
        "@com_google_protobuf//:protobuf",
    ],
)

cc_test(
    name = "common_proto_test",
    srcs = ["common_proto_test.cc"],
    deps = [
        ":common_proto",
        ":date_time_util",
        "//googlesql/base:status",
        "//googlesql/base/testing:googlesql_gtest_main",
        "//googlesql/base/testing:status_matchers",
        "//googlesql/public:civil_time",
        "@com_google_absl//absl/status",
        "@com_google_absl//absl/strings",
        "@com_google_absl//absl/strings:cord",
        "@com_google_googleapis//google/type:timeofday_cc_proto",
        "@com_google_protobuf//:protobuf",
    ],
)

proto_library(
    name = "rank_type_proto",
    srcs = ["rank_type.proto"],
    deps = ["//googlesql/public:type_proto"],
)

cc_proto_library(
    name = "rank_type_cc_proto",
    deps = [":rank_type_proto"],
)

java_proto_library(
    name = "rank_type_java_proto",
    deps = [":rank_type_proto"],
)

cc_library(
    name = "hash",
    srcs = ["hash.cc"],
    hdrs = ["hash.h"],
    deps = [
        "//googlesql/base",
        "//googlesql/base:check",
        "//googlesql/base:endian",
        "@boringssl//:crypto",
        "@com_google_absl//absl/base",
        "@com_google_absl//absl/base:core_headers",
        "@com_google_absl//absl/numeric:int128",
        "@com_google_absl//absl/status",
        "@com_google_absl//absl/status:statusor",
        "@com_google_absl//absl/strings",
        "@com_google_farmhash//:farmhash_fingerprint",
    ],
)

cc_test(
    name = "hash_test",
    srcs = ["hash_test.cc"],
    deps = [
        ":hash",
        "//googlesql/base/testing:googlesql_gtest_main",
        "//googlesql/compliance:functions_testlib",
        "//googlesql/public:type",
        "//googlesql/public:value",
        "//googlesql/testing:test_function",
        "@com_google_absl//absl/strings",
    ],
)

cc_library(
    name = "json_format",
    srcs = ["json_format.cc"],
    hdrs = ["json_format.h"],
    deps = [
        ":date_time_util",
        "//googlesql/base:check",
        "//googlesql/base:ret_check",
        "//googlesql/base:status",
        "//googlesql/common:errors",
        "//googlesql/common:json_util",
        "//googlesql/common:string_util",
        "//googlesql/public:civil_time",
        "//googlesql/public:interval_value",
        "//googlesql/public:json_value",
        "//googlesql/public:numeric_value",
        "//googlesql/public:options_cc_proto",
        "//googlesql/public:timestamp_picos_value",
        "//googlesql/public:type",
        "//googlesql/public:type_cc_proto",
        "//googlesql/public:uuid_value",
        "//googlesql/public:value",
        "@com_google_absl//absl/status",
        "@com_google_absl//absl/strings",
        "@com_google_absl//absl/time",
        "@com_google_protobuf//:protobuf",
    ],
)

cc_test(
    name = "json_format_test",
    srcs = ["json_format_test.cc"],
    deps = [
        ":date_time_util",
        ":json_format",
        "//googlesql/base:check",
        "//googlesql/base:map_util",
        "//googlesql/base/testing:googlesql_gtest_main",
        "//googlesql/base/testing:status_matchers",
        "//googlesql/compliance:functions_testlib",
        "//googlesql/public:json_value",
        "//googlesql/public:options_cc_proto",
        "//googlesql/public:pico_time",
        "//googlesql/public:timestamp_picos_value",
        "//googlesql/public:type",
        "//googlesql/public:value",
        "//googlesql/testing:test_function",
        "@com_google_absl//absl/status",
        "@com_google_absl//absl/strings",
        "@com_google_absl//absl/time",
    ],
)

cc_library(
    name = "to_json",
    srcs = ["to_json.cc"],
    hdrs = ["to_json.h"],
    deps = [
        ":json_format",
        ":unsupported_fields_cc_proto",
        "//googlesql/base:logging",
        "//googlesql/base:ret_check",
        "//googlesql/base:status",
        "//googlesql/common:errors",
        "//googlesql/common:thread_stack",
        "//googlesql/public:interval_value",
        "//googlesql/public:json_value",
        "//googlesql/public:language_options",
        "//googlesql/public:options_cc_proto",
        "//googlesql/public:type",
        "//googlesql/public:type_cc_proto",
        "//googlesql/public:uuid_value",
        "//googlesql/public:value",
        "//googlesql/public/types",
        "@com_google_absl//absl/status",
        "@com_google_absl//absl/status:statusor",
        "@com_google_absl//absl/strings",
        "@com_google_absl//absl/types:span",
        "@com_google_protobuf//:protobuf",
    ],
)

cc_test(
    name = "to_json_test",
    srcs = ["to_json_test.cc"],
    deps = [
        ":date_time_util",
        ":to_json",
        ":unsupported_fields_cc_proto",
        "//googlesql/base:map_util",
        "//googlesql/base/testing:googlesql_gtest_main",
        "//googlesql/base/testing:status_matchers",
        "//googlesql/common:float_margin",
        "//googlesql/common:internal_value",
        "//googlesql/compliance:functions_testlib",
        "//googlesql/public:json_value",
        "//googlesql/public:language_options",
        "//googlesql/public:options_cc_proto",
        "//googlesql/public:pico_time",
        "//googlesql/public:simple_token_list",
        "//googlesql/public:timestamp_picos_value",
        "//googlesql/public:value",
        "//googlesql/public/types",
        "//googlesql/testing:test_function",
        "//googlesql/testing:test_value",
        "@com_google_absl//absl/status",
        "@com_google_absl//absl/status:statusor",
        "@com_google_absl//absl/strings",
        "@com_google_absl//absl/time",
        "@com_google_protobuf//:protobuf",
    ],
)

cc_library(
    name = "percentile",
    srcs = ["percentile.cc"],
    hdrs = ["percentile.h"],
    deps = [
        "//googlesql/base",
        "//googlesql/base:check",
        "//googlesql/base:mathutil",
        "//googlesql/base:ret_check",
        "//googlesql/base:status",
        "//googlesql/common:multiprecision_int",
        "//googlesql/public:collator",
        "//googlesql/public:numeric_value",
        "@com_google_absl//absl/base:core_headers",
        "@com_google_absl//absl/status",
        "@com_google_absl//absl/status:statusor",
        "@com_google_absl//absl/strings",
    ],
)

cc_test(
    name = "percentile_test",
    size = "small",
    srcs = ["percentile_test.cc"],
    deps = [
        ":percentile",
        "//googlesql/base/testing:googlesql_gtest_main",
        "//googlesql/base/testing:status_matchers",
        "//googlesql/common:string_util",
        "//googlesql/public:numeric_value",
        "@com_google_absl//absl/status",
        "@com_google_absl//absl/strings",
        "@com_google_absl//absl/types:span",
    ],
)

cc_library(
    name = "range",
    srcs = ["range.cc"],
    hdrs = ["range.h"],
    deps = [
        ":date_time_util",
        ":datetime_cc_proto",
        "//googlesql/base:case",
        "//googlesql/base:endian",
        "//googlesql/base:ret_check",
        "//googlesql/base:status",
        "//googlesql/common:errors",
        "//googlesql/public:civil_time",
        "//googlesql/public:interval_value",
        "//googlesql/public:type_cc_proto",
        "//googlesql/public:value",
        "//googlesql/public/types",
        "@com_google_absl//absl/base:core_headers",
        "@com_google_absl//absl/status",
        "@com_google_absl//absl/status:statusor",
        "@com_google_absl//absl/strings",
        "@com_google_absl//absl/strings:str_format",
        "@com_google_absl//absl/time",
    ],
)

cc_test(
    name = "range_test",
    srcs = ["range_test.cc"],
    deps = [
        ":date_time_util",
        ":range",
        "//googlesql/base:map_util",
        "//googlesql/base:status",
        "//googlesql/base/testing:googlesql_gtest_main",
        "//googlesql/base/testing:status_matchers",
        "//googlesql/compliance:functions_testlib",
        "//googlesql/public:civil_time",
        "//googlesql/public:interval_value",
        "//googlesql/public:options_cc_proto",
        "//googlesql/public:type",
        "//googlesql/public:value",
        "//googlesql/public/types",
        "//googlesql/testing:test_function",
        "//googlesql/testing:test_value",
        "@com_google_absl//absl/status",
        "@com_google_absl//absl/status:statusor",
        "@com_google_absl//absl/strings",
        "@com_google_absl//absl/strings:str_format",
        "@com_google_absl//absl/time",
    ],
)

cc_library(
    name = "like",
    srcs = ["like.cc"],
    hdrs = ["like.h"],
    deps = [
        "//googlesql/base",
        "//googlesql/base:status",
        "//googlesql/public:type_cc_proto",
        "@com_google_absl//absl/base:core_headers",
        "@com_google_absl//absl/memory",
        "@com_google_absl//absl/status:statusor",
        "@com_google_absl//absl/strings",
        "@com_googlesource_code_re2//:re2",
    ],
)

cc_test(
    name = "like_test",
    size = "small",
    srcs = ["like_test.cc"],
    deps = [
        ":like",
        "//googlesql/base:status",
        "//googlesql/base/testing:googlesql_gtest_main",
        "@com_google_absl//absl/strings",
        "@com_googlesource_code_re2//:re2",
    ],
)
