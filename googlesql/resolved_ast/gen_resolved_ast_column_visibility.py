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

"""Column visibility annotations for gen_resolved_ast.py, and their codegen.

The annotations are documented in the "ResolvedColumn visibility" section of
gen_resolved_ast.py. This module is build-time machinery that checks them and
turns them into C++. To review its behavior, it's easiest to read the
generated code, resolved_ast_column_visibility_checker.{h,cc} (checked in for
review in (broken link)), rather than this file in detail.

Outline
=======

Vocabulary
----------
The annotation sources used in gen_resolved_ast.py (which re-exports them):
INPUT, CREATED, OUTER and UNCHECKED (_ScopeKeyword instances), IF(field,
source, else_=...), and CONTEXT(name). The context names are defined in
gen_resolved_ast.py (COLUMN_SCOPE_CONTEXT_NAMES) and passed in.

Checking the annotations
------------------------
ColumnVisibilityCodegen.Run() visits every node class. For each concrete node
it checks the annotations of all its own and inherited fields, and raises
ColumnVisibilityError for any mistake:
  - Names and dotted paths refer to real fields, and the fields can provide
    columns (_RenderPath, _CanProvide).
  - Each source is allowed where it's used: OUTER only in outer_scope;
    INPUT, CREATED and CONTEXT not in outer_scope; only ColumnRef fields in
    outer_scope; INPUT and CONTEXT not in provided_columns; UNCHECKED alone
    (_RenderSource).
  - Field-level rules: scope= not on scan fields, scope_sequential only on
    non-scan node vectors, no annotations on scalar or created-column fields,
    only scope= on referenced ResolvedColumn fields, known context names
    (_GenerateField).
  - Node-level rules: INPUT only on scans with a pipe input scan,
    column_list_scope only on scans without column_list_is_created_columns,
    provided_columns not on scans, inherited_field_scopes names inherited
    fields (_GenerateNode, _GenerateProvidedColumns, _CheckOverrides).
  - No cycles between fields' scopes, except a field naming itself
    (_CheckCycles).
Most checks happen while rendering the C++, so every annotation that's used
is checked.

Generating C++
--------------
The generated ColumnVisibilityChecker visits the tree with an Env for each
node: the VISIBLE set, the OUTER set (columns for correlated references), and
one column set per named context. An Env holds plain pointers to column sets
(null is the empty set), and is only copied for a field that changes it. A
set built for a field is owned by the generated Check method's `local_sets`,
or by the checker when it records environments for comparison mode.

For each concrete node, the generated Check<NodeName>(node, env):
  1. Computes INPUT: the pipe input scan's column_list for a scan, otherwise
     env.visible.
  2. For each field, builds the field's Env from its annotations (scope ->
     VISIBLE, outer_scope -> OUTER, context -> named contexts; scan fields get
     an empty VISIBLE set), or reuses the node's Env if the field doesn't
     change it, then recurses into child nodes, or checks
     referenced ResolvedColumns against VISIBLE. A ResolvedColumnRef is checked
     against OUTER if is_correlated, otherwise VISIBLE.
  3. For a scan whose column_list isn't created, checks column_list against
     column_list_scope (default [INPUT, CREATED]).
A scope list becomes C++ that adds each source's columns to a
ColumnSetBuilder (_RenderAssign and _RenderSource). What a node-typed field
provides comes from the generated AddProvidedColumns(), which uses the node
class's provided_columns annotation (a generated ProvidedColumnsOf<NodeName>)
or else GetColumnsCreated().

Run() stores the C++ bodies in each node dict, as
node['visibility_check_body'] and node['provided_columns_body']. The templates
resolved_ast_column_visibility_checker.{h,cc}.template add the fixed parts
(Env, ColumnSetBuilder, error messages, dispatch on node kind).

Comparison mode
---------------
With ColumnVisibilityChecker::Options::record_environments, the checker records
each node's Env and computed column_list_scope. In comparison mode
(ValidatorOptions::compare_column_visibility), the Validator runs the checker
first, then compares those recorded sets with its own hand-written
visible_columns, visible_parameters and CheckColumnList sets
(Validator::CompareColumnVisibility in validator.cc).

Terminology used in this file:

  node dict
    The dict that TreeGenerator.AddNode creates for a node class, e.g.
    node['name'], node['fields'], node['inherited_fields'].

  field dict
    The dict that Field() creates, e.g. field['name'], field['ctype'],
    field['scope'].

  scope list
    A list of scope sources, e.g. [INPUT, 'right_scan'].
"""

