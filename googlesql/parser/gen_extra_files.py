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

"""A simple tool to generate associated code from parse_tree.h.

Generates the following files from parse_tree.h:
parse_tree_visitor.h
parse_tree_decls.h
parse_tree_accept_methods.inc
"""

import re
import sys
import textwrap


def GetClasses(input_filename):
  """Computes the set of classes for AST objects in the given file.

  Args:
    input_filename: The input file.

  Returns:
    A pair (concrete_classes, abstract_classes) where concrete_classes is a list
    of all final (concrete) class names in the input file, and abstract_classes
    is a list of non-final class names.
  """
  concrete_classes = []
  abstract_classes = ['ASTNode']
  for input_line in open(input_filename):
    m = re.search('^class (AST[a-zA-Z]*) final : public', input_line)
    if m:
      concrete_classes.append(m.group(1))
    m = re.search('^class (AST[a-zA-Z]*) : public', input_line)
    if m:
      abstract_classes.append(m.group(1))
  return (concrete_classes, abstract_classes)


def GenerateParseTreeVisitor(concrete_classes):
  """Generates parse_tree_visitor.h contents containing ParseTreeVisitor class.

  Args:
    concrete_classes: a list of classes for which to generate visit methods

  Yields:
    A string part of the output code.
  """
  yield textwrap.dedent('''\
      #ifndef STORAGE_GOOGLESQL_PARSER_PARSE_TREE_VISITOR_H_
      #define STORAGE_GOOGLESQL_PARSER_PARSE_TREE_VISITOR_H_
      #include "googlesql/parser/parse_tree.h"
      #include "googlesql/parser/visit_result.h"

      namespace googlesql {
      class ParseTreeVisitor {
       public:
        virtual ~ParseTreeVisitor() {}
        virtual void visit(const ASTNode *node, void* data) = 0;
      ''')
  for cls in concrete_classes:
    yield ('  virtual void visit{0}(const {0}* node, void* data) = 0;\n\n'
           .format(cls))
  yield textwrap.dedent('''\
      };

      class DefaultParseTreeVisitor : public ParseTreeVisitor {
       public:
        virtual void defaultVisit(const ASTNode* node, void* data) = 0;
        void visit(const ASTNode* node, void* data) override {
          defaultVisit(node, data);
        }
      ''')
  for cls in concrete_classes:
    yield (
        '  void visit{0}(const {0}* node, void* data) override {{\n' +  #
        '    defaultVisit(node, data);\n' +  #
        '  }}\n' +  #
        '\n').format(cls)
  yield """\
      };

      class ParseTreeStatusVisitor {
       public:
        virtual ~ParseTreeStatusVisitor() {}
        virtual absl::Status Visit(const ASTNode* node, std::any& output) = 0;
      """
  for cls in concrete_classes:
    yield (("""\
        virtual absl::Status Visit{0}(const {0}* node, std::any& output) = 0;
        """).format(cls))
  yield """\
      };

      class DefaultParseTreeStatusVisitor : public ParseTreeStatusVisitor {
       public:
        virtual absl::Status DefaultVisit(const ASTNode* node, std::any& output) {
          return node->ChildrenAccept(*this, output);
        }
        absl::Status Visit(const ASTNode* node, std::any& output) override {
          return DefaultVisit(node, output);
        }
      """
  for cls in concrete_classes:
    yield """\

        absl::Status Visit{0}(const {0}* node, std::any& output) override {{
          return DefaultVisit(node, output);
        }}""".format(cls)
  yield textwrap.dedent('''\
      };

      class NonRecursiveParseTreeVisitor {
       public:
        virtual ~NonRecursiveParseTreeVisitor() {}
        virtual absl::StatusOr<VisitResult> defaultVisit(const ASTNode* node) = 0;
        absl::StatusOr<VisitResult> visit(const ASTNode* node) {
          return defaultVisit(node);
        }
      ''')
  for cls in concrete_classes:
    yield (('  virtual absl::StatusOr<VisitResult> visit{0}(const {0}* node) ' +
            '{{return defaultVisit(node);}};\n\n').format(cls))
  yield textwrap.dedent('''\
      };
      }  // namespace googlesql
      #endif  // STORAGE_GOOGLESQL_PARSER_PARSE_TREE_VISITOR_H_
      ''')


def GenerateParseTreeDecls(concrete_classes,
                           abstract_classes):
  """Generates parse_tree_decls.h contents containing forward declarations.

  Args:
    concrete_classes: a list of classes for which to generate declarations
    abstract_classes: a list of classes for which to generate declarations

  Yields:
    A string part of the output code.
  """
  yield textwrap.dedent('''\
      #ifndef STORAGE_GOOGLESQL_PARSER_PARSE_TREE_DECLS_H
      #define STORAGE_GOOGLESQL_PARSER_PARSE_TREE_DECLS_H
      namespace googlesql {
      ''')
  for cls in abstract_classes + concrete_classes:
    yield 'class {0};\n'.format(cls)
  yield textwrap.dedent('''\

      }  // namespace googlesql
      #endif  // STORAGE_GOOGLESQL_PARSER_PARSE_TREE_DECLS_H
      ''')


def GeneerateParseTreeAcceptMethods(
    concrete_classes):
  """Generates parse_tree_accept_methods.inc contents containing Accept methods.

  Args:
    concrete_classes: a list of classes for which to generate Accept methods

  Yields:
    A string part of the output code.
  """
  yield textwrap.dedent('''\
      #include "googlesql/parser/parse_tree.h"
      namespace googlesql {
      ''')
  for cls in concrete_classes:
    yield textwrap.dedent('''\
        void {0}::Accept(ParseTreeVisitor* visitor, void* data) const {{
          visitor->visit{0}(this, data);
        }}

        ''').format(cls)

  for cls in concrete_classes:
    yield """\
        absl::Status {0}::Accept(ParseTreeStatusVisitor& visitor, std::any& output) const {{
          return visitor.Visit{0}(this, output);
        }}

        """.format(cls)

  for cls in concrete_classes:
    yield textwrap.dedent('''\
        absl::StatusOr<VisitResult> {0}::Accept(NonRecursiveParseTreeVisitor* visitor) const {{
          return visitor->visit{0}(this);
        }}

        ''').format(cls)
  yield '}  // namespace googlesql\n'


def ToFile(output_filename, data):
  """Writes a sequence of strings to a file.

  Args:
    output_filename: the name (and path) of the file to write.
    data: a sequence of strings to write to the file.
  """
  with open(output_filename, 'w') as output:
    for chunk in data:
      output.write(chunk)


def main(argv):
  if len(argv) != 5:
    raise Exception(
        'Usage: %s <input/path/to/parse_tree_generated.h> <output/path/to/parse_tree_visitor.h> <output/path/to/parse_tree_decls.h> <output/path/to/parse_tree_accept_methods.inc>'
    )

  (concrete_classes, abstract_classes) = GetClasses(argv[1])
  ToFile(argv[2], GenerateParseTreeVisitor(concrete_classes))
  ToFile(argv[3], GenerateParseTreeDecls(concrete_classes, abstract_classes))
  ToFile(argv[4], GeneerateParseTreeAcceptMethods(concrete_classes))


if __name__ == '__main__':
  main(sys.argv)
