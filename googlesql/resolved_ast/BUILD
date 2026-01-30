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

load("@bazel_skylib//:bzl_library.bzl", "bzl_library")
load("@com_google_protobuf//bazel:java_proto_library.bzl", "java_proto_library")
load("@com_google_protobuf//bazel:proto_library.bzl", "proto_library")
load("@rules_cc//cc:cc_library.bzl", "cc_library")
load("@rules_cc//cc:cc_test.bzl", "cc_test")
load("@rules_python//python:defs.bzl", "py_binary")
load("@rules_python//python:proto.bzl", "py_proto_library")
load("//googlesql/resolved_ast:build_rules.bzl", "gen_resolved_ast_files")

DEFAULT_VISIBILITY = ["//googlesql:googlesql_implementation"]

package(default_visibility = ["//googlesql/base:googlesql_implementation"])

py_binary(
    name = "gen_resolved_ast",
    srcs = [
        "gen_resolved_ast.py",
    ],
    deps = [
        ":resolved_ast_enums_py_pb2",
        "//googlesql/parser:generator_utils",
        "@io_abseil_py//absl:app",
        "@io_abseil_py//absl/flags",
        "@io_abseil_py//absl/logging",
        "@pypi//jinja2",
        "@pypi//markupsafe",
    ],
)

gen_resolved_ast_files(
    name = "run_gen_resolved_ast_proto",
    srcs = [
        "resolved_ast.proto.template",
        "resolved_node_kind.proto.template",
    ],
    outs = [
        "resolved_ast.proto",
        "resolved_node_kind.proto",
    ],
)

gen_resolved_ast_files(
    name = "run_gen_resolved_ast_cc",
    srcs = [
        "resolved_ast.cc.template",
        "resolved_ast.h.template",
        "resolved_ast_builder.h.template",
        "resolved_ast_deep_copy_visitor.cc.template",
        "resolved_ast_deep_copy_visitor.h.template",
        "resolved_ast_rewrite_visitor.cc.template",
        "resolved_ast_rewrite_visitor.h.template",
        "resolved_ast_visitor.h.template",
        "resolved_node_kind.h.template",
    ],
    # Do not sort: Order must match the above list.
    outs = [
        "resolved_ast.cc",
        "resolved_ast.h",
        "resolved_ast_builder.h",
        "resolved_ast_deep_copy_visitor.cc",
        "resolved_ast_deep_copy_visitor.h",
        "resolved_ast_rewrite_visitor_{shard_num}.cc",
        "resolved_ast_rewrite_visitor.h",
        "resolved_ast_visitor.h",
        "resolved_node_kind.h",
    ],
    data = [
        "resolved_ast_field.h.template.import",
    ],
    shard_count = 4,
)

gen_resolved_ast_files(
    name = "run_gen_resolved_ast_md",
    srcs = ["resolved_ast.md.template"],
    outs = ["resolved_ast.md"],
    flags = ["--noallow_hash_prefix"],
)

proto_library(
    name = "resolved_node_kind_proto",
    srcs = ["resolved_node_kind.proto"],
    visibility = ["//visibility:public"],
)

cc_proto_library(
    name = "resolved_node_kind_cc_proto",
    visibility = ["//visibility:public"],
    deps = [":resolved_node_kind_proto"],
)

java_proto_library(
    name = "resolved_node_kind_java_proto",
    visibility = ["//visibility:public"],
    deps = [":resolved_node_kind_proto"],
)

