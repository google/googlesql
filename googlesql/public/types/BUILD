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

filegroup(
    name = "type_headers",
    srcs = [
        "annotation.h",
        "array_type.h",
        "collation.h",
        "container_type.h",
        "extended_type.h",
        "list_backed_type.h",
        "map_type.h",
        "measure_type.h",
        "range_type.h",
        "row_type.h",
        "simple_type.h",
        "simple_value.h",
        "struct_type.h",
        "type.h",
        "type_modifiers.h",
        "type_parameters.h",
        "value_equality_check_options.h",
    ],
)

cc_library(
    name = "type",
    srcs = [
        "annotation.cc",
        "array_type.cc",
        "collation.cc",
        "extended_type.cc",
        "internal_utils.h",
        "map_type.cc",
        "measure_type.cc",
        "range_type.cc",
        "row_type.cc",
        "simple_type.cc",
        "simple_value.cc",
        "struct_type.cc",
        "type.cc",
        "type_modifiers.cc",
        "type_parameters.cc",
    ],
    hdrs = [":type_headers"],
    deps = [
        ":timestamp_util",
        ":value_representations",
        "//googlesql/base",
        "//googlesql/base:case",
        "//googlesql/base:check",
        "//googlesql/base:compact_reference_counted",
        "//googlesql/base:map_util",
        "//googlesql/base:ret_check",
        "//googlesql/base:status",
        "//googlesql/base:time_proto_util",
        "//googlesql/common:errors",
        "//googlesql/common:float_margin",
        "//googlesql/common:string_util",
        "//googlesql/common:thread_stack",
        "//googlesql/common:unicode_utils",
        "//googlesql/common/search/public:token_list_util",
        "//googlesql/public:annotation_cc_proto",
        "//googlesql/public:civil_time",
        "//googlesql/public:collation_cc_proto",
        "//googlesql/public:interval_value",
        "//googlesql/public:json_value",
        "//googlesql/public:language_options",
        "//googlesql/public:numeric_value",
        "//googlesql/public:options_cc_proto",
        "//googlesql/public:pico_time",
        "//googlesql/public:simple_value_cc_proto",
        "//googlesql/public:strings",
        "//googlesql/public:timestamp_picos_value",
        "//googlesql/public:type_annotation_cc_proto",
        "//googlesql/public:type_cc_proto",
        "//googlesql/public:type_modifiers_cc_proto",
        "//googlesql/public:type_parameters_cc_proto",
        "//googlesql/public:uuid_value",
        "//googlesql/public:value_cc_proto",
        "//googlesql/public:value_content",
        "//googlesql/public/functions:date_time_util",
        "//googlesql/public/functions:rounding_mode_cc_proto",
        "@com_google_absl//absl/base:core_headers",
        "@com_google_absl//absl/base:no_destructor",
        "@com_google_absl//absl/base:nullability",
        "@com_google_absl//absl/container:flat_hash_map",
        "@com_google_absl//absl/container:flat_hash_set",
        "@com_google_absl//absl/container:node_hash_map",
        "@com_google_absl//absl/container:node_hash_set",
        "@com_google_absl//absl/hash",
        "@com_google_absl//absl/log",
        "@com_google_absl//absl/memory",
        "@com_google_absl//absl/meta:type_traits",
        "@com_google_absl//absl/status",
        "@com_google_absl//absl/status:statusor",
        "@com_google_absl//absl/strings",
        "@com_google_absl//absl/synchronization",
        "@com_google_absl//absl/time",
        "@com_google_absl//absl/types:optional",
        "@com_google_absl//absl/types:span",
        "@com_google_absl//absl/types:variant",
        "@com_google_protobuf//:protobuf",
    ],
)

# This includes parts of the ":type" library that include Catalog dependencies.
# These are in a separate build target to avoid circular dependencies.
# These methods are only needed in analyzer code that actually calls them.
cc_library(
    name = "type_with_catalog_impl",
    srcs = ["row_type_with_catalog.cc"],
    deps = [
        ":type",
        "//googlesql/public:catalog",
    ],
    alwayslink = 1,
)

cc_library(
    name = "internal_proto_utils",
    srcs = ["internal_proto_utils.cc"],
    hdrs = ["internal_proto_utils.h"],
    deps = [
        ":type",
        "//googlesql/base:logging",
        "//googlesql/base:ret_check",
        "//googlesql/base:status",
        "//googlesql/common:proto_helper",
        "//googlesql/public:options_cc_proto",
        "@com_google_absl//absl/status",
        "@com_google_protobuf//:protobuf",
    ],
)

