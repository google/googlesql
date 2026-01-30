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

"""Module extension for GoogleSQL http_archive dependencies."""

load("@bazel_tools//tools/build_defs/repo:http.bzl", "http_archive")

def _http_archive_deps_impl(_):
    http_archive(
        name = "com_google_differential_privacy",
        sha256 = "1a8612290740bd0bacc7052df7accb4f3368e2c2cb3de8eb608f58522c2e1891",
        strip_prefix = "differential-privacy-4.0.0",
        url = "https://github.com/google/differential-privacy/archive/refs/tags/v4.0.0.tar.gz",
    )

    http_archive(
        name = "com_google_cc_differential_privacy",
        sha256 = "1a8612290740bd0bacc7052df7accb4f3368e2c2cb3de8eb608f58522c2e1891",
        strip_prefix = "differential-privacy-4.0.0/cc",
        url = "https://github.com/google/differential-privacy/archive/refs/tags/v4.0.0.tar.gz",
    )

    http_archive(
        name = "com_google_farmhash",
        build_file = "//bazel:farmhash.BUILD",
        sha256 = "6560547c63e4af82b0f202cb710ceabb3f21347a4b996db565a411da5b17aba0",
        strip_prefix = "farmhash-816a4ae622e964763ca0862d9dbd19324a1eaf45",
        url = "https://github.com/google/farmhash/archive/816a4ae622e964763ca0862d9dbd19324a1eaf45.tar.gz",
    )

    http_archive(
        name = "org_publicsuffix",
        build_file_content = "licenses([\"reciprocal\"])\n\nexports_files([\n    \"LICENSE\",\n    \"public_suffix_list.dat\",\n    \"tests/test_psl.txt\",\n    ],\n    visibility = [\"//visibility:public\"]\n)\nalias(\n    name = \"test_psl.txt\",\n    actual = \"tests/test_psl.txt\",\n    visibility = [\"//visibility:public\"]\n)\n",
        sha256 = "2f84929af28e2b712a235ab544fbb4dd7bd5d075ac351de0723915e528c99a38",
        strip_prefix = "list-d111481d5931f704c1d9d3a50af19e4e34fc5ba3",
        urls = [
            "https://github.com/publicsuffix/list/archive/d111481d5931f704c1d9d3a50af19e4e34fc5ba3.zip",
        ],
    )

    http_archive(
        name = "com_google_file_based_test_driver",
        sha256 = "b564acb6f083ce6e91fc2734bdad259cc4edf1a95766f93750a16784ff86218a",
        strip_prefix = "file-based-test-driver-fd7661b168f640f68da39f97dad26e426eb6c339",
        url = "https://github.com/google/file-based-test-driver/archive/fd7661b168f640f68da39f97dad26e426eb6c339.tar.gz",
    )

    http_archive(
        name = "mstch",
        build_file = "//bazel:mstch.BUILD",
        sha256 = "a06980c2031cd9222b6356a2f3674064c6aa923c25a15a8acf2652769f3e6628",
        strip_prefix = "mstch-1.0.2",
        urls = [
            "https://github.com/no1msd/mstch/archive/1.0.2.zip",
        ],
    )

    http_archive(
        name = "native_utils",
        build_file_content = "licenses([\"notice\"]) # MIT\njava_library(\nname = \"native_utils\",\nvisibility = [\"//visibility:public\"],\nsrcs = glob([\"src/main/java/cz/adamh/utils/*.java\"]),\n)",
        sha256 = "6013c0988ba40600e238e47088580fd562dcecd4afd3fcf26130efe7cb1620de",
        strip_prefix = "native-utils-e6a39489662846a77504634b6fafa4995ede3b1d",
        url = "https://github.com/adamheinrich/native-utils/archive/e6a39489662846a77504634b6fafa4995ede3b1d.tar.gz",
    )

    http_archive(
        name = "icu",
        build_file = "//bazel:icu.BUILD",
        sha256 = "dfacb46bfe4747410472ce3e1144bf28a102feeaa4e3875bac9b4c6cf30f4f3e",
        strip_prefix = "icu",
        urls = ["https://github.com/unicode-org/icu/releases/download/release-76-1/icu4c-76_1-src.tgz"],
        patch_cmds = [
            "find source -name BUILD.bazel -delete",
        ],
    )

googlesql_http_archive_deps = module_extension(implementation = _http_archive_deps_impl)
