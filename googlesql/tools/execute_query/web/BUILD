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
load(":embed_files.bzl", "gen_string_constant_header_file")

package(default_visibility = ["//googlesql/base:googlesql_implementation"])

gen_string_constant_header_file(
    name = "page_template_h",
    src = "page_template.html",
    variable = "kPageTemplate",
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
