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

# Tests for GoogleSQL resolvedast classes.
package(
    default_testonly = 1,
    default_visibility = ["//googlesql/base:googlesql_implementation"],
)

java_library(
    name = "tests",
    srcs = glob(["*.java"]),
    deps = [
        "//googlesql/proto:function_java_proto",
        "//googlesql/public:function_java_proto",
        "//googlesql/public:options_java_proto",
        "//googlesql/public:type_proto_java_proto",
        "//googlesql/public:value_java_proto",
        "//googlesql/resolved_ast:resolved_ast_enums_java_proto",
        "//googlesql/resolved_ast:serialization_java_proto",
        "//java/com/google/googlesql:analyzer",
        "//java/com/google/googlesql:client",
        "//java/com/google/googlesql:testaccess",
        "//java/com/google/googlesql:types",
        "//java/com/google/googlesql/resolvedast",
        "//javatests/com/google/googlesql:util",
        "@maven//:com_google_guava_guava",
        "@maven//:com_google_truth_truth",
        "@maven//:junit_junit",
    ],
)

junit_test_suites(
    name = "gen_tests",
    runtime_deps = ["//java/com/google/googlesql:jni_channel"],
    deps = [":tests"],
)
