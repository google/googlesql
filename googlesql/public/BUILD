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
load("@rules_python//python:proto.bzl", "py_proto_library")

package(default_visibility = ["//visibility:public"])

cc_library(
    name = "aggregation_threshold_utils",
    srcs = [
        "aggregation_threshold_utils.cc",
    ],
    hdrs = [
        "aggregation_threshold_utils.h",
    ],
    deps = [
        ":analyzer_options",
        "//googlesql/base:case",
        "//googlesql/base:no_destructor",
        "//googlesql/proto:options_cc_proto",
        "//googlesql/public/types",
        "@com_google_absl//absl/container:flat_hash_map",
    ],
)

cc_library(
    name = "anonymization_utils",
    srcs = [
        "anonymization_utils.cc",
    ],
    hdrs = [
        "anonymization_utils.h",
    ],
    deps = [
        ":constant_evaluator",
        ":function_headers",
        ":type",
        ":value",
        "//googlesql/base:ret_check",
        "//googlesql/base:status",
        "//googlesql/public/functions:differential_privacy_cc_proto",
        "//googlesql/resolved_ast",
        "@com_google_absl//absl/container:flat_hash_set",
        "@com_google_absl//absl/status",
        "@com_google_absl//absl/status:statusor",
        "@com_google_absl//absl/strings",
        "@com_google_absl//absl/types:span",
        "@com_google_cc_differential_privacy//algorithms:partition-selection",
    ],
)

cc_test(
    name = "anonymization_utils_test",
    size = "small",
    srcs = ["anonymization_utils_test.cc"],
    deps = [
        ":anonymization_utils",
        ":function_cc_proto",
        ":function_headers",
        ":id_string",
        ":value",
        "//googlesql/base/testing:googlesql_gtest_main",
        "//googlesql/base/testing:status_matchers",
        "//googlesql/public/functions:differential_privacy_cc_proto",
        "//googlesql/public/types",
        "//googlesql/resolved_ast",
        "@com_google_absl//absl/status",
        "@com_google_absl//absl/status:statusor",
        "@com_google_absl//absl/strings",
    ],
)

cc_library(
    name = "type",
    srcs = [
        "convert_type_to_proto.cc",
    ],
    hdrs = [
        "convert_type_to_proto.h",
        "type.h",
    ],
    deps = [
        ":type_annotation_cc_proto",
        ":type_cc_proto",
        ":value_cc_proto",
        "//googlesql/base",
        "//googlesql/base:case",
        "//googlesql/base:map_util",
        "//googlesql/base:ret_check",
        "//googlesql/base:status",
        "//googlesql/common:errors",
        "//googlesql/public/proto:type_annotation_cc_proto",
        "//googlesql/public/types",
        "@com_google_absl//absl/container:btree",
        "@com_google_absl//absl/container:flat_hash_map",
        "@com_google_absl//absl/container:node_hash_map",
        "@com_google_absl//absl/status",
        "@com_google_absl//absl/strings",
        "@com_google_absl//absl/types:span",
        "@com_google_protobuf//:protobuf",
    ],
)

cc_library(
    name = "value_content",
    hdrs = [
        "value_content.h",
    ],
)

cc_library(
    name = "catalog",
    srcs = [
        "catalog.cc",
        "catalog_helper.cc",
    ],
    hdrs = [
        "catalog.h",
        "catalog_helper.h",
        "property_graph.h",
    ],
    deps = [
        ":evaluator_table_iterator",
        ":strings",
        ":type",
        "//googlesql/base",
        "//googlesql/base:check",
        "//googlesql/base:edit_distance",
        "//googlesql/base:map_util",
        "//googlesql/base:ret_check",
        "//googlesql/base:source_location",
        "//googlesql/base:status",
        "//googlesql/base:strings",
        "//googlesql/public/types",
        "@com_google_absl//absl/base:core_headers",
        "@com_google_absl//absl/base:nullability",
        "@com_google_absl//absl/container:flat_hash_map",
        "@com_google_absl//absl/container:flat_hash_set",
        "@com_google_absl//absl/flags:flag",
        "@com_google_absl//absl/functional:function_ref",
        "@com_google_absl//absl/log",
        "@com_google_absl//absl/status",
        "@com_google_absl//absl/status:statusor",
        "@com_google_absl//absl/strings",
        "@com_google_absl//absl/types:span",
        "@com_google_absl//absl/types:variant",
        "@com_google_protobuf//:protobuf",
    ],
)

cc_library(
    name = "cycle_detector",
    srcs = [
        "cycle_detector.cc",
    ],
    hdrs = ["cycle_detector.h"],
    deps = [
        "//googlesql/base",
        "//googlesql/base:map_util",
        "//googlesql/base:source_location",
        "//googlesql/base:status",
        "//googlesql/common:thread_stack",
        "@com_google_absl//absl/base:core_headers",
        "@com_google_absl//absl/strings",
    ],
)

cc_test(
    name = "cycle_detector_test",
    size = "small",
    srcs = ["cycle_detector_test.cc"],
    deps = [
        ":cycle_detector",
        "//googlesql/base/testing:googlesql_gtest_main",
        "@com_google_absl//absl/strings",
    ],
)

cc_library(
    name = "multi_catalog",
    srcs = ["multi_catalog.cc"],
    hdrs = ["multi_catalog.h"],
    deps = [
        ":catalog",
        ":constant",
        ":function",
        ":type",
        "//googlesql/base",
        "//googlesql/base:ret_check",
        "//googlesql/base:status",
        "//googlesql/public/types",
        "@com_google_absl//absl/status",
        "@com_google_absl//absl/status:statusor",
        "@com_google_absl//absl/strings",
        "@com_google_absl//absl/types:span",
    ],
)

proto_library(
    name = "constness_level_proto",
    srcs = ["constness_level.proto"],
)

java_proto_library(
    name = "constness_level_java_proto",
    deps = [":constness_level_proto"],
)

cc_proto_library(
    name = "constness_level_cc_proto",
    deps = [":constness_level_proto"],
)

cc_library(
    name = "constant_evaluator",
    hdrs = ["constant_evaluator.h"],
    deps = [
        ":value",
        "//googlesql/base:ret_check",
        "//googlesql/common:constant_utils",
        "//googlesql/resolved_ast",
        "@com_google_absl//absl/status:statusor",
    ],
)

cc_library(
    name = "prepared_expression_constant_evaluator",
    srcs = ["prepared_expression_constant_evaluator.cc"],
    hdrs = ["prepared_expression_constant_evaluator.h"],
    deps = [
        ":analyzer_options",
        ":constant_evaluator",
        ":evaluator",
        ":language_options",
        ":value",
        "//googlesql/base:status",
        "//googlesql/resolved_ast",
        "@com_google_absl//absl/status:statusor",
    ],
)

cc_test(
    name = "prepared_expression_constant_evaluator_test",
    srcs = ["prepared_expression_constant_evaluator_test.cc"],
    deps = [
        ":analyzer_options",
        ":builtin_function_options",
        ":prepared_expression_constant_evaluator",
        ":simple_catalog",
        ":value",
        "//googlesql/base:status",
        "//googlesql/base/testing:googlesql_gtest_main",
        "//googlesql/base/testing:status_matchers",
        "//googlesql/public/types",
        "//googlesql/resolved_ast",
        "//googlesql/resolved_ast:rewrite_utils",
        "@com_google_absl//absl/status:statusor",
    ],
)

cc_library(
    name = "module_contents_fetcher",
    srcs = ["module_contents_fetcher.cc"],
    hdrs = ["module_contents_fetcher.h"],
    deps = [
        ":analyzer_options",
        ":language_options",
        ":options_cc_proto",
        ":parse_resume_location",
        "//googlesql/base:logging",
        "//googlesql/base:map_util",
        "//googlesql/base:status",
        "//googlesql/common:errors",
        "//googlesql/parser",
        "//googlesql/proto:module_options_cc_proto",
        "@com_google_absl//absl/container:flat_hash_map",
        "@com_google_absl//absl/status",
        "@com_google_absl//absl/types:span",
        "@com_google_protobuf//:protobuf",
    ],
)

cc_library(
    name = "modules",
    srcs = [
        "module_factory.cc",
        "modules.cc",
    ],
    hdrs = [
        "module_factory.h",
        "modules.h",
    ],
    deps = [
        ":analyzer",
        ":analyzer_options",
        ":analyzer_output",
        ":catalog",
        ":constant",
        ":constant_evaluator",
        ":cycle_detector",
        ":error_helpers",
        ":function",
        ":function_cc_proto",
        ":module_contents_fetcher",
        ":module_details",
        ":multi_catalog",
        ":options_cc_proto",
        ":parse_location",
        ":parse_resume_location",
        ":remote_tvf_factory",
        ":simple_catalog",
        ":sql_constant",
        ":type",
        ":value",
        "//googlesql/base",
        "//googlesql/base:map_util",
        "//googlesql/base:ret_check",
        "//googlesql/base:status",
        "//googlesql/base:stl_util",
        "//googlesql/base:strings",
        "//googlesql/common:errors",
        "//googlesql/common:lazy_resolution_catalog",
        "//googlesql/common:resolution_scope",
        "//googlesql/common:scope_error_catalog",
        "//googlesql/parser",
        "//googlesql/proto:module_options_cc_proto",
        "//googlesql/public/types",
        "//googlesql/resolved_ast",
        "@com_google_absl//absl/base:core_headers",
        "@com_google_absl//absl/cleanup",
        "@com_google_absl//absl/container:btree",
        "@com_google_absl//absl/functional:bind_front",
        "@com_google_absl//absl/status",
        "@com_google_absl//absl/status:statusor",
        "@com_google_absl//absl/strings",
        "@com_google_absl//absl/types:span",
        "@com_google_protobuf//:protobuf",
    ],
)

cc_test(
    name = "modules_test",
    size = "small",
    srcs = [
        "module_factory_test.cc",
        "modules_test.cc",
    ],
    data = [
        "//googlesql/testdata:test_files",
        "//googlesql/testdata:test_proto3_proto",
        "//googlesql/testdata:test_schema_proto",
        "//googlesql/testdata/modules:module_files",
        "//googlesql/testdata/proto_dag_like:protos",
    ],
    deps = [
        ":analyzer_options",
        ":builtin_function",
        ":builtin_function_options",
        ":catalog",
        ":constant",
        ":constant_evaluator",
        ":error_helpers",
        ":error_location_cc_proto",
        ":file_module_contents_fetcher",
        ":function",
        ":function_headers",
        ":language_options",
        ":module_contents_fetcher",
        ":module_details",
        ":modules",
        ":options_cc_proto",
        ":prepared_expression_constant_evaluator",
        ":remote_tvf_factory",
        ":simple_catalog",
        ":sql_constant",
        ":sql_function",
        ":sql_tvf",
        ":sql_view",
        ":type",
        ":value",
        "//googlesql/base",
        "//googlesql/base:check",
        "//googlesql/base:file_util",
        "//googlesql/base:map_util",
        "//googlesql/base:path",
        "//googlesql/base:status",
        "//googlesql/base/testing:googlesql_gtest_main",
        "//googlesql/base/testing:status_matchers",
        "//googlesql/common:status_payload_utils",
        "//googlesql/resolved_ast",
        "//googlesql/resolved_ast:resolved_node_kind_cc_proto",
        "//googlesql/testing:test_module_contents_fetcher",
        "@com_google_absl//absl/container:flat_hash_map",
        "@com_google_absl//absl/status",
        "@com_google_absl//absl/status:statusor",
        "@com_google_absl//absl/strings",
        "@com_google_absl//absl/types:span",
        "@com_google_protobuf//:protobuf",
    ],
)

