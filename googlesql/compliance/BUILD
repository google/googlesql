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

load("@bazel_skylib//:bzl_library.bzl", "bzl_library")
load("@com_google_protobuf//bazel:java_proto_library.bzl", "java_proto_library")
load("@com_google_protobuf//bazel:proto_library.bzl", "proto_library")
load("@rules_cc//cc:cc_library.bzl", "cc_library")
load("@rules_cc//cc:cc_test.bzl", "cc_test")
load("@rules_python//python:proto.bzl", "py_proto_library")
load(
    "//googlesql/compliance:builddefs.bzl",
    "googlesql_compliance_test",
)

package(
    default_visibility = ["//googlesql/base:googlesql_implementation"],
)

cc_library(
    name = "measure_test_tables",
    testonly = 1,
    srcs = ["measure_test_tables.cc"],
    hdrs = ["measure_test_tables.h"],
    deps = [
        ":test_driver",
        "//googlesql/common:internal_value",
        "//googlesql/common:measure_analysis_utils",
        "//googlesql/public:value",
        "//googlesql/testing:test_value",
    ],
)

cc_library(
    name = "sql_test_base",
    testonly = 1,
    srcs = ["sql_test_base.cc"],
    hdrs = ["sql_test_base.h"],
    deps = [
        ":compliance_label_cc_proto",
        ":compliance_label_extractor",
        ":known_error_cc_proto",
        ":legal_runtime_errors",
        ":matchers",
        ":measure_test_tables",
        ":sql_test_filebased_options",
        ":test_database_catalog",
        ":test_driver",
        ":test_driver_cc_proto",
        ":test_util",
        "//googlesql/base",
        "//googlesql/base:file_util",
        "//googlesql/base:map_util",
        "//googlesql/base:path",
        "//googlesql/base:ret_check",
        "//googlesql/base:source_location",
        "//googlesql/base:status",
        "//googlesql/base/testing:status_matchers",
        "//googlesql/common:float_margin",
        "//googlesql/common:internal_value",
        "//googlesql/common:measure_analysis_utils",
        "//googlesql/common:options_utils",
        "//googlesql/common:status_payload_utils",
        "//googlesql/public:analyzer",
        "//googlesql/public:analyzer_options",
        "//googlesql/public:feature_label_extractor",
        "//googlesql/public:language_options",
        "//googlesql/public:options_cc_proto",
        "//googlesql/public:parse_helpers",
        "//googlesql/public:simple_catalog",
        "//googlesql/public:simple_catalog_util",
        "//googlesql/public:strings",
        "//googlesql/public:type",
        "//googlesql/public:type_cc_proto",
        "//googlesql/public:value",
        "//googlesql/public/functions:string",
        "//googlesql/public/types",
        "//googlesql/reference_impl:evaluation",
        "//googlesql/reference_impl:reference_driver",
        "//googlesql/resolved_ast",
        "//googlesql/resolved_ast:resolved_node_kind_cc_proto",
        "//googlesql/resolved_ast:sql_builder",
        "//googlesql/testing:test_value",
        "//googlesql/testing:type_util",
        "@com_google_absl//absl/base:core_headers",
        "@com_google_absl//absl/cleanup",
        "@com_google_absl//absl/container:btree",
        "@com_google_absl//absl/container:flat_hash_map",
        "@com_google_absl//absl/container:node_hash_set",
        "@com_google_absl//absl/flags:flag",
        "@com_google_absl//absl/functional:bind_front",
        "@com_google_absl//absl/log",
        "@com_google_absl//absl/status",
        "@com_google_absl//absl/status:statusor",
        "@com_google_absl//absl/strings",
        "@com_google_absl//absl/strings:str_format",
        "@com_google_absl//absl/time",
        "@com_google_absl//absl/types:span",
        "@com_google_farmhash//:farmhash_fingerprint",
        "@com_google_file_based_test_driver//file_based_test_driver",
        "@com_google_file_based_test_driver//file_based_test_driver:run_test_case_result",
        "@com_google_file_based_test_driver//file_based_test_driver:test_case_options",
        "@com_google_googletest//:gtest",
        "@com_google_protobuf//:protobuf",
        "@com_googlesource_code_re2//:re2",
    ],
    alwayslink = 1,
)