# TODO How do I make the library with generated headers live in public/?
cc_library(
    name = "resolved_ast",
    srcs = [
        "resolved_ast.cc",
        "resolved_ast_deep_copy_visitor.cc",
        "resolved_ast_helper.cc",
        "resolved_collation.cc",
        "resolved_column.cc",
        "resolved_node.cc",
    ],
    hdrs = [
        "resolved_ast.h",
        "resolved_ast_deep_copy_visitor.h",
        "resolved_ast_helper.h",
        "resolved_ast_visitor.h",
        "resolved_collation.h",
        "resolved_column.h",
        "resolved_node.h",
        "resolved_node_kind.h",
    ],
    features = ["header_modules"],
    visibility = ["//visibility:public"],
    deps = [
        ":resolved_ast_cc_proto",
        ":resolved_ast_enums_cc_proto",
        ":resolved_node_kind_cc_proto",
        ":serialization_cc_proto",
        "//googlesql/base",
        "//googlesql/base:check",
        "//googlesql/base:map_util",
        "//googlesql/base:ret_check",
        "//googlesql/base:source_location",
        "//googlesql/base:status",
        "//googlesql/common:box_glyphs",
        "//googlesql/common:errors",
        "//googlesql/common:thread_stack",
        "//googlesql/public:annotation_cc_proto",
        "//googlesql/public:catalog",
        "//googlesql/public:constant",
        "//googlesql/public:function",
        "//googlesql/public:function_cc_proto",
        "//googlesql/public:id_string",
        "//googlesql/public:parse_location",
        "//googlesql/public:parse_location_range_cc_proto",
        "//googlesql/public:strings",
        "//googlesql/public:type",
        "//googlesql/public:type_annotation_cc_proto",
        "//googlesql/public:type_modifiers_cc_proto",
        "//googlesql/public:type_parameters_cc_proto",
        "//googlesql/public:value",
        "//googlesql/public/proto:type_annotation_cc_proto",
        "//googlesql/public/types",
        "@com_google_absl//absl/base:nullability",
        "@com_google_absl//absl/container:flat_hash_map",
        "@com_google_absl//absl/container:flat_hash_set",
        "@com_google_absl//absl/hash",
        "@com_google_absl//absl/log",
        "@com_google_absl//absl/memory",
        "@com_google_absl//absl/status",
        "@com_google_absl//absl/status:statusor",
        "@com_google_absl//absl/strings",
        "@com_google_absl//absl/types:span",
        "@com_google_protobuf//:protobuf",
    ],
)

cc_library(
    name = "resolved_ast_builder",
    srcs = [],
    hdrs = [
        "resolved_ast_builder.h",
    ],
    deps = [
        ":resolved_ast",
        "//googlesql/base:logging",
        "//googlesql/base:source_location",
        "//googlesql/common:status_payload_utils",
        "@com_google_absl//absl/status:statusor",
    ],
)

cc_library(
    name = "resolved_ast_rewrite_visitor",
    srcs = [
        "resolved_ast_rewrite_visitor_1.cc",
        "resolved_ast_rewrite_visitor_2.cc",
        "resolved_ast_rewrite_visitor_3.cc",
        "resolved_ast_rewrite_visitor_4.cc",
    ],
    hdrs = [
        "resolved_ast_rewrite_visitor.h",
    ],
    # Debug builds create particularly degenerate stack frames for Dispatch
    # 8000b stack frames in dbg vs 100b in copt
    # So we force this to always build optimized.
    copts = [
        "-O3",
        "-Wframe-larger-than=4000",
    ],
    deps = [
        ":resolved_ast",
        ":resolved_ast_builder",
        "//googlesql/base:logging",
        "//googlesql/base:ret_check",
        "//googlesql/base:status",
        "//googlesql/common:thread_stack",
        "@com_google_absl//absl/status",
        "@com_google_absl//absl/status:statusor",
    ],
)

cc_library(
    name = "make_node_vector",
    srcs = [
        "make_node_vector_internal.h",
    ],
    hdrs = [
        "make_node_vector.h",
    ],
    visibility = ["//visibility:public"],
    deps = [
        ":resolved_ast",
        "@com_google_absl//absl/meta:type_traits",
    ],
)

gen_resolved_ast_files(
    name = "run_gen_resolved_ast_comparator",
    srcs = [
        "resolved_ast_comparator.cc.template",
        "resolved_ast_comparator.h.template",
    ],
    outs = [
        "resolved_ast_comparator.cc",
        "resolved_ast_comparator.h",
    ],
)