from typing import Any

# A node dict or field dict; see the module docstring.
NodeDict = dict[str, Any]
FieldDict = dict[str, Any]


class _ScopeKeyword:
  """One of the keyword scope sources INPUT, CREATED, OUTER or UNCHECKED."""

  def __init__(self, name):
    self.name = name

  def __repr__(self):
    return self.name


INPUT = _ScopeKeyword('INPUT')
CREATED = _ScopeKeyword('CREATED')
OUTER = _ScopeKeyword('OUTER')
UNCHECKED = _ScopeKeyword('UNCHECKED')


class IF:  # pylint: disable=invalid-name
  """Conditional scope source. See "ResolvedColumn visibility"."""

  def __init__(self, field, source, else_ = None):
    assert isinstance(field, str), field
    self.field = field
    self.source = source
    self.else_ = else_

  def __repr__(self):
    if self.else_ is None:
      return 'IF(%r, %r)' % (self.field, self.source)
    return 'IF(%r, %r, else_=%r)' % (self.field, self.source, self.else_)


class CONTEXT:  # pylint: disable=invalid-name
  """Scope source reading a named context."""

  def __init__(self, name):
    # The context name is checked by ColumnVisibilityCodegen.
    assert isinstance(name, str), name
    self.name = name

  def __repr__(self):
    return 'CONTEXT(%r)' % self.name


# The keyword arguments of Field() that hold column visibility annotations.
# These are also the keys allowed in AddNode(inherited_field_scopes=...).
FIELD_ANNOTATION_KEYS = ('scope', 'scope_sequential', 'outer_scope', 'context')


class ColumnVisibilityError(RuntimeError):
  """An invalid column visibility annotation."""


# The kinds of annotation a scope list can be. These control which sources
# are allowed.
_SCOPE = 'scope'
_OUTER_SCOPE = 'outer_scope'
_COLUMN_LIST_SCOPE = 'column_list_scope'
_PROVIDED_COLUMNS = 'provided_columns'


