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

"""Macros to simplify generating maven files.
"""

load("@google_bazel_common//tools/maven:pom_file.bzl", default_pom_file = "pom_file")

def pom_file(name, targets, artifact_name, artifact_id, excluded_artifacts = None, **kwargs):
    if excluded_artifacts == None:
        excluded_artifacts = []
    excluded_artifacts.append("com.google.googlesql:{}".format(artifact_id))

    default_pom_file(
        name = name,
        targets = targets,
        preferred_group_ids = [
            "com.google.googlesql",
            "com.google",
        ],
        template_file = "//tools:pom-template.xml",
        substitutions = {
            "{artifact_name}": artifact_name,
            "{artifact_id}": artifact_id,
        },
        excluded_artifacts = excluded_artifacts,
        **kwargs
    )