cc_library(
    name = "comparator",
    srcs = ["resolved_ast_comparator.cc"],
    hdrs = ["resolved_ast_comparator.h"],
    visibility = ["//visibility:public"],
    deps = [
        ":resolved_ast",
        "//googlesql/base",
        "//googlesql/base:case",
        "//googlesql/base:status",
        "//googlesql/common:errors",
        "//googlesql/common:thread_stack",
        "//googlesql/public:catalog",
        "//googlesql/public/types:type",
        "@com_google_absl//absl/status:statusor",
        "@com_google_protobuf//:protobuf",
    ],
)

cc_test(
    name = "resolved_ast_comparator_test",
    srcs = ["resolved_ast_comparator_test.cc"],
    deps = [
        ":comparator",
        ":resolved_ast",
        "//googlesql/base/testing:googlesql_gtest_main",
        "//googlesql/base/testing:status_matchers",
        "//googlesql/public/annotation:collation",
        "//googlesql/public/types",
        "//googlesql/public/types:type",
    ],
)

cc_test(
    name = "query_expression_test",
    srcs = ["query_expression_test.cc"],
    deps = [
        ":sql_builder",
        "//googlesql/base/testing:googlesql_gtest_main",
    ],
)

cc_library(
    name = "validator",
    srcs = ["validator.cc"],
    hdrs = ["validator.h"],
    visibility = ["//visibility:public"],
    deps = [
        ":node_sources",
        ":resolved_ast",
        ":resolved_ast_enums_cc_proto",
        ":resolved_node_kind_cc_proto",
        ":rewrite_utils",
        "//googlesql/analyzer:expr_matching_helpers",
        "//googlesql/analyzer:filter_fields_path_validator",
        "//googlesql/base",
        "//googlesql/base:check",
        "//googlesql/base:map_util",
        "//googlesql/base:ret_check",
        "//googlesql/base:status",
        "//googlesql/base:stl_util",
        "//googlesql/base:strings",
        "//googlesql/base:varsetter",
        "//googlesql/common:graph_element_utils",
        "//googlesql/common:measure_utils",
        "//googlesql/common:thread_stack",
        "//googlesql/public:aggregation_threshold_utils",
        "//googlesql/public:analyzer_options",
        "//googlesql/public:builtin_function_cc_proto",
        "//googlesql/public:catalog",
        "//googlesql/public:constant",
        "//googlesql/public:function",
        "//googlesql/public:function_cc_proto",
        "//googlesql/public:id_string",
        "//googlesql/public:language_options",
        "//googlesql/public:options_cc_proto",
        "//googlesql/public:sql_view",
        "//googlesql/public:strings",
        "//googlesql/public:templated_sql_function",
        "//googlesql/public:templated_sql_tvf_no_resolver",
        "//googlesql/public:type",
        "//googlesql/public:type_cc_proto",
        "//googlesql/public:value",
        "//googlesql/public:with_modifier_mode",
        "//googlesql/public/annotation:collation",
        "//googlesql/public/types",
        "@com_google_absl//absl/algorithm:container",
        "@com_google_absl//absl/cleanup",
        "@com_google_absl//absl/container:flat_hash_map",
        "@com_google_absl//absl/container:flat_hash_set",
        "@com_google_absl//absl/log",
        "@com_google_absl//absl/status",
        "@com_google_absl//absl/strings",
        "@com_google_absl//absl/types:span",
        "@com_google_googletest//:gtest",
        "@com_google_protobuf//:protobuf",
    ],
)

