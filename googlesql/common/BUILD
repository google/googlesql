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

load("@rules_cc//cc:cc_test.bzl", "cc_test")

package(
    default_visibility = ["//googlesql/base:googlesql_implementation"],
    features = ["parse_headers"],
)

cc_library(
    name = "status_payload_utils",
    srcs = ["status_payload_utils.cc"],
    hdrs = ["status_payload_utils.h"],
    deps = [
        "//googlesql/base:status",
        "@com_google_absl//absl/memory",
        "@com_google_absl//absl/strings",
        "@com_google_absl//absl/strings:cord",
        "@com_google_googleapis//google/rpc:status_cc_proto",
    ],
)

cc_test(
    name = "status_payload_utils_test",
    srcs = ["status_payload_utils_test.cc"],
    deps = [
        ":status_payload_utils",
        "//googlesql/base:status",
        "//googlesql/base/testing:googlesql_gtest_main",
        "//googlesql/base/testing:status_matchers",
        "//googlesql/common/testing:proto_matchers",
        "//googlesql/testdata:test_schema_cc_proto",
    ],
)

cc_library(
    name = "constant_utils",
    srcs = ["constant_utils.cc"],
    hdrs = ["constant_utils.h"],
    deps = [
        "//googlesql/base:ret_check",
        "//googlesql/base:status",
        "//googlesql/public/types:type",
        "//googlesql/resolved_ast",
        "@com_google_absl//absl/status:statusor",
    ],
)

cc_test(
    name = "constant_utils_test",
    srcs = ["constant_utils_test.cc"],
    deps = [
        ":constant_utils",
        "//googlesql/base:check",
        "//googlesql/base/testing:googlesql_gtest_main",
        "//googlesql/base/testing:status_matchers",
        "//googlesql/public:analyzer",
        "//googlesql/public:analyzer_options",
        "//googlesql/public:analyzer_output",
        "//googlesql/public:function",
        "//googlesql/public:language_options",
        "//googlesql/public/types",
        "//googlesql/resolved_ast",
        "//googlesql/testdata:sample_catalog",
        "//googlesql/testdata:test_schema_cc_proto",
        "@com_google_absl//absl/status",
        "@com_google_absl//absl/strings:string_view",
    ],
)

cc_library(
    name = "errors",
    srcs = ["errors.cc"],
    hdrs = ["errors.h"],
    features = ["header_modules"],
    deps = [
        ":status_payload_utils",
        "//googlesql/base:check",
        "//googlesql/base:ret_check",
        "//googlesql/base:status",
        "//googlesql/proto:internal_error_location_cc_proto",
        "//googlesql/public:deprecation_warning_cc_proto",
        "//googlesql/public:error_helpers",
        "//googlesql/public:error_location_cc_proto",
        "//googlesql/public:options_cc_proto",
        "//googlesql/public:parse_location",
        "@com_google_absl//absl/base:core_headers",
        "@com_google_absl//absl/flags:flag",
        "@com_google_absl//absl/log",
        "@com_google_absl//absl/status",
        "@com_google_absl//absl/status:statusor",
        "@com_google_absl//absl/strings",
        "@com_google_absl//absl/types:span",
        "@com_google_protobuf//:protobuf_lite",
    ],
)

cc_test(
    name = "errors_test",
    size = "small",
    srcs = ["errors_test.cc"],
    deps = [
        ":errors",
        ":status_payload_utils",
        "//googlesql/base:status",
        "//googlesql/base/testing:googlesql_gtest_main",
        "//googlesql/base/testing:status_matchers",
        "//googlesql/common/testing:proto_matchers",
        "//googlesql/proto:internal_error_location_cc_proto",
        "//googlesql/public:deprecation_warning_cc_proto",
        "//googlesql/public:error_helpers",
        "//googlesql/public:parse_location",
        "//googlesql/testdata:test_schema_cc_proto",
        "@com_google_absl//absl/status",
        "@com_google_absl//absl/strings",
    ],
)

cc_library(
    name = "timer_util",
    hdrs = ["timer_util.h"],
    deps = [
        ":thread_stack",
        "//googlesql/base:check",
        "//googlesql/public/proto:logging_cc_proto",
        "@com_google_absl//absl/base:core_headers",
        "@com_google_absl//absl/time",
        "@com_google_protobuf//:protobuf",
    ],
)

cc_test(
    name = "timer_util_test",
    srcs = ["timer_util_test.cc"],
    deps = [
        ":timer_util",
        "//googlesql/base/testing:googlesql_gtest_main",
        "@com_google_absl//absl/random",
        "@com_google_absl//absl/random:distributions",
        "@com_google_absl//absl/time",
        "@com_google_protobuf//:protobuf",
    ],
)

