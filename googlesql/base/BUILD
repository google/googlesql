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

load("@com_google_protobuf//bazel:proto_library.bzl", "proto_library")
load("@rules_cc//cc:cc_library.bzl", "cc_library")
load("@rules_cc//cc:cc_test.bzl", "cc_test")

licenses(["notice"])

package(default_visibility = [":googlesql_implementation"])

package_group(
    name = "googlesql_implementation",
    packages = [
        "//googlesql/...",
        "//java/...",
        "//javatests/...",
    ],
)

cc_library(
    name = "base",
    hdrs = [
        "atomic_sequence_num.h",
        "logging.h",
        "varsetter.h",
    ],
    deps = [
        ":logging",
        "@com_google_absl//absl/base:core_headers",
        "@com_google_absl//absl/base:log_severity",
        "@com_google_absl//absl/log:absl_check",
        "@com_google_absl//absl/log:absl_log",
    ],
)

cc_library(
    name = "varsetter",
    hdrs = [
        "varsetter.h",
    ],
)

cc_library(
    name = "logging",
    srcs = ["logging.cc"],
    hdrs = ["logging.h"],
    deps = [
        "@com_google_absl//absl/base:core_headers",
        "@com_google_absl//absl/base:log_severity",
        "@com_google_absl//absl/log:absl_check",
        "@com_google_absl//absl/log:absl_log",
    ],
)

cc_library(
    name = "check",
    hdrs = ["check.h"],
    deps = [
        ":logging",
        ":status",
    ],
)

cc_library(
    name = "die_if_null",
    hdrs = ["die_if_null.h"],
    deps = [
        ":check",
    ],
)

cc_library(
    name = "arena",
    srcs = ["arena.cc"],
    hdrs = [
        "arena.h",
    ],
    deps = [
        ":logging",
        "@com_google_absl//absl/base:core_headers",
        "@com_google_absl//absl/synchronization",
    ],
)

cc_library(
    name = "arena_allocator",
    srcs = ["arena_allocator.cc"],
    hdrs = [
        "arena_allocator.h",
    ],
    deps = [
        ":arena",
        ":logging",
        "@com_google_absl//absl/base:core_headers",
    ],
)

cc_test(
    name = "arena_test",
    srcs = ["arena_unittest.cc"],
    deps = [
        ":arena",
        ":arena_allocator",
        ":logging",
        "//googlesql/base/testing:googlesql_gtest_main",
        "@com_google_absl//absl/container:node_hash_set",
        "@com_google_absl//absl/strings",
    ],
)

cc_test(
    name = "arena_leakage_unittest",
    size = "small",
    srcs = ["arena_leakage_unittest.cc"],
    deps = [
        ":arena",
        "//googlesql/base/testing:googlesql_gtest_main",
    ],
)

cc_library(
    name = "source_location",
    hdrs = ["source_location.h"],
    deps = ["@com_google_absl//absl/base:config"],
)

cc_test(
    name = "source_location_test",
    srcs = ["source_location_test.cc"],
    deps = [
        ":source_location",
        "//googlesql/base/testing:googlesql_gtest_main",
    ],
)

cc_library(
    name = "endian",
    hdrs = [
        "endian.h",
        "unaligned_access.h",
    ],
    deps = [
        ":logging",
        "@com_google_absl//absl/base:config",
        "@com_google_absl//absl/base:core_headers",
        "@com_google_absl//absl/numeric:int128",
    ],
)

cc_test(
    name = "endian_test",
    srcs = ["endian_test.cc"],
    deps = [
        ":endian",
        "//googlesql/base/testing:googlesql_gtest_main",
        "@com_google_absl//absl/base:config",
        "@com_google_absl//absl/numeric:int128",
    ],
)