cc_library(
    name = "sql_builder",
    srcs = [
        "query_expression.cc",
        "sql_builder.cc",
    ],
    hdrs = [
        "query_expression.h",
        "sql_builder.h",
    ],
    visibility = ["//visibility:public"],
    deps = [
        ":node_sources",
        ":resolved_ast",
        ":resolved_ast_enums_cc_proto",
        ":resolved_node_kind_cc_proto",
        ":rewrite_utils",
        ":target_syntax",
        "//googlesql/analyzer:expr_matching_helpers",
        "//googlesql/analyzer:resolver",
        "//googlesql/analyzer:set_operation_resolver_base",
        "//googlesql/base:case",
        "//googlesql/base:check",
        "//googlesql/base:map_util",
        "//googlesql/base:ret_check",
        "//googlesql/base:status",
        "//googlesql/base:stl_util",
        "//googlesql/base:varsetter",
        "//googlesql/common:function_utils",
        "//googlesql/common:graph_element_utils",
        "//googlesql/common:measure_utils",
        "//googlesql/common:thread_stack",
        "//googlesql/parser:parse_tree",
        "//googlesql/public:analyzer",
        "//googlesql/public:analyzer_options",
        "//googlesql/public:builtin_function_cc_proto",
        "//googlesql/public:catalog",
        "//googlesql/public:constant",
        "//googlesql/public:function",
        "//googlesql/public:function_cc_proto",
        "//googlesql/public:options_cc_proto",
        "//googlesql/public:strings",
        "//googlesql/public:type",
        "//googlesql/public:type_cc_proto",
        "//googlesql/public:type_parameters_cc_proto",
        "//googlesql/public:value",
        "//googlesql/public/annotation:collation",
        "//googlesql/public/functions:date_time_util",
        "//googlesql/public/functions:datetime_cc_proto",
        "//googlesql/public/functions:differential_privacy_cc_proto",
        "//googlesql/public/functions:normalize_mode_cc_proto",
        "//googlesql/public/types",
        "@com_google_absl//absl/algorithm:container",
        "@com_google_absl//absl/base:core_headers",
        "@com_google_absl//absl/base:log_severity",
        "@com_google_absl//absl/cleanup",
        "@com_google_absl//absl/container:btree",
        "@com_google_absl//absl/container:flat_hash_map",
        "@com_google_absl//absl/container:flat_hash_set",
        "@com_google_absl//absl/log",
        "@com_google_absl//absl/status",
        "@com_google_absl//absl/status:statusor",
        "@com_google_absl//absl/strings",
        "@com_google_absl//absl/strings:str_format",
        "@com_google_absl//absl/types:span",
        "@com_google_googletest//:gtest",
        "@com_googlesource_code_re2//:re2",
    ],
)

cc_test(
    name = "resolved_ast_builder_test",
    size = "small",
    srcs = ["resolved_ast_builder_test.cc"],
    deps = [
        ":resolved_ast",
        ":resolved_ast_builder",
        "//googlesql/base/testing:googlesql_gtest_main",
        "//googlesql/base/testing:status_matchers",
        "//googlesql/public:function_headers",
        "//googlesql/public:simple_catalog",
        "//googlesql/public:value",
        "//googlesql/public/types",
        "@com_google_absl//absl/memory",
        "@com_google_absl//absl/status",
        "@com_google_absl//absl/status:statusor",
    ],
)

cc_test(
    name = "resolved_ast_test",
    size = "small",
    srcs = ["resolved_ast_test.cc"],
    deps = [
        ":make_node_vector",
        ":resolved_ast",
        ":resolved_ast_cc_proto",
        ":resolved_node_kind_cc_proto",
        ":serialization_cc_proto",
        ":test_utils",
        ":validator",
        "//googlesql/base",
        "//googlesql/base:check",
        "//googlesql/base:status",
        "//googlesql/base/testing:googlesql_gtest_main",
        "//googlesql/base/testing:status_matchers",
        "//googlesql/public:analyzer",
        "//googlesql/public:analyzer_output",
        "//googlesql/public:builtin_function_options",
        "//googlesql/public:catalog",
        "//googlesql/public:function",
        "//googlesql/public:id_string",
        "//googlesql/public:options_cc_proto",
        "//googlesql/public:simple_catalog",
        "//googlesql/public:type",
        "//googlesql/public:type_cc_proto",
        "//googlesql/public:value",
        "//googlesql/public:value_cc_proto",
        "@com_google_absl//absl/base:log_severity",
        "@com_google_absl//absl/log",
        "@com_google_absl//absl/status",
        "@com_google_absl//absl/strings",
    ],
)