cc_library(
    name = "measure_utils",
    srcs = ["measure_utils.cc"],
    hdrs = ["measure_utils.h"],
    deps = [
        "//googlesql/analyzer:name_scope",
        "//googlesql/base",
        "//googlesql/base:arena",
        "//googlesql/base:ret_check",
        "//googlesql/base:status",
        "//googlesql/parser:parse_tree",
        "//googlesql/public:id_string",
        "//googlesql/public:options_cc_proto",
        "//googlesql/public:strings",
        "//googlesql/public/types:type",
        "//googlesql/resolved_ast",
        "//googlesql/resolved_ast:column_factory",
        "//googlesql/resolved_ast:resolved_ast_rewrite_visitor",
        "//googlesql/resolved_ast:resolved_node_kind_cc_proto",
        "//googlesql/resolved_ast:rewrite_utils",
        "@com_google_absl//absl/container:flat_hash_set",
        "@com_google_absl//absl/status",
        "@com_google_absl//absl/status:statusor",
        "@com_google_absl//absl/strings",
    ],
)

# `measure_analysis_utils` is separated from `measure_utils` to avoid a circular dependency between
# `measure_utils` and the analyzer.
cc_library(
    name = "measure_analysis_utils",
    srcs = ["measure_analysis_utils.cc"],
    hdrs = ["measure_analysis_utils.h"],
    deps = [
        ":internal_analyzer_options",
        ":internal_value",
        ":measure_utils",
        "//googlesql/base",
        "//googlesql/base:arena",
        "//googlesql/base:ret_check",
        "//googlesql/base:status",
        "//googlesql/base:strings",
        "//googlesql/public:analyzer",
        "//googlesql/public:analyzer_options",
        "//googlesql/public:catalog",
        "//googlesql/public:id_string",
        "//googlesql/public:language_options",
        "//googlesql/public:options_cc_proto",
        "//googlesql/public:simple_catalog",
        "//googlesql/public:value",
        "//googlesql/public/types",
        "//googlesql/public/types:type",
        "//googlesql/resolved_ast",
        "//googlesql/resolved_ast:column_factory",
        "//googlesql/resolved_ast:resolved_node_kind_cc_proto",
        "@com_google_absl//absl/container:flat_hash_set",
        "@com_google_absl//absl/status",
        "@com_google_absl//absl/status:statusor",
        "@com_google_absl//absl/strings",
        "@com_google_absl//absl/strings:string_view",
        "@com_google_absl//absl/types:span",
        "@com_google_protobuf//:protobuf",
    ],
)

cc_library(
    name = "string_util",
    hdrs = ["string_util.h"],
    deps = [
        ":canonicalize_signed_zero_to_string",
        "//googlesql/base:string_numbers",
        "@com_google_absl//absl/strings",
    ],
)

cc_library(
    name = "utf_util",
    srcs = ["utf_util.cc"],
    hdrs = ["utf_util.h"],
    deps = [
        "//googlesql/base",
        "//googlesql/base:ret_check",
        "@com_google_absl//absl/status:statusor",
        "@com_google_absl//absl/strings",
        "@com_google_absl//absl/types:optional",
        "@icu//:headers",
    ],
)

cc_library(
    name = "options_utils",
    srcs = ["options_utils.cc"],
    hdrs = ["options_utils.h"],
    deps = [
        "//googlesql/base:ret_check",
        "//googlesql/base:status",
        "//googlesql/public:analyzer_options",
        "//googlesql/public:evaluator",
        "//googlesql/public:language_options",
        "//googlesql/public:options_cc_proto",
        "//googlesql/public:parse_helpers",
        "//googlesql/public:strings",
        "@com_google_absl//absl/container:btree",
        "@com_google_absl//absl/container:flat_hash_map",
        "@com_google_absl//absl/container:flat_hash_set",
        "@com_google_absl//absl/status",
        "@com_google_absl//absl/status:statusor",
        "@com_google_absl//absl/strings",
        "@com_google_absl//absl/types:span",
        "@com_google_protobuf//:protobuf",
    ],
)

cc_test(
    name = "options_utils_test",
    size = "small",
    srcs = ["options_utils_test.cc"],
    deps = [
        ":options_utils",
        "//googlesql/base/testing:googlesql_gtest_main",
        "//googlesql/base/testing:status_matchers",
        "//googlesql/common/testing:proto_matchers",
        "//googlesql/common/testing:testing_proto_util",
        "//googlesql/public:analyzer_options",
        "//googlesql/public:builtin_function_options",
        "//googlesql/public:options_cc_proto",
        "//googlesql/public:simple_catalog",
        "//googlesql/testdata:test_schema_cc_proto",
        "//googlesql/testing:test_value",
        "@com_google_absl//absl/container:btree",
        "@com_google_absl//absl/flags:commandlineflag",
        "@com_google_absl//absl/flags:flag",
        "@com_google_absl//absl/flags:reflection",
        "@com_google_absl//absl/status:statusor",
    ],
)

cc_test(
    name = "utf_util_test",
    size = "small",
    srcs = ["utf_util_test.cc"],
    deps = [
        ":utf_util",
        "//googlesql/base/testing:googlesql_gtest_main",
        "//googlesql/base/testing:status_matchers",
        "//googlesql/compliance:functions_testlib",
        "@com_google_absl//absl/strings",
    ],
)

