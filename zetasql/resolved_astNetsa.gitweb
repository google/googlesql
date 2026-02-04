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

r"""Generates c++ classes for the ZetaSQL resolved AST representation.

The classes and fields to generate are specified in main() in this python
file. The base class for the tree is hand-written in resolved_node.h. Code
generated files can be viewed at (broken link)
Documentation is generated in resolved_ast.html.
A viewable copy of the documentation is at (broken link).

Example (Note the double quotes around each set of files):
  gen_resolved_py \
    --input_templates="\
      zetasql/resolved_ast/resolved_ast.cc.template\
      zetasql/resolved_ast/resolved_ast.h.template"
    --output_files="\
      blaze-genfiles/zetasql/resolved_ast/resolved_ast.cc \
      blaze-genfiles/zetasql/resolved_ast/resolved_ast.h"

"""

import operator
import os.path
import re
import time

from absl import app
from absl import flags
from absl import logging
import jinja2
import markupsafe

from zetasql.parser.generator_utils import CleanIndent
from zetasql.parser.generator_utils import JavaDoc
from zetasql.parser.generator_utils import LowerCamelCase
from zetasql.parser.generator_utils import NameToNodeKindName
from zetasql.parser.generator_utils import ScalarType
from zetasql.parser.generator_utils import Trim
from zetasql.parser.generator_utils import UpperCamelCase
from zetasql.resolved_ast import resolved_ast_enums_pb2

FLAGS = flags.FLAGS

# Most template files allow single line jinja statements, for example:
#   # for x in list
#     ...
#   # endfor
#
# However, for markdown, this is challenging, because '#' is used extensively
# for section hierarchy:
#
#  # Page Title
#  ## Subheader
#  ...
#
# This flag allows the markdown generator to disable these single-line
# statements (it must use {% for x in list %} style jinja directives).
flags.DEFINE_boolean('allow_hash_prefix', True,
                     'Allows jinja statements starting with "# "')

flags.DEFINE_spaceseplist('input_templates', None,
                          'Space-separated list of templates')

flags.DEFINE_spaceseplist('output_files', None, 'Space-separated list of output'
                          ' files corresponding to the input templates in the'
                          ' same order.')

flags.DEFINE_spaceseplist('data_files', [], 'Space-separated list of data files'
                          ' that templates can reference and load')

# Enums indicating whether a field can be ignored without breaking semantics.
# If a field is not ignorable, and the user doesn't look at the field, then
# CheckFieldsAccessed() will return an error saying this query can't run
# because feature X is unimplemented.
NOT_IGNORABLE = 0  # Field can never be ignored. This is the default.
IGNORABLE = 1  # Field can always be ignored.
IGNORABLE_DEFAULT = 2  # Field can be ignored if it has its default value.
# Fields marked IGNORABLE_DEFAULT are optional to specify
# in Builder APIs, and are not shown in DebugString
# methods when they have the default value.

NODE_NAME_PREFIX = 'Resolved'
ROOT_NODE_NAME = 'ResolvedNode'

# Disabling "Wrong hanging indentation" lint warnings because Field arrays
# are not following correct indentation.
# pylint: disable=bad-continuation


SCALAR_TYPE = ScalarType(
    'const Type*', 'TypeProto', 'Type', java_default='null')
SCALAR_TYPE_PARAMETERS = ScalarType(
    'TypeParameters',
    'TypeParametersProto',
    java_default='null',
    passed_by_reference=True)
SCALAR_TYPE_MODIFIERS = ScalarType(
    'TypeModifiers',
    'TypeModifiersProto',
    java_default='null',
    passed_by_reference=True)
SCALAR_ANNOTATION_MAP = ScalarType(
    'const AnnotationMap*',
    'AnnotationMapProto',
    cpp_default='nullptr',
    java_type='AnnotationMap',
    java_default='null')
SCALAR_RESOLVED_COLLATION = ScalarType(
    'ResolvedCollation',
    'ResolvedCollationProto',
    java_default='null',
    passed_by_reference=True)
SCALAR_VALUE = ScalarType(
    'Value', 'ValueWithTypeProto', passed_by_reference=True)
SCALAR_TABLE = ScalarType('const Table*', 'TableRefProto', 'Table')
SCALAR_MODEL = ScalarType('const Model*', 'ModelRefProto', 'Model')
SCALAR_CONNECTION = ScalarType('const Connection*', 'ConnectionRefProto',
                               'Connection')
SCALAR_SEQUENCE = ScalarType('const Sequence*', 'SequenceRefProto', 'Sequence')
SCALAR_RESOLVED_COLUMN = ScalarType(
    'ResolvedColumn', 'ResolvedColumnProto', passed_by_reference=True)
SCALAR_CONSTANT = ScalarType('const Constant*', 'ConstantRefProto', 'Constant')
SCALAR_FUNCTION = ScalarType('const Function*', 'FunctionRefProto', 'Function')
SCALAR_FUNCTION_SIGNATURE = ScalarType(
    'FunctionSignature',
    'FunctionSignatureProto',
    passed_by_reference=True,
    # FunctionSignature does not have a default constructor, but builders need a
    # way to indicate when it is not yet set, without breaking the API.
    is_default_constructible=False)
SCALAR_FUNCTION_SIGNATURE_PTR = ScalarType(
    'std::shared_ptr<FunctionSignature>', 'FunctionSignatureProto',
    passed_by_reference=True,
    java_type='FunctionSignature',
    java_default='null',
    cpp_default='nullptr',
    not_serialize_if_default=True)
SCALAR_FUNCTION_CALL_INFO = ScalarType(
    'std::shared_ptr<ResolvedFunctionCallInfo>',
    'ResolvedFunctionCallInfoProto',
    passed_by_reference=True,
    java_type='ResolvedFunctionCallInfo',
    java_default='new ResolvedFunctionCallInfo()',
    cpp_default='std::make_shared<ResolvedFunctionCallInfo>()')
SCALAR_FIELD_DESCRIPTOR = ScalarType('const google::protobuf::FieldDescriptor*',
                                     'FieldDescriptorRefProto',
                                     'ZetaSQLFieldDescriptor')
SCALAR_ONEOF_DESCRIPTOR = ScalarType('const google::protobuf::OneofDescriptor*',
                                     'OneofDescriptorRefProto',
                                     'ZetaSQLOneofDescriptor')
SCALAR_ENUM_DESCRIPTOR = ScalarType('const google::protobuf::EnumDescriptor*',
                                    'EnumTypeProto', 'ZetaSQLEnumDescriptor')
SCALAR_FIELD_FORMAT = ScalarType(
    'FieldFormat::Format',
    'FieldFormat.Format',
    'FieldFormat.Format',
    has_proto_setter=True)

SCALAR_STRING = ScalarType(
    'std::string',
    proto_type='string',
    java_type='String',
    passed_by_reference=True,
    has_proto_setter=True,
    java_default='""')
SCALAR_BOOL = ScalarType(
    'bool',
    java_type='boolean',
    java_reference_type='Boolean',
    java_default='false')
SCALAR_INT = ScalarType(
    'int',
    'int64',
    java_type='long',
    java_reference_type='Long',
    has_proto_setter=True,
    java_default='0')
TABLE_VALUED_FUNCTION = ScalarType(
    'const TableValuedFunction*',
    'TableValuedFunctionRefProto',
    java_type='TableValuedFunction')
TVF_SIGNATURE = ScalarType(
    'std::shared_ptr<TVFSignature>',
    'TVFSignatureProto',
    passed_by_reference=True,
    java_type='TVFSignature')
SCALAR_PROCEDURE = ScalarType('const Procedure*', 'ProcedureRefProto',
                              'Procedure')
SCALAR_COLUMN = ScalarType('const Column*', 'ColumnRefProto', 'Column')


def EnumScalarType(enum_name, node_name, cpp_default=None, java_default=None):
  """Create a ScalarType for enums defined in resolved_ast_enums.proto.

  Args:
    enum_name: name of the enum.
    node_name: name of the ResolvedNode that this enum belongs to.
    cpp_default: the default c++ value of this enum.
    java_default: the default java value of this enum.

  Returns:
    The ScalarType.
  """
  return ScalarType(
      ctype='%s::%s' % (node_name, enum_name),
      java_type=enum_name,
      proto_type='%sEnums.%s' % (node_name, enum_name),
      has_proto_setter=True,
      is_enum=True,
      cpp_default=cpp_default,
      java_default=java_default,
  )


SCALAR_SUBQUERY_TYPE = EnumScalarType('SubqueryType', 'ResolvedSubqueryExpr')
SCALAR_JOIN_TYPE = EnumScalarType('JoinType', 'ResolvedJoinScan')
SCALAR_SET_OPERATION_TYPE = EnumScalarType('SetOperationType',
                                           'ResolvedSetOperationScan')
SCALAR_SET_OPERATION_COLUMN_MATCH_MODE = EnumScalarType(
    'SetOperationColumnMatchMode',
    'ResolvedSetOperationScan',
    cpp_default='BY_POSITION',
    java_default='SetOperationColumnMatchMode.BY_POSITION',
)
SCALAR_SET_OPERATION_COLUMN_PROPAGATION_MODE = EnumScalarType(
    'SetOperationColumnPropagationMode',
    'ResolvedSetOperationScan',
    cpp_default='STRICT',
    java_default='SetOperationColumnPropagationMode.STRICT',
)
SCALAR_RECURSIVE_SET_OPERATION_TYPE = EnumScalarType(
    'RecursiveSetOperationType', 'ResolvedRecursiveScan')
SCALAR_SAMPLE_UNIT = EnumScalarType('SampleUnit', 'ResolvedSampleScan')
SCALAR_NULL_ORDER_MODE = EnumScalarType('NullOrderMode', 'ResolvedOrderByItem')
SCALAR_CREATE_MODE = EnumScalarType('CreateMode', 'ResolvedCreateStatement')
SCALAR_CREATE_SCOPE = EnumScalarType('CreateScope', 'ResolvedCreateStatement')
SCALAR_READ_WRITE_TRANSACTION_MODE = EnumScalarType('ReadWriteMode',
                                                    'ResolvedBeginStmt')
SCALAR_FRAME_UNIT = EnumScalarType('FrameUnit', 'ResolvedWindowFrame')
SCALAR_BOUNDARY_TYPE = EnumScalarType('BoundaryType', 'ResolvedWindowFrameExpr')
SCALAR_INSERT_MODE = EnumScalarType('InsertMode', 'ResolvedInsertStmt')
SCALAR_MERGE_ACTION_TYPE = EnumScalarType('ActionType', 'ResolvedMergeWhen')
SCALAR_MERGE_MATCH_TYPE = EnumScalarType('MatchType', 'ResolvedMergeWhen')
SCALAR_ARGUMENT_KIND = EnumScalarType('ArgumentKind', 'ResolvedArgumentDef')
SCALAR_HAVING_MODIFIER_KIND = EnumScalarType('HavingModifierKind',
                                             'ResolvedAggregateHavingModifier')
SCALAR_NULL_HANDLING_MODIFIER_KIND = EnumScalarType(
    'NullHandlingModifier', 'ResolvedNonScalarFunctionCallBase')
SCALAR_IMPORT_KIND = EnumScalarType('ImportKind', 'ResolvedImportStmt')
SCALAR_ERROR_MODE = EnumScalarType('ErrorMode', 'ResolvedFunctionCallBase')
SCALAR_OBJECT_ACCESS = EnumScalarType('ObjectAccess', 'ResolvedStatement')
SCALAR_FOREIGN_KEY_MATCH_MODE = EnumScalarType(
    'MatchMode', 'ResolvedForeignKey')
SCALAR_FOREIGN_KEY_ACTION_OPERATION = EnumScalarType(
    'ActionOperation', 'ResolvedForeignKey')
SCALAR_SQL_SECURITY = EnumScalarType('SqlSecurity', 'ResolvedCreateStatement')
SCALAR_DETERMINISM_LEVEL = EnumScalarType('DeterminismLevel',
                                          'ResolvedCreateStatement')
SCALAR_STORED_MODE = EnumScalarType('StoredMode', 'ResolvedGeneratedColumnInfo')
SCALAR_GENERATED_MODE = EnumScalarType(
    'GeneratedMode', 'ResolvedGeneratedColumnInfo'
)
SCALAR_DROP_MODE = EnumScalarType('DropMode', 'ResolvedDropStmt')
SCALAR_INSERTION_MODE = EnumScalarType('InsertionMode',
                                       'ResolvedAuxLoadDataStmt')
SCALAR_INDEX_TYPE = EnumScalarType('IndexType', 'ResolvedDropIndexStmt')
SCALAR_OPTIONS_ASSIGNMENT_OP = EnumScalarType(
    'AssignmentOp',
    'ResolvedOption',
    cpp_default='DEFAULT_ASSIGN',
    java_default='AssignmentOp.DEFAULT_ASSIGN',
)


def Field(name,
          ctype,
          tag_id,
          vector=False,
          ignorable=NOT_IGNORABLE,
          is_constructor_arg=True,
          is_optional_constructor_arg=False,
          to_string_method=None,
          java_to_string_method=None,
          comment=None,
          propagate_order=False):
  """Make a field to put in a node class.

  Args:
    name: field name
    ctype: c++ type for this field
           Should be a ScalarType like an int, string or enum type,
           or the name of a node class type (e.g. ResolvedProjectScan).
           Cannot be a pointer type, and should not include modifiers like
           const.
    tag_id: unique tag number for the proto field. This should never change or
            be reused.
    vector: True if this field is a vector of ctype. (Not supported for
            scalars.)
    ignorable: one of the IGNORABLE enums above.
    is_constructor_arg: True if this field should be in the constructor's
                        argument list.
    is_optional_constructor_arg: True if node class should have two
                                 constructors, and this field should be present
                                 in one of them and absent in another. Requires:
                                 is_constructor_arg=True.
    to_string_method: Override the default ToStringImpl method used to print
                      this object.
    java_to_string_method: Override the default to_string_method method used for
                           java.
    comment: Comment text for this field.  Text will be stripped and
             de-indented.
    propagate_order: If true, this field and the parent node must both be
                     ResolvedScan subclasses, and the is_ordered property of
                     the field will be propagated to the containing scan.
  Returns:
    The newly created field.

  Raises:
    RuntimeError: If an error is detected in one or more arguments.
  """
  assert tag_id > 1, tag_id
  assert ignorable in (IGNORABLE, NOT_IGNORABLE, IGNORABLE_DEFAULT)
  member_name = name + '_'

  is_default_constructible = True

  if vector:
    assert name.endswith('_list') or name.endswith('_path'), (
        'Vector fields should normally be named <x>_list or <x>_path: %s' %
        name)

  # C++ code requires a large number of variations of the same type declaration.
  # These are:
  #   member_type - type of the class member
  #   member_accessor - get the member variable, unwrapping unique_ptr if needed
  #   setter_arg_type - type when passed to constructor or setter method.
  #   maybe_ptr_setter_arg_type - type when passed to constructor or setter
  #                               method as a bare pointer which transfers
  #                               ownership.
  #   getter_return_type - type returned from a getter method
  #   release_return_type - type returned from a release method, or None to
  #                         indicate there should be no release() method.
  #   element_arg_type - for vectors, the element type for get/set methods
  #   element_storage_type - for vectors, the type inside the vector.
  #   element_getter_return_type - for vectors, the return type for get/set
  #                         methods returning a specific element.
  #   element_unwrapper - '.get()', if necessary, to unwrap unique_ptrs.
  #   is_move_only - setters must use std::move instead of assign/copy as
  #                  for anything wrapped in a unique_ptr/vector<unique_ptr>.
  if isinstance(ctype, ScalarType):
    is_node_type = False
    member_type = ctype.ctype
    proto_type = ctype.proto_type
    has_proto_setter = ctype.has_proto_setter
    java_default = ctype.java_default
    cpp_default = ctype.cpp_default
    nullable_java_type = ctype.java_reference_type
    member_accessor = member_name
    element_arg_type = None
    element_storage_type = None
    element_getter_return_type = None
    element_unwrapper = ''
    release_return_type = None
    is_enum = ctype.is_enum
    is_move_only = False
    is_default_constructible = ctype.is_default_constructible
    not_serialize_if_default = ctype.not_serialize_if_default

    if ctype.passed_by_reference:
      setter_arg_type = 'const %s&' % member_type
      getter_return_type = 'const %s&' % member_type
    else:
      setter_arg_type = member_type
      getter_return_type = ctype.ctype

    if vector:
      element_arg_type = ctype.ctype
      element_getter_return_type = getter_return_type
      element_storage_type = ctype.ctype
      member_type = 'std::vector<%s>' % ctype.ctype
      setter_arg_type = 'const %s&' % member_type
      getter_return_type = setter_arg_type
      java_type = ctype.java_reference_type
      java_default = 'ImmutableList.of()'
    else:
      java_type = ctype.java_type
      java_default = ctype.java_default

    # For scalars, these are always the same
    maybe_ptr_setter_arg_type = setter_arg_type

  else:
    # Non-scalars should all be node types (possibly node vectors).
    assert 'const' not in ctype, ctype
    assert '*' not in ctype, ctype
    if not ctype.startswith(NODE_NAME_PREFIX):
      raise RuntimeError('Invalid field type: %s' % (ctype,))
    is_node_type = True
    has_proto_setter = False
    element_pointer_type = 'const %s*' % ctype
    element_storage_type = 'std::unique_ptr<const %s>' % ctype
    element_unwrapper = '.get()'
    proto_type = '%sProto' % ctype
    java_type = ctype
    nullable_java_type = ctype
    is_enum = False
    is_move_only = True
    not_serialize_if_default = False
    cpp_default = ''
    if vector:
      element_arg_type = element_pointer_type
      element_getter_return_type = element_pointer_type
      member_type = 'std::vector<%s>' % element_storage_type
      maybe_ptr_setter_arg_type = (
          'const std::vector<%s>&' % element_pointer_type)
      getter_return_type = 'const %s&' % member_type
      release_return_type = 'std::vector<%s>' % element_pointer_type
      member_accessor = member_name
      java_default = 'ImmutableList.of()'
    else:
      member_type = element_storage_type
      maybe_ptr_setter_arg_type = element_pointer_type
      getter_return_type = element_pointer_type
      release_return_type = element_pointer_type
      element_arg_type = None
      element_storage_type = None
      element_getter_return_type = None
      member_accessor = member_name + '.get()'
      if is_constructor_arg and not is_optional_constructor_arg:
        java_default = None
      else:
        java_default = 'null'

    # For node vector, we use std::move, which requires setters = member.
    setter_arg_type = member_type

  if vector:
    optional_or_repeated = 'repeated'
    full_java_type = 'ImmutableList<%s>' % java_type
    nullable_java_type = full_java_type
  else:
    optional_or_repeated = 'optional'
    full_java_type = java_type

  if not to_string_method:
    to_string_method = 'ToStringImpl'
  if not java_to_string_method:
    java_to_string_method = to_string_method[0].lower() + to_string_method[1:]

  if is_enum:
    scoped_setter_arg_type = ctype.ctype
  else:
    scoped_setter_arg_type = setter_arg_type

  if not is_constructor_arg:
    if java_default is None:
      raise RuntimeError(
          'Field %s must either be a constructor arg, or have a java_default. ctype:\n%s'
          % (
              name,
              ctype,
          ))
    if is_optional_constructor_arg:
      raise RuntimeError(
          'Field %s must be a constructor arg if it has is_optional_constructor_arg=True'
          % (name,))

  is_ignorable_default = (ignorable == IGNORABLE_DEFAULT)
  is_required_for_ctor = is_constructor_arg and not is_optional_constructor_arg

  is_required_builder_arg = not is_default_constructible or (
      is_required_for_ctor and not vector and not is_ignorable_default)

  return {
      'ctype': ctype,
      'java_type': java_type,
      'full_java_type': full_java_type,
      'nullable_java_type': nullable_java_type,
      'java_default': java_default,
      'cpp_default': cpp_default,
      'tag_id': tag_id,
      'member_name': member_name,  # member variable name
      'name': name,  # name without trailing underscore
      'comment': CleanIndent(comment, prefix='  // '),
      'javadoc': JavaDoc(comment, indent=4),
      'member_accessor': member_accessor,
      'member_type': member_type,
      'proto_type': proto_type,
      'optional_or_repeated': optional_or_repeated,
      'has_proto_setter': has_proto_setter,
      'setter_arg_type': setter_arg_type,
      'maybe_ptr_setter_arg_type': maybe_ptr_setter_arg_type,
      'scoped_setter_arg_type': scoped_setter_arg_type,
      'getter_return_type': getter_return_type,
      'release_return_type': release_return_type,
      'is_vector': vector,
      'element_getter_return_type': element_getter_return_type,
      'element_arg_type': element_arg_type,
      'element_storage_type': element_storage_type,
      'element_unwrapper': element_unwrapper,
      'is_node_ptr': is_node_type and not vector,
      'is_node_vector': is_node_type and vector,
      'is_enum_vector': is_enum and vector,
      'is_move_only': is_move_only,
      'is_not_ignorable': ignorable == NOT_IGNORABLE,
      'is_ignorable_default': is_ignorable_default,
      'is_default_constructible': is_default_constructible,
      'is_constructor_arg': is_constructor_arg,
      'is_optional_constructor_arg': is_optional_constructor_arg,
      'is_required_builder_arg': is_required_builder_arg,
      'to_string_method': to_string_method,
      'java_to_string_method': java_to_string_method,
      'propagate_order': propagate_order,
      'not_serialize_if_default': not_serialize_if_default
  }


class TreeGenerator(object):
  """Generates code to define tree objects.
  """

  def __init__(self):
    self.nodes = []

    # Map node name to node (which is just the dictionary created in AddNode
    # right now).
    self.node_map = {}

    # We don't actually have the real root node ResolvedNode, so this
    # is a list of the first-level children.
    self.root_child_nodes = []

  def AddNode(self,
              name,
              tag_id,
              parent,
              fields,
              is_abstract=False,
              extra_defs='',
              extra_defs_node_only='',
              emit_default_constructor=True,
              use_custom_debug_string=False,
              comment=None):
    """Add a node class to be generated.

    Args:
      name: class name for this node
      tag_id: unique tag number for the node as a proto field or an enum value.
          tag_id for each node type is hard coded and should never change.
          Next tag_id: 245
      parent: class name of the parent node
      fields: list of fields in this class; created with Field function
      is_abstract: true if this node is an abstract class
      extra_defs: extra c++ definitions to put in this class and its builder.
      extra_defs_node_only: extra C++ definitions for this class but not to be
        shared with the builder.
      emit_default_constructor: If True, emit default constructor for this class
      use_custom_debug_string: If True, use hand-written
        CollectDebugStringFields and GetNameForDebugString c++ methods for
        DebugString.
      comment: Comment text for this node.  Text will be stripped and
        de-indented.
    """
    assert name not in self.node_map, name
    assert name != ROOT_NODE_NAME, name
    assert parent in self.node_map or parent == ROOT_NODE_NAME, parent
    if parent == ROOT_NODE_NAME:
      # Nodes generally fit into these four top-level categories.
      # Nodes that are part of other nodes are usually ResolvedArguments.
      assert name in ('ResolvedArgument', 'ResolvedExpr', 'ResolvedScan',
                      'ResolvedStatement'), name
    assert len(fields) <= 32, 'More than 32 fields not supported'
    fields = [field.copy() for field in fields]
    extra_enum_decls, extra_enum_defs = self._ResolveImportEnums(name)

    make_enum_name_re = re.compile(r'([a-z])([A-Z])')

    def NameToEnumName(name):
      """Convert a camel-case c++ ClassName into CLASS_NAME."""
      return make_enum_name_re.sub(r'\1_\2', name).upper()

    # Compute list of fields inherited from the parent node.
    # Used for constructors that accept and pass on all inherited fields.
    inherited_fields = []
    if parent != ROOT_NODE_NAME:
      inherited_fields = (self.node_map[parent]['inherited_fields'] +
                          self.node_map[parent]['fields'])

    # Add position-related field members.
    for (pos, field) in enumerate(fields):
      field.update({'bitmap': '(1<<%d)' % pos})
      field.update({'builder_bitmap': pos + len(inherited_fields)})

    proto_type = '%sProto' % name

    # Marking classes and virtual methods as final may allow the
    # compiler to avoid some virtual function calls.
    if is_abstract:
      class_final = ''
      override_or_final = 'override'
      proto_field_type = 'Any%sProto' % name
    else:
      class_final = 'final'
      override_or_final = 'final'
      proto_field_type = proto_type

    has_node_vector_constructor_arg = any(
        field['is_node_vector'] and field['is_constructor_arg']
        for field in fields + inherited_fields)

    def JoinSections(a, b):
      separator = ''
      if a and b:
        separator = '\n'
      return a + separator + b

    node_dict = ({
        'name': name,
        'builder_name': name + 'Builder' if not is_abstract else '',
        'proto_type': proto_type,
        'proto_field_type': proto_field_type,
        'enum_name': NameToEnumName(name),
        'member_name': NameToEnumName(name).lower(),
        'node_kind_name': NameToNodeKindName(name, NODE_NAME_PREFIX),
        'tag_id': tag_id,
        'parent': parent,
        'parent_proto_container_type': 'Any%sProto' % parent,
        'parent_proto_type': '%sProto' % parent,
        'depth': (self.node_map[parent]['depth'] + 1
                  if parent != ROOT_NODE_NAME else 1),
        'is_abstract': is_abstract,
        'class_final': class_final,
        'override_or_final': override_or_final,
        'comment': CleanIndent(comment, prefix='// '),
        'javadoc': JavaDoc(comment, indent=2),
        'fields': fields,
        'inherited_fields': inherited_fields,
        'extra_enum_defs': extra_enum_defs,
        'extra_defs': JoinSections(extra_enum_decls,
                                   CleanIndent(extra_defs, '  ')),
        'extra_defs_node_only': extra_defs_node_only,
        'emit_default_constructor': emit_default_constructor,
        'has_node_vector_constructor_arg': has_node_vector_constructor_arg,
        'use_custom_debug_string': use_custom_debug_string,
        'subclasses': []
    })
    # This is the name of the defining node rather than a reference to
    # avoid creating a recursive data structure that cannot be compared.
    # TraverseToRoot contains such a comparison.
    #
    # In the template, use the defining_node(field) function to find
    # the node in which this field is defined.
    for field in fields:
      field.update({'defining_node_name': name})

    self.nodes.append(node_dict)
    self.node_map[name] = node_dict

  def _ResolveImportEnums(self, cpp_class_name):
    """Import enums under <cpp_class_name>Enums message into the C++ class.

    Args:
      cpp_class_name: C++ class name the enums should be imported into.

    Returns:
      Tuple of C++ declarations and definitions for the .h and .cc files.

    Load definitions of enums defined under <cpp_class_name>Enums message in
    resolved_ast_enums.proto if any, and convert them to C++ declarations and
    definitions to be included in the generated .h/.cc files, so that the enum
    values can be referred as static consts under the C++ class.
    E.g.: if cpp_class_name='ResolvedWindowFrame' and there's a FrameUnit enum
    defined under ResolvedWindowFrameEnums in resolved_ast_enums.proto as:
    message ResolvedWindowFrameEnums {
      enum FrameUnit {
        ROWS = 0;
        RANGE = 1;
      };
    };
    then this function will return the C++ declarations:
      typedef ResolvedWindowFrameEnums::FrameUnit FrameUnit;
      static const FrameUnit ROWS = ResolvedWindowFrameEnums::ROWS;
      static const FrameUnit RANGE = ResolvedWindowFrameEnums::RANGE;
    which should be included in the class declaration, and C++ definitions:
      const ResolvedWindowFrame::FrameUnit ResolvedWindowFrame::ROWS;
      const ResolvedWindowFrame::FrameUnit ResolvedWindowFrame::RANGE;
    which should be included in the .cc file.

    """
    message_types = resolved_ast_enums_pb2.DESCRIPTOR.message_types_by_name
    message_name = cpp_class_name + 'Enums'

    # Hack: We import the same enum names into ResolvedArgument{Ref,Def}.
    if message_name == 'ResolvedArgumentRefEnums':
      message_name = 'ResolvedArgumentDefEnums'

    if message_name not in message_types:
      return '', ''

    typedefs = []
    declarations = []
    definitions = []

    for enum_type in message_types[message_name].enum_types:
      enum_name = enum_type.name
      typedefs.append('  typedef %s::%s %s;\n' % (message_name, enum_name,
                                                  enum_name))
      for value in enum_type.values:
        declarations.append('  static const %s %s = %s::%s;\n' %
                            (enum_name, value.name, message_name, value.name))
        definitions.append('const %s::%s %s::%s;\n' %
                           (cpp_class_name, enum_name, cpp_class_name,
                            value.name))
    return ''.join(typedefs) + ''.join(declarations), ''.join(definitions)

  def _JavaEnumClasses(self):
    classes = []
    message_types = resolved_ast_enums_pb2.DESCRIPTOR.message_types_by_name
    for name, message in message_types.items():
      for enum_type in message.enum_types:
        classes.append('%s.%s' % (name, enum_type.name))
    return classes

  def _GetNodeByName(self, name):
    return self.node_map[name]

  def _ComputeTreeData(self):
    """Add computed nodes to the tree.

    Traverse the node tree hierarchy and add computed nodes that couldn't be
    built before we have the full tree.
    """
    assert not self.root_child_nodes

    def TraverseToRoot(node):
      """Count the number of leaf nodes existing as descendants of each node.

      Traverses upwards from each leaf, incrementing a counter.

      Args:
        node: Node to count descendants of.
      """
      node.setdefault('num_descendant_leaf_types', 0)
      node['num_descendant_leaf_types'] += 1

      for field in node['fields']:
        field_type = field['ctype']
        if field_type in self.node_map:
          field_node = self._GetNodeByName(field_type)
          field['proto_type'] = field_node['proto_field_type']

      parent_name = node['parent']
      if parent_name != ROOT_NODE_NAME:
        parent_node = self._GetNodeByName(parent_name)
        TraverseToRoot(parent_node)
        parent_subclass_list = parent_node['subclasses']
      else:
        parent_subclass_list = self.root_child_nodes

      # Note: this comparison is by value, so the transitive closure of <node>'s
      # entries cannot contain <node>.
      if node not in parent_subclass_list:
        parent_subclass_list.append(node)

    for node in self.nodes:
      if not node['is_abstract']:
        TraverseToRoot(node)

  def Generate(self, input_file_paths, output_file_paths, data_files):
    """Materialize the templates to generate the output files."""

    def ShouldAutoescape(template_name):
      return template_name and '.html' in template_name

    data_dirs = list(map(os.path.dirname, data_files))

    line_statement_prefix = '# ' if FLAGS.allow_hash_prefix else None
    jinja_env = jinja2.Environment(
        undefined=jinja2.StrictUndefined,
        autoescape=ShouldAutoescape,
        trim_blocks=True,
        lstrip_blocks=True,
        line_statement_prefix=line_statement_prefix,
        loader=jinja2.FileSystemLoader([''] + data_dirs))

    # This can be used to filter a list of fields to only those with
    # <is_constructor_arg = true>.  Used in templates like this:
    #   for node.fields | is_constructor_arg
    def IsConstructorArg(field_list):
      return [field for field in field_list if field['is_constructor_arg']]

    def IsFieldRequiredConstructorArg(field):
      return field[
          'is_constructor_arg'] and not field['is_optional_constructor_arg']

    # This can be used to filter a list of fields to only those with
    # <is_constructor_arg = true> and <is_optional_constructor_arg = false>.
    # Used in templates like this:
    #   for node.fields | is_required_constructor_arg
    def IsRequiredConstructorArg(field_list):
      return [
          field for field in field_list if IsFieldRequiredConstructorArg(field)
      ]

    # This can be used to filter a list of fields to only those that would be
    # required for constructors, but shouold be set to default in the builders.
    # Used in templates like this:
    #   for node.fields | is_required_for_builder
    def IsRequiredBuilderArg(field_list):
      return [
          field for field in field_list if field['is_required_builder_arg']
      ]

    # This can be used to filter a list of fields to only those with
    # <is_constructor_arg = true> and <is_optional_constructor_arg = true>.
    # Used in templates like this:
    #   for node.fields | is_optional_constructor_arg
    def IsOptionalConstructorArg(field_list):
      return [
          field for field in field_list if field['is_constructor_arg'] and
          field['is_optional_constructor_arg']
      ]

    def IsNodeVectorConstructorArg(field_list):
      return [
          field for field in field_list
          if field['is_node_vector'] and field['is_constructor_arg']
      ]

    jinja_env.filters['is_constructor_arg'] = IsConstructorArg
    jinja_env.filters['is_required_constructor_arg'] = IsRequiredConstructorArg
    jinja_env.filters['is_optional_constructor_arg'] = IsOptionalConstructorArg
    jinja_env.filters['is_required_builder_arg'] = IsRequiredBuilderArg
    jinja_env.filters[
        'is_node_vector_constructor_arg'] = IsNodeVectorConstructorArg

    # These can be used to filter a list of fields to only those that
    # hold references to child nodes.
    def IsNodePtr(field_list):
      return [field for field in field_list if field['is_node_ptr']]
    def IsNodeVector(field_list):
      return [field for field in field_list if field['is_node_vector']]

    jinja_env.filters['is_node_ptr'] = IsNodePtr
    jinja_env.filters['is_node_vector'] = IsNodeVector

    # These can be used to make relative links and targets within a doc.
    # {{name|as_link}} will make emit name with a link to "#name".
    # {{name|as_target}} will make emit name and make it a target for "#name".
    def AsLink(text):
      # Markup makes this not get re-escaped.
      return markupsafe.Markup('<a href="#%s">%s</a>' % (text, text))

    def AsTarget(text):
      return markupsafe.Markup('<a name="%s">%s</a>' % (text, text))

    jinja_env.filters['as_link'] = AsLink
    jinja_env.filters['as_target'] = AsTarget

    # This can be used to find node names in a string (e.g. a c++ return type)
    # and turn them into relative links inside the doc.
    linkify_re = re.compile('(Resolved[a-zA-Z]*)')

    def LinkifyNodeNames(text):
      # Escape the text which may include strings like std::vector<xxx>,
      # and Markup the output to not be escaped further.
      text = markupsafe.escape(text)
      match = linkify_re.search(text)
      if match:
        name = match.groups()[0]
        return markupsafe.Markup(text.replace(name, AsLink(name)))
      else:
        return text

    jinja_env.filters['linkify_node_names'] = LinkifyNodeNames

    # {{items|sort_by_name}} can be used to sort a list of objects by name.
    def SortByName(items):
      return sorted(items, key=operator.itemgetter('name'))

    jinja_env.filters['sort_by_name'] = SortByName

    # {{items|sort_by_tag}} can be used to sort a list of objects by tag.
    def SortByTagId(items):
      return sorted(items, key=operator.itemgetter('tag_id'))

    jinja_env.filters['sort_by_tag'] = SortByTagId

    jinja_env.filters['upper_camel_case'] = UpperCamelCase

    jinja_env.filters['lower_camel_case'] = LowerCamelCase

    # {{defining_node(some_field)}} will be the node a field is defined in.
    # We have this, rather than a direct field reference, to prevent the data
    # structure from becoming recursive.
    def DefiningNode(field):
      return self.node_map[field['defining_node_name']]

    jinja_env.globals['defining_node'] = DefiningNode

    # For when we need to force a blank line and jinja wants to
    # eat blank lines from the template. Defined in globals instead of context
    # to be accessible from macros as well.
    jinja_env.globals['blank_line'] = '\n'

    def RaiseException(error_message):
      raise RuntimeError(error_message)

    jinja_env.globals['RaiseException'] = RaiseException
    jinja_env.globals['len'] = len

    self._ComputeTreeData()

    context = {
        'nodes': self.nodes,
        'root_node_name': ROOT_NODE_NAME,
        'root_child_nodes': self.root_child_nodes,
        'java_enum_classes': self._JavaEnumClasses(),
        'timestamp': time.ctime(),
    }
    assert len(input_file_paths) == len(output_file_paths)

    for (in_path, out_path) in zip(input_file_paths, output_file_paths):
      if (os.path.basename(in_path) !=
          os.path.basename(out_path) + '.template'):
        logging.fatal(
            'Input must match output with ".template" postfix.'
            'Saw: input=%s output=%s', os.path.basename(in_path),
            os.path.basename(out_path))

      template = jinja_env.get_template(in_path)
      out = open(out_path, 'wt')
      out.write(Trim(template.render(context)))
      out.close()