cc_library(
    name = "sql_test_filebased_options",
    testonly = 1,
    srcs = ["sql_test_filebased_options.cc"],
    hdrs = ["sql_test_filebased_options.h"],
    deps = [
        ":known_error_cc_proto",
        ":parameters_test_util",
        ":test_driver",
        "//googlesql/base:file_util",
        "//googlesql/base:ret_check",
        "//googlesql/base:status",
        "//googlesql/public:options_cc_proto",
        "//googlesql/public:type",
        "//googlesql/public:type_cc_proto",
        "//googlesql/public:value",
        "//googlesql/reference_impl:reference_driver",
        "@com_google_absl//absl/container:flat_hash_set",
        "@com_google_absl//absl/flags:flag",
        "@com_google_absl//absl/memory",
        "@com_google_absl//absl/status",
        "@com_google_absl//absl/status:statusor",
        "@com_google_absl//absl/strings",
        "@com_google_file_based_test_driver//file_based_test_driver:test_case_options",
    ],
)

cc_test(
    name = "sql_test_filebased_options_test",
    testonly = 1,
    srcs = ["sql_test_filebased_options_test.cc"],
    deps = [
        ":sql_test_filebased_options",
        ":test_driver",
        "//googlesql/base/testing:googlesql_gtest_main",
        "//googlesql/base/testing:status_matchers",
        "//googlesql/reference_impl:reference_driver",
        "@com_google_absl//absl/strings:string_view",
    ],
)

cc_library(
    name = "compliance_label_extractor",
    srcs = ["compliance_label_extractor.cc"],
    hdrs = ["compliance_label_extractor.h"],
    deps = [
        "//googlesql/base:check",
        "//googlesql/base:logging",
        "//googlesql/base:ret_check",
        "//googlesql/base:status",
        "//googlesql/common:function_utils",
        "//googlesql/public:builtin_function",
        "//googlesql/public:builtin_function_cc_proto",
        "//googlesql/public:builtin_function_options",
        "//googlesql/public:options_cc_proto",
        "//googlesql/public:type",
        "//googlesql/public:type_cc_proto",
        "//googlesql/resolved_ast",
        "//googlesql/resolved_ast:resolved_ast_enums_cc_proto",
        "//googlesql/resolved_ast:resolved_node_kind_cc_proto",
        "@com_google_absl//absl/container:btree",
        "@com_google_absl//absl/container:flat_hash_map",
        "@com_google_absl//absl/container:flat_hash_set",
        "@com_google_absl//absl/status",
        "@com_google_absl//absl/strings",
    ],
)

cc_test(
    name = "compliance_label_extractor_test",
    srcs = ["compliance_label_extractor_test.cc"],
    deps = [
        ":compliance_label_extractor",
        "//googlesql/base:status",
        "//googlesql/base/testing:googlesql_gtest_main",
        "//googlesql/public:builtin_function_cc_proto",
        "//googlesql/public/types",
        "//googlesql/resolved_ast",
        "//googlesql/resolved_ast:resolved_ast_builder",
        "@com_google_absl//absl/strings",
    ],
)

cc_library(
    name = "compliance_test_base",
    testonly = 1,
    data = glob(["testdata/*.test"]) + [
        "//googlesql/public/functions:array_find_mode_proto",
        "//googlesql/public/functions:array_zip_mode_proto",
        "//googlesql/public/functions:bitwise_agg_mode_proto",
        "//googlesql/public/functions:rounding_mode_proto",
        "//googlesql/testdata:test_proto3_proto",
        "//googlesql/testdata:test_schema_proto",
        "@com_google_googleapis//google/type:date_proto",
        "@com_google_googleapis//google/type:latlng_proto",
        "@com_google_googleapis//google/type:timeofday_proto",
        "@com_google_protobuf//:timestamp_proto",
        "@com_google_protobuf//:wrappers_proto",
    ],
    deps = [
        ":sql_test_base",
    ],
)