cc_library(
    name = "status",
    srcs = [
        "status_builder.cc",
        "status_payload.cc",
    ],
    hdrs = [
        "canonical_errors.h",
        "status.h",
        "status_builder.h",
        "status_macros.h",
        "status_payload.h",
    ],
    visibility = ["//visibility:public"],
    deps = [
        ":logging",
        ":source_location",
        "@com_google_absl//absl/base:core_headers",
        "@com_google_absl//absl/base:log_severity",
        "@com_google_absl//absl/log",
        "@com_google_absl//absl/status",
        "@com_google_absl//absl/status:statusor",
        "@com_google_absl//absl/strings",
        "@com_google_absl//absl/strings:cord",
    ],
)

cc_test(
    name = "status_test",
    srcs = ["status_test.cc"],
    deps = [
        "//googlesql/base/testing:googlesql_gtest_main",
        "@com_google_absl//absl/status",
    ],
)

cc_test(
    name = "status_payload_test",
    srcs = ["status_payload_test.cc"],
    deps = [
        ":status",
        ":test_payload_cc_proto",
        "//googlesql/base/testing:googlesql_gtest_main",
        "@com_google_absl//absl/base:core_headers",
        "@com_google_absl//absl/status",
        "@com_google_absl//absl/strings",
        "@com_google_protobuf//:protobuf",
    ],
)

cc_library(
    name = "enum_utils",
    hdrs = ["enum_utils.h"],
    deps = [
        "@com_google_protobuf//:protobuf",
    ],
)

cc_test(
    name = "enum_utils_test",
    srcs = [
        "enum_utils_test.cc",
    ],
    deps = [
        ":enum_utils",
        ":test_payload_cc_proto",
        "//googlesql/base/testing:googlesql_gtest_main",
    ],
)

cc_library(
    name = "ret_check",
    srcs = ["ret_check.cc"],
    hdrs = [
        "ret_check.h",
    ],
    deps = [
        ":logging",
        ":source_location",
        ":status",
        "@com_google_absl//absl/status",
    ],
)

cc_test(
    name = "status_macros_test",
    srcs = ["status_macros_test.cc"],
    deps = [
        ":source_location",
        ":status",
        "//googlesql/base/testing:googlesql_gtest_main",
        "@com_google_absl//absl/base:core_headers",
        "@com_google_absl//absl/memory",
        "@com_google_absl//absl/status",
        "@com_google_absl//absl/status:statusor",
        "@com_google_absl//absl/strings",
    ],
)

cc_library(
    name = "no_destructor",
    hdrs = ["no_destructor.h"],
)

cc_library(
    name = "map_util",
    hdrs = [
        "map_traits.h",
        "map_util.h",
    ],
    deps = [
        ":logging",
        ":no_destructor",
        "@com_google_absl//absl/meta:type_traits",
    ],
)

cc_test(
    name = "map_util_unittest",
    srcs = [
        "map_util_test.h",
        "map_util_unittest.cc",
    ],
    deps = [
        ":logging",
        ":map_util",
        "//googlesql/base/testing:googlesql_gtest_main",
        "@com_google_absl//absl/container:btree",
        "@com_google_absl//absl/container:flat_hash_map",
        "@com_google_absl//absl/container:node_hash_map",
    ],
)

cc_library(
    name = "map_view",
    hdrs = [
        "map_view.h",
    ],
    deps = [
        ":associative_view_internal",
        "@com_google_absl//absl/base:core_headers",
    ],
)

cc_test(
    name = "map_view_test",
    srcs = ["map_view_test.cc"],
    deps = [
        ":map_view",
        "//googlesql/base/testing:googlesql_gtest_main",
        "@com_google_absl//absl/algorithm:container",
        "@com_google_absl//absl/base:core_headers",
        "@com_google_absl//absl/container:flat_hash_map",
        "@com_google_absl//absl/random",
        "@com_google_absl//absl/strings",
        "@com_google_absl//absl/strings:cord",
    ],
)

cc_library(
    name = "associative_view_internal",
    hdrs = [
        "associative_view_internal.h",
    ],
    deps = [
        ":requires",
        "@com_google_absl//absl/base:core_headers",
        "@com_google_absl//absl/strings",
        "@com_google_absl//absl/strings:cord",
    ],
)

cc_test(
    name = "associative_view_internal_test",
    srcs = ["associative_view_internal_test.cc"],
    deps = [
        ":associative_view_internal",
        "//googlesql/base/testing:googlesql_gtest_main",
    ],
)

