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

cc_library(
    name = "quoting",
    srcs = ["quoting.cc"],
    hdrs = ["quoting.h"],
    deps = [
        "//googlesql/base:check",
        "//googlesql/base:ret_check",
        "//googlesql/base:status",
        "@com_google_absl//absl/status:statusor",
        "@com_google_absl//absl/strings",
        "@com_google_absl//absl/strings:str_format",
    ],
)

cc_test(
    name = "quoting_test",
    srcs = ["quoting_test.cc"],
    deps = [
        ":quoting",
        "//googlesql/base/testing:googlesql_gtest_main",
        "//googlesql/base/testing:status_matchers",
        "@com_google_absl//absl/status:statusor",
        "@com_google_absl//absl/strings",
        "@com_google_absl//absl/strings:str_format",
    ],
)

cc_library(
    name = "macro_catalog",
    srcs = ["macro_catalog.cc"],
    hdrs = ["macro_catalog.h"],
    visibility = [
        "//googlesql/parser:__pkg__",
        "//googlesql/tools/execute_query:__pkg__",
    ],
    deps = [
        "//googlesql/public:parse_location",
        "@com_google_absl//absl/container:node_hash_map",
        "@com_google_absl//absl/status",
        "@com_google_absl//absl/strings",
    ],
)

cc_test(
    name = "macro_catalog_test",
    srcs = ["macro_catalog_test.cc"],
    deps = [
        ":macro_catalog",
        "//googlesql/base:check",
        "//googlesql/base:status",
        "//googlesql/base/testing:googlesql_gtest_main",
        "//googlesql/base/testing:status_matchers",
        "//googlesql/parser",
        "//googlesql/parser:parser_mode",
        "//googlesql/public:language_options",
        "@com_google_absl//absl/status",
        "@com_google_absl//absl/strings",
    ],
)

cc_library(
    name = "token_provider_base",
    hdrs = ["token_provider_base.h"],
    deps = [
        "//googlesql/base:status",
        "//googlesql/parser:token_with_location",
        "@com_google_absl//absl/status:statusor",
        "@com_google_absl//absl/strings",
    ],
)

cc_library(
    name = "token_provider",
    srcs = ["token_provider.cc"],
    hdrs = ["token_provider.h"],
    visibility = [
        "//googlesql/parser:__pkg__",
        "//googlesql/tools/execute_query:__pkg__",
    ],
    deps = [
        ":token_provider_base",
        "//googlesql/base:check",
        "//googlesql/base:status",
        "//googlesql/parser:token",
        "//googlesql/parser:token_with_location",
        "//googlesql/parser:tokenizer",
        "//googlesql/public:parse_location",
        "@com_google_absl//absl/status:statusor",
        "@com_google_absl//absl/strings",
    ],
)

cc_test(
    name = "token_provider_test",
    srcs = ["token_provider_test.cc"],
    deps = [
        ":token_provider",
        "//googlesql/base/testing:googlesql_gtest_main",
        "//googlesql/base/testing:status_matchers",
        "//googlesql/parser:lookahead_transformer",  # build_cleaner: keep
        "//googlesql/parser:token",
        "//googlesql/parser:token_with_location",
        "//googlesql/public:parse_location",
        "@com_google_absl//absl/strings",
        "@com_google_absl//absl/strings:str_format",
    ],
)

cc_library(
    name = "token_splicing_utils",
    srcs = ["token_splicing_utils.cc"],
    hdrs = ["token_splicing_utils.h"],
    deps = [
        "//googlesql/parser:keywords",
        "//googlesql/parser:token",
        "//googlesql/parser:token_with_location",
        "@com_google_absl//absl/strings",
    ],
)

