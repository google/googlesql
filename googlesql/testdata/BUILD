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

load("@com_google_protobuf//bazel:java_proto_library.bzl", "java_proto_library")
load("@com_google_protobuf//bazel:proto_library.bzl", "proto_library")
load("@rules_cc//cc:cc_binary.bzl", "cc_binary")
load("@rules_cc//cc:cc_library.bzl", "cc_library")
load("@rules_cc//cc:cc_test.bzl", "cc_test")

package(
    default_visibility = ["//googlesql/base:googlesql_implementation"],
)

exports_files(["external_extension.proto"])

filegroup(
    name = "test_files",
    srcs = [
        "bad_extension_schema.proto",
        "bad_file_dependency.proto",
        "bad_parse_schema.proto",
        "bad_parse_schema_simple.proto",
        "bad_reference.proto",
        "bad_test_schema.proto",
        "external_extension.proto",
        "keyword_in_package_name.proto",
        "recursive_schema.proto",
        "referenced_schema.proto",
        "referencing_schema.proto",
        "test_proto3.proto",
        "test_schema.proto",
    ],
)

proto_library(
    name = "bad_test_schema_proto",
    testonly = 1,
    srcs = [
        "bad_extension_schema.proto",
        "bad_test_schema.proto",
    ],
    deps = ["//googlesql/public/proto:type_annotation_proto"],
)

cc_proto_library(
    name = "bad_test_schema_cc_proto",
    testonly = 1,
    deps = [":bad_test_schema_proto"],
)

java_proto_library(
    name = "bad_test_schema_java_proto",
    testonly = 1,
    deps = [":bad_test_schema_proto"],
)

proto_library(
    name = "test_schema_proto",
    srcs = [
        "referenced_schema.proto",
        "referencing_schema.proto",
        "test_schema.proto",
    ],
    features = ["-proto_dynamic_mode_static_link"],
    deps = [
        "//googlesql/public/proto:type_annotation_proto",
        "@com_google_protobuf//:descriptor_proto",
    ],
)

cc_proto_library(
    name = "test_schema_cc_proto",
    deps = [":test_schema_proto"],
)

java_proto_library(
    name = "test_schema_java_proto",
    deps = [":test_schema_proto"],
)

proto_library(
    name = "test_proto3_proto",
    srcs = [
        "test_proto3.proto",
    ],
    deps = [
        ":test_schema_proto",
        "//googlesql/public/proto:type_annotation_proto",
        "@com_google_protobuf//:any_proto",
        "@com_google_protobuf//:api_proto",
        "@com_google_protobuf//:duration_proto",
        "@com_google_protobuf//:empty_proto",
        "@com_google_protobuf//:field_mask_proto",
        "@com_google_protobuf//:source_context_proto",
        "@com_google_protobuf//:struct_proto",
        "@com_google_protobuf//:timestamp_proto",
        "@com_google_protobuf//:wrappers_proto",
    ],
)

cc_proto_library(
    name = "test_proto3_cc_proto",
    deps = [":test_proto3_proto"],
)

java_proto_library(
    name = "test_proto3_java_proto",
    deps = [":test_proto3_proto"],
)

proto_library(
    name = "test_packageless_proto",
    srcs = [
        "packageless.proto",
    ],
    deps = [":test_schema_proto"],
)

cc_proto_library(
    name = "test_packageless_cc_proto",
    deps = [":test_packageless_proto"],
)

java_proto_library(
    name = "test_packageless_java_proto",
    testonly = 1,
    deps = [":test_packageless_proto"],
)

proto_library(
    name = "ambiguous_has_proto",
    srcs = [
        "ambiguous_has.proto",
    ],
)

cc_proto_library(
    name = "ambiguous_has_cc_proto",
    deps = [":ambiguous_has_proto"],
)

java_proto_library(
    name = "ambiguous_has_java_proto",
    testonly = 1,
    deps = [":ambiguous_has_proto"],
)

cc_library(
    name = "sample_annotation",
    srcs = ["sample_annotation.cc"],
    hdrs = ["sample_annotation.h"],
    deps = [
        "//googlesql/base:ret_check",
        "//googlesql/base:status",
        "//googlesql/public:type",
        "//googlesql/public/types",
        "//googlesql/resolved_ast",
        "@com_google_absl//absl/status",
    ],
)

cc_library(
    name = "sample_system_variables",
    testonly = 1,
    srcs = ["sample_system_variables.cc"],
    hdrs = ["sample_system_variables.h"],
    deps = [
        ":test_schema_cc_proto",
        "//googlesql/base:check",
        "//googlesql/public:analyzer",
        "//googlesql/public:analyzer_options",
        "//googlesql/public:type",
        "//googlesql/public/types",
        "@com_google_absl//absl/log",
        "@com_google_absl//absl/status",
        "@com_google_absl//absl/strings",
    ],
)

cc_library(
    name = "sample_catalog",
    testonly = 1,
    srcs = ["sample_catalog.cc"],
    hdrs = ["sample_catalog.h"],
    deps = [
        ":sample_catalog_impl",
        "//googlesql/base:check",
        "//googlesql/public:builtin_function_options",
        "//googlesql/public:function",
        "//googlesql/public:language_options",
        "//googlesql/public:type",
        "//googlesql/public/types",
        "@com_google_absl//absl/status",
    ],
)