cc_library(
    name = "file_module_contents_fetcher",
    srcs = ["file_module_contents_fetcher.cc"],
    hdrs = ["file_module_contents_fetcher.h"],
    deps = [
        ":module_contents_fetcher",
        ":options_cc_proto",
        "//googlesql/base:file_util",
        "//googlesql/base:logging",
        "//googlesql/base:path",
        "//googlesql/base:ret_check",
        "//googlesql/base:status",
        "//googlesql/common:proto_helper",
        "//googlesql/common:status_payload_utils",
        "//googlesql/proto:module_options_cc_proto",
        "@com_google_absl//absl/memory",
        "@com_google_absl//absl/status",
        "@com_google_absl//absl/strings",
        "@com_google_absl//absl/strings:string_view",
        "@com_google_protobuf//:protobuf",
    ],
)

cc_library(
    name = "remote_tvf_factory",
    hdrs = ["remote_tvf_factory.h"],
    deps = [
        ":catalog",
        ":function",
        ":module_details",
        "//googlesql/resolved_ast",
        "@com_google_absl//absl/base:nullability",
        "@com_google_absl//absl/status:statusor",
    ],
)

cc_library(
    name = "module_details",
    srcs = ["module_details.cc"],
    hdrs = ["module_details.h"],
    deps = [
        ":constant_evaluator",
        ":options_cc_proto",
        ":value",
        "//googlesql/base:map_util",
        "//googlesql/base:status",
        "//googlesql/base:strings",
        "//googlesql/common:errors",
        "//googlesql/common:resolution_scope",
        "//googlesql/proto:module_options_cc_proto",
        "//googlesql/public/types",
        "//googlesql/resolved_ast",
        "//googlesql/resolved_ast:resolved_node_kind_cc_proto",
        "@com_google_absl//absl/base:no_destructor",
        "@com_google_absl//absl/container:btree",
        "@com_google_absl//absl/container:flat_hash_map",
        "@com_google_absl//absl/log",
        "@com_google_absl//absl/status",
        "@com_google_absl//absl/status:statusor",
        "@com_google_absl//absl/strings",
        "@com_google_absl//absl/types:span",
    ],
)

cc_test(
    name = "module_details_test",
    size = "small",
    srcs = ["module_details_test.cc"],
    deps = [
        ":analyzer",
        ":analyzer_options",
        ":analyzer_output",
        ":builtin_function_options",
        ":constant_evaluator",
        ":module_details",
        ":options_cc_proto",
        ":prepared_expression_constant_evaluator",
        ":simple_catalog",
        "//googlesql/base:ret_check",
        "//googlesql/base/testing:googlesql_gtest_main",
        "//googlesql/base/testing:status_matchers",
        "//googlesql/common:resolution_scope",
        "//googlesql/common/testing:proto_matchers",
        "//googlesql/proto:module_options_cc_proto",
        "//googlesql/public/types",
        "//googlesql/resolved_ast",
        "@com_google_absl//absl/status",
        "@com_google_absl//absl/status:statusor",
        "@com_google_absl//absl/strings",
        "@com_google_absl//absl/strings:string_view",
        "@com_google_protobuf//:protobuf",
    ],
)

cc_library(
    name = "civil_time",
    srcs = [
        "civil_time.cc",
    ],
    hdrs = [
        "civil_time.h",
    ],
    deps = [
        "//googlesql/base:mathutil",
        "@com_google_absl//absl/strings",
        "@com_google_absl//absl/strings:str_format",
        "@com_google_absl//absl/time",
        "@com_google_absl//absl/types:compare",
    ],
)

cc_test(
    name = "civil_time_test",
    size = "small",
    srcs = ["civil_time_test.cc"],
    deps = [
        ":civil_time",
        "//googlesql/base",
        "//googlesql/base/testing:googlesql_gtest_main",
        "@com_google_absl//absl/hash:hash_testing",
        "@com_google_absl//absl/time",
        "@com_google_absl//absl/types:compare",
    ],
)

cc_library(
    name = "rewriter_interface",
    hdrs = ["rewriter_interface.h"],
    deps = [
        "//googlesql/resolved_ast",
        "@com_google_absl//absl/status",
        "@com_google_absl//absl/status:statusor",
    ],
)

cc_library(
    name = "simple_catalog",
    srcs = [
        "simple_catalog.cc",
        "simple_property_graph.cc",
        "table_from_proto.cc",
    ],
    hdrs = [
        "simple_catalog.h",
        "simple_property_graph.h",
        "table_from_proto.h",
    ],
    deps = [
        ":builtin_function",
        ":builtin_function_options",
        ":catalog",
        ":constant",
        ":evaluator_table_iterator",
        ":function",
        ":simple_connection_cc_proto",
        ":simple_constant_cc_proto",
        ":simple_model_cc_proto",
        ":simple_sequence_cc_proto",
        ":simple_table_cc_proto",
        ":sql_view",
        ":strings",
        ":type",
        ":value",
        "//googlesql/base",
        "//googlesql/base:case",
        "//googlesql/base:check",
        "//googlesql/base:clock",
        "//googlesql/base:map_util",
        "//googlesql/base:ret_check",
        "//googlesql/base:status",
        "//googlesql/common:errors",
        "//googlesql/common:simple_evaluator_table_iterator",
        "//googlesql/proto:simple_catalog_cc_proto",
        "//googlesql/proto:simple_property_graph_cc_proto",
        "//googlesql/public/proto:type_annotation_cc_proto",
        "//googlesql/public/types",
        "//googlesql/resolved_ast",
        "//googlesql/resolved_ast:resolved_ast_cc_proto",
        "@com_google_absl//absl/base:core_headers",
        "@com_google_absl//absl/base:nullability",
        "@com_google_absl//absl/container:btree",
        "@com_google_absl//absl/container:flat_hash_map",
        "@com_google_absl//absl/container:flat_hash_set",
        "@com_google_absl//absl/memory",
        "@com_google_absl//absl/status",
        "@com_google_absl//absl/status:statusor",
        "@com_google_absl//absl/strings",
        "@com_google_absl//absl/synchronization",
        "@com_google_absl//absl/time",
        "@com_google_absl//absl/types:span",
        "@com_google_protobuf//:protobuf",
        "@com_google_protobuf//:protobuf_lite",
    ],
)

cc_library(
    name = "simple_catalog_util",
    srcs = ["simple_catalog_util.cc"],
    hdrs = ["simple_catalog_util.h"],
    deps = [
        ":analyzer",
        ":analyzer_options",
        ":analyzer_output",
        ":catalog",
        ":constant_evaluator",
        ":function",
        ":function_cc_proto",
        ":function_headers",
        ":simple_catalog",
        ":sql_constant",
        ":sql_function",
        ":sql_tvf",
        ":templated_sql_function",
        ":templated_sql_tvf",
        ":value",
        "//googlesql/base:ret_check",
        "//googlesql/base:status",
        "//googlesql/common:internal_property_graph",
        "//googlesql/resolved_ast",
        "//googlesql/resolved_ast:resolved_ast_enums_cc_proto",
        "//googlesql/resolved_ast:resolved_node_kind_cc_proto",
        "@com_google_absl//absl/base:nullability",
        "@com_google_absl//absl/container:flat_hash_map",
        "@com_google_absl//absl/container:flat_hash_set",
        "@com_google_absl//absl/status",
        "@com_google_absl//absl/status:statusor",
        "@com_google_absl//absl/strings",
        "@com_google_absl//absl/strings:str_format",
        "@com_google_absl//absl/types:span",
    ],
)

cc_library(
    name = "with_modifier_mode",
    hdrs = ["with_modifier_mode.h"],
    deps = ["@com_google_absl//absl/strings"],
)

cc_test(
    name = "simple_catalog_util_test",
    srcs = ["simple_catalog_util_test.cc"],
    deps = [
        ":analyzer",
        ":analyzer_options",
        ":catalog",
        ":function",
        ":function_headers",
        ":options_cc_proto",
        ":simple_catalog",
        ":simple_catalog_util",
        ":value",
        "//googlesql/base:status",
        "//googlesql/base/testing:googlesql_gtest_main",
        "//googlesql/base/testing:status_matchers",
        "//googlesql/public/types",
        "//googlesql/resolved_ast",
        "//googlesql/resolved_ast:resolved_node_kind_cc_proto",
        "@com_google_absl//absl/status",
        "@com_google_absl//absl/status:statusor",
        "@com_google_absl//absl/strings:string_view",
    ],
)

cc_library(
    name = "sql_formatter",
    srcs = ["sql_formatter.cc"],
    hdrs = ["sql_formatter.h"],
    deps = [
        ":error_helpers",
        ":language_options",
        ":options_cc_proto",
        ":parse_helpers",
        ":parse_location",
        ":parse_resume_location",
        "//googlesql/base",
        "//googlesql/base:ret_check",
        "//googlesql/base:source_location",
        "//googlesql/base:status",
        "//googlesql/parser",
        "@com_google_absl//absl/status",
        "@com_google_absl//absl/strings",
        "@com_google_absl//absl/strings:string_view",
    ],
)

cc_test(
    name = "sql_formatter_test",
    size = "small",
    srcs = ["sql_formatter_test.cc"],
    deps = [
        ":sql_formatter",
        "//googlesql/base/testing:googlesql_gtest_main",
        "//googlesql/base/testing:status_matchers",
        "@com_google_absl//absl/status",
        "@com_google_absl//absl/strings",
    ],
)

cc_library(
    name = "coercer",
    srcs = [
        "cast.cc",
        "coercer.cc",
    ],
    hdrs = [
        "cast.h",
        "coercer.h",
    ],
    deps = [
        ":catalog",
        ":civil_time",
        ":function",
        ":interval_value",
        ":json_value",
        ":language_options",
        ":numeric_value",
        ":options_cc_proto",
        ":pico_time",
        ":proto_value_conversion",
        ":signature_match_result",
        ":strings",
        ":timestamp_picos_value",
        ":type",
        ":type_cc_proto",
        ":uuid_value",
        ":value",
        "//googlesql/base",
        "//googlesql/base:check",
        "//googlesql/base:map_util",
        "//googlesql/base:ret_check",
        "//googlesql/base:status",
        "//googlesql/common:errors",
        "//googlesql/common:graph_element_utils",
        "//googlesql/common:internal_value",
        "//googlesql/common:utf_util",
        "//googlesql/public/functions:cast_date_time",
        "//googlesql/public/functions:convert",
        "//googlesql/public/functions:convert_proto",
        "//googlesql/public/functions:convert_string",
        "//googlesql/public/functions:convert_string_with_format",
        "//googlesql/public/functions:date_time_util",
        "//googlesql/public/functions:datetime_cc_proto",
        "//googlesql/public/functions:range",
        "//googlesql/public/functions:string",
        "//googlesql/public/types",
        "@com_google_absl//absl/algorithm:container",
        "@com_google_absl//absl/base:core_headers",
        "@com_google_absl//absl/container:flat_hash_map",
        "@com_google_absl//absl/container:flat_hash_set",
        "@com_google_absl//absl/log",
        "@com_google_absl//absl/status",
        "@com_google_absl//absl/status:statusor",
        "@com_google_absl//absl/strings",
        "@com_google_absl//absl/strings:cord",
        "@com_google_absl//absl/strings:str_format",
        "@com_google_absl//absl/time",
        "@com_google_absl//absl/types:optional",
        "@com_google_absl//absl/types:span",
    ],
)

cc_library(
    name = "signature_match_result",
    srcs = ["signature_match_result.cc"],
    hdrs = ["signature_match_result.h"],
    deps = [
        ":type",
        "//googlesql/base:logging",
        "@com_google_absl//absl/strings",
    ],
)

cc_library(
    name = "numeric_constants",
    hdrs = ["numeric_constants.h"],
    features = ["header_modules"],
)

cc_library(
    name = "numeric_parser",
    srcs = ["numeric_parser.cc"],
    hdrs = ["numeric_parser.h"],
    features = ["header_modules"],
    deps = [
        ":numeric_constants",
        "//googlesql/base:status",
        "//googlesql/common:multiprecision_int",
        "@com_google_absl//absl/base:core_headers",
        "@com_google_absl//absl/status",
        "@com_google_absl//absl/strings",
    ],
)