cc_library(
    name = "test_driver",
    testonly = 1,
    srcs = ["test_driver.cc"],
    hdrs = ["test_driver.h"],
    deps = [
        ":test_driver_cc_proto",
        "//googlesql/base",
        "//googlesql/base:file_util",
        "//googlesql/base:path",
        "//googlesql/base:ret_check",
        "//googlesql/base:status",
        "//googlesql/common:measure_analysis_utils",
        "//googlesql/common:type_visitors",
        "//googlesql/common/testing:testing_proto_util",
        "//googlesql/public:annotation_cc_proto",
        "//googlesql/public:language_options",
        "//googlesql/public:options_cc_proto",
        "//googlesql/public:type",
        "//googlesql/public:value",
        "//googlesql/public/functions:date_time_util",
        "//googlesql/public/types",
        "@com_google_absl//absl/base:core_headers",
        "@com_google_absl//absl/flags:flag",
        "@com_google_absl//absl/status",
        "@com_google_absl//absl/status:statusor",
        "@com_google_absl//absl/strings",
        "@com_google_absl//absl/time",
        "@com_google_absl//absl/types:span",
        "@com_google_protobuf//:protobuf",
    ],
)

cc_test(
    name = "test_driver_test",
    size = "small",
    srcs = ["test_driver_test.cc"],
    deps = [
        ":test_driver",
        ":test_driver_cc_proto",
        "//googlesql/base:check",
        "//googlesql/base/testing:googlesql_gtest_main",
        "//googlesql/base/testing:status_matchers",
        "//googlesql/common:measure_analysis_utils",
        "//googlesql/public:options_cc_proto",
        "//googlesql/public:value",
        "//googlesql/public/types",
        "//googlesql/testdata:test_schema_cc_proto",
        "@com_google_absl//absl/strings",
        "@com_google_absl//absl/strings:cord",
    ],
)

proto_library(
    name = "test_driver_proto",
    srcs = ["test_driver.proto"],
    deps = [
        "//googlesql/public:annotation_proto",
        "//googlesql/public:options_proto",
        "//googlesql/public:type_proto",
        "//googlesql/public:value_proto",
    ],
)

cc_proto_library(
    name = "test_driver_cc_proto",
    deps = [":test_driver_proto"],
)

java_proto_library(
    name = "test_driver_java_proto",
    deps = [":test_driver_proto"],
)

cc_library(
    name = "test_util",
    testonly = 1,
    srcs = ["test_util.cc"],
    hdrs = ["test_util.h"],
    deps = [
        "//googlesql/base",
        "//googlesql/base:map_util",
        "//googlesql/base:status",
        "//googlesql/public/functions:array_find_mode_cc_proto",
        "//googlesql/public/functions:array_zip_mode_cc_proto",
        "//googlesql/public/functions:rounding_mode_cc_proto",
        "@com_google_absl//absl/base:no_destructor",
        "@com_google_absl//absl/container:btree",
        "@com_google_absl//absl/container:flat_hash_map",
        "@com_google_absl//absl/flags:commandlineflag",
        "@com_google_absl//absl/flags:reflection",
        "@com_google_absl//absl/status",
        "@com_google_absl//absl/strings",
        "@com_google_absl//absl/strings:str_format",
        "@com_google_googleapis//google/type:date_cc_proto",
        "@com_google_googleapis//google/type:timeofday_cc_proto",
        "@com_google_protobuf//:protobuf",
    ],
)

cc_test(
    name = "test_util_test",
    srcs = ["test_util_test.cc"],
    deps = [
        ":test_util",
        "//googlesql/base/testing:googlesql_gtest_main",
    ],
)