cc_library(
    name = "thread_stack",
    srcs = ["thread_stack.cc"],
    hdrs = ["thread_stack.h"],
    deps = [
        "//googlesql/base:logging",
        "//googlesql/base:status",
        "@com_google_absl//absl/algorithm:container",
        "@com_google_absl//absl/base",
        "@com_google_absl//absl/base:core_headers",
        "@com_google_absl//absl/base:log_severity",
        "@com_google_absl//absl/container:flat_hash_map",
        "@com_google_absl//absl/debugging:stacktrace",
        "@com_google_absl//absl/flags:flag",
        "@com_google_absl//absl/log",
        "@com_google_absl//absl/status",
        "@com_google_absl//absl/strings",
        "@com_google_absl//absl/strings:str_format",
        "@com_google_absl//absl/synchronization",
        "@com_google_absl//absl/time",
    ] + select({
        "//conditions:default": [
        ],
    }),
)

cc_library(
    name = "box_glyphs",
    hdrs = ["box_glyphs.h"],
    deps = [
        "@com_google_absl//absl/strings:string_view",
    ],
)

cc_library(
    name = "proto_helper",
    srcs = ["proto_helper.cc"],
    hdrs = ["proto_helper.h"],
    deps = [
        ":errors",
        ":thread_stack",
        "//googlesql/base",
        "//googlesql/base:map_util",
        "//googlesql/base:ret_check",
        "//googlesql/base:status",
        "@com_google_absl//absl/strings",
        "@com_google_absl//absl/types:optional",
        "@com_google_protobuf//:protobuf",
    ],
)

cc_test(
    name = "proto_helper_test",
    size = "small",
    srcs = ["proto_helper_test.cc"],
    data = [
        "//googlesql/testdata:test_files",
        "//googlesql/testdata:test_schema_proto",
    ],
    deps = [
        ":proto_helper",
        "//googlesql/base:path",
        "//googlesql/base/testing:googlesql_gtest_main",
        "//googlesql/base/testing:status_matchers",
        "//googlesql/common/testing:testing_proto_util",
        "@com_google_absl//absl/memory",
        "@com_google_absl//absl/strings",
        "@com_google_protobuf//:protobuf",
    ],
)

cc_library(
    name = "parsed_templated_sql_function",
    hdrs = ["parsed_templated_sql_function.h"],
    deps = [
        "//googlesql/parser",
        "//googlesql/public:function_headers",
        "//googlesql/public:templated_sql_function",
    ],
)

cc_library(
    name = "lazy_resolution_catalog",
    srcs = ["lazy_resolution_catalog.cc"],
    hdrs = ["lazy_resolution_catalog.h"],
    deps = [
        ":errors",
        ":parsed_templated_sql_function",
        ":status_payload_utils",
        ":thread_stack",
        "//googlesql/base",
        "//googlesql/base:map_util",
        "//googlesql/base:ret_check",
        "//googlesql/base:status",
        "//googlesql/base:stl_util",
        "//googlesql/base:strings",
        "//googlesql/parser",
        "//googlesql/proto:internal_error_location_cc_proto",
        "//googlesql/proto:internal_fix_suggestion_cc_proto",
        "//googlesql/public:analyzer",
        "//googlesql/public:analyzer_options",
        "//googlesql/public:analyzer_output",
        "//googlesql/public:catalog",
        "//googlesql/public:constant",
        "//googlesql/public:cycle_detector",
        "//googlesql/public:error_helpers",
        "//googlesql/public:fix_suggestion_cc_proto",
        "//googlesql/public:function",
        "//googlesql/public:function_cc_proto",
        "//googlesql/public:module_details",
        "//googlesql/public:multi_catalog",
        "//googlesql/public:non_sql_function",
        "//googlesql/public:options_cc_proto",
        "//googlesql/public:parse_location",
        "//googlesql/public:parse_resume_location",
        "//googlesql/public:remote_tvf_factory",
        "//googlesql/public:simple_catalog",
        "//googlesql/public:sql_constant",
        "//googlesql/public:sql_function",
        "//googlesql/public:sql_procedure",
        "//googlesql/public:sql_tvf",
        "//googlesql/public:sql_view",
        "//googlesql/public:templated_sql_function",
        "//googlesql/public:templated_sql_tvf",
        "//googlesql/public:type",
        "//googlesql/public:value",
        "//googlesql/public/types",
        "//googlesql/resolved_ast",
        "@com_google_absl//absl/base:core_headers",
        "@com_google_absl//absl/container:flat_hash_map",
        "@com_google_absl//absl/functional:bind_front",
        "@com_google_absl//absl/memory",
        "@com_google_absl//absl/status",
        "@com_google_absl//absl/status:statusor",
        "@com_google_absl//absl/strings",
        "@com_google_absl//absl/synchronization",
        "@com_google_absl//absl/types:optional",
        "@com_google_absl//absl/types:span",
        "@com_google_googletest//:gtest",
    ],
)

