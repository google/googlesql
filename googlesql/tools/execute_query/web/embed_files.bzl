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

"""Macro for generating a C++ header file with an embedded string constant.

Unfortunately, the `cc_embed_data` rule is not supported in bazel, so we need
to roll our own. Unlike cc_embed_data, this rule does not support embedding
multiple files.
"""

def gen_string_constant_header_file(
        name,
        src,
        variable):
    """Generates a header file with containing the contents of the file 'src'.

    Args:
      name: The name of the rule.
      src: The file to embed.
      variable: The name of the variable to generate.

    Example:

      gen_string_constant_header_file(
        name = "my_file_txt",
        src = "my_file.txt",
        variable = "kMyFileContents",
      )

    This will generate a header file named 'my_file.txt.h' containing a constant
    `googlesql::embedded_resources::kMyFileContents` of type
    `constexpr const char []`.

    Although the constant is defined in a header file, C++17 guarantees that
    there is only a single copy of kMyFileContents in a program ((broken link))
    """

    guard = "STORAGE_GOOGLESQL_TOOLS_EXECUTE_QUERY_WEB_%s_H" % \
            src.replace("-", "_").replace(".", "_").replace("/", "_").upper()

    preamble = """// This file was generated from {src}
#ifndef {guard}
#define {guard}

namespace googlesql {{
namespace embedded_resources {{
inline constexpr const char {variable}[] =""".format(
        guard = guard,
        src = src,
        variable = variable,
    )

    finale = """
}}  // namespace embedded_resources
}}  // namespace googlesql
#endif  // {guard}""".format(guard = guard)

    native.genrule(
        name = name,
        srcs = [src],
        outs = [src + ".h"],
        cmd = ("cat > '$@' <<END\n" + preamble + "\nEND\n" +
               "echo -n 'R\"embed(' >> '$@'; " +
               "cat '$<' >> '$@'; " +
               "echo ')embed\";' >> '$@'; " +
               "cat >> '$@' <<END\n" + finale + "\nEND"),
    )
