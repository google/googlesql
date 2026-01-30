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

# Definitions of optional builtin functions for the reference implementation and evaluator library.
# See ../public/evaluator_lite.h for more information.
#
# Static self-registration with alwayslink=1 is a common pattern for this, but
# explicitly calling the registration functions is preferred here to allow
# the linker to prune unused modules.

load("@rules_cc//cc:cc_library.bzl", "cc_library")

package(
    default_visibility = ["//googlesql/base:googlesql_implementation"],
)

cc_library(
    name = "register_all",
    srcs = ["register_all.cc"],
    hdrs = ["register_all.h"],
    deps = [
        ":compression",
        ":graph",
        ":hash",
        ":json",
        ":map",
        ":range",
        ":string_with_collation",
        ":uuid",
    ],
)

cc_library(
    name = "hash",
    srcs = ["hash.cc"],
    hdrs = ["hash.h"],
    deps = [
        "//googlesql/base:ret_check",
        "//googlesql/base:status",
        "//googlesql/public:type_cc_proto",
        "//googlesql/public:value",
        "//googlesql/public/functions:hash",
        "//googlesql/public/types",
        "//googlesql/reference_impl:evaluation",
        "@com_google_absl//absl/log",
        "@com_google_absl//absl/status",
        "@com_google_absl//absl/status:statusor",
        "@com_google_absl//absl/strings:string_view",
        "@com_google_absl//absl/types:span",
    ],
)

cc_library(
    name = "json",
    srcs = ["json.cc"],
    hdrs = ["json.h"],
    deps = [
        "//googlesql/base:check",
        "//googlesql/base:ret_check",
        "//googlesql/base:status",
        "//googlesql/common:errors",
        "//googlesql/common:internal_value",
        "//googlesql/public:json_value",
        "//googlesql/public:language_options",
        "//googlesql/public:options_cc_proto",
        "//googlesql/public:type_cc_proto",
        "//googlesql/public:value",
        "//googlesql/public/functions:json",
        "//googlesql/public/functions:json_format",
        "//googlesql/public/functions:json_internal",
        "//googlesql/public/functions:to_json",
        "//googlesql/public/functions:unsupported_fields_cc_proto",
        "//googlesql/public/types",
        "//googlesql/reference_impl:evaluation",
        "@com_google_absl//absl/status",
        "@com_google_absl//absl/status:statusor",
        "@com_google_absl//absl/strings",
        "@com_google_absl//absl/types:span",
    ],
)

cc_library(
    name = "string_with_collation",
    srcs = ["string_with_collation.cc"],
    hdrs = ["string_with_collation.h"],
    deps = [
        "//googlesql/base:ret_check",
        "//googlesql/base:status",
        "//googlesql/public:collator",
        "//googlesql/public:type_cc_proto",
        "//googlesql/public:value",
        "//googlesql/public/functions:string_with_collation",
        "//googlesql/public/types",
        "//googlesql/reference_impl:evaluation",
        "@com_google_absl//absl/status",
        "@com_google_absl//absl/status:statusor",
        "@com_google_absl//absl/strings:cord",
        "@com_google_absl//absl/strings:string_view",
        "@com_google_absl//absl/types:span",
    ],
)

cc_library(
    name = "like",
    srcs = ["like.cc"],
    hdrs = ["like.h"],
    deps = [
        "//googlesql/base:check",
        "//googlesql/base:ret_check",
        "//googlesql/base:status",
        "//googlesql/public:collator",
        "//googlesql/public:type",
        "//googlesql/public:type_cc_proto",
        "//googlesql/public:value",
        "//googlesql/public/functions:like",
        "//googlesql/public/functions:string_with_collation",
        "//googlesql/public/proto:type_annotation_cc_proto",
        "//googlesql/public/types",
        "@com_google_absl//absl/status",
        "@com_google_absl//absl/status:statusor",
        "@com_google_absl//absl/strings",
        "@com_google_absl//absl/types:span",
        "@com_googlesource_code_re2//:re2",
    ],
)

cc_library(
    name = "uuid",
    srcs = ["uuid.cc"],
    hdrs = ["uuid.h"],
    deps = [
        "//googlesql/base:ret_check",
        "//googlesql/public:value",
        "//googlesql/public/functions:uuid",
        "//googlesql/public/types",
        "//googlesql/reference_impl:evaluation",
        "@com_google_absl//absl/status:statusor",
        "@com_google_absl//absl/types:span",
    ],
)

cc_library(
    name = "range",
    srcs = ["range.cc"],
    hdrs = ["range.h"],
    deps = [
        "//googlesql/base:ret_check",
        "//googlesql/base:source_location",
        "//googlesql/base:status",
        "//googlesql/common:errors",
        "//googlesql/public:civil_time",
        "//googlesql/public:interval_value",
        "//googlesql/public:type_cc_proto",
        "//googlesql/public:value",
        "//googlesql/public/functions:range",
        "//googlesql/public/types",
        "//googlesql/reference_impl:evaluation",
        "@com_google_absl//absl/status",
        "@com_google_absl//absl/status:statusor",
        "@com_google_absl//absl/time",
        "@com_google_absl//absl/types:span",
    ],
)

cc_library(
    name = "graph",
    srcs = ["graph.cc"],
    hdrs = ["graph.h"],
    deps = [
        "//googlesql/base:check",
        "//googlesql/base:ret_check",
        "//googlesql/base:status",
        "//googlesql/base:strings",
        "//googlesql/common:errors",
        "//googlesql/public:coercer",
        "//googlesql/public:json_value",
        "//googlesql/public:options_cc_proto",
        "//googlesql/public:type",
        "//googlesql/public:type_cc_proto",
        "//googlesql/public:value",
        "//googlesql/public/functions:json",
        "//googlesql/public/functions:json_format",
        "//googlesql/public/types",
        "//googlesql/reference_impl:evaluation",
        "@com_google_absl//absl/algorithm:container",
        "@com_google_absl//absl/container:flat_hash_set",
        "@com_google_absl//absl/status",
        "@com_google_absl//absl/status:statusor",
        "@com_google_absl//absl/strings:string_view",
        "@com_google_absl//absl/time",
        "@com_google_absl//absl/types:span",
    ],
)

cc_library(
    name = "map",
    srcs = ["map.cc"],
    hdrs = ["map.h"],
    deps = [
        "//googlesql/base:check",
        "//googlesql/base:optional_ref",
        "//googlesql/base:ret_check",
        "//googlesql/base:status",
        "//googlesql/common:errors",
        "//googlesql/common:internal_value",
        "//googlesql/public:type",
        "//googlesql/public:type_cc_proto",
        "//googlesql/public:value",
        "//googlesql/public/types",
        "//googlesql/reference_impl:evaluation",
        "@com_google_absl//absl/algorithm:container",
        "@com_google_absl//absl/container:flat_hash_set",
        "@com_google_absl//absl/status",
        "@com_google_absl//absl/status:statusor",
        "@com_google_absl//absl/types:span",
    ],
)

cc_library(
    name = "compression",
    srcs = ["compression.cc"],
    hdrs = ["compression.h"],
    deps = [
        "//googlesql/base:check",
        "//googlesql/base:ret_check",
        "//googlesql/base:status",
        "//googlesql/public:options_cc_proto",
        "//googlesql/public:type_cc_proto",
        "//googlesql/public:value",
        "//googlesql/public/functions:compression",
        "//googlesql/public/types",
        "//googlesql/reference_impl:evaluation",
        "@com_google_absl//absl/status:statusor",
        "@com_google_absl//absl/strings:string_view",
        "@com_google_absl//absl/types:span",
    ],
)