cc_test(
    name = "lazy_resolution_catalog_test",
    size = "small",
    srcs = ["lazy_resolution_catalog_test.cc"],
    deps = [
        ":lazy_resolution_catalog",
        ":parsed_templated_sql_function",
        ":status_payload_utils",
        "//googlesql/base",
        "//googlesql/base:check",
        "//googlesql/base:ret_check",
        "//googlesql/base:status",
        "//googlesql/base/testing:googlesql_gtest_main",
        "//googlesql/base/testing:status_matchers",
        "//googlesql/parser",
        "//googlesql/public:analyzer_options",
        "//googlesql/public:builtin_function",
        "//googlesql/public:builtin_function_options",
        "//googlesql/public:cycle_detector",
        "//googlesql/public:error_helpers",
        "//googlesql/public:function",
        "//googlesql/public:function_cc_proto",
        "//googlesql/public:function_headers",
        "//googlesql/public:language_options",
        "//googlesql/public:module_details",
        "//googlesql/public:options_cc_proto",
        "//googlesql/public:simple_catalog",
        "//googlesql/public:sql_constant",
        "//googlesql/public:sql_function",
        "//googlesql/public:sql_procedure",
        "//googlesql/public:sql_view",
        "//googlesql/public:type",
        "//googlesql/resolved_ast",
        "//googlesql/testdata:sample_catalog",
        "@com_google_absl//absl/container:flat_hash_map",
        "@com_google_absl//absl/status",
        "@com_google_absl//absl/status:statusor",
        "@com_google_absl//absl/strings",
    ],
)

cc_library(
    name = "float_margin",
    hdrs = [
        "float_margin.h",
    ],
    deps = [
        ":string_util",
        "//googlesql/base",
        "//googlesql/base:mathutil",
        "@com_google_absl//absl/strings",
    ],
)

cc_library(
    name = "multiprecision_int",
    srcs = [
        "multiprecision_int.cc",
        "multiprecision_int_impl.cc",
    ],
    hdrs = [
        "multiprecision_int.h",
        "multiprecision_int_impl.h",
    ],
    features = ["header_modules"],
    deps = [
        "//googlesql/base",
        "//googlesql/base:bits",
        "//googlesql/base:endian",
        "@com_google_absl//absl/base:core_headers",
        "@com_google_absl//absl/numeric:bits",
        "@com_google_absl//absl/strings",
        "@com_google_absl//absl/types:compare",
        "@com_google_absl//absl/types:span",
    ],
)

cc_test(
    name = "multiprecision_int_test",
    srcs = ["multiprecision_int_test.cc"],
    deps = [
        ":multiprecision_int",
        "//googlesql/base:mathutil",
        "//googlesql/base/testing:googlesql_gtest_main",
        "@com_google_absl//absl/base:core_headers",
        "@com_google_absl//absl/hash",
        "@com_google_absl//absl/hash:hash_testing",
        "@com_google_absl//absl/numeric:int128",
        "@com_google_absl//absl/random",
        "@com_google_absl//absl/strings",
        "@com_google_absl//absl/types:optional",
    ],
)

cc_library(
    name = "internal_value",
    srcs = [
        "internal_value.cc",
    ],
    hdrs = [
        "internal_value.h",
    ],
    deps = [
        ":float_margin",
        "//googlesql/base:ret_check",
        "//googlesql/base:status",
        "//googlesql/public:language_options",
        "//googlesql/public:type",
        "//googlesql/public:value",
        "//googlesql/public/types",
        "//googlesql/public/types:value_representations",
        "@com_google_absl//absl/status",
        "@com_google_absl//absl/status:statusor",
        "@com_google_absl//absl/types:span",
    ],
)

cc_test(
    name = "internal_value_test",
    srcs = [
        "internal_value_test.cc",
    ],
    deps = [
        ":internal_value",
        "//googlesql/base/testing:googlesql_gtest_main",
        "//googlesql/base/testing:status_matchers",
        "//googlesql/public:language_options",
        "//googlesql/public:value",
        "//googlesql/public/types",
        "//googlesql/public/types:type",
        "//googlesql/testing:test_value",
        "@com_google_absl//absl/status",
    ],
)

cc_library(
    name = "initialize_required_fields",
    srcs = ["initialize_required_fields.cc"],
    hdrs = ["initialize_required_fields.h"],
    deps = ["@com_google_protobuf//:protobuf"],
)

proto_library(
    name = "initialize_required_fields_test_proto",
    testonly = 1,
    srcs = ["initialize_required_fields_test.proto"],
)

cc_proto_library(
    name = "initialize_required_fields_test_cc_proto",
    testonly = 1,
    deps = [":initialize_required_fields_test_proto"],
)

cc_test(
    name = "initialize_required_fields_test",
    size = "small",
    srcs = ["initialize_required_fields_test.cc"],
    deps = [
        ":initialize_required_fields",
        ":initialize_required_fields_test_cc_proto",
        "//googlesql/base/testing:googlesql_gtest_main",
        "//googlesql/testdata:test_schema_cc_proto",
        "@com_google_protobuf//:protobuf",
    ],
)