cc_library(
    name = "numeric_value",
    srcs = ["numeric_value.cc"],
    hdrs = [
        "numeric_value.h",
    ],
    features = ["header_modules"],
    deps = [
        ":numeric_constants",
        ":numeric_parser",
        "//googlesql/base",
        "//googlesql/base:endian",
        "//googlesql/base:mathutil",
        "//googlesql/base:status",
        "//googlesql/base:stl_util",
        "//googlesql/common:errors",
        "//googlesql/common:multiprecision_int",
        "@com_google_absl//absl/base:core_headers",
        "@com_google_absl//absl/hash",
        "@com_google_absl//absl/status",
        "@com_google_absl//absl/status:statusor",
        "@com_google_absl//absl/strings",
        "@com_google_absl//absl/types:compare",
        "@com_google_absl//absl/types:optional",
    ],
)

cc_library(
    name = "numeric_value_test_utils",
    testonly = 1,
    hdrs = ["numeric_value_test_utils.h"],
    deps = [
        ":numeric_value",
        "//googlesql/common:multiprecision_int",
        "@com_google_absl//absl/random",
        "@com_google_absl//absl/status:statusor",
    ],
)

cc_test(
    name = "numeric_value_test_utils_test",
    srcs = ["numeric_value_test_utils_test.cc"],
    deps = [
        ":numeric_value",
        ":numeric_value_test_utils",
        "//googlesql/base/testing:googlesql_gtest_main",
        "//googlesql/base/testing:status_matchers",
        "@com_google_absl//absl/random",
        "@com_google_absl//absl/strings",
        "@com_google_absl//absl/strings:str_format",
    ],
)

cc_test(
    name = "numeric_value_test",
    size = "small",
    timeout = "moderate",
    srcs = ["numeric_value_test.cc"],
    deps = [
        ":numeric_constants",
        ":numeric_value",
        ":numeric_value_test_utils",
        "//googlesql/base:bits",
        "//googlesql/base:endian",
        "//googlesql/base:mathutil",
        "//googlesql/base:status",
        "//googlesql/base/testing:googlesql_gtest_main",
        "//googlesql/base/testing:status_matchers",
        "//googlesql/common:multiprecision_int",
        "@com_google_absl//absl/base",
        "@com_google_absl//absl/base:core_headers",
        "@com_google_absl//absl/hash:hash_testing",
        "@com_google_absl//absl/numeric:int128",
        "@com_google_absl//absl/random",
        "@com_google_absl//absl/status:statusor",
        "@com_google_absl//absl/strings",
        "@com_google_absl//absl/strings:str_format",
        "@com_google_absl//absl/types:optional",
        "@com_google_absl//absl/types:variant",
    ],
)

cc_library(
    name = "pico_time",
    srcs = ["pico_time.cc"],
    hdrs = ["pico_time.h"],
    deps = [
        "@com_google_absl//absl/numeric:int128",
        "@com_google_absl//absl/status",
        "@com_google_absl//absl/status:statusor",
        "@com_google_absl//absl/strings",
        "@com_google_absl//absl/strings:string_view",
        "@com_google_absl//absl/time",
    ],
)

cc_test(
    name = "pico_time_test",
    srcs = ["pico_time_test.cc"],
    deps = [
        ":pico_time",
        "//googlesql/base/testing:googlesql_gtest_main",
        "//googlesql/base/testing:status_matchers",
        "//googlesql/public/types:timestamp_util",
        "@com_google_absl//absl/log",
        "@com_google_absl//absl/numeric:int128",
        "@com_google_absl//absl/status",
        "@com_google_absl//absl/strings:string_view",
        "@com_google_absl//absl/time",
    ],
)

cc_library(
    name = "timestamp_picos_value",
    srcs = ["timestamp_picos_value.cc"],
    hdrs = [
        "timestamp_picos_value.h",
    ],
    deps = [
        ":pico_time",
        "//googlesql/base:check",
        "//googlesql/base:endian",
        "//googlesql/base:status",
        "//googlesql/public/functions:date_time_util",
        "@com_google_absl//absl/base:core_headers",
        "@com_google_absl//absl/hash",
        "@com_google_absl//absl/numeric:int128",
        "@com_google_absl//absl/status",
        "@com_google_absl//absl/status:statusor",
        "@com_google_absl//absl/strings",
        "@com_google_absl//absl/strings:string_view",
        "@com_google_absl//absl/time",
    ],
)

cc_test(
    name = "timestamp_picos_value_test",
    srcs = ["timestamp_picos_value_test.cc"],
    deps = [
        ":timestamp_picos_value",
        "//googlesql/base/testing:googlesql_gtest_main",
        "//googlesql/base/testing:status_matchers",
        "//googlesql/public/types:timestamp_util",
        "@com_google_absl//absl/hash:hash_testing",
        "@com_google_absl//absl/numeric:int128",
        "@com_google_absl//absl/status",
        "@com_google_absl//absl/strings",
        "@com_google_absl//absl/time",
    ],
)

cc_library(
    name = "json_value",
    srcs = ["json_value.cc"],
    hdrs = [
        "json_value.h",
    ],
    deps = [
        ":numeric_parser",
        "//googlesql/base",
        "//googlesql/base:map_util",
        "//googlesql/base:status",
        "//googlesql/common:multiprecision_int",
        "@com_google_absl//absl/base:core_headers",
        "@com_google_absl//absl/hash",
        "@com_google_absl//absl/status",
        "@com_google_absl//absl/status:statusor",
        "@com_google_absl//absl/strings",
        "@com_google_absl//absl/types:compare",
        "@json//:singleheader-json",
    ],
)

cc_test(
    name = "json_value_test",
    srcs = ["json_value_test.cc"],
    deps = [
        ":json_value",
        "//googlesql/base:check",
        "//googlesql/base:status",
        "//googlesql/base/testing:googlesql_gtest_main",
        "//googlesql/base/testing:status_matchers",
        "//googlesql/compliance:depth_limit_detector_test_cases",
        "@com_google_absl//absl/container:flat_hash_map",
        "@com_google_absl//absl/log",
        "@com_google_absl//absl/status",
        "@com_google_absl//absl/status:statusor",
        "@com_google_absl//absl/strings",
        "@com_google_absl//absl/time",
        "@com_google_absl//absl/types:compare",
        "@com_google_absl//absl/types:span",
    ],
)

cc_library(
    name = "interval_value",
    srcs = ["interval_value.cc"],
    hdrs = [
        "interval_value.h",
    ],
    deps = [
        "//googlesql/base:check",
        "//googlesql/base:endian",
        "//googlesql/base:ret_check",
        "//googlesql/base:status",
        "//googlesql/common:multiprecision_int",
        "//googlesql/public/functions:datetime_cc_proto",
        "@com_google_absl//absl/base:core_headers",
        "@com_google_absl//absl/hash",
        "@com_google_absl//absl/numeric:int128",
        "@com_google_absl//absl/status",
        "@com_google_absl//absl/status:statusor",
        "@com_google_absl//absl/strings",
        "@com_google_absl//absl/strings:str_format",
        "@com_googlesource_code_re2//:re2",
    ],
)

cc_library(
    name = "interval_value_test_util",
    testonly = 1,
    hdrs = [
        "interval_value_test_util.h",
    ],
    deps = [
        ":interval_value",
        "@com_google_absl//absl/random",
        "@com_google_absl//absl/random:distributions",
        "@com_google_absl//absl/strings:str_format",
        "@com_google_googletest//:gtest",
    ],
)

cc_test(
    name = "interval_value_test",
    srcs = ["interval_value_test.cc"],
    deps = [
        ":interval_value",
        ":interval_value_test_util",
        "//googlesql/base/testing:googlesql_gtest_main",
        "//googlesql/base/testing:status_matchers",
        "//googlesql/public/functions:datetime_cc_proto",
        "@com_google_absl//absl/hash:hash_testing",
        "@com_google_absl//absl/numeric:int128",
        "@com_google_absl//absl/random",
        "@com_google_absl//absl/random:distributions",
        "@com_google_absl//absl/status",
        "@com_google_absl//absl/strings",
    ],
)

cc_library(
    name = "uuid_value",
    srcs = ["uuid_value.cc"],
    hdrs = [
        "uuid_value.h",
    ],
    deps = [
        "//googlesql/base:endian",
        "//googlesql/base:no_destructor",
        "//googlesql/base:status",
        "//googlesql/common:errors",
        "@com_google_absl//absl/base:core_headers",
        "@com_google_absl//absl/container:flat_hash_map",
        "@com_google_absl//absl/hash",
        "@com_google_absl//absl/status",
        "@com_google_absl//absl/status:statusor",
        "@com_google_absl//absl/strings",
    ],
)

cc_test(
    name = "uuid_value_test",
    srcs = ["uuid_value_test.cc"],
    deps = [
        ":uuid_value",
        "//googlesql/base/testing:googlesql_gtest_main",
        "//googlesql/base/testing:status_matchers",
        "//googlesql/public/functions:uuid",
        "@com_google_absl//absl/hash:hash_testing",
        "@com_google_absl//absl/random",
        "@com_google_absl//absl/status",
        "@com_google_absl//absl/status:statusor",
        "@com_google_absl//absl/strings",
        "@com_google_absl//absl/types:span",
    ],
)

cc_library(
    name = "value",
    srcs = [
        "proto_util.cc",
        "value.cc",
        "value_inl.h",
    ],
    hdrs = [
        "proto_util.h",
        "value.h",
    ],
    deps = [
        ":civil_time",
        ":interval_value",
        ":json_value",
        ":language_options",
        ":numeric_value",
        ":options_cc_proto",
        ":simple_token_list",
        ":timestamp_picos_value",
        ":type",
        ":type_cc_proto",
        ":uuid_value",
        ":value_cc_proto",
        ":value_content",
        "//googlesql/base",
        "//googlesql/base:check",
        "//googlesql/base:compact_reference_counted",
        "//googlesql/base:map_util",
        "//googlesql/base:map_view",
        "//googlesql/base:ret_check",
        "//googlesql/base:source_location",
        "//googlesql/base:status",
        "//googlesql/base:strings",
        "//googlesql/common:errors",
        "//googlesql/common:thread_stack",
        "//googlesql/public/functions:arithmetics",
        "//googlesql/public/functions:comparison",
        "//googlesql/public/functions:convert_string",
        "//googlesql/public/functions:date_time_util",
        "//googlesql/public/proto:type_annotation_cc_proto",
        "//googlesql/public/types",
        "//googlesql/public/types:timestamp_util",
        "//googlesql/public/types:value_representations",
        "@com_google_absl//absl/algorithm:container",
        "@com_google_absl//absl/base",
        "@com_google_absl//absl/base:core_headers",
        "@com_google_absl//absl/container:btree",
        "@com_google_absl//absl/container:flat_hash_map",
        "@com_google_absl//absl/container:flat_hash_set",
        "@com_google_absl//absl/container:inlined_vector",
        "@com_google_absl//absl/flags:flag",
        "@com_google_absl//absl/hash",
        "@com_google_absl//absl/log",
        "@com_google_absl//absl/memory",
        "@com_google_absl//absl/status",
        "@com_google_absl//absl/status:statusor",
        "@com_google_absl//absl/strings",
        "@com_google_absl//absl/strings:cord",
        "@com_google_absl//absl/strings:str_format",
        "@com_google_absl//absl/time",
        "@com_google_absl//absl/types:span",
        "@com_google_googletest//:gtest",
        "@com_google_protobuf//:protobuf",
        "@com_google_protobuf//:protobuf_lite",
    ],
)