cc_test(
    name = "resolved_node_test",
    size = "small",
    srcs = ["resolved_node_test.cc"],
    deps = [
        ":make_node_vector",
        ":resolved_ast",
        ":resolved_ast_builder",
        "//googlesql/base/testing:googlesql_gtest_main",
        "//googlesql/base/testing:status_matchers",
        "//googlesql/public:id_string",
        "//googlesql/public:type_parameters_cc_proto",
        "//googlesql/public:value",
        "//googlesql/public/types",
        "@com_google_absl//absl/status:statusor",
    ],
)

cc_test(
    name = "make_node_vector_test",
    size = "small",
    srcs = ["make_node_vector_test.cc"],
    deps = [
        ":make_node_vector",
        ":resolved_ast",
        "//googlesql/base/testing:googlesql_gtest_main",
        "//googlesql/public:function_headers",
        "//googlesql/public:id_string",
        "//googlesql/public:value",
        "//googlesql/public/types",
    ],
)

cc_test(
    name = "resolved_ast_deep_copy_visitor_test",
    size = "small",
    srcs = ["resolved_ast_deep_copy_visitor_test.cc"],
    deps = [
        ":make_node_vector",
        ":resolved_ast",
        "//googlesql/base:status",
        "//googlesql/base/testing:googlesql_gtest_main",
        "//googlesql/base/testing:status_matchers",
        "//googlesql/public:analyzer",
        "//googlesql/public:analyzer_output",
        "//googlesql/public:builtin_function",
        "//googlesql/public:builtin_function_cc_proto",
        "//googlesql/public:builtin_function_options",
        "//googlesql/public:function",
        "//googlesql/public:id_string",
        "//googlesql/public:language_options",
        "//googlesql/public:options_cc_proto",
        "//googlesql/public:simple_catalog",
        "//googlesql/public:type",
        "//googlesql/public:value",
        "@com_google_absl//absl/status",
        "@com_google_absl//absl/status:statusor",
        "@com_google_absl//absl/strings",
    ],
)

cc_test(
    name = "resolved_ast_rewrite_visitor_test",
    size = "small",
    srcs = ["resolved_ast_rewrite_visitor_test.cc"],
    deps = [
        ":resolved_ast",
        ":resolved_ast_builder",
        ":resolved_ast_rewrite_visitor",
        "//googlesql/base:check",
        "//googlesql/base:ret_check",
        "//googlesql/base:status",
        "//googlesql/base/testing:googlesql_gtest_main",
        "//googlesql/base/testing:status_matchers",
        "//googlesql/public:analyzer",
        "//googlesql/public:analyzer_output",
        "//googlesql/public:builtin_function_cc_proto",
        "//googlesql/public:builtin_function_options",
        "//googlesql/public:function",
        "//googlesql/public:id_string",
        "//googlesql/public:language_options",
        "//googlesql/public:options_cc_proto",
        "//googlesql/public:simple_catalog",
        "//googlesql/public:type",
        "//googlesql/public:value",
        "@com_google_absl//absl/status",
        "@com_google_absl//absl/status:statusor",
        "@com_google_absl//absl/strings",
    ],
)

cc_test(
    name = "resolved_ast_helper_test",
    size = "small",
    srcs = ["resolved_ast_helper_test.cc"],
    deps = [
        ":make_node_vector",
        ":resolved_ast",
        "//googlesql/base:status",
        "//googlesql/base/testing:googlesql_gtest_main",
        "//googlesql/public:id_string",
        "//googlesql/public:type",
        "//googlesql/public:value",
        "//googlesql/testdata:test_schema_cc_proto",
    ],
)

cc_test(
    name = "resolved_column_test",
    size = "small",
    srcs = ["resolved_column_test.cc"],
    deps = [
        ":resolved_ast",
        ":serialization_cc_proto",
        "//googlesql/base:check",
        "//googlesql/base:status",
        "//googlesql/base/testing:googlesql_gtest_main",
        "//googlesql/public:id_string",
        "//googlesql/public:type",
        "//googlesql/public:type_cc_proto",
        "//googlesql/public/annotation:collation",
        "//googlesql/public/types",
        "@com_google_absl//absl/status:statusor",
    ],
)