class ColumnVisibilityCodegen:
  """Checks column visibility annotations and generates the checker.

  For each node dict, Run() sets:

    node['visibility_check_body']
      For concrete nodes, a string with the C++ statements for the body of
      ColumnVisibilityChecker::Check<NodeName>. Empty for abstract nodes.

    node['provided_columns_body']
      For concrete nodes with an (own or inherited) `provided_columns`
      annotation, a string with the C++ statements for the body of
      ProvidedColumnsOf<NodeName>. Otherwise empty.

  The generated Check function for a node gets the node and the Env (the
  VISIBLE and OUTER sets and the named contexts) it was entered with. It
  computes the Env for each field from the annotations, recurses into child
  nodes, and checks referenced columns. Scope lists are rendered as C++
  statements that add columns to a ColumnSetBuilder named `b`, and build the
  set into the method's `local_sets`. A scope list that is exactly INPUT or
  OUTER reuses that set, and an empty one is null, so the common cases don't
  build any sets. A field that doesn't change the Env reuses the node's Env.
  """

  def __init__(
      self,
      node_map,
      root_node_name,
      context_names,
  ):
    """Constructor.

    Args:
      node_map: Map from node class name to its node dict, for all nodes.
      root_node_name: The name of the root node class, ResolvedNode, which isn't
        in `node_map`.
      context_names: The names of the named contexts that annotations can use
        (COLUMN_SCOPE_CONTEXT_NAMES in gen_resolved_ast.py).
    """
    self._node_map = node_map
    self._root_node_name = root_node_name
    self._context_names = context_names
    # Map from node class name to whether it (or a subclass) can provide
    # columns. See _NodeCanProvide.
    self._can_provide_cache: dict[str, bool] = {}

  def Run(self, nodes):
    """Checks the annotations on `nodes` and sets their generated code.

    Args:
      nodes: All node dicts, in definition order.

    Raises:
      ColumnVisibilityError: If any annotation is invalid.
    """
    for node in nodes:
      node['provided_columns_body'] = ''
      if node['is_abstract']:
        node['visibility_check_body'] = ''
      else:
        node['visibility_check_body'] = self._GenerateNode(node)
        provided_columns = self._ProvidedColumns(node)
        if provided_columns is not None:
          node['provided_columns_body'] = self._GenerateProvidedColumns(
              node, provided_columns
          )

  def _Error(
      self, node, field, message
  ):
    field_name = field['name'] if field is not None else '<node>'
    return ColumnVisibilityError(
        'Column visibility annotation error on %s.%s: %s'
        % (node['name'], field_name, message)
    )

  # Field classification.

  def _FieldKind(self, field):
    """Returns the kind of a field, for column visibility purposes.

    Args:
      field: A field dict.

    Returns:
      One of 'column' (ResolvedColumn), 'scalar' (any other scalar), 'scan',
      'column_ref', 'holder' (ResolvedColumnHolder) or 'node' (any other
      node type).
    """
    ctype = field['ctype']
    if not isinstance(ctype, str):
      # A ScalarType.
      if ctype.ctype == 'ResolvedColumn':
        return 'column'
      return 'scalar'
    if self._node_map[ctype]['is_scan']:
      return 'scan'
    if ctype == 'ResolvedColumnRef':
      return 'column_ref'
    if ctype == 'ResolvedColumnHolder':
      return 'holder'
    return 'node'

  def _NodeCanProvide(self, name):
    """True if node class `name`, or a subclass, can provide columns.

    A node provides its `provided_columns` if it has that annotation
    (possibly inherited), and otherwise the columns of its own
    `column_is_created` fields.

    Args:
      name: A node class name.

    Returns:
      Whether a field of type `name` can provide any columns.
    """
    if name not in self._can_provide_cache:
      node = self._node_map[name]
      result = (
          any(f['column_is_created'] for f in self._AllFields(node))
          or bool(self._ProvidedColumns(node))
          or any(
              self._NodeCanProvide(sub['name']) for sub in node['subclasses']
          )
      )
      self._can_provide_cache[name] = result
    return self._can_provide_cache[name]

  def _CanProvide(self, node, field):
    """True if `field` (on node class `node`) can provide any columns."""
    kind = self._FieldKind(field)
    if kind == 'scan':
      return True
    if kind == 'column' and field['name'] == 'column_list' and node['is_scan']:
      # A scan's own column_list provides its columns only if it creates them.
      return node['column_list_is_created_columns']
    if kind in ('column', 'holder'):
      return field['column_is_created']
    if kind == 'node':
      return self._NodeCanProvide(field['ctype'])
    return False

  def _AllFields(self, node):
    """Returns the inherited and own field dicts of `node`, in order."""
    return node['inherited_fields'] + node['fields']

  def _Ancestry(self, node):
    """Returns `node` and its ancestor node dicts, nearest first."""
    result = [node]
    while result[-1]['parent'] != self._root_node_name:
      result.append(self._node_map[result[-1]['parent']])
    return result

  def _HasPipeInput(self, node):
    """True if scan node class `node` has a pipe input scan."""
    for n in self._Ancestry(node):
      if n['pipe_input_scan_getter']:
        return True
      if n['name'] != 'ResolvedScan' and (
          'GetPipeInputScan' in n['extra_defs_node_only']
      ):
        return True
    return False

  def _Annotations(self, node, field):
    """Returns the effective annotations for `field` on node class `node`.

    Args:
      node: The node dict of a concrete node class.
      field: One of the fields of `node`.

    Returns:
      A dict with FIELD_ANNOTATION_KEYS as keys. Overrides from
      `inherited_field_scopes` on the nearest node class between `node` and
      the field's defining node are applied.
    """
    annotations = {key: field[key] for key in FIELD_ANNOTATION_KEYS}
    for n in self._Ancestry(node):
      if n['name'] == field['defining_node_name']:
        break
      override = n['inherited_field_scopes'].get(field['name'])
      if override is not None:
        annotations.update(override)
        break
    if annotations['context'] is None:
      annotations['context'] = {}
    return annotations

  def _ColumnListScope(self, node):
    """Returns the effective column_list_scope of `node`.

    Args:
      node: A node dict.

    Returns:
      A tuple of the column_list_scope (None if neither `node` nor an ancestor
      sets it) and whether it's set on `node` itself.
    """
    for n in self._Ancestry(node):
      if n['column_list_scope'] is not None:
        return n['column_list_scope'], n is node
    return None, False

  def _ProvidedColumns(self, node):
    """Returns the effective `provided_columns` of `node`, or None."""
    for n in self._Ancestry(node):
      if n['provided_columns'] is not None:
        return n['provided_columns']
    return None

  def _CheckOverrides(self, node):
    """Checks AddNode(inherited_field_scopes=...) on `node`."""
    inherited_names = {f['name'] for f in node['inherited_fields']}
    for name, override in node['inherited_field_scopes'].items():
      if name not in inherited_names:
        raise ColumnVisibilityError(
            'inherited_field_scopes on %s names %s, which is not an inherited'
            ' field' % (node['name'], name)
        )
      for key in override:
        if key not in FIELD_ANNOTATION_KEYS:
          raise ColumnVisibilityError(
              'inherited_field_scopes on %s.%s has unknown key %s'
              % (node['name'], name, key)
          )

  # Rendering scope lists as C++.

  def _RenderPath(
      self,
      node,
      fields_by_name,
      path,
      self_field,
      kind,
      indent,
  ):
    """Renders C++ adding what a sibling field, or dotted path, provides.

    A dotted path 'a.b.c' goes through node-typed fields `a` and `b` (either
    may be a vector, in which case all elements are used) and gives what the
    final field `c` provides on each of those child nodes. Paths are for
    columns from descendants that their parent nodes don't provide, e.g. the
    scans inside TVF arguments.

    Args:
      node: The node dict whose annotation is being rendered.
      fields_by_name: Map from field name to field dict, for all fields of
        `node`.
      path: The sibling field name or dotted path.
      self_field: The field dict whose annotation is being rendered, or None for
        node-level annotations.
      kind: The kind of annotation being rendered, e.g. _SCOPE.
      indent: Indentation of the generated C++.

    Returns:
      Lines of C++ that add the columns to a ColumnSetBuilder `b`.

    Raises:
      ColumnVisibilityError: If the path is invalid.
    """
    parts = path.split('.')
    is_outer = kind == _OUTER_SCOPE

    def Render(
        owner,
        fields,
        index,
        expr,
        indent,
        depth,
    ):
      p = ' ' * indent
      f = fields.get(parts[index])
      if f is None:
        raise self._Error(
            node,
            self_field,
            '%r names %r, which is not a field' % (path, parts[index]),
        )
      getter = '%s->%s()' % (expr, f['name'])
      if index == len(parts) - 1:
        if is_outer:
          if self._FieldKind(f) != 'column_ref' or index > 0:
            raise self._Error(
                node,
                self_field,
                'outer_scope can only name sibling ResolvedColumnRef fields,'
                ' not %r' % path,
            )
        elif not self._CanProvide(owner, f):
          raise self._Error(
              node, self_field, '%r can never provide any columns' % path
          )
        return self._RenderProvide(getter, f, indent)
      if self._FieldKind(f) in ('column', 'scalar'):
        raise self._Error(
            node,
            self_field,
            'path %r goes through %s, which is not a node field'
            % (path, f['name']),
        )
      child = self._node_map[f['ctype']]
      child_fields = {cf['name']: cf for cf in self._AllFields(child)}
      var = 'path%d' % depth
      if f['is_vector']:
        return (
            [p + 'for (const auto& %s : %s) {' % (var, getter)]
            + Render(child, child_fields, index + 1, var, indent + 2, depth + 1)
            + [p + '}']
        )
      return (
          [p + 'if (const auto* %s = %s; %s != nullptr) {' % (var, getter, var)]
          + Render(child, child_fields, index + 1, var, indent + 2, depth + 1)
          + [p + '}']
      )

    return Render(node, fields_by_name, 0, 'node', indent, 0)

  def _RenderProvideOne(self, kind, expr, indent):
    """Renders C++ adding what one child `expr` of field kind `kind` provides.

    Args:
      kind: The field kind (see _FieldKind) of the field holding `expr`.
      expr: A C++ expression for a pointer to the child node.
      indent: Indentation of the generated C++.

    Returns:
      Lines of C++.
    """
    p = ' ' * indent
    if kind == 'scan':
      return [p + 'b.Add(%s->column_list());' % expr]
    if kind in ('holder', 'column_ref'):
      return [p + 'b.Add(%s->column());' % expr]
    assert kind == 'node', kind
    return [p + 'AddProvidedColumns(&*%s, b);' % expr]

  def _RenderProvide(self, getter, f, indent):
    """Renders C++ adding what field `f`, read by C++ `getter`, provides."""
    p = ' ' * indent
    kind = self._FieldKind(f)
    if kind == 'column':
      return [p + 'b.Add(%s);' % getter]
    if f['is_vector']:
      return (
          [p + 'for (const auto& child : %s) {' % getter]
          + self._RenderProvideOne(kind, 'child', indent + 2)
          + [p + '}']
      )
    return (
        [p + 'if (%s != nullptr) {' % getter]
        + self._RenderProvideOne(kind, getter, indent + 2)
        + [p + '}']
    )

  def _IfBranch(self, branch):
    """Returns the sources in an IF() branch, which can be a list or None."""
    if branch is None:
      return []
    if isinstance(branch, list):
      return branch
    return [branch]

  def _RenderIfCondition(
      self,
      node,
      fields_by_name,
      source,
      self_field,
  ):
    """Returns the C++ condition for IF(): the field is truthy in Python."""
    cond_field = fields_by_name.get(source.field)
    if cond_field is None:
      raise self._Error(
          node, self_field, 'IF() names unknown field %r' % source.field
      )
    getter = 'node->%s()' % source.field
    if cond_field['is_vector']:
      return '!%s.empty()' % getter
    if cond_field['is_node_ptr']:
      return '%s != nullptr' % getter
    ctype = cond_field['ctype']
    if not isinstance(ctype, str) and ctype.ctype == 'bool':
      return getter
    if not isinstance(ctype, str) and ctype.ctype == 'std::string':
      return '!%s.empty()' % getter
    raise self._Error(
        node,
        self_field,
        'IF() field %r must be a bool, string, vector or node field'
        % source.field,
    )

  def _RenderSource(
      self,
      node,
      fields_by_name,
      source,
      kind,
      self_field,
      indent,
  ):
    """Renders C++ adding the columns of one scope source to builder `b`.

    Args:
      node: The node dict whose annotation is being rendered.
      fields_by_name: Map from field name to field dict, for all fields of
        `node`.
      source: The scope source, e.g. INPUT or 'field_name'.
      kind: The kind of annotation being rendered, e.g. _SCOPE.
      self_field: The field dict whose annotation is being rendered, or None for
        node-level annotations.
      indent: Indentation of the generated C++.

    Returns:
      Lines of C++.

    Raises:
      ColumnVisibilityError: If `source` isn't allowed here.
    """
    p = ' ' * indent
    if source is UNCHECKED:
      raise self._Error(
          node, self_field, 'UNCHECKED must be the only element of a list'
      )
    if kind == _PROVIDED_COLUMNS and (
        source is INPUT or source is OUTER or isinstance(source, CONTEXT)
    ):
      raise self._Error(
          node, self_field, '%r is not allowed in provided_columns' % source
      )
    if source is INPUT or source is CREATED or isinstance(source, CONTEXT):
      if kind == _OUTER_SCOPE:
        raise self._Error(
            node, self_field, '%r is not allowed in outer_scope' % source
        )
      if source is INPUT:
        return [p + 'b.Add(input);']
      if isinstance(source, CONTEXT):
        if source.name not in self._context_names:
          raise self._Error(
              node, self_field, 'unknown context name %r' % source.name
          )
        return [p + 'b.Add(env.%s);' % source.name]
      lines = []
      for f in self._CreatedFields(node, self_field):
        lines.extend(self._RenderProvide('node->%s()' % f['name'], f, indent))
      return lines
    if source is OUTER:
      if kind != _OUTER_SCOPE:
        raise self._Error(
            node, self_field, 'OUTER is only allowed in outer_scope'
        )
      return [p + 'b.Add(env.outer);']
    if isinstance(source, IF):
      cond = self._RenderIfCondition(node, fields_by_name, source, self_field)
      lines = [p + 'if (%s) {' % cond]
      for branch in self._IfBranch(source.source):
        lines += self._RenderSource(
            node, fields_by_name, branch, kind, self_field, indent + 2
        )
      if source.else_ is not None:
        lines.append(p + '} else {')
        for branch in self._IfBranch(source.else_):
          lines += self._RenderSource(
              node, fields_by_name, branch, kind, self_field, indent + 2
          )
      lines.append(p + '}')
      return lines
    if isinstance(source, str):
      # Naming the field itself is allowed, for vector fields whose elements
      # can all see each other (e.g. generated columns in CREATE TABLE). Any
      # longer cycle is rejected in _CheckCycles.
      return self._RenderPath(
          node, fields_by_name, source, self_field, kind, indent
      )
    raise self._Error(node, self_field, 'invalid scope source %r' % source)

  def _CreatedFields(
      self, node, self_field
  ):
    """Returns the field dicts of `node` whose columns make up CREATED.

    Args:
      node: A node dict.
      self_field: A field dict to exclude, or None.

    Returns:
      The non-scan fields (except a scan's `column_list`) that can provide
      columns, excluding `self_field`.
    """
    return [
        f
        for f in self._AllFields(node)
        if f is not self_field
        and self._FieldKind(f) != 'scan'
        and not (f['name'] == 'column_list' and node['is_scan'])
        and self._CanProvide(node, f)
    ]

  def _RenderAssign(
      self,
      node,
      fields_by_name,
      sources,
      kind,
      self_field,
      target,
      indent,
  ):
    """Renders C++ assigning the value of scope list `sources` to `target`."""
    p = ' ' * indent
    if not isinstance(sources, list):
      raise self._Error(node, self_field, '%s must be a list' % kind)
    if sources == [UNCHECKED]:
      return [p + '%s = UncheckedColumnSet();' % target]
    if not sources:
      return [p + '%s = nullptr;' % target]
    if len(sources) == 1 and sources[0] is INPUT and kind != _OUTER_SCOPE:
      return [p + '%s = input;' % target]
    if len(sources) == 1 and sources[0] is OUTER and kind == _OUTER_SCOPE:
      return [p + '%s = env.outer;' % target]
    lines = [p + '{', p + '  ColumnSetBuilder b;']
    for source in sources:
      lines += self._RenderSource(
          node, fields_by_name, source, kind, self_field, indent + 2
      )
    lines += [p + '  %s = b.Build(Storage(local_sets));' % target, p + '}']
    return lines

  # Annotation checks that don't happen while rendering.

  def _MentionsInput(self, sources):
    """True if scope list `sources` uses INPUT, possibly inside an IF()."""
    for source in sources or []:
      if source is INPUT:
        return True
      if isinstance(source, IF) and self._MentionsInput(
          self._IfBranch(source.source) + self._IfBranch(source.else_)
      ):
        return True
    return False

  def _Deps(
      self,
      node,
      sources,
      self_field,
  ):
    """Returns the names of the sibling fields that scope list `sources` uses.

    Args:
      node: A node dict.
      sources: A scope list on `self_field`, or None.
      self_field: The field dict whose scope list it is.

    Returns:
      A set of field names. For a dotted path, the first field.
    """
    deps = set()
    for source in sources or []:
      if source is CREATED:
        deps |= {f['name'] for f in self._CreatedFields(node, self_field)}
      elif isinstance(source, IF):
        deps |= self._Deps(
            node,
            self._IfBranch(source.source) + self._IfBranch(source.else_),
            self_field,
        )
      elif isinstance(source, str):
        deps.add(source.split('.')[0])
    return deps

  def _CheckCycles(self, node, edges):
    """Checks that no field of `node` can see itself through its siblings.

    Args:
      node: A node dict.
      edges: Map from field name to the names of the fields its scopes use.

    Raises:
      ColumnVisibilityError: If there's a cycle, other than a field naming
        itself directly.
    """
    state = {}  # Map from field name to 'visiting' or 'done'.

    def Visit(name, path):
      if state.get(name) == 'done':
        return
      if state.get(name) == 'visiting':
        raise ColumnVisibilityError(
            'Column visibility annotation error on %s: cycle in field scopes:'
            ' %s' % (node['name'], ' -> '.join(path + [name]))
        )
      state[name] = 'visiting'
      for dep in sorted(edges.get(name, ())):
        Visit(dep, path + [name])
      state[name] = 'done'

    for name in sorted(edges):
      # A field naming itself directly is allowed; see _RenderSource.
      edges[name] = {dep for dep in edges[name] if dep != name}
    for name in sorted(edges):
      Visit(name, [])

  # Code generation for one concrete node.

  def _GenerateNode(self, node):
    """Checks annotations and returns the visibility check body for `node`.

    Args:
      node: The node dict of a concrete node class.

    Returns:
      The C++ statements for ColumnVisibilityChecker::Check<NodeName>.

    Raises:
      ColumnVisibilityError: If an annotation on `node` is invalid.
    """
    self._CheckOverrides(node)
    fields = self._AllFields(node)
    fields_by_name = {f['name']: f for f in fields}
    is_scan = node['is_scan']
    has_pipe_input = is_scan and self._HasPipeInput(node)

    lines = []
    if is_scan:
      lines += [
          '  [[maybe_unused]] const ColumnSet* input =',
          '      GetPipeInputColumns(node, Storage(local_sets));',
      ]
    else:
      lines.append('  [[maybe_unused]] const ColumnSet* input = env.visible;')

    edges: dict[str, set[str]] = {}
    for field in fields:
      if is_scan and field['name'] == 'column_list':
        continue  # Checked below.
      annotations = self._Annotations(node, field)
      if is_scan and not has_pipe_input:
        lists = [annotations['scope']] + list(annotations['context'].values())
        if any(self._MentionsInput(l) for l in lists):
          raise self._Error(
              node, field, 'INPUT used on a scan with no pipe input scan'
          )
      edges[field['name']] = self._Deps(node, annotations['scope'], field)
      for sources in annotations['context'].values():
        edges[field['name']] |= self._Deps(node, sources, field)
      field_lines = self._GenerateField(
          node, fields_by_name, field, annotations
      )
      if field_lines:
        lines += ['  // Field `%s`.' % field['name'], '  {']
        lines += field_lines
        lines.append('  }')
    self._CheckCycles(node, edges)

    column_list_scope, is_own = self._ColumnListScope(node)
    if is_scan and node['column_list_is_created_columns']:
      if column_list_scope is not None and is_own:
        raise self._Error(
            node,
            None,
            'column_list_scope is not allowed with'
            ' column_list_is_created_columns',
        )
    elif is_scan:
      if column_list_scope is None:
        column_list_scope = [INPUT, CREATED]
      elif not has_pipe_input and self._MentionsInput(column_list_scope):
        raise self._Error(
            node, None, 'INPUT used on a scan with no pipe input scan'
        )
      lines += [
          '  // Field `column_list`.',
          '  {',
          '    const ColumnSet* scope = nullptr;',
      ]
      lines += self._RenderAssign(
          node,
          fields_by_name,
          column_list_scope,
          _COLUMN_LIST_SCOPE,
          fields_by_name['column_list'],
          'scope',
          4,
      )
      lines += ['    GOOGLESQL_RETURN_IF_ERROR(CheckColumnList(node, scope));', '  }']
    elif column_list_scope is not None:
      raise self._Error(node, None, 'column_list_scope is only for scans')

    lines.append('  return absl::OkStatus();')
    if any('local_sets' in line for line in lines):
      lines = [
          "  // Owns the column sets built for this node's fields, which are",
          "  // only used while checking this node's subtree.",
          '  ColumnSetStorage local_sets;',
      ] + lines
    return '\n'.join(lines)

  def _GenerateProvidedColumns(
      self, node, provided_columns
  ):
    """Returns the body of ProvidedColumnsOf<NodeName> for a concrete node.

    Args:
      node: The node dict of a concrete node class.
      provided_columns: Its effective `provided_columns` scope list.

    Returns:
      C++ statements adding the provided columns to ColumnSetBuilder `b`.

    Raises:
      ColumnVisibilityError: If the annotation is invalid.
    """
    if node['is_scan']:
      raise self._Error(node, None, 'provided_columns is not allowed on scans')
    if not isinstance(provided_columns, list) or UNCHECKED in provided_columns:
      raise self._Error(
          node, None, 'provided_columns must be a list of sources'
      )
    fields_by_name = {f['name']: f for f in self._AllFields(node)}
    lines = []
    for source in provided_columns:
      lines += self._RenderSource(
          node, fields_by_name, source, _PROVIDED_COLUMNS, None, 2
      )
    return '\n'.join(lines)

  def _GenerateField(
      self,
      node,
      fields_by_name,
      field,
      annotations,
  ):
    """Renders C++ statements checking one field of a node.

    Args:
      node: The node dict of a concrete node class.
      fields_by_name: Map from field name to field dict, for all fields of
        `node`.
      field: The field dict to check.
      annotations: The effective annotations of `field`; see _Annotations.

    Returns:
      Lines of C++.

    Raises:
      ColumnVisibilityError: If an annotation on `field` is invalid.
    """
    kind = self._FieldKind(field)
    name = field['name']
    getter = 'node->%s()' % name
    has_annotations = (
        annotations['scope'] is not None
        or annotations['outer_scope'] is not None
        or annotations['scope_sequential']
        or annotations['context']
    )
    is_created = field['column_is_created']

    if kind == 'scalar' or (kind in ('column', 'holder') and is_created):
      if has_annotations:
        raise self._Error(
            node,
            field,
            'column visibility annotations are only allowed on node fields'
            ' and referenced ResolvedColumn fields',
        )
      return []

    if node['name'] == 'ResolvedColumnRef' and name == 'column':
      return ['    GOOGLESQL_RETURN_IF_ERROR(CheckColumnRef(node, env));']

    if annotations['scope_sequential'] and kind not in ('node', 'holder'):
      raise self._Error(
          node, field, 'scope_sequential is only allowed on non-scan nodes'
      )

    if kind == 'column':
      if annotations['outer_scope'] is not None or annotations['context']:
        raise self._Error(
            node,
            field,
            'only scope= is allowed on referenced ResolvedColumn fields',
        )
      lines = ['    const ColumnSet* visible = nullptr;']
      lines += self._RenderAssign(
          node,
          fields_by_name,
          annotations['scope'] or [INPUT],
          _SCOPE,
          field,
          'visible',
          4,
      )
      if field['is_vector']:
        lines += [
            '    for (const ResolvedColumn& column : %s) {' % getter,
            '      GOOGLESQL_RETURN_IF_ERROR(CheckColumn(node, "%s", column, visible));'
            % name,
            '    }',
        ]
      else:
        lines.append(
            '    GOOGLESQL_RETURN_IF_ERROR(CheckColumn(node, "%s", %s, visible));'
            % (name, getter)
        )
      return lines

    # Node-typed fields: compute the field's Env, then recurse. The Env is
    # only copied if the field changes it.
    lines = []
    if kind == 'scan':
      if annotations['scope'] is not None:
        raise self._Error(node, field, 'scope= is not allowed on scan fields')
      lines.append('    field_env.visible = nullptr;')
    elif annotations['scope'] is not None:
      lines += self._RenderAssign(
          node,
          fields_by_name,
          annotations['scope'],
          _SCOPE,
          field,
          'field_env.visible',
          4,
      )
    elif node['is_scan']:
      lines.append('    field_env.visible = input;')
    if annotations['outer_scope'] is not None:
      lines += self._RenderAssign(
          node,
          fields_by_name,
          annotations['outer_scope'],
          _OUTER_SCOPE,
          field,
          'field_env.outer',
          4,
      )
    for context_name, sources in sorted(annotations['context'].items()):
      if context_name not in self._context_names:
        raise self._Error(node, field, 'unknown context name %r' % context_name)
      lines += self._RenderAssign(
          node,
          fields_by_name,
          sources,
          _SCOPE,
          field,
          'field_env.%s' % context_name,
          4,
      )
    if lines or annotations['scope_sequential']:
      lines = ['    Env field_env = env;'] + lines
    else:
      lines = ['    const Env& field_env = env;']

    if not field['is_vector']:
      lines += [
          '    if (%s != nullptr) {' % getter,
          '      GOOGLESQL_RETURN_IF_ERROR(CheckNode(%s, field_env));' % getter,
          '    }',
      ]
    elif not annotations['scope_sequential']:
      lines += [
          '    for (const auto& child : %s) {' % getter,
          '      GOOGLESQL_RETURN_IF_ERROR(CheckNode(child.get(), field_env));',
          '    }',
      ]
    else:
      lines += [
          '    // scope_sequential: each element also sees what earlier',
          '    // elements provide.',
          '    ColumnSetBuilder b;',
          '    b.Add(field_env.visible);',
          '    for (const auto& child : %s) {' % getter,
          '      field_env.visible = b.Build(Storage(local_sets));',
          '      GOOGLESQL_RETURN_IF_ERROR(CheckNode(child.get(), field_env));',
      ]
      lines += self._RenderProvideOne(kind, 'child', 6)
      lines += ['    }']
    return lines