cc_test(
    name = "value_test",
    size = "medium",
    timeout = "long",
    srcs = ["value_test.cc"],
    deps = [
        ":analyzer",
        ":builtin_function_options",
        ":civil_time",
        ":evaluator",
        ":interval_value",
        ":interval_value_test_util",
        ":json_value",
        ":language_options",
        ":numeric_value",
        ":options_cc_proto",
        ":pico_time",
        ":simple_catalog",
        ":simple_token_list",
        ":timestamp_picos_value",
        ":token_list_util",
        ":type",
        ":uuid_value",
        ":value",
        "//googlesql/base",
        "//googlesql/base/testing:googlesql_gtest_main",
        "//googlesql/base/testing:status_matchers",
        "//googlesql/common:float_margin",
        "//googlesql/common:internal_value",
        "//googlesql/common/testing:proto_matchers",
        "//googlesql/common/testing:testing_proto_util",
        "//googlesql/compliance:functions_testlib",
        "//googlesql/public/types",
        "//googlesql/public/types:value_representations",
        "//googlesql/testdata:test_proto3_cc_proto",
        "//googlesql/testdata:test_schema_cc_proto",
        "//googlesql/testing:test_value",
        "@com_google_absl//absl/algorithm:container",
        "@com_google_absl//absl/container:flat_hash_map",
        "@com_google_absl//absl/container:flat_hash_set",
        "@com_google_absl//absl/hash",
        "@com_google_absl//absl/hash:hash_testing",
        "@com_google_absl//absl/status",
        "@com_google_absl//absl/status:statusor",
        "@com_google_absl//absl/strings",
        "@com_google_absl//absl/strings:str_format",
        "@com_google_absl//absl/time",
        "@com_google_absl//absl/types:span",
        "@com_google_protobuf//:protobuf",
        "@com_google_protobuf//:protobuf_lite",
    ],
)

cc_test(
    name = "proto_util_test",
    size = "small",
    srcs = ["proto_util_test.cc"],
    deps = [
        ":civil_time",
        ":strings",
        ":timestamp_picos_value",
        ":type",
        ":type_cc_proto",
        ":value",
        "//googlesql/base",
        "//googlesql/base:check",
        "//googlesql/base:ret_check",
        "//googlesql/base:status",
        "//googlesql/base/testing:googlesql_gtest_main",
        "//googlesql/base/testing:status_matchers",
        "//googlesql/common/testing:proto_matchers",
        "//googlesql/common/testing:testing_proto_util",
        "//googlesql/public/proto:type_annotation_cc_proto",
        "//googlesql/public/types",
        "//googlesql/testdata:test_proto3_cc_proto",
        "//googlesql/testdata:test_schema_cc_proto",
        "@com_google_absl//absl/flags:flag",
        "@com_google_absl//absl/status:statusor",
        "@com_google_absl//absl/strings",
        "@com_google_absl//absl/strings:cord",
    ],
)

proto_library(
    name = "builtin_function_proto",
    srcs = ["builtin_function.proto"],
)

cc_proto_library(
    name = "builtin_function_cc_proto",
    deps = [":builtin_function_proto"],
)

java_proto_library(
    name = "builtin_function_java_proto",
    deps = [":builtin_function_proto"],
)

proto_library(
    name = "deprecation_warning_proto",
    srcs = ["deprecation_warning.proto"],
    deps = [":error_location_proto"],
)

cc_proto_library(
    name = "deprecation_warning_cc_proto",
    deps = [":deprecation_warning_proto"],
)

java_proto_library(
    name = "deprecation_warning_java_proto",
    deps = [":deprecation_warning_proto"],
)

proto_library(
    name = "function_proto",
    srcs = ["function.proto"],
)

cc_proto_library(
    name = "function_cc_proto",
    deps = [":function_proto"],
)

java_proto_library(
    name = "function_java_proto",
    deps = [":function_proto"],
)

proto_library(
    name = "parse_resume_location_proto",
    srcs = ["parse_resume_location.proto"],
)

cc_proto_library(
    name = "parse_resume_location_cc_proto",
    deps = [":parse_resume_location_proto"],
)

java_proto_library(
    name = "parse_resume_location_java_proto",
    deps = [":parse_resume_location_proto"],
)

proto_library(
    name = "options_proto",
    srcs = ["options.proto"],
    deps = ["@com_google_protobuf//:descriptor_proto"],
)

cc_proto_library(
    name = "options_cc_proto",
    deps = [":options_proto"],
)

java_proto_library(
    name = "options_java_proto",
    deps = [":options_proto"],
)

proto_library(
    name = "simple_table_proto",
    srcs = ["simple_table.proto"],
    deps = [
        ":annotation_proto",
        ":type_proto",
    ],
)

cc_proto_library(
    name = "simple_table_cc_proto",
    deps = [":simple_table_proto"],
)

java_proto_library(
    name = "simple_table_java_proto",
    deps = [":simple_table_proto"],
)

proto_library(
    name = "simple_constant_proto",
    srcs = ["simple_constant.proto"],
    deps = [
        ":type_proto",
        ":value_proto",
    ],
)

cc_proto_library(
    name = "simple_constant_cc_proto",
    deps = [":simple_constant_proto"],
)

java_proto_library(
    name = "simple_constant_java_proto",
    deps = [":simple_constant_proto"],
)

proto_library(
    name = "simple_connection_proto",
    srcs = ["simple_connection.proto"],
)

cc_proto_library(
    name = "simple_connection_cc_proto",
    deps = [":simple_connection_proto"],
)

java_proto_library(
    name = "simple_connection_java_proto",
    deps = [":simple_connection_proto"],
)

proto_library(
    name = "simple_model_proto",
    srcs = ["simple_model.proto"],
    deps = [":simple_table_proto"],
)

cc_proto_library(
    name = "simple_model_cc_proto",
    deps = [":simple_model_proto"],
)

java_proto_library(
    name = "simple_model_java_proto",
    deps = [":simple_model_proto"],
)

proto_library(
    name = "simple_sequence_proto",
    srcs = ["simple_sequence.proto"],
)

cc_proto_library(
    name = "simple_sequence_cc_proto",
    deps = [":simple_sequence_proto"],
)

java_proto_library(
    name = "simple_sequence_java_proto",
    deps = [":simple_sequence_proto"],
)

proto_library(
    name = "parse_location_range_proto",
    srcs = ["parse_location_range.proto"],
)

cc_proto_library(
    name = "parse_location_range_cc_proto",
    deps = [":parse_location_range_proto"],
)

java_proto_library(
    name = "parse_location_range_java_proto",
    deps = [":parse_location_range_proto"],
)

proto_library(
    name = "type_proto",
    srcs = ["type.proto"],
    deps = ["@com_google_protobuf//:descriptor_proto"],
)

cc_proto_library(
    name = "type_cc_proto",
    deps = [":type_proto"],
)

java_proto_library(
    name = "type_proto_java_proto",
    deps = [":type_proto"],
)

proto_library(
    name = "type_annotation_proto",
    srcs = ["type_annotation.proto"],
    deps = ["//googlesql/public/proto:type_annotation_proto"],
)

cc_proto_library(
    name = "type_annotation_cc_proto",
    deps = [":type_annotation_proto"],
)

java_proto_library(
    name = "type_annotation_java_proto",
    deps = [":type_annotation_proto"],
)

proto_library(
    name = "value_proto",
    srcs = ["value.proto"],
    deps = [
        "@com_google_protobuf//:timestamp_proto",
    ],
)

cc_proto_library(
    name = "value_cc_proto",
    deps = [":value_proto"],
)

java_proto_library(
    name = "value_java_proto",
    deps = [":value_proto"],
)

proto_library(
    name = "type_parameters_proto",
    srcs = ["type_parameters.proto"],
    deps = [":simple_value_proto"],
)

cc_proto_library(
    name = "type_parameters_cc_proto",
    deps = [":type_parameters_proto"],
)

java_proto_library(
    name = "type_parameters_java_proto",
    deps = [":type_parameters_proto"],
)

proto_library(
    name = "collation_proto",
    srcs = ["collation.proto"],
)

cc_proto_library(
    name = "collation_cc_proto",
    deps = [":collation_proto"],
)

java_proto_library(
    name = "collation_java_proto",
    deps = [":collation_proto"],
)

proto_library(
    name = "type_modifiers_proto",
    srcs = ["type_modifiers.proto"],
    deps = [
        ":collation_proto",
        ":type_parameters_proto",
    ],
)

cc_proto_library(
    name = "type_modifiers_cc_proto",
    deps = [":type_modifiers_proto"],
)

java_proto_library(
    name = "type_modifiers_java_proto",
    deps = [":type_modifiers_proto"],
)

proto_library(
    name = "error_location_proto",
    srcs = ["error_location.proto"],
)

proto_library(
    name = "formatter_options_proto",
    srcs = ["formatter_options.proto"],
)

cc_proto_library(
    name = "formatter_options_cc_proto",
    deps = [":formatter_options_proto"],
)

java_proto_library(
    name = "formatter_options_java_proto",
    deps = [":formatter_options_proto"],
)

cc_proto_library(
    name = "error_location_cc_proto",
    deps = [":error_location_proto"],
)

java_proto_library(
    name = "error_location_java_proto",
    deps = [":error_location_proto"],
)

cc_library(
    name = "error_helpers",
    srcs = ["error_helpers.cc"],
    hdrs = ["error_helpers.h"],
    features = ["header_modules"],
    deps = [
        ":deprecation_warning_cc_proto",
        ":error_location_cc_proto",
        ":fix_suggestion_cc_proto",
        ":options_cc_proto",
        ":parse_location",
        "//googlesql/base:check",
        "//googlesql/base:ret_check",
        "//googlesql/base:status",
        "//googlesql/common:status_payload_utils",
        "//googlesql/common:utf_util",
        "//googlesql/proto:internal_error_location_cc_proto",
        "//googlesql/proto:internal_fix_suggestion_cc_proto",
        "//googlesql/proto:script_exception_cc_proto",
        "@com_google_absl//absl/base:core_headers",
        "@com_google_absl//absl/flags:flag",
        "@com_google_absl//absl/log",
        "@com_google_absl//absl/status",
        "@com_google_absl//absl/status:statusor",
        "@com_google_absl//absl/strings",
        "@com_google_absl//absl/strings:cord",
        "@com_google_absl//absl/strings:str_format",
        "@com_googlesource_code_re2//:re2",
    ],
)

cc_test(
    name = "error_helpers_test",
    size = "small",
    srcs = ["error_helpers_test.cc"],
    deps = [
        ":analyzer_options",
        ":deprecation_warning_cc_proto",
        ":error_helpers",
        ":error_location_cc_proto",
        ":fix_suggestion_cc_proto",
        ":options_cc_proto",
        ":parse_location",
        "//googlesql/base",
        "//googlesql/base:enum_utils",
        "//googlesql/base:map_util",
        "//googlesql/base:status",
        "//googlesql/base/testing:googlesql_gtest_main",
        "//googlesql/base/testing:status_matchers",
        "//googlesql/common:errors",
        "//googlesql/common:status_payload_utils",
        "//googlesql/common/testing:proto_matchers",
        "//googlesql/common/testing:status_payload_matchers",
        "//googlesql/proto:internal_error_location_cc_proto",
        "//googlesql/proto:internal_fix_suggestion_cc_proto",
        "//googlesql/proto:script_exception_cc_proto",
        "//googlesql/testdata:test_schema_cc_proto",
        "@com_google_absl//absl/flags:flag",
        "@com_google_absl//absl/status",
        "@com_google_absl//absl/strings",
    ],
)

cc_library(
    name = "proto_value_conversion",
    srcs = ["proto_value_conversion.cc"],
    hdrs = ["proto_value_conversion.h"],
    deps = [
        ":json_value",
        ":numeric_value",
        ":options_cc_proto",
        ":pico_time",
        ":timestamp_picos_value",
        ":type",
        ":type_cc_proto",
        ":uuid_value",
        ":value",
        ":value_cc_proto",
        "//googlesql/base:ret_check",
        "//googlesql/base:source_location",
        "//googlesql/base:status",
        "//googlesql/public/functions:date_time_util",
        "//googlesql/public/functions:range",
        "//googlesql/public/proto:type_annotation_cc_proto",
        "@com_google_absl//absl/status",
        "@com_google_absl//absl/status:statusor",
        "@com_google_absl//absl/strings",
        "@com_google_absl//absl/strings:cord",
        "@com_google_absl//absl/time",
        "@com_google_protobuf//:protobuf",
    ],
)