cc_test(
    name = "resolved_collation_test",
    size = "small",
    srcs = ["resolved_collation_test.cc"],
    deps = [
        ":resolved_ast",
        ":serialization_cc_proto",
        "//googlesql/base:check",
        "//googlesql/base/testing:googlesql_gtest_main",
        "//googlesql/base/testing:status_matchers",
        "//googlesql/public/types",
        "@com_google_absl//absl/types:span",
    ],
)

proto_library(
    name = "resolved_ast_enums_proto",
    srcs = ["resolved_ast_enums.proto"],
    visibility = ["//visibility:public"],
)

cc_proto_library(
    name = "resolved_ast_enums_cc_proto",
    visibility = ["//visibility:public"],
    deps = [":resolved_ast_enums_proto"],
)

java_proto_library(
    name = "resolved_ast_enums_java_proto",
    visibility = ["//visibility:public"],
    deps = [":resolved_ast_enums_proto"],
)

py_proto_library(
    name = "resolved_ast_enums_py_pb2",
    deps = [":resolved_ast_enums_proto"],
)

proto_library(
    name = "resolved_ast_proto",
    srcs = ["resolved_ast.proto"],
    deps = [
        ":resolved_ast_enums_proto",
        ":resolved_node_kind_proto",
        ":serialization_proto",
        "//googlesql/proto:function_proto",
        "//googlesql/public:annotation_proto",
        "//googlesql/public:type_modifiers_proto",
        "//googlesql/public:type_parameters_proto",
        "//googlesql/public:type_proto",
        "//googlesql/public/proto:type_annotation_proto",
        "@com_google_protobuf//:descriptor_proto",
    ],
)

cc_proto_library(
    name = "resolved_ast_cc_proto",
    deps = [":resolved_ast_proto"],
)

java_proto_library(
    name = "resolved_ast_java_proto",
    deps = [":resolved_ast_proto"],
)

proto_library(
    name = "serialization_proto",
    srcs = ["serialization.proto"],
    deps = [
        "//googlesql/public:annotation_proto",
        "//googlesql/public:parse_location_range_proto",
        "//googlesql/public:type_proto",
        "//googlesql/public:value_proto",
    ],
)

cc_proto_library(
    name = "serialization_cc_proto",
    deps = [":serialization_proto"],
)

java_proto_library(
    name = "serialization_java_proto",
    deps = [":serialization_proto"],
)

cc_library(
    name = "rewrite_utils",
    srcs = ["rewrite_utils.cc"],
    hdrs = ["rewrite_utils.h"],
    visibility = ["//visibility:public"],
    deps = [
        ":column_factory",
        ":resolved_ast",
        ":resolved_ast_builder",
        ":resolved_ast_enums_cc_proto",
        ":resolved_ast_rewrite_visitor",
        "//googlesql/analyzer:annotation_propagator",
        "//googlesql/base:check",
        "//googlesql/base:logging",
        "//googlesql/base:ret_check",
        "//googlesql/base:status",
        "//googlesql/public:analyzer_options",
        "//googlesql/public:builtin_function_cc_proto",
        "//googlesql/public:catalog",
        "//googlesql/public:coercer",
        "//googlesql/public:function",
        "//googlesql/public:function_cc_proto",
        "//googlesql/public:numeric_value",
        "//googlesql/public:options_cc_proto",
        "//googlesql/public:type_cc_proto",
        "//googlesql/public/annotation:collation",
        "//googlesql/public/functions:differential_privacy_cc_proto",
        "//googlesql/public/types",
        "@com_google_absl//absl/algorithm:container",
        "@com_google_absl//absl/base:nullability",
        "@com_google_absl//absl/container:flat_hash_map",
        "@com_google_absl//absl/container:flat_hash_set",
        "@com_google_absl//absl/memory",
        "@com_google_absl//absl/status",
        "@com_google_absl//absl/status:statusor",
        "@com_google_absl//absl/strings",
        "@com_google_absl//absl/types:span",
    ],
)