cc_test(
    name = "builtin_tvfs_test",
    srcs = ["builtin_tvfs_test.cc"],
    deps = [
        ":builtin_function_internal",
        ":builtins_output_properties",
        "//googlesql/base/testing:googlesql_gtest_main",
        "//googlesql/base/testing:status_matchers",
        "//googlesql/public:builtin_function_options",
        "//googlesql/public:language_options",
        "//googlesql/public:options_cc_proto",
        "//googlesql/public/types",
        "//googlesql/public/types:type",
        "@com_google_absl//absl/strings:string_view",
    ],
)

cc_library(
    name = "builtins_output_properties",
    hdrs = ["builtins_output_properties.h"],
    deps = [
        "//googlesql/public:builtin_function_cc_proto",
        "//googlesql/public:options_cc_proto",
        "@com_google_absl//absl/container:flat_hash_map",
        "@com_google_absl//absl/container:flat_hash_set",
    ],
)

cc_library(
    name = "builtin_function_internal",
    srcs = [
        "builtin_enum_type.cc",
        "builtin_function_array.cc",
        "builtin_function_comparison.cc",
        "builtin_function_differential_privacy.cc",
        "builtin_function_distance.cc",
        "builtin_function_graph.cc",
        "builtin_function_internal_1.cc",
        "builtin_function_internal_2.cc",
        "builtin_function_internal_3.cc",
        "builtin_function_map.cc",
        "builtin_function_match_recognize.cc",
        "builtin_function_measure.cc",
        "builtin_function_range.cc",
        "builtin_function_sketches.cc",
        "builtin_function_time_series.cc",
        "builtin_tvfs.cc",
    ],
    hdrs = ["builtin_function_internal.h"],
    deps = [
        ":builtins_output_properties",
        ":errors",
        "//googlesql/base",
        "//googlesql/base:check",
        "//googlesql/base:map_util",
        "//googlesql/base:no_destructor",
        "//googlesql/base:ret_check",
        "//googlesql/base:status",
        "//googlesql/proto:anon_output_with_report_cc_proto",
        "//googlesql/proto:options_cc_proto",
        "//googlesql/public:analyzer_options",
        "//googlesql/public:anon_function",
        "//googlesql/public:builtin_function_cc_proto",
        "//googlesql/public:builtin_function_options",
        "//googlesql/public:catalog",
        "//googlesql/public:coercer",
        "//googlesql/public:cycle_detector",
        "//googlesql/public:function",
        "//googlesql/public:function_cc_proto",
        "//googlesql/public:language_options",
        "//googlesql/public:options_cc_proto",
        "//googlesql/public:strings",
        "//googlesql/public:type",
        "//googlesql/public:type_cc_proto",
        "//googlesql/public:value",
        "//googlesql/public/annotation:collation",
        "//googlesql/public/functions:array_zip_mode_cc_proto",
        "//googlesql/public/functions:bitwise_agg_mode_cc_proto",
        "//googlesql/public/functions:date_time_util",
        "//googlesql/public/functions:datetime_cc_proto",
        "//googlesql/public/functions:differential_privacy_cc_proto",
        "//googlesql/public/functions:rank_type_cc_proto",
        "//googlesql/public/functions:regexp",
        "//googlesql/public/functions:string_format",
        "//googlesql/public/functions:unsupported_fields_cc_proto",
        "//googlesql/public/types",
        "@com_google_absl//absl/base:nullability",
        "@com_google_absl//absl/container:flat_hash_map",
        "@com_google_absl//absl/container:flat_hash_set",
        "@com_google_absl//absl/functional:bind_front",
        "@com_google_absl//absl/log",
        "@com_google_absl//absl/status",
        "@com_google_absl//absl/status:statusor",
        "@com_google_absl//absl/strings",
        "@com_google_absl//absl/strings:str_format",
        "@com_google_absl//absl/time",
        "@com_google_absl//absl/types:span",
        "@com_google_googleapis//google/type:date_cc_proto",
        "@com_google_googleapis//google/type:timeofday_cc_proto",
        "@com_google_protobuf//:protobuf",
    ],
)

cc_test(
    name = "builtin_function_internal_test",
    srcs = ["builtin_function_internal_test.cc"],
    deps = [
        ":builtin_function_internal",
        "//googlesql/base/testing:googlesql_gtest_main",
        "//googlesql/base/testing:status_matchers",
        "//googlesql/public:builtin_function_cc_proto",
        "//googlesql/public:builtin_function_options",
        "//googlesql/public:function",
        "//googlesql/public:function_headers",
        "//googlesql/public:options_cc_proto",
        "//googlesql/public:type",
        "//googlesql/public/types",
        "@com_google_absl//absl/container:flat_hash_map",
        "@com_google_absl//absl/status",
    ],
)

cc_library(
    name = "simple_evaluator_table_iterator",
    srcs = ["simple_evaluator_table_iterator.cc"],
    hdrs = ["simple_evaluator_table_iterator.h"],
    deps = [
        "//googlesql/base",
        "//googlesql/base:clock",
        "//googlesql/base:source_location",
        "//googlesql/base:status",
        "//googlesql/public:catalog",
        "//googlesql/public:evaluator_table_iterator",
        "//googlesql/public:type",
        "//googlesql/public:value",
        "@com_google_absl//absl/base:core_headers",
        "@com_google_absl//absl/flags:flag",
        "@com_google_absl//absl/synchronization",
        "@com_google_absl//absl/time",
        "@com_google_absl//absl/types:span",
    ],
)