cc_test(
    name = "proto_value_conversion_test",
    size = "small",
    srcs = ["proto_value_conversion_test.cc"],
    deps = [
        ":analyzer",
        ":builtin_function_options",
        ":language_options",
        ":options_cc_proto",
        ":proto_value_conversion",
        ":simple_catalog",
        ":timestamp_picos_value",
        ":type",
        ":type_cc_proto",
        ":value",
        ":value_cc_proto",
        "//googlesql/base",
        "//googlesql/base:path",
        "@com_google_protobuf//:protobuf",
        "//googlesql/common/testing:proto_matchers",
        "//googlesql/base/testing:status_matchers",
        "//googlesql/public/proto:type_annotation_cc_proto",
        ":token_list_util",
        "//googlesql/public/types",
        "//googlesql/resolved_ast",
        "//googlesql/resolved_ast:resolved_node_kind_cc_proto",
        # buildcleaner: keep (text format proto)
        "//googlesql/testdata:test_schema_cc_proto",
        "//googlesql/base/testing:googlesql_gtest_main",
        "@com_google_absl//absl/functional:bind_front",
        "@com_google_absl//absl/status",
        "@com_google_absl//absl/status:statusor",
        "@com_google_absl//absl/strings:cord",
        "@com_google_absl//absl/time",
        "@com_google_absl//absl/strings",
        "//googlesql/base:map_util",
        "//googlesql/base:status",
        "//googlesql/base:ret_check",
    ],
)

cc_library(
    name = "strings",
    srcs = ["strings.cc"],
    hdrs = ["strings.h"],
    deps = [
        ":id_string",
        ":language_options",
        ":options_cc_proto",
        "//googlesql/base:case",
        "//googlesql/base:check",
        "//googlesql/base:map_util",
        "//googlesql/base:ret_check",
        "//googlesql/base:status",
        "//googlesql/common:errors",
        "//googlesql/common:utf_util",
        "//googlesql/parser:keywords",
        "@com_google_absl//absl/algorithm:container",
        "@com_google_absl//absl/base:core_headers",
        "@com_google_absl//absl/base:no_destructor",
        "@com_google_absl//absl/container:flat_hash_set",
        "@com_google_absl//absl/status",
        "@com_google_absl//absl/status:statusor",
        "@com_google_absl//absl/strings",
        "@com_google_absl//absl/types:span",
        "@icu//:headers",
    ],
)

cc_test(
    name = "strings_test",
    size = "small",
    srcs = ["strings_test.cc"],
    deps = [
        ":id_string",
        ":language_options",
        ":options_cc_proto",
        ":strings",
        "//googlesql/base:status",
        "//googlesql/base/testing:googlesql_gtest_main",
        "//googlesql/base/testing:status_matchers",
        "//googlesql/common:utf_util",
        "//googlesql/parser",
        "@com_google_absl//absl/base:core_headers",
        "@com_google_absl//absl/container:btree",
        "@com_google_absl//absl/container:flat_hash_set",
        "@com_google_absl//absl/status",
        "@com_google_absl//absl/strings",
        "@com_google_absl//absl/strings:str_format",
    ],
)

cc_library(
    name = "builtin_function_options",
    srcs = ["builtin_function_options.cc"],
    hdrs = ["builtin_function_options.h"],
    deps = [
        ":builtin_function_cc_proto",
        ":language_options",
        "//googlesql/proto:options_cc_proto",
        "//googlesql/public/types",
        "@com_google_absl//absl/container:flat_hash_map",
        "@com_google_absl//absl/container:flat_hash_set",
        "@com_google_absl//absl/functional:bind_front",
    ],
)

cc_library(
    name = "sql_procedure",
    hdrs = ["sql_procedure.h"],
    deps = [
        ":catalog",
        ":function",
        ":module_details",
        "//googlesql/resolved_ast",
        "@com_google_absl//absl/memory",
        "@com_google_absl//absl/status:statusor",
    ],
)

cc_test(
    name = "sql_procedure_test",
    srcs = ["sql_procedure_test.cc"],
    deps = [
        ":analyzer",
        ":analyzer_options",
        ":analyzer_output",
        ":builtin_function_options",
        ":module_details",
        ":simple_catalog",
        ":sql_procedure",
        "//googlesql/base/testing:googlesql_gtest_main",
        "//googlesql/base/testing:status_matchers",
        "//googlesql/public/types",
        "//googlesql/resolved_ast",
    ],
)

cc_library(
    name = "builtin_function",
    srcs = ["builtin_function.cc"],
    hdrs = ["builtin_function.h"],
    deps = [
        ":builtin_function_cc_proto",
        ":builtin_function_options",
        ":function",
        ":language_options",
        ":options_cc_proto",
        ":type",
        "//googlesql/base:check",
        "//googlesql/base:map_util",
        "//googlesql/base:status",
        "//googlesql/common:builtin_function_internal",
        "//googlesql/common:builtins_output_properties",
        "//googlesql/proto:options_cc_proto",
        "//googlesql/public/types",
        "//googlesql/public/types:type",
        "@com_google_absl//absl/base:core_headers",
        "@com_google_absl//absl/container:flat_hash_map",
        "@com_google_absl//absl/container:flat_hash_set",
        "@com_google_absl//absl/status",
        "@com_google_absl//absl/strings",
    ],
)

# This header library allows implementation details of some of
# the private member function to be factored out for re-use in
# contexts where the respective class is not required.
cc_library(
    name = "function_headers",
    hdrs = [
        "function.h",
        "function_signature.h",
        "input_argument_type.h",
    ],
    deps = [
        ":builtin_function_cc_proto",
        ":constness_level_cc_proto",
        ":deprecation_warning_cc_proto",
        ":function_cc_proto",
        ":id_string",
        ":language_options",
        ":options_cc_proto",
        ":type",
        ":type_cc_proto",
        ":value",
        "//googlesql/base",
        "//googlesql/base:case",
        "//googlesql/base:check",
        "//googlesql/base:map_util",
        "//googlesql/base:status",
        "//googlesql/proto:function_cc_proto",
        "//googlesql/public/types",
        "//googlesql/resolved_ast:resolved_ast_enums_cc_proto",
        "@com_google_absl//absl/algorithm:container",
        "@com_google_absl//absl/base:core_headers",
        "@com_google_absl//absl/container:flat_hash_map",
        "@com_google_absl//absl/container:flat_hash_set",
        "@com_google_absl//absl/hash",
        "@com_google_absl//absl/status",
        "@com_google_absl//absl/status:statusor",
        "@com_google_absl//absl/strings",
        "@com_google_absl//absl/types:optional",
        "@com_google_absl//absl/types:span",
    ],
)

cc_library(
    name = "function",
    srcs = [
        "function.cc",
        "function_signature.cc",
        "input_argument_type.cc",
        "procedure.cc",
        "table_valued_function.cc",
    ],
    hdrs = [
        "function.h",
        "function_signature.h",
        "input_argument_type.h",
        "procedure.h",
        "table_valued_function.h",
    ],
    deps = [
        ":builtin_function_cc_proto",
        ":catalog",
        ":constness_level_cc_proto",
        ":deprecation_warning_cc_proto",
        ":evaluator_table_iterator",
        ":function_cc_proto",
        ":function_headers",
        ":id_string",
        ":language_options",
        ":options_cc_proto",
        ":parse_location",
        ":signature_match_result",
        ":simple_table_cc_proto",
        ":strings",
        ":type",
        ":type_cc_proto",
        ":value",
        "//googlesql/base",
        "//googlesql/base:case",
        "//googlesql/base:check",
        "//googlesql/base:map_util",
        "//googlesql/base:ret_check",
        "//googlesql/base:status",
        "//googlesql/base:strings",
        "//googlesql/common:errors",
        "//googlesql/common:function_utils",
        "//googlesql/proto:function_cc_proto",
        "//googlesql/public/types",
        "//googlesql/resolved_ast:resolved_ast_enums_cc_proto",
        "//googlesql/resolved_ast:serialization_cc_proto",
        "@com_google_absl//absl/algorithm:container",
        "@com_google_absl//absl/base:core_headers",
        "@com_google_absl//absl/base:no_destructor",
        "@com_google_absl//absl/container:btree",
        "@com_google_absl//absl/container:flat_hash_map",
        "@com_google_absl//absl/container:flat_hash_set",
        "@com_google_absl//absl/hash",
        "@com_google_absl//absl/log",
        "@com_google_absl//absl/status",
        "@com_google_absl//absl/status:statusor",
        "@com_google_absl//absl/strings",
        "@com_google_absl//absl/types:optional",
        "@com_google_absl//absl/types:span",
        "@com_google_protobuf//:protobuf",
        "@com_googlesource_code_re2//:re2",
    ],
)

cc_library(
    name = "sql_function",
    srcs = ["sql_function.cc"],
    hdrs = ["sql_function.h"],
    deps = [
        ":function",
        ":function_cc_proto",
        ":parse_resume_location",
        "//googlesql/base:ret_check",
        "//googlesql/resolved_ast",
        "@com_google_absl//absl/base:core_headers",
        "@com_google_absl//absl/memory",
        "@com_google_absl//absl/status",
        "@com_google_absl//absl/status:statusor",
        "@com_google_absl//absl/strings",
        "@com_google_absl//absl/strings:string_view",
    ],
)

cc_library(
    name = "non_sql_function",
    srcs = ["non_sql_function.cc"],
    hdrs = ["non_sql_function.h"],
    deps = [
        ":function",
        ":module_details",
        ":parse_resume_location",
        "//googlesql/base:ret_check",
        "//googlesql/base:status",
        "//googlesql/resolved_ast",
        "@com_google_absl//absl/status",
        "@com_google_absl//absl/strings",
        "@com_google_absl//absl/types:optional",
    ],
)

cc_library(
    name = "templated_sql_function",
    srcs = [
        "templated_sql_function.cc",
    ],
    hdrs = [
        "templated_sql_function.h",
    ],
    deps = [
        ":error_location_cc_proto",
        ":function",
        ":parse_resume_location",
        ":type",
        "//googlesql/base",
        "//googlesql/base:check",
        "//googlesql/base:ret_check",
        "//googlesql/base:status",
        "//googlesql/proto:function_cc_proto",
        "//googlesql/proto:internal_error_location_cc_proto",
        "//googlesql/public/types",
        "//googlesql/resolved_ast",
        "@com_google_absl//absl/base:core_headers",
        "@com_google_absl//absl/status",
        "@com_google_absl//absl/strings",
        "@com_google_protobuf//:protobuf",
    ],
)

cc_test(
    name = "templated_sql_function_test",
    srcs = ["templated_sql_function_test.cc"],
    deps = [
        ":function_headers",
        ":parse_resume_location",
        ":templated_sql_function",
        "//googlesql/base/testing:googlesql_gtest_main",
        "//googlesql/base/testing:status_matchers",
        "//googlesql/public/types",
    ],
)

cc_library(
    name = "anon_function",
    srcs = ["anon_function.cc"],
    hdrs = ["anon_function.h"],
    deps = [
        ":function",
        ":function_cc_proto",
        ":language_options",
        "@com_google_absl//absl/functional:bind_front",
        "@com_google_absl//absl/strings",
        "@com_google_absl//absl/types:span",
    ],
)

cc_library(
    name = "sql_tvf",
    srcs = ["sql_tvf.cc"],
    hdrs = ["sql_tvf.h"],
    deps = [
        ":catalog",
        ":function",
        "//googlesql/base:ret_check",
        "//googlesql/base:status",
        "//googlesql/public/types",
        "//googlesql/resolved_ast",
        "@com_google_absl//absl/status",
    ],
)