cc_library(
    name = "parameters_test_util",
    testonly = 1,
    srcs = ["parameters_test_util.cc"],
    hdrs = ["parameters_test_util.h"],
    deps = [
        ":test_driver",
        "//googlesql/base:ret_check",
        "//googlesql/base:status",
        "//googlesql/public:language_options",
        "//googlesql/public:type",
        "//googlesql/public:value",
        "//googlesql/public/types",
        "//googlesql/reference_impl:reference_driver",
        "@com_google_absl//absl/status",
        "@com_google_absl//absl/strings",
    ],
)

cc_library(
    name = "depth_limit_detector_test_cases",
    testonly = 1,
    srcs = ["depth_limit_detector_test_cases.cc"],
    hdrs = [
        "depth_limit_detector_internal.h",
        "depth_limit_detector_test_cases.h",
    ],
    deps = [
        "//googlesql/base:check",
        "//googlesql/public:language_options",
        "//googlesql/public:options_cc_proto",
        "@com_google_absl//absl/base:core_headers",
        "@com_google_absl//absl/flags:flag",
        "@com_google_absl//absl/functional:any_invocable",
        "@com_google_absl//absl/functional:function_ref",
        "@com_google_absl//absl/log",
        "@com_google_absl//absl/status",
        "@com_google_absl//absl/strings:string_view",
        "@com_google_absl//absl/time",
        "@com_google_absl//absl/types:span",
        "@com_google_googletest//:gtest",
    ],
)

cc_library(
    name = "compliance_test_cases",
    testonly = 1,
    srcs = ["compliance_test_cases.cc"],
    hdrs = ["compliance_test_cases.h"],
    deps = [
        # Required runtime dependency
        # buildcleaner: keep
        ":compliance_test_base",
        ":functions_testlib",
        ":sql_test_base",
        ":depth_limit_detector_test_cases",
        ":test_driver",
        "//googlesql/base",
        "//googlesql/base:path",
        "@com_google_protobuf//:protobuf",
        "//googlesql/common:float_margin",
        "//googlesql/base/testing:status_matchers",
        "//googlesql/common/testing:testing_proto_util",
        "//googlesql/public:civil_time",
        "//googlesql/public:language_options",
        "//googlesql/public:options_cc_proto",
        "//googlesql/public:strings",
        "//googlesql/public:type",
        "//googlesql/public:type_cc_proto",
        "//googlesql/public:value",
        "//googlesql/public/functions:date_time_util",
        "//googlesql/reference_impl:reference_driver",
        "//googlesql/testdata:test_schema_cc_proto",
        "//googlesql/testing:test_function",
        "//googlesql/testing:test_value",
        "//googlesql/testing:type_util",
        "@com_google_googletest//:gtest",
        "@com_google_absl//absl/base",
        "@com_google_absl//absl/base:core_headers",
        "@com_google_absl//absl/container:flat_hash_set",
        "@com_google_absl//absl/flags:flag",
        "@com_google_absl//absl/status",
        "@com_google_absl//absl/status:statusor",
        "@com_google_absl//absl/strings",
        "@com_google_absl//absl/strings:cord",
        "@com_google_absl//absl/strings:str_format",
        "@com_google_absl//absl/types:span",
        "//googlesql/base:status",
    ],
)

cc_library(
    name = "matchers",
    testonly = 1,
    srcs = ["matchers.cc"],
    hdrs = ["matchers.h"],
    deps = [
        "//googlesql/base:map_util",
        "//googlesql/base:status",
        "@com_google_absl//absl/base:core_headers",
        "@com_google_absl//absl/container:fixed_array",
        "@com_google_absl//absl/status",
        "@com_google_absl//absl/strings",
        "@com_google_absl//absl/types:span",
        "@com_google_protobuf//:protobuf",
        "@com_googlesource_code_re2//:re2",
    ],
)

