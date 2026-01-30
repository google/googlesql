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

"""Common routines shared by gen_parse_tree.py and gen_resolved_ast.py.
"""
import re


def Trim(s):
  """Remove double blank lines and trailing spaces from string."""
  s = re.sub(r'  *\n', r'\n', s)
  s = re.sub(r'\n\n\n*', r'\n\n', s)
  return s


def CleanIndent(text, prefix=''):
  """Remove extra indentation from comments or code.

  This allows code to be written as triple-quoted strings with natural
  indentation in the python file, and that indentation will be removed
  and replaced with the provided prefix.

  Args:
    text: Input code
    prefix: will be added to the start of each line.

  Returns:
    Code text with indentation regenerated.
  """
  if text:
    text = text.strip()
    lines = text.split('\n')

    def NumLeadingSpaces(line):
      num = 0
      for c in line:
        if c.isspace():
          num += 1
        else:
          break
      return num

    # Compute the indentation we will strip off.  Do it by looking at the
    # minimum indentation on non-empty lines after the first line.
    # (The first line will have already been stripped.)
    # This is assuming there is a uniform indentation on the left of the
    # comment that we should strip off, but there could be additional
    # indentation on some lines that we'd rather keep.
    non_empty_lines = [line for line in lines[1:] if line.strip()]
    if non_empty_lines:
      leading_spaces = min(NumLeadingSpaces(line) for line in non_empty_lines)
      strip_prefix = ' ' * leading_spaces
    else:
      strip_prefix = ''

    def Unindent(line):
      if line.startswith(strip_prefix):
        line = line[len(strip_prefix):]
      return line

    # Remove leading spaces from each line and add prefix.
    text = '\n'.join(prefix + Unindent(line.rstrip()) for line in lines)
  return text


class ScalarType(object):
  """Class used for scalar types as Field ctype parameters."""

  def __init__(self,
               ctype,
               proto_type=None,
               java_type=None,
               java_reference_type=None,
               passed_by_reference=False,
               has_proto_setter=False,
               is_enum=False,
               scoped_ctype=None,
               java_default=None,
               cpp_default=None,
               not_serialize_if_default=None,
               is_default_constructible=True):
    """Create a ScalarType.

    Args:
      ctype: C type name for this ScalarType
      proto_type: The proto field type name used to store this field.  If not
          set, this defaults to using the same name as ctype.
      java_type: Java type name for this ScalarType. Defaults to ctype.
      java_reference_type: Java type name when reference type is needed for this
                           ScalarType. Defaults to java_type.
      passed_by_reference: Specify whether this ScalarType should be passed by
          value or by reference in constructors and getter methods. Types that
          are really classes should be passed by reference. Real scalar types
          (PODs) should be passed by value.
      has_proto_setter: True if fields of this type have a set_X(value) method
          in C++. For example, enum, int64, string, fields do. Message fields
          don't. Always set to True if ctype == proto_type. Otherwise defaults
          to False.
      is_enum: True if this ScalarType represents an Enum normally persisted
          as integers in proto form. This is used to generate serialization
          logic that casts between the enum type and underlying int as
          necessary.
      scoped_ctype: C type, possibly with scope prepended as in the case of
          inner types.  Useful for locally declared enums that need to be
          referenced externally to that class.
          If not set, this defaults to using the same name as ctype.
      java_default: Non-Constructor args and optional constructor args require a
          default value. While java field defaults match c++ (for PODS), it's
          best practice to initialize them explicitly.
      cpp_default: Non-Constructor args and optional constructor args require a
          default value. This value could be set using this argument, otherwise
          C++ default value is used.
      not_serialize_if_default: Do not serialize this field when its value is in
          the default value, and set to default value during deserialization
          when its proto field is empty.
      is_default_constructible: The field has no default constructor. We store
          such fields as `absl::optional`. This way, builders have a way to
          track whether a value has been set.
    """
    self.ctype = ctype
    self.is_enum = is_enum
    self.passed_by_reference = passed_by_reference
    self.is_default_constructible = is_default_constructible
    if java_type is None:
      self.java_type = ctype
    else:
      self.java_type = java_type
    if java_reference_type is None:
      self.java_reference_type = self.java_type
    else:
      self.java_reference_type = java_reference_type
    if proto_type is None:
      self.proto_type = ctype
      self.has_proto_setter = True
    else:
      self.proto_type = proto_type
      self.has_proto_setter = has_proto_setter
    if scoped_ctype is None:
      self.scoped_ctype = ctype
    else:
      self.scoped_ctype = scoped_ctype
    self.java_default = java_default
    if cpp_default is None:
      self.cpp_default = ''
    else:
      self.cpp_default = cpp_default
    if not_serialize_if_default is None:
      self.not_serialize_if_default = False
    else:
      self.not_serialize_if_default = not_serialize_if_default


def JavaDoc(text, indent=0):
  """Returns text for a JavaDoc comment from the given text.

  Args:
    text: comment text
    indent: indent level

  It will be indented by the specified number of spaces.
  """
  if not text:
    return text
  indent_text = ' ' * indent
  content = CleanIndent(text, '%s * ' % indent_text)

  # Prefix <p> to lines that start a new paragraph. The regex finds lines that
  # follow an empty line.
  add_paragraph_re = re.compile(r'\* \n( *\* )(\S)')
  content = add_paragraph_re.sub(r'* \n\1<p> \2', content)

  # Add the leading line (/**) and trailing line (*/)
  return '%s/**\n%s\n%s */' % (indent_text, content, indent_text)


def LowerCamelCase(value):
  """Turns some_string or SOME_STRING into someString."""
  split_value = value.lower().split('_')
  result = [split_value[0]] + [part.capitalize() for part in split_value[1:]]
  return ''.join(result)


def UpperCamelCase(value):
  """Turns some_string or SOME_STRING into SomeString."""
  split_value = value.lower().split('_')
  return ''.join([part.capitalize() for part in split_value])


def NameToNodeKindName(name, prefix):
  """Returns the name of a node kind, suitable for a tree dump.

  This is simply the name, minus the "Resolved" or "AST" prefix (if present).
  Args:
    name: name of node class (in Java/C++).
    prefix: prefix to remove.
  """
  if name.startswith(prefix):
    return name[len(prefix):]
  else:
    return name