filegroup(
    name = "type_factory_headers",
    srcs = [
        "enum_type.h",
        "graph_element_type.h",
        "graph_path_type.h",
        "internal_proto_utils.h",
        "proto_type.h",
        "type_deserializer.h",
        "type_factory.h",
    ],
)

cc_library(
    name = "type_factory",
    srcs = [
        "enum_type.cc",
        "graph_element_type.cc",
        "graph_path_type.cc",
        "internal_utils.h",
        "proto_type.cc",
        "type_deserializer.cc",
        "type_factory.cc",
    ],
    hdrs = [
        ":type_factory_headers",
    ],
    deps = [
        ":internal_proto_utils",
        ":type",
        ":value_representations",
        "//googlesql/base",
        "//googlesql/base:case",
        "//googlesql/base:check",
        "//googlesql/base:map_util",
        "//googlesql/base:ret_check",
        "//googlesql/base:status",
        "//googlesql/common:errors",
        "//googlesql/common:float_margin",
        "//googlesql/common:proto_helper",
        "//googlesql/common:thread_stack",
        "//googlesql/public:annotation_cc_proto",
        "//googlesql/public:language_options",
        "//googlesql/public:options_cc_proto",
        "//googlesql/public:strings",
        "//googlesql/public:type_annotation_cc_proto",
        "//googlesql/public:type_cc_proto",
        "//googlesql/public:value_cc_proto",
        "//googlesql/public:value_content",
        "//googlesql/public/functions:array_find_mode_cc_proto",
        "//googlesql/public/functions:array_zip_mode_cc_proto",
        "//googlesql/public/functions:bitwise_agg_mode_cc_proto",
        "//googlesql/public/functions:convert_proto",
        "//googlesql/public/functions:datetime_cc_proto",
        "//googlesql/public/functions:differential_privacy_cc_proto",
        "//googlesql/public/functions:normalize_mode_cc_proto",
        "//googlesql/public/functions:range_sessionize_mode_cc_proto",
        "//googlesql/public/functions:rank_type_cc_proto",
        "//googlesql/public/functions:rounding_mode_cc_proto",
        "//googlesql/public/functions:unsupported_fields_cc_proto",
        "//googlesql/public/proto:type_annotation_cc_proto",
        "@com_google_absl//absl/algorithm:container",
        "@com_google_absl//absl/base:core_headers",
        "@com_google_absl//absl/base:nullability",
        "@com_google_absl//absl/cleanup",
        "@com_google_absl//absl/container:flat_hash_map",
        "@com_google_absl//absl/container:flat_hash_set",
        "@com_google_absl//absl/container:inlined_vector",
        "@com_google_absl//absl/container:node_hash_map",
        "@com_google_absl//absl/container:node_hash_set",
        "@com_google_absl//absl/flags:flag",
        "@com_google_absl//absl/functional:function_ref",
        "@com_google_absl//absl/hash",
        "@com_google_absl//absl/memory",
        "@com_google_absl//absl/meta:type_traits",
        "@com_google_absl//absl/status",
        "@com_google_absl//absl/status:statusor",
        "@com_google_absl//absl/strings",
        "@com_google_absl//absl/strings:cord",
        "@com_google_absl//absl/synchronization",
        "@com_google_absl//absl/types:span",
        "@com_google_protobuf//:protobuf",
    ],
)