cc_library(
    name = "templated_sql_tvf_no_resolver",
    hdrs = ["templated_sql_tvf.h"],
    deps = [
        ":catalog",
        ":error_helpers",
        ":function",
        ":options_cc_proto",
        ":parse_resume_location",
        ":type",
        "//googlesql/resolved_ast",
        "@com_google_absl//absl/base:core_headers",
        "@com_google_absl//absl/status",
        "@com_google_absl//absl/strings",
    ],
)

cc_library(
    name = "templated_sql_tvf",
    srcs = ["templated_sql_tvf.cc"],
    hdrs = ["templated_sql_tvf.h"],
    deps = [
        ":catalog",
        ":cycle_detector",
        ":deprecation_warning_cc_proto",
        ":error_helpers",
        ":error_location_cc_proto",
        ":function",
        ":function_cc_proto",
        ":id_string",
        ":options_cc_proto",
        ":parse_location",
        ":parse_resume_location",
        ":simple_table_cc_proto",
        ":strings",
        ":type",
        "//googlesql/analyzer:name_scope",
        "//googlesql/analyzer:resolver",
        "//googlesql/base",
        "//googlesql/base:map_util",
        "//googlesql/base:ret_check",
        "//googlesql/base:status",
        "//googlesql/common:errors",
        "//googlesql/common:status_payload_utils",
        "//googlesql/parser",
        "//googlesql/proto:function_cc_proto",
        "//googlesql/proto:internal_error_location_cc_proto",
        "//googlesql/public/types",
        "//googlesql/resolved_ast",
        "//googlesql/resolved_ast:resolved_ast_enums_cc_proto",
        "//googlesql/resolved_ast:resolved_node_kind_cc_proto",
        "@com_google_absl//absl/base:core_headers",
        "@com_google_absl//absl/status",
        "@com_google_absl//absl/strings",
    ],
)

cc_library(
    name = "sql_view",
    hdrs = ["sql_view.h"],
    deps = [
        ":catalog",
        "//googlesql/resolved_ast:resolved_ast_enums_cc_proto",
    ],
)

# :collator_lite is a basic collator implementation optimized for binary size,
# supporting only the "unicode:cs" binary-comparison collation. You most likely
# want :collator instead.
cc_library(
    name = "collator_lite",
    srcs = ["collator_lite.cc"],
    hdrs = ["collator.h"],
    deps = [
        "//googlesql/base:ret_check",
        "//googlesql/base:status",
        "@com_google_absl//absl/base:core_headers",
        "@com_google_absl//absl/status",
        "@com_google_absl//absl/status:statusor",
        "@com_google_absl//absl/strings",
        "@com_google_absl//absl/synchronization",
        "@icu//:headers",
    ],
)

cc_test(
    name = "collator_lite_test",
    srcs = ["collator_lite_test.cc"],
    deps = [
        ":collator_lite",
        "//googlesql/base/testing:googlesql_gtest_main",
        "//googlesql/base/testing:status_matchers",
        "@com_google_googletest//:gtest",
    ],
)

cc_library(
    name = "collator",
    srcs = ["collator.cc"],
    hdrs = ["collator.h"],
    deps = [
        ":collator_lite",
        "//googlesql/base",
        "//googlesql/base:ret_check",
        "//googlesql/base:status",
        "//googlesql/common:errors",
        "@com_google_absl//absl/memory",
        "@com_google_absl//absl/status",
        "@com_google_absl//absl/status:statusor",
        "@com_google_absl//absl/strings",
        "@com_google_absl//absl/strings:cord",
        "@icu",  # buildcleaner: keep
        "@icu//:common",
    ],
)

cc_library(
    name = "sql_constant",
    srcs = ["sql_constant.cc"],
    hdrs = ["sql_constant.h"],
    deps = [
        ":constant",
        ":error_helpers",
        ":parse_location",
        ":value",
        "//googlesql/base:check",
        "//googlesql/base:ret_check",
        "//googlesql/public/types:type",
        "//googlesql/resolved_ast",
        "@com_google_absl//absl/status",
        "@com_google_absl//absl/status:statusor",
        "@com_google_absl//absl/strings",
    ],
)

cc_library(
    name = "constant",
    hdrs = ["constant.h"],
    deps = [
        ":type",
        ":value",
        "//googlesql/base",
        "//googlesql/base:ret_check",
        "@com_google_absl//absl/strings",
    ],
)

cc_test(
    name = "cast_test",
    size = "small",
    srcs = ["cast_test.cc"],
    deps = [
        ":coercer",
        ":function",
        ":json_value",
        ":language_options",
        ":options_cc_proto",
        ":type",
        ":value",
        "//googlesql/base",
        "//googlesql/base:status",
        "//googlesql/base/testing:googlesql_gtest_main",
        "//googlesql/base/testing:status_matchers",
        "//googlesql/common:graph_element_utils",
        "//googlesql/compliance:functions_testlib",
        "//googlesql/public/types",
        "//googlesql/testdata:test_schema_cc_proto",
        "//googlesql/testing:test_function",
        "//googlesql/testing:test_value",
        "@com_google_absl//absl/base",
        "@com_google_absl//absl/status",
        "@com_google_absl//absl/status:statusor",
        "@com_google_absl//absl/strings",
        "@com_google_absl//absl/strings:cord",
        "@com_google_absl//absl/time",
        "@com_google_absl//absl/types:span",
    ],
)

cc_test(
    name = "function_test",
    size = "medium",
    srcs = ["function_test.cc"],
    deps = [
        ":builtin_function",
        ":builtin_function_options",
        ":deprecation_warning_cc_proto",
        ":error_location_cc_proto",
        ":function",
        ":function_cc_proto",
        ":language_options",
        ":parse_location",
        ":parse_location_range_cc_proto",
        ":sql_function",
        ":type",
        ":value",
        "//googlesql/base",
        "//googlesql/base:case",
        "//googlesql/base:check",
        "//googlesql/base/testing:googlesql_gtest_main",
        "//googlesql/base/testing:status_matchers",
        "//googlesql/common:function_signature_testutil",
        "//googlesql/common/testing:proto_matchers",
        "//googlesql/common/testing:testing_proto_util",
        "//googlesql/proto:function_cc_proto",
        "//googlesql/public/types",
        "//googlesql/resolved_ast",
        "@com_google_absl//absl/container:flat_hash_map",
        "@com_google_absl//absl/container:flat_hash_set",
        "@com_google_absl//absl/strings",
        "@com_google_absl//absl/types:span",
    ],
)

cc_test(
    name = "function_signature_test",
    size = "small",
    timeout = "moderate",
    srcs = ["function_signature_test.cc"],
    deps = [
        ":error_location_cc_proto",
        ":function",
        ":function_cc_proto",
        ":options_cc_proto",
        ":type",
        ":value",
        "//googlesql/base:status",
        "//googlesql/base/testing:googlesql_gtest_main",
        "//googlesql/base/testing:status_matchers",
        "//googlesql/common:function_signature_testutil",
        "//googlesql/proto:function_cc_proto",
        "//googlesql/public/types",
        "@com_google_absl//absl/base:log_severity",
        "@com_google_absl//absl/status",
        "@com_google_absl//absl/strings",
        "@com_google_absl//absl/types:span",
    ],
)

cc_test(
    name = "input_argument_type_test",
    size = "small",
    srcs = ["input_argument_type_test.cc"],
    deps = [
        ":function",
        ":id_string",
        ":type",
        "//googlesql/base/testing:googlesql_gtest_main",
        "//googlesql/base/testing:status_matchers",
        "//googlesql/testdata:test_schema_cc_proto",
        "@com_google_absl//absl/strings",
        "@com_google_absl//absl/strings:cord",
        "@com_google_absl//absl/types:span",
    ],
)

cc_test(
    name = "procedure_test",
    size = "small",
    srcs = ["procedure_test.cc"],
    deps = [
        ":function",
        "//googlesql/base/testing:googlesql_gtest_main",
        "//googlesql/base/testing:status_matchers",
        "//googlesql/common/testing:proto_matchers",
        "//googlesql/public/types",
        "@com_google_absl//absl/strings",
        "@com_google_protobuf//:protobuf",
    ],
)

cc_test(
    name = "collator_test",
    size = "small",
    srcs = ["collator_test.cc"],
    deps = [
        ":collator",
        "//googlesql/base/testing:googlesql_gtest_main",
        "//googlesql/base/testing:status_matchers",
        "@com_google_absl//absl/status",
        "@com_google_absl//absl/status:statusor",
        "@com_google_absl//absl/strings",
        "@com_google_absl//absl/strings:cord",
        "@com_google_absl//absl/strings:str_format",
    ],
)

cc_library(
    name = "parse_resume_location",
    hdrs = ["parse_resume_location.h"],
    deps = [
        ":parse_resume_location_cc_proto",
        "//googlesql/base:ret_check",
        "//googlesql/base:status",
        "@com_google_absl//absl/strings",
    ],
)

cc_test(
    name = "parse_resume_location_test",
    size = "small",
    srcs = ["parse_resume_location_test.cc"],
    deps = [
        ":parse_resume_location",
        ":parse_resume_location_cc_proto",
        "//googlesql/base/testing:googlesql_gtest_main",
        "@com_google_protobuf//:protobuf",
    ],
)

cc_library(
    name = "parse_location",
    srcs = ["parse_location.cc"],
    hdrs = ["parse_location.h"],
    features = ["header_modules"],
    deps = [
        ":parse_location_range_cc_proto",
        "//googlesql/base",
        "//googlesql/base:check",
        "//googlesql/base:mathutil",
        "//googlesql/base:ret_check",
        "//googlesql/base:status",
        "//googlesql/proto:internal_error_location_cc_proto",
        "@com_google_absl//absl/base:core_headers",
        "@com_google_absl//absl/status:statusor",
        "@com_google_absl//absl/strings",
        "@icu//:common",
    ],
)

cc_test(
    name = "parse_location_test",
    size = "small",
    srcs = ["parse_location_test.cc"],
    deps = [
        ":parse_location",
        "//googlesql/base:status",
        "//googlesql/base/testing:googlesql_gtest_main",
        "//googlesql/base/testing:status_matchers",
        "//googlesql/proto:internal_error_location_cc_proto",
    ],
)

cc_library(
    name = "parse_helpers",
    srcs = [
        "parse_helpers.cc",
        "parse_tokens.cc",
    ],
    hdrs = [
        "parse_helpers.h",
        "parse_tokens.h",
    ],
    deps = [
        ":error_helpers",
        ":language_options",
        ":options_cc_proto",
        ":parse_location",
        ":parse_resume_location",
        ":strings",
        ":value",
        "//googlesql/base",
        "//googlesql/base:arena",
        "//googlesql/base:check",
        "//googlesql/base:ret_check",
        "//googlesql/base:status",
        "//googlesql/common:errors",
        "//googlesql/parser",
        "//googlesql/parser:keywords",
        "//googlesql/parser:lookahead_transformer",
        "//googlesql/parser:parser_mode",
        "//googlesql/parser:token",
        "//googlesql/parser:token_with_location",
        "//googlesql/parser:tokenizer",
        "//googlesql/public/functions:convert_string",
        "//googlesql/resolved_ast:resolved_node_kind_cc_proto",
        "@com_google_absl//absl/container:flat_hash_map",
        "@com_google_absl//absl/container:flat_hash_set",
        "@com_google_absl//absl/status",
        "@com_google_absl//absl/status:statusor",
        "@com_google_absl//absl/strings",
        "@com_google_absl//absl/types:span",
    ],
)