cc_test(
    name = "matchers_test",
    size = "small",
    srcs = ["matchers_test.cc"],
    deps = [
        ":matchers",
        ":runtime_expected_errors",
        "//googlesql/base:logging",
        "//googlesql/base/testing:googlesql_gtest_main",
        "//googlesql/testdata:test_schema_cc_proto",
        "@com_google_absl//absl/status",
    ],
)

cc_library(
    name = "runtime_expected_errors",
    testonly = 1,
    srcs = ["runtime_expected_errors.cc"],
    hdrs = ["runtime_expected_errors.h"],
    deps = [
        ":matchers",
        "//googlesql/base:status",
        "@com_google_absl//absl/status",
        "@com_google_absl//absl/strings",
    ],
)

cc_library(
    name = "test_database_catalog",
    testonly = 1,
    srcs = ["test_database_catalog.cc"],
    hdrs = ["test_database_catalog.h"],
    deps = [
        ":test_driver",
        ":test_util",
        "//googlesql/base:check",
        "//googlesql/base:ret_check",
        "//googlesql/base:status",
        "//googlesql/common:internal_value",
        "//googlesql/common:measure_analysis_utils",
        "//googlesql/common/testing:testing_proto_util",
        "//googlesql/public:analyzer_output",
        "//googlesql/public:builtin_function",
        "//googlesql/public:builtin_function_options",
        "//googlesql/public:catalog",
        "//googlesql/public:function_headers",
        "//googlesql/public:language_options",
        "//googlesql/public:simple_catalog",
        "//googlesql/public:simple_catalog_util",
        "//googlesql/public:type",
        "//googlesql/public/types",
        "@com_google_absl//absl/algorithm:container",
        "@com_google_absl//absl/base:core_headers",
        "@com_google_absl//absl/container:flat_hash_map",
        "@com_google_absl//absl/container:flat_hash_set",
        "@com_google_absl//absl/log",
        "@com_google_absl//absl/status",
        "@com_google_absl//absl/strings",
        "@com_google_absl//absl/types:span",
        "@com_google_protobuf//:protobuf",
    ],
)

