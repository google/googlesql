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

load("@rules_java//java:defs.bzl", "java_library")
load("//javatests/com/google/googlesql:junit_test_suites.bzl", "junit_test_suites")

# Tests for GoogleSQL public Java API.
package(
    default_testonly = 1,
    default_visibility = ["//googlesql/base:googlesql_implementation"],
)

java_library(
    name = "tests",
    srcs = glob(["*Test.java"]),
    deps = [
        ":util",
        "@com_google_protobuf//:protobuf_java",
        "@maven//:com_google_guava_guava",
        "@maven//:com_google_guava_guava_testlib",
        "//java/com/google/googlesql:analyzer",
        "//java/com/google/googlesql:client",
        "//java/com/google/googlesql:property_graph",
        "//java/com/google/googlesql:types",
        "//java/com/google/googlesql/resolvedast",
        "//googlesql/local_service:local_service_java_proto",
        "//googlesql/proto:function_java_proto",
        "//googlesql/proto:options_java_proto",
        "//googlesql/proto:simple_catalog_java_proto",
        "//googlesql/proto:simple_property_graph_java_proto",
        # "//googlesql/public/functions:datetime_java_proto",
        "//googlesql/public:annotation_java_proto",
        "//googlesql/public:builtin_function_java_proto",
        "//googlesql/public:deprecation_warning_java_proto",
        "//googlesql/public:error_location_java_proto",
        "//googlesql/public:formatter_options_java_proto",
        "//googlesql/public:function_java_proto",
        "//googlesql/public:options_java_proto",
        "//googlesql/public:parse_resume_location_java_proto",
        "//googlesql/public:simple_table_java_proto",
        "//googlesql/public:simple_value_java_proto",
        "//googlesql/public:type_proto_java_proto",
        "//googlesql/public/functions:rounding_mode_java_proto",
        "//googlesql/public:type_parameters_java_proto",
        "//googlesql/public:value_java_proto",
        "//googlesql/public/proto:type_annotation_java_proto",
        "//googlesql/resolved_ast:resolved_node_kind_java_proto",  # buildcleaner: keep
        "//googlesql/testdata:bad_test_schema_java_proto",
        "//googlesql/testdata:test_proto3_java_proto",
        "//googlesql/testdata:test_schema_java_proto",
        "@io_grpc_grpc_java//core",
        "@maven//:joda_time_joda_time",
        "@maven//:junit_junit",
        "@maven//:com_google_truth_truth",
        "@maven//:com_google_truth_extensions_truth_proto_extension",
    ],
)

java_library(
    name = "util",
    srcs = [
        "AnnotationMapTestConstants.java",
        "TestUtil.java",
        "TypeTestBase.java",
    ],
    deps = [
        "//googlesql/public:type_proto_java_proto",
        "//java/com/google/googlesql:types",
        "@com_google_protobuf//:protobuf_java",
        "@maven//:com_google_guava_guava",
        "@maven//:com_google_guava_guava_testlib",
        "@maven//:com_google_truth_truth",
    ],
)

junit_test_suites(
    name = "gen_tests_jni",
    runtime_deps = ["//java/com/google/googlesql:jni_channel"],
    deps = [":tests"],
)
