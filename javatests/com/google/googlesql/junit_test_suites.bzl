#
# Copyright 2018 Google LLC
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
""" Rule for junit suite generation """

load("@rules_java//java:defs.bzl", "java_test")

def _package_from_path(package_path):
    package = "com.google.googlesql"
    if not package_path.startswith("javatests/com/google/googlesql"):
        fail("Not in javatests", "package_path")
    return package + package_path[30:].replace("/", ".")

def junit_test_suites(
        name,
        deps = None,
        runtime_deps = None):
    """ Generate junit suite from input """

    if len(deps) != 1:
        fail("Only one deps value supported", "deps")
    if deps[0][0] != ":":
        fail("Dep must be in same package", "deps")

    if runtime_deps == None:
        runtime_deps = []

    package = _package_from_path(native.package_name())

    tests = []
    src = native.existing_rule(deps[0][1:])
    for f in src["srcs"]:
        if f[0] != ":" or not f.endswith(".java"):
            fail("Invalid input %s" % f, "deps")
        f = f[1:-5]

        test_name = name + "_" + f
        tests.append(test_name)
        java_test(
            name = test_name,
            test_class = package + "." + f,
            runtime_deps = deps + runtime_deps,
        )
    native.test_suite(
        name = name,
        tests = tests,
    )