cc_library(
    name = "functions_testlib",
    testonly = 1,
    srcs = [
        "functions_testlib_1.cc",
        "functions_testlib_2.cc",
        "functions_testlib_cast.cc",
        "functions_testlib_common.cc",
        "functions_testlib_distance.cc",
        "functions_testlib_format.cc",
        "functions_testlib_format_floating_point.cc",
        "functions_testlib_format_integral.cc",
        "functions_testlib_format_json.cc",
        "functions_testlib_format_nulls.cc",
        "functions_testlib_format_numeric.cc",
        "functions_testlib_format_strings.cc",
        "functions_testlib_generate_array.cc",
        "functions_testlib_interval.cc",
        "functions_testlib_json.cc",
        "functions_testlib_json_extract_array.cc",
        "functions_testlib_like.cc",
        "functions_testlib_math.cc",
        "functions_testlib_math_round.cc",
        "functions_testlib_math_trig.cc",
        "functions_testlib_net.cc",
        "functions_testlib_numeric.cc",
        "functions_testlib_range.cc",
        "functions_testlib_regex.cc",
        "functions_testlib_string_1.cc",
        "functions_testlib_string_2.cc",
        "functions_testlib_string_3.cc",
        "functions_testlib_string_4.cc",
        "functions_testlib_string_5.cc",
        "functions_testlib_timestamp.cc",
        "functions_testlib_tojson.cc",
        "functions_testlib_uuid.cc",
    ],
    hdrs = [
        "functions_testlib.h",
        "functions_testlib_common.h",
    ],
    copts = ["$(STACK_FRAME_UNLIMITED)"],
    deps = [
        "//googlesql/base",
        "//googlesql/base:check",
        "//googlesql/base:map_util",
        "//googlesql/base:ret_check",
        "//googlesql/base:source_location",
        "//googlesql/base:status",
        "//googlesql/base:stl_util",
        "//googlesql/common:float_margin",
        "//googlesql/common:status_payload_utils",
        "//googlesql/common/testing:testing_proto_util",
        "//googlesql/public:civil_time",
        "//googlesql/public:coercer",
        "//googlesql/public:interval_value",
        "//googlesql/public:interval_value_test_util",
        "//googlesql/public:json_value",
        "//googlesql/public:language_options",
        "//googlesql/public:numeric_value",
        "//googlesql/public:options_cc_proto",
        "//googlesql/public:pico_time",
        "//googlesql/public:timestamp_picos_value",
        "//googlesql/public:token_list_util",
        "//googlesql/public:type",
        "//googlesql/public:type_cc_proto",
        "//googlesql/public:uuid_value",
        "//googlesql/public:value",
        "//googlesql/public/functions:bitwise_agg_mode_cc_proto",
        "//googlesql/public/functions:common_proto",
        "//googlesql/public/functions:date_time_util",
        "//googlesql/public/functions:datetime_cc_proto",
        "//googlesql/public/functions:range",
        "//googlesql/public/types",
        "//googlesql/testdata:test_schema_cc_proto",
        "//googlesql/testing:test_function",
        "//googlesql/testing:test_value",
        "@com_google_absl//absl/algorithm:container",
        "@com_google_absl//absl/base",
        "@com_google_absl//absl/container:btree",
        "@com_google_absl//absl/log",
        "@com_google_absl//absl/log:log_streamer",
        "@com_google_absl//absl/random",
        "@com_google_absl//absl/status",
        "@com_google_absl//absl/status:statusor",
        "@com_google_absl//absl/strings",
        "@com_google_absl//absl/strings:cord",
        "@com_google_absl//absl/strings:str_format",
        "@com_google_absl//absl/time",
        "@com_google_absl//absl/types:span",
        "@com_google_googleapis//google/type:date_cc_proto",
        "@com_google_googleapis//google/type:latlng_cc_proto",
        "@com_google_googleapis//google/type:timeofday_cc_proto",
        "@com_google_googletest//:gtest",
        "@com_google_protobuf//:protobuf",
        "@com_google_protobuf//:time_util",
        "@com_googlesource_code_re2//:re2",
    ],
)

proto_library(
    name = "known_error_proto",
    srcs = ["known_error.proto"],
)

proto_library(
    name = "compliance_label_proto",
    srcs = ["compliance_label.proto"],
    deps = [
        ":known_error_proto",
    ],
)

cc_proto_library(
    name = "compliance_label_cc_proto",
    deps = [":compliance_label_proto"],
)

cc_proto_library(
    name = "known_error_cc_proto",
    deps = [":known_error_proto"],
)

# This test verifies the golden query results in testbased compliance tests and the
# expected result values in codebased compliance tests.
#
# It may be reasonable to fold `required_features_integrity_test` into this test.
# TODO: b/234509805 - Fold `required_features_integrity_test` into this test.
googlesql_compliance_test(
    name = "compliance_goldens_test",
    size = "large",
    args = [
        "--googlesql_verify_compliance_goldens",
        "--googlesql_compliance_print_array_orderedness",
    ],
    known_error_files = [
        "//googlesql/compliance/known_errors:reference_impl_known_errors.textproto",
        "//googlesql/compliance/known_errors:googlesql_known_errors.textproto",
    ],
    shard_count = 30,
    deps = [
        "//googlesql/reference_impl:reference_driver",
        # buildcleaner: keep
        "//googlesql/reference_impl:use_reference_driver_for_compliance_test",
    ],
)

cc_library(
    name = "legal_runtime_errors",
    testonly = 1,
    srcs = ["legal_runtime_errors.cc"],
    hdrs = ["legal_runtime_errors.h"],
    deps = [
        ":matchers",
        "//googlesql/base:status",
        "@com_google_absl//absl/memory",
    ],
)

# To avoid linker errors, these files need to be built from source for each test-driver running
# binary.
filegroup(
    name = "run_compliance_driver_files",
    srcs = [
        "run_compliance_driver.cc",
    ],
)