cc_library(
    name = "requires",
    hdrs = [
        "requires.h",
    ],
)

cc_test(
    name = "requires_test",
    srcs = ["requires_test.cc"],
    deps = [
        ":requires",
        "//googlesql/base/testing:googlesql_gtest_main",
    ],
)

cc_library(
    name = "stl_util",
    hdrs = [
        "stl_util.h",
    ],
)

cc_test(
    name = "stl_util_test",
    srcs = [
        "stl_util_test.cc",
    ],
    deps = [
        ":stl_util",
        "//googlesql/base/testing:googlesql_gtest_main",
        "@com_google_absl//absl/base:core_headers",
    ],
)

cc_test(
    name = "no_destructor_test",
    size = "small",
    srcs = ["no_destructor_test.cc"],
    deps = [
        ":logging",
        ":no_destructor",
        "//googlesql/base/testing:googlesql_gtest_main",
    ],
)

cc_test(
    name = "map_traits_test",
    srcs = ["map_traits_test.cc"],
    deps = [
        ":map_util",
        "//googlesql/base/testing:googlesql_gtest_main",
        "@com_google_absl//absl/container:node_hash_map",
    ],
)

cc_library(
    name = "mathutil",
    srcs = [
        "mathutil.cc",
    ],
    hdrs = [
        "mathutil.h",
    ],
    deps = [
        ":bits",
        ":check",
        "@com_google_absl//absl/base",
    ],
)

cc_test(
    name = "mathutil_test",
    srcs = [
        "mathutil_test.cc",
    ],
    deps = [
        ":mathutil",
        "//googlesql/base/testing:googlesql_gtest_main",
        "@com_google_absl//absl/base:core_headers",
        "@com_google_absl//absl/log",
    ],
)

cc_library(
    name = "bits",
    srcs = ["bits.cc"],
    hdrs = [
        "bits.h",
    ],
    deps = [
        ":check",
        ":endian",
        "@com_google_absl//absl/base",
        "@com_google_absl//absl/numeric:int128",
        "@com_google_googletest//:gtest",
    ],
)

cc_test(
    name = "bits_test",
    srcs = ["bits_test.cc"],
    deps = [
        ":bits",
        "//googlesql/base/testing:googlesql_gtest_main",
        "@com_google_absl//absl/numeric:int128",
    ],
)

cc_test(
    name = "status_builder_test",
    srcs = ["status_builder_test.cc"],
    deps = [
        ":source_location",
        ":status",
        ":test_payload_cc_proto",
        "//googlesql/base/testing:googlesql_gtest_main",
        "//googlesql/base/testing:status_matchers",
        "@com_google_absl//absl/status",
        "@com_google_absl//absl/status:statusor",
        "@com_google_absl//absl/strings",
        "@com_google_protobuf//:protobuf",
    ],
)

proto_library(
    name = "test_payload_proto",
    testonly = 1,
    srcs = ["test_payload.proto"],
)

cc_proto_library(
    name = "test_payload_cc_proto",
    testonly = 1,
    deps = [":test_payload_proto"],
)

alias(
    name = "statusor",
    actual = ":status",
)

alias(
    name = "status_payload",
    actual = ":status",
)

cc_library(
    name = "case",
    srcs = ["case.cc"],
    hdrs = ["case.h"],
    deps = [
        "@com_google_absl//absl/base:core_headers",
        "@com_google_absl//absl/strings",
    ],
)

cc_test(
    name = "case_test",
    srcs = ["case_test.cc"],
    deps = [
        ":case",
        "//googlesql/base/testing:googlesql_gtest_main",
    ],
)

cc_library(
    name = "path",
    srcs = ["path.cc"],
    hdrs = ["path.h"],
    deps = [
        "@com_google_absl//absl/strings",
    ],
)

cc_test(
    name = "path_test",
    srcs = ["path_test.cc"],
    deps = [
        ":path",
        "//googlesql/base/testing:googlesql_gtest_main",
    ],
)