cc_test(
    name = "rewrite_utils_test",
    size = "small",
    srcs = ["rewrite_utils_test.cc"],
    deps = [
        ":column_factory",
        ":resolved_ast",
        ":resolved_ast_builder",
        ":rewrite_utils",
        ":serialization_cc_proto",
        ":test_utils",
        "//googlesql/base:ret_check",
        "//googlesql/base/testing:googlesql_gtest_main",
        "//googlesql/base/testing:status_matchers",
        "//googlesql/public:analyzer",
        "//googlesql/public:analyzer_options",
        "//googlesql/public:analyzer_output",
        "//googlesql/public:builtin_function_options",
        "//googlesql/public:function_headers",
        "//googlesql/public:id_string",
        "//googlesql/public:numeric_value",
        "//googlesql/public:options_cc_proto",
        "//googlesql/public:simple_catalog",
        "//googlesql/public:value",
        "//googlesql/public/proto:type_annotation_cc_proto",
        "//googlesql/public/types",
        "@com_google_absl//absl/status",
        "@com_google_absl//absl/status:statusor",
        "@com_google_absl//absl/strings",
        "@com_google_absl//absl/strings:str_format",
        "@com_google_absl//absl/types:span",
    ],
)

cc_test(
    name = "validator_test",
    srcs = ["validator_test.cc"],
    deps = [
        ":make_node_vector",
        ":resolved_ast",
        ":resolved_ast_builder",
        ":resolved_ast_enums_cc_proto",
        ":test_utils",
        ":validator",
        "//googlesql/base:no_destructor",
        "//googlesql/base:status",
        "//googlesql/base/testing:googlesql_gtest_main",
        "//googlesql/base/testing:status_matchers",
        "//googlesql/public:analyzer_options",
        "//googlesql/public:catalog",
        "//googlesql/public:function",
        "//googlesql/public:function_cc_proto",
        "//googlesql/public:function_headers",
        "//googlesql/public:id_string",
        "//googlesql/public:language_options",
        "//googlesql/public:options_cc_proto",
        "//googlesql/public:simple_catalog",
        "//googlesql/public:value",
        "//googlesql/public/types",
        "@com_google_absl//absl/container:flat_hash_set",
        "@com_google_absl//absl/status",
        "@com_google_absl//absl/status:statusor",
        "@com_google_absl//absl/strings",
    ],
)

cc_library(
    name = "test_utils",
    srcs = ["test_utils.cc"],
    hdrs = ["test_utils.h"],
    deps = [
        ":make_node_vector",
        ":resolved_ast",
        "//googlesql/base:check",
        "//googlesql/base:ret_check",
        "//googlesql/base:status",
        "//googlesql/public:analyzer_options",
        "//googlesql/public:builtin_function_cc_proto",
        "//googlesql/public:catalog",
        "//googlesql/public:function_headers",
        "//googlesql/public:id_string",
        "//googlesql/public:type_cc_proto",
        "//googlesql/public/annotation:collation",
        "//googlesql/public/types",
        "@com_google_absl//absl/status:statusor",
        "@com_google_absl//absl/strings",
        "@com_google_absl//absl/strings:string_view",
    ],
)

cc_library(
    name = "node_sources",
    hdrs = ["node_sources.h"],
)

cc_library(
    name = "target_syntax",
    hdrs = ["target_syntax.h"],
    deps = [
        ":resolved_ast",
        "@com_google_absl//absl/container:flat_hash_map",
    ],
)

cc_library(
    name = "column_factory",
    srcs = ["column_factory.cc"],
    hdrs = ["column_factory.h"],
    deps = [
        ":resolved_ast",
        "//googlesql/base:check",
        "//googlesql/public:builtin_function_cc_proto",
        "//googlesql/public:id_string",
        "//googlesql/public/types",
        "@com_google_absl//absl/base:core_headers",
        "@com_google_absl//absl/strings",
    ],
)

cc_test(
    name = "column_factory_test",
    srcs = ["column_factory_test.cc"],
    deps = [
        ":column_factory",
        ":resolved_ast",
        "//googlesql/base/testing:googlesql_gtest_main",
        "//googlesql/base/testing:status_matchers",
        "//googlesql/public/types",
    ],
)
