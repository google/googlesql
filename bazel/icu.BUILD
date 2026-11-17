# Copyright 2025 Google LLC
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


"""
Rules for adding './configure && make' style dependencies.
"""

load("@rules_cc//cc:cc_library.bzl", "cc_library")
load("@rules_foreign_cc//foreign_cc:configure.bzl", "configure_make")

licenses(["notice"])  # Apache v2.0

package(
    default_visibility = ["//visibility:public"],
)

# We need to label this for configure_make.
filegroup(
    name = "all",
    srcs = glob(["**"]),
)

configure_make(
    name = "icu",
    configure_command = "source/configure",
    args = select({
        # AR is overridden to be libtool by rules_foreign_cc. It does not support ar style arguments
        # like "r". We need to prevent the icu make rules from adding unsupported parameters by
        # forcing ARFLAGS to keep the rules_foreign_cc value in this parameter
        "@platforms//os:macos": [
            "ARFLAGS=\"-static -o\"",
            "MAKE=gnumake",
            "-j32"
        ],
        "//conditions:default": [
            "-j32"
        ],
    }),
    env = {
        "CXXFLAGS": "-fPIC",  # For JNI
        "CFLAGS": "-fPIC",  # For JNI
    },
    configure_options = [
        "--enable-option-checking",
        "--enable-static",
        "--enable-tools",  # needed to build data
        "--disable-shared",
        "--disable-dyload",
        "--disable-extras",
        "--disable-plugins",
        "--disable-tests",
        "--disable-samples",
        "--with-data-packaging=static",
    ],
    lib_source = "@icu//:all",
    out_static_libs = [
        "libicui18n.a",
        "libicuio.a",
        "libicuuc.a",
        "libicudata.a",
    ],
)

cc_library(
    name = "common",
    deps = [
        "icu",
    ],
)

cc_library(
    name = "headers",
    deps = [
        "icu",
    ],
)

cc_library(
    name = "unicode",
    deps = [
        "icu",
    ],
)

exports_files([
    "LICENSE",
])