cc_library(
    name = "time_proto_util",
    srcs = ["time_proto_util.cc"],
    hdrs = ["time_proto_util.h"],
    deps = [
        "@com_google_absl//absl/status",
        "@com_google_absl//absl/status:statusor",
        "@com_google_absl//absl/strings",
        "@com_google_absl//absl/time",
        "@com_google_protobuf//:protobuf",
    ],
)

cc_test(
    name = "time_proto_util_test",
    srcs = ["time_proto_util_test.cc"],
    deps = [
        ":time_proto_util",
        "//googlesql/base/testing:googlesql_gtest_main",
        "//googlesql/base/testing:status_matchers",
        "@com_google_absl//absl/status",
        "@com_google_absl//absl/time",
        "@com_google_protobuf//:protobuf",
    ],
)

cc_library(
    name = "string_numbers",
    srcs = ["string_numbers.cc"],
    hdrs = ["string_numbers.h"],
    deps = [
        ":check",
        ":logging",
        "@com_google_absl//absl/strings",
    ],
)

cc_library(
    name = "strings",
    deps = [
        ":case",
        ":string_numbers",
    ],
)

cc_test(
    name = "string_numbers_test",
    srcs = ["string_numbers_test.cc"],
    deps = [
        ":string_numbers",
        "//googlesql/base/testing:googlesql_gtest_main",
        "@com_google_absl//absl/strings",
    ],
)

cc_library(
    name = "compact_reference_counted",
    hdrs = [
        "compact_reference_counted.h",
    ],
)

cc_test(
    name = "compact_reference_counted_test",
    srcs = ["compact_reference_counted_test.cc"],
    deps = [
        ":compact_reference_counted",
        "//googlesql/base/testing:googlesql_gtest_main",
    ],
)

cc_library(
    name = "general_trie",
    hdrs = [
        "general_trie.h",
    ],
    deps = [
        ":base",
        "@com_google_absl//absl/base:core_headers",
        "@com_google_absl//absl/strings",
        "@com_google_absl//absl/types:span",
    ],
)

cc_test(
    name = "general_trie_unittest",
    srcs = ["general_trie_unittest.cc"],
    deps = [
        ":general_trie",
        "//googlesql/base/testing:googlesql_gtest_main",
        "@com_google_absl//absl/strings",
    ],
)

cc_library(
    name = "flat_internal",
    srcs = ["flat_internal.cc"],
    hdrs = ["flat_internal.h"],
    deps = ["@com_google_absl//absl/meta:type_traits"],
)

cc_test(
    name = "flat_internal_test",
    srcs = [
        "flat_internal_test.cc",
    ],
    deps = [
        ":flat_internal",
        "//googlesql/base/testing:googlesql_gtest_main",
    ],
)

cc_library(
    name = "flat_set",
    hdrs = [
        "flat_set.h",
    ],
    deps = [
        ":flat_internal",
        "@com_google_absl//absl/meta:type_traits",
    ],
)

cc_test(
    name = "flat_set_test",
    srcs = [
        "flat_set_test.cc",
    ],
    deps = [
        ":base",
        ":flat_set",
        ":stl_util",
        "//googlesql/base/testing:googlesql_gtest_main",
        "@com_google_absl//absl/container:inlined_vector",
        "@com_google_absl//absl/hash:hash_testing",
        "@com_google_absl//absl/memory",
        "@com_google_absl//absl/random",
        "@com_google_absl//absl/time",
        "@com_google_absl//absl/types:span",
    ],
)

cc_library(
    name = "clock",
    srcs = [
        "clock.cc",
    ],
    hdrs = [
        "clock.h",
    ],
    deps = [
        ":base",
        "@com_google_absl//absl/base:core_headers",
        "@com_google_absl//absl/synchronization",
        "@com_google_absl//absl/time",
    ],
)

cc_test(
    name = "clock_test",
    srcs = [
        "clock_test.cc",
    ],
    deps = [
        ":clock",
        "//googlesql/base/testing:googlesql_gtest_main",
        "@com_google_absl//absl/synchronization",
        "@com_google_absl//absl/time",
    ],
)

