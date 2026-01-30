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

""" Defines blaze extensions for use testing googlesql engines. """

load("@rules_cc//cc:cc_binary.bzl", "cc_binary")
load("@rules_cc//cc:cc_test.bzl", "cc_test")

# Invoke the GoogleSQL compliance test suite against a SQL engine.
#
# Usage:
#
#   load("//googlesql/compliance:builddefs.bzl", "googlesql_compliance_test")
#
#   googlesql_compliance_test(
#      name = "my_compliance_test",
#      size = "small",
#      known_error_files = ["//my/full/path:engine_known_error.textproto"],
#      deps = [":my_test_driver"],
#   )
#
# The deps should include a cc_library that subclases and implements
# //googlesql/compliance:test_driver, and implements
# GetComplianceTestDriver() to return an instance of the test driver.
#
# One or more known error files can be provided to exclude tests. See
# https://github.com/google/googlesql/blob/master/googlesql/compliance/known_errors.proto for more background.
#
# Any other standard arguments to cc_test can be used here and will be
# forward to the underlying cc_test rules for the test suite.
#
# Generates an additional build target to run the driver against a standalone query
# specified on the command-line. This is useful for repro'ing test failures.
#
# See run_compliance_driver.cc for details on the commandline syntax accepted by this binary.
#
def googlesql_compliance_test(
        name,
        deps = [],
        args = [],
        include_gtest_main = True,
        driver_exec_properties = None,
        tags = [],
        **extra_args):
    """Invoke the GoogleSQL compliance test suite against a SQL engine."""

    orig_deps = deps
    if include_gtest_main:
        deps = deps + ["//googlesql/base/testing:googlesql_gtest_main"]

    sql_e2e_test(
        name = name,
        deps = deps + [
            "//googlesql/compliance:compliance_test_cases",
        ],
        args = args + [
            "--googlesql_reference_impl_validate_timestamp_precision",
        ],
        tags = tags,
        **extra_args
    )

    driver_deps = {d: True for d in orig_deps}
    driver_deps["//googlesql/compliance:test_driver"] = True

def sql_e2e_test(
        name,
        data = [],
        args = [],
        known_error_files = [],
        **extra_args):
    """Invoke a SQL end-to-end test suite against a SQL engine.

     Similar to cc_test, with an extra argument "known_error_files".

       load("//googlesql/compliance:builddefs.bzl", "sql_e2e_test")

       sql_e2e_test(
          ...
          known_error_files = ["//my/full/path:my_project_known_errors.textproto"],
          ...
       )

     One or more known_error files be provided to exclude tests.

    Args:
      name: as cc_test
      data: as cc_test
      args: as cc_test
      known_error_files: list of build targets to known_error_files. See
         https://github.com/google/googlesql/blob/master/googlesql/compliance/known_errors.proto
      **extra_args: as cc_test
    """

    known_error_fullpaths = ["$(rootpath %s)" % file for file in known_error_files]

    data = data + known_error_files
    args = args + ["--known_error_files=%s" % ",".join(known_error_fullpaths)]
    cc_test(
        name = name,
        data = data,
        args = args,
        **extra_args
    )
