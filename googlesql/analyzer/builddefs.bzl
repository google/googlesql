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

""" Macros for the creation of analyzer test targets """

load("@rules_cc//cc:cc_test.bzl", "cc_test")

def gen_analyzer_test(filename):
    """ Create an analyzer cc_test using queries in testdata/<filename>.  """
    name = "analyzer_" + filename.replace(".", "_")
    datafile = "testdata/" + filename
    cc_test(
        name = name,
        size = "medium",
        data = [datafile],
        deps = [
            ":run_analyzer_test_lib",
            "//googlesql/base/testing:googlesql_gtest_main",
        ],
        args = ["--test_file=$(location " + datafile + ")"],
    )