cc_test(
    name = "parse_tokens_test",
    size = "small",
    srcs = ["parse_tokens_test.cc"],
    deps = [
        ":error_helpers",
        ":options_cc_proto",
        ":parse_helpers",
        ":parse_location",
        ":parse_resume_location",
        "//googlesql/base:path",
        "//googlesql/base/testing:googlesql_gtest_main",
        "//googlesql/base/testing:status_matchers",
        "@com_google_absl//absl/status",
        "@com_google_absl//absl/strings",
        "@com_google_absl//absl/types:span",
    ],
)

cc_library(
    name = "language_options",
    srcs = ["language_options.cc"],
    hdrs = ["language_options.h"],
    deps = [
        ":options_cc_proto",
        "//googlesql/base",
        "//googlesql/base:case",
        "//googlesql/base:map_util",
        "//googlesql/base:status",
        "//googlesql/parser:keywords",
        "//googlesql/proto:options_cc_proto",
        "//googlesql/resolved_ast:resolved_node_kind_cc_proto",
        "@com_google_absl//absl/base:core_headers",
        "@com_google_absl//absl/container:btree",
        "@com_google_absl//absl/container:flat_hash_set",
        "@com_google_absl//absl/hash",
        "@com_google_absl//absl/status",
        "@com_google_absl//absl/strings",
        "@com_google_googletest//:gtest",
        "@com_google_protobuf//:protobuf",
    ],
)

cc_test(
    name = "language_options_test",
    size = "small",
    srcs = ["language_options_test.cc"],
    deps = [
        ":language_options",
        ":options_cc_proto",
        "//googlesql/base/testing:googlesql_gtest_main",
        "//googlesql/base/testing:status_matchers",
        "//googlesql/resolved_ast:resolved_node_kind_cc_proto",
        "@com_google_absl//absl/container:flat_hash_set",
        "@com_google_absl//absl/status",
        "@com_google_absl//absl/strings",
        "@com_google_protobuf//:protobuf",
        "@com_googlesource_code_re2//:re2",
    ],
)

cc_library(
    name = "analyzer_options",
    srcs = ["analyzer_options.cc"],
    hdrs = ["analyzer_options.h"],
    deps = [
        ":catalog",
        ":constant_evaluator",
        ":error_helpers",
        ":id_string",
        ":options_cc_proto",
        ":rewriter_interface",
        ":time_zone_util",
        ":type",
        ":type_cc_proto",
        "//googlesql/base",
        "//googlesql/base:arena",
        "//googlesql/base:check",
        "//googlesql/base:map_util",
        "//googlesql/base:ret_check",
        "//googlesql/base:status",
        "//googlesql/base:strings",
        "//googlesql/common:errors",
        "//googlesql/parser",
        "//googlesql/parser:parser_mode",
        "//googlesql/proto:options_cc_proto",
        "//googlesql/public/types",
        "//googlesql/resolved_ast",
        "//googlesql/resolved_ast:resolved_ast_builder",
        "@com_google_absl//absl/base:core_headers",
        "@com_google_absl//absl/base:nullability",
        "@com_google_absl//absl/container:btree",
        "@com_google_absl//absl/container:flat_hash_map",
        "@com_google_absl//absl/container:flat_hash_set",
        "@com_google_absl//absl/flags:flag",
        "@com_google_absl//absl/status",
        "@com_google_absl//absl/status:statusor",
        "@com_google_absl//absl/strings",
        "@com_google_absl//absl/time",
        "@com_google_absl//absl/types:span",
        "@com_google_protobuf//:protobuf",
        "@com_google_protobuf//:protobuf_lite",
    ],
)

cc_test(
    name = "analyzer_options_test",
    srcs = ["analyzer_options_test.cc"],
    deps = [
        ":analyzer_options",
        ":language_options",
        ":options_cc_proto",
        "//googlesql/base",
        "//googlesql/base/testing:googlesql_gtest_main",
        "//googlesql/base/testing:status_matchers",  # buildcleaner: keep
        "@com_google_absl//absl/status",
    ],
)

cc_library(
    name = "analyzer_output_properties",
    hdrs = ["analyzer_output_properties.h"],
    deps = [
        ":options_cc_proto",
        "//googlesql/resolved_ast:target_syntax",
        "@com_google_absl//absl/base:core_headers",
        "@com_google_absl//absl/container:btree",
        "@com_google_absl//absl/container:flat_hash_set",
        "@com_google_absl//absl/strings:string_view",
    ],
)

cc_library(
    name = "analyzer_output",
    srcs = ["analyzer_output.cc"],
    hdrs = ["analyzer_output.h"],
    deps = [
        ":analyzer_options",
        ":analyzer_output_properties",
        ":id_string",
        ":language_options",
        ":options_cc_proto",
        "//googlesql/base:arena",
        "//googlesql/base:enum_utils",
        "//googlesql/base:map_util",
        "//googlesql/common:timer_util",
        "//googlesql/parser",
        "//googlesql/public/proto:logging_cc_proto",
        "//googlesql/public/types",
        "//googlesql/resolved_ast",
        "@com_google_absl//absl/container:flat_hash_map",
        "@com_google_absl//absl/status",
        "@com_google_absl//absl/strings",
        "@com_google_absl//absl/strings:str_format",
        "@com_google_absl//absl/time",
    ],
)

cc_library(
    name = "analyzer",
    srcs = [
        "analyzer.cc",
        "table_name_resolver.cc",
        "table_name_resolver.h",
    ],
    hdrs = [
        "analyzer.h",
    ],
    deps = [
        ":analyzer_options",
        ":analyzer_output",
        ":catalog",
        ":cycle_detector",
        ":language_options",
        ":options_cc_proto",
        ":parse_helpers",
        ":parse_location",
        ":parse_resume_location",
        ":type",
        ":type_cc_proto",
        ":value",
        "//googlesql/analyzer:all_rewriters",
        "//googlesql/analyzer:analyzer_impl",
        "//googlesql/analyzer:analyzer_output_mutator",
        "//googlesql/analyzer:resolver",
        "//googlesql/analyzer:rewrite_resolved_ast",
        "//googlesql/base",
        "//googlesql/base:check",
        "//googlesql/base:map_util",
        "//googlesql/base:ret_check",
        "//googlesql/base:status",
        "//googlesql/common:errors",
        "//googlesql/common:internal_analyzer_options",
        "//googlesql/common:status_payload_utils",
        "//googlesql/common:thread_stack",
        "//googlesql/common:timer_util",
        "//googlesql/parser",
        "//googlesql/public/types",
        "//googlesql/resolved_ast",
        "//googlesql/resolved_ast:column_factory",
        "//googlesql/resolved_ast:resolved_node_kind_cc_proto",
        "//googlesql/resolved_ast:validator",
        "@com_google_absl//absl/base:nullability",
        "@com_google_absl//absl/container:btree",
        "@com_google_absl//absl/flags:flag",
        "@com_google_absl//absl/memory",
        "@com_google_absl//absl/status",
        "@com_google_absl//absl/status:statusor",
        "@com_google_absl//absl/strings",
        "@com_google_absl//absl/types:span",
    ],
)

cc_library(
    name = "literal_remover",
    srcs = ["literal_remover.cc"],
    hdrs = ["literal_remover.h"],
    deps = [
        ":analyzer_options",
        ":language_options",
        ":parse_location",
        ":type",
        ":value",
        "//googlesql/base",
        "//googlesql/base:map_util",
        "//googlesql/base:ret_check",
        "//googlesql/base:status",
        "//googlesql/base:strings",
        "//googlesql/resolved_ast",
        "//googlesql/resolved_ast:resolved_node_kind_cc_proto",
        "@com_google_absl//absl/container:flat_hash_map",
        "@com_google_absl//absl/container:flat_hash_set",
        "@com_google_absl//absl/container:node_hash_set",
        "@com_google_absl//absl/status",
        "@com_google_absl//absl/strings",
    ],
)

# Abstract base classes for the full and lite evaluators.
# Use either :evaluator or :evaluator_lite instead.
cc_library(
    name = "evaluator_base",
    srcs = ["evaluator_base.cc"],
    hdrs = ["evaluator_base.h"],
    deps = [
        ":analyzer",
        ":analyzer_options",
        ":catalog",
        ":evaluator_table_iterator",
        ":language_options",
        ":options_cc_proto",
        ":simple_catalog",
        ":strings",
        ":type",
        ":value",
        "//googlesql/base",
        "//googlesql/base:clock",
        "//googlesql/base:map_util",
        "//googlesql/base:ret_check",
        "//googlesql/base:source_location",
        "//googlesql/base:status",
        "//googlesql/base:strings",
        "//googlesql/common:internal_analyzer_options",
        "//googlesql/reference_impl:algebrizer",
        "//googlesql/reference_impl:common",
        "//googlesql/reference_impl:evaluation",
        "//googlesql/reference_impl:parameters",
        "//googlesql/resolved_ast",
        "//googlesql/resolved_ast:resolved_node_kind_cc_proto",
        "//googlesql/resolved_ast:validator",
        "@com_google_absl//absl/base:core_headers",
        "@com_google_absl//absl/container:flat_hash_map",
        "@com_google_absl//absl/container:node_hash_map",
        "@com_google_absl//absl/memory",
        "@com_google_absl//absl/status",
        "@com_google_absl//absl/status:statusor",
        "@com_google_absl//absl/strings:string_view",
        "@com_google_absl//absl/synchronization",
        "@com_google_absl//absl/time",
        "@com_google_absl//absl/types:span",
        "@com_google_absl//absl/types:variant",
    ],
)

# Full evaluator implementation.
cc_library(
    name = "evaluator",
    srcs = ["evaluator.cc"],
    hdrs = ["evaluator.h"],
    deps = [
        ":evaluator_base",
        "//googlesql/common:evaluator_registration_utils",
        "@com_google_absl//absl/strings:string_view",
    ],
)

# Lite evaluator implementation; optimized for executable size at the expense
# of some features. See evaluator_lite.h for pointers on what is excluded
# and how to selectively reenable features.
cc_library(
    name = "evaluator_lite",
    hdrs = ["evaluator_lite.h"],
    deps = [
        ":evaluator_base",
    ],
)

cc_test(
    name = "evaluator_test",
    size = "small",
    timeout = "moderate",
    srcs = ["evaluator_test.cc"],
    deps = [
        ":analyzer",
        ":analyzer_output",
        ":builtin_function_options",
        ":catalog",
        ":civil_time",
        ":evaluator",
        ":evaluator_base",
        ":evaluator_table_iterator",
        ":function",
        ":function_cc_proto",
        ":id_string",
        ":language_options",
        ":options_cc_proto",
        ":simple_catalog",
        ":type",
        ":type_cc_proto",
        ":value",
        "//googlesql/base",
        "//googlesql/base:check",
        "//googlesql/base:clock",
        "//googlesql/base:map_util",
        "//googlesql/base:ret_check",
        "//googlesql/base:status",
        "//googlesql/base/testing:googlesql_gtest_main",
        "//googlesql/base/testing:status_matchers",
        "//googlesql/common:evaluator_test_table",
        "//googlesql/public/functions:date_time_util",
        "//googlesql/public/types",
        "//googlesql/reference_impl:evaluation",
        "//googlesql/resolved_ast",
        "//googlesql/resolved_ast:resolved_node_kind_cc_proto",
        "//googlesql/resolved_ast:sql_builder",
        "//googlesql/testdata:populate_sample_tables",
        "//googlesql/testdata:sample_catalog",
        "//googlesql/testdata:test_schema_cc_proto",
        "//googlesql/testing:test_value",
        "@com_google_absl//absl/flags:flag",
        "@com_google_absl//absl/memory",
        "@com_google_absl//absl/status",
        "@com_google_absl//absl/status:statusor",
        "@com_google_absl//absl/strings",
        "@com_google_absl//absl/strings:cord",
        "@com_google_absl//absl/strings:str_format",
        "@com_google_absl//absl/time",
        "@com_google_absl//absl/types:span",
        "@com_google_protobuf//:protobuf",
    ],
)

