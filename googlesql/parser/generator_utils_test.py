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

from googlesql.parser.generator_utils import CleanIndent
from googlesql.parser.generator_utils import JavaDoc
from googlesql.parser.generator_utils import LowerCamelCase
from googlesql.parser.generator_utils import NameToNodeKindName
from googlesql.parser.generator_utils import Trim
from googlesql.parser.generator_utils import UpperCamelCase
from absl.testing import absltest


class GeneratorUtilsTest(absltest.TestCase):

  long_comment = """
    First line of comment, often rather short.

    Sometimes there is a much longer, rambling, multiline continuation which
    has a lot more detail.
      """

  def test_trim(self):
    untrimmed = '\n\n\nbrevity   \n\n\nis the soul \n\nof wit\n\n\n\n\n'
    expected = '\n\nbrevity\n\nis the soul\n\nof wit\n\n'
    self.assertEqual(expected, Trim(untrimmed))

  def test_clean_indent(self):
    expected = """// First line of comment, often rather short.
//\u0020
// Sometimes there is a much longer, rambling, multiline continuation which
// has a lot more detail."""
    self.assertEqual(expected, CleanIndent(self.long_comment, prefix='// '))

  def test_javadoc(self):
    expected = """  /**
   * First line of comment, often rather short.
   *\u0020
   * <p> Sometimes there is a much longer, rambling, multiline continuation which
   * has a lot more detail.
   */"""
    self.assertEqual(expected, JavaDoc(self.long_comment, 2))

  def test_lower_camel_case(self):
    self.assertEqual('larryCurlyMoe', LowerCamelCase('LARRY_CURLY_MOE'))
    self.assertEqual('larryCurlyMoe', LowerCamelCase('larry_curly_moe'))

  def test_upper_camel_case(self):
    self.assertEqual('LarryCurlyMoe', UpperCamelCase('LARRY_CURLY_MOE'))
    self.assertEqual('LarryCurlyMoe', UpperCamelCase('larry_curly_moe'))

  def test_name_to_node_kind_name(self):
    self.assertEqual('Foo', NameToNodeKindName('ASTFoo', 'AST'))
    self.assertEqual('Bar', NameToNodeKindName('Bar', 'AST'))

if __name__ == '__main__':
  absltest.main()
