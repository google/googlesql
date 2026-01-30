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

def gen_resolved_ast_files(
        name,
        srcs = [],
        outs = [],
        data = [],
        flags = [],
        shard_count = 1,
        **extra_args):
    """Creates a rule to generate the ResolvedAST files.

    Args:
      name: name of the rule
      srcs: input top-level template files
      outs: output files, corresponding in order to srcs
      data: resource files, including shared logic imported from srcs
      flags: any flags to pass to the rule
      shard_count: The number of shards to split sharded output files (those with `{shard_num}` in
        their name) into. Any file with a name containing `{shard_num}` will have this placeholder
        filled with 1..shard_count, and the template will be expanded into that many output files.
      **extra_args: any extra args to pass to the rule
    """
    if len(srcs) != len(outs):
        fail("srcs and outs must have the same length")

    # Expand outs with `{shard_num}` in their name into multiple files.
    concrete_srcs = []
    concrete_outs = []
    concrete_shard_nums = []
    for i in range(0, len(srcs)):
        if "{shard_num}" in outs[i]:
            for shard_num in range(1, shard_count + 1):
                concrete_srcs.append(srcs[i])
                concrete_outs.append(outs[i].replace("{shard_num}", str(shard_num)))
                concrete_shard_nums.append(shard_num)
        else:
            concrete_srcs.append(srcs[i])
            concrete_outs.append(outs[i])
            concrete_shard_nums.append(1)
    cmd = "$(location //googlesql/resolved_ast:gen_resolved_ast) "
    for f in flags:
        cmd += f + " "
    cmd += ' --input_templates="'
    for t in concrete_srcs:
        cmd += "$(location %s) " % t
    cmd += '" --output_files="'
    for o in concrete_outs:
        cmd += "$(location %s) " % o
    cmd += '"'
    for s in concrete_shard_nums:
        cmd += " --shard_nums=" + str(s)
    cmd += " --shard_count=%d" % shard_count
    if len(data) > 0:
        cmd += ' --data_files="'
        for s in data:
            cmd += "$(location %s) " % s
        cmd += '"'
    native.genrule(
        name = name,
        srcs = srcs + data,
        outs = concrete_outs,
        cmd = cmd,
        tools = ["//googlesql/resolved_ast:gen_resolved_ast"],
        **extra_args
    )
