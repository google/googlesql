#
# Copyright 2020 Google LLC
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
"""Generates a C++ file with the contents of a file as a string_view.

./simple_embedder <input_file> <output_file> <symbol_namespace> <symbol_name>

The generated file is of the form:

#include "absl/strings/string_view.h"

namespace <symbol_namespace> {
constexpr absl::string_view <symbol_name> =
    R"simple_embedder(<contents>)simple_embedder";
}  // namespace _symbol_namespace_

"""
from absl import app


def main(argv):
  input_file = argv[1]
  output_file = argv[2]
  symbol_namespace = argv[3].encode('utf-8')
  symbol_name = argv[4].encode('utf-8')

  delim = b'simple_embedder'
  input_contents = open(input_file, 'rb').read()
  if input_contents.find(delim) != -1:
    raise 'input contains forbidden sequence %s' % delim

  with open(output_file, 'wb') as output:
    output.write(b'#include "absl/strings/string_view.h"\n')
    output.write(b'namespace ')
    output.write(symbol_namespace)
    output.write(b' {\nconstexpr absl::string_view ')
    output.write(symbol_name)
    output.write(b' =\n    R"')
    output.write(delim)
    output.write(b'(')
    output.write(input_contents)
    output.write(b')')
    output.write(delim)
    output.write(b'";\n}  // namespace ')
    output.write(symbol_namespace)
    output.write(b'\n')


if __name__ == '__main__':
  app.run(main)