cc_library(
    name = "types",
    hdrs = [
        ":type_factory_headers",
        ":type_headers",
    ],
    deps = [
        ":timestamp_util",
        ":type",
        ":type_factory",
        ":value_representations",
        "//googlesql/base",
        "//googlesql/base:case",
        "//googlesql/base:check",
        "//googlesql/base:map_util",
        "//googlesql/base:status",
        "//googlesql/common:errors",
        "//googlesql/common:float_margin",
        "//googlesql/common:thread_stack",
        "//googlesql/public:annotation_cc_proto",
        "//googlesql/public:civil_time",
        "//googlesql/public:collation_cc_proto",
        "//googlesql/public:language_options",
        "//googlesql/public:options_cc_proto",
        "//googlesql/public:simple_value_cc_proto",
        "//googlesql/public:type_annotation_cc_proto",
        "//googlesql/public:type_cc_proto",
        "//googlesql/public:type_modifiers_cc_proto",
        "//googlesql/public:type_parameters_cc_proto",
        "//googlesql/public/proto:type_annotation_cc_proto",
        "@com_google_absl//absl/base:core_headers",
        "@com_google_absl//absl/base:nullability",
        "@com_google_absl//absl/container:flat_hash_map",
        "@com_google_absl//absl/container:flat_hash_set",
        "@com_google_absl//absl/container:inlined_vector",
        "@com_google_absl//absl/container:node_hash_map",
        "@com_google_absl//absl/flags:flag",
        "@com_google_absl//absl/hash",
        "@com_google_absl//absl/status",
        "@com_google_absl//absl/status:statusor",
        "@com_google_absl//absl/strings",
        "@com_google_absl//absl/synchronization",
        "@com_google_absl//absl/time",
        "@com_google_absl//absl/types:optional",
        "@com_google_absl//absl/types:span",
        "@com_google_absl//absl/types:variant",
        "@com_google_protobuf//:protobuf",
    ],
)

cc_library(
    name = "value_representations",
    hdrs = [
        "value_representations.h",
    ],
    deps = [
        "//googlesql/base:compact_reference_counted",
        "//googlesql/base:logging",
        "//googlesql/base:strings",
        "//googlesql/public:interval_value",
        "//googlesql/public:json_value",
        "//googlesql/public:numeric_value",
        "//googlesql/public:simple_token_list",
        "//googlesql/public:timestamp_picos_value",
        "//googlesql/public:uuid_value",
        "//googlesql/public:value_content",
        "@com_google_absl//absl/container:btree",
        "@com_google_absl//absl/strings:cord",
        "@com_google_absl//absl/strings:string_view",
        "@com_google_absl//absl/types:optional",
        "@com_google_absl//absl/types:span",
        "@com_google_absl//absl/types:variant",
    ],
)

cc_library(
    name = "timestamp_util",
    srcs = [
        "timestamp_util.cc",
    ],
    hdrs = [
        "timestamp_util.h",
    ],
    deps = [
        "@com_google_absl//absl/time",
    ],
)

cc_test(
    name = "graph_element_type_test",
    srcs = ["graph_element_type_test.cc"],
    deps = [
        ":types",
        "//googlesql/base:check",
        "//googlesql/base/testing:googlesql_gtest_main",
        "//googlesql/base/testing:status_matchers",
        "//googlesql/public:language_options",
        "//googlesql/public:options_cc_proto",
        "//googlesql/public:type_cc_proto",
        "//googlesql/public:type_parameters_cc_proto",
        "@com_google_absl//absl/hash",
        "@com_google_absl//absl/status",
        "@com_google_protobuf//:protobuf",
    ],
)

cc_test(
    name = "graph_path_type_test",
    srcs = ["graph_path_type_test.cc"],
    deps = [
        ":types",
        "//googlesql/base/testing:googlesql_gtest_main",
        "//googlesql/base/testing:status_matchers",
        "//googlesql/common/testing:proto_matchers",
        "//googlesql/public:language_options",
        "//googlesql/public:options_cc_proto",
        "//googlesql/public:type_cc_proto",
        "//googlesql/public:type_parameters_cc_proto",
        "@com_google_absl//absl/hash:hash_testing",
        "@com_google_absl//absl/status",
        "@com_google_protobuf//:protobuf",
    ],
)

cc_test(
    name = "map_type_test",
    srcs = ["map_type_test.cc"],
    deps = [
        ":types",
        "//googlesql/base/testing:googlesql_gtest_main",
        "//googlesql/base/testing:status_matchers",
        "//googlesql/public:language_options",
        "//googlesql/public:options_cc_proto",
        "//googlesql/public:type_parameters_cc_proto",
        "//googlesql/public:value",
        "//googlesql/testdata:test_schema_cc_proto",
        "//googlesql/testing:test_value",
        "@com_google_absl//absl/status",
        "@com_google_protobuf//:protobuf",
    ],
)

cc_test(
    name = "measure_type_test",
    srcs = ["measure_type_test.cc"],
    deps = [
        ":types",
        "//googlesql/base/testing:googlesql_gtest_main",
        "//googlesql/base/testing:status_matchers",
        "//googlesql/public:options_cc_proto",
        "//googlesql/testdata:test_schema_cc_proto",
        "@com_google_absl//absl/status",
    ],
)