cc_library(
    name = "sample_catalog_impl",
    srcs = ["sample_catalog_impl.cc"],
    hdrs = ["sample_catalog_impl.h"],
    deps = [
        ":ambiguous_has_cc_proto",
        ":sample_annotation",
        ":test_packageless_cc_proto",
        ":test_proto3_cc_proto",
        ":test_schema_cc_proto",
        "//googlesql/base",
        "//googlesql/base:check",
        "//googlesql/base:clock",
        "//googlesql/base:map_util",
        "//googlesql/base:ret_check",
        "//googlesql/base:source_location",
        "//googlesql/base:status",
        "//googlesql/common:errors",
        "//googlesql/common:internal_property_graph",
        "//googlesql/common:measure_analysis_utils",
        "//googlesql/common:simple_evaluator_table_iterator",
        "//googlesql/public:analyzer",
        "//googlesql/public:analyzer_output",
        "//googlesql/public:anon_function",
        "//googlesql/public:builtin_function_cc_proto",
        "//googlesql/public:builtin_function_options",
        "//googlesql/public:catalog",
        "//googlesql/public:cycle_detector",
        "//googlesql/public:deprecation_warning_cc_proto",
        "//googlesql/public:error_location_cc_proto",
        "//googlesql/public:evaluator_table_iterator",
        "//googlesql/public:function",
        "//googlesql/public:function_cc_proto",
        "//googlesql/public:language_options",
        "//googlesql/public:options_cc_proto",
        "//googlesql/public:parse_resume_location",
        "//googlesql/public:simple_catalog",
        "//googlesql/public:simple_catalog_util",
        "//googlesql/public:sql_function",
        "//googlesql/public:sql_tvf",
        "//googlesql/public:strings",
        "//googlesql/public:templated_sql_function",
        "//googlesql/public:templated_sql_tvf",
        "//googlesql/public:type",
        "//googlesql/public:type_cc_proto",
        "//googlesql/public:value",
        "//googlesql/public/annotation:collation",
        "//googlesql/public/functions:differential_privacy_cc_proto",
        "//googlesql/public/types",
        "//googlesql/resolved_ast",
        "//googlesql/resolved_ast:resolved_ast_enums_cc_proto",
        "//googlesql/resolved_ast:resolved_node_kind_cc_proto",
        "@com_google_absl//absl/base:core_headers",
        "@com_google_absl//absl/base:no_destructor",
        "@com_google_absl//absl/base:nullability",
        "@com_google_absl//absl/container:btree",
        "@com_google_absl//absl/container:flat_hash_map",
        "@com_google_absl//absl/container:flat_hash_set",
        "@com_google_absl//absl/container:node_hash_map",
        "@com_google_absl//absl/functional:function_ref",
        "@com_google_absl//absl/memory",
        "@com_google_absl//absl/status",
        "@com_google_absl//absl/status:statusor",
        "@com_google_absl//absl/strings",
        "@com_google_absl//absl/synchronization",
        "@com_google_absl//absl/time",
        "@com_google_absl//absl/types:span",
        "@com_google_protobuf//:protobuf",
    ],
)

cc_test(
    name = "sample_catalog_test",
    srcs = ["sample_catalog_test.cc"],
    deps = [
        ":sample_catalog",
        "//googlesql/base/testing:googlesql_gtest_main",
        "//googlesql/base/testing:status_matchers",
        "//googlesql/proto:simple_catalog_cc_proto",
        "//googlesql/public:catalog",
        "//googlesql/public:evaluator_table_iterator",
        "//googlesql/public:function",
        "//googlesql/public:function_cc_proto",
        "//googlesql/public:function_headers",
        "//googlesql/public:language_options",
        "//googlesql/public:sql_view",
        "//googlesql/public/types",
        "//googlesql/resolved_ast:resolved_ast_enums_cc_proto",
        "@com_google_absl//absl/strings",
    ],
)

cc_test(
    name = "sample_catalog_property_graph_test",
    srcs = ["sample_catalog_property_graph_test.cc"],
    deps = [
        ":sample_catalog",
        "//googlesql/base/testing:googlesql_gtest_main",
        "//googlesql/base/testing:status_matchers",
        "//googlesql/public:catalog",
        "//googlesql/public:language_options",
        "//googlesql/public:simple_catalog",
        "//googlesql/public:type_cc_proto",
        "//googlesql/public/types",
        "@com_google_absl//absl/algorithm:container",
        "@com_google_absl//absl/container:flat_hash_set",
        "@com_google_absl//absl/strings",
    ],
)

cc_library(
    name = "error_catalog",
    testonly = 1,
    srcs = ["error_catalog.cc"],
    hdrs = ["error_catalog.h"],
    deps = [
        "//googlesql/base:ret_check",
        "//googlesql/base:status",
        "//googlesql/public:catalog",
        "@com_google_absl//absl/status",
        "@com_google_absl//absl/types:span",
    ],
)

cc_library(
    name = "special_catalog",
    testonly = 1,
    srcs = ["special_catalog.cc"],
    hdrs = ["special_catalog.h"],
    deps = [
        ":test_schema_cc_proto",
        "//googlesql/base:check",
        "//googlesql/base:map_util",
        "//googlesql/public:catalog",
        "//googlesql/public:simple_catalog",
        "//googlesql/public:type",
        "@com_google_absl//absl/container:btree",
        "@com_google_absl//absl/strings",
        "@com_google_absl//absl/types:span",
    ],
)

cc_library(
    name = "populate_sample_tables",
    testonly = 1,
    srcs = ["populate_sample_tables.cc"],
    hdrs = ["populate_sample_tables.h"],
    deps = [
        ":sample_catalog",
        ":test_schema_cc_proto",
        "//googlesql/base:status",
        "//googlesql/public:catalog",
        "//googlesql/public:simple_catalog",
        "//googlesql/public:type",
        "//googlesql/public:value",
        "//googlesql/public/types",
        "//googlesql/reference_impl:evaluation",
        "//googlesql/testing:test_value",
        "@com_google_absl//absl/status",
        "@com_google_absl//absl/time",
    ],
)