cc_test(
    name = "simple_evaluator_table_iterator_test",
    srcs = ["simple_evaluator_table_iterator_test.cc"],
    deps = [
        ":simple_evaluator_table_iterator",
        "//googlesql/base:clock",
        "//googlesql/base:status",
        "//googlesql/base/testing:googlesql_gtest_main",
        "//googlesql/base/testing:status_matchers",
        "//googlesql/public:simple_catalog",
        "@com_google_absl//absl/memory",
        "@com_google_absl//absl/status:statusor",
        "@com_google_absl//absl/strings",
    ],
)

cc_library(
    name = "evaluator_test_table",
    testonly = 1,
    hdrs = ["evaluator_test_table.h"],
    deps = [
        ":simple_evaluator_table_iterator",
        "//googlesql/base",
        "//googlesql/base:clock",
        "//googlesql/base:status",
        "//googlesql/public:catalog",
        "//googlesql/public:evaluator_table_iterator",
        "//googlesql/public:simple_catalog",
        "//googlesql/public:type",
        "//googlesql/public:value",
        "@com_google_absl//absl/status:statusor",
        "@com_google_absl//absl/strings",
        "@com_google_absl//absl/types:span",
    ],
)

cc_library(
    name = "unicode_utils",
    srcs = ["unicode_utils.cc"],
    hdrs = ["unicode_utils.h"],
    deps = [
        "//googlesql/base",
        "@com_google_absl//absl/algorithm:container",
        "@com_google_absl//absl/base:nullability",
        "@com_google_absl//absl/flags:flag",
        "@com_google_absl//absl/log",
        "@com_google_absl//absl/strings",
        "@icu//:common",
    ],
)

cc_test(
    name = "unicode_utils_test",
    size = "small",
    srcs = ["unicode_utils_test.cc"],
    deps = [
        ":unicode_utils",
        "//googlesql/base/testing:googlesql_gtest_main",
        "@com_google_absl//absl/flags:flag",
    ],
)

cc_library(
    name = "json_parser",
    srcs = ["json_parser.cc"],
    hdrs = ["json_parser.h"],
    deps = [
        ":thread_stack",
        "//googlesql/base",
        "//googlesql/base:ret_check",
        "//googlesql/base:status",
        "//googlesql/base:strings",
        "@com_google_absl//absl/status",
        "@com_google_absl//absl/strings",
        "@com_googlesource_code_re2//:re2",
        "@icu//:headers",
    ],
)

cc_test(
    name = "json_parser_test",
    srcs = ["json_parser_test.cc"],
    deps = [
        ":json_parser",
        "//googlesql/base",
        "//googlesql/base/testing:googlesql_gtest_main",
        "//googlesql/base/testing:status_matchers",
        "//googlesql/compliance:depth_limit_detector_test_cases",
        "@com_google_absl//absl/status",
        "@com_google_absl//absl/strings",
        "@com_google_absl//absl/strings:str_format",
        "@com_google_absl//absl/time",
    ],
)

cc_library(
    name = "json_util",
    srcs = ["json_util.cc"],
    hdrs = ["json_util.h"],
    deps = [
        "@com_google_absl//absl/strings",
        "@com_google_absl//absl/strings:str_format",
    ],
)

cc_test(
    name = "json_util_test",
    srcs = ["json_util_test.cc"],
    deps = [
        ":json_util",
        "//googlesql/base/testing:googlesql_gtest_main",
        "@com_google_absl//absl/strings",
    ],
)

cc_library(
    name = "evaluator_registration_utils",
    srcs = ["evaluator_registration_utils.cc"],
    hdrs = ["evaluator_registration_utils.h"],
    deps = [
        "//googlesql/public:collator",
        "//googlesql/reference_impl/functions:register_all",
        "@com_google_absl//absl/base",
        "@com_google_absl//absl/strings:string_view",
    ],
)

cc_library(
    name = "proto_from_iterator",
    srcs = [
        "proto_from_iterator.cc",
    ],
    hdrs = [
        "proto_from_iterator.h",
    ],
    deps = [
        "//googlesql/base:ret_check",
        "//googlesql/base:status",
        "//googlesql/public:evaluator_table_iterator",
        "//googlesql/public:proto_value_conversion",
        "//googlesql/public:type",
        "//googlesql/public:value",
        "//googlesql/public/types",
        "@com_google_absl//absl/memory",
        "@com_google_absl//absl/status",
        "@com_google_absl//absl/status:statusor",
        "@com_google_absl//absl/strings",
        "@com_google_protobuf//:protobuf",
    ],
)