cc_library(
    name = "macro_expander",
    srcs = [
        "macro_expander.cc",
    ],
    hdrs = [
        "macro_expander.h",
    ],
    visibility = [
        "//googlesql/parser:__pkg__",
        "//googlesql/tools/execute_query:__pkg__",
    ],
    deps = [
        ":diagnostic",
        ":macro_catalog",
        ":quoting",
        ":standalone_macro_expansion",
        ":token_provider_base",
        ":token_splicing_utils",
        "//googlesql/base:arena",
        "//googlesql/base:arena_allocator",
        "//googlesql/base:check",
        "//googlesql/base:ret_check",
        "//googlesql/base:status",
        "//googlesql/base:strings",
        "//googlesql/common:thread_stack",
        "//googlesql/parser:token",
        "//googlesql/parser:token_with_location",
        "//googlesql/proto:internal_error_location_cc_proto",
        "//googlesql/public:error_helpers",
        "//googlesql/public:error_location_cc_proto",
        "//googlesql/public:parse_location",
        "@com_google_absl//absl/base:nullability",
        "@com_google_absl//absl/container:btree",
        "@com_google_absl//absl/container:flat_hash_set",
        "@com_google_absl//absl/memory",
        "@com_google_absl//absl/status",
        "@com_google_absl//absl/status:statusor",
        "@com_google_absl//absl/strings",
        "@com_google_absl//absl/strings:str_format",
    ],
)

cc_test(
    name = "macro_expander_test",
    size = "small",
    srcs = ["macro_expander_test.cc"],
    deps = [
        ":diagnostic",
        ":macro_catalog",
        ":macro_expander",
        ":quoting",
        ":standalone_macro_expansion",
        ":token_provider",
        "//googlesql/base:arena",
        "//googlesql/base/testing:googlesql_gtest_main",
        "//googlesql/base/testing:status_matchers",
        "//googlesql/parser",
        "//googlesql/parser:lookahead_transformer",  # build_cleaner: keep
        "//googlesql/parser:parser_mode",
        "//googlesql/parser:token",
        "//googlesql/parser:token_with_location",
        "//googlesql/public:error_location_cc_proto",
        "//googlesql/public:language_options",
        "//googlesql/public:options_cc_proto",
        "//googlesql/public:parse_location",
        "//googlesql/public:parse_resume_location",
        "@com_google_absl//absl/base:nullability",
        "@com_google_absl//absl/status",
        "@com_google_absl//absl/status:statusor",
        "@com_google_absl//absl/strings",
        "@com_google_absl//absl/strings:str_format",
    ],
)

cc_library(
    name = "standalone_macro_expansion",
    srcs = ["standalone_macro_expansion.cc"],
    hdrs = ["standalone_macro_expansion.h"],
    visibility = [
        "//googlesql/parser:__pkg__",
        "//googlesql/tools/execute_query:__pkg__",
    ],
    deps = [
        ":token_splicing_utils",
        "//googlesql/base:check",
        "//googlesql/parser:token",
        "//googlesql/parser:token_with_location",
        "@com_google_absl//absl/base:core_headers",
        "@com_google_absl//absl/container:btree",
        "@com_google_absl//absl/strings",
        "@com_google_absl//absl/types:span",
    ],
)

cc_test(
    name = "standalone_macro_expansion_test",
    srcs = ["standalone_macro_expansion_test.cc"],
    deps = [
        ":standalone_macro_expansion",
        "//googlesql/base/testing:googlesql_gtest_main",
        "//googlesql/parser:token",
        "//googlesql/parser:token_with_location",
        "//googlesql/public:parse_location",
        "@com_google_absl//absl/container:btree",
        "@com_google_absl//absl/strings",
    ],
)

cc_library(
    name = "standalone_macro_expansion_impl",
    hdrs = ["standalone_macro_expansion_impl.h"],
    visibility = [
        "//googlesql/parser:__pkg__",
        "//googlesql/tools/execute_query:__pkg__",
    ],
    deps = [
        ":macro_catalog",
        ":macro_expander",
        ":token_provider",
        "//googlesql/public:options_cc_proto",
        "@com_google_absl//absl/status:statusor",
        "@com_google_absl//absl/strings",
    ],
)

cc_library(
    name = "diagnostic",
    srcs = ["diagnostic.cc"],
    hdrs = ["diagnostic.h"],
    deps = [
        "//googlesql/base:status",
        "//googlesql/common:errors",
        "//googlesql/parser:token_with_location",
        "//googlesql/public:error_helpers",
        "//googlesql/public:parse_location",
        "@com_google_absl//absl/base:nullability",
        "@com_google_absl//absl/status",
        "@com_google_absl//absl/status:statusor",
        "@com_google_absl//absl/strings",
        "@com_google_absl//absl/strings:string_view",
    ],
)
