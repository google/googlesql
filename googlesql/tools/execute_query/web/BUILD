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
load("@rules_cc//cc:cc_library.bzl", "cc_library")
load(":embed_files.bzl", "gen_string_constant_header_file")

package(default_visibility = ["//googlesql/base:googlesql_implementation"])

gen_string_constant_header_file(
    name = "page_template_h",
    src = "page_template.html",
    variable = "kPageTemplate",
)

gen_string_constant_header_file(
    name = "local_storage_manager_js_h",
    src = "local_storage_manager.js",
    variable = "kLocalStorageManagerJS",
)

gen_string_constant_header_file(
    name = "rewrite_steps_js_h",
    src = "rewrite_steps.js",
    variable = "kRewriteStepsJS",
)

gen_string_constant_header_file(
    name = "splitter_js_h",
    src = "splitter.js",
    variable = "kSplitterJS",
)

gen_string_constant_header_file(
    name = "theme_js_h",
    src = "theme.js",
    variable = "kThemeJS",
)

gen_string_constant_header_file(
    name = "init_js_h",
    src = "init.js",
    variable = "kInitJS",
)

gen_string_constant_header_file(
    name = "page_body_h",
    src = "page_body.html",
    variable = "kPageBody",
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
        "init.js.h",
        "local_storage_manager.js.h",
        "page_body.html.h",
        "page_template.html.h",
        "rewrite_steps.js.h",
        "splitter.js.h",
        "style.css.h",
        "table.html.h",
        "theme.js.h",
    ],
    hdrs = ["embedded_resources.h"],
    deps = ["@com_google_absl//absl/strings"],
)

bzl_library(
    name = "embed_files_bzl",
    srcs = ["embed_files.bzl"],
)