cc_test(
    name = "evaluator_lite_test",
    size = "small",
    srcs = ["evaluator_lite_test.cc"],
    deps = [
        ":evaluator_lite",
        ":value",
        "//googlesql/base/testing:googlesql_gtest_main",
        "//googlesql/base/testing:status_matchers",
    ],
)

cc_library(
    name = "evaluator_table_iterator",
    hdrs = ["evaluator_table_iterator.h"],
    deps = [
        ":value",
        "//googlesql/base:status",
    ],
)

cc_library(
    name = "id_string",
    srcs = ["id_string.cc"],
    hdrs = ["id_string.h"],
    deps = [
        "//googlesql/base",
        "//googlesql/base:arena",
        "//googlesql/base:case",
        "//googlesql/base:endian",
        "//googlesql/base:map_util",
        "//googlesql/common:unicode_utils",
        "@com_google_absl//absl/algorithm:container",
        "@com_google_absl//absl/base:core_headers",
        "@com_google_absl//absl/container:flat_hash_map",
        "@com_google_absl//absl/container:flat_hash_set",
        "@com_google_absl//absl/flags:flag",
        "@com_google_absl//absl/hash",
        "@com_google_absl//absl/strings",
        "@com_google_absl//absl/synchronization",
        "@com_google_absl//absl/types:span",
        "@com_google_googletest//:gtest",
    ],
)

cc_test(
    name = "id_string_test",
    size = "small",
    srcs = ["id_string_test.cc"],
    deps = [
        ":id_string",
        "//googlesql/base:map_util",
        "//googlesql/base:strings",
        "//googlesql/base/testing:googlesql_gtest_main",
        "//googlesql/common:unicode_utils",
        "@com_google_absl//absl/container:flat_hash_set",
        "@com_google_absl//absl/container:node_hash_set",
        "@com_google_absl//absl/flags:flag",
        "@com_google_absl//absl/hash",
        "@com_google_absl//absl/strings",
        "@com_google_absl//absl/types:span",
    ],
)

cc_test(
    name = "table_valued_function_test",
    srcs = ["table_valued_function_test.cc"],
    deps = [
        ":analyzer_options",
        ":catalog",
        ":function",
        ":function_cc_proto",
        ":language_options",
        ":options_cc_proto",
        ":parse_location",
        ":type",
        ":type_cc_proto",
        "//googlesql/base/testing:googlesql_gtest_main",
        "//googlesql/base/testing:status_matchers",
        "//googlesql/proto:function_cc_proto",
        "//googlesql/public/types",
        "@com_google_absl//absl/status",
        "@com_google_absl//absl/strings",
    ],
)

cc_library(
    name = "time_zone_util",
    srcs = ["time_zone_util.cc"],
    hdrs = ["time_zone_util.h"],
    deps = [
        "//googlesql/common:errors",
        "@com_google_absl//absl/status",
        "@com_google_absl//absl/strings",
        "@com_google_absl//absl/time",
    ],
)

cc_test(
    name = "time_zone_util_test",
    size = "small",
    srcs = ["time_zone_util_test.cc"],
    deps = [
        ":time_zone_util",
        "//googlesql/base/testing:googlesql_gtest_main",
        "//googlesql/base/testing:status_matchers",
        "//googlesql/public/functions:date_time_util",
        "@com_google_absl//absl/time",
    ],
)

proto_library(
    name = "annotation_proto",
    srcs = ["annotation.proto"],
    deps = [":simple_value_proto"],
)

cc_proto_library(
    name = "annotation_cc_proto",
    deps = [":annotation_proto"],
)

java_proto_library(
    name = "annotation_java_proto",
    deps = [":annotation_proto"],
)

proto_library(
    name = "simple_value_proto",
    srcs = ["simple_value.proto"],
)

cc_proto_library(
    name = "simple_value_cc_proto",
    deps = [":simple_value_proto"],
)

java_proto_library(
    name = "simple_value_java_proto",
    deps = [":simple_value_proto"],
)

cc_library(
    name = "lenient_formatter",
    srcs = ["lenient_formatter.cc"],
    hdrs = ["lenient_formatter.h"],
    deps = [
        ":formatter_options",
        "//googlesql/base:status",
        "//googlesql/tools/formatter/internal:layout",
        "//googlesql/tools/formatter/internal:parsed_file",
        "@com_google_absl//absl/status",
        "@com_google_absl//absl/status:statusor",
        "@com_google_absl//absl/strings",
    ],
)

cc_test(
    name = "lenient_formatter_test",
    size = "small",
    srcs = ["lenient_formatter_test.cc"],
    deps = [
        ":formatter_options",
        ":lenient_formatter",
        "//googlesql/base/testing:googlesql_gtest_main",
        "//googlesql/base/testing:status_matchers",
        "@com_google_absl//absl/status",
        "@com_google_absl//absl/strings",
        "@icu//:headers",
    ],
)

cc_library(
    name = "formatter_options",
    srcs = ["formatter_options.cc"],
    hdrs = ["formatter_options.h"],
    deps = [
        ":formatter_options_cc_proto",
        "@com_google_absl//absl/strings",
    ],
)

proto_library(
    name = "feature_label_dictionary_proto",
    srcs = ["feature_label_dictionary.proto"],
)

cc_proto_library(
    name = "feature_label_dictionary_proto_cc_proto",
    deps = [":feature_label_dictionary_proto"],
)

cc_library(
    name = "feature_label_extractor",
    srcs = ["feature_label_extractor.cc"],
    hdrs = ["feature_label_extractor.h"],
    deps = [
        ":analyzer",
        ":analyzer_options",
        ":analyzer_output_properties",
        ":catalog",
        ":feature_label_dictionary_proto_cc_proto",
        ":options_cc_proto",
        ":type",
        "//googlesql/base:ret_check",
        "//googlesql/base:status",
        "//googlesql/common:errors",
        "//googlesql/common:internal_analyzer_output_properties",
        "//googlesql/compliance:compliance_label_cc_proto",
        "//googlesql/compliance:compliance_label_extractor",
        "//googlesql/resolved_ast",
        "//googlesql/resolved_ast:target_syntax",
        "@com_google_absl//absl/base:core_headers",
        "@com_google_absl//absl/base:no_destructor",
        "@com_google_absl//absl/container:btree",
        "@com_google_absl//absl/container:flat_hash_map",
        "@com_google_absl//absl/container:flat_hash_set",
        "@com_google_absl//absl/status",
        "@com_google_absl//absl/status:statusor",
        "@com_google_absl//absl/strings",
    ],
)

exports_files(["type_annotation.proto"])

cc_test(
    name = "table_name_resolver_test",
    size = "small",
    srcs = ["table_name_resolver_test.cc"],
    deps = [
        ":analyzer",
        ":analyzer_options",
        ":options_cc_proto",
        ":parse_resume_location",
        ":simple_catalog",
        "//googlesql/base/testing:googlesql_gtest_main",
        "//googlesql/base/testing:status_matchers",
        "//googlesql/common:errors",
        "//googlesql/parser",
        "//googlesql/public/types",
        "//googlesql/resolved_ast",
        "//googlesql/resolved_ast:resolved_node_kind_cc_proto",
        "@com_google_absl//absl/container:btree",
        "@com_google_absl//absl/container:flat_hash_map",
        "@com_google_absl//absl/status",
    ],
)

cc_test(
    name = "graph_element_value_test",
    srcs = ["graph_element_value_test.cc"],
    deps = [
        ":json_value",
        ":type_cc_proto",
        ":value",
        "//googlesql/base:check",
        "//googlesql/base:status",
        "//googlesql/base/testing:googlesql_gtest_main",
        "//googlesql/base/testing:status_matchers",
        "//googlesql/common:graph_element_utils",
        "//googlesql/public/types",
        "//googlesql/testing:test_value",
        "@com_google_absl//absl/status",
        "@com_google_absl//absl/status:statusor",
        "@com_google_absl//absl/strings:string_view",
        "@com_google_absl//absl/types:span",
    ],
)

cc_test(
    name = "graph_path_value_test",
    srcs = ["graph_path_value_test.cc"],
    deps = [
        ":type_cc_proto",
        ":value",
        "//googlesql/base/testing:googlesql_gtest_main",
        "//googlesql/base/testing:status_matchers",
        "//googlesql/public/types",
        "//googlesql/testing:test_value",
        "@com_google_absl//absl/hash:hash_testing",
        "@com_google_absl//absl/status",
    ],
)

proto_library(
    name = "simple_token_list_proto",
    srcs = ["simple_token_list.proto"],
)

cc_proto_library(
    name = "simple_token_list_cc_proto",
    deps = [":simple_token_list_proto"],
)

java_proto_library(
    name = "simple_token_list_java_proto",
    deps = [":simple_token_list_proto"],
)

cc_library(
    name = "simple_token_list",
    srcs = ["simple_token_list.cc"],
    hdrs = ["simple_token_list.h"],
    deps = [
        ":simple_token_list_cc_proto",
        "@com_google_absl//absl/container:flat_hash_set",
        "@com_google_absl//absl/status",
        "@com_google_absl//absl/status:statusor",
        "@com_google_absl//absl/strings",
        "@com_google_absl//absl/strings:str_format",
        "@com_google_absl//absl/types:span",
    ],
)

cc_library(
    name = "token_list_util",
    srcs = ["token_list_util.cc"],
    hdrs = ["token_list_util.h"],
    deps = [
        ":simple_token_list",
        ":value",
        "//googlesql/base:status",
        "@com_google_absl//absl/status:statusor",
    ],
)

cc_test(
    name = "token_list_util_test",
    srcs = ["token_list_util_test.cc"],
    deps = [
        ":token_list_util",
        ":type_cc_proto",
        ":value",
        "//googlesql/base/testing:googlesql_gtest_main",
        "//googlesql/base/testing:status_matchers",
    ],
)

cc_test(
    name = "simple_token_list_test",
    srcs = ["simple_token_list_test.cc"],
    deps = [
        ":simple_token_list",
        "//googlesql/base:ret_check",
        "//googlesql/base/testing:googlesql_gtest_main",
        "//googlesql/base/testing:status_matchers",
        "@com_google_absl//absl/status",
        "@com_google_absl//absl/status:statusor",
    ],
)

cc_library(
    name = "measure_expression",
    srcs = ["measure_expression.cc"],
    hdrs = ["measure_expression.h"],
    deps = [
        ":analyzer_options",
        ":catalog",
        "//googlesql/common:measure_analysis_utils",
        "//googlesql/public/types",
        "//googlesql/resolved_ast",
        "//googlesql/resolved_ast:resolved_node_kind_cc_proto",
        "@com_google_absl//absl/status:statusor",
        "@com_google_absl//absl/strings:string_view",
    ],
)

cc_test(
    name = "measure_expression_test",
    srcs = ["measure_expression_test.cc"],
    deps = [
        ":analyzer_options",
        ":analyzer_output",
        ":builtin_function_options",
        ":catalog",
        ":function",
        ":function_cc_proto",
        ":function_headers",
        ":measure_expression",
        ":parse_resume_location",
        ":simple_catalog",
        ":templated_sql_function",
        ":templated_sql_tvf_no_resolver",
        "//googlesql/base:check",
        "//googlesql/base:status",
        "//googlesql/base/testing:googlesql_gtest_main",
        "//googlesql/base/testing:status_matchers",
        "//googlesql/public/types",
        "//googlesql/resolved_ast",
        "//googlesql/testdata:test_schema_cc_proto",
        "@com_google_absl//absl/status",
        "@com_google_absl//absl/status:statusor",
        "@com_google_absl//absl/strings:string_view",
    ],
)

proto_library(
    name = "fix_suggestion_proto",
    srcs = ["fix_suggestion.proto"],
    deps = [
        ":error_location_proto",
    ],
)

cc_proto_library(
    name = "fix_suggestion_cc_proto",
    deps = [":fix_suggestion_proto"],
)