cc_test(
    name = "proto_from_iterator_test",
    size = "small",
    srcs = ["proto_from_iterator_test.cc"],
    deps = [
        ":proto_from_iterator",
        "//googlesql/base:status",
        "//googlesql/base/testing:googlesql_gtest_main",
        "//googlesql/base/testing:status_matchers",
        "//googlesql/common/testing:proto_matchers",
        "//googlesql/public:evaluator_table_iterator",
        "//googlesql/public:simple_catalog",
        "//googlesql/public:type",
        "//googlesql/public:value",
        "//googlesql/public/types",
        "//googlesql/testdata:test_schema_cc_proto",
        "@com_google_absl//absl/status",
        "@com_google_absl//absl/status:statusor",
        "@com_google_protobuf//:protobuf",
    ],
)

cc_library(
    name = "aggregate_null_handling",
    srcs = ["aggregate_null_handling.cc"],
    hdrs = ["aggregate_null_handling.h"],
    deps = [
        "//googlesql/base:case",
        "//googlesql/resolved_ast",
        "@com_google_absl//absl/container:flat_hash_set",
    ],
)

cc_library(
    name = "function_utils",
    srcs = ["function_utils.cc"],
    hdrs = ["function_utils.h"],
    deps = [
        "//googlesql/base:check",
        "//googlesql/public:function_headers",
        "@com_google_absl//absl/container:flat_hash_set",
        "@com_google_absl//absl/strings",
    ],
)

cc_library(
    name = "type_visitors",
    srcs = ["type_visitors.cc"],
    hdrs = ["type_visitors.h"],
    deps = [
        "//googlesql/base:ret_check",
        "//googlesql/base:status",
        "//googlesql/public:type_cc_proto",
        "//googlesql/public/types",
        "@com_google_absl//absl/status",
        "@com_google_absl//absl/status:statusor",
        "@com_google_absl//absl/types:span",
    ],
)

cc_test(
    name = "type_visitors_test",
    srcs = ["type_visitors_test.cc"],
    deps = [
        ":type_visitors",
        "//googlesql/base:ret_check",
        "//googlesql/base:status",
        "//googlesql/base/testing:googlesql_gtest_main",
        "//googlesql/base/testing:status_matchers",
        "//googlesql/common/testing:proto_matchers",
        "//googlesql/public/annotation:collation",
        "//googlesql/public/types",
        "//googlesql/testdata:test_schema_cc_proto",
        "@com_google_absl//absl/container:flat_hash_set",
        "@com_google_absl//absl/status",
        "@com_google_absl//absl/status:statusor",
        "@com_google_absl//absl/strings:string_view",
    ],
)

cc_library(
    name = "type_and_argument_kind_visitor",
    srcs = [
        "type_and_argument_kind_visitor.cc",
    ],
    hdrs = [
        "type_and_argument_kind_visitor.h",
    ],
    deps = [
        "//googlesql/base:ret_check",
        "//googlesql/base:status",
        "//googlesql/public:function_cc_proto",
        "//googlesql/public:function_headers",
        "//googlesql/public/types:type",
        "@com_google_absl//absl/base:no_destructor",
        "@com_google_absl//absl/container:flat_hash_map",
        "@com_google_absl//absl/status",
        "@com_google_absl//absl/status:statusor",
        "@com_google_absl//absl/types:span",
    ],
)

cc_library(
    name = "canonicalize_signed_zero_to_string",
    srcs = ["canonicalize_signed_zero_to_string.cc"],
    hdrs = ["canonicalize_signed_zero_to_string.h"],
    deps = ["@com_google_absl//absl/flags:flag"],
)

cc_library(
    name = "internal_analyzer_options",
    hdrs = ["internal_analyzer_options.h"],
    deps = [
        ":errors",
        "//googlesql/base:map_util",
        "//googlesql/public:analyzer_options",
        "//googlesql/public/types:type",
        "@com_google_absl//absl/status",
        "@com_google_absl//absl/strings",
        "@com_google_absl//absl/strings:string_view",
    ],
)

cc_library(
    name = "internal_property_graph",
    hdrs = ["internal_property_graph.h"],
    deps = [
        "//googlesql/base:check",
        "//googlesql/resolved_ast",
    ],
)

cc_library(
    name = "internal_analyzer_output_properties",
    hdrs = ["internal_analyzer_output_properties.h"],
    deps = [
        "//googlesql/public:analyzer_output_properties",
        "//googlesql/resolved_ast",
        "//googlesql/resolved_ast:target_syntax",
    ],
)

cc_test(
    name = "function_utils_test",
    srcs = ["function_utils_test.cc"],
    deps = [
        ":function_utils",
        "//googlesql/base/testing:googlesql_gtest_main",
        "//googlesql/public:function",
    ],
)

cc_library(
    name = "graph_element_utils",
    srcs = ["graph_element_utils.cc"],
    hdrs = ["graph_element_utils.h"],
    deps = [
        "//googlesql/base:ret_check",
        "//googlesql/base:status",
        "//googlesql/public:json_value",
        "//googlesql/public:value",
        "//googlesql/public/functions:json",
        "//googlesql/public/types",
        "//googlesql/resolved_ast",
        "@com_google_absl//absl/status:statusor",
        "@com_google_absl//absl/strings:string_view",
        "@com_google_absl//absl/types:span",
    ],
)