def main(unused_argv):
  data_files = FLAGS.data_files
  input_templates = FLAGS.input_templates
  output_files = FLAGS.output_files
  if len(input_templates) != len(output_files):
    raise RuntimeError('Must provide an equal number of input and output files')
  if not input_templates:
    raise RuntimeError('Must specify at least one input-output pair')

  gen = TreeGenerator()

  gen.AddNode(
      name='ResolvedArgument',
      tag_id=1,
      parent='ResolvedNode',
      is_abstract=True,
      comment="""
      Argument nodes are not self-contained nodes in the tree.  They exist
      only to describe parameters to another node (e.g. columns in an OrderBy).
      This node is here for organizational purposes only, to cluster these
      argument nodes.
              """,
      fields=[])

  # Expressions

  gen.AddNode(
      name='ResolvedExpr',
      tag_id=2,
      parent='ResolvedNode',
      is_abstract=True,
      fields=[
          Field('type', SCALAR_TYPE, tag_id=2, ignorable=IGNORABLE),
          Field(
              'type_annotation_map',
              SCALAR_ANNOTATION_MAP,
              tag_id=3,
              ignorable=IGNORABLE,
              is_constructor_arg=False)
      ],
      extra_defs="""
        bool IsExpression() const final { return true; }

        AnnotatedType annotated_type() const {
          return {type(), type_annotation_map()};
        }
      """)

  gen.AddNode(
      name='ResolvedLiteral',
      tag_id=3,
      parent='ResolvedExpr',
      comment="""
      Any literal value, including NULL literals.
      There is a special-cased constructor here that gets the type from the
      Value.
              """,
      fields=[
          Field('value', SCALAR_VALUE, tag_id=2),
          Field(
              'has_explicit_type',
              SCALAR_BOOL,
              is_optional_constructor_arg=True,
              tag_id=3,
              ignorable=IGNORABLE,
              comment="""
              If true, then the literal is explicitly typed and cannot be used
              for literal coercions.

              This exists mainly for resolver bookkeeping and should be ignored
              by engines.
                      """),
          Field(
              'float_literal_id',
              SCALAR_INT,
              is_optional_constructor_arg=True,
              tag_id=4,
              ignorable=IGNORABLE,
              comment="""
              Distinct ID of the literal, if it is a floating point value,
              within the resolved AST. When coercing from floating point
              to NUMERIC, the resolver uses the float_literal_id to find the
              original image of the literal to avoid precision loss. An ID of 0
              represents a literal without a cached image.
              """),
          Field(
              'preserve_in_literal_remover',
              SCALAR_BOOL,
              is_constructor_arg=False,
              tag_id=5,
              ignorable=IGNORABLE,
              comment="""
              Indicates whether ReplaceLiteralsByParameters() should leave
              this literal value in place, rather than replace it with a query
              parameter.
              """)
      ])

  gen.AddNode(
      name='ResolvedParameter',
      tag_id=4,
      parent='ResolvedExpr',
      fields=[
          Field(
              'name',
              SCALAR_STRING,
              tag_id=2,
              ignorable=IGNORABLE_DEFAULT,
              comment="""
              If non-empty, the name of the parameter.

              A ResolvedParameter will have either a name or a position but not
              both.
                      """),
          Field(
              'position',
              SCALAR_INT,
              tag_id=5,
              is_optional_constructor_arg=True,
              ignorable=IGNORABLE_DEFAULT,
              comment="""
              If non-zero, the 1-based position of the positional parameter.

              A ResolvedParameter will have either a name or a position but not
              both.
                      """),
          Field(
              'is_untyped',
              SCALAR_BOOL,
              tag_id=3,
              is_optional_constructor_arg=True,
              ignorable=IGNORABLE,
              comment="""
              If true, then the parameter has no specified type.

              This exists mainly for resolver bookkeeping and should be ignored
              by engines.
                      """)
      ])

  gen.AddNode(
      name='ResolvedExpressionColumn',
      tag_id=5,
      parent='ResolvedExpr',
      comment="""
      This represents a column when analyzing a standalone expression.
      This is only used when the analyzer was called using AnalyzeExpression.
      Expression column names and types come from
      AnalyzerOptions::AddExpressionColumn.
      <name> will always be in lowercase.
              """,
      fields=[Field('name', SCALAR_STRING, tag_id=2)])

  gen.AddNode(
      name='ResolvedCatalogColumnRef',
      tag_id=206,
      parent='ResolvedExpr',
      comment="""
      An expression referencing a Column from the Catalog. This is used to
      represent a column reference in an expression inside a DDL statement.
      The DDL statement will normally define the Table context, and the
      referenced Column should be a Column of that Table.
      """,
      fields=[Field('column', SCALAR_COLUMN, tag_id=2)])

  gen.AddNode(
      name='ResolvedColumnRef',
      tag_id=6,
      parent='ResolvedExpr',
      comment="""
      An expression referencing the value of some column visible in the
      current Scan node.

      If <is_correlated> is false, this must be a column visible in the Scan
      containing this expression, either because it was produced inside that
      Scan or it is on the <column_list> of some child of this Scan.

      If <is_correlated> is true, this references a column from outside a
      subquery that is visible as a correlated column inside.
      The column referenced here must show up on the parameters list for the
      subquery.  See ResolvedSubqueryExpr.
              """,
      fields=[
          Field('column', SCALAR_RESOLVED_COLUMN, tag_id=2),
          Field(
              'is_correlated', SCALAR_BOOL, tag_id=3, ignorable=IGNORABLE)
      ])

  gen.AddNode(
      name='ResolvedGroupingSetMultiColumn',
      tag_id=237,
      parent='ResolvedArgument',
      comment="""
      A list of ResolvedColumnRef expression references that will be batched
      together in rollup/cube when being expanded to grouping sets. For
      example, ROLLUP((a, b), c) will be expanded to 3 grouping sets [(a, b, c),
      (a, b), ()], (a, b) is a multi-column.

      Duplicated columns are not allowed in the ResolvedGroupingSetMultiColumn
      as they are equivalent to deduplicated columns. column_list must have
      at least one element inside.
              """,
      fields=[Field('column_list', 'ResolvedColumnRef', tag_id=2, vector=True)],
  )

  gen.AddNode(
      name='ResolvedConstant',
      tag_id=103,
      parent='ResolvedExpr',
      use_custom_debug_string=True,
      comment="""
      A reference to a named constant.
              """,
      fields=[
          Field(
              'constant',
              SCALAR_CONSTANT,
              tag_id=2,
              comment="""
        The matching Constant from the Catalog.
            """)
      ])

  gen.AddNode(
      name='ResolvedSystemVariable',
      tag_id=139,
      parent='ResolvedExpr',
      use_custom_debug_string=True,
      comment="""
      A reference to a system variable.
              """,
      fields=[
          Field(
              'name_path',
              SCALAR_STRING,
              vector=True,
              tag_id=2,
              comment="""
        Path to system variable.
            """)
      ])

  gen.AddNode(
      name='ResolvedInlineLambda',
      tag_id=159,
      parent='ResolvedArgument',
      comment="""
      A lambda expression, used inline as a function argument.
      This represents both the definition of the lambda and the resolution of
      its templated signature and body for this function call.
      Currently can only be used as an argument of a function.

      <argument_list> defines the argument types and names for the lambda, and
      creates new ResolvedColumns which can be used to reference the arguments
      inside <body>.

      The return type of the lambda function is the type of <body>.

      In addition to the <argument_list>, the body of a lambda expression can
      reference columns visible to the scope of the function call for which this
      lambda is provided as an argument. Columns in this scope accessed by the
      body are stored in <parameter_list>.

      For example, the following query
        SELECT ARRAY_FILTER([1,2,3], e -> e = key) FROM KeyValue;
      would have a lambda with <parameter_list> ['key'] and <argument_list>
      ['e'].

      <body> is the body expression of the lambda. The expression can only
      reference columns in <parameter_list> and <argument_list>.
      """,
      fields=[
          Field('argument_list', SCALAR_RESOLVED_COLUMN, tag_id=2, vector=True),
          Field('parameter_list', 'ResolvedColumnRef', tag_id=3, vector=True),
          Field('body', 'ResolvedExpr', tag_id=4),
      ])

  gen.AddNode(
      name='ResolvedSequence',
      tag_id=233,
      parent='ResolvedArgument',
      comment="""
      Represents a sequence as a function argument
              """,
      fields=[
          Field('sequence', SCALAR_SEQUENCE, tag_id=2),
      ],
  )

  gen.AddNode(
      name='ResolvedFilterFieldArg',
      tag_id=173,
      parent='ResolvedArgument',
      comment="""
      An argument to the FILTER_FIELDS() function which specifies a sign to show
      inclusion/exclusion status and a field path to include or exclude.
      """,
      fields=[
          Field(
              'include',
              SCALAR_BOOL,
              tag_id=2,
              comment="""
              True if we want to include this proto path in the resulting proto
              (though we may still remove paths below it).
              If False, we will remove this path (but may still include paths
              below it).
              """),
          Field(
              'field_descriptor_path',
              SCALAR_FIELD_DESCRIPTOR,
              tag_id=3,
              vector=True,
              to_string_method='ToStringVectorFieldDescriptor',
              java_to_string_method='toStringPeriodSeparatedForFieldDescriptors',
              comment="""
              A vector of FieldDescriptors that denotes the path to a proto
              field that will be include or exclude.
              """),
      ])

  gen.AddNode(
      name='ResolvedFilterField',
      tag_id=174,
      parent='ResolvedExpr',
      comment="""
      Represents a call to the FILTER_FIELDS() function. This function can be
      used to modify a proto, prune fields and output the resulting proto. The
      SQL syntax for this function is
        FILTER_FIELDS(<expr>, <filter_field_arg_list>).

      <expr> must have proto type. <filter_field_arg> contains a sign ('+' or
      '-') and a field path starting from the proto.

      For example:
        FILTER_FIELDS(proto, +field1, -field1.field2)
      means the resulting proto only contains field1.* except field1.field2.*.

      Field paths are evaluated and processed in order,
      ```
        IF filter_field_arg_list[0].include:
          CLEAR all fields
        FOR filter_field_arg IN filter_field_arg_list:
          IF filter_field_arg.include:
            UNCLEAR filter_field_arg.field_descriptor_path (and all children)
          ELSE:
            CLEAR filter_field_arg.field_descriptor_path (and all children)
      ```

      The order of field_field args have following constraints:
      1. There must be at least one filter_field arg.
      2. Args for ancestor fields must precede descendants.
      3. Each arg must have opposite `include` compared to the last preceding
         ancestor field.

      See (broken link) for more detail.
      """,
      fields=[
          Field(
              'expr',
              'ResolvedExpr',
              tag_id=2,
              comment="""The proto to modify."""),
          Field(
              'filter_field_arg_list',
              'ResolvedFilterFieldArg',
              tag_id=3,
              vector=True,
              comment="""
              The list of field paths to include or exclude. The path starts
              from the proto type of <expr>.
                      """),
          Field(
              'reset_cleared_required_fields',
              SCALAR_BOOL,
              tag_id=4,
              comment="""If true, will reset cleared required fields into a
              default value.
                      """),
      ])

  gen.AddNode(
      name='ResolvedFunctionCallBase',
      tag_id=7,
      parent='ResolvedExpr',
      emit_default_constructor=False,
      is_abstract=True,
      use_custom_debug_string=True,
      comment="""
      Common base class for scalar and aggregate function calls.

      <argument_list> contains a list of arguments of type ResolvedExpr.

      <generic_argument_list> contains an alternative list of generic arguments.
      This is used for function calls that accept non-expression arguments (i.e.
      arguments that aren't part of the type system, like lambdas).

      If all arguments of this function call are ResolvedExprs, <argument_list>
      is used. If any of the argument is not a ResolvedExpr,
      <generic_argument_list> will be used. Only one of <argument_list> or
      <generic_argument_list> can be non-empty.

      <collation_list> (only set when FEATURE_V_1_3_COLLATION_SUPPORT is
      enabled) is the operation collation to use.
      (broken link) lists the functions affected by
      collation, where this can show up.
      <collation_list> is a vector for future extension. For now, functions
      could have at most one element in the <collation_list>.
              """,
      fields=[
          Field(
              'function',
              SCALAR_FUNCTION,
              tag_id=2,
              comment="""The matching Function from the Catalog."""),
          Field(
              'signature',
              SCALAR_FUNCTION_SIGNATURE,
              tag_id=3,
              ignorable=IGNORABLE,
              comment="""
              The concrete FunctionSignature reflecting the matching Function
              signature and the function's resolved input <argument_list>.
              The function has the mode AGGREGATE iff it is an aggregate
              function, in which case this node must be either
              ResolvedAggregateFunctionCall or ResolvedAnalyticFunctionCall.
                      """),
          Field(
              'argument_list',
              'ResolvedExpr',
              tag_id=4,
              ignorable=IGNORABLE_DEFAULT,
              vector=True,
          ),
          Field(
              'generic_argument_list',
              'ResolvedFunctionArgument',
              is_optional_constructor_arg=True,
              tag_id=6,
              ignorable=IGNORABLE_DEFAULT,
              vector=True,
          ),
          Field(
              'error_mode',
              SCALAR_ERROR_MODE,
              tag_id=5,
              ignorable=IGNORABLE_DEFAULT,
              comment="""
              If error_mode=SAFE_ERROR_MODE, and if this function call returns a
              semantic error (based on input data, not transient server
              problems), return NULL instead of an error. This is used for
              functions called using SAFE, as in SAFE.FUNCTION(...).
                      """),
          Field(
              'hint_list',
              'ResolvedOption',
              tag_id=7,
              ignorable=IGNORABLE,
              is_constructor_arg=False,
              vector=True,
              comment="""
              Function call hints.
                      """),
          Field(
              'collation_list',
              SCALAR_RESOLVED_COLLATION,
              tag_id=8,
              ignorable=IGNORABLE_DEFAULT,
              vector=True,
              java_to_string_method='toStringCommaSeparated',
              is_constructor_arg=False),
      ])

  gen.AddNode(
      name='ResolvedFunctionCall',
      tag_id=8,
      parent='ResolvedFunctionCallBase',
      emit_default_constructor=False,
      fields=[
          Field(
              'function_call_info',
              SCALAR_FUNCTION_CALL_INFO,
              is_optional_constructor_arg=True,
              tag_id=2,
              ignorable=IGNORABLE,
              comment="""
              This contains optional custom information about a particular
              function call.

              If some Function subclass requires computing additional
              information at resolving time, that extra information can be
              stored as a subclass of ResolvedFunctionCallInfo here.
              For example, TemplatedSQLFunction stores the resolved template
              body here as a TemplatedSQLFunctionCall.

              This field is ignorable because for most types of function calls,
              there is no extra information to consider besides the arguments
              and other fields from ResolvedFunctionCallBase.
                      """)
      ],
      comment="""
      A regular function call.  The signature will always have mode SCALAR.
      Most scalar expressions show up as FunctionCalls using builtin signatures.
              """)

  gen.AddNode(
      name='ResolvedNonScalarFunctionCallBase',
      tag_id=86,
      parent='ResolvedFunctionCallBase',
      emit_default_constructor=False,
      is_abstract=True,
      comment="""
      Common base class for analytic and aggregate function calls.
              """,
      fields=[
          Field(
              'distinct',
              SCALAR_BOOL,
              tag_id=2,
              ignorable=IGNORABLE_DEFAULT,
              comment="""
              Apply DISTINCT to the stream of input values before calling
              function.
                      """),
          Field(
              'null_handling_modifier',
              SCALAR_NULL_HANDLING_MODIFIER_KIND,
              tag_id=3,
              ignorable=IGNORABLE_DEFAULT,
              comment="""
              Apply IGNORE/RESPECT NULLS filtering to the stream of input
              values.
                      """),
          Field(
              'with_group_rows_subquery',
              'ResolvedScan',
              tag_id=4,
              ignorable=IGNORABLE_DEFAULT,
              is_constructor_arg=False,
              comment="""
              Holds a table subquery defined in WITH GROUP_ROWS(...) that is
              evaluated over the input rows of a ResolvedAggregateScan
              corresponding to the current group. The function itself is
              evaluated over the rows returned from the subquery.

              The subquery should refer to a special TVF GROUP_ROWS(), which
              resolves as ResolvedGroupRowsScan. The subquery will be run for
              each group produced by ResolvedAggregateScan.

              GROUP_ROWS() produces a row for each source row in the
              ResolvedAggregateScan's input that matches current group.

              The subquery cannot reference any ResolvedColumns from the outer
              query except what comes in via <with_group_rows_parameter_list>,
              and GROUP_ROWS().

              The subquery can return more than one column, and these columns
              can be referenced by the function.

              The subquery can be correlated. In this case the
              <with_group_rows_parameter_list> gives the set of ResolvedColumns
              from outside the subquery that are used inside. The subuery cannot
              refer to correlated columns that are used as aggregation input in
              the immediate outer query. The same rules apply to
              <with_group_rows_parameter_list> as in ResolvedSubqueryExpr.
                      """),
          Field(
              'with_group_rows_parameter_list',
              'ResolvedColumnRef',
              tag_id=5,
              vector=True,
              ignorable=IGNORABLE_DEFAULT,
              is_constructor_arg=False,
              comment="""
              Correlated parameters to <with_group_rows_subquery>
                      """),
      ])

  gen.AddNode(
      name='ResolvedAggregateFunctionCall',
      tag_id=9,
      parent='ResolvedNonScalarFunctionCallBase',
      emit_default_constructor=False,
      comment="""
      An aggregate function call.  The signature always has mode AGGREGATE.
      This node only ever shows up as the outer function call in a
      ResolvedAggregateScan::aggregate_list.
              """,
      fields=[
          Field(
              'having_modifier',
              'ResolvedAggregateHavingModifier',
              tag_id=5,
              ignorable=IGNORABLE_DEFAULT,
              comment="""
              Apply HAVING MAX/MIN filtering to the stream of input values.
                      """),
          Field(
              'order_by_item_list',
              'ResolvedOrderByItem',
              tag_id=3,
              ignorable=IGNORABLE_DEFAULT,
              comment="""
              Apply ordering to the stream of input values before calling
              function.
                      """,
              vector=True),
          Field(
              'limit',
              'ResolvedExpr',
              tag_id=4,
              ignorable=IGNORABLE_DEFAULT),
          Field(
              'function_call_info',
              SCALAR_FUNCTION_CALL_INFO,
              is_optional_constructor_arg=True,
              tag_id=6,
              ignorable=IGNORABLE,
              comment="""
              This contains optional custom information about a particular
              function call. Functions may introduce subclasses of this class to
              add custom information as needed on a per-function basis.

              This field is ignorable because for most types of function calls,
              there is no extra information to consider besides the arguments
              and other fields from ResolvedFunctionCallBase. However, for
              example, the TemplateSQLFunction in
              zetasql/public/templated_sql_function.h defines the
              TemplatedSQLFunctionCall subclass which includes the
              fully-resolved function body in context of the actual concrete
              types of the arguments provided to the function call.
                      """)
      ])

  gen.AddNode(
      name='ResolvedAnalyticFunctionCall',
      tag_id=10,
      parent='ResolvedNonScalarFunctionCallBase',
      emit_default_constructor=False,
      comment="""
      An analytic function call. The mode of the function is either AGGREGATE
      or ANALYTIC. This node only ever shows up as a function call in a
      ResolvedAnalyticFunctionGroup::analytic_function_list. Its associated
      window is not under this node but as a sibling of its parent node.

      <window_frame> can be NULL.
              """,
      fields=[Field('window_frame', 'ResolvedWindowFrame', tag_id=2)])

  gen.AddNode(
      name='ResolvedExtendedCastElement',
      tag_id=151,
      parent='ResolvedArgument',
      use_custom_debug_string=True,
      comment="""
      Describes a leaf extended cast of ResolvedExtendedCast. See the comment
      for element_list field of ResolvedExtendedCast for more details.
              """,
      fields=[
          Field(
              'from_type', SCALAR_TYPE, tag_id=2, ignorable=IGNORABLE),
          Field('to_type', SCALAR_TYPE, tag_id=3, ignorable=IGNORABLE),
          Field(
              'function',
              SCALAR_FUNCTION,
              tag_id=4,
              ignorable=IGNORABLE)
      ])

  gen.AddNode(
      name='ResolvedExtendedCast',
      tag_id=158,
      parent='ResolvedArgument',
      comment="""
      Describes overall cast operation between two values where at least one
      value's type is or contains an extended type (e.g. on a struct field).
              """,
      fields=[
          Field(
              'element_list',
              'ResolvedExtendedCastElement',
              vector=True,
              tag_id=2,
              comment="""
              Stores the list of leaf extended casts required as elements of
              this cast.  Each element is a cast where at least one of the input
              or output is an extended type. For structs or arrays, the elements
              will be casts for the field or element types. For structs, there
              can be multiple cast elements (one for each distinct pair of field
              types). For non-struct types, there will be just a single element.
                      """)
      ])

  gen.AddNode(
      name='ResolvedCast',
      tag_id=11,
      parent='ResolvedExpr',
      use_custom_debug_string=True,
      comment="""
      A cast expression, casting the result of an input expression to the
      target Type.

      Valid casts are defined in the CastHashMap (see cast.cc), which identifies
      valid from-Type, to-Type pairs.  Consumers can access it through
      GetZetaSQLCasts().
              """,
      fields=[
          Field('expr', 'ResolvedExpr', tag_id=2),
          Field(
              'return_null_on_error',
              SCALAR_BOOL,
              tag_id=3,
              ignorable=IGNORABLE_DEFAULT,
              comment="""
              Whether to return NULL if the cast fails. This is set to true for
              SAFE_CAST.
                      """),
          Field(
              'extended_cast',
              'ResolvedExtendedCast',
              tag_id=4,
              ignorable=IGNORABLE,
              is_optional_constructor_arg=True,
              comment="""
              If at least one of types involved in this cast is or contains an
              extended (TYPE_EXTENDED) type, this field contains information
              necessary to execute this cast.
                      """),
          Field(
              'format',
              'ResolvedExpr',
              tag_id=5,
              ignorable=IGNORABLE_DEFAULT,
              is_optional_constructor_arg=True,
              comment="""
              The format string specified by the optional FORMAT clause. It is
              nullptr when the clause does not exist.
                      """),
          Field(
              'time_zone',
              'ResolvedExpr',
              tag_id=6,
              ignorable=IGNORABLE_DEFAULT,
              is_optional_constructor_arg=True,
              comment="""
              The time zone expression by the optional AT TIME ZONE clause. It
              is nullptr when the clause does not exist.
                      """),
          Field(
              'type_modifiers',
              SCALAR_TYPE_MODIFIERS,
              tag_id=8,
              ignorable=IGNORABLE_DEFAULT,
              is_optional_constructor_arg=True,
              comment="""
              Contains the TypeModifiers object which wraps all modifiers
              following the type name in a type string (e.g. type parameters,
              collation) in a cast. If there are no type modifiers specified,
              this object will be empty.

              Type parameters can be specified inside parentheses following the
              type name (e.g. STRING(2)). If specified, the result of the cast
              should conform to the type parameters. Engines are expected to
              enforce type parameter constraints by erroring out or truncating
              the cast result, depending on the output type. See
              (broken link) for more details.

              For example:
                CAST("ABC" as STRING(2)) should error out
                CAST(1234 as NUMERIC(2)) should error out
                CAST(1.234 as NUMERIC(2,1)) should return a NumericValue of 1.2

              Collation can be specified with the COLLATE keyword on a string
              type, e.g. STRING COLLATE 'und:ci'. If specified, the
              <type_annotation_map> of the ResolvedCast node will have equal
              collation annotations. See
              (broken link) for more details.

              For example:
                CAST("abc" AS STRING COLLATE "und:ci") should return value "abc"
                  in STRING type with collation "und:ci".
                CAST(["abc"] AS ARRAY<STRING COLLATE "und:ci">) should return
                  the array ["abc"] with collation "und:ci" at the element type.
                      """)
      ],
      extra_defs="""
        // Helper getter, setter of <type_parameters> field for backward
        // compatibility.
        // TODO: We could consider dropping this after we clean up
        // all references.
        ABSL_DEPRECATED("use type_modifiers().type_parameters() instead.")
        const TypeParameters& type_parameters() const {
          return type_modifiers().type_parameters();
        }

        ABSL_DEPRECATED("use set_type_modifiers() instead.")
        void set_type_parameters(const TypeParameters& v);
      """
  )

  gen.AddNode(
      name='ResolvedMakeStruct',
      tag_id=12,
      parent='ResolvedExpr',
      comment="""
      Construct a struct value.  <type> is always a StructType.
      <field_list> matches 1:1 with the fields in <type> position-wise.
      Each field's type will match the corresponding field in <type>.
              """,
      fields=[Field('field_list', 'ResolvedExpr', tag_id=2, vector=True)])

  gen.AddNode(
      name='ResolvedMakeProto',
      tag_id=13,
      parent='ResolvedExpr',
      comment="""
      Construct a proto value.  <type> is always a ProtoType.
      <field_list> is a vector of (FieldDescriptor, expr) pairs to write.
      <field_list> will contain all required fields, and no duplicate fields.
              """,
      fields=[
          Field(
              'field_list', 'ResolvedMakeProtoField', tag_id=2, vector=True)
      ])

  gen.AddNode(
      name='ResolvedMakeProtoField',
      tag_id=14,
      parent='ResolvedArgument',
      use_custom_debug_string=True,
      comment="""
      One field assignment in a ResolvedMakeProto expression.
      The type of expr will match with the zetasql type of the proto field.
      The type will be an array iff the field is repeated.

      For NULL values of <expr>, the proto field should be cleared.

      If any value of <expr> cannot be written into the field, this query
      should fail.
              """,
      fields=[
          Field('field_descriptor', SCALAR_FIELD_DESCRIPTOR, tag_id=2),
          Field(
              'format',
              SCALAR_FIELD_FORMAT,
              tag_id=3,
              ignorable=IGNORABLE_DEFAULT,
              comment="""
              Provides the Format annotation that should be used when building
              this field.  The annotation specifies both the ZetaSQL type and
              the encoding format for this field.
                      """),
          Field('expr', 'ResolvedExpr', tag_id=4)
      ])

  gen.AddNode(
      name='ResolvedGetStructField',
      tag_id=15,
      parent='ResolvedExpr',
      comment="""
      Get the field in position <field_idx> (0-based) from <expr>, which has a
      STRUCT type.
              """,
      fields=[
          Field('expr', 'ResolvedExpr', tag_id=2),
          Field('field_idx', SCALAR_INT, tag_id=3),
          Field(
              'field_expr_is_positional',
              SCALAR_BOOL,
              tag_id=4,
              ignorable=IGNORABLE,
              is_optional_constructor_arg=True,
              comment="""True if using s[OFFSET(0)] syntax rather than
              specifying field name, Only for preserving user intent; no
              semantic consequences"""),
      ])

  gen.AddNode(
      name='ResolvedGetProtoField',
      tag_id=16,
      parent='ResolvedExpr',
      fields=[
          Field('expr', 'ResolvedExpr', tag_id=2),
          Field(
              'field_descriptor',
              SCALAR_FIELD_DESCRIPTOR,
              tag_id=3,
              comment="""
              The proto2 FieldDescriptor to extract.  This provides the tag
              number and wire type.  Additional decoding may be necessary if any
              of the other modifiers below are set.  Consumers should use those
              ZetaSQL-computed modifiers rather than examining field
              annotations directly.

              The field is an extension field iff
              field_descriptor->is_extension() is true.  NOTE: The extended
              descriptor's full_name must match the <expr>'s type's full_name,
              but may not be the same Descriptor. Extension FieldDescriptors may
              come from a different DescriptorPool.

              The field is required if field_descriptor->is_required().  If the
              field is required and not present, an error should result.
                      """),
          Field(
              'default_value',
              SCALAR_VALUE,
              tag_id=4,
              ignorable=IGNORABLE,
              comment="""
              Default value to use when the proto field is not set. The default
              may be NULL (e.g. for proto2 fields with a use_defaults=false
              annotation).

              This will not be filled in (the Value will be uninitialized) if
              get_has_bit is true, or the field is required.

              If field_descriptor->is_required() and the field is not present,
              the engine should return an error.

              If the <expr> itself returns NULL, then extracting a field should
              also return NULL, unless <return_default_value_when_unset> is
              true. In that case, the default value is returned.

              TODO Make un-ignorable after clients migrate to start
              using it.
                      """),
          Field(
              'get_has_bit',
              SCALAR_BOOL,
              tag_id=5,
              ignorable=IGNORABLE_DEFAULT,
              comment="""
              Indicates whether to return a bool indicating if a value was
              present, rather than return the value (or NULL). Never set for
              repeated fields. This field cannot be set if
              <return_default_value_when_unset> is true, and vice versa.
              Expression type will be BOOL.
                      """),
          Field(
              'format',
              SCALAR_FIELD_FORMAT,
              tag_id=6,
              ignorable=IGNORABLE_DEFAULT,
              comment="""
               Provides the Format annotation that should be used when reading
               this field.  The annotation specifies both the ZetaSQL type and
               the encoding format for this field. This cannot be set when
               get_has_bit is true.
                      """),
          Field(
              'return_default_value_when_unset',
              SCALAR_BOOL,
              tag_id=7,
              ignorable=IGNORABLE_DEFAULT,
              comment="""
              Indicates that the default value should be returned if <expr>
              (the parent message) is NULL.  Note that this does *not* affect
              the return value when the extracted field itself is unset, in
              which case the return value depends on the extracted field's
              annotations (e.g., use_field_defaults).

              This can only be set for non-message fields. If the field is a
              proto2 field, then it must be annotated with
              zetasql.use_defaults=true. This cannot be set when <get_has_bit>
              is true or the field is required.
                      """),
      ])

  gen.AddNode(
      name='ResolvedGetJsonField',
      tag_id=165,
      parent='ResolvedExpr',
      comment="""
      Get the field <field_name> from <expr>, which has a JSON type.
              """,
      fields=[
          Field('expr', 'ResolvedExpr', tag_id=2),
          Field('field_name', SCALAR_STRING, tag_id=3)
      ])

  gen.AddNode(
      name='ResolvedFlatten',
      tag_id=149,
      parent='ResolvedExpr',
      comment="""
      Constructs an initial input ARRAY<T> from expr. For each get_field_list
      expr, we evaluate the expression once with each array input element and
      use the output as a new array of inputs for the next get_field_list expr.
      If the result of a single expr is an array, we add each element from that
      array as input to the next step instead of adding the array itself.

      The array elements are evaluated and kept in order. For example, if only
      expr is an array, the result will be equivalent to that array having the
      get_field_list evaluated on each array element retaining order.
              """,
      fields=[
          Field('expr', 'ResolvedExpr', tag_id=2),
          Field(
              'get_field_list',
              'ResolvedExpr',
              vector=True,
              tag_id=3,
              comment="""
              List of 'get' fields to evaluate in order (0 or more struct get
              fields followed by 0 or more proto or json get fields) starting
              from expr. Each get is evaluated N times where N is the number of
              array elements from the previous get (or expr for the first
              expression) generated.

              The 'get' fields may either be a ResolvedGet*Field or an array
              offset function around a ResolvedGet*Field.
                      """),
      ])

  gen.AddNode(
      name='ResolvedFlattenedArg',
      tag_id=150,
      parent='ResolvedExpr',
      comment="""
      Argument for a child of ResolvedFlatten. This is a placeholder to indicate
      that it will be invoked once for each array element from ResolvedFlatten's
      expr or previous get_field_list entry.
              """,
      fields=[])

  gen.AddNode(
      name='ResolvedReplaceFieldItem',
      tag_id=128,
      parent='ResolvedArgument',
      comment="""
      An argument to the REPLACE_FIELDS() function which specifies a field path
      and a value that this field will be set to. The field path to be modified
      can be constructed through the <struct_index_path> and <proto_field_path>
      fields. These vectors correspond to field paths in a STRUCT and PROTO,
      respectively. At least one of these vectors must be non-empty.

      If only <struct_index_path> is non-empty, then the field path only
      references top-level and nested struct fields.

      If only <proto_field_path> is non-empty, then the field path only
      references top-level and nested message fields.

      If both <struct_index_path> and <proto_field_path> are non-empty, then the
      field path should be expanded starting with <struct_index_path>. The last
      field in <struct_index_path> will be the proto from which the first field
      in <proto_field_path> is extracted.

      <expr> and the field to be modified must be the same type.
      """,
      fields=[
          Field(
              'expr',
              'ResolvedExpr',
              tag_id=2,
              comment="""
              The value that the final field in <proto_field_path> will be set
              to.

              If <expr> is NULL, the field will be unset. If <proto_field_path>
              is a required field, the engine must return an error if it is set
              to NULL.
              """),
          Field(
              'struct_index_path',
              SCALAR_INT,
              tag_id=3,
              ignorable=IGNORABLE_DEFAULT,
              vector=True,
              to_string_method='ToStringCommaSeparated',
              java_to_string_method='toStringCommaSeparatedForInt',
              comment="""
              A vector of integers that denotes the path to a struct field that
              will be modified. The integer values in this vector correspond to
              field positions (0-based) in a STRUCT. If <proto_field_path>
              is also non-empty, then the field corresponding to the last index
              in this vector should be of proto type.
                      """),
          Field(
              'proto_field_path',
              SCALAR_FIELD_DESCRIPTOR,
              tag_id=4,
              ignorable=IGNORABLE_DEFAULT,
              vector=True,
              to_string_method='ToStringVectorFieldDescriptor',
              java_to_string_method='toStringPeriodSeparatedForFieldDescriptors',
              comment="""
              A vector of FieldDescriptors that denotes the path to a proto
              field that will be modified. If <struct_index_path> is also
              non-empty, then the first element in this vector should be a
              subfield of the proto corresponding to the last element in
              <struct_index_path>.
                      """),
      ])

  gen.AddNode(
      name='ResolvedReplaceField',
      tag_id=129,
      parent='ResolvedExpr',
      comment="""
              Represents a call to the REPLACE_FIELDS() function. This function
              can be used to copy a proto or struct, modify a few fields and
              output the resulting proto or struct. The SQL syntax for this
              function is REPLACE_FIELDS(<expr>, <replace_field_item_list>).

              See (broken link) for more detail.
      """,
      fields=[
          Field(
              'expr',
              'ResolvedExpr',
              tag_id=2,
              comment=""" The proto/struct to modify. """),
          Field(
              'replace_field_item_list',
              'ResolvedReplaceFieldItem',
              tag_id=3,
              vector=True,
              comment="""
              The list of field paths to be modified along with their new
              values.

              Engines must check at evaluation time that the modifications in
              <replace_field_item_list> obey the following rules
              regarding updating protos in ZetaSQL:
              - Modifying a subfield of a NULL-valued proto-valued field is an
                error.
              - Clearing a required field or subfield is an error.
                      """),
      ])

  gen.AddNode(
      name='ResolvedGetProtoOneof',
      tag_id=130,
      parent='ResolvedExpr',
      comment="""
      Returns a string value indicating which field of <oneof_descriptor> is
      set in the containing proto <expr>. If none of the fields are set, an
      empty string is returned.

      See (broken link) for more detail.
              """,
      fields=[
          Field('expr', 'ResolvedExpr', tag_id=2),
          Field(
              'oneof_descriptor',
              SCALAR_ONEOF_DESCRIPTOR,
              tag_id=3,
              comment="""
              The google::protobuf::OneofDescriptor for a Oneof contained in <expr>.
              This descriptor provides google::protobuf::FieldDescriptors for each of
              the fields contained in the Oneof.
                      """),
      ])

  gen.AddNode(
      name='ResolvedSubqueryExpr',
      tag_id=17,
      parent='ResolvedExpr',
      comment="""
      A subquery in an expression (not a FROM clause).  The subquery runs
      in the context of a single input row and produces a single output value.

      Correlated subqueries can be thought of like functions, with a parameter
      list.  The <parameter_list> gives the set of ResolvedColumns from outside
      the subquery that are used inside.

      Inside the subquery, the only allowed references to values outside the
      subquery are to the named ColumnRefs listed in <parameter_list>.
      Any reference to one of these parameters will be represented as a
      ResolvedColumnRef with <is_correlated> set to true.

      These parameters are only visible through one level of expression
      subquery.  An expression subquery inside an expression has to list
      parameters again if parameters from the outer query are passed down
      further.  (This does not apply for table subqueries inside an expression
      subquery.  Table subqueries are never indicated in the resolved AST, so
      Scan nodes inside an expression query may have come from a nested table
      subquery, and they can still reference the expression subquery's
      parameters.)

      An empty <parameter_list> means that the subquery is uncorrelated.  It is
      permissable to run an uncorrelated subquery only once and reuse the \
result.
      TODO Do we want to specify semantics more firmly here?

      The semantics vary based on SubqueryType:
        SCALAR
          Usage: ( <subquery> )
          If the subquery produces zero rows, the output value is NULL.
          If the subquery produces exactly one row, that row is the output \
value.
          If the subquery produces more than one row, raise a runtime error.

        ARRAY
          Usage: ARRAY( <subquery> )
          The subquery produces an array value with zero or more rows, with
          one array element per subquery row produced.

        EXISTS
          Usage: EXISTS( <subquery> )
          The output type is always bool.  The result is true if the subquery
          produces at least one row, and false otherwise.

        IN
          Usage: <in_expr> [NOT] IN ( <subquery> )
          The output type is always bool.  The result is true when <in_expr> is
          equal to at least one row, and false otherwise.  The <subquery> row
          contains only one column, and the types of <in_expr> and the
          subquery column must exactly match a built-in signature for the
          '$equals' comparison function (they must be the same type or one
          must be INT64 and the other UINT64).  NOT will be expressed as a $not
          FunctionCall wrapping this SubqueryExpr.

       LIKE
          Usage: <in_expr> [NOT] LIKE ANY|SOME|ALL ( <subquery> )
          The output type is always bool. The result is true when <in_expr>
          matches at least one row for LIKE ANY|SOME or matches all rows for
          LIKE ALL, and false otherwise.  The <subquery> row contains only one
          column, and the types of <in_expr> and the subquery column must
          exactly match a built-in signature for the relevant '$like_any' or
          '$like_all' comparison function (both must be the same type of either
          STRING or BYTES).  NOT will be expressed as a $not FunctionCall
          wrapping this SubqueryExpr.

      The subquery for a SCALAR, ARRAY, IN or LIKE subquery must have exactly
      one output column.
      The output type for a SCALAR or ARRAY subquery is that column's type or
      an array of that column's type.  (The subquery scan may include a Project
      with a MakeStruct or MakeProto expression to construct a single value
      from multiple columns.)
              """,
      fields=[
          Field('subquery_type', SCALAR_SUBQUERY_TYPE, tag_id=2),
          Field(
              'parameter_list',
              'ResolvedColumnRef',
              tag_id=3,
              vector=True,
              ignorable=IGNORABLE_DEFAULT),
          Field(
              'in_expr',
              'ResolvedExpr',
              tag_id=4,
              ignorable=IGNORABLE_DEFAULT,
              comment="""
              Field is only populated for subqueries of type IN or LIKE
              ANY|SOME|ALL.
                      """),
          Field(
              'in_collation',
              SCALAR_RESOLVED_COLLATION,
              tag_id=7,
              ignorable=IGNORABLE_DEFAULT,
              is_constructor_arg=False,
              comment="""
              Field is only populated for subqueries of type IN to specify the
              operation collation to use to compare <in_expr> with the rows from
              <subquery>.
                      """),
          Field('subquery', 'ResolvedScan', tag_id=5),
          Field(
              'hint_list',
              'ResolvedOption',
              tag_id=6,
              ignorable=IGNORABLE,
              is_constructor_arg=False,
              vector=True,
              comment="""
              Note: Hints currently happen only for EXISTS, IN, or a LIKE
              expression subquery but not for ARRAY or SCALAR subquery.
                      """)
      ])

  gen.AddNode(
      name='ResolvedWithExpr',
      tag_id=197,
      parent='ResolvedExpr',
      emit_default_constructor=False,
      comment="""
      ResolvedWithExpr introduces one or more columns in <assignment_list> that
      can then be referenced inside <expr>. Each assigned expression is
      evaluated once, and each reference to that column in <expr> sees the same
      value even if the assigned expression is volatile. Multiple assignment
      expressions are independent and cannot reference other columns in the
      <assignment_list>.

      <assignment_list> One or more columns that are computed before evaluating
                        <expr>, and which may be referenced by <expr>.
      <expr> Computes the result of the ResolvedWithExpr. May reference columns
             from <assignment_list>.
              """,
      fields=[
          Field(
              'assignment_list',
              'ResolvedComputedColumn',
              tag_id=2,
              vector=True),
          Field('expr', 'ResolvedExpr', tag_id=3),
      ])

  # Scans (anything that shows up in a from clause and produces rows).

  gen.AddNode(
      name='ResolvedScan',
      tag_id=18,
      parent='ResolvedNode',
      is_abstract=True,
      comment="""
      Common superclass for all Scans, which are nodes that produce rows
      (e.g. scans, joins, table subqueries).  A query's FROM clause is
      represented as a single Scan that composes all input sources into
      a single row stream.

      Each Scan has a `column_list` that says what columns are produced.
      The Scan logically produces a stream of output rows, where each row
      has exactly these columns.

      Each Scan may have an attached `hint_list`, storing each hint as
      a ResolvedOption.

      If `is_ordered` is true, this Scan produces an ordered output, either
      by generating order itself (OrderByScan) or by preserving the order
      of its single input scan (LimitOffsetScan, ProjectScan, or WithScan).

      Each Scan has a `node_source` field that, if populated, represents where
      the scan was from or why it was added. This is used in some cases by the
      resolver, rewriters or the random query generator to record why a node was
      added. The SQLBuilder may also use `node_source` to influence which query
      patterns to generate.
              """,
      fields=[
          Field(
              'column_list',
              SCALAR_RESOLVED_COLUMN,
              tag_id=2,
              ignorable=IGNORABLE,
              vector=True),
          Field(
              'hint_list',
              'ResolvedOption',
              tag_id=3,
              ignorable=IGNORABLE,
              is_constructor_arg=False,
              vector=True),
          Field(
              'is_ordered',
              SCALAR_BOOL,
              tag_id=4,
              ignorable=IGNORABLE,
              is_constructor_arg=False,
          ),
          Field(
              'node_source',
              SCALAR_STRING,
              tag_id=5,
              ignorable=IGNORABLE,
              is_constructor_arg=False,
          ),
      ],
      extra_defs="""bool IsScan() const final { return true; }""")

  gen.AddNode(
      name='ResolvedExecuteAsRoleScan',
      tag_id=207,
      parent='ResolvedScan',
      comment="""
      This node provides the role context for its subtree. Currently, it only
      handles subtrees from inlined TVFs and VIEWs created with DEFINER rights.
      Due to the lack of a ROLE catalog object, we are using the original
      catalog object (VIEW or TVF) as a proxy. The engine is expected to extract
      the target role of these objects.

      In the future, when we have catalog objects for roles, this node should
      be updated to attach role object, rather than the original inlined object.

      The node's role context covers the whole subtree underneath it, except
      subtrees under other nested ResolvedExecuteAsRoleScan nodes.

      Always creates new output columns in <column_list>, which map 1:1 with
      the <input_scan>'s output columns. Most rewriters trace their columns all
      the way back to the scan that defined them so this makes this node a
      boundary, as desired.
      """,
      fields=[
          Field(
              'input_scan',
              'ResolvedScan',
              tag_id=2,
              propagate_order=True,
              comment="""
              The input scan whose subtree is to be encompassed by the current
              role context.
              """),
          Field(
              'original_inlined_view',
              SCALAR_TABLE,
              tag_id=3,
              comment="""
              The original view that was inlined. If set, then
              'original_inlined_tvf' is null. The validator checks that this
              table is indeed a SqlView, not some other subclass of Table.
              """),
          Field(
              'original_inlined_tvf',
              TABLE_VALUED_FUNCTION,
              tag_id=4,
              comment="""
              The original TVF that was inlined. If set, then
              'original_inlined_view' is null.
              """),
      ])

  gen.AddNode(
      name='ResolvedModel',
      tag_id=109,
      parent='ResolvedArgument',
      comment="""
      Represents a machine learning model as a TVF argument.
      <model> is the machine learning model object known to the resolver
      (usually through the catalog).
              """,
      fields=[
          Field('model', SCALAR_MODEL, tag_id=2),
      ])

  gen.AddNode(
      name='ResolvedConnection',
      tag_id=141,
      parent='ResolvedArgument',
      comment="""
      Represents a connection object as a TVF argument.
      <connection> is the connection object encapsulated metadata to connect to
      an external data source.
              """,
      fields=[
          Field('connection', SCALAR_CONNECTION, tag_id=2),
      ])

  gen.AddNode(
      name='ResolvedDescriptor',
      tag_id=144,
      parent='ResolvedArgument',
      comment="""
      Represents a descriptor object as a TVF argument.
      A descriptor is basically a list of unresolved column names, written
        DESCRIPTOR(column1, column2)

      <descriptor_column_name_list> contains the column names.

      If FunctionArgumentTypeOptions.get_resolve_descriptor_names_table_offset()
      is true, then <descriptor_column_list> contains resolved columns from
      the sibling ResolvedFunctionArgument of scan type, and will match
      positionally with <descriptor_column_name_list>.
      """,
      fields=[
          Field(
              'descriptor_column_list',
              SCALAR_RESOLVED_COLUMN,
              tag_id=2,
              vector=True,
              ignorable=IGNORABLE_DEFAULT),
          Field(
              'descriptor_column_name_list',
              SCALAR_STRING,
              tag_id=3,
              vector=True,
              ignorable=IGNORABLE_DEFAULT,
              to_string_method='ToStringCommaSeparated'),
      ])

  gen.AddNode(
      name='ResolvedSingleRowScan',
      tag_id=19,
      parent='ResolvedScan',
      fields=[],
      comment="""
      Scan that produces a single row with no columns.  Used for queries without
      a FROM clause, where all output comes from the select list.
              """)

  gen.AddNode(
      name='ResolvedTableScan',
      tag_id=20,
      parent='ResolvedScan',
      comment="""
      Scan a Table.
      The <column_list>[i] should be matched to a Table column by
      <table>.GetColumn(<column_index_list>[i]).

      If AnalyzerOptions::prune_unused_columns is true, the <column_list> and
      <column_index_list> will include only columns that were referenced
      in the user query. (SELECT * counts as referencing all columns.)
      This column_list can then be used for column-level ACL checking on tables.
      Pruning has no effect on value tables (the value is never pruned).

      for_system_time_expr when non NULL resolves to TIMESTAMP used in
      FOR SYSTEM_TIME AS OF clause. The expression is expected to be constant
      and no columns are visible to it.

      <column_index_list> This list matches 1-1 with the <column_list>, and
      identifies the ordinal of the corresponding column in the <table>'s
      column list.

      If provided, <alias> refers to an explicit alias which was used to
      reference a Table in the user query. If the Table was given an implicitly
      generated alias, then defaults to "".

      TODO: Enforce <column_index_list> in the constructor arg list. For
      historical reasons, some clients match <column_list> to Table columns by
      ResolvedColumn name. This violates the ResolvedColumn contract, which
      explicitly states that the ResolvedColumn name has no semantic meaning.
      All code building a ResolvedTableScan should always
      set_column_index_list() immediately after construction.
              """,
      fields=[
          Field('table', SCALAR_TABLE, tag_id=2),
          Field(
              'for_system_time_expr',
              'ResolvedExpr',
              tag_id=3,
              ignorable=IGNORABLE_DEFAULT),
          Field(
              'column_index_list',
              SCALAR_INT,
              tag_id=4,
              ignorable=IGNORABLE,
              vector=True,
              is_constructor_arg=False,
              to_string_method='ToStringCommaSeparated',
              java_to_string_method='toStringCommaSeparatedForInt'),
          Field(
              'alias',
              SCALAR_STRING,
              is_optional_constructor_arg=True,
              tag_id=5,
              ignorable=IGNORABLE),
      ])

  gen.AddNode(
      name='ResolvedJoinScan',
      tag_id=21,
      parent='ResolvedScan',
      comment="""
      A Scan that joins two input scans.
      The <column_list> will contain columns selected from the union
      of the input scan's <column_lists>.
      When the join is a LEFT/RIGHT/FULL join, ResolvedColumns that came from
      the non-joined side get NULL values.
              """,
      fields=[
          Field(
              'join_type',
              SCALAR_JOIN_TYPE,
              tag_id=2,
              ignorable=IGNORABLE_DEFAULT),
          Field('left_scan', 'ResolvedScan', tag_id=3),
          Field('right_scan', 'ResolvedScan', tag_id=4),
          Field(
              'join_expr',
              'ResolvedExpr',
              tag_id=5,
              ignorable=IGNORABLE_DEFAULT)
      ])

  gen.AddNode(
      name='ResolvedArrayScan',
      tag_id=22,
      parent='ResolvedScan',
      comment="""
      Scan an array value, produced from some expression.

      If input_scan is NULL, this scans the given array value and produces
      one row per array element.  This can occur when using UNNEST(expression).

      If <input_scan> is non-NULL, for each row in the stream produced by
      input_scan, this evaluates the expression <array_expr> (which must return
      an array type) and then produces a stream with one row per array element.

      If <join_expr> is non-NULL, then this condition is evaluated as an ON
      clause for the array join.  The named column produced in <array_expr>
      may be used inside <join_expr>.

      If the array is empty (after evaluating <join_expr>), then
      1. If <is_outer> is false, the scan produces zero rows.
      2. If <is_outer> is true, the scan produces one row with a NULL value for
         the <element_column>.

      <element_column> is the new column produced by this scan that stores the
      array element value for each row.

      If present, <array_offset_column> defines the column produced by this
      scan that stores the array offset (0-based) for the corresponding
      <element_column>.

      This node's column_list can have columns from input_scan, <element_column>
      and <array_offset_column>.
              """,
      fields=[
          Field(
              'input_scan',
              'ResolvedScan',
              tag_id=2,
              ignorable=IGNORABLE_DEFAULT),
          Field('array_expr', 'ResolvedExpr', tag_id=3),
          Field('element_column', SCALAR_RESOLVED_COLUMN, tag_id=4),
          Field(
              'array_offset_column',
              'ResolvedColumnHolder',
              tag_id=5,
              ignorable=IGNORABLE_DEFAULT),
          Field(
              'join_expr',
              'ResolvedExpr',
              tag_id=6,
              ignorable=IGNORABLE_DEFAULT),
          Field(
              'is_outer',
              SCALAR_BOOL,
              tag_id=7,
              ignorable=IGNORABLE_DEFAULT)
      ])

  gen.AddNode(
      name='ResolvedColumnHolder',
      tag_id=23,
      parent='ResolvedArgument',
      comment="""
      This wrapper is used for an optional ResolvedColumn inside another node.
              """,
      fields=[Field('column', SCALAR_RESOLVED_COLUMN, tag_id=2)])

  gen.AddNode(
      name='ResolvedFilterScan',
      tag_id=24,
      parent='ResolvedScan',
      comment="""
      Scan rows from input_scan, and emit all rows where filter_expr
      evaluates to true.  filter_expr is always of type bool.
      This node's column_list will be a subset of input_scan's column_list.
              """,
      fields=[
          Field('input_scan', 'ResolvedScan', tag_id=2),
          Field('filter_expr', 'ResolvedExpr', tag_id=3)
      ])

  gen.AddNode(
      name='ResolvedGroupingCall',
      tag_id=241,
      parent='ResolvedArgument',
      comment="""
      A GROUPING function call. `group_by_column` must be a column from
      the `group_by_list` in `ResolvedAggregateScan`.
      `output_column` is a new column of type int64.
      Its output value is 0 if `group_by_column` is included in the current
      grouping set, or 1 if not.
              """,
      fields=[
          Field('group_by_column', 'ResolvedColumnRef', tag_id=2),
          Field('output_column', SCALAR_RESOLVED_COLUMN, tag_id=3),
      ],
  )

  gen.AddNode(
      name='ResolvedGroupingSetBase',
      tag_id=238,
      parent='ResolvedArgument',
      is_abstract=True,
      comment="""
      The parent node for grouping set, rollup and cube nodes.

      This node exists for organizational purposes only, to cluster
      grouping set, rollup and cube nodes. It doesn't have any actual fields.
              """,
      fields=[],
  )

  gen.AddNode(
      name='ResolvedGroupingSet',
      tag_id=93,
      parent='ResolvedGroupingSetBase',
      comment="""
      List of group by columns that form a grouping set.

      Columns must come from group_by_list in ResolvedAggregateScan.
      group_by_column_list will not contain any duplicates. There may be more
      than one ResolvedGroupingSet in the ResolvedAggregateScan with the same
      columns, however.
              """,
      fields=[
          Field(
              'group_by_column_list', 'ResolvedColumnRef', tag_id=2, vector=True
          )
      ],
  )

  gen.AddNode(
      name='ResolvedRollup',
      tag_id=239,
      parent='ResolvedGroupingSetBase',
      comment="""
      List of ResolvedGroupingSetMultiColumn that forms a rollup.

      Each ResolvedGroupingSetMultiColumn is a list of column references that
      will be batched together when expanding to grouping sets. E.g.
      ROLLUP((a, b), c) is expanded to grouping sets [(a, b, c), (a, b), ()].
      Both (a, b) and c are store in ResolvedGroupingSetMultiColumn for
      convenience.

      Column references in each ResolvedGroupingSetMultiColumn must come from
      group_by_list in ResolvedAggregateScan. It is allowed to have
      duplicated ResolvedGroupingSetMultiColumn in rollup_column_list.
              """,
      fields=[
          Field(
              'rollup_column_list',
              'ResolvedGroupingSetMultiColumn',
              tag_id=2,
              vector=True,
          )
      ],
  )

  gen.AddNode(
      name='ResolvedCube',
      tag_id=240,
      parent='ResolvedGroupingSetBase',
      comment="""
      List of ResolvedGroupingSetMultiColumn that forms a cube in grouping sets.

      See comments in ResolvdRollup for explanation about
      ResolvedGroupingSetMultiColumn.
              """,
      fields=[
          Field(
              'cube_column_list',
              'ResolvedGroupingSetMultiColumn',
              tag_id=2,
              vector=True,
          )
      ],
  )

  gen.AddNode(
      name='ResolvedAggregateScanBase',
      tag_id=111,
      parent='ResolvedScan',
      is_abstract=True,
      comment="""
      Base class for aggregation scans. Apply aggregation to rows produced from
      input_scan, and output aggregated rows.

      Group by keys in <group_by_list>.  If <group_by_list> is empty,
      aggregate all input rows into one output row.

      <collation_list> is either empty to indicate that all the elements in
      <group_by_list> have the default collation, or <collation_list> has the
      same number of elements as <group_by_list>.  Each element is the collation
      for the element in <group_by_list> with the same index, or can be empty to
      indicate default collation or when the type is not collatable.
      <collation_list> is only set when FEATURE_V_1_3_COLLATION_SUPPORT is
      enabled.
      See (broken link).

      Compute all aggregations in <aggregate_list>.  All expressions in
      <aggregate_list> have a ResolvedAggregateFunctionCall with mode
      Function::AGGREGATE as their outermost node.

      The output <column_list> contains only columns produced from
      <group_by_list> and <aggregate_list>.  No other columns are visible after
      aggregation.

      If <grouping_set_list> is empty, output rows grouped by the full
      <group_by_list>.

      If <grouping_set_list> is non-empty, then
      for each item in <grouping_set_list>, output rows computing the
      same <aggregate_list> over the input rows using a particular grouping set.
      Each item in <grouping_set_list> is either a ResolvedGroupingSet,
      ResolvedRollup, or ResolvedCube before the grouping set rewriter expands
      the grouping set list. After rewriting, it will only contain
      ResolvedGroupingSet that are expanded from grouping set, rollup and cube.

      The aggregation input values, including <input_scan>, computed columns in
      <group_by_list>, and aggregate function arguments in <aggregate_list>,
      should be computed just once and then reused as aggregation input for each
      grouping set. (This ensures that ROLLUP rows have correct totals, even
      with non-stable functions in the input.) For each grouping set, the
      <group_by_list> elements not included in the <group_by_column_list> are
      replaced with NULL.

      <rollup_column_list> is the original list of columns from
      GROUP BY ROLLUP(...), if there was a ROLLUP clause, and is used only for
      rebuilding equivalent SQL for the resolved AST. Engines should refer to
      <grouping_set_list> rather than <rollup_column_list>.

      <grouping_call_list> is the list of resolved group-by columns referenced
      in GROUPING aggregate function calls. Each item in the grouping_call_list
      is associated with a unique GROUPING function call in the SELECT list.
              """,
      fields=[
          Field('input_scan', 'ResolvedScan', tag_id=2),
          Field(
              'group_by_list', 'ResolvedComputedColumn', tag_id=3, vector=True
          ),
          Field(
              'collation_list',
              SCALAR_RESOLVED_COLLATION,
              tag_id=5,
              ignorable=IGNORABLE_DEFAULT,
              vector=True,
              java_to_string_method='toStringCommaSeparated',
              is_constructor_arg=False,
          ),
          Field(
              'aggregate_list', 'ResolvedComputedColumn', tag_id=4, vector=True
          ),
          Field(
              'grouping_set_list',
              'ResolvedGroupingSetBase',
              tag_id=6,
              vector=True,
              ignorable=IGNORABLE_DEFAULT,
          ),
          Field(
              'rollup_column_list',
              'ResolvedColumnRef',
              tag_id=7,
              vector=True,
              ignorable=IGNORABLE_DEFAULT,
          ),
          Field(
              'grouping_call_list',
              'ResolvedGroupingCall',
              tag_id=8,
              vector=True,
              ignorable=IGNORABLE_DEFAULT,
              is_optional_constructor_arg=True,
          )
      ],
  )

  gen.AddNode(
      name='ResolvedAggregateScan',
      tag_id=25,
      parent='ResolvedAggregateScanBase',
      comment="""
      Apply aggregation to rows produced from input_scan, and output aggregated
      rows.
              """,
      # ResolvedAggregateScan just needs the fields from the base class
      # `ResolvedAggregationScanBase`.
      fields=[],
  )
  gen.AddNode(
      name='ResolvedAnonymizedAggregateScan',
      tag_id=112,
      parent='ResolvedAggregateScanBase',
      comment="""
      Apply differentially private aggregation (anonymization) to rows produced
      from input_scan, and output anonymized rows.
      Spec: (broken link)

      <k_threshold_expr> when non-null, is a function call that uses one or more
      items from the <aggregate_list> as arguments.
      The engine then adds a filter that acts like:
        HAVING <k_threshold_expr> >= <implementation-defined k-threshold>
      omitting any rows that would not pass this condition.
      TODO: Update this comment after splitting the rewriter out
      into a separate stage.

      <anonymization_option_list> provides user-specified options, and
      requires that option names are one of: delta, epsilon, kappa,
      max_groups_contributed, max_rows_contributed, or k_threshold.

              """,
      fields=[
          Field('k_threshold_expr', 'ResolvedExpr', tag_id=5),
          Field(
              'anonymization_option_list',
              'ResolvedOption',
              tag_id=6,
              vector=True,
              ignorable=IGNORABLE_DEFAULT,
          ),
      ],
  )

  gen.AddNode(
      name='ResolvedDifferentialPrivacyAggregateScan',
      tag_id=221,
      parent='ResolvedAggregateScanBase',
      comment="""
      Apply differentially private aggregation (anonymization) to rows produced
      from input_scan, and output anonymized rows.
      Spec: (broken link)


      <group_selection_threshold_expr> when non-null, is a function call that
      uses one or more items from the <aggregate_list> as arguments.
      The engine then adds a filter that acts like:
        HAVING <group_selection_threshold_expr> >=
        <implementation-defined group_selection_threshold>
      omitting any rows that would not pass this condition.
      TODO: Update this comment after splitting the rewriter out
      into a separate stage.

      <option_list> provides user-specified options, and
      requires that option names are one of: delta, epsilon,
      max_groups_contributed or max_rows_contributed.

              """,
      fields=[
          Field('group_selection_threshold_expr', 'ResolvedExpr', tag_id=5),
          Field(
              'option_list',
              'ResolvedOption',
              tag_id=6,
              vector=True,
              ignorable=IGNORABLE_DEFAULT,
          ),
      ],
  )

  gen.AddNode(
      name='ResolvedAggregationThresholdAggregateScan',
      tag_id=228,
      parent='ResolvedAggregateScanBase',
      comment="""
      Apply aggregation to rows produced from input_scan and output rows to
      that pass aggregation thresholds. It adds:
      HAVING COUNT(DISTINCT `privacy_unit_column`) >= `threshold`.
      Spec: (broken link)

      <option_list> provides user-specified options. Allowed options are defined
      in GetAllowedAggregationThresholdOptions function.
              """,
      fields=[
          Field(
              'option_list',
              'ResolvedOption',
              tag_id=5,
              vector=True,
              ignorable=IGNORABLE_DEFAULT,
          )
      ],
  )

  gen.AddNode(
      name='ResolvedSetOperationItem',
      tag_id=94,
      parent='ResolvedArgument',
      comment="""
      This is one input item in a ResolvedSetOperation.
      The <output_column_list> matches 1:1 with the ResolvedSetOperation's
      <column_list> and specifies how columns from <scan> map to output columns.
      Each column from <scan> can map to zero or more output columns.
              """,
      fields=[
          Field('scan', 'ResolvedScan', tag_id=2),
          Field(
              'output_column_list',
              SCALAR_RESOLVED_COLUMN,
              tag_id=3,
              vector=True)
      ],
  )

  gen.AddNode(
      name='ResolvedSetOperationScan',
      tag_id=26,
      parent='ResolvedScan',
      comment="""
      Apply a set operation (specified by <op_type>) on two or more input scans.

      <scan_list> will have at least two elements.

      <column_list> is a set of new ResolvedColumns created by this scan.
      Each input ResolvedSetOperationItem has an <output_column_list> which
      matches 1:1 with <column_list> and specifies how the input <scan>'s
      columns map into the final <column_list>.

      - Results of {UNION, INTERSECT, EXCEPT} ALL can include duplicate rows.
        More precisely, with two input scans, if a given row R appears exactly
        m times in first input and n times in second input (m >= 0, n >= 0):
        For UNION ALL, R will appear exactly m + n times in the result.
        For INTERSECT ALL, R will appear exactly min(m, n) in the result.
        For EXCEPT ALL, R will appear exactly max(m - n, 0) in the result.

      - Results of {UNION, INTERSECT, EXCEPT} DISTINCT cannot contain any
        duplicate rows. For UNION and INTERSECT, the DISTINCT is computed
        after the result above is computed.  For EXCEPT DISTINCT, row R will
        appear once in the output if m > 0 and n = 0.

      - For n (>2) input scans, the above operations generalize so the output is
        the same as if the inputs were combined incrementally from left to
        right.

      <column_match_mode> represents how columns from different queries were
      matched, for example BY_POSITION or CORRESPONDING (by name). Engines can
      ignore this field; it is included for informational purposes.

      <column_propagation_mode> represents how non-matching columns were
      treated, for example INNER (non-matching columns are dropped) or STRICT
      (non-matching columns are not allowed). Engines can ignore this field;
      it is included for informational purposes.
              """,
      fields=[
          Field('op_type', SCALAR_SET_OPERATION_TYPE, tag_id=2),
          Field(
              'input_item_list',
              'ResolvedSetOperationItem',
              tag_id=4,
              vector=True,
          ),
          Field(
              'column_match_mode',
              SCALAR_SET_OPERATION_COLUMN_MATCH_MODE,
              tag_id=5,
              is_constructor_arg=False,
              ignorable=IGNORABLE,
          ),
          Field(
              'column_propagation_mode',
              SCALAR_SET_OPERATION_COLUMN_PROPAGATION_MODE,
              tag_id=6,
              is_constructor_arg=False,
              ignorable=IGNORABLE,
          ),
      ],
  )

  gen.AddNode(
      name='ResolvedOrderByScan',
      tag_id=27,
      parent='ResolvedScan',
      comment="""
      Apply ordering to rows produced from input_scan, and output ordered
      rows.

      The <order_by_item_list> must not be empty.  Each element identifies
      a sort column and indicates direction (ascending or descending).

      Order Preservation:
        A ResolvedScan produces an ordered output if it has <is_ordered>=true.
        If <is_ordered>=false, the scan may discard order.  This can happen
        even for a ResolvedOrderByScan, if it is the top-level scan in a
        subquery (which discards order).

      The following Scan nodes may have <is_ordered>=true, producing or
      propagating an ordering:
        * ResolvedOrderByScan
        * ResolvedLimitOffsetScan
        * ResolvedProjectScan
        * ResolvedWithScan
      Other Scan nodes will always discard ordering.
              """,
      fields=[
          Field('input_scan', 'ResolvedScan', tag_id=2),
          Field(
              'order_by_item_list',
              'ResolvedOrderByItem',
              tag_id=3,
              vector=True)
      ])

  gen.AddNode(
      name='ResolvedLimitOffsetScan',
      tag_id=28,
      parent='ResolvedScan',
      comment="""
      Apply a LIMIT and optional OFFSET to the rows from input_scan. Emit all
      rows after OFFSET rows have been scanned and up to LIMIT total rows
      emitted. The offset is the number of rows to skip.
      E.g., OFFSET 1 means to skip one row, so the first row emitted will be the
      second ROW, provided the LIMIT is greater than zero.

      The arguments to LIMIT <int64> OFFSET <int64> must be non-negative
      integer literals or (possibly casted) query parameters.  Query
      parameter values must be checked at run-time by ZetaSQL compliant
      backend systems.

      OFFSET is optional and the absence of OFFSET implies OFFSET 0.
              """,
      fields=[
          Field(
              'input_scan', 'ResolvedScan', tag_id=2, propagate_order=True),
          Field('limit', 'ResolvedExpr', tag_id=3),
          Field('offset', 'ResolvedExpr', tag_id=4)
      ])

  gen.AddNode(
      name='ResolvedWithRefScan',
      tag_id=29,
      parent='ResolvedScan',
      comment="""
      Scan the subquery defined in a WITH statement.
      See ResolvedWithScan for more detail.
      The column_list produced here will match 1:1 with the column_list produced
      by the referenced subquery and will given a new unique id to each column
      produced for this scan.
              """,
      fields=[Field('with_query_name', SCALAR_STRING, tag_id=2)])

  gen.AddNode(
      name='ResolvedAnalyticScan',
      tag_id=30,
      parent='ResolvedScan',
      comment="""
      Apply analytic functions to rows produced from input_scan.

      The set of analytic functions are partitioned into a list of analytic
      function groups <function_group_list> by the window PARTITION BY and the
      window ORDER BY.

      The output <column_list> contains all columns from <input_scan>,
      one column per analytic function. It may also conain partitioning/ordering
      expression columns if they reference to select columns.
              """,
      fields=[
          Field('input_scan', 'ResolvedScan', tag_id=2),
          Field(
              'function_group_list',
              'ResolvedAnalyticFunctionGroup',
              tag_id=3,
              vector=True)
      ])

  gen.AddNode(
      name='ResolvedSampleScan',
      tag_id=31,
      parent='ResolvedScan',
      comment="""
      Samples rows from <input_scan>.
      Specs: (broken link)
      Specs for WITH WEIGHT and PARTITION BY: (broken link)

      <method> is the identifier for the sampling algorithm and will always be
      in lowercase.
      For example BERNOULLI, RESERVOIR, SYSTEM. Engines can also support their
      own implementation-specific set of sampling algorithms.

      <size> and <unit> specifies the sample size.
      If <unit> is "ROWS", <size> must be an <int64> and non-negative.
      If <unit> is "PERCENT", <size> must either be a <double> or an <int64> and
      in the range [0, 100].
      <size> can only be a literal value or a (possibly casted) parameter.

      <repeatable_argument> is present if we had a REPEATABLE(<argument>) in the
      TABLESAMPLE clause and can only be a literal value or a (possibly
      casted) parameter.

      If present, <weight_column> defines the column produced by this scan that
      stores the scaling weight for the corresponding sampled row.

      <partition_by_list> can be empty. If <partition_by_list> is not empty,
      <unit> must be ROWS and <method> must be RESERVOIR.
      """,
      fields=[
          Field('input_scan', 'ResolvedScan', tag_id=2),
          Field('method', SCALAR_STRING, tag_id=3),
          Field('size', 'ResolvedExpr', tag_id=4),
          Field('unit', SCALAR_SAMPLE_UNIT, tag_id=5),
          Field('repeatable_argument', 'ResolvedExpr', tag_id=6),
          Field(
              'weight_column',
              'ResolvedColumnHolder',
              tag_id=7,
              ignorable=IGNORABLE_DEFAULT),
          Field(
              'partition_by_list',
              'ResolvedExpr',
              tag_id=8,
              vector=True,
              ignorable=IGNORABLE_DEFAULT),
      ])

  # Other nodes

  gen.AddNode(
      name='ResolvedComputedColumn',
      tag_id=32,
      parent='ResolvedArgument',
      use_custom_debug_string=True,
      comment="""
      This is used when an expression is computed and given a name (a new
      ResolvedColumn) that can be referenced elsewhere.  The new ResolvedColumn
      can appear in a column_list or in ResolvedColumnRefs in other expressions,
      when appropriate.  This node is not an expression itself - it is a
      container that holds an expression.
              """,
      fields=[
          Field(
              'column',
              SCALAR_RESOLVED_COLUMN,
              tag_id=2,
              ignorable=IGNORABLE),
          Field('expr', 'ResolvedExpr', tag_id=3)
      ])

  gen.AddNode(
      name='ResolvedOrderByItem',
      tag_id=33,
      parent='ResolvedArgument',
      comment="""
      This represents one column of an ORDER BY clause, with the requested
      ordering direction.

      <collation_name> is the ORDER BY COLLATE expression, and could be a string
      literal or query parameter.  <collation_name> can only be set when the
      FEATURE_V_1_1_ORDER_BY_COLLATE is enabled.
      See (broken link) for COLLATE clause.
      <collation> (only set when FEATURE_V_1_3_COLLATION_SUPPORT is enabled) is
      the derived collation to use.  It comes from the <column_ref> and COLLATE
      clause.  It is unset if COLLATE is present and set to a parameter.
      See (broken link) for general Collation Support.
      When both features are enabled, if <collation_name> is present and is
      - a parameter, then <collation> is empty
      - a non-parameter, then <collation> is set to the same collation
      An engine which supports both features could read the fields as:
        If <collation> is set then use it, otherwise use <collation_name>, which
        must be a query parameter if set.

      <null_order> indicates the ordering of NULL values relative to non-NULL
      values. NULLS_FIRST indicates that NULLS sort prior to non-NULL values,
      and NULLS_LAST indicates that NULLS sort after non-NULL values.
              """,
      fields=[
          Field('column_ref', 'ResolvedColumnRef', tag_id=2),
          Field(
              'collation_name',
              'ResolvedExpr',
              tag_id=3,
              ignorable=IGNORABLE_DEFAULT),
          Field(
              'is_descending',
              SCALAR_BOOL,
              tag_id=4,
              ignorable=IGNORABLE_DEFAULT),
          Field(
              'null_order',
              SCALAR_NULL_ORDER_MODE,
              tag_id=5,
              ignorable=IGNORABLE_DEFAULT),
          Field(
              'collation',
              SCALAR_RESOLVED_COLLATION,
              tag_id=6,
              ignorable=IGNORABLE_DEFAULT,
              is_constructor_arg=False)
      ])

  gen.AddNode(
      name='ResolvedColumnAnnotations',
      tag_id=104,
      parent='ResolvedArgument',
      comment="""
      This is used in CREATE TABLE statements to provide column annotations
      such as collation, NOT NULL, type parameters, and OPTIONS().

      This class is recursive. It mirrors the structure of the column type
      except that child_list might be truncated.

      For ARRAY:
        If the element or its subfield has annotations, then child_list.size()
        is 1, and child_list(0) stores the element annotations.
        Otherwise child_list is empty.
      For STRUCT:
        If the i-th field has annotations then child_list(i) stores the
        field annotations.
        Otherwise either child_list.size() <= i or child_list(i) is trivial.
        If none of the fields and none of their subfields has annotations, then
        child_list is empty.
      For other types, child_list is empty.
              """,
      fields=[
          Field(
              'collation_name',
              'ResolvedExpr',
              tag_id=6,
              ignorable=IGNORABLE_DEFAULT,
              comment="""
              <collation_name> can only be a string literal, and is only set
              when FEATURE_V_1_3_COLLATION_SUPPORT is enabled. See
              (broken link)."""),
          Field(
              'not_null',
              SCALAR_BOOL,
              tag_id=2,
              ignorable=IGNORABLE_DEFAULT),
          Field(
              'option_list',
              'ResolvedOption',
              tag_id=3,
              vector=True,
              ignorable=IGNORABLE_DEFAULT),
          Field(
              'child_list',
              'ResolvedColumnAnnotations',
              tag_id=4,
              vector=True,
              ignorable=IGNORABLE_DEFAULT),
          Field(
              'type_parameters',
              SCALAR_TYPE_PARAMETERS,
              tag_id=5,
              ignorable=IGNORABLE_DEFAULT,
              comment="""
              child_list in <type_parameters> is not used in here.
              Instead we use child_list of this node (ResolvedColumnAnnotations)
              to store type parameters of subfields of STRUCT or ARRAY. Users
              can access the full type parameters with child_list by calling
              ResolvedColumnDefinition.getFullTypeParameters() function.""")
      ],
      extra_defs="""
        // Get the full TypeParameters object for these annotations given a
        // type, including parameters on nested fields.
        absl::StatusOr<TypeParameters> GetFullTypeParameters(
            const Type* type) const;
      """)

  gen.AddNode(
      name='ResolvedGeneratedColumnInfo',
      tag_id=105,
      parent='ResolvedArgument',
      comment="""

      <expression> indicates the expression that defines the column. The type of
      the expression will always match the type of the column.
        - The <expression> can contain ResolvedColumnRefs corresponding to
        ResolvedColumnDefinition.<column> for any of the
        ResolvedColumnDefinitions in the enclosing statement.
        - The expression can never include a subquery.

      <stored_mode> is the mode of a generated column: Values are:
        - 'NON_STORED': The <expression> must always be evaluated at read time.
        - 'STORED': The <expression> should be pre-emptively computed at write
             time (to save work at read time) and must not call any volatle
             function (e.g. RAND).
        - 'STORED_VOLATILE': The <expression> must be computed at write time and
             may call volatile functions (e.g. RAND).

      `generated_mode` dictates how the generated column is populated. Values
      are:
        - 'ALWAYS' the generated value is always applied to the column,
            meaning users cannot write to the column.
        - 'BY_DEFAULT', the generated value is applied to to the column only if
             the user does not write to the column.
        This field is set to ALWAYS by default.

      See (broken link) and
      (broken link).""",
      fields=[
          Field('expression', 'ResolvedExpr', tag_id=2),
          Field(
              'stored_mode',
              SCALAR_STORED_MODE,
              tag_id=5,
              ignorable=IGNORABLE_DEFAULT,
          ),
          Field(
              'generated_mode',
              SCALAR_GENERATED_MODE,
              tag_id=6,
              ignorable=IGNORABLE_DEFAULT,
          ),
      ],
  )

  gen.AddNode(
      name='ResolvedColumnDefaultValue',
      tag_id=188,
      parent='ResolvedArgument',
      comment="""
      <expression> is the default value expression of the column. The type of
      the expression must be coercible to the column type.
        - <default_value> cannot contain any references to another column.
        - <default_value> cannot include a subquery, aggregation, or window
          function.

      <sql> is the original SQL string for the default value expression.

      Since we can't enforce engines to access at least one of the fields, we
      leave both fields NOT_IGNORABLE to ensure engines access at least one of
      them.
      """,
      fields=[
          Field('expression', 'ResolvedExpr', tag_id=2),
          Field('sql', SCALAR_STRING, tag_id=3),
      ])

  gen.AddNode(
      name='ResolvedColumnDefinition',
      tag_id=91,
      parent='ResolvedArgument',
      comment="""
      This is used in CREATE TABLE statements to provide an explicit column
      definition.

      if <is_hidden> is TRUE, then the column won't show up in SELECT * queries.

      if <generated_column_info> is non-NULL, then this column is a generated
      column.

      if <default_value> is non-NULL, then this column has default value.

      <generated_column_info> and <default_value> cannot both be set at the
      same time.

      <column> defines an ID for the column, which may appear in expressions in
      the PARTITION BY, CLUSTER BY clause or <generated_column_info> if either
      is present.
              """,
      fields=[
          Field('name', SCALAR_STRING, tag_id=2),
          Field('type', SCALAR_TYPE, tag_id=3),
          Field(
              'annotations',
              'ResolvedColumnAnnotations',
              tag_id=4,
              ignorable=IGNORABLE_DEFAULT),
          Field(
              'is_hidden',
              SCALAR_BOOL,
              tag_id=5,
              ignorable=IGNORABLE_DEFAULT),
          Field(
              'column',
              SCALAR_RESOLVED_COLUMN,
              tag_id=6,
              ignorable=IGNORABLE),
          Field(
              'generated_column_info',
              'ResolvedGeneratedColumnInfo',
              tag_id=7,
              ignorable=IGNORABLE_DEFAULT),
          Field(
              'default_value',
              'ResolvedColumnDefaultValue',
              tag_id=9,
              ignorable=IGNORABLE_DEFAULT),
      ],
      extra_defs="""
        // Get the full TypeParameters object for this column, including
        // parameters on nested fields. <annotations.type_parameters> includes
        // only parameters on the outermost type.
        absl::StatusOr<TypeParameters> GetFullTypeParameters() const;
      """)

  gen.AddNode(
      name='ResolvedConstraint',
      tag_id=162,
      parent='ResolvedArgument',
      is_abstract=True,
      comment="""
      Intermediate class for resolved constraints.
              """,
      fields=[])

  gen.AddNode(
      name='ResolvedPrimaryKey',
      tag_id=92,
      parent='ResolvedConstraint',
      comment="""
      This represents the PRIMARY KEY constraint on a table.
      <column_offset_list> provides the offsets of the column definitions that
                           comprise the primary key. This is empty when a
                           0-element primary key is defined or when the altered
                           table does not exist.
      <unenforced> specifies whether the constraint is unenforced.
      <constraint_name> specifies the constraint name, if present
      <column_name_list> provides the column names used in column definitions
                         that comprise the primary key.
      """,
      fields=[
          Field(
              'column_offset_list',
              SCALAR_INT,
              tag_id=2,
              vector=True,
              to_string_method='ToStringCommaSeparated',
              java_to_string_method='toStringCommaSeparatedForInt'),
          Field(
              'option_list',
              'ResolvedOption',
              tag_id=3,
              vector=True,
              ignorable=IGNORABLE_DEFAULT),
          Field(
              'unenforced', SCALAR_BOOL, tag_id=4, ignorable=IGNORABLE_DEFAULT),
          Field(
              'constraint_name',
              SCALAR_STRING,
              ignorable=IGNORABLE_DEFAULT,
              tag_id=5),
          Field(
              'column_name_list',
              SCALAR_STRING,
              tag_id=6,
              vector=True,
              ignorable=IGNORABLE,
              to_string_method='ToStringCommaSeparated',
              java_to_string_method='toStringCommaSeparated'),
      ])

  gen.AddNode(
      name='ResolvedForeignKey',
      tag_id=110,
      parent='ResolvedConstraint',
      comment="""
      This represents the FOREIGN KEY constraint on a table. It is of the form:

        CONSTRAINT <constraint_name>
        FOREIGN KEY <referencing_column_offset_list>
        REFERENCES <referenced_table> <referenced_column_offset_list>
        <match_mode>
        <update_action>
        <delete_action>
        <enforced>
        <option_list>

      <constraint_name> uniquely identifies the constraint.

      <referencing_column_offset_list> provides the offsets of the column
      definitions for the table defining the foreign key.

      <referenced_table> identifies the table this constraint references.

      <referenced_column_offset_list> provides the offsets of the column
      definitions for the table referenced by the foreign key.

      <match_mode> specifies how referencing keys with null values are handled.

      <update_action> specifies what action to take, if any, when a referenced
      value is updated.

      <delete_action> specifies what action to take, if any, when a row with a
      referenced values is deleted.

      <enforced> specifies whether or not the constraint is enforced.

      <option_list> for foreign key table constraints. Empty for foreign key
      column attributes (see instead ResolvedColumnAnnotations).

      <referencing_column_list> provides the names for the foreign key's
      referencing columns.
      """,
      fields=[
          Field('constraint_name', SCALAR_STRING, tag_id=2),
          Field(
              'referencing_column_offset_list',
              SCALAR_INT,
              tag_id=3,
              vector=True,
              to_string_method='ToStringCommaSeparated',
              java_to_string_method='toStringCommaSeparatedForInt'),
          Field('referenced_table', SCALAR_TABLE, tag_id=4),
          Field(
              'referenced_column_offset_list',
              SCALAR_INT,
              tag_id=5,
              vector=True,
              to_string_method='ToStringCommaSeparated',
              java_to_string_method='toStringCommaSeparatedForInt'),
          Field('match_mode', SCALAR_FOREIGN_KEY_MATCH_MODE, tag_id=6),
          Field('update_action', SCALAR_FOREIGN_KEY_ACTION_OPERATION, tag_id=7),
          Field('delete_action', SCALAR_FOREIGN_KEY_ACTION_OPERATION, tag_id=8),
          Field('enforced', SCALAR_BOOL, tag_id=9),
          Field('option_list', 'ResolvedOption', tag_id=10, vector=True),
          Field(
              'referencing_column_list',
              SCALAR_STRING,
              tag_id=11,
              vector=True,
              ignorable=IGNORABLE)
      ])

  gen.AddNode(
      name='ResolvedCheckConstraint',
      tag_id=113,
      parent='ResolvedConstraint',
      comment="""
      This represents the ABSL_CHECK constraint on a table. It is of the form:

        CONSTRAINT <constraint_name>
        ABSL_CHECK <expression>
        <enforced>
        <option_list>

      <constraint_name> uniquely identifies the constraint.

      <expression> defines a boolean expression to be evaluated when the row is
      updated. If the result is FALSE, update to the row is not allowed.

      <enforced> specifies whether or not the constraint is enforced.

      <option_list> list of options for check constraint.

      See (broken link).
      """,
      fields=[
          Field('constraint_name', SCALAR_STRING, tag_id=2),
          Field(
              'expression',
              'ResolvedExpr',
              tag_id=3,
          ),
          Field('enforced', SCALAR_BOOL, tag_id=4),
          Field('option_list', 'ResolvedOption', tag_id=5, vector=True)
      ])

  gen.AddNode(
      name='ResolvedOutputColumn',
      tag_id=34,
      parent='ResolvedArgument',
      use_custom_debug_string=True,
      comment="""
      This is used in ResolvedQueryStmt to provide a user-visible name
      for each output column.
              """,
      fields=[
          Field('name', SCALAR_STRING, tag_id=2, ignorable=IGNORABLE),
          Field('column', SCALAR_RESOLVED_COLUMN, tag_id=3)
      ])

  gen.AddNode(
      name='ResolvedProjectScan',
      tag_id=35,
      parent='ResolvedScan',
      comment="""
      A Project node computes new expression values, and possibly drops
      columns from the input Scan's column_list.

      Each entry in <expr_list> is a new column computed from an expression.

      The column_list can include any columns from input_scan, plus these
      newly computed columns.

      NOTE: This scan will propagate the is_ordered property of <input_scan>
      by default.  To make this scan unordered, call set_is_ordered(false).
              """,
      fields=[
          Field(
              'expr_list', 'ResolvedComputedColumn', tag_id=2, vector=True),
          Field(
              'input_scan', 'ResolvedScan', tag_id=3, propagate_order=True)
      ])

  gen.AddNode(
      name='ResolvedTVFScan',
      tag_id=81,
      parent='ResolvedScan',
      emit_default_constructor=False,
      comment="""
      This scan represents a call to a table-valued function (TVF). Each TVF
      returns an entire output relation instead of a single scalar value. The
      enclosing query may refer to the TVF as if it were a table subquery. The
      TVF may accept scalar arguments and/or other input relations.

      Scalar arguments work the same way as arguments for non-table-valued
      functions: in the resolved AST, their types are equal to the required
      argument types specified in the function signature.

      The function signature may also include relation arguments, and any such
      relation argument may specify a required schema. If such a required schema
      is present, then in the resolved AST, the ResolvedScan for each relational
      ResolvedFunctionArgument is guaranteed to have the same number of columns
      as the required schema, and the provided columns match position-wise with
      the required columns. Each provided column has the same name and type as
      the corresponding required column.

      If AnalyzerOptions::prune_unused_columns is true, the <column_list> and
      <column_index_list> will include only columns that were referenced
      in the user query. (SELECT * counts as referencing all columns.)
      Pruning has no effect on value tables (the value is never pruned).

      <column_list> is a set of new ResolvedColumns created by this scan.
      The <column_list>[i] should be matched to the related TVFScan's output
      relation column by
      <signature>.result_schema().column(<column_index_list>[i]).

      <tvf> The TableValuedFunction entry that the catalog returned for this TVF
            scan. Contains non-concrete function signatures which may include
            arguments with templated types.
      <signature> The concrete table function signature for this TVF call,
                  including the types of all scalar arguments and the
                  number and types of columns of all table-valued
                  arguments. An engine may also subclass this object to
                  provide extra custom information and return an instance
                  of the subclass from the TableValuedFunction::Resolve
                  method.
      <argument_list> The vector of resolved concrete arguments for this TVF
                      call, including the default values or NULLs injected for
                      the omitted arguments (Note the NULL injection is a
                      temporary solution to handle omitted named arguments. This
                      is subject to change by upcoming CLs).

      <column_index_list> This list matches 1-1 with the <column_list>, and
      identifies the index of the corresponding column in the <signature>'s
      result relation column list.

      <alias> The AS alias for the scan, or empty if none.
      <function_call_signature> The FunctionSignature object from the
                                <tvf->signatures()> list that matched the
                                current call. The TVFScan's
                                <FunctionSignature::ConcreteArgument> list
                                matches 1:1 to <argument_list>, while its
                                <FunctionSignature::arguments> list still has
                                the full argument list.
                                The analyzer only sets this field when
                                it could be ambiguous for an engine to figure
                                out the actual arguments provided, e.g., when
                                there are arguments omitted from the call. When
                                it is provided, engines may use this object to
                                check for the argument names and omitted
                                arguments. SQLBuilder may also need this object
                                in cases when the named argument notation is
                                required for this call.
              """,
      fields=[
          Field('tvf', TABLE_VALUED_FUNCTION, tag_id=2),
          Field('signature', TVF_SIGNATURE, tag_id=3),
          Field(
              'argument_list',
              'ResolvedFunctionArgument',
              tag_id=5,
              vector=True),
          Field(
              'column_index_list',
              SCALAR_INT,
              tag_id=8,
              ignorable=IGNORABLE,
              vector=True,
              is_constructor_arg=True,
              to_string_method='ToStringCommaSeparated',
              java_to_string_method='toStringCommaSeparatedForInt'),
          Field('alias', SCALAR_STRING, tag_id=6, ignorable=IGNORABLE),
          Field(
              'function_call_signature',
              SCALAR_FUNCTION_SIGNATURE_PTR,
              tag_id=7,
              ignorable=IGNORABLE_DEFAULT,
              is_optional_constructor_arg=True)
      ])

  gen.AddNode(
      name='ResolvedGroupRowsScan',
      tag_id=176,
      parent='ResolvedScan',
      emit_default_constructor=False,
      comment="""
      ResolvedGroupRowsScan represents a call to a special TVF GROUP_ROWS().
      It can only show up inside WITH GROUP_ROWS clause, which is resolved as
      the field with_group_rows_subquery in ResolvedNonScalarFunctionCallBase
      ResolvedGroupRowsScan. This scan produces rows corresponding to the input
      of ResolvedAggregateScan that belong to the current group.

      <input_column_list> is a list of new columns created to store values
      coming from the input of the aggregate scan. ResolvedComputedColumn can
      only hold ResolvedColumnRef's and can reference anything from the
      pre-aggregation scan.

      <alias> is the alias of the scan or empty if none.
              """,
      fields=[
          Field(
              'input_column_list',
              'ResolvedComputedColumn',
              tag_id=2,
              vector=True),
          Field('alias', SCALAR_STRING, tag_id=3, ignorable=IGNORABLE),
      ])

  gen.AddNode(
      name='ResolvedFunctionArgument',
      tag_id=82,
      parent='ResolvedArgument',
      comment="""
      This represents a generic argument to a function. The argument can be
      semantically an expression, relation, model, connection descriptor, or
      sequence.

      The following fields are mutally exclusive:
      * `expr` represents a scalar function argument.
      * `scan` represents a table-typed argument.
      * `model` represents a ML model function argument.
      * `connection` represents a connection object function argument.
      * `descriptor_arg` represents a descriptor object function argument.
      * `inline_lambda` represents a lambda function argument.
      * `sequence` represents a sequence object function argument.

      This node could be used in multiple places:
      * ResolvedTVFScan supports all of these.
      * ResolvedFunctionCall supports `expr`, `inline_lambda`, and `sequence`.
      * ResolvedCallStmt supports only `expr`.

      If the argument has type `scan`, `argument_column_list` maps columns from
      `scan` into specific columns of the argument's input schema, matching
      those columns positionally. i.e. `scan`'s column_list may have fewer
      columns or out-of-order columns, and this vector maps those columns into
      specific input columns.

      Some arguments may also have an alias, stored in the `argument_alias`
      field, which is not mutally exclusive with the fields above.
              """,
      fields=[
          Field('expr', 'ResolvedExpr', ignorable=IGNORABLE_DEFAULT, tag_id=2),
          Field('scan', 'ResolvedScan', ignorable=IGNORABLE_DEFAULT, tag_id=3),
          Field(
              'model', 'ResolvedModel', ignorable=IGNORABLE_DEFAULT, tag_id=5
          ),
          Field(
              'connection',
              'ResolvedConnection',
              ignorable=IGNORABLE_DEFAULT,
              tag_id=6,
          ),
          Field(
              # Can't name it 'descriptor' because of conflict in protos.
              'descriptor_arg',
              'ResolvedDescriptor',
              ignorable=IGNORABLE_DEFAULT,
              tag_id=7,
          ),
          Field(
              'argument_column_list',
              SCALAR_RESOLVED_COLUMN,
              tag_id=4,
              vector=True,
              ignorable=IGNORABLE_DEFAULT,
          ),
          Field(
              'inline_lambda',
              'ResolvedInlineLambda',
              tag_id=8,
              ignorable=IGNORABLE_DEFAULT,
          ),
          Field(
              'sequence',
              'ResolvedSequence',
              ignorable=IGNORABLE_DEFAULT,
              tag_id=9,
          ),
          Field(
              'argument_alias',
              SCALAR_STRING,
              tag_id=10,
              ignorable=IGNORABLE_DEFAULT,
              is_optional_constructor_arg=True,
              comment="""
              Stores the alias of the argument, either provided by the user or
              generated by the resolver. This can only be populated if allowed
              by `FunctionArgumentTypeOptions::argument_alias_kind`.

              An argument alias is an identifier associated with a function
              argument in the form of F(<arg> AS <alias>), where <alias> is the
              argument alias for the function argument <arg>.

              Examples include
                * STRUCT(1 AS x, 2 AS y)
                * ARRAY_ZIP(arr1 AS a, arr2 AS b)
              where the argument alias is used as a field name in an output
              STRUCT value. For dynamic types like JSON, these aliases may be
              used at run-time.

              This field will be empty if the argument does not support aliases,
              or an alias could not be inferred.

              The current implementation only allows an argument to have an
              alias if its type is `expr`, but the support may be extended to
              other types, e.g. `scan` or `model` in the future.
              """,
          ),
      ],
  )

  gen.AddNode(
      name='ResolvedStatement',
      tag_id=36,
      parent='ResolvedNode',
      is_abstract=True,
      comment="""
      The superclass of all ZetaSQL statements.
              """,
      fields=[
          Field(
              'hint_list',
              'ResolvedOption',
              tag_id=2,
              ignorable=IGNORABLE,
              is_constructor_arg=False,
              vector=True)
      ],
      extra_defs="""bool IsStatement() const final { return true; }""")

  gen.AddNode(
      name='ResolvedExplainStmt',
      tag_id=37,
      parent='ResolvedStatement',
      comment="""
      An Explain statement. This is always the root of a statement hierarchy.
      Its child may be any statement type except another ResolvedExplainStmt.

      It is implementation dependent what action a back end system takes for an
      ExplainStatement.
              """,
      fields=[Field('statement', 'ResolvedStatement', tag_id=2)])

  gen.AddNode(
      name='ResolvedQueryStmt',
      tag_id=38,
      parent='ResolvedStatement',
      comment="""
      A SQL query statement.  This is the outermost query statement that runs
      and produces rows of output, like a SELECT.  (The contained query may be
      a Scan corresponding to a non-Select top-level operation like UNION ALL
      or WITH.)

      <output_column_list> gives the user-visible column names that should be
      returned in the API or query tools.  There may be duplicate names, and
      multiple output columns may reference the same column from <query>.
              """,
      fields=[
          Field(
              'output_column_list',
              'ResolvedOutputColumn',
              tag_id=2,
              vector=True),
          Field(
              'is_value_table',
              SCALAR_BOOL,
              tag_id=3,
              ignorable=IGNORABLE_DEFAULT,
              comment="""
              If true, the result of this query is a value table. Rather than
              producing rows with named columns, it produces rows with a single
              unnamed value type.  output_column_list will have exactly one
              column, with an empty name. See (broken link).
                      """),
          Field('query', 'ResolvedScan', tag_id=4)
      ])

  gen.AddNode(
      name='ResolvedCreateDatabaseStmt',
      tag_id=95,
      parent='ResolvedStatement',
      comment="""
      This statement:
        CREATE DATABASE <name> [OPTIONS (...)]
      <name_path> is a vector giving the identifier path in the database name.
      <option_list> specifies the options of the database.
              """,
      fields=[
          Field('name_path', SCALAR_STRING, tag_id=2, vector=True),
          Field(
              'option_list',
              'ResolvedOption',
              tag_id=3,
              vector=True,
              ignorable=IGNORABLE_DEFAULT)
      ])

  gen.AddNode(
      name='ResolvedCreateStatement',
      tag_id=39,
      parent='ResolvedStatement',
      is_abstract=True,
      comment="""
      Common superclass for CREATE statements with standard modifiers like
              CREATE [OR REPLACE] [TEMP|TEMPORARY|PUBLIC|PRIVATE] <object type>
              [IF NOT EXISTS] <name> ...

      <name_path> is a vector giving the identifier path in the table name.
      <create_scope> is the relevant scope, i.e., DEFAULT, TEMP, PUBLIC,
                     or PRIVATE.  PUBLIC/PRIVATE are only valid in module
                     resolution context, see (broken link)
                     for details.
      <create_mode> indicates if this was CREATE, CREATE OR REPLACE, or
                    CREATE IF NOT EXISTS.
              """,
      fields=[
          Field(
              'name_path',
              SCALAR_STRING,
              tag_id=2,
              vector=True,
              ignorable=IGNORABLE_DEFAULT),
          Field(
              'create_scope',
              SCALAR_CREATE_SCOPE,
              tag_id=5,
              ignorable=IGNORABLE_DEFAULT),
          Field(
              'create_mode',
              SCALAR_CREATE_MODE,
              tag_id=4,
              ignorable=IGNORABLE_DEFAULT)
      ])

  gen.AddNode(
      name='ResolvedIndexItem',
      tag_id=96,
      parent='ResolvedArgument',
      comment="""
      Represents one of indexed items in CREATE INDEX statement, with the
      ordering direction specified.
              """,
      fields=[
          Field('column_ref', 'ResolvedColumnRef', tag_id=2),
          Field('descending', SCALAR_BOOL, tag_id=3)
      ])

  gen.AddNode(
      name='ResolvedUnnestItem',
      tag_id=126,
      parent='ResolvedArgument',
      comment="""
      This is used in CREATE INDEX STMT to represent the unnest operation
      performed on the base table. The produced element columns or array offset
      columns (optional) can appear in other ResolvedUnnestItem or index keys.

      <array_expr> is the expression of the array field, e.g., t.array_field.
      <element_column> is the new column produced by this unnest item that
                       stores the array element value for each row.
      <array_offset_column> is optional. If present, it defines the column
                            produced by this unnest item that stores the array
                            offset (0-based) for the corresponding
                            <element_column>.
              """,
      fields=[
          Field('array_expr', 'ResolvedExpr', tag_id=2),
          Field('element_column', SCALAR_RESOLVED_COLUMN, tag_id=3),
          Field(
              'array_offset_column',
              'ResolvedColumnHolder',
              tag_id=4,
              ignorable=IGNORABLE_DEFAULT),
      ])

  gen.AddNode(
      name='ResolvedCreateIndexStmt',
      tag_id=97,
      parent='ResolvedCreateStatement',
      comment="""
      This statement:
      CREATE [OR REPLACE] [UNIQUE] [SEARCH | VECTOR] INDEX [IF NOT EXISTS]
       <index_name_path> ON <table_name_path>
      [STORING (Expression, ...)]
      [UNNEST(path_expression) [[AS] alias] [WITH OFFSET [[AS] alias]], ...]
      (path_expression [ASC|DESC], ...) [OPTIONS (name=value, ...)];

      <table_name_path> is the name of table being indexed.
      <table_scan> is a TableScan on the table being indexed.
      <is_unique> specifies if the index has unique entries.
      <is_search> specifies if the index is for search. It is mutually exclusive
                  with is_vector.
      <is_vector> specifies if the index is for vector search. It is mutually
                  exclusive with is_search.
      <index_all_columns> specifies if indexing all the columns of the table.
                          When this field is true, index_item_list must be
                          empty and is_search must be true.
      <index_item_list> has the columns being indexed, specified as references
                        to 'computed_columns_list' entries or the columns of
                        'table_scan'.
      <storing_expression_list> has the expressions in the storing clause.
      <option_list> has engine-specific directives for how and where to
                    materialize this index.
      <computed_columns_list> has computed columns derived from the columns of
                              'table_scan' or 'unnest_expressions_list'. For
                              example, the extracted field (e.g., x.y.z).
      <unnest_expressions_list> has unnest expressions derived from
                                'table_scan' or previous unnest expressions in
                                the list. So the list order is significant.
              """,
      fields=[
          Field(
              'table_name_path', SCALAR_STRING, tag_id=2, vector=True),
          Field(
              'table_scan',
              'ResolvedTableScan',
              tag_id=3,
              ignorable=IGNORABLE),
          Field('is_unique', SCALAR_BOOL, tag_id=4),
          # TODO: Replace is_search and is_vector with an enum.
          Field(
              'is_search',
              SCALAR_BOOL,
              tag_id=10,
              is_optional_constructor_arg=True,
              ignorable=IGNORABLE_DEFAULT),
          Field(
              'is_vector',
              SCALAR_BOOL,
              tag_id=12,
              is_optional_constructor_arg=True,
              ignorable=IGNORABLE_DEFAULT),
          Field(
              'index_all_columns',
              SCALAR_BOOL,
              tag_id=11,
              is_optional_constructor_arg=True,
              ignorable=IGNORABLE_DEFAULT),
          Field(
              'index_item_list',
              'ResolvedIndexItem',
              tag_id=5,
              vector=True),
          Field(
              'storing_expression_list',
              'ResolvedExpr',
              tag_id=9,
              vector=True,
              ignorable=IGNORABLE_DEFAULT),
          Field(
              'option_list',
              'ResolvedOption',
              tag_id=6,
              vector=True,
              ignorable=IGNORABLE_DEFAULT),
          Field(
              'computed_columns_list',
              'ResolvedComputedColumn',
              tag_id=7,
              vector=True,
              ignorable=IGNORABLE_DEFAULT),
          Field(
              'unnest_expressions_list',
              'ResolvedUnnestItem',
              tag_id=8,
              vector=True,
              ignorable=IGNORABLE_DEFAULT),
      ])

  gen.AddNode(
      name='ResolvedCreateSchemaStmt',
      tag_id=157,
      parent='ResolvedCreateStatement',
      comment="""
      This statement:
        CREATE [OR REPLACE] SCHEMA [IF NOT EXISTS] <name>
        [DEFAULT COLLATE <collation>]
        [OPTIONS (name=value, ...)]

      <option_list> engine-specific options.
      <collation_name> specifies the default collation specification for future
        tables created in the dataset. If a table is created in this dataset
        without specifying table-level default collation, it inherits the
        dataset default collation. A change to this field affects only tables
        created afterwards, not the existing tables. Only string literals
        are allowed for this field.

        Note: If a table being created in this schema does not specify table
        default collation, the engine should copy the dataset default collation
        to the table as the table default collation.
              """,
      fields=[
          Field(
              'collation_name',
              'ResolvedExpr',
              tag_id=3,
              ignorable=IGNORABLE_DEFAULT),
          Field(
              'option_list',
              'ResolvedOption',
              tag_id=2,
              vector=True,
              ignorable=IGNORABLE_DEFAULT)
      ])

  gen.AddNode(
      name='ResolvedCreateTableStmtBase',
      tag_id=106,
      parent='ResolvedCreateStatement',
      is_abstract=True,
      comment="""
      This statement:
        CREATE [TEMP] TABLE <name> [(column type, ...) | LIKE <name_path>]
        [DEFAULT COLLATE <collation>] [PARTITION BY expr, ...]
        [CLUSTER BY expr, ...] [OPTIONS (...)]

      <option_list> has engine-specific directives for how and where to
                    materialize this table.
      <column_definition_list> has the names and types of the columns in the
                               created table. If <is_value_table> is true, it
                               must contain exactly one column, with a generated
                               name such as "$struct".
      <pseudo_column_list> is a list of some pseudo-columns expected to be
                           present on the created table (provided by
                           AnalyzerOptions::SetDdlPseudoColumns*).  These can be
                           referenced in expressions in <partition_by_list> and
                           <cluster_by_list>.
      <primary_key> specifies the PRIMARY KEY constraint on the table, it is
                    nullptr when no PRIMARY KEY is specified.
      <foreign_key_list> specifies the FOREIGN KEY constraints on the table.
      <check_constraint_list> specifies the ABSL_CHECK constraints on the table.
      <partition_by_list> specifies the partitioning expressions for the table.
      <cluster_by_list> specifies the clustering expressions for the table.
      TODO: Return error when the PARTITION BY / CLUSTER BY
      expression resolves to have collation specified.
      <is_value_table> specifies whether the table is a value table.
                       See (broken link).
      <like_table> identifies the table in the LIKE <name_path>.
                   By default, all fields (column names, types, constraints,
                   keys, clustering etc.) will be inherited from the source
                   table. But if explicitly set, the explicit settings will
                   take precedence.
      <collation_name> specifies the default collation specification to apply to
        newly added STRING fields in this table. A change of this field affects
        only the STRING columns and the STRING fields in STRUCTs added
        afterwards, not existing columns. Only string literals are allowed for
        this field.

        Note: During table creation or alteration, if a STRING field is added to
        this table without explicit collation specified, the engine should copy
        the table default collation to the STRING field.
              """,
      fields=[
          Field(
              'option_list',
              'ResolvedOption',
              tag_id=2,
              vector=True,
              ignorable=IGNORABLE_DEFAULT),
          Field(
              'column_definition_list',
              'ResolvedColumnDefinition',
              tag_id=3,
              vector=True,
              # This is ignorable for backwards compatibility.
              ignorable=IGNORABLE),
          Field(
              'pseudo_column_list',
              SCALAR_RESOLVED_COLUMN,
              tag_id=7,
              vector=True,
              ignorable=IGNORABLE),
          Field(
              'primary_key',
              'ResolvedPrimaryKey',
              tag_id=4,
              ignorable=IGNORABLE_DEFAULT),
          Field(
              'foreign_key_list',
              'ResolvedForeignKey',
              tag_id=9,
              vector=True,
              ignorable=IGNORABLE_DEFAULT),
          Field(
              'check_constraint_list',
              'ResolvedCheckConstraint',
              tag_id=10,
              vector=True,
              ignorable=IGNORABLE_DEFAULT),
          Field(
              'is_value_table',
              SCALAR_BOOL,
              tag_id=8,
              ignorable=IGNORABLE_DEFAULT),
          Field(
              'like_table',
              SCALAR_TABLE,
              tag_id=11,
              ignorable=IGNORABLE_DEFAULT),
          Field(
              'collation_name',
              'ResolvedExpr',
              tag_id=12,
              ignorable=IGNORABLE_DEFAULT),
          Field(
              'connection',
              'ResolvedConnection',
              tag_id=13,
              ignorable=IGNORABLE_DEFAULT)
      ])

  gen.AddNode(
      name='ResolvedCreateTableStmt',
      tag_id=90,
      parent='ResolvedCreateTableStmtBase',
      comment="""
      This statement:
        CREATE [TEMP] TABLE <name>
        [(column schema, ...) | LIKE <name_path> |
            {CLONE|COPY} <name_path>
                [FOR SYSTEM_TIME AS OF <time_expr>]
                [WHERE <where_clause>]]
        [DEFAULT COLLATE <collation_name>]
        [PARTITION BY expr, ...] [CLUSTER BY expr, ...]
        [WITH CONNECTION connection_name]
        [OPTIONS (...)]

      One of <clone_from> or <copy_from> can be present for CLONE or COPY.
        <clone_from> specifes the data source to clone from (cheap, typically
        O(1) operation); while <copy_from> is intended for a full copy.

        ResolvedTableScan will represent the source table, with an optional
        for_system_time_expr.

        The ResolvedTableScan may be wrapped inside a ResolvedFilterScan if the
        source table has a where clause. No other Scan types are allowed here.

        If the OPTIONS clause is explicitly specified, the option values are
        intended to be used for the created or replaced table.
        If any OPTION is unspecified, the corresponding option from the source
        table will be used instead.

        The 'clone_from.column_list' field may be set, but should be ignored.

        clone_from and copy_from cannot be value tables.
              """,
      fields=[
          Field(
              'clone_from',
              'ResolvedScan',
              tag_id=7,
              ignorable=IGNORABLE_DEFAULT),
          Field(
              'copy_from',
              'ResolvedScan',
              tag_id=8,
              ignorable=IGNORABLE_DEFAULT),
          Field(
              'partition_by_list',
              'ResolvedExpr',
              tag_id=5,
              vector=True,
              ignorable=IGNORABLE_DEFAULT),
          Field(
              'cluster_by_list',
              'ResolvedExpr',
              tag_id=6,
              vector=True,
              ignorable=IGNORABLE_DEFAULT),
      ])

  gen.AddNode(
      name='ResolvedCreateTableAsSelectStmt',
      tag_id=40,
      parent='ResolvedCreateTableStmtBase',
      comment="""
      This statement:
        CREATE [TEMP] TABLE <name> [(column schema, ...) | LIKE <name_path>]
        [DEFAULT COLLATE <collation_name>] [PARTITION BY expr, ...]
        [CLUSTER BY expr, ...]
        [WITH CONNECTION connection_name]
        [OPTIONS (...)]
        AS SELECT ...

      The <output_column_list> matches 1:1 with the <column_definition_list> in
      ResolvedCreateTableStmtBase, and maps ResolvedColumns produced by <query>
      into specific columns of the created table.  The output column names and
      types must match the column definition names and types.  If the table is
      a value table, <output_column_list> must have exactly one column, with a
      generated name such as "$struct".

      <output_column_list> does not contain all table schema information that
      <column_definition_list> does. For example, NOT NULL annotations, column
      OPTIONS, and primary keys are only available in <column_definition_list>.
      Consumers are encouraged to read from <column_definition_list> rather
      than than <output_column_list> to determine the table schema, if possible.

      <query> is the query to run.
              """,
      fields=[
          Field(
              'partition_by_list',
              'ResolvedExpr',
              tag_id=5,
              vector=True,
              ignorable=IGNORABLE_DEFAULT),
          Field(
              'cluster_by_list',
              'ResolvedExpr',
              tag_id=6,
              vector=True,
              ignorable=IGNORABLE_DEFAULT),
          Field(
              'output_column_list',
              'ResolvedOutputColumn',
              tag_id=2,
              vector=True),
          Field('query', 'ResolvedScan', tag_id=3)
      ])

  gen.AddNode(
      name='ResolvedCreateModelAliasedQuery',
      tag_id=226,
      parent='ResolvedArgument',
      comment="""
      Resolves a create model aliased query:
        identifier AS <query>

        <alias> is the string representation of identifier.
        <query> is the ResolvedScan of the subquery.
        <output_column_list> matches 1:1 with the <query>'s column_list and
        identifies the names and types of the columns output from the query.
      """,
      fields=[
          Field('alias', SCALAR_STRING, tag_id=2),
          Field('query', 'ResolvedScan', tag_id=3),
          Field(
              'output_column_list',
              'ResolvedOutputColumn',
              tag_id=4,
              vector=True,
          ),
      ],
  )

  gen.AddNode(
      name='ResolvedCreateModelStmt',
      tag_id=107,
      parent='ResolvedCreateStatement',
      comment="""
      This statement:
        CREATE [TEMP] MODEL <name> [INPUT(...) OUTPUT(...)] [TRANSFORM(...)]
        [REMOTE [WITH CONNECTION ...]] [OPTIONS (...)]
        [AS <query> | (<identifier> AS (<query>) [, ...])]

      At most one of <query> and <aliased_query_list> can be set. <query> is
      used when there is a single query as input. <aliased_query_list> is used
      for the syntax with multiple queries with identifiers. For below, let
      <has_query> be true if either of these are present.

      Models can be evaluated either locally or remotely. Orthogonally, they can
      be either trained using SQL or come from external source. Depending on
      these properties, different clauses are expected to be present.

      * Local models <is_remote> = FALSE
        * Trained: <has_query>
        * External: !<has_query>
      * Remote models <is_remote> = TRUE
        * Trained: <has_query> [Not supported yet]
        * External: !<has_query>

      <option_list> has engine-specific directives for how to train this model.
      <query> is the AS SELECT statement. It can be only set when <is_remote> is
        false and all of <input_column_definition_list>,
        <output_column_definition_list> and <aliased_query_list> are empty.
      TODO: consider rename to <query_output_column_list>.
      <output_column_list> matches 1:1 with the <query>'s column_list and
        identifies the names and types of the columns output from the select
        statement. Set only when <query> is present.
      <input_column_definition_list> contains names and types of model's input
        columns. Cannot be set if <has_query> is true. Might be absent when
        <is_remote> is true, meaning schema is read from the remote model
        itself.
      <output_column_definition_list> contains names and types of model's output
        columns. Cannot be set if <has_query> is true. Might be absent when
        <is_remote> is true, meaning schema is read from the remote model
        itself.
      <is_remote> is true if this is a remote model. Cannot be set when
        <has_query> is true.
      <connection> is the identifier path of the connection object. It can be
        only set when <is_remote> is true.
      <transform_list> is the list of ResolvedComputedColumn in TRANSFORM
        clause. It can be only set when <query> is present.
      <transform_input_column_list> introduces new ResolvedColumns that have the
        same names and types of the columns in the <output_column_list>. The
        transform expressions resolve against these ResolvedColumns. It's only
        set when <transform_list> is non-empty.
      <transform_output_column_list> matches 1:1 with <transform_list> output.
        It records the names of the output columns from TRANSFORM clause.
      <transform_analytic_function_group_list> is the list of
        AnalyticFunctionGroup for analytic functions inside TRANSFORM clause.
        It records the input expression of the analytic functions. It can
        see all the columns from <transform_input_column_list>. The only valid
        group is for the full, unbounded window generated from empty OVER()
        clause.
        For example, CREATE MODEL statement
        "create model Z
          transform (max(c) over() as d)
          options ()
          as select 1 c, 2 b;"
        will generate transform_analytic_function_group_list:
        +-transform_analytic_function_group_list=
          +-AnalyticFunctionGroup
            +-analytic_function_list=
              +-d#5 :=
                +-AnalyticFunctionCall(ZetaSQL:max(INT64) -> INT64)
                  +-ColumnRef(type=INT64, column=Z.c#3)
                  +-window_frame=
                    +-WindowFrame(frame_unit=ROWS)
                      +-start_expr=
                      | +-WindowFrameExpr(boundary_type=UNBOUNDED PRECEDING)
                      +-end_expr=
                        +-WindowFrameExpr(boundary_type=UNBOUNDED FOLLOWING)
      <aliased_query_list> is the aliased query list. It can only be set when
        <is_remote> is false and <query> is null. It cannot coexist with any
        transform related resolved nodes.
              """,
      fields=[
          Field(
              'option_list',
              'ResolvedOption',
              tag_id=2,
              vector=True,
              ignorable=IGNORABLE_DEFAULT,
          ),
          Field(
              'output_column_list',
              'ResolvedOutputColumn',
              tag_id=3,
              vector=True,
              ignorable=IGNORABLE_DEFAULT,
          ),
          Field('query', 'ResolvedScan', tag_id=4),
          Field(
              'aliased_query_list',
              'ResolvedCreateModelAliasedQuery',
              tag_id=13,
              vector=True,
              ignorable=IGNORABLE_DEFAULT,
          ),
          Field(
              'transform_input_column_list',
              'ResolvedColumnDefinition',
              tag_id=8,
              vector=True,
              ignorable=IGNORABLE_DEFAULT,
          ),
          Field(
              'transform_list',
              'ResolvedComputedColumn',
              tag_id=5,
              vector=True,
              ignorable=IGNORABLE_DEFAULT,
          ),
          Field(
              'transform_output_column_list',
              'ResolvedOutputColumn',
              tag_id=6,
              vector=True,
              ignorable=IGNORABLE_DEFAULT,
          ),
          Field(
              'transform_analytic_function_group_list',
              'ResolvedAnalyticFunctionGroup',
              tag_id=7,
              vector=True,
              ignorable=IGNORABLE_DEFAULT,
          ),
          Field(
              'input_column_definition_list',
              'ResolvedColumnDefinition',
              tag_id=9,
              vector=True,
              ignorable=IGNORABLE_DEFAULT,
          ),
          Field(
              'output_column_definition_list',
              'ResolvedColumnDefinition',
              tag_id=10,
              vector=True,
              ignorable=IGNORABLE_DEFAULT,
          ),
          Field(
              'is_remote', SCALAR_BOOL, tag_id=11, ignorable=IGNORABLE_DEFAULT
          ),
          Field(
              'connection',
              'ResolvedConnection',
              tag_id=12,
              ignorable=IGNORABLE_DEFAULT,
          ),
      ],
  )

  gen.AddNode(
      name='ResolvedCreateViewBase',
      tag_id=108,
      parent='ResolvedCreateStatement',
      is_abstract=True,
      comment="""
      Common superclass for CREATE view/materialized view:
        CREATE [TEMP|MATERIALIZED] [RECURSIVE] VIEW <name> [(...)]
          [OPTIONS (...)]
          AS SELECT ...

      <option_list> has engine-specific directives for options attached to
                    this view.
      <output_column_list> has the names and types of the columns in the
                           created view, and maps from <query>'s column_list
                           to these output columns. If <has_explicit_columns> is
                           true, names will be explicitly provided.
      <has_explicit_columns> If this is set, the statement includes an explicit
        column name list. These column names should still be applied even if the
        query changes or is re-resolved in the future. The view becomes invalid
        if the query produces a different number of columns.
      <query> is the query to run.
      <sql> is the view query text.
      <sql_security> is the declared security mode for the function. Values
         include 'INVOKER', 'DEFINER'.
      <recursive> specifies whether or not the view is created with the
        RECURSIVE keyword.
      <column_definition_list> matches 1:1 with the <output_column_list> and
        provides explicit definition for each output column. Output column names
        and types must match column definition names and types. If the table is
        a value table, <column_definition_list> must have exactly one column,
        with a generated name such as "$struct".

      Currently <column_definition_list> contains the same schema information
      (column names and types) as <output_column_list>, but also contains the
      column OPTIONS. Therefore, consumers are encouraged to read from
      <column_definition_list> rather than from <output_column_list> to
      determine the schema, if possible.

      Note that <query> and <sql> are both marked as IGNORABLE because
      an engine could look at either one (but might not look at both).
      An engine must look at one (and cannot ignore both) to be
      semantically valid, but there is currently no way to enforce that.

      The view must produce named columns with unique names.
              """,
      fields=[
          Field(
              'option_list',
              'ResolvedOption',
              tag_id=2,
              vector=True,
              ignorable=IGNORABLE_DEFAULT),
          Field(
              'output_column_list',
              'ResolvedOutputColumn',
              tag_id=3,
              vector=True),
          Field(
              'has_explicit_columns',
              SCALAR_BOOL,
              tag_id=9,
              ignorable=IGNORABLE_DEFAULT),
          Field('query', 'ResolvedScan', tag_id=5, ignorable=IGNORABLE),
          Field('sql', SCALAR_STRING, tag_id=6, ignorable=IGNORABLE),
          Field(
              'sql_security',
              SCALAR_SQL_SECURITY,
              tag_id=7,
              ignorable=IGNORABLE_DEFAULT),
          Field(
              'is_value_table',
              SCALAR_BOOL,
              tag_id=4,
              ignorable=IGNORABLE_DEFAULT,
              comment="""
              If true, this view produces a value table. Rather than producing
              rows with named columns, it produces rows with a single unnamed
              value type.  output_column_list will have exactly one column, with
              an empty name. See (broken link).
                      """),
          Field(
              'recursive',
              SCALAR_BOOL,
              tag_id=8,
              ignorable=IGNORABLE,
              comment="""
                True if the view uses the RECURSIVE keyword. <query>
                can be a ResolvedRecursiveScan only if this is true."""),
          Field(
              'column_definition_list',
              'ResolvedColumnDefinition',
              tag_id=10,
              vector=True,
              # This is ignorable for backwards compatibility (engines may
              # already be using output_column_list instead).
              ignorable=IGNORABLE)
      ])

  gen.AddNode(
      name='ResolvedCreateViewStmt',
      tag_id=41,
      parent='ResolvedCreateViewBase',
      comment="""
      This statement:
        CREATE [TEMP] VIEW <name> [(...)] [OPTIONS (...)] AS SELECT ...
              """,
      fields=[])

  gen.AddNode(
      name='ResolvedWithPartitionColumns',
      tag_id=153,
      parent='ResolvedArgument',
      comment="""
      This statement:
        WITH PARTITION COLUMNS [(column schema, ...)]
              """,
      fields=[
          # Restrict With Partition Column clause to column definitions only
          # even though parser allows constraints as well.
          Field(
              'column_definition_list',
              'ResolvedColumnDefinition',
              tag_id=2,
              vector=True,
              ignorable=IGNORABLE_DEFAULT),
      ])

  gen.AddNode(
      name='ResolvedCreateSnapshotTableStmt',
      tag_id=182,
      parent='ResolvedCreateStatement',
      comment="""
      This statement:
        CREATE SNAPSHOT TABLE [IF NOT EXISTS] <name> [OPTIONS (...)]
        CLONE <name>
                [FOR SYSTEM_TIME AS OF <time_expr>]

      <clone_from> the source data to clone data from.
                   ResolvedTableScan will represent the source table, with an
                   optional for_system_time_expr.
                   The ResolvedTableScan may be wrapped inside a
                   ResolvedFilterScan if the source table has a where clause.
                   No other Scan types are allowed here.
                   By default, all fields (column names, types, constraints,
                   partition, clustering, options etc.) will be inherited from
                   the source table. If table options are explicitly set, the
                   explicit options will take precedence.
                   The 'clone_from.column_list' field may be set, but should be
                   ignored.
            """,
      fields=[
          Field(
              'clone_from',
              'ResolvedScan',
              tag_id=2),
          Field(
              'option_list',
              'ResolvedOption',
              tag_id=3,
              vector=True,
              ignorable=IGNORABLE_DEFAULT),
      ])

  gen.AddNode(
      name='ResolvedCreateExternalTableStmt',
      tag_id=42,
      parent='ResolvedCreateTableStmtBase',
      comment="""
      This statement:
        CREATE [TEMP] EXTERNAL TABLE <name> [(column type, ...)]
        [DEFAULT COLLATE <collation_name>]
        [WITH PARTITION COLUMN [(column type, ...)]]
        [WITH CONNECTION connection_name]
        OPTIONS (...)
            """,
      fields=[
          Field(
              'with_partition_columns',
              'ResolvedWithPartitionColumns',
              tag_id=2,
              ignorable=IGNORABLE_DEFAULT),
      ])

  gen.AddNode(
      name='ResolvedExportModelStmt',
      tag_id=152,
      parent='ResolvedStatement',
      comment="""
      This statement:
        EXPORT MODEL <model_name_path> [WITH CONNECTION <connection>]
        <option_list>
      which is used to export a model to a specific location.
      <connection> is the connection that the model is written to.
      <option_list> identifies user specified options to use when exporting the
        model.
           """,
      fields=[
          Field('model_name_path', SCALAR_STRING, vector=True, tag_id=2),
          Field(
              'connection',
              'ResolvedConnection',
              tag_id=3,
              ignorable=IGNORABLE_DEFAULT),
          Field('option_list', 'ResolvedOption', vector=True, tag_id=4),
      ])

  gen.AddNode(
      name='ResolvedExportDataStmt',
      tag_id=43,
      parent='ResolvedStatement',
      comment="""
      This statement:
        EXPORT DATA [WITH CONNECTION] <connection> (<option_list>) AS SELECT ...
      which is used to run a query and export its result somewhere
      without giving the result a table name.
      <connection> connection reference for accessing destination source.
      <option_list> has engine-specific directives for how and where to
                    materialize the query result.
      <output_column_list> has the names and types of the columns produced by
                           the query, and maps from <query>'s column_list
                           to these output columns.  The engine may ignore
                           the column names depending on the output format.
      <query> is the query to run.

      The query must produce named columns with unique names.
              """,
      fields=[
          Field(
              'connection',
              'ResolvedConnection',
              tag_id=6,
              ignorable=IGNORABLE_DEFAULT),
          Field(
              'option_list',
              'ResolvedOption',
              tag_id=2,
              vector=True,
              ignorable=IGNORABLE_DEFAULT),
          Field(
              'output_column_list',
              'ResolvedOutputColumn',
              tag_id=3,
              vector=True),
          Field(
              'is_value_table',
              SCALAR_BOOL,
              tag_id=4,
              ignorable=IGNORABLE_DEFAULT,
              comment="""
              If true, the result of this query is a value table. Rather than
              producing rows with named columns, it produces rows with a single
              unnamed value type.  output_column_list will have exactly one
              column, with an empty name. See (broken link).
                      """),
          Field('query', 'ResolvedScan', tag_id=5)
      ])

  gen.AddNode(
      name='ResolvedExportMetadataStmt',
      tag_id=234,
      parent='ResolvedStatement',
      comment="""
      This statement:
        EXPORT <schema_object_kind> METADATA FROM <name_path>
        [WITH CONNECTION <connection>] [OPTIONS(<option_list>)]

      <schema_object_kind> is a string identifier for the object for which the
      metadata should be exported. Currently, only 'TABLE' object is supported.
      <name_path> is a vector giving the identifier path for the object for
      which the metadata should be exported.
      <connection> connection reference for accessing destination source.
      <option_list> identifies user specified options to use when exporting
      object's metadata.
          """,
      fields=[
          Field('schema_object_kind', SCALAR_STRING, tag_id=2),
          Field('name_path', SCALAR_STRING, tag_id=3, vector=True),
          Field(
              'connection',
              'ResolvedConnection',
              tag_id=4,
              ignorable=IGNORABLE_DEFAULT,
          ),
          Field('option_list', 'ResolvedOption', vector=True, tag_id=5),
      ],
  )

  gen.AddNode(
      name='ResolvedDefineTableStmt',
      tag_id=44,
      parent='ResolvedStatement',
      comment="""
      This statement: DEFINE TABLE name (...);

      <name_path> is a vector giving the identifier path in the table name.
      <option_list> has engine-specific options of how the table is defined.

      DEFINE TABLE normally has the same effect as CREATE TEMP EXTERNAL TABLE.
              """,
      fields=[
          Field('name_path', SCALAR_STRING, tag_id=2, vector=True),
          Field('option_list', 'ResolvedOption', tag_id=3, vector=True)
      ])

  gen.AddNode(
      name='ResolvedDescribeStmt',
      tag_id=45,
      parent='ResolvedStatement',
      comment="""
      This statement: DESCRIBE [<object_type>] <name> [FROM <from_name_path>];

      <object_type> is an optional string identifier,
                    e.g., "INDEX", "FUNCTION", "TYPE", etc.
      <name_path> is a vector giving the identifier path for the object to be
                  described.
      <from_name_path> is an optional vector giving the identifier path of a
                         containing object, e.g. a table.
              """,
      fields=[
          Field('object_type', SCALAR_STRING, tag_id=2),
          Field('name_path', SCALAR_STRING, tag_id=3, vector=True),
          Field(
              'from_name_path',
              SCALAR_STRING,
              tag_id=4,
              vector=True,
              ignorable=IGNORABLE_DEFAULT)
      ])

  gen.AddNode(
      name='ResolvedShowStmt',
      tag_id=46,
      parent='ResolvedStatement',
      comment="""
      This statement: SHOW <identifier> [FROM <name_path>] [LIKE <like_expr>];

      <identifier> is a string that determines the type of objects to be shown,
                   e.g., TABLES, COLUMNS, INDEXES, STATUS,
      <name_path> is an optional path to an object from which <identifier>
                  objects will be shown, e.g., if <identifier> = INDEXES and
                  <name> = table_name, the indexes of "table_name" will be
                  shown,
      <like_expr> is an optional ResolvedLiteral of type string that if present
                  restricts the objects shown to have a name like this string.
              """,
      fields=[
          Field('identifier', SCALAR_STRING, tag_id=2),
          Field(
              'name_path',
              SCALAR_STRING,
              tag_id=3,
              vector=True,
              ignorable=IGNORABLE_DEFAULT),
          Field(
              'like_expr',
              'ResolvedLiteral',
              tag_id=4,
              ignorable=IGNORABLE_DEFAULT)
      ])

  gen.AddNode(
      name='ResolvedBeginStmt',
      tag_id=47,
      parent='ResolvedStatement',
      comment="""
      This statement: BEGIN [TRANSACTION] [ <transaction_mode> [, ...] ]

      Where transaction_mode is one of:
           READ ONLY
           READ WRITE
           <isolation_level>

      <isolation_level> is a string vector storing the identifiers after
            ISOLATION LEVEL. The strings inside vector could be one of the
            SQL standard isolation levels:

                        READ UNCOMMITTED
                        READ COMMITTED
                        READ REPEATABLE
                        SERIALIZABLE

            or could be arbitrary strings. ZetaSQL does not validate that
            the string is valid.
              """,
      fields=[
          Field(
              'read_write_mode',
              SCALAR_READ_WRITE_TRANSACTION_MODE,
              tag_id=3,
              ignorable=IGNORABLE_DEFAULT),
          Field(
              'isolation_level_list',
              SCALAR_STRING,
              tag_id=2,
              vector=True,
              ignorable=IGNORABLE_DEFAULT)
      ])

  gen.AddNode(
      name='ResolvedSetTransactionStmt',
      tag_id=120,
      parent='ResolvedStatement',
      comment="""
      This statement: SET TRANSACTION <transaction_mode> [, ...]

      Where transaction_mode is one of:
           READ ONLY
           READ WRITE
           <isolation_level>

      <isolation_level> is a string vector storing the identifiers after
            ISOLATION LEVEL. The strings inside vector could be one of the
            SQL standard isolation levels:

                        READ UNCOMMITTED
                        READ COMMITTED
                        READ REPEATABLE
                        SERIALIZABLE

            or could be arbitrary strings. ZetaSQL does not validate that
            the string is valid.
              """,
      fields=[
          Field(
              'read_write_mode',
              SCALAR_READ_WRITE_TRANSACTION_MODE,
              tag_id=3,
              ignorable=IGNORABLE_DEFAULT),
          Field(
              'isolation_level_list',
              SCALAR_STRING,
              tag_id=2,
              vector=True,
              ignorable=IGNORABLE_DEFAULT)
      ],
      extra_defs="""
  typedef ResolvedBeginStmt::ReadWriteMode ReadWriteMode;""")

  gen.AddNode(
      name='ResolvedCommitStmt',
      tag_id=48,
      parent='ResolvedStatement',
      comment="""
      This statement: COMMIT [TRANSACTION];
              """,
      fields=[])

  gen.AddNode(
      name='ResolvedRollbackStmt',
      tag_id=49,
      parent='ResolvedStatement',
      comment="""
      This statement: ROLLBACK [TRANSACTION];
              """,
      fields=[])

  gen.AddNode(
      name='ResolvedStartBatchStmt',
      tag_id=122,
      parent='ResolvedStatement',
      comment="""
      This statement: START BATCH [<batch_type>];

      <batch_type> is an optional string identifier that identifies the type of
                   the batch. (e.g. "DML" or "DDL)
              """,
      fields=[
          Field(
              'batch_type',
              SCALAR_STRING,
              tag_id=2,
              ignorable=IGNORABLE_DEFAULT),
      ])

  gen.AddNode(
      name='ResolvedRunBatchStmt',
      tag_id=123,
      parent='ResolvedStatement',
      comment="""
      This statement: RUN BATCH;
              """,
      fields=[])

  gen.AddNode(
      name='ResolvedAbortBatchStmt',
      tag_id=124,
      parent='ResolvedStatement',
      comment="""
      This statement: ABORT BATCH;
              """,
      fields=[])

  gen.AddNode(
      name='ResolvedDropStmt',
      tag_id=50,
      parent='ResolvedStatement',
      comment="""
      This statement: DROP <object_type> [IF EXISTS] <name_path> [<drop_mode>];

      <object_type> is an string identifier,
                    e.g., "TABLE", "VIEW", "INDEX", "FUNCTION", "TYPE", etc.
      <name_path> is a vector giving the identifier path for the object to be
                  dropped.
      <is_if_exists> silently ignore the "name_path does not exist" error.
      <drop_mode> specifies drop mode RESTRICT/CASCASE, if any.

              """,
      fields=[
          Field('object_type', SCALAR_STRING, tag_id=2),
          Field('is_if_exists', SCALAR_BOOL, tag_id=3),
          Field('name_path', SCALAR_STRING, tag_id=4, vector=True),
          Field(
              'drop_mode',
              SCALAR_DROP_MODE,
              tag_id=5,
              ignorable=IGNORABLE_DEFAULT)
      ])

  gen.AddNode(
      name='ResolvedDropMaterializedViewStmt',
      tag_id=121,
      parent='ResolvedStatement',
      comment="""
      This statement: DROP MATERIALIZED VIEW [IF EXISTS] <name_path>;

      <name_path> is a vector giving the identifier path for the object to be
                  dropped.
      <is_if_exists> silently ignore the "name_path does not exist" error.
              """,
      fields=[
          Field('is_if_exists', SCALAR_BOOL, tag_id=3),
          Field('name_path', SCALAR_STRING, tag_id=4, vector=True)
      ])

  gen.AddNode(
      name='ResolvedDropSnapshotTableStmt',
      tag_id=183,
      parent='ResolvedStatement',
      comment="""
      This statement: DROP SNAPSHOT TABLE [IF EXISTS] <name_path>;

      <name_path> is a vector giving the identifier path for the object to be
                  dropped.
      <is_if_exists> silently ignore the "name_path does not exist" error.
              """,
      fields=[
          Field('is_if_exists', SCALAR_BOOL, tag_id=3),
          Field('name_path', SCALAR_STRING, tag_id=4, vector=True)
      ])

  gen.AddNode(
      name='ResolvedRecursiveRefScan',
      tag_id=147,
      parent='ResolvedScan',
      comment="""
      Scan the previous iteration of the recursive alias currently being
      defined, from inside the recursive subquery which defines it. Such nodes
      can exist only in the recursive term of a ResolvedRecursiveScan node.
      The column_list produced here will match 1:1 with the column_list produced
      by the referenced subquery and will be given a new unique name to each
      column produced for this scan.
              """,
      fields=[])

  gen.AddNode(
      name='ResolvedRecursiveScan',
      tag_id=148,
      parent='ResolvedScan',
      comment="""
      A recursive query inside a WITH RECURSIVE or RECURSIVE VIEW. A
      ResolvedRecursiveScan may appear in a resolved tree only as a top-level
      input scan of a ResolvedWithEntry or ResolvedCreateViewBase.

      Recursive queries must satisfy the form:
          <non-recursive-query> UNION [ALL|DISTINCT] <recursive-query>

      where self-references to table being defined are allowed only in the
      <recursive-query> section.

      <column_list> is a set of new ResolvedColumns created by this scan.
      Each input ResolvedSetOperationItem has an <output_column_list> which
      matches 1:1 with <column_list> and specifies how the input <scan>'s
      columns map into the final <column_list>.

      At runtime, a recursive scan is evaluated using an iterative process:

      Step 1: Evaluate the non-recursive term. If UNION DISTINCT
        is specified, discard duplicates.

      Step 2:
        Repeat until step 2 produces an empty result:
          Evaluate the recursive term, binding the recursive table to the
          new rows produced by previous step. If UNION DISTINCT is specified,
          discard duplicate rows, as well as any rows which match any
          previously-produced result.

      Step 3:
        The final content of the recursive table is the UNION ALL of all results
        produced (step 1, plus all iterations of step 2).

      ResolvedRecursiveScan only supports a recursive WITH entry which
        directly references itself; ZetaSQL does not support mutual recursion
        between multiple with-clause elements.

      See (broken link) for details.
      """,
      fields=[
          Field('op_type', SCALAR_RECURSIVE_SET_OPERATION_TYPE, tag_id=2),
          Field('non_recursive_term', 'ResolvedSetOperationItem', tag_id=3),
          Field('recursive_term', 'ResolvedSetOperationItem', tag_id=4),
      ])

  gen.AddNode(
      name='ResolvedWithScan',
      tag_id=51,
      parent='ResolvedScan',
      comment="""
      This represents a SQL WITH query (or subquery) like
        WITH [RECURSIVE] <with_query_name1> AS (<with_subquery1>),
             <with_query_name2> AS (<with_subquery2>)
        <query>;

      WITH entries are sorted in dependency order so that an entry can only
      reference entries earlier in <with_entry_list>, plus itself if the
      RECURSIVE keyword is used. If the RECURSIVE keyword is not used, this will
      be the same order as in the original query, since an entry which
      references itself or any entry later in the list is not allowed.

      If a WITH subquery is referenced multiple times, the full query should
      behave as if the subquery runs only once and its result is reused.

      There will be one ResolvedWithEntry here for each subquery in the SQL
      WITH statement, in the same order as in the query.

      Inside the resolved <query>, or any <with_entry_list> occurring after
      its definition, a <with_query_name> used as a table scan will be
      represented using a ResolvedWithRefScan.

      The <with_query_name> aliases are always unique within a query, and should
      be used to connect the ResolvedWithRefScan to the original query
      definition.  The subqueries are not inlined and duplicated into the tree.

      In ZetaSQL 1.0, WITH is allowed only on the outermost query and not in
      subqueries, so the ResolvedWithScan node can only occur as the outermost
      scan in a statement (e.g. a QueryStmt or CreateTableAsSelectStmt).

      In ZetaSQL 1.1 (language option FEATURE_V_1_1_WITH_ON_SUBQUERY), WITH
      is allowed on subqueries.  Then, ResolvedWithScan can occur anywhere in
      the tree.  The alias introduced by a ResolvedWithEntry is visible only
      in subsequent ResolvedWithEntry queries and in <query>.  The aliases used
      must be globally unique in the resolved AST however, so consumers do not
      need to implement any scoping for these names.  Because the aliases are
      unique, it is legal to collect all ResolvedWithEntries in the tree and
      treat them as if they were a single WITH clause at the outermost level.

      In ZetaSQL 1.3 (language option FEATURE_V_1_3_WITH_RECURSIVE), WITH
      RECURSIVE is supported, which allows any <with_subquery> to reference
      any <with_query_name>, regardless of order, including WITH entries which
      reference themself. Circular dependency chains of WITH entries are allowed
      only for direct self-references, and only when the corresponding
      <with_subquery> takes the form "<non-recursive-term> UNION [ALL|DISTINCT]
      <recursive-term>", with all references to the current <with_query_name>
      confined to the recursive term.

      The subqueries inside ResolvedWithEntries cannot be correlated.

      If a WITH subquery is defined but never referenced, it will still be
      resolved and still show up here.  Query engines may choose not to run it.

              """,
      fields=[
          Field(
              'with_entry_list',
              'ResolvedWithEntry',
              tag_id=2,
              vector=True),
          Field(
              'query', 'ResolvedScan', tag_id=3, propagate_order=True),
          Field(
              'recursive',
              SCALAR_BOOL,
              tag_id=4,
              ignorable=IGNORABLE,
              comment="""
                True if the WITH clause uses the recursive keyword.""")
      ])

  gen.AddNode(
      name='ResolvedWithEntry',
      tag_id=52,
      parent='ResolvedArgument',
      comment="""
      This represents one aliased subquery introduced in a WITH clause.

      The <with_query_name>s must be globally unique in the full resolved AST.
      The <with_subquery> cannot be correlated and cannot reference any
      columns from outside.  It may reference other WITH subqueries.

      See ResolvedWithScan for full details.
              """,
      fields=[
          Field('with_query_name', SCALAR_STRING, tag_id=2),
          Field('with_subquery', 'ResolvedScan', tag_id=3)
      ])

  gen.AddNode(
      name='ResolvedOption',
      tag_id=53,
      parent='ResolvedArgument',
      use_custom_debug_string=True,
      comment="""
      This represents one SQL hint or option key/value pair.
      The SQL syntax @{ key1=value1, key2=value2, some_db.key3=value3 }
      will expand to three ResolvedOptions.  Keyword hints (e.g. LOOKUP JOIN)
      are interpreted as shorthand, and will be expanded to a ResolvedOption
      attached to the appropriate node before any explicit long-form hints.

      ResolvedOptions are attached to the ResolvedScan corresponding to the
      operator that the SQL hint was associated with.
      See (broken link) for more detail.
      Hint semantics are implementation defined.

      Each hint or option is resolved as a [<qualifier>.]<name>:=<value> pair.
        <qualifier> will be empty if no qualifier was present.
        <name> is always non-empty.
        <value> can be a ResolvedLiteral or a ResolvedParameter,
                a cast of a ResolvedParameter (for typed hints only),
                or a general expression (on constant inputs).
        <assignment_op> is an enum that indicates the assignment operation for
                        array type options.

      If AllowedHintsAndOptions was set in AnalyzerOptions, and this hint or
      option was included there and had an expected type, the type of <value>
      will match that expected type. For assignment_op that's not the default
      value, also checks whether the expected type is Array and whether
      allow_alter_array is true. Unknown hints and options(not listed in
      AllowedHintsAndOptions) are not stripped and will still show up here.

      If non-empty, <qualifier> should be interpreted as a target system name,
      and a database system should ignore any hints targeted to different
      systems.

      <qualifier> is set only for hints, and will always be empty in options
      lists.

      <assignment_op> will always be DEFAULT_ASSIGN (i.e. "=") for hints, and
                      defaults to the same value for options. Can be set to
                      ADD_ASSIGN ("+=") and SUB_ASSIGN ("-=") for options with
                      Array type.

      The SQL syntax allows using an identifier as a hint value.
      Such values are stored here as ResolvedLiterals with string type.
              """,
      fields=[
          Field(
              'qualifier', SCALAR_STRING, tag_id=2, ignorable=IGNORABLE_DEFAULT
          ),
          Field('name', SCALAR_STRING, tag_id=3, ignorable=IGNORABLE),
          Field('value', 'ResolvedExpr', tag_id=4, ignorable=IGNORABLE),
          Field(
              'assignment_op',
              SCALAR_OPTIONS_ASSIGNMENT_OP,
              tag_id=5,
              ignorable=IGNORABLE_DEFAULT,
              is_constructor_arg=False,
          ),
      ],
  )

  gen.AddNode(
      name='ResolvedWindowPartitioning',
      tag_id=54,
      parent='ResolvedArgument',
      comment="""
      Window partitioning specification for an analytic function call.

      PARTITION BY keys in <partition_by_list>.

      <collation_list> is either empty to indicate that all the elements in
      <partition_by_list> have the default collation, or <collation_list> has
      the same number of elements as <partition_by_list>.  Each element is the
      collation for the element in <partition_by_list> with the same index, or
      can be empty to indicate default collation or when the type is not
      collatable. <collation_list> is only set when
      FEATURE_V_1_3_COLLATION_SUPPORT is enabled.
      See (broken link).
              """,
      fields=[
          Field(
              'partition_by_list',
              'ResolvedColumnRef',
              tag_id=2,
              vector=True),
          Field(
              'hint_list',
              'ResolvedOption',
              tag_id=3,
              ignorable=IGNORABLE,
              is_constructor_arg=False,
              vector=True,
          ),
          Field(
              'collation_list',
              SCALAR_RESOLVED_COLLATION,
              tag_id=4,
              ignorable=IGNORABLE_DEFAULT,
              vector=True,
              java_to_string_method='toStringCommaSeparated',
              is_constructor_arg=False,
          ),
      ],
  )

  gen.AddNode(
      name='ResolvedWindowOrdering',
      tag_id=55,
      parent='ResolvedArgument',
      comment="""
     Window ordering specification for an analytic function call.

     ORDER BY items in <order_by_list>. There should be exactly one ORDER
     BY item if this is a window ORDER BY for a RANGE-based window.
              """,
      fields=[
          Field(
              'order_by_item_list',
              'ResolvedOrderByItem',
              tag_id=2,
              vector=True),
          Field(
              'hint_list',
              'ResolvedOption',
              tag_id=3,
              ignorable=IGNORABLE,
              is_constructor_arg=False,
              vector=True)
      ])

  gen.AddNode(
      name='ResolvedWindowFrame',
      tag_id=56,
      parent='ResolvedArgument',
      comment="""
      Window framing specification for an analytic function call.

      ROW-based window frames compute the frame based on physical offsets
      from the current row.
      RANGE-based window frames compute the frame based on a logical
      range of rows around the current row based on the current row's
      ORDER BY key value.

      <start_expr> and <end_expr> cannot be NULL. If the window frame
      is one-sided in the input query, the resolver will generate an
      implicit ending boundary.
              """,
      fields=[
          Field('frame_unit', SCALAR_FRAME_UNIT, tag_id=2),
          Field('start_expr', 'ResolvedWindowFrameExpr', tag_id=3),
          Field('end_expr', 'ResolvedWindowFrameExpr', tag_id=4)
      ],
      use_custom_debug_string=True,
      extra_defs="""
  std::string GetFrameUnitString() const;
  static std::string FrameUnitToString(FrameUnit frame_unit);""")

  gen.AddNode(
      name='ResolvedAnalyticFunctionGroup',
      tag_id=57,
      parent='ResolvedArgument',
      comment="""
      This represents a group of analytic function calls that shares PARTITION
      BY and ORDER BY.

      <partition_by> can be NULL. <order_by> may be NULL depending on the
      functions in <analytic_function_list> and the window frame unit. See
      (broken link) for more details.

      All expressions in <analytic_function_list> have a
      ResolvedAnalyticFunctionCall with a function in mode
      Function::AGGREGATE or Function::ANALYTIC.
              """,
      fields=[
          Field('partition_by', 'ResolvedWindowPartitioning', tag_id=2),
          Field('order_by', 'ResolvedWindowOrdering', tag_id=3),
          Field(
              'analytic_function_list',
              'ResolvedComputedColumn',
              tag_id=4,
              vector=True)
      ])

  gen.AddNode(
      name='ResolvedWindowFrameExpr',
      tag_id=58,
      parent='ResolvedArgument',
      comment="""
      Window frame boundary expression that determines the first/last row of
      the moving window for each tuple.

      <expression> cannot be NULL if the type is OFFSET_PRECEDING
      or OFFSET_FOLLOWING. It must be a constant expression. If this is a
      boundary for a ROW-based window, it must be integer type. Otherwise,
      it must be numeric type and must match exactly the type of the window
      ordering expression.  See (broken link) for more
      details.
              """,
      fields=[
          Field('boundary_type', SCALAR_BOUNDARY_TYPE, tag_id=2),
          Field('expression', 'ResolvedExpr', tag_id=3)
      ],
      use_custom_debug_string=True,
      extra_defs="""
  std::string GetBoundaryTypeString() const;
  static std::string BoundaryTypeToString(BoundaryType boundary_type);""")

  gen.AddNode(
      name='ResolvedDMLValue',
      tag_id=59,
      parent='ResolvedArgument',
      comment="""
      This represents a value inside an INSERT or UPDATE statement.

      The <value> is either an expression or a DMLDefault.

      For proto fields, NULL values mean the field should be cleared.
              """,
      fields=[Field('value', 'ResolvedExpr', tag_id=2)])

  gen.AddNode(
      name='ResolvedDMLDefault',
      tag_id=60,
      parent='ResolvedExpr',
      comment="""
      This is used to represent the value DEFAULT that shows up (in place of a
      value expression) in INSERT and UPDATE statements.
      For columns, engines should substitute the engine-defined default value
      for that column, or give an error.
      For proto fields, this always means to clear the field.
      This will never show up inside expressions other than ResolvedDMLValue.
              """,
      fields=[])

  gen.AddNode(
      name='ResolvedAssertStmt',
      tag_id=98,
      parent='ResolvedStatement',
      comment="""
      This represents the ASSERT statement:
        ASSERT <expression> [AS <description>];

      <expression> is any expression that returns a bool.
      <description> is an optional string literal used to give a more
      descriptive error message in case the ASSERT fails.
              """,
      fields=[
          Field('expression', 'ResolvedExpr', tag_id=2),
          Field(
              'description',
              SCALAR_STRING,
              tag_id=3,
              ignorable=IGNORABLE_DEFAULT)
      ])

  gen.AddNode(
      name='ResolvedAssertRowsModified',
      tag_id=61,
      parent='ResolvedArgument',
      comment="""
      This represents the ASSERT ROWS MODIFIED clause on a DML statement.
      The value must be a literal or (possibly casted) parameter int64.

      The statement should fail if the number of rows updated does not
      exactly match this number.
              """,
      fields=[Field('rows', 'ResolvedExpr', tag_id=2)])

  gen.AddNode(
      name='ResolvedInsertRow',
      tag_id=62,
      parent='ResolvedArgument',
      comment="""
      This represents one row in the VALUES clause of an INSERT.
              """,
      fields=[Field('value_list', 'ResolvedDMLValue', tag_id=2, vector=True)])

  gen.AddNode(
      name='ResolvedInsertStmt',
      tag_id=63,
      parent='ResolvedStatement',
      comment="""
      This represents an INSERT statement, or a nested INSERT inside an
      UPDATE statement.

      For top-level INSERT statements, <table_scan> gives the table to
      scan and creates ResolvedColumns for its columns.  Those columns can be
      referenced in <insert_column_list>.

      For nested INSERTS, there is no <table_scan> or <insert_column_list>.
      There is implicitly a single column to insert, and its type is the
      element type of the array being updated in the ResolvedUpdateItem
      containing this statement.

      For nested INSERTs, alternate modes are not supported and <insert_mode>
      will always be set to OR_ERROR.

      The rows to insert come from <row_list> or the result of <query>.
      Exactly one of these must be present.

      If <row_list> is present, the columns in the row_list match
      positionally with <insert_column_list>.

      If <query> is present, <query_output_column_list> must also be present.
      <query_output_column_list> is the list of output columns produced by
      <query> that correspond positionally with the target <insert_column_list>
      on the output table.  For nested INSERTs with no <insert_column_list>,
      <query_output_column_list> must have exactly one column.

      <query_parameter_list> is set for nested INSERTs where <query> is set and
      references non-target values (columns or field values) from the table. It
      is only set when FEATURE_V_1_2_CORRELATED_REFS_IN_NESTED_DML is enabled.

      If <returning> is present, the INSERT statement will return newly inserted
      rows. <returning> can only occur on top-level statements.

      The returning clause has a <output_column_list> to represent the data
      sent back to clients. It can only access columns from the <table_scan>.
      <topologically_sorted_generated_column_id_list> is set for queries to
      tables having generated columns. It provides the resolved column ids of
      the generated columns in topological order.
      <generated_expr_list> has generated expressions for the corresponding
      generated column in the topologically_sorted_generated_column_id_list.
      Hence, these lists have the same size.

      <column_access_list> indicates for each column in <table_scan.column_list>
      whether it was read and/or written. The query engine may also require
      read or write permissions across all columns, including unreferenced
      columns, depending on the operation.
              """,
      fields=[
          Field(
              'table_scan',
              'ResolvedTableScan',
              tag_id=2,
              ignorable=IGNORABLE_DEFAULT),
          Field(
              'insert_mode',
              SCALAR_INSERT_MODE,
              tag_id=3,
              ignorable=IGNORABLE_DEFAULT,
              comment="""
              Behavior on duplicate rows (normally defined to mean duplicate
              primary keys).
                      """),
          Field(
              'assert_rows_modified',
              'ResolvedAssertRowsModified',
              tag_id=4,
              ignorable=IGNORABLE_DEFAULT),
          Field(
              'returning',
              'ResolvedReturningClause',
              tag_id=10,
              ignorable=IGNORABLE_DEFAULT),
          Field(
              'insert_column_list',
              SCALAR_RESOLVED_COLUMN,
              tag_id=5,
              vector=True,
              ignorable=IGNORABLE_DEFAULT),
          Field(
              'query_parameter_list',
              'ResolvedColumnRef',
              vector=True,
              ignorable=IGNORABLE_DEFAULT,
              tag_id=9),
          Field(
              'query',
              'ResolvedScan',
              tag_id=6,
              ignorable=IGNORABLE_DEFAULT),
          Field(
              'query_output_column_list',
              SCALAR_RESOLVED_COLUMN,
              tag_id=8,
              vector=True,
              ignorable=IGNORABLE_DEFAULT),
          Field(
              'row_list',
              'ResolvedInsertRow',
              tag_id=7,
              vector=True,
              ignorable=IGNORABLE_DEFAULT),
          Field(
              'column_access_list',
              SCALAR_OBJECT_ACCESS,
              tag_id=11,
              ignorable=IGNORABLE,
              vector=True,
              is_constructor_arg=False,
              java_to_string_method='toStringObjectAccess'),
          Field(
              'topologically_sorted_generated_column_id_list',
              SCALAR_INT,
              tag_id=14,
              vector=True,
              ignorable=IGNORABLE_DEFAULT,
              is_optional_constructor_arg=True,
              to_string_method='ToStringCommaSeparated',
              java_to_string_method='toStringCommaSeparatedForInt',
              comment="""
               This returns a topologically sorted list of generated columns
               resolved ids in the table accessed by insert statement.
               For example for below table
               CREATE TABLE T(
               k1 INT64 NOT NULL,
               data INT64,
               gen1 INT64 AS data+1,
               gen2 INT64 AS gen1*2,
               gen3 INT64 AS data*2 + gen1,
               ) PRIMARY KEY(k1);
              data------------------->gen1--------------------->gen2
                *                      *----------> *
                *  ------------------------------->gen3
              the vector would have corresponding indexes of one of these values
              gen1 gen2 gen3 OR gen1 gen3 gen2.
              """),
          Field(
              'generated_column_expr_list',
              'ResolvedExpr',
              tag_id=15,
              vector=True,
              is_optional_constructor_arg=True,
              ignorable=IGNORABLE_DEFAULT,
              comment="""
              This field returns the vector of generated column expressions
              corresponding to the column ids in
              topologically_sorted_generated_column_id_list. Both the lists have
              the same size and 1-to-1 mapping for the column id with its
              corresponding expression. This field is not directly accessed
              from the catalog since these expressions are rewritten to replace
              the ResolvedExpressionColumn for the referred columns in the
              catalog to corresponding ResolvedColumnRef.
              """)
      ],
      extra_defs="""
  std::string GetInsertModeString() const;
  static std::string InsertModeToString(InsertMode boundary_type);""")

  gen.AddNode(
      name='ResolvedDeleteStmt',
      tag_id=64,
      parent='ResolvedStatement',
      comment="""
      This represents a DELETE statement or a nested DELETE inside an
      UPDATE statement.

      For top-level DELETE statements, <table_scan> gives the table to
      scan and creates ResolvedColumns for its columns.  Those columns can
      be referenced inside the <where_expr>.

      For nested DELETEs, there is no <table_scan>.  The <where_expr> can
      only reference:
        (1) the element_column from the ResolvedUpdateItem containing this
            statement,
        (2) columns from the outer statements, and
        (3) (optionally) <array_offset_column>, which represents the 0-based
            offset of the array element being modified.

      <where_expr> is required.

      If <returning> is present, the DELETE statement will return deleted rows
      back. It can only occur on top-level statements.

      This returning clause has a <output_column_list> to represent the data
      sent back to clients. It can only access columns from the <table_scan>.

      <column_access_list> indicates for each column in <table_scan.column_list>
      whether it was read and/or written. The query engine may also require
      read or write permissions across all columns, including unreferenced
      columns, depending on the operation.
              """,
      fields=[
          Field(
              'table_scan',
              'ResolvedTableScan',
              tag_id=2,
              ignorable=IGNORABLE_DEFAULT),
          Field(
              'assert_rows_modified',
              'ResolvedAssertRowsModified',
              tag_id=3,
              ignorable=IGNORABLE_DEFAULT),
          Field(
              'returning',
              'ResolvedReturningClause',
              tag_id=6,
              ignorable=IGNORABLE_DEFAULT),
          Field(
              'column_access_list',
              SCALAR_OBJECT_ACCESS,
              tag_id=7,
              ignorable=IGNORABLE,
              vector=True,
              is_constructor_arg=False,
              java_to_string_method='toStringObjectAccess'),
          Field(
              'array_offset_column',
              'ResolvedColumnHolder',
              tag_id=5,
              ignorable=IGNORABLE_DEFAULT),
          Field('where_expr', 'ResolvedExpr', tag_id=4)
      ])

  gen.AddNode(
      name='ResolvedUpdateItem',
      tag_id=65,
      parent='ResolvedArgument',
      comment="""
      This represents one item inside the SET clause of an UPDATE.

      The entity being updated is specified by <target>.

      For a regular
        SET {target} = {expression} | DEFAULT
      clause (not including an array element update like SET a[OFFSET(0)] = 5),
      <target> and <set_value> will be present, and all other fields will be
      unset.

      For an array element update (e.g. SET a.b[<expr>].c = <value>),
        - <target> is set to the array,
        - <element_column> is a new ResolvedColumn that can be used inside the
          update items to refer to the array element.
        - <array_update_list> will have a node corresponding to the offset into
          that array and the modification to that array element.
      For example, for SET a.b[<expr>].c = <value>, we have
         ResolvedUpdateItem
         +-<target> = a.b
         +-<element_column> = <x>
         +-<array_update_list>
           +-ResolvedUpdateArrayItem
             +-<offset> = <expr>
             +-<update_item> = ResolvedUpdateItem
               +-<target> = <x>.c
               +-<set_value> = <value>

      The engine is required to fail the update if there are two elements of
      <array_update_list> corresponding to offset expressions that evaluate to
      the same value. These are considered to be conflicting updates.

      Multiple updates to the same array are always represented as multiple
      elements of <array_update_list> under a single ResolvedUpdateItem
      corresponding to that array. <array_update_list> will only have one
      element for modifications to an array-valued subfield of an array element.
      E.g., for SET a[<expr1>].b[<expr2>] = 5, a[<expr3>].b[<expr4>] = 6, we
      will have:
          ResolvedUpdateItem
          +-<target> = a
          +-<element_column> = x
          +-<array_update_list>
            +-ResolvedUpdateArrayItem
              +-<offset> = <expr1>
              +-ResolvedUpdateItem for <x>.b[<expr2>] = 5
            +-ResolvedUpdateArrayItem
              +-<offset> = <expr3>
              +-ResolvedUpdateItem for <x>.b[<expr4>] = 6
      The engine must give a runtime error if <expr1> and <expr3> evaluate to
      the same thing. Notably, it does not have to understand that the
      two ResolvedUpdateItems corresponding to "b" refer to the same array iff
      <expr1> and <expr3> evaluate to the same thing.

      TODO: Consider allowing the engine to execute an update like
      SET a[<expr1>].b = 1, a[<expr2>].c = 2 even if <expr1> == <expr2> since
      "b" and "c" do not overlap. Also consider allowing a more complex example
      like SET a[<expr1>].b[<expr2>] = ...,
      a[<expr3>].b[<expr4>].c[<expr5>] = ... even if <expr1> == <expr3>, as long
      as <expr2> != <expr4> in that case.

      For nested DML, <target> and <element_column> will both be set, and one or
      more of the nested statement lists will be non-empty. <target> must have
      ARRAY type, and <element_column> introduces a ResolvedColumn representing
      elements of that array. The nested statement lists will always be empty in
      a ResolvedUpdateItem child of a ResolvedUpdateArrayItem node.

      See (broken link) for more detail.
              """,
      fields=[
          Field(
              'target',
              'ResolvedExpr',
              tag_id=2,
              comment="""
              The target entity to be updated.

              This is an expression evaluated using the ResolvedColumns visible
              inside this statement.  This expression can contain only
              ResolvedColumnRefs, ResolvedGetProtoField and
              ResolvedGetStructField nodes.

              In a top-level UPDATE, the expression always starts with a
              ResolvedColumnRef referencing a column from the statement's
              TableScan.

              In a nested UPDATE, the expression always starts with a
              ResolvedColumnRef referencing the element_column from the
              ResolvedUpdateItem containing this scan.

              This node is also used to represent a modification of a single
              array element (when it occurs as a child of a
              ResolvedUpdateArrayItem node).  In that case, the expression
              starts with a ResolvedColumnRef referencing the <element_column>
              from its grandparent ResolvedUpdateItem. (E.g., for "SET a[<expr>]
              = 5", the grandparent ResolvedUpdateItem has <target> "a", the
              parent ResolvedUpdateArrayItem has offset <expr>, and this node
              has <set_value> 5 and target corresponding to the grandparent's
              <element_column> field.)

              For either a nested UPDATE or an array modification, there may be
              a path of field accesses after the initial ResolvedColumnRef,
              represented by a chain of GetField nodes.

              NOTE: We use the same GetField nodes as we do for queries, but
              they are not treated the same.  Here, they express a path inside
              an object that is being mutated, so they have reference semantics.
                      """),
          Field(
              'set_value',
              'ResolvedDMLValue',
              tag_id=3,
              ignorable=IGNORABLE_DEFAULT,
              comment="""
              Set the target entity to this value.  The types must match.
              This can contain the same columns that can appear in the
              <where_expr> of the enclosing ResolvedUpdateStmt.

              This is mutually exclusive with all fields below, which are used
              for nested updates only.
                      """),
          Field(
              'element_column',
              'ResolvedColumnHolder',
              tag_id=4,
              comment="""
              The ResolvedColumn introduced to represent the elements of the
              array being updated.  This works similarly to
              ArrayScan::element_column.

              <target> must have array type, and this column has the array's
              element type.

              This column can be referenced inside the nested statements below.
                      """),
          Field(
              'array_update_list',
              'ResolvedUpdateArrayItem',
              tag_id=8,
              vector=True,
              ignorable=IGNORABLE_DEFAULT,
              comment="""
              Array element modifications to apply. Each item runs on the value
              of <element_column> specified by ResolvedUpdateArrayItem.offset.
              This field is always empty if the analyzer option
              FEATURE_V_1_2_ARRAY_ELEMENTS_WITH_SET is disabled.

              The engine must fail if two elements in this list have offset
              expressions that evaluate to the same value.
              TODO: Consider generalizing this to allow
              SET a[<expr1>].b = ..., a[<expr2>].c = ...
                      """),
          Field(
              'delete_list',
              'ResolvedDeleteStmt',
              tag_id=5,
              vector=True,
              ignorable=IGNORABLE_DEFAULT,
              comment="""
              Nested DELETE statements to apply.  Each delete runs on one value
              of <element_column> and may choose to delete that array element.

              DELETEs are applied before INSERTs or UPDATEs.

              It is legal for the same input element to match multiple DELETEs.
                      """),
          Field(
              'update_list',
              'ResolvedUpdateStmt',
              tag_id=6,
              vector=True,
              ignorable=IGNORABLE_DEFAULT,
              comment="""
              Nested UPDATE statements to apply.  Each update runs on one value
              of <element_column> and may choose to update that array element.

              UPDATEs are applied after DELETEs and before INSERTs.

              It is an error if any element is matched by multiple UPDATEs.
                      """),
          Field(
              'insert_list',
              'ResolvedInsertStmt',
              tag_id=7,
              vector=True,
              ignorable=IGNORABLE_DEFAULT,
              comment="""
              Nested INSERT statements to apply.  Each insert will produce zero
              or more values for <element_column>.

              INSERTs are applied after DELETEs and UPDATEs.

              For nested UPDATEs, insert_mode will always be the default, and
              has no effect.
                      """)
      ])

  gen.AddNode(
      name='ResolvedUpdateArrayItem',
      tag_id=102,
      parent='ResolvedArgument',
      comment="""
      For an array element modification, this node represents the offset
      expression and the modification, but not the array. E.g., for
      SET a[<expr>] = 5, this node represents a modification of "= 5" to offset
      <expr> of the array defined by the parent node.
              """,
      fields=[
          Field(
              'offset',
              'ResolvedExpr',
              tag_id=2,
              comment="""
              The array offset to be modified.
                      """),
          Field(
              'update_item',
              'ResolvedUpdateItem',
              tag_id=3,
              comment="""
              The modification to perform to the array element.
                      """)
      ])

  gen.AddNode(
      name='ResolvedUpdateStmt',
      tag_id=66,
      parent='ResolvedStatement',
      comment="""
      This represents an UPDATE statement, or a nested UPDATE inside an
      UPDATE statement.

      For top-level UPDATE statements, <table_scan> gives the table to
      scan and creates ResolvedColumns for its columns.  Those columns can be
      referenced in the <update_item_list>. The top-level UPDATE statement may
      also have <from_scan>, the output of which is joined with
      the <table_scan> using expressions in the <where_expr>. The columns
      exposed in the <from_scan> are visible in the right side of the
      expressions in the <update_item_list> and in the <where_expr>.
      <array_offset_column> is never set for top-level UPDATE statements.

      Top-level UPDATE statements will also have <column_access_list> populated.
      For each column, this vector indicates if the column was read and/or
      written. The columns in this vector match those of
      <table_scan.column_list>. If a column was not encountered when producing
      the resolved AST, then the value at that index will be
      ResolvedStatement::NONE.

      For nested UPDATEs, there is no <table_scan>.  The <where_expr> can
      only reference:
        (1) the element_column from the ResolvedUpdateItem containing this
            statement,
        (2) columns from the outer statements, and
        (3) (optionally) <array_offset_column>, which represents the 0-based
            offset of the array element being modified.
      The left hand sides of the expressions in <update_item_list> can only
      reference (1). The right hand sides of those expressions can reference
      (1), (2), and (3).

      The updates in <update_item_list> will be non-overlapping.
      If there are multiple nested statements updating the same entity,
      they will be combined into one ResolvedUpdateItem.

      See (broken link) for more detail on nested DML.

      If <returning> is present, the UPDATE statement will return updated rows.
      <returning> can only occur on top-level statements.

      This returning clause has a <output_column_list> to represent the data
      sent back to clients. It can only access columns from the <table_scan>.
      The columns in <from_scan> are not allowed.
      TODO: allow columns in <from_scan> to be referenced.
              """,
      fields=[
          Field(
              'table_scan',
              'ResolvedTableScan',
              tag_id=2,
              ignorable=IGNORABLE_DEFAULT),
          Field(
              'column_access_list',
              SCALAR_OBJECT_ACCESS,
              tag_id=8,
              ignorable=IGNORABLE,
              vector=True,
              is_constructor_arg=False,
              java_to_string_method='toStringObjectAccess'),
          Field(
              'assert_rows_modified',
              'ResolvedAssertRowsModified',
              tag_id=3,
              ignorable=IGNORABLE_DEFAULT),
          Field(
              'returning',
              'ResolvedReturningClause',
              tag_id=9,
              ignorable=IGNORABLE_DEFAULT),
          Field(
              'array_offset_column',
              'ResolvedColumnHolder',
              tag_id=7,
              ignorable=IGNORABLE_DEFAULT),
          Field('where_expr', 'ResolvedExpr', tag_id=4),
          Field(
              'update_item_list',
              'ResolvedUpdateItem',
              tag_id=5,
              vector=True),
          Field(
              'from_scan',
              'ResolvedScan',
              tag_id=6,
              ignorable=IGNORABLE_DEFAULT)
      ])

  gen.AddNode(
      name='ResolvedMergeWhen',
      tag_id=100,
      parent='ResolvedArgument',
      comment="""
      This is used by ResolvedMergeStmt to represent one WHEN ... THEN clause
      within MERGE statement.

      There are three types of clauses, which are MATCHED, NOT_MATCHED_BY_SOURCE
      and NOT_MATCHED_BY_TARGET. The <match_type> must have one of these values.

      The <match_expr> defines an optional expression to apply to the join
      result of <table_scan> and <from_scan> of the parent ResolvedMergeStmt.

      Each ResolvedMergeWhen must define exactly one of three operations,
        -- INSERT: <action_type> is ResolvedMergeWhen::INSERT.
                   Both <insert_column_list> and <insert_row> are non-empty.
                   The size of <insert_column_list> must be the same with the
                   value_list size of <insert_row>, and, the column data type
                   must match.
        -- UPDATE: <action_type> is ResolvedMergeWhen::UPDATE.
                   <update_item_list> is non-empty.
        -- DELETE: <action_type> is ResolvedMergeWhen::DELETE.
      The INSERT, UPDATE and DELETE operations are mutually exclusive.

      When <match_type> is MATCHED, <action_type> must be UPDATE or DELETE.
      When <match_type> is NOT_MATCHED_BY_TARGET, <action_type> must be INSERT.
      When <match_type> is NOT_MATCHED_BY_SOURCE, <action_type> must be UPDATE
      or DELETE.

      The column visibility within a ResolvedMergeWhen clause is defined as
      following,
        -- When <match_type> is MATCHED,
           -- All columns from <table_scan> and <from_scan> are allowed in
              <match_expr>.
           -- If <action_type> is UPDATE, only columns from <table_scan> are
              allowed on left side of expressions in <update_item_list>.
              All columns from <table_scan> and <from_scan> are allowed on right
              side of expressions in <update_item_list>.
        -- When <match_type> is NOT_MATCHED_BY_TARGET,
           -- Only columns from <from_scan> are allowed in <match_expr>.
           -- Only columns from <table_scan> are allowed in
              <insert_column_list>.
           -- Only columns from <from_scan> are allowed in <insert_row>.
        -- When <match_type> is NOT_MATCHED_BY_SOURCE,
           -- Only columns from <table_scan> are allowed in <match_expr>.
           -- If <action_type> is UPDATE, only columns from <table_scan> are
              allowed in <update_item_list>.
              """,
      fields=[
          Field('match_type', SCALAR_MERGE_MATCH_TYPE, tag_id=2),
          Field('match_expr', 'ResolvedExpr', tag_id=3),
          Field('action_type', SCALAR_MERGE_ACTION_TYPE, tag_id=4),
          Field(
              'insert_column_list',
              SCALAR_RESOLVED_COLUMN,
              tag_id=5,
              ignorable=IGNORABLE_DEFAULT,
              vector=True),
          Field(
              'insert_row',
              'ResolvedInsertRow',
              tag_id=6,
              ignorable=IGNORABLE_DEFAULT),
          Field(
              'update_item_list',
              'ResolvedUpdateItem',
              tag_id=7,
              ignorable=IGNORABLE_DEFAULT,
              vector=True)
      ])

  gen.AddNode(
      name='ResolvedMergeStmt',
      tag_id=101,
      parent='ResolvedStatement',
      comment="""
      This represents a MERGE statement.

      <table_scan> gives the target table to scan and creates ResolvedColumns
      for its columns.

      <column_access_list> indicates for each column, whether it was read and/or
      written. The columns in this vector match those of
      <table_scan.column_list>. If a column was not encountered when producing
      the resolved AST, then the value at that index will be
      ResolvedStatement::NONE(0).

      The output of <from_scan> is joined with <table_scan> using the join
      expression <merge_expr>.

      The order of elements in <when_clause_list> matters, as they are executed
      sequentially. At most one of the <when_clause_list> clause will be applied
      to each row from <table_scan>.

      <table_scan>, <from_scan>, <merge_expr> and <when_clause_list> are
      required. <when_clause_list> must be non-empty.

      See (broken link) for more detail on MERGE statement.
              """,
      fields=[
          Field('table_scan', 'ResolvedTableScan', tag_id=2),
          Field(
              'column_access_list',
              SCALAR_OBJECT_ACCESS,
              tag_id=6,
              ignorable=IGNORABLE,
              vector=True,
              is_constructor_arg=False,
              java_to_string_method='toStringObjectAccess'),
          Field('from_scan', 'ResolvedScan', tag_id=3),
          Field('merge_expr', 'ResolvedExpr', tag_id=4),
          Field(
              'when_clause_list',
              'ResolvedMergeWhen',
              tag_id=5,
              vector=True)
      ])

  gen.AddNode(
      name='ResolvedTruncateStmt',
      tag_id=133,
      parent='ResolvedStatement',
      comment="""
      This represents a TRUNCATE TABLE statement.

      Statement:
        TRUNCATE TABLE <table_name> [WHERE <boolean_expression>]

      <table_scan> is a TableScan for the target table, which is used during
                   resolving and validation. Consumers can use either the table
                   object inside it or name_path to reference the table.
      <where_expr> boolean expression that can reference columns in
                   ResolvedColumns (which the TableScan creates); the
                   <where_expr> should always correspond to entire partitions,
                   and is optional.
              """,
      fields=[
          Field(
              'table_scan',
              'ResolvedTableScan',
              tag_id=3),
          Field('where_expr', 'ResolvedExpr', tag_id=4),
      ])

  gen.AddNode(
      name='ResolvedObjectUnit',
      tag_id=200,
      parent='ResolvedArgument',
      comment="""
      A reference to a unit of an object (e.g. a column or field of a table).

      <name_path> is a vector giving the identifier path of the object unit.
              """,
      fields=[
          Field('name_path', SCALAR_STRING, tag_id=2, vector=True),
      ])

  gen.AddNode(
      name='ResolvedPrivilege',
      tag_id=67,
      parent='ResolvedArgument',
      comment="""
      A grantable privilege.

      <action_type> is the type of privilege action, e.g. SELECT, INSERT, UPDATE
      or DELETE.
      <unit_list> is an optional list of units of the object (e.g. columns of a
      table, fields in a value table) that the privilege is scoped to. The
      privilege is on the whole object if the list is empty.
              """,
      fields=[
          Field('action_type', SCALAR_STRING, tag_id=2),
          Field(
              'unit_list', 'ResolvedObjectUnit', tag_id=3, vector=True)
      ])

  gen.AddNode(
      name='ResolvedGrantOrRevokeStmt',
      tag_id=68,
      parent='ResolvedStatement',
      is_abstract=True,
      comment="""
      Common superclass of GRANT/REVOKE statements.

      <privilege_list> is the list of privileges to be granted/revoked. ALL
      PRIVILEGES should be granted/fromed if it is empty.
      <object_type_list> is an optional list of string identifiers, e.g., TABLE,
      VIEW, MATERIALIZED VIEW.
      <name_path> is a vector of segments of the object identifier's pathname.
      <grantee_list> (DEPRECATED) is the list of grantees (strings).
      <grantee_expr_list> is the list of grantees, and may include parameters.

      Only one of <grantee_list> or <grantee_expr_list> will be populated,
      depending on whether or not the FEATURE_PARAMETERS_IN_GRANTEE_LIST
      is enabled.  The <grantee_list> is deprecated, and will be removed
      along with the corresponding FEATURE once all engines have migrated to
      use the <grantee_expr_list>.  Once <grantee_expr_list> is the only
      one, then it should be marked as NOT_IGNORABLE.
              """,
      fields=[
          Field('privilege_list', 'ResolvedPrivilege', tag_id=2, vector=True),
          Field('object_type_list', SCALAR_STRING, tag_id=3, vector=True),
          Field('name_path', SCALAR_STRING, tag_id=4, vector=True),
          Field(
              'grantee_list',
              SCALAR_STRING,
              ignorable=IGNORABLE_DEFAULT,
              tag_id=5,
              vector=True,
              to_string_method='ToStringCommaSeparated',
              java_to_string_method='toStringCommaSeparated',
          ),
          Field(
              'grantee_expr_list',
              'ResolvedExpr',
              ignorable=IGNORABLE_DEFAULT,
              tag_id=6,
              vector=True,
          ),
      ],
      extra_defs="""
      ABSL_DEPRECATED("Use `object_type_list()` instead")
      inline const std::string& object_type() const {
          return object_type_list()[0];
      }
         """,
  )

  gen.AddNode(
      name='ResolvedGrantStmt',
      tag_id=69,
      parent='ResolvedGrantOrRevokeStmt',
      comment="""
      A GRANT statement. It represents the action to grant a list of privileges
      on a specific object to/from list of grantees.
              """,
      fields=[])

  gen.AddNode(
      name='ResolvedRevokeStmt',
      tag_id=70,
      parent='ResolvedGrantOrRevokeStmt',
      comment="""
      A REVOKE statement. It represents the action to revoke a list of
      privileges on a specific object to/from list of grantees.
              """,
      fields=[])

  gen.AddNode(
      name='ResolvedAlterObjectStmt',
      tag_id=114,
      parent='ResolvedStatement',
      is_abstract=True,
      comment="""
      Common super class for statements:
        ALTER <object> [IF EXISTS] <name_path> <alter_action_list>

      <name_path> is a vector giving the identifier path in the table <name>. It
                  is optional if
                  FEATURE_ALLOW_MISSING_PATH_EXPRESSION_IN_ALTER_DDL is enabled.
      <alter_action_list> is a vector of actions to be done to the object.
      <is_if_exists> silently ignores the "name_path does not exist" error.
              """,
      fields=[
          Field(
              'name_path',
              SCALAR_STRING,
              tag_id=2,
              vector=True,
              ignorable=IGNORABLE_DEFAULT,
              is_optional_constructor_arg=True),
          Field(
              'alter_action_list',
              'ResolvedAlterAction',
              tag_id=3,
              vector=True),
          Field(
              'is_if_exists',
              SCALAR_BOOL,
              tag_id=4,
              ignorable=IGNORABLE_DEFAULT),
      ])

  gen.AddNode(
      name='ResolvedAlterDatabaseStmt',
      tag_id=134,
      parent='ResolvedAlterObjectStmt',
      comment="""
      This statement:
        ALTER DATABASE [IF EXISTS] <name_path> <alter_action_list>

      This statement could be used to change the database level options.
              """,
      fields=[])

  gen.AddNode(
      name='ResolvedAlterMaterializedViewStmt',
      tag_id=127,
      parent='ResolvedAlterObjectStmt',
      comment="""
      This statement:
        ALTER MATERIALIZED VIEW [IF EXISTS] <name_path> <alter_action_list>
              """,
      fields=[])

  gen.AddNode(
      name='ResolvedAlterApproxViewStmt',
      tag_id=236,
      parent='ResolvedAlterObjectStmt',
      comment="""
      This statement:
        ALTER APPROX VIEW [IF EXISTS] <name_path> <alter_action_list>
              """,
      fields=[],
  )

  gen.AddNode(
      name='ResolvedAlterSchemaStmt',
      tag_id=160,
      parent='ResolvedAlterObjectStmt',
      comment="""
      This statement:
        ALTER SCHEMA [IF NOT EXISTS] <name_path> <alter_action_list>;
              """,
      fields=[])

  gen.AddNode(
      name='ResolvedAlterModelStmt',
      tag_id=205,
      parent='ResolvedAlterObjectStmt',
      comment="""
      This statement:
        ALTER MODEL [IF EXISTS] <name_path> <alter_action_list>
              """,
      fields=[])

  gen.AddNode(
      name='ResolvedAlterTableStmt',
      tag_id=115,
      parent='ResolvedAlterObjectStmt',
      comment="""
      This statement:
        ALTER TABLE [IF EXISTS] <name_path> <alter_action_list>
              """,
      fields=[])

  gen.AddNode(
      name='ResolvedAlterViewStmt',
      tag_id=118,
      parent='ResolvedAlterObjectStmt',
      comment="""
      This statement:
        ALTER VIEW [IF EXISTS] <name_path> <alter_action_list>
              """,
      fields=[])

  gen.AddNode(
      name='ResolvedAlterAction',
      tag_id=116,
      parent='ResolvedArgument',
      is_abstract=True,
      comment="""
      A common super class for all actions in statement ALTER <object>
              """,
      fields=[])

  gen.AddNode(
      name='ResolvedAlterColumnAction',
      tag_id=201,
      parent='ResolvedAlterAction',
      is_abstract=True,
      comment="""
      A super class for all ALTER COLUMN actions in the ALTER TABLE statement:
        ALTER TABLE <table_name> ALTER COLUMN [IF EXISTS] <column>

      <is_if_exists> silently ignores the "column does not exist" error.
      <column> is the name of the column.
      """,
      fields=[
          Field(
              'is_if_exists',
              SCALAR_BOOL,
              tag_id=2,
              ignorable=IGNORABLE_DEFAULT,
          ),
          Field('column', SCALAR_STRING, tag_id=3)
      ])

  gen.AddNode(
      name='ResolvedSetOptionsAction',
      tag_id=117,
      parent='ResolvedAlterAction',
      comment="""
      SET OPTIONS action for ALTER <object> statement

      <option_list> has engine-specific directives that specify how to
                    alter the metadata for this object.
              """,
      fields=[
          Field('option_list', 'ResolvedOption', tag_id=2, vector=True),
      ])

  gen.AddNode(
      name='ResolvedAlterSubEntityAction',
      tag_id=202,
      parent='ResolvedAlterAction',
      comment="""
      Alter sub-entity action for ALTER <object> statement.
      (broken link)

      ALTER <entity_type> [IF EXISTS] <name> <alter_action>

      <entity_type> engine-specific sub-entity type to be altered.
      <name> the identifier for the sub-entity resource being altered.
      <alter_action> action for the sub-entity resource, such as
          SET OPTIONS or a further nested ALTER sub-entity action.
      <is_if_exists> if set, skip the alter action if the resource does
          not exist.
              """,
      fields=[
          Field(
              'entity_type',
              SCALAR_STRING,
              tag_id=2,
              ignorable=NOT_IGNORABLE),
          Field('name', SCALAR_STRING, tag_id=3),
          Field('alter_action', 'ResolvedAlterAction', tag_id=4),
          Field(
              'is_if_exists',
              SCALAR_BOOL,
              tag_id=5,
              ignorable=IGNORABLE_DEFAULT),
      ])

  gen.AddNode(
      name='ResolvedAddSubEntityAction',
      tag_id=203,
      parent='ResolvedAlterAction',
      comment="""
      Add sub-entity action for ALTER <object> statement.
      (broken link)

      ADD <entity_type> [IF NOT EXISTS] <name> [OPTIONS(...)]

      <entity_type> engine-specific sub-entity type to be added.
      <name> the identifier for the sub-entity resource being added.
      <options_list> engine specific options_list for the sub-entity resource.
      <is_if_not_exists> if set, skip the add action if the resource
          already exists.
              """,
      fields=[
          Field(
              'entity_type',
              SCALAR_STRING,
              tag_id=2,
              ignorable=NOT_IGNORABLE),
          Field('name', SCALAR_STRING, tag_id=3),
          Field(
              'options_list', 'ResolvedOption', tag_id=4, vector=True),
          Field(
              'is_if_not_exists',
              SCALAR_BOOL,
              tag_id=5,
              ignorable=IGNORABLE_DEFAULT),
      ])

  gen.AddNode(
      name='ResolvedDropSubEntityAction',
      tag_id=204,
      parent='ResolvedAlterAction',
      comment="""
      Drop sub-entity action for ALTER <object> statement.
      (broken link)

      DROP <entity_type> [IF EXISTS] <name>

      <entity_type> engine-specific sub-entity type to be dropped.
      <name> the identifier for the sub-entity resource being dropped.
      <is_if_exists> if set, skip the drop action if the resource does
          not exist.
              """,
      fields=[
          Field(
              'entity_type',
              SCALAR_STRING,
              tag_id=2,
              ignorable=NOT_IGNORABLE),
          Field('name', SCALAR_STRING, tag_id=3),
          Field(
              'is_if_exists',
              SCALAR_BOOL,
              tag_id=4,
              ignorable=IGNORABLE_DEFAULT),
      ])

  gen.AddNode(
      name='ResolvedAddColumnAction',
      tag_id=131,
      parent='ResolvedAlterAction',
      comment="""
      ADD COLUMN action for ALTER TABLE statement
              """,
      fields=[
          Field('is_if_not_exists', SCALAR_BOOL, tag_id=2),
          Field(
              'column_definition', 'ResolvedColumnDefinition', tag_id=3)
      ])

  gen.AddNode(
      name='ResolvedAddConstraintAction',
      tag_id=163,
      parent='ResolvedAlterAction',
      comment="""
      ADD CONSTRAINT for ALTER TABLE statement
              """,
      fields=[
          Field('is_if_not_exists', SCALAR_BOOL, tag_id=2),
          Field('constraint', 'ResolvedConstraint', tag_id=3),
          Field('table', SCALAR_TABLE, tag_id=4, ignorable=IGNORABLE_DEFAULT),
      ])

  gen.AddNode(
      name='ResolvedDropConstraintAction',
      tag_id=164,
      parent='ResolvedAlterAction',
      comment="""
      DROP CONSTRAINT for ALTER TABLE statement
              """,
      fields=[
          Field('is_if_exists', SCALAR_BOOL, tag_id=2),
          Field('name', SCALAR_STRING, tag_id=3),
      ])

  gen.AddNode(
      name='ResolvedDropPrimaryKeyAction',
      tag_id=184,
      parent='ResolvedAlterAction',
      comment="""
      DROP PRIMARY KEY [IF EXISTS] for ALTER TABLE statement
              """,
      fields=[
          Field('is_if_exists', SCALAR_BOOL, tag_id=2),
      ])

  gen.AddNode(
      name='ResolvedAlterColumnOptionsAction',
      tag_id=169,
      parent='ResolvedAlterColumnAction',
      comment="""
      This ALTER action:
        ALTER COLUMN [IF EXISTS] <column> SET OPTIONS <options_list>

      <options_list> has engine-specific directives that specify how to
                     alter the metadata for a column.
              """,
      fields=[
          Field('option_list', 'ResolvedOption', tag_id=2, vector=True),
      ])

  gen.AddNode(
      name='ResolvedAlterColumnDropNotNullAction',
      tag_id=178,
      parent='ResolvedAlterColumnAction',
      comment="""
      This ALTER action:
        ALTER COLUMN [IF EXISTS] <column> DROP NOT NULL

      Removes the NOT NULL constraint from the given column.
              """,
      fields=[])

  gen.AddNode(
      name='ResolvedAlterColumnDropGeneratedAction',
      tag_id=243,
      parent='ResolvedAlterColumnAction',
      comment="""
      This ALTER action:
        ALTER COLUMN [IF EXISTS] <column> DROP GENERATED

      Removes the generated value (either an expression or identity column)
      from the given column.
              """,
      fields=[])

  gen.AddNode(
      name='ResolvedAlterColumnSetDataTypeAction',
      tag_id=181,
      parent='ResolvedAlterColumnAction',
      comment="""
              ALTER COLUMN <column> SET DATA TYPE action for ALTER TABLE
              statement. It supports updating the data type of the column as
              well as updating type parameters and collation specifications of
              the column (and on struct fields and array elements).
              """,
      fields=[
          Field(
              'updated_type',
              SCALAR_TYPE,
              tag_id=4,
              comment='The new type for the column.'),
          Field(
              'updated_type_parameters',
              SCALAR_TYPE_PARAMETERS,
              tag_id=5,
              ignorable=IGNORABLE_DEFAULT,
              comment="""
              The new type parameters for the column, if the new type has
              parameters. Note that unlike with CREATE TABLE, the child_list is
              populated for ARRAY and STRUCT types.
              TODO Use updated_annotations to pass type parameters.
              """),
          Field(
              'updated_annotations',
              'ResolvedColumnAnnotations',
              tag_id=6,
              ignorable=IGNORABLE,
              comment="""
              The new annotations for the column including the new collation
              specifications. Changing options using SET DATA TYPE action is not
              allowed.
              """),
      ])

  gen.AddNode(
      name='ResolvedAlterColumnSetDefaultAction',
      tag_id=198,
      parent='ResolvedAlterColumnAction',
      comment="""
      Alter column set default action:
        ALTER COLUMN [IF EXISTS] <column> SET DEFAULT <default_value>

      <default_value> sets the new default value expression. It only impacts
      future inserted rows, and has no impact on existing rows with the current
      default value. This is a metadata only operation.

      Resolver validates that <default_value> expression can be coerced to the
      column type when <column> exists. If <column> is not found and
      <is_if_exists> is true, Resolver skips type match check.
              """,
      fields=[
          Field(
              'default_value', 'ResolvedColumnDefaultValue', tag_id=4),
      ])

  gen.AddNode(
      name='ResolvedAlterColumnDropDefaultAction',
      tag_id=199,
      parent='ResolvedAlterColumnAction',
      comment="""
      This ALTER action:
        ALTER COLUMN [IF EXISTS] <column> DROP DEFAULT

      Removes the DEFAULT constraint from the given column.
              """,
      fields=[])

  gen.AddNode(
      name='ResolvedDropColumnAction',
      tag_id=132,
      parent='ResolvedAlterAction',
      comment="""
      DROP COLUMN action for ALTER TABLE statement

      <name> is the name of the column to drop.
              """,
      fields=[
          Field('is_if_exists', SCALAR_BOOL, tag_id=2),
          Field('name', SCALAR_STRING, tag_id=3),
      ])

  gen.AddNode(
      name='ResolvedRenameColumnAction',
      tag_id=185,
      parent='ResolvedAlterAction',
      comment="""
      RENAME COLUMN action for ALTER TABLE statement.

      <name> is the name of the column to rename.
      <new_name> is the new name of the column.

      RENAME COLUMN actions cannot be part of the same alter_action_list as any
      other type of action.
      Chains of RENAME COLUMN will be interpreted as a sequence of mutations.
      The order of actions matters. Each <name> refers to a column name that
      exists after all preceding renames have been applied.
              """,
      fields=[
          Field(
              'is_if_exists',
              SCALAR_BOOL,
              tag_id=4,
              ignorable=IGNORABLE_DEFAULT,
          ),
          Field('name', SCALAR_STRING, tag_id=2),
          Field('new_name', SCALAR_STRING, tag_id=3),
      ])

  gen.AddNode(
      name='ResolvedSetAsAction',
      tag_id=156,
      parent='ResolvedAlterAction',
      comment="""
      SET AS action for generic ALTER <entity_type> statement.
      Exactly one of <entity_body_json>, <entity_body_text> should be non-empty.

      <entity_body_json> is a JSON literal to be interpreted by engine.
      <entity_body_text> is a text literal to be interpreted by engine.
              """,
      fields=[
          # TODO: convert type into JSON literal type when it is
          # ready.
          Field(
              'entity_body_json',
              SCALAR_STRING,
              tag_id=2,
              ignorable=IGNORABLE_DEFAULT),
          Field(
              'entity_body_text',
              SCALAR_STRING,
              tag_id=3,
              ignorable=IGNORABLE_DEFAULT),
      ])

  gen.AddNode(
      name='ResolvedSetCollateClause',
      tag_id=187,
      parent='ResolvedAlterAction',
      comment="""
      SET DEFAULT COLLATE clause for generic ALTER <entity_type> statement.

      <collation_name> specifies the new default collation specification for a
        table or schema. Modifying the default collation for a table or schema
        does not affect any existing columns or tables - the new default
        collation only affects new tables and/or columns if applicable. Only
        string literals are allowed for this field.
              """,
      fields=[
          Field(
              'collation_name',
              'ResolvedExpr',
              tag_id=2),
      ])

  gen.AddNode(
      name='ResolvedAlterTableSetOptionsStmt',
      tag_id=71,
      parent='ResolvedStatement',
      comment="""
      This statement:
        ALTER TABLE [IF EXISTS] <name> SET OPTIONS (...)

      NOTE: This is deprecated in favor of ResolvedAlterTableStmt.

      <name_path> is a vector giving the identifier path in the table <name>.
      <option_list> has engine-specific directives that specify how to
                    alter the metadata for this table.
      <is_if_exists> silently ignore the "name_path does not exist" error.
              """,
      fields=[
          Field('name_path', SCALAR_STRING, tag_id=2, vector=True),
          Field('option_list', 'ResolvedOption', tag_id=3, vector=True),
          Field(
              'is_if_exists',
              SCALAR_BOOL,
              tag_id=4,
              ignorable=IGNORABLE_DEFAULT)
      ])

  gen.AddNode(
      name='ResolvedRenameStmt',
      tag_id=72,
      parent='ResolvedStatement',
      comment="""
      This statement: RENAME <object_type> <old_name_path> TO <new_name_path>;

      <object_type> is an string identifier,
                    e.g., "TABLE", "VIEW", "INDEX", "FUNCTION", "TYPE", etc.
      <old_name_path> is a vector giving the identifier path for the object to
                      be renamed.
      <new_name_path> is a vector giving the identifier path for the object to
                      be renamed to.
              """,
      fields=[
          Field('object_type', SCALAR_STRING, tag_id=2),
          Field('old_name_path', SCALAR_STRING, tag_id=3, vector=True),
          Field('new_name_path', SCALAR_STRING, tag_id=4, vector=True)
      ])

  gen.AddNode(
      name='ResolvedCreatePrivilegeRestrictionStmt',
      tag_id=191,
      parent='ResolvedCreateStatement',
      comment="""
      This statement:
          CREATE [OR REPLACE] PRIVILEGE RESTRICTION [IF NOT EXISTS]
          ON <column_privilege_list> ON <object_type> <name_path>
          [RESTRICT TO (<restrictee_list>)]

      <column_privilege_list> is the name of the column privileges on which
                              to apply the restrictions.
      <object_type> is a string identifier, which is currently either TABLE or
                    VIEW, which tells the engine how to look up the name.
      <restrictee_list> is a list of users and groups the privilege restrictions
                        should apply to. Each restrictee is either a string
                        literal or a parameter.
              """,
      fields=[
          Field(
              'column_privilege_list',
              'ResolvedPrivilege',
              tag_id=2,
              vector=True),
          Field('object_type', SCALAR_STRING, tag_id=3),
          Field(
              'restrictee_list',
              'ResolvedExpr',
              ignorable=IGNORABLE_DEFAULT,
              tag_id=4,
              vector=True),
      ])

  gen.AddNode(
      name='ResolvedCreateRowAccessPolicyStmt',
      tag_id=73,
      parent='ResolvedStatement',
      comment="""
      This statement: CREATE [OR REPLACE] ROW ACCESS POLICY [IF NOT EXISTS]
                      [<name>] ON <target_name_path>
                      [GRANT TO (<grantee_list>)]
                      FILTER USING (<predicate>);

      <create_mode> indicates if this was CREATE, CREATE OR REPLACE, or
                    CREATE IF NOT EXISTS.
      <name> is the name of the row access policy to be created or an empty
             string.
      <target_name_path> is a vector giving the identifier path of the target
                         table.
      <table_scan> is a TableScan for the target table, which is used during
                   resolving and validation. Consumers can use either the table
                   object inside it or target_name_path to reference the table.
      <grantee_list> (DEPRECATED) is the list of user principals the policy
                     should apply to.
      <grantee_expr_list> is the list of user principals the policy should
                          apply to, and may include parameters.
      <predicate> is a boolean expression that selects the rows that are being
                  made visible.
      <predicate_str> is the string form of the predicate.

      Only one of <grantee_list> or <grantee_expr_list> will be populated,
      depending on whether or not the FEATURE_PARAMETERS_IN_GRANTEE_LIST
      is enabled.  The <grantee_list> is deprecated, and will be removed
      along with the corresponding FEATURE once all engines have migrated to
      use the <grantee_expr_list>.  Once <grantee_expr_list> is the only
      one, then it should be marked as NOT_IGNORABLE.
              """,
      fields=[
          Field(
              'create_mode',
              SCALAR_CREATE_MODE,
              tag_id=2,
              ignorable=IGNORABLE_DEFAULT),
          Field(
              'name',
              SCALAR_STRING,
              ignorable=IGNORABLE_DEFAULT,
              tag_id=3),
          Field(
              'target_name_path', SCALAR_STRING, tag_id=4, vector=True),
          Field(
              'grantee_list',
              SCALAR_STRING,
              ignorable=IGNORABLE_DEFAULT,
              tag_id=5,
              vector=True,
              to_string_method='ToStringCommaSeparated',
              java_to_string_method='toStringCommaSeparated'),
          Field(
              'grantee_expr_list',
              'ResolvedExpr',
              ignorable=IGNORABLE_DEFAULT,
              tag_id=9,
              vector=True),
          Field(
              'table_scan',
              'ResolvedTableScan',
              tag_id=6,
              ignorable=IGNORABLE),
          Field(
              'predicate',
              'ResolvedExpr',
              tag_id=7,
              ignorable=IGNORABLE),
          Field(
              'predicate_str',
              SCALAR_STRING,
              tag_id=8,
              ignorable=IGNORABLE)
      ],
      extra_defs="""
  typedef ResolvedCreateStatement::CreateMode CreateMode;""")

  gen.AddNode(
      name='ResolvedDropPrivilegeRestrictionStmt',
      tag_id=192,
      parent='ResolvedStatement',
      comment="""
      This statement:
          DROP PRIVILEGE RESTRICTION [IF EXISTS]
          ON <column_privilege_list> ON <object_type> <name_path>

      <column_privilege_list> is the name of the column privileges on which
                              the restrictions have been applied.
      <object_type> is a string identifier, which is currently either TABLE or
                    VIEW, which tells the engine how to look up the name.
      <name_path> is the name of the table the restrictions are scoped to.
              """,
      fields=[
          Field('object_type', SCALAR_STRING, tag_id=2),
          Field(
              'is_if_exists',
              SCALAR_BOOL,
              ignorable=IGNORABLE_DEFAULT,
              tag_id=3),
          Field('name_path', SCALAR_STRING, tag_id=4, vector=True),
          Field(
              'column_privilege_list',
              'ResolvedPrivilege',
              tag_id=5,
              vector=True),
      ])

  gen.AddNode(
      name='ResolvedDropRowAccessPolicyStmt',
      tag_id=74,
      parent='ResolvedStatement',
      comment="""
      This statement:
          DROP ROW ACCESS POLICY <name> ON <target_name_path>; or
          DROP ALL ROW [ACCESS] POLICIES ON <target_name_path>;

      <is_drop_all> indicates that all policies should be dropped.
      <is_if_exists> silently ignore the "policy <name> does not exist" error.
                     This is not allowed if is_drop_all is true.
      <name> is the name of the row policy to be dropped or an empty string.
      <target_name_path> is a vector giving the identifier path of the target
                         table.
              """,
      fields=[
          Field(
              'is_drop_all',
              SCALAR_BOOL,
              ignorable=IGNORABLE_DEFAULT,
              tag_id=2),
          Field(
              'is_if_exists',
              SCALAR_BOOL,
              ignorable=IGNORABLE_DEFAULT,
              tag_id=3),
          Field(
              'name', SCALAR_STRING, ignorable=IGNORABLE_DEFAULT, tag_id=4),
          Field(
              'target_name_path', SCALAR_STRING, tag_id=5, vector=True)
      ])

  gen.AddNode(
      name='ResolvedDropIndexStmt',
      tag_id=242,
      parent='ResolvedStatement',
      comment="""
      DROP SEARCH|VECTOR INDEX [IF EXISTS] <name> [ON <table_name_path>];
      Note: DROP INDEX without SEARCH or VECTOR is currently resolved to a
      generic ResolvedDropStmt. The index_type currently would never be
      INDEX_DEFAULT.

      <name> is the name of the search index to be dropped.
      <table_name_path> is a vector giving the identifier path of the target
                        table.
              """,
      fields=[
          Field(
              'is_if_exists',
              SCALAR_BOOL,
              tag_id=2),
          Field('name', SCALAR_STRING, tag_id=3),
          Field(
              'table_name_path',
              SCALAR_STRING,
              tag_id=4,
              vector=True),
          Field(
              'index_type',
              SCALAR_INDEX_TYPE,
              ignorable=IGNORABLE_DEFAULT,
              tag_id=5,
          ),
      ],
      extra_defs="""
  std::string GetIndexTypeString() const;
  static std::string IndexTypeToString(IndexType index_type);""",
  )

  gen.AddNode(
      name='ResolvedGrantToAction',
      tag_id=135,
      parent='ResolvedAlterAction',
      comment="""
      GRANT TO action for ALTER ROW ACCESS POLICY statement

      <grantee_expr_list> is the list of grantees, and may include parameters.
              """,
      fields=[
          Field(
              'grantee_expr_list',
              'ResolvedExpr',
              ignorable=NOT_IGNORABLE,
              tag_id=2,
              vector=True),
      ])

  gen.AddNode(
      name='ResolvedRestrictToAction',
      tag_id=193,
      parent='ResolvedAlterAction',
      comment="""
      This action for ALTER PRIVILEGE RESTRICTION statement:
          RESTRICT TO <restrictee_list>

      <restrictee_list> is a list of users and groups the privilege restrictions
                        should apply to. Each restrictee is either a string
                        literal or a parameter.
             """,
      fields=[
          Field(
              'restrictee_list',
              'ResolvedExpr',
              ignorable=IGNORABLE_DEFAULT,
              tag_id=2,
              vector=True),
      ])

  gen.AddNode(
      name='ResolvedAddToRestricteeListAction',
      tag_id=194,
      parent='ResolvedAlterAction',
      comment="""
      This action for ALTER PRIVILEGE RESTRICTION statement:
          ADD [IF NOT EXISTS] <restrictee_list>

      <restrictee_list> is a list of users and groups the privilege restrictions
                        should apply to. Each restrictee is either a string
                        literal or a parameter.
              """,
      fields=[
          Field(
              'is_if_not_exists',
              SCALAR_BOOL,
              ignorable=IGNORABLE_DEFAULT,
              tag_id=2),
          Field(
              'restrictee_list',
              'ResolvedExpr',
              ignorable=IGNORABLE_DEFAULT,
              tag_id=3,
              vector=True),
      ])

  gen.AddNode(
      name='ResolvedRemoveFromRestricteeListAction',
      tag_id=195,
      parent='ResolvedAlterAction',
      comment="""
      This action for ALTER PRIVILEGE RESTRICTION statement:
          REMOVE [IF EXISTS] <restrictee_list>

      <restrictee_list> is a list of users and groups the privilege restrictions
                        should apply to. Each restrictee is either a string
                        literal or a parameter.
              """,
      fields=[
          Field(
              'is_if_exists',
              SCALAR_BOOL,
              ignorable=IGNORABLE_DEFAULT,
              tag_id=2),
          Field(
              'restrictee_list',
              'ResolvedExpr',
              ignorable=IGNORABLE_DEFAULT,
              tag_id=3,
              vector=True),
      ])

  gen.AddNode(
      name='ResolvedFilterUsingAction',
      tag_id=136,
      parent='ResolvedAlterAction',
      comment="""
      FILTER USING action for ALTER ROW ACCESS POLICY statement

      <predicate> is a boolean expression that selects the rows that are being
                  made visible.
      <predicate_str> is the string form of the predicate.
              """,
      fields=[
          Field(
              'predicate', 'ResolvedExpr', tag_id=2, ignorable=IGNORABLE),
          Field(
              'predicate_str',
              SCALAR_STRING,
              tag_id=3,
              ignorable=NOT_IGNORABLE)
      ])

  gen.AddNode(
      name='ResolvedRevokeFromAction',
      tag_id=137,
      parent='ResolvedAlterAction',
      comment="""
      REVOKE FROM action for ALTER ROW ACCESS POLICY statement

      <revokee_expr_list> is the list of revokees, and may include parameters.
      <is_revoke_from_all> is a boolean indicating whether it was a REVOKE FROM
                           ALL statement.
              """,
      fields=[
          Field(
              'revokee_expr_list',
              'ResolvedExpr',
              ignorable=IGNORABLE_DEFAULT,
              tag_id=2,
              vector=True),
          Field(
              'is_revoke_from_all',
              SCALAR_BOOL,
              ignorable=IGNORABLE_DEFAULT,
              tag_id=3)
      ])

  gen.AddNode(
      name='ResolvedRenameToAction',
      tag_id=138,
      parent='ResolvedAlterAction',
      comment="""
      RENAME TO action for ALTER ROW ACCESS POLICY statement
              and ALTER TABLE statement

      <new_path> is the new name of the row access policy,
              or the new path of the table.
              """,
      fields=[
          Field(
              'new_path',
              SCALAR_STRING,
              vector=True,
              ignorable=NOT_IGNORABLE,
              tag_id=2),
      ])

  gen.AddNode(
      name='ResolvedAlterPrivilegeRestrictionStmt',
      tag_id=196,
      parent='ResolvedAlterObjectStmt',
      comment="""
      This statement:
          ALTER PRIVILEGE RESTRICTION [IF EXISTS]
          ON <column_privilege_list> ON <object_type> <name_path>
          <alter_action_list>

      <column_privilege_list> is the name of the column privileges on which
                              the restrictions have been applied.
      <object_type> is a string identifier, which is currently either TABLE or
                    VIEW, which tells the engine how to look up the name.
              """,
      fields=[
          Field(
              'column_privilege_list',
              'ResolvedPrivilege',
              tag_id=2,
              vector=True),
          Field('object_type', SCALAR_STRING, tag_id=3),
      ])

  gen.AddNode(
      name='ResolvedAlterRowAccessPolicyStmt',
      tag_id=75,
      parent='ResolvedAlterObjectStmt',
      comment="""
      This statement:
          ALTER ROW ACCESS POLICY [IF EXISTS]
          <name> ON <name_path>
          <alter_action_list>

      <name> is the name of the row access policy to be altered, scoped to the
             table in the base <name_path>.
      <table_scan> is a TableScan for the target table, which is used during
                   resolving and validation. Consumers can use either the table
                   object inside it or base <name_path> to reference the table.
              """,
      fields=[
          Field('name', SCALAR_STRING, tag_id=2),
          Field(
              'table_scan',
              'ResolvedTableScan',
              tag_id=6,
              ignorable=IGNORABLE),
      ])

  gen.AddNode(
      name='ResolvedAlterAllRowAccessPoliciesStmt',
      tag_id=145,
      parent='ResolvedAlterObjectStmt',
      comment="""
      This statement:
          ALTER ALL ROW ACCESS POLICIES ON <name_path> <alter_action_list>

      <name_path> is a vector giving the identifier path in the table name.
      <alter_action_list> is a vector of actions to be done to the object. It
                          must have exactly one REVOKE FROM action with either
                          a non-empty grantee list or 'all'.
      <table_scan> is a TableScan for the target table, which is used during
                   resolving and validation. Consumers can use either the table
                   object inside it or base <name_path> to reference the table.
              """,
      fields=[
          Field(
              'table_scan',
              'ResolvedTableScan',
              tag_id=6,
              ignorable=IGNORABLE),
      ])

  gen.AddNode(
      name='ResolvedCreateConstantStmt',
      tag_id=99,
      parent='ResolvedCreateStatement',
      emit_default_constructor=False,
      comment="""
      This statement creates a user-defined named constant:
        CREATE [OR REPLACE] [TEMP | TEMPORARY | PUBLIC | PRIVATE] CONSTANT
          [IF NOT EXISTS] <name_path> = <expression>

        <name_path> is the identifier path of the named constants.
        <expr> is the expression that determines the type and the value of the
               named constant. Note that <expr> need not be constant. Its value
               is bound to the named constant which is then treated as
               immutable. <expr> can be evaluated at the time this statement is
               processed or later (lazy evaluation during query execution).
              """,
      fields=[Field('expr', 'ResolvedExpr', tag_id=2)])

  gen.AddNode(
      name='ResolvedCreateFunctionStmt',
      tag_id=76,
      parent='ResolvedCreateStatement',
      emit_default_constructor=False,
      comment="""
      This statement creates a user-defined function:
        CREATE [TEMP] FUNCTION [IF NOT EXISTS] <name_path> (<arg_list>)
          [RETURNS <return_type>] [SQL SECURITY <sql_security>]
          [<determinism_level>]
          [[LANGUAGE <language>] [AS <code> | AS ( <function_expression> )]
           | REMOTE [WITH CONNECTION <connection>]]
          [OPTIONS (<option_list>)]

        <name_path> is the identifier path of the function.
        <has_explicit_return_type> is true iff RETURNS clause is present.
        <return_type> is the return type for the function, which can be any
               valid ZetaSQL type, including ARRAY or STRUCT. It is inferred
               from <function_expression> if not explicitly set.
               TODO: Deprecate and remove this. The return type is
               already specified by the <signature>.
        <argument_name_list> The names of the function arguments.
        <signature> is the FunctionSignature of the created function, with all
               options.  This can be used to create a Function to load into a
               Catalog for future queries.
        <is_aggregate> is true if this is an aggregate function.  All arguments
               are assumed to be aggregate input arguments that may vary for
               every row.
        <language> is the programming language used by the function. This field
               is set to 'SQL' for SQL functions and 'REMOTE' for remote
               functions and otherwise to the language name specified in the
               LANGUAGE clause. This field is set to 'REMOTE' iff <is_remote> is
               set to true.
        <code> is a string literal that contains the function definition.  Some
               engines may allow this argument to be omitted for certain types
               of external functions. This will always be set for SQL functions.
        <aggregate_expression_list> is a list of SQL aggregate functions to
               compute prior to computing the final <function_expression>.
               See below.
        <function_expression> is the resolved SQL expression invoked for the
               function. This will be unset for external language functions. For
               non-template SQL functions, this is a resolved representation of
               the expression in <code>.
        <option_list> has engine-specific directives for modifying functions.
        <sql_security> is the declared security mode for the function. Values
               include 'INVOKER', 'DEFINER'.
        <determinism_level> is the declared determinism level of the function.
               Values are 'DETERMINISTIC', 'NOT DETERMINISTIC', 'IMMUTABLE',
               'STABLE', 'VOLATILE'.
        <is_remote> is true if this is an remote function. It is true iff its
               <language> is set to 'REMOTE'.
        <connection> is the identifier path of the connection object. It can be
               only set when <is_remote> is true.

      Note that <function_expression> and <code> are both marked as IGNORABLE
      because an engine could look at either one (but might not look at both).
      An engine must look at one (and cannot ignore both, unless the function is
      remote) to be semantically valid, but there is currently no way to enforce
      that.

      For aggregate functions, <is_aggregate> will be true.
      Aggregate functions will only occur if LanguageOptions has
      FEATURE_CREATE_AGGREGATE_FUNCTION enabled.

      Arguments to aggregate functions must have
      <FunctionSignatureArgumentTypeOptions::is_not_aggregate> true or false.
      Non-aggregate arguments must be passed constant values only.

      For SQL aggregate functions, there will be both an
      <aggregate_expression_list>, with aggregate expressions to compute first,
      and then a final <function_expression> to compute on the results
      of the aggregates.  Each aggregate expression is a
      ResolvedAggregateFunctionCall, and may reference any input arguments.
      Each ResolvedComputedColumn in <aggregate_expression_list> gives the
      aggregate expression a column id.  The final <function_expression> can
      reference these created aggregate columns, and any input arguments
      with <argument_kind>=NOT_AGGREGATE.

      For example, with
        CREATE TEMP FUNCTION my_avg(x) = (SUM(x) / COUNT(x));
      we would have an <aggregate_expression_list> with
        agg1#1 := SUM(ResolvedArgumentRef(x))
        agg2#2 := COUNT(ResolvedArgumentRef(x))
      and a <function_expression>
        ResolvedColumnRef(agg1#1) / ResolvedColumnRef(agg2#2)

      For example, with
        CREATE FUNCTION scaled_avg(x,y NOT AGGREGATE) = (SUM(x) / COUNT(x) * y);
      we would have an <aggregate_expression_list> with
        agg1#1 := SUM(ResolvedArgumentRef(x))
        agg2#2 := COUNT(ResolvedArgumentRef(x))
      and a <function_expression>
        ResolvedColumnRef(agg1#1) / ResolvedColumnRef(agg2#2) * \
ResolvedArgumentRef(y)

      When resolving a query that calls an aggregate UDF, the query will
      have a ResolvedAggregateScan that invokes the UDF function.  The engine
      should remove the UDF aggregate function from the <aggregate_list>, and
      instead compute the additional aggregates from the
      UDF's <aggregate_expression_list>, and then add an additional Project
      to compute the final <function_expression>, which should produce the
      value for the original ResolvedAggregateScan's computed column for the
      UDF.  Some rewrites of the ResolvedColumn references inside the UDF will
      be required.  TODO If using ResolvedColumns makes this renaming
      too complicated, we could switch to use ResolvedArgumentRefs, or
      something new.
              """,
      fields=[
          Field(
              'has_explicit_return_type',
              SCALAR_BOOL,
              tag_id=13,
              ignorable=IGNORABLE),
          Field(
              'return_type',
              SCALAR_TYPE,
              tag_id=3,
              is_optional_constructor_arg=True,
              ignorable=IGNORABLE),
          Field(
              'argument_name_list',
              SCALAR_STRING,
              ignorable=IGNORABLE,
              vector=True,
              tag_id=11,
              to_string_method='ToStringCommaSeparated'),
          Field(
              'signature',
              SCALAR_FUNCTION_SIGNATURE,
              tag_id=10,
              ignorable=IGNORABLE,
              to_string_method='ToStringVerbose'),
          Field(
              'is_aggregate',
              SCALAR_BOOL,
              ignorable=IGNORABLE_DEFAULT,
              tag_id=8),
          Field(
              'language',
              SCALAR_STRING,
              ignorable=IGNORABLE_DEFAULT,
              tag_id=4),
          Field('code', SCALAR_STRING, ignorable=IGNORABLE, tag_id=5),
          Field(
              'aggregate_expression_list',
              'ResolvedComputedColumn',
              tag_id=9,
              vector=True,
              ignorable=IGNORABLE_DEFAULT),
          Field(
              'function_expression',
              'ResolvedExpr',
              ignorable=IGNORABLE,
              tag_id=6),
          Field(
              'option_list',
              'ResolvedOption',
              ignorable=IGNORABLE_DEFAULT,
              tag_id=7,
              vector=True),
          Field(
              'sql_security',
              SCALAR_SQL_SECURITY,
              tag_id=12,
              ignorable=IGNORABLE_DEFAULT),
          Field(
              'determinism_level',
              SCALAR_DETERMINISM_LEVEL,
              tag_id=14,
              ignorable=IGNORABLE_DEFAULT),
          Field(
              'is_remote',
              SCALAR_BOOL,
              tag_id=15,
              # This is ignorable because existing consumers get this
              # information from language=REMOTE.
              ignorable=IGNORABLE),
          Field(
              'connection',
              'ResolvedConnection',
              tag_id=16,
              ignorable=IGNORABLE_DEFAULT),
      ],
      extra_defs="""
        // Converts the function's determinism level into a volatility.
        // Functions with unspecified/non deterministic/volatile
        // specifiers are considered volatile, functions with deterministic
        // and immutable specifiers are considered immutable and functions
        // with the stable specifier are considered stable.
        FunctionEnums::Volatility volatility() const;
      """,
  )

  gen.AddNode(
      name='ResolvedArgumentDef',
      tag_id=77,
      parent='ResolvedArgument',
      comment="""
      This represents an argument definition, e.g. in a function's argument
      list.

      <name> is the name of the argument; optional for DROP FUNCTION statements.
      <type> is the type of the argument.
      <argument_kind> indicates what kind of argument this is, including scalar
              vs aggregate.  NOT_AGGREGATE means this is a non-aggregate
              argument in an aggregate function, which can only passed constant
              values only.

      NOTE: Statements that create functions now include a FunctionSignature
      directly, and an argument_name_list if applicable.  These completely
      describe the function signature, so the ResolvedArgumentDef list can
      be considered unnecessary and deprecated.
      TODO We could remove this node in the future.
              """,
      fields=[
          Field('name', SCALAR_STRING, tag_id=2, ignorable=IGNORABLE),
          Field('type', SCALAR_TYPE, tag_id=3),
          Field(
              'argument_kind',
              SCALAR_ARGUMENT_KIND,
              tag_id=4,
              ignorable=IGNORABLE_DEFAULT)
      ])

  gen.AddNode(
      name='ResolvedArgumentRef',
      tag_id=78,
      parent='ResolvedExpr',
      comment="""
      This represents an argument reference, e.g. in a function's body.
      <name> is the name of the argument.
      <argument_kind> is the ArgumentKind from the ResolvedArgumentDef.
              For scalar functions, this is always SCALAR.
              For aggregate functions, it can be AGGREGATE or NOT_AGGREGATE.
              If NOT_AGGREGATE, then this is a non-aggregate argument
              to an aggregate function, which has one constant value
              for the entire function call (over all rows in all groups).
              (This is copied from the ResolvedArgumentDef for convenience.)
              """,
      fields=[
          Field('name', SCALAR_STRING, tag_id=2),
          Field(
              'argument_kind',
              SCALAR_ARGUMENT_KIND,
              ignorable=IGNORABLE,
              tag_id=3)
      ])

  gen.AddNode(
      name='ResolvedCreateTableFunctionStmt',
      tag_id=88,
      parent='ResolvedCreateStatement',
      emit_default_constructor=False,
      comment="""
      This statement creates a user-defined table-valued function:
        CREATE [TEMP] TABLE FUNCTION [IF NOT EXISTS]
          <name_path> (<argument_name_list>)
          [RETURNS <return_type>]
          [OPTIONS (<option_list>)]
          [LANGUAGE <language>]
          [AS <code> | AS ( <query> )]

        <argument_name_list> contains the names of the function arguments.
        <signature> is the FunctionSignature of the created function, with all
               options.  This can be used to create a Function to load into a
               Catalog for future queries.
        <option_list> has engine-specific directives for modifying functions.
        <language> is the programming language used by the function. This field
               is set to 'SQL' for SQL functions, to the language name specified
               in the LANGUAGE clause if present, and to 'UNDECLARED' if both
               the LANGUAGE clause and query are not present.
        <code> is an optional string literal that contains the function
               definition.  Some engines may allow this argument to be omitted
               for certain types of external functions.  This will always be set
               for SQL functions.
        <query> is the SQL query invoked for the function.  This will be unset
               for external language functions. For non-templated SQL functions,
               this is a resolved representation of the query in <code>.
        <output_column_list> is the list of resolved output
               columns returned by the table-valued function.
        <is_value_table> If true, this function returns a value table.
               Rather than producing rows with named columns, it produces
               rows with a single unnamed value type. <output_column_list> will
               have exactly one anonymous column (with no name).
               See (broken link).
        <sql_security> is the declared security mode for the function. Values
               include 'INVOKER', 'DEFINER'.
        <has_explicit_return_schema> is true iff RETURNS clause is present.

      ----------------------
      Table-Valued Functions
      ----------------------

      This is a statement to create a new table-valued function. Each
      table-valued function returns an entire table as output instead of a
      single scalar value. Table-valued functions can only be created if
      LanguageOptions has FEATURE_CREATE_TABLE_FUNCTION enabled.

      For SQL table-valued functions that include a defined SQL body, the
      <query> is non-NULL and contains the resolved SQL body.
      In this case, <output_column_list> contains a list of the
      output columns of the SQL body. The <query> uses
      ResolvedArgumentRefs to refer to scalar arguments and
      ResolvedRelationArgumentScans to refer to relation arguments.

      The table-valued function may include RETURNS TABLE<...> to explicitly
      specify a schema for the output table returned by the function. If the
      function declaration includes a SQL body, then the names and types of the
      output columns of the corresponding <query> will have been
      coerced to exactly match 1:1 with the names and types of the columns
      specified in the RETURNS TABLE<...> section.

      When resolving a query that calls a table-valued function, the query will
      have a ResolvedTVFScan that invokes the function.

      Value tables: If the function declaration includes a value-table
      parameter, this is written as an argument of type "TABLE" where the table
      contains a single anonymous column with a type but no name. In this case,
      calls to the function may pass a (regular or value) table with a single
      (named or unnamed) column for any of these parameters, and ZetaSQL
      accepts these arguments as long as the column type matches.

      Similarly, if the CREATE TABLE FUNCTION statement includes a "RETURNS
      TABLE" section with a single column with no name, then this defines a
      value-table return type. The function then returns a value table as long
      as the SQL body returns a single column whose type matches (independent of
      whether the SQL body result is a value table or not, and whether the
      returned column is named or unnamed).

      --------------------------------
      Templated Table-Valued Functions
      --------------------------------

      ZetaSQL supports table-valued function declarations with parameters of
      type ANY TABLE. This type indicates that any schema is valid for tables
      passed for this parameter. In this case:

      * the IsTemplated() method of the <signature> field returns true,
      * the <output_column_list> field is empty,
      * the <is_value_table> field is set to a default value of false (since
        ZetaSQL cannot analyze the function body in the presence of templated
        parameters, it is not possible to detect this property yet),

      TODO: Update this description once ZetaSQL supports more types
      of templated function parameters. Currently only ANY TABLE is supported.
              """,
      fields=[
          Field(
              'argument_name_list',
              SCALAR_STRING,
              ignorable=IGNORABLE,
              vector=True,
              tag_id=2,
              to_string_method='ToStringCommaSeparated'),
          Field(
              'signature',
              SCALAR_FUNCTION_SIGNATURE,
              tag_id=3,
              ignorable=IGNORABLE,
              to_string_method='ToStringVerbose'),
          Field(
              'has_explicit_return_schema',
              SCALAR_BOOL,
              tag_id=11,
              ignorable=IGNORABLE),
          Field(
              'option_list',
              'ResolvedOption',
              ignorable=IGNORABLE_DEFAULT,
              tag_id=4,
              vector=True),
          Field(
              'language',
              SCALAR_STRING,
              ignorable=IGNORABLE_DEFAULT,
              tag_id=5),
          Field('code', SCALAR_STRING, ignorable=IGNORABLE, tag_id=6),
          Field(
              'query',
              'ResolvedScan',
              ignorable=IGNORABLE_DEFAULT,
              tag_id=7),
          Field(
              'output_column_list',
              'ResolvedOutputColumn',
              tag_id=8,
              vector=True),
          Field(
              'is_value_table',
              SCALAR_BOOL,
              tag_id=9,
              ignorable=IGNORABLE_DEFAULT),
          Field(
              'sql_security',
              SCALAR_SQL_SECURITY,
              tag_id=10,
              ignorable=IGNORABLE_DEFAULT)
      ])

  gen.AddNode(
      name='ResolvedRelationArgumentScan',
      tag_id=89,
      parent='ResolvedScan',
      comment="""
      This represents a relation argument reference in a table-valued function's
      body. The 'column_list' of this ResolvedScan includes column names from
      the relation argument in the table-valued function signature.
              """,
      fields=[
          Field(
              'name',
              SCALAR_STRING,
              tag_id=2,
              comment="""
              This is the name of the relation argument for the table-valued
              function.  It is used to match this relation argument reference in
              a TVF SQL function body with one of possibly several relation
              arguments in the TVF call.
                      """),
          Field(
              'is_value_table',
              SCALAR_BOOL,
              tag_id=3,
              ignorable=IGNORABLE_DEFAULT,
              comment="""
              If true, the result of this query is a value table. Rather than
              producing rows with named columns, it produces rows with a single
              unnamed value type. See (broken link).
                      """)
      ])

  gen.AddNode(
      name='ResolvedArgumentList',
      tag_id=79,
      parent='ResolvedArgument',
      comment="""
      This statement: [ (<arg_list>) ];

      <arg_list> is an optional list of parameters.  If given, each parameter
                 may consist of a type, or a name and a type.

      NOTE: This can be considered deprecated in favor of the FunctionSignature
            stored directly in the statement.

      NOTE: ResolvedArgumentList is not related to the ResolvedArgument class,
            which just exists to organize node classes.
             """,
      fields=[
          Field(
              'arg_list',
              'ResolvedArgumentDef',
              tag_id=2,
              vector=True,
              ignorable=IGNORABLE)
      ])

  gen.AddNode(
      name='ResolvedFunctionSignatureHolder',
      tag_id=84,
      parent='ResolvedArgument',
      emit_default_constructor=False,
      comment="""
      This wrapper is used for an optional FunctionSignature.
              """,
      fields=[
          Field(
              'signature',
              SCALAR_FUNCTION_SIGNATURE,
              tag_id=2,
              to_string_method='ToStringVerbose')
      ])

  gen.AddNode(
      name='ResolvedDropFunctionStmt',
      tag_id=80,
      parent='ResolvedStatement',
      comment="""
      This statement: DROP FUNCTION [IF EXISTS] <name_path>
        [ (<arguments>) ];

      <is_if_exists> silently ignore the "name_path does not exist" error.
      <name_path> is the identifier path of the function to be dropped.
      <arguments> is an optional list of parameters.  If given, each parameter
                 may consist of a type, or a name and a type.  The name is
                 disregarded, and is allowed to permit copy-paste from CREATE
                 FUNCTION statements.
      <signature> is the signature of the dropped function.  Argument names and
                 argument options are ignored because only the types matter
                 for matching signatures in DROP FUNCTION.  The return type
                 in this signature will always be <void>, since return type
                 is ignored when matching signatures for DROP.
                 TODO <arguments> could be deprecated in favor of this.
              """,
      fields=[
          Field('is_if_exists', SCALAR_BOOL, tag_id=2),
          Field('name_path', SCALAR_STRING, tag_id=3, vector=True),
          Field(
              'arguments',
              'ResolvedArgumentList',
              tag_id=4,
              ignorable=IGNORABLE,
              comment="""
              NOTE: arguments for DROP FUNCTION statements are matched only on
              type; names for any arguments in ResolvedArgumentList will be set
              to the empty string irrespective of whether or not argument names
              were given in the DROP FUNCTION statement.
                      """),
          Field(
              'signature',
              'ResolvedFunctionSignatureHolder',
              tag_id=5,
              ignorable=IGNORABLE,
              comment="""
              NOTE: arguments for DROP FUNCTION statements are matched only on
              type; names are irrelevant, so no argument names are saved to use
              with this signature.  Additionally, the return type will always be
              <void>, since return types are ignored for DROP FUNCTION.
                      """)
      ])

  gen.AddNode(
      name='ResolvedDropTableFunctionStmt',
      tag_id=175,
      parent='ResolvedStatement',
      comment="""
      This statement: DROP TABLE FUNCTION [IF EXISTS] <name_path>;

      <is_if_exists> silently ignore the "name_path does not exist" error.
      <name_path> is the identifier path of the function to be dropped.
              """,
      fields=[
          Field('is_if_exists', SCALAR_BOOL, tag_id=2),
          Field('name_path', SCALAR_STRING, tag_id=3, vector=True),
      ])

  gen.AddNode(
      name='ResolvedCallStmt',
      tag_id=83,
      parent='ResolvedStatement',
      emit_default_constructor=False,
      comment="""
      This statement: CALL <procedure>;

      <procedure> Procedure to call.
      <signature> Resolved FunctionSignature for this procedure.
      <argument_list> Procedure arguments.
              """,
      fields=[
          Field('procedure', SCALAR_PROCEDURE, tag_id=2),
          Field(
              'signature',
              SCALAR_FUNCTION_SIGNATURE,
              tag_id=3,
              ignorable=IGNORABLE),
          Field('argument_list', 'ResolvedExpr', tag_id=4, vector=True)
      ])

  gen.AddNode(
      name='ResolvedImportStmt',
      tag_id=86,
      parent='ResolvedStatement',
      comment="""
      This statement: IMPORT <import_kind>
                                   [<name_path> [AS|INTO <alias_path>]
                                   |<file_path>]
                             [<option_list>];

      <import_kind> The type of the object, currently supports MODULE and PROTO.
      <name_path>   The identifier path of the object to import, e.g., foo.bar,
                    used in IMPORT MODULE statement.
      <file_path>   The file path of the object to import, e.g., "file.proto",
                    used in IMPORT PROTO statement.
      <alias_path>  The AS alias path for the object.
      <into_alias_path>  The INTO alias path for the object.
      <option_list> Engine-specific directives for the import.

      Either <name_path> or <file_path> will be populated but not both.
            <name_path> will be populated for IMPORT MODULE.
            <file_path> will be populated for IMPORT PROTO.

      At most one of <alias_path> or <into_alias_path> will be populated.
            <alias_path> may be populated for IMPORT MODULE.
            <into_alias_path> may be populated for IMPORT PROTO.

      IMPORT MODULE and IMPORT PROTO both support options.

      See (broken link) for more detail on IMPORT MODULE.
      See (broken link) for more detail on IMPORT PROTO.
              """,
      fields=[
          Field('import_kind', SCALAR_IMPORT_KIND, tag_id=2),
          Field(
              'name_path',
              SCALAR_STRING,
              tag_id=3,
              vector=True,
              ignorable=IGNORABLE_DEFAULT),
          Field(
              'file_path',
              SCALAR_STRING,
              tag_id=4,
              ignorable=IGNORABLE_DEFAULT),
          Field(
              'alias_path',
              SCALAR_STRING,
              tag_id=5,
              vector=True,
              ignorable=IGNORABLE_DEFAULT),
          Field(
              'into_alias_path',
              SCALAR_STRING,
              tag_id=7,
              vector=True,
              ignorable=IGNORABLE_DEFAULT),
          Field(
              'option_list',
              'ResolvedOption',
              ignorable=IGNORABLE_DEFAULT,
              tag_id=6,
              vector=True)
      ],
      extra_defs="""
  std::string GetImportKindString() const;
  static std::string ImportKindToString(ImportKind kind);""")

  gen.AddNode(
      name='ResolvedModuleStmt',
      tag_id=87,
      parent='ResolvedStatement',
      comment="""
      This statement: MODULE <name_path> [<option_list>];

      <name_path> is the identifier path of the module.
      <option_list> Engine-specific directives for the module statement.

      See (broken link) for more detail on MODULEs.
              """,
      fields=[
          Field('name_path', SCALAR_STRING, tag_id=2, vector=True),
          Field(
              'option_list',
              'ResolvedOption',
              ignorable=IGNORABLE_DEFAULT,
              tag_id=3,
              vector=True)
      ])

  gen.AddNode(
      name='ResolvedAggregateHavingModifier',
      tag_id=85,
      parent='ResolvedArgument',
      comment="""
      This represents a HAVING MAX or HAVING MIN modifier in an aggregate
      expression. If an aggregate has arguments (x HAVING {MAX/MIN} y),
      the aggregate will be computed over only the x values in the rows with the
      maximal/minimal values of y.

      <kind> the MAX/MIN kind of this HAVING
      <having_expr> the HAVING expression (y in the above example)
              """,
      fields=[
          Field('kind', SCALAR_HAVING_MODIFIER_KIND, tag_id=2),
          Field('having_expr', 'ResolvedExpr', tag_id=3)
      ],
      extra_defs="""
  std::string GetHavingModifierKindString() const;
  static std::string HavingModifierKindToString(HavingModifierKind kind);""")

  gen.AddNode(
      name='ResolvedCreateMaterializedViewStmt',
      tag_id=119,
      parent='ResolvedCreateViewBase',
      comment="""
      This statement:
        CREATE MATERIALIZED VIEW <name> [(...)] [PARTITION BY expr, ...]
        [CLUSTER BY expr, ...] [OPTIONS (...)]
        {AS SELECT ... | AS REPLICA OF ...}

      <partition_by_list> specifies the partitioning expressions for the
                          materialized view.
      <cluster_by_list> specifies the clustering expressions for the
                        materialized view.
              """,
      fields=[
          Field(
              'partition_by_list',
              'ResolvedExpr',
              tag_id=3,
              vector=True,
              ignorable=IGNORABLE_DEFAULT,
          ),
          Field(
              'cluster_by_list',
              'ResolvedExpr',
              tag_id=4,
              vector=True,
              ignorable=IGNORABLE_DEFAULT,
          ),
          Field(
              'replica_source',
              'ResolvedScan',
              tag_id=5,
              ignorable=IGNORABLE_DEFAULT,
          ),
      ],
  )

  gen.AddNode(
      name='ResolvedCreateApproxViewStmt',
      tag_id=235,
      parent='ResolvedCreateViewBase',
      comment="""
      This statement creates an APPROX VIEW:
        CREATE APPROX VIEW <name> [(...)] [OPTIONS (...)] AS SELECT ...

      An APPROX VIEW provides approximate results from the view query, as
      opposed to a regular view which returns exact results from its view query.
      APPROX VIEW is a generic operator whose behavior is implementation-defined
      based on the OPTIONS and query engines.

      See (broken link) for more details on APPROX VIEW.
              """,
      fields=[],
  )

  gen.AddNode(
      name='ResolvedCreateProcedureStmt',
      tag_id=125,
      parent='ResolvedCreateStatement',
      emit_default_constructor=False,
      comment="""
      This statement creates a user-defined procedure:
        CREATE [OR REPLACE] [TEMP] PROCEDURE [IF NOT EXISTS] <name_path>
        (<arg_list>) [EXTERNAL SECURITY <external_security>]
        [WITH CONNECTION <connection>] [OPTIONS (<option_list>)]
        [BEGIN <procedure_body> END | LANGUAGE <language> [AS <code>]];

        <name_path> is the identifier path of the procedure.
        <argument_name_list> The names of the function arguments.
        <signature> is the FunctionSignature of the created procedure, with all
               options.  This can be used to create a procedure to load into a
               Catalog for future queries.
        <external_security> is the external security mode for the created
               procedure. Values include 'INVOKER', 'DEFINER'.
        <connection> is the identifier path of the connection object.
        <option_list> has engine-specific directives for modifying procedures.
        <procedure_body> is a string literal that contains the SQL procedure
               body. It includes everything from the BEGIN keyword to the END
               keyword, inclusive. This will always be set for SQL procedures
               and unset for external language procedures.

               The resolver will perform some basic validation on the procedure
               body, for example, verifying that DECLARE statements are in the
               proper position, and that variables are not declared more than
               once, but any validation that requires the catalog (including
               generating resolved tree nodes for individual statements) is
               deferred until the procedure is actually called.  This deferral
               makes it possible to define a procedure which references a table
               or routine that does not yet exist, so long as the entity is
               created before the procedure is called.
        <language> is the programming language used by the procedure. This field
               is set to the language name specified in the LANGUAGE clause.
               Exactly one of <procedure_body> and <language> must be set.
        <code> is a string literal that contains the external language procedure
               definition. It is allowed only if <language> is set.
              """,
      fields=[
          Field(
              'argument_name_list',
              SCALAR_STRING,
              ignorable=NOT_IGNORABLE,
              vector=True,
              tag_id=2,
              to_string_method='ToStringCommaSeparated',
          ),
          Field(
              'signature',
              SCALAR_FUNCTION_SIGNATURE,
              tag_id=3,
              ignorable=NOT_IGNORABLE,
              to_string_method='ToStringVerbose',
          ),
          Field(
              'option_list',
              'ResolvedOption',
              ignorable=IGNORABLE_DEFAULT,
              tag_id=4,
              vector=True,
          ),
          Field(
              'procedure_body',
              SCALAR_STRING,
              ignorable=IGNORABLE_DEFAULT,
              tag_id=5,
          ),
          Field(
              'connection',
              'ResolvedConnection',
              tag_id=6,
              ignorable=IGNORABLE_DEFAULT,
          ),
          Field(
              'language', SCALAR_STRING, ignorable=IGNORABLE_DEFAULT, tag_id=7
          ),
          Field('code', SCALAR_STRING, ignorable=IGNORABLE_DEFAULT, tag_id=8),
          Field(
              'external_security',
              SCALAR_SQL_SECURITY,
              ignorable=IGNORABLE_DEFAULT,
              tag_id=9,
          ),
      ],
  )

  gen.AddNode(
      name='ResolvedExecuteImmediateArgument',
      tag_id=143,
      parent='ResolvedArgument',
      comment="""
      An argument for an EXECUTE IMMEDIATE's USING clause.

        <name> an optional name for this expression
        <expression> the expression's value
             """,
      fields=[
          Field('name', SCALAR_STRING, tag_id=2),
          Field('expression', 'ResolvedExpr', tag_id=3),
      ])

  gen.AddNode(
      name='ResolvedExecuteImmediateStmt',
      tag_id=140,
      parent='ResolvedStatement',
      comment="""
      An EXECUTE IMMEDIATE statement
          EXECUTE IMMEDIATE <sql> [<into_clause>] [<using_clause>]

          <sql> a string expression indicating a SQL statement to be dynamically
            executed
          <into_identifier_list> the identifiers whose values should be set.
            Identifiers should not be repeated in the list.
          <using_argument_list> a list of arguments to supply for dynamic SQL.
             The arguments should either be all named or all unnamed, and
             arguments should not be repeated in the list.
              """,
      fields=[
          Field(
              'sql', 'ResolvedExpr', ignorable=NOT_IGNORABLE, tag_id=2),
          Field(
              'into_identifier_list',
              SCALAR_STRING,
              ignorable=NOT_IGNORABLE,
              tag_id=3,
              vector=True,
              to_string_method='ToStringCommaSeparated',
              java_to_string_method='toStringCommaSeparated'),
          Field(
              'using_argument_list',
              'ResolvedExecuteImmediateArgument',
              ignorable=NOT_IGNORABLE,
              tag_id=4,
              vector=True),
      ])

  gen.AddNode(
      name='ResolvedAssignmentStmt',
      tag_id=142,
      parent='ResolvedStatement',
      comment="""
      An assignment of a value to another value.
      """,
      fields=[
          Field(
              'target',
              'ResolvedExpr',
              tag_id=2,
              comment="""
                  Target of the assignment.  Currently, this will be
                  either ResolvedSystemVariable, or a chain of ResolveGetField
                  operations around it.
                """),
          Field(
              'expr',
              'ResolvedExpr',
              tag_id=3,
              comment="""
                  Value to assign into the target.  This will always be
                  the same type as the target.
                """)
      ])

  gen.AddNode(
      name='ResolvedCreateEntityStmt',
      tag_id=154,
      parent='ResolvedCreateStatement',
      comment="""
      (broken link)
      This statement:
      CREATE [OR REPLACE] <entity_type> [IF NOT EXISTS] <path_expression>
      [OPTIONS <option_list>]
      [AS <entity_body_json>];

      At most one of <entity_body_json>, <entity_body_text> can be non-empty.

      <entity_type> engine-specific entity type to be created.
      <entity_body_json> is a JSON literal to be interpreted by engine.
      <entity_body_text> is a text literal to be interpreted by engine.
      <option_list> has engine-specific directives for how to
                    create this entity.
              """,
      fields=[
          Field(
              'entity_type',
              SCALAR_STRING,
              tag_id=2,
              ignorable=NOT_IGNORABLE),
          Field(
              'entity_body_json',
              SCALAR_STRING,
              tag_id=3,
              ignorable=IGNORABLE_DEFAULT),
          Field(
              'entity_body_text',
              SCALAR_STRING,
              tag_id=5,
              ignorable=IGNORABLE_DEFAULT),
          Field(
              'option_list',
              'ResolvedOption',
              tag_id=4,
              vector=True,
              ignorable=IGNORABLE_DEFAULT),
      ])

  gen.AddNode(
      name='ResolvedAlterEntityStmt',
      tag_id=155,
      parent='ResolvedAlterObjectStmt',
      comment="""
      (broken link)
      This statement:
      ALTER <entity_type> [IF EXISTS]  <path_expression>
      <generic_alter_action>, ...

      <entity_type> engine-specific entity type to be altered.
              """,
      fields=[
          Field(
              'entity_type',
              SCALAR_STRING,
              tag_id=2,
              ignorable=NOT_IGNORABLE),
      ])

  gen.AddNode(
      'ResolvedPivotColumn',
      tag_id=166,
      parent='ResolvedArgument',
      comment="""
              Represents a column produced by aggregating a particular pivot
              expression over a subset of the input for which the FOR expression
              matches a particular pivot value. This aggregation is further
              broken up by the enclosing ResolvedPivotScan's groupby columns,
              with each distinct value of the groupby columns producing a
              separate row in the output.

              In any pivot column, 'c',
              'c' is produced by aggregating pivot expression
                <pivot_expr_list[c.pivot_expr_index]>
              over input rows such that
                <for_expr> IS NOT DISTINCT FROM
                <pivot_value_list[c.pivot_value_index]>
              """,
      fields=[
          Field(
              'column',
              SCALAR_RESOLVED_COLUMN,
              tag_id=2,
              ignorable=NOT_IGNORABLE,
              comment="""
                The output column used to represent the result of the pivot.
                """),
          Field(
              'pivot_expr_index',
              SCALAR_INT,
              tag_id=3,
              ignorable=NOT_IGNORABLE,
              comment="""
              Specifies the index of the pivot expression
              within the enclosing ResolvedPivotScan's <pivot_expr_list> used to
              determine the result of the column.
              """),
          Field(
              'pivot_value_index',
              SCALAR_INT,
              tag_id=4,
              ignorable=NOT_IGNORABLE,
              comment="""
              Specifies the index of the pivot value within
              the enclosing ResolvedPivotScan's <pivot_value_list> used to
              determine the subset of input rows the pivot expression should be
              evaluated over.
              """),
      ])

  gen.AddNode(
      name='ResolvedPivotScan',
      tag_id=161,
      parent='ResolvedScan',
      comment="""
      A scan produced by the following SQL fragment:
        <input_scan> PIVOT(... FOR ... IN (...))

      The column list of this scan consists of a subset of columns from
      <group_by_column_list> and <pivot_column_list>.

      Details: (broken link)
      """,
      fields=[
          Field(
              'input_scan',
              'ResolvedScan',
              tag_id=2,
              ignorable=NOT_IGNORABLE,
              comment="""
              Input to the PIVOT clause
              """),
          Field(
              'group_by_list',
              'ResolvedComputedColumn',
              tag_id=3,
              vector=True,
              ignorable=NOT_IGNORABLE,
              comment="""
              The columns from <input_scan> to group by.
              The output will have one row for each distinct combination of
              values for all grouping columns. (There will be one output row if
              this list is empty.)

              Each element is a ResolvedComputedColumn. The expression is always
              a ResolvedColumnRef that references a column from <input_scan>.
              """),
          Field(
              'pivot_expr_list',
              'ResolvedExpr',
              tag_id=4,
              ignorable=NOT_IGNORABLE,
              vector=True,
              comment="""
              Pivot expressions which aggregate over the subset of <input_scan>
              where <for_expr> matches each value in <pivot_value_list>, plus
              all columns in <group_by_list>.
              """),
          Field(
              'for_expr',
              'ResolvedExpr',
              tag_id=5,
              ignorable=NOT_IGNORABLE,
              comment="""
            Expression following the FOR keyword, to be evaluated over each row
            in <input_scan>. This value is compared with each value in
            <pivot_value_list> to determine which columns the aggregation
            results of <pivot_expr_list> should go to.
            """),
          Field(
              'pivot_value_list',
              'ResolvedExpr',
              tag_id=6,
              ignorable=NOT_IGNORABLE,
              vector=True,
              comment="""
              A list of pivot values within the IN list, to be compared against
              the result of <for_expr> for each row in the input table. Each
              pivot value generates a distinct column in the output for each
              pivot expression, representing the result of the corresponding
              pivot expression over the subset of input where <for_expr> matches
              this pivot value.

              All pivot values in this list must have the same type as
              <for_expr> and must be constant.
              """),
          Field(
              'pivot_column_list',
              'ResolvedPivotColumn',
              tag_id=7,
              vector=True,
              ignorable=NOT_IGNORABLE,
              comment="""
              List of columns created to store the output pivot columns.
              Each is computed using one of pivot_expr_list and one of
              pivot_value_list.
              """),
      ])

  gen.AddNode(
      name='ResolvedReturningClause',
      tag_id=170,
      parent='ResolvedArgument',
      comment="""
              Represents the returning clause on a DML statement.
              """,
      fields=[
          Field(
              'output_column_list',
              'ResolvedOutputColumn',
              tag_id=2,
              vector=True,
              comment="""
              Specifies the columns in the returned output row with column
              names. It can reference columns from the target table scan
              <table_scan> from INSERT/DELETE/UPDATE statements. Also this list
              can have columns computed in the <expr_list> or an <action_column>
              as the last column.
                      """),
          Field(
              'action_column',
              'ResolvedColumnHolder',
              tag_id=3,
              ignorable=NOT_IGNORABLE,
              comment="""
              Represents the WITH ACTION column in <output_column_list> as a
              string type column. There are four valid values for this action
              column: "INSERT", "REPLACE", "UPDATE", and "DELETE".
                      """),
          Field(
              'expr_list',
              'ResolvedComputedColumn',
              tag_id=4,
              vector=True,
              ignorable=NOT_IGNORABLE,
              comment="""
              Represents the computed expressions so they can be referenced in
              <output_column_list>. Worth noting, it can't see <action_column>
              and can only access columns from the DML statement target table.
                      """)
      ])

  gen.AddNode(
      'ResolvedUnpivotArg',
      tag_id=171,
      parent='ResolvedArgument',
      comment="""
      A column group in the UNPIVOT IN clause.

      Example:
        'a' in 'UNPIVOT(x FOR z IN (a , b , c))'
        or '(a , b)' in 'UNPIVOT((x , y) FOR z IN ((a , b), (c , d))'
              """,
      fields=[
          Field(
              'column_list',
              'ResolvedColumnRef',
              tag_id=2,
              ignorable=NOT_IGNORABLE,
              vector=True,
              comment="""
              A list of columns referencing an output column of the <input_scan>
              of ResolvedUnpivotScan. The size of this vector is
              the same as <value_column_list>.
                      """),
      ])

  gen.AddNode(
      name='ResolvedUnpivotScan',
      tag_id=172,
      parent='ResolvedScan',
      comment="""
      A scan produced by the following SQL fragment:
      <input_scan> UNPIVOT(<value_column_list>
        FOR <label_column>
        IN (<unpivot_arg_list>))

      size of (<unpivot_arg_list>[i], i.e. column groups inside
      <unpivot_arg_list>)
        = size of (<value_column_list>)
        = Let's say num_value_columns

      size of (<unpivot_arg_list>)
        = size of (<label_list>)
        = Let's say num_args

      Here is how output rows are generated --
      for each input row :
        for arg_index = 0 .. (num_args - 1) :
          output a row with the original columns from <input_scan>

            plus
          arg = <unpivot_arg_list>[arg_index]
          for value_column_index = 0 .. (num_value_columns - 1) :
            output_value_column = <value_column_list>[value_column_index]
            input_arg_column = arg [value_column_index]
            output_value_column = input_arg_column

            plus
          <label_column> = <label_list>[arg_index]


      Hence the total number of rows generated in the output =
        input rows * size of <unpivot_arg_list>

      For all column groups inside <unpivot_arg_list>, datatype of
      columns at the same position in the vector must be equivalent, and
      also equivalent to the datatype of the column at the same position in
      <value_column_list>.
      I.e. in the above pseudocode, datatypes must be equivalent for
      output_value_column and input_arg_column.
      Datatype of <label_column> must be the same as datatype of
      <label_list> and can be string or int64.

      Details: (broken link)
              """,
      fields=[
          Field('input_scan', 'ResolvedScan', tag_id=2),
          Field(
              'value_column_list',
              SCALAR_RESOLVED_COLUMN,
              tag_id=3,
              vector=True,
              comment="""
              This is a list of one or more new columns added by UNPIVOT.
              These new column(s) store the value of input columns that are in
              the UNPIVOT IN clause.
                      """),
          Field(
              'label_column',
              SCALAR_RESOLVED_COLUMN,
              tag_id=4,
              comment="""
              This is a new column added in the output for storing labels for
              input columns groups that are present in the IN clause. Its
              values are taken from <label_list>.
                      """),
          Field(
              'label_list',
              'ResolvedLiteral',
              tag_id=5,
              vector=True,
              comment="""
              String or integer literal for each column group in
              <unpivot_arg_list>.
                      """),
          Field(
              'unpivot_arg_list',
              'ResolvedUnpivotArg',
              tag_id=6,
              vector=True,
              comment="""
              The list of groups of columns in the UNPIVOT IN list. Each group
              contains references to the output columns of <input_scan> of the
              ResolvedUnpivotScan. The values of these columns are stored in the
              new <value_column_list> and the column group labels/names
              in the <label_column>.
                      """),
          Field(
              'projected_input_column_list',
              'ResolvedComputedColumn',
              tag_id=7,
              vector=True,
              ignorable=IGNORABLE,
              comment="""
              The columns from <input_scan> that are not unpivoted in UNPIVOT
              IN clause. Columns in <projected_input_column_list> and
              <unpivot_arg_list> are mutually exclusive and their union is the
              complete set of columns in the unpivot input-source.

              The expression of each ResolvedComputedColumn is a
              ResolvedColumnRef that references a column from <input_scan>.
              """),
          Field(
              'include_nulls',
              SCALAR_BOOL,
              tag_id=8,
              comment="""
              Whether we need to include the rows from output where ALL columns
              from <value_column_list> are null.
                      """),
      ])

  gen.AddNode(
      name='ResolvedCloneDataStmt',
      tag_id=177,
      parent='ResolvedStatement',
      comment="""
      CLONE DATA INTO <table_name> FROM ...

      <target_table> the table to clone data into. Cannot be value table.
      <clone_from> The source table(s) to clone data from.
                   For a single table, the scan is TableScan, with an optional
                       for_system_time_expr;
                   If WHERE clause is present, the Scan is wrapped inside
                       ResolvedFilterScan;
                   When multiple sources are present, they are UNION'ed together
                       in a ResolvedSetOperationScan.

                   Constraints:
                     The target_table must not be the same as any source table,
                     and two sources cannot refer to the same table.
                     All source tables and target table must have equal number
                     of columns, with positionally identical column names and
                     types.
                     Cannot be value table.
              """,
      fields=[
          Field('target_table', 'ResolvedTableScan', tag_id=2),
          Field('clone_from', 'ResolvedScan', tag_id=3)
      ])

  gen.AddNode(
      name='ResolvedTableAndColumnInfo',
      tag_id=179,
      parent='ResolvedArgument',
      comment="""
      Identifies the <table> and <column_index_list> (which can be empty) that
      are targets of the ANALYZE statement.

      <column_index_list> This list identifies the ordinals of columns to be
      analyzed in the <table>'s column list.
              """,
      fields=[
          Field(
              'table',
              SCALAR_TABLE,
              tag_id=2,
              ignorable=IGNORABLE_DEFAULT),
          Field(
              'column_index_list',
              SCALAR_INT,
              tag_id=3,
              ignorable=IGNORABLE_DEFAULT,
              vector=True,
              is_constructor_arg=False,
              to_string_method='ToStringCommaSeparated',
              java_to_string_method='toStringCommaSeparatedForInt'),
      ])

  gen.AddNode(
      name='ResolvedAnalyzeStmt',
      tag_id=180,
      parent='ResolvedStatement',
      comment="""
      This represents the ANALYZE statement:
      ANALYZE [OPTIONS (<option_list>)] [<table_and_column_index_list> [, ...]];

      <option_list> is a list of options for ANALYZE.

      <table_and_column_info_list> identifies a list of tables along with their
      related columns that are the target of ANALYZE.
              """,
      fields=[
          Field(
              'option_list',
              'ResolvedOption',
              tag_id=2,
              vector=True,
              ignorable=IGNORABLE_DEFAULT),
          Field(
              'table_and_column_index_list',
              'ResolvedTableAndColumnInfo',
              tag_id=3,
              vector=True,
              ignorable=IGNORABLE_DEFAULT),
      ])

  gen.AddNode(
      name='ResolvedAuxLoadDataPartitionFilter',
      tag_id=222,
      parent='ResolvedArgument',
      comment="""
      Indicates that the LOAD DATA statement will load to or overwrite the
      selected partitions:
        [OVERWRITE] PARTITIONS (<filter>)
              """,
      fields=[
          Field(
              'filter',
              'ResolvedExpr',
              tag_id=2,
              comment="""
              Expression to find the partitions to load.
              Scan rows from source file, and fail the query if filter
              evaluates some rows to false.
              <filter> is always of type bool.
              When this expression produces NULL, the query should fail.
              """),
          Field(
              'is_overwrite',
              SCALAR_BOOL,
              tag_id=3,
              ignorable=IGNORABLE_DEFAULT,
              comment="""
              Indicates whether the load data will append to or overwrite the
              selected partition.
              """),
      ])

  gen.AddNode(
      name='ResolvedAuxLoadDataStmt',
      tag_id=186,
      parent='ResolvedStatement',
      comment="""
      LOAD DATA {OVERWRITE|INTO} [{TEMP|TEMPORARY} TABLE] <table_name>
      [[OVERWRITE] PARTITIONS(...)] ...
      FROM FILES ...
        This statement loads an external file to a new or existing table.
        See (broken link).

      <insertion_mode> either OVERWRITE or APPEND (INTO) the destination table.
      <is_temp_table> True if the destination table should be a temporary table.
        Otherwise, the destination table should be a persistent table.
      <name_path> the table to load data into.
      <output_column_list> the list of visible columns of the destination table.
        If <column_definition_list> is explicitly specified:
          <output_column_list> =
              <column_definition_list> + <with_partition_columns>
        Or if the table already exists:
          <output_column_list> = <name_path>.columns
        Last, if the table doesn't exist and <column_definition_list> isn't
        explicitly specified:
          <output_column_list> = detected-columns + <with_partition_columns>
      <column_definition_list> If not empty, the explicit columns of the
          destination table. Must be coerciable from the source file's fields.

          When the destination table doesn't already exist, it will be created
          with these columns (plus the additional columns from WITH PARTITION
          COLUMNS subclause); otherwise, the destination table's schema must
          match the explicit columns by both name and type.
      <pseudo_column_list> is a list of pseudo-columns expected to be present on
          the created table (provided by AnalyzerOptions::SetDdlPseudoColumns*).
          These can be referenced in expressions in <partition_by_list> and
          <cluster_by_list>.
      <primary_key> specifies the PRIMARY KEY constraint on the table. It is
          nullptr when no PRIMARY KEY is specified.
          If specified, and the table already exists, the primary_key is
          required to be the same as that of the existing.
      <foreign_key_list> specifies the FOREIGN KEY constraints on the table.
          If specified, and the table already exists, the foreign keys are
          required to be the same as that of the existing.
      <check_constraint_list> specifies the ABSL_CHECK constraints on the table.
          If specified, and the table already exists, the constraints are
          required to be the same as that of the existing.
      <partition_by_list> The list of columns to partition the destination
          table. Similar to <column_definition_list>, it must match the
          destination table's partitioning spec if it already exists.
      <cluster_by_list> The list of columns to cluster the destination
          table. Similar to <column_definition_list>, it must match the
          destination table's partitioning spec if it already exists.
      <partition_filter> specifies the destination partition selections and
          whether load to or overwrite the selected partitions.
      <option_list> the options list describing the destination table.
          If the destination doesn't already exist, it will be created with
          these options; otherwise it must match the existing destination
          table's options.
      <with_partition_columns> The columns decoded from partitioned source
          files. If the destination table doesn't already exist, these columns
          will be implicitly added to the destination table's schema; otherwise
          the destination table must already have these columns
          (matching by both names and types).

          The hive partition columns from the source file do not automatically
          partition the destination table. To apply the partition, the
          <partition_by_list> must be specified.
      <connection> optional connection reference for accessing files.
      <from_files_option_list> the options list describing the source file(s).
              """,
      fields=[
          Field('insertion_mode', SCALAR_INSERTION_MODE, tag_id=2),
          Field('is_temp_table', SCALAR_BOOL, tag_id=17),
          Field(
              'name_path',
              SCALAR_STRING,
              tag_id=3,
              vector=True,
              ignorable=IGNORABLE_DEFAULT),
          Field(
              'partition_filter',
              'ResolvedAuxLoadDataPartitionFilter',
              tag_id=16,
              ignorable=IGNORABLE_DEFAULT),
          Field(
              'output_column_list',
              'ResolvedOutputColumn',
              tag_id=4,
              vector=True,
              ignorable=IGNORABLE),
          Field(
              'column_definition_list',
              'ResolvedColumnDefinition',
              tag_id=5,
              vector=True,
              ignorable=IGNORABLE_DEFAULT),
          Field(
              'pseudo_column_list',
              SCALAR_RESOLVED_COLUMN,
              tag_id=6,
              vector=True,
              ignorable=IGNORABLE),
          Field(
              'primary_key',
              'ResolvedPrimaryKey',
              tag_id=7,
              ignorable=IGNORABLE_DEFAULT),
          Field(
              'foreign_key_list',
              'ResolvedForeignKey',
              tag_id=8,
              vector=True,
              ignorable=IGNORABLE_DEFAULT),
          Field(
              'check_constraint_list',
              'ResolvedCheckConstraint',
              tag_id=9,
              vector=True,
              ignorable=IGNORABLE_DEFAULT),
          Field(
              'partition_by_list',
              'ResolvedExpr',
              tag_id=10,
              vector=True,
              ignorable=IGNORABLE_DEFAULT),
          Field(
              'cluster_by_list',
              'ResolvedExpr',
              tag_id=11,
              vector=True,
              ignorable=IGNORABLE_DEFAULT),
          Field(
              'option_list',
              'ResolvedOption',
              tag_id=12,
              vector=True,
              ignorable=IGNORABLE_DEFAULT),
          Field(
              'with_partition_columns',
              'ResolvedWithPartitionColumns',
              tag_id=13,
              ignorable=IGNORABLE_DEFAULT),
          Field(
              'connection',
              'ResolvedConnection',
              tag_id=14,
              ignorable=IGNORABLE_DEFAULT),
          Field(
              'from_files_option_list',
              'ResolvedOption',
              tag_id=15,
              vector=True,
              ignorable=IGNORABLE_DEFAULT)
      ])

  gen.AddNode(
      name='ResolvedUndropStmt',
      tag_id=227,
      parent='ResolvedStatement',
      comment="""
      This statement:
        UNDROP <schema_object_kind> [IF NOT EXISTS] <name_path>
        FOR SYSTEM_TIME AS OF [<for_system_time_expr>];

      <schema_object_kind> is a string identifier for the entity to be
      undroped. Currently, only 'SCHEMA' object is supported.

      <name_path> is a vector giving the identifier path for the object to
      be undropped.

      <is_if_not_exists> if set, skip the undrop if the resource already
      exists.

      <for_system_time_expr> specifies point in time from which entity is to
      be undropped.
          """,
      fields=[
          Field('schema_object_kind', SCALAR_STRING, tag_id=2),
          Field(
              'is_if_not_exists',
              SCALAR_BOOL,
              tag_id=3,
              ignorable=IGNORABLE_DEFAULT,
          ),
          Field('name_path', SCALAR_STRING, tag_id=4, vector=True),
          Field(
              'for_system_time_expr',
              'ResolvedExpr',
              tag_id=5,
              ignorable=IGNORABLE_DEFAULT,
          ),
      ],
  )

  gen.Generate(
      input_file_paths=input_templates,
      output_file_paths=output_files,
      data_files=data_files)

if __name__ == '__main__':
  flags.mark_flag_as_required('input_templates')
  flags.mark_flag_as_required('output_files')
  app.run(main)