cc_library(
    name = "exactfloat",
    srcs = [
        "exactfloat.cc",
    ],
    hdrs = [
        "exactfloat.h",
    ],
    deps = [
        "@boringssl//:crypto",
        "@com_google_absl//absl/base:core_headers",
        "@com_google_absl//absl/container:fixed_array",
        "@com_google_absl//absl/log:absl_check",
        "@com_google_absl//absl/log:absl_log",
        "@com_google_absl//absl/numeric:bits",
        "@com_google_absl//absl/strings",
        "@com_google_absl//absl/strings:str_format",
    ],
)

cc_test(
    name = "exactfloat_test",
    srcs = [
        "exactfloat_test.cc",
    ],
    deps = [
        ":exactfloat",
        "//googlesql/base/testing:googlesql_gtest_main",
        "@com_google_absl//absl/base",
        "@com_google_absl//absl/base:core_headers",
        "@com_google_absl//absl/base:no_destructor",
        "@com_google_absl//absl/log:absl_check",
        "@com_google_absl//absl/log:absl_log",
    ],
)

cc_test(
    name = "exactfloat_underflow_test",
    timeout = "eternal",  # This test does take a long time.
    srcs = [
        "exactfloat_underflow_test.cc",
    ],
    shard_count = 4,
    deps = [
        ":exactfloat",
        "//googlesql/base/testing:googlesql_gtest_main",
    ],
)

cc_library(
    name = "file_util",
    hdrs = ["file_util.h"],
    deps = [
        ":file_util_oss",
    ],
)

cc_library(
    name = "file_util_oss",
    hdrs = ["file_util_oss.h"],
    deps = [
        ":path",
        "@com_google_absl//absl/status",
        "@com_google_absl//absl/strings",
        "@com_googlesource_code_re2//:re2",
    ],
)

cc_test(
    name = "file_util_oss_test",
    srcs = ["file_util_oss_test.cc"],
    data = ["file_util_oss_test.input_file"],
    deps = [
        ":file_util_oss",
        "//googlesql/base/testing:googlesql_gtest_main",
        "//googlesql/base/testing:status_matchers",
        "@com_google_absl//absl/status",
        "@com_google_absl//absl/strings",
    ],
)

cc_library(
    name = "edit_distance",
    hdrs = ["edit_distance.h"],
    deps = ["@com_google_absl//absl/container:fixed_array"],
)

cc_test(
    name = "edit_distance_test",
    srcs = ["edit_distance_test.cc"],
    deps = [
        ":edit_distance",
        ":logging",
        "//googlesql/base/testing:googlesql_gtest_main",
    ],
)

cc_library(
    name = "castops",
    hdrs = ["castops.h"],
)

cc_test(
    name = "castops_test",
    srcs = [
        "castops_test.cc",
    ],
    deps = [
        ":castops",
        "//googlesql/base/testing:googlesql_gtest_main",
        "@com_google_absl//absl/numeric:int128",
    ],
)

cc_library(
    name = "lossless_convert",
    hdrs = ["lossless_convert.h"],
    deps = [
        ":castops",
    ],
)

cc_test(
    name = "lossless_convert_test",
    size = "small",
    srcs = ["lossless_convert_test.cc"],
    deps = [
        ":lossless_convert",
        "//googlesql/base/testing:googlesql_gtest_main",
    ],
)

cc_library(
    name = "optional_ref",
    hdrs = ["optional_ref.h"],
    deps = [
        "@com_google_absl//absl/base:core_headers",
    ],
)

cc_library(
    name = "optional_ref_matchers",
    testonly = 1,
    hdrs = ["optional_ref_matchers.h"],
    deps = [
        "//googlesql/base/testing:googlesql_gtest_main",
    ],
)

cc_test(
    name = "optional_ref_test",
    srcs = ["optional_ref_test.cc"],
    deps = [
        ":logging",
        ":optional_ref",
        ":optional_ref_matchers",
        "//googlesql/base/testing:googlesql_gtest_main",
        "@com_google_absl//absl/strings",
    ],
)