cc_test(
    name = "graph_element_utils_test",
    srcs = ["graph_element_utils_test.cc"],
    deps = [
        ":graph_element_utils",
        "//googlesql/base/testing:googlesql_gtest_main",
        "//googlesql/base/testing:status_matchers",
        "//googlesql/public:json_value",
        "//googlesql/public/functions:json",
        "//googlesql/public/types",
        "@com_google_absl//absl/types:span",
    ],
)

cc_test(
    name = "aggregate_null_handling_test",
    srcs = ["aggregate_null_handling_test.cc"],
    deps = [
        ":aggregate_null_handling",
        "//googlesql/base/testing:googlesql_gtest_main",
        "//googlesql/base/testing:status_matchers",
        "//googlesql/public:analyzer",
        "//googlesql/public:analyzer_options",
        "//googlesql/public:analyzer_output",
        "//googlesql/public:options_cc_proto",
        "//googlesql/resolved_ast",
        "//googlesql/testdata:sample_catalog",
    ],
)

cc_library(
    name = "function_signature_testutil",
    testonly = True,
    srcs = ["function_signature_testutil.cc"],
    hdrs = ["function_signature_testutil.h"],
    deps = [
        "//googlesql/public:function_cc_proto",
    ],
)

cc_library(
    name = "warning_sink",
    srcs = [
        "warning_sink.cc",
    ],
    hdrs = [
        "warning_sink.h",
    ],
    deps = [
        ":status_payload_utils",
        "//googlesql/proto:internal_error_location_cc_proto",
        "//googlesql/public:deprecation_warning_cc_proto",
        "@com_google_absl//absl/container:flat_hash_set",
        "@com_google_absl//absl/status",
        "@com_google_absl//absl/types:span",
    ],
)

cc_test(
    name = "warning_sink_test",
    srcs = [
        "warning_sink_test.cc",
    ],
    deps = [
        ":errors",
        ":warning_sink",
        "//googlesql/base/testing:googlesql_gtest_main",
        "//googlesql/base/testing:status_matchers",
        "//googlesql/public:parse_location",
        "@com_google_absl//absl/strings:string_view",
    ],
)

cc_library(
    name = "scope_error_catalog",
    srcs = ["scope_error_catalog.cc"],
    hdrs = ["scope_error_catalog.h"],
    deps = [
        "//googlesql/base:ret_check",
        "//googlesql/base:status",
        "//googlesql/public:catalog",
        "//googlesql/public:strings",
        "//googlesql/public/types",
        "@com_google_absl//absl/memory",
        "@com_google_absl//absl/status",
        "@com_google_absl//absl/status:statusor",
        "@com_google_absl//absl/strings",
        "@com_google_absl//absl/strings:string_view",
        "@com_google_absl//absl/types:span",
    ],
)

cc_test(
    name = "scope_error_catalog_test",
    srcs = [
        "scope_error_catalog_test.cc",
    ],
    deps = [
        ":scope_error_catalog",
        "//googlesql/base:status",
        "//googlesql/base/testing:googlesql_gtest_main",
        "//googlesql/base/testing:status_matchers",
        "//googlesql/public:catalog",
        "//googlesql/public:function",
        "//googlesql/public:function_headers",
        "//googlesql/public:simple_catalog",
        "//googlesql/public/types",
        "@com_google_absl//absl/status",
        "@com_google_absl//absl/status:statusor",
        "@com_google_absl//absl/strings",
    ],
)

cc_library(
    name = "resolution_scope",
    srcs = ["resolution_scope.cc"],
    hdrs = ["resolution_scope.h"],
    deps = [
        "//googlesql/base:ret_check",
        "//googlesql/base:strings",
        "//googlesql/parser:parse_tree",
        "@com_google_absl//absl/status:statusor",
        "@com_google_absl//absl/strings",
        "@com_google_absl//absl/strings:string_view",
    ],
)

cc_test(
    name = "resolution_scope_test",
    srcs = [
        "resolution_scope_test.cc",
    ],
    deps = [
        ":resolution_scope",
        "//googlesql/base/testing:googlesql_gtest_main",
        "//googlesql/base/testing:status_matchers",
        "//googlesql/parser",
        "//googlesql/public:language_options",
        "@com_google_absl//absl/status",
        "@com_google_absl//absl/strings:string_view",
    ],
)

cc_library(
    name = "reflection_helper",
    srcs = ["reflection_helper.cc"],
    hdrs = ["reflection_helper.h"],
    deps = [
        ":reflection_cc_proto",
        "//googlesql/public:strings",
        "@com_google_absl//absl/strings",
        "@com_google_absl//absl/strings:str_format",
        "@com_google_absl//absl/types:span",
        "@com_google_protobuf//:protobuf",
    ],
)

proto_library(
    name = "reflection_proto",
    srcs = ["reflection.proto"],
)

cc_proto_library(
    name = "reflection_cc_proto",
    deps = [":reflection_proto"],
)
