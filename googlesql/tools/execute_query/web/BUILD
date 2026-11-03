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
load("//bazel:ts_build_defs.bzl", "js_binary", "ts_library")
load(":embed_files.bzl", "gen_string_constant_header_file")

package(default_visibility = ["//googlesql/base:googlesql_implementation"])

gen_string_constant_header_file(
    name = "page_template_h",
    src = "page_template.html",
    variable = "kPageTemplate",
)

ts_library(
    name = "decorate_tree",
    srcs = ["decorate_tree.ts"],
)

ts_library(
    name = "local_storage_manager",
    srcs = ["local_storage_manager.ts"],
)

ts_library(
    name = "history",
    srcs = ["history.ts"],
    deps = [":local_storage_manager"],
)

ts_library(
    name = "history_dialog",
    srcs = ["history_dialog.ts"],
    deps = [":history"],
)

ts_library(
    name = "splitter",
    srcs = ["splitter.ts"],
    deps = [":local_storage_manager"],
)

ts_library(
    name = "theme",
    srcs = ["theme.ts"],
    deps = [":local_storage_manager"],
)

ts_library(
    name = "tree_diff",
    srcs = ["tree_diff.ts"],
)

ts_library(
    name = "rewrite_steps",
    srcs = ["rewrite_steps.ts"],
    deps = [":tree_diff"],
)

ts_library(
    name = "init",
    srcs = ["init.ts"],
    deps = [
        ":decorate_tree",
        ":history",
        ":history_dialog",
        ":local_storage_manager",
        ":rewrite_steps",
        ":splitter",
        ":theme",
    ],
)

js_binary(
    name = "inline_bundle",
    entry_point = "init.ts",
    deps = [
        ":init",
    ],
)

gen_string_constant_header_file(
    name = "inline_bundle_js_h",
    src = "inline_bundle.js",
    variable = "kInlineBundleJS",
)

gen_string_constant_header_file(
    name = "page_body_h",
    src = "page_body.html",
    variable = "kPageBody",
)

gen_string_constant_header_file(
    name = "history_dialog_h",
    src = "history_dialog.html",
    variable = "kHistoryDialog",
)

gen_string_constant_header_file(
    name = "table_h",
    src = "table.html",
    variable = "kTable",
)

gen_string_constant_header_file(
    name = "style_css_h",
    src = "style.css",
    variable = "kStyleCSS",
)

cc_library(
    name = "embedded_resources",
    srcs = [
        "embedded_resources.cc",
        "history_dialog.html.h",
        "inline_bundle.js.h",
        "page_body.html.h",
        "page_template.html.h",
        "style.css.h",
        "table.html.h",
    ],
    hdrs = ["embedded_resources.h"],
)

bzl_library(
    name = "embed_files_bzl",
    srcs = ["embed_files.bzl"],
)
