

<!-- mdlint off(WHITESPACE_LINE_LENGTH) -->

# GQL patterns

Graph Query Language (GQL) supports the following patterns. Patterns can
be used in a `MATCH` statement.

## Pattern list

<table>
  <thead>
    <tr>
      <th>Name</th>
      <th>Summary</th>
    </tr>
  </thead>
  <tbody>

<tr>
  <td><a href="#graph_pattern_definition">Graph pattern</a>
</td>
  <td>A pattern to search for in a graph.</td>
</tr>

<tr>
  <td><a href="#element_pattern_definition">Element pattern</a>
</td>
  <td>
    Represents a node pattern or an edge pattern in a path pattern.
  </td>
</tr>

<tr>
  <td><a href="#graph_subpaths">Subpath pattern</a>
</td>
  <td>Matches a portion of a path.</td>
</tr>

<tr>
  <td><a href="#quantified_paths">Quantified path pattern</a>
</td>
  <td>
    A path pattern with a portion that can repeat within a specified range.
  </td>
</tr>

<tr>
  <td><a href="#label_expression_definition">Label expression</a>
</td>
  <td>
    An expression composed from one or more graph label names in an
    element pattern.
  </td>
</tr>

<tr>
  <td><a href="#search_prefix">Path search prefix</a>
</td>
  <td>
    Restricts path pattern to return all paths, any path, or a shortest path
    from each data partition.
  </td>
</tr>

<tr>
  <td><a href="#path_mode">Path mode</a>
</td>
  <td>
    Includes or excludes paths that have repeating edges.
  </td>
</tr>

  </tbody>
</table>

## Graph pattern 
<a id="graph_pattern_definition"></a>

<pre>
<span class="var">graph_pattern</span>:
  <span class="var">path_pattern_list</span> [ <span class="var">where_clause</span> ]

<span class="var">path_pattern_list</span>:
  <span class="var">top_level_path_pattern</span>[, ...]

<span class="var">top_level_path_pattern</span>:
  [ { <span class="var">path_search_prefix</span> | <span class="var">path_mode</span> } ] <span class="var">path_pattern</span>

<span class="var">path_pattern</span>:
  <span class="var">path_term</span>[ ...]

<span class="var">subpath_pattern</span>:
  ( [ <span class="var">path_mode</span> ] <span class="var">path_pattern</span> [ <span class="var">where_clause</span> ] )

<span class="var">path_term</span>:
  { <span class="var">path_primary</span> | <span class="var">quantified_path_primary</span> }

<span class="var">path_primary</span>:
  {
    <span class="var">element_pattern</span>
    | <span class="var">subpath_pattern</span>
  }

<span class="var">where_clause</span>:
  WHERE <span class="var">bool_expression</span>
</pre>

#### Description

A graph pattern consists of a list path patterns. You can optionally
include a `WHERE` clause. For example:

  ```googlesql {.no-copy}
  (a:Account)-[e:Transfers]->(b:Account)          -- path pattern
  WHERE a != b                                    -- WHERE clause
  ```

#### Definitions

+ `path_pattern_list`: A list of path patterns. For example, the
  following list contains two path patterns:

  ```googlesql {.no-copy}
  (a:Account)-[t:Transfers]->(b:Account),         -- path pattern 1
  (a)<-[o:Owns]-(p:Person)                        -- path pattern 2
  ```
+ `path_search_prefix`: a qualifier for a path pattern to return all paths, any
path, or any shortest path. For more information, see [Path search prefix][search-prefix].

+ `path_mode`: The [path mode][path-mode] for a path pattern. Used to filter out
   paths that have repeating edges.
+ `path_pattern`: A path pattern that matches paths in a property graph.
  For example:

  ```googlesql {.no-copy}
  (a:Account)-[e:Transfers]->(b:Account)
  ```
+ `path_term`: An [element pattern][element-pattern-definition] or a
  [subpath pattern][graph-subpaths] in a path pattern.
+ `subpath_pattern`: A path pattern enclosed in parentheses. To learn
  more, see [Graph subpath pattern][graph-subpaths].
+ `quantified_path_primary`: The quantified path pattern to add to the
  graph query. To learn more, see [Quantified path pattern][quantified-paths].
+ `element_pattern`: A node pattern or an edge pattern. To learn more, see
  [Element pattern definition][element-pattern-definition].
+ `where_clause`: A `WHERE` clause, which filters the matched results. For
  example:

  ```googlesql {.no-copy}
  MATCH (a:Account)->(b:Account)
  WHERE a != b
  ```

  Boolean expressions can be used in a `WHERE` clause, including
  graph-specific [predicates][graph-predicates] and
  [logical operators][graph-operators]. Use the
  [field access operator][field-access-operator] to access graph properties.

#### Examples

Note: The examples in this section reference a property graph called
[`FinGraph`][fin-graph].

[fin-graph]: https://github.com/google/googlesql/blob/master/docs/graph-schema-statements.md#fin_graph

The following query matches all nodes:

```googlesql
GRAPH FinGraph
MATCH (n)
RETURN n.name, n.id

/*-----------+
 | name | id |
 +-----------+
 | NULL | 7  |
 | NULL | 16 |
 | NULL | 20 |
 | Alex | 1  |
 | Dana | 2  |
 | Lee  | 3  |
 +-----------*/
```

The following query matches all directed edges:

```googlesql
GRAPH FinGraph
MATCH ()-[e]->()
RETURN COUNT(e.id) AS results

/*---------+
 | results |
 +---------+
 | 8       |
 +---------*/
```

The following query matches all directed edges in either direction:

```googlesql
GRAPH FinGraph
MATCH ()-[e]-()
RETURN COUNT(e.id) AS results

/*---------+
 | results |
 +---------+
 | 16      |
 +---------*/
```

The following query matches paths matching two path patterns:

```googlesql
GRAPH FinGraph
MATCH
  (src:Account)-[t1:Transfers]->(mid:Account)-[t2:Transfers]->(dst:Account),
  (mid)<-[:Owns]-(p:Person)
RETURN
  p.name, src.id AS src_account_id, mid.id AS mid_account_id,
  dst.id AS dst_account_id

/*---------------------------------------------------------+
 | name | src_account_id | mid_account_id | dst_account_id |
 +---------------------------------------------------------+
 | Alex | 20             | 7              | 16             |
 | Alex | 20             | 7              | 16             |
 | Dana | 16             | 20             | 7              |
 | Dana | 16             | 20             | 16             |
 | Lee  | 7              | 16             | 20             |
 | Lee  | 7              | 16             | 20             |
 | Lee  | 20             | 16             | 20             |
 +---------------------------------------------------------*/
```

[graph-subpaths]: #graph_subpaths

[element-pattern-definition]: #element_pattern_definition

[graph-predicates]: https://github.com/google/googlesql/blob/master/docs/operators.md#graph_predicates

[graph-operators]: https://github.com/google/googlesql/blob/master/docs/operators.md#graph_logical_operators

[search-prefix]: #search_prefix

[path-mode]: #path_mode

[gql-match]: https://github.com/google/googlesql/blob/master/docs/graph-query-statements.md#gql_match

[horizontal-aggregation]: https://github.com/google/googlesql/blob/master/docs/graph-gql-functions.md

[quantified-paths]: #quantified_paths

## Element pattern 
<a id="element_pattern_definition"></a>

Note: Syntax characters enclosed in double quotes (`""`) are literal and
required.

<pre>
<span class="var">element_pattern</span>:
  {
    <span class="var">node_pattern</span> |
    <span class="var">edge_pattern</span>
  }

<span class="var">node_pattern</span>:
  (<span class="var">pattern_filler</span>)

<span class="var">edge_pattern</span>:
  {
    <span class="var">full_edge_any</span> |
    <span class="var">full_edge_left</span> |
    <span class="var">full_edge_right</span> |
    <span class="var">abbreviated_edge_any</span> |
    <span class="var">abbreviated_edge_left</span> |
    <span class="var">abbreviated_edge_right</span>
  }

<span class="var">full_edge_any</span>:
  "-[" <span class="var">pattern_filler</span> "]-"

<span class="var">full_edge_left</span>:
  "<-[" <span class="var">pattern_filler</span> "]-"

<span class="var">full_edge_right</span>:
  "-[" <span class="var">pattern_filler</span> "]->"

<span class="var">abbreviated_edge_any</span>:
  -

<span class="var">abbreviated_edge_left</span>:
  <-

<span class="var">abbreviated_edge_right</span>:
  ->

<span class="var">pattern_filler</span>:
  [ <span class="var">graph_pattern_variable</span> ]
  [ <span class="var">is_label_condition</span> ]
  [ { <span class="var">where_clause</span> | <span class="var">property_filters</span> } ]
  [ <span class="var">cost_expression</span> ]

<span class="var">is_label_condition</span>:
  { IS | : } <span class="var">label_expression</span>

<span class="var">cost_expression</span>:
  COST <span class="var">expression</span>

<span class="var">where_clause</span>:
  WHERE <span class="var">bool_expression</span>

<span class="var">property_filters</span>:
  "{" <span class="var">element_property</span>[, ...] "}"

<span class="var">element_property</span>:
  <span class="var">element_property_name</span> : <span class="var">element_property_value</span>
</pre>

#### Description

An element pattern is either a node pattern or an edge pattern.

#### Definitions

+ `node_pattern`: a pattern to match nodes in a property graph. For example:

  ```googlesql {.no-copy}
  (n:Person)          -- Matches all Person nodes in a property graph.
  ```

  ```googlesql {.no-copy}
  (c:City)            -- Matches all City nodes in a property graph.
  ```

  ```googlesql {.no-copy}
  ()                  -- Matches all nodes in a property graph.
  ```
+ `edge_pattern`: a pattern to match edges in a property graph. For example:

  ```googlesql {.no-copy}
  -[LivesIn]->        -- Matches all LivesIn edges in a property graph.
  ```

  ```googlesql {.no-copy}
  -[]->               -- Matches all right directed edges in a property graph.
  ```

  ```googlesql {.no-copy}
  (n:Person)-(c:City) -- Matches edges between Person and City nodes in any direction.
  ```

  There are several types of edge patterns:

  + `full_edge_any`: Any-direction edge with an optional pattern filler.
  + `abbreviated_edge_any`: Any-direction edge, no pattern filler.

  ```googlesql {.no-copy}
-[e:Located_In]-     -- Any-direction full edge with filler.
-[]-                 -- Any-direction full edge, no filler.
-                    -- Any-direction abbreviated edge.
  ```

  + `full_edge_left`: Left-direction edge with an optional pattern filler.
  + `abbreviated_edge_left`: Left-direction edge, no pattern filler.

  ```googlesql {.no-copy}
  <-[e:Located_In]-  -- Left full edge with filler.
  <-[]-              -- Left full edge, no filler.
  <-                 -- Left abbreviated edge.
  ```

  + `full_edge_right`: Right-direction edge with an optional pattern filler.
  + `abbreviated_edge_right`: Right-direction edge, no pattern filler.

  ```googlesql {.no-copy}
  -[e:Located_In]->  -- Right full edge with filler.
  -[]->              -- Right full edge, no filler.
  ->                 -- Right abbreviated edge.
  ```
+ `pattern_filler`: A pattern filler represents specifications on the node or
  edge pattern that you want to match. A pattern filler can optionally contain
  `graph_pattern_variable`, `is_label_condition`,
  
  `where_clause` or `property_filters`, and a cost expression
  . For example:

  ```googlesql {.no-copy}
  (p:Person WHERE p.name = 'Kai')
  ```

<a id="graph_pattern_variables"></a>

+ `graph_pattern_variable`: A variable for the pattern filler.
  You can use a graph pattern variable to reference the element
  it's bound to in a linear graph query.

  `p` is the variable for the graph pattern element `p:Person` in the
  following example:

  ```googlesql {.no-copy}
  (p:Person)-[:Located_In]->(c:City),
  (p)-[:Knows]->(p:Person WHERE p.name = 'Kai')
  ```
+ `is_label_condition`: A `label expression` that the matched nodes and edges
  must satisfy. This condition includes `label expression`. You can use
  either `IS` or `:` to begin a condition. For example, these are the same:

  ```googlesql {.no-copy}
  (p IS Person)
  ```

  ```googlesql {.no-copy}
  (p:Person)
  ```

  ```googlesql {.no-copy}
  -[IS Knows]->
  ```

  ```googlesql {.no-copy}
  -[:Knows]->
  ```
+ `label_expression`: The expression for the label. For more information,
  see [Label expression definition][label-expression-definition].
+ `where_clause`: A `WHERE` clause, which filters the nodes or edges that were
  matched.

  Boolean expressions are supported, including graph-specific [predicates][graph-predicates]
  and [logical operators][graph-operators].

  The `WHERE` clause can't reference properties when the graph pattern variable
  is absent.

  Use the [field access operator][field-access-operator] to access
  graph properties.

  Examples:

  ```googlesql {.no-copy}
  (m:MusicCreator WHERE m.name = 'Cruz Richards')
  ```

  ```googlesql {.no-copy}
  (s:Singer)->(album:Album)<-(s2)
  WHERE s.name != s2.name
  ```

  ```googlesql {.no-copy}
  (s:Singer)-[has_friend:Knows]->
  (s2:Singer WHERE s2.singer_name = 'Mahan Lomond')
  ```
+ `cost_expression`: An optional expression for an edge pattern. This expression
  is used to calculate the total compute cost of a path when used with the
  `ANY CHEAPEST` or `CHEAPEST k` path search
  prefixes. The expression must evaluate to a finite positive number. `COST`
  can be applied only to edge patterns. For more information, see [Path search
  prefix][search-prefix].

  **Example**

  ```googlesql
  GRAPH FinGraph
  MATCH ANY CHEAPEST (a)-[e:Transfer COST e.amount]->{1,3}(b)
  RETURN a.id, b.id
  ```

<a id="property_filters"></a>

+ `property_filters`: Filters the nodes or edges that were matched. It contains
  a key value map of element properties and their values. Property filters can
  appear in both node and edge patterns.

  Examples:

  ```googlesql {.no-copy}
  {name: 'Cruz Richards'}
  ```

  ```googlesql {.no-copy}
  {last_name: 'Richards', albums: 2}
  ```
+ `element_property`: An element property in `property_filters`. The same
  element property can be included more than once in the same
  property filter list. Element properties can be included in any order in a
  property filter list.

  + `element_property_name`: An identifier that represents the name of the
    element property. The property that is identified must be defined in the
    graph element.

  + `element_property_value`: A scalar expression that represents the value for
    the element property. It must be equal to the property value for the filter
    to match. This value can be a `NULL` literal, but the `NULL` literal is
    interpreted as `= NULL`, not `IS NULL` when the element property filter is
    applied.

  Examples:

  ```googlesql {.no-copy}
  (n:Person {age: 20})
  ```

  ```googlesql {.no-copy}
  (n:Person {id: n.age})
  ```

  ```googlesql {.no-copy}
  (n1:Person)-[e: Owns {since: 2023}]->(n2:Account)
  ```

  ```googlesql {.no-copy}
  (:Person {id: 100, age: 20})-[e:Knows]->(n2:Person)
  ```

  ```googlesql {.no-copy}
  (n:Person|Student {id: n.age + n.student_id})
  ```

  ```googlesql {.no-copy}
  (n:Person {age: 20, id: 30})
  ```

  ```googlesql {.no-copy}
  (n {id: 100, age: 20})
  ```

  ```googlesql {.no-copy}
  (n:Person {id: 10 + n.age})-[e:Knows {since: 2023 + e.id}]->
  ```

  The following are equivalent:

  ```googlesql {.no-copy}
  (n:Person WHERE n.id = 100 AND n.age = 20)
  ```

  ```googlesql {.no-copy}
  (n:Person {id: 100, age: 20})
  ```

  The following are equivalent:

  ```googlesql {.no-copy}
  (a:Employee {employee_id: 10})->(:University)<-(a:Alumni {alumni_id: 20})
  ```

  ```googlesql {.no-copy}
  (a:Employee&Alumni {employee_id: 10, alumni_id: 20})->
  (:University)<-(a:Employee&Alumni {employee_id: 10, alumni_id: 20})
  ```

  Although a `NULL` literal can be used as property value in the
  property filter, the semantics is `= NULL`, not `IS NULL`.
  This distinction is important when you create an element pattern:

  ```googlesql {.no-copy}
  (n:Person {age: NULL})          -- '= NULL'
  (n:Person WHERE n.age = NULL)   -- '= NULL'
  (n:Person WHERE n.age IS NULL)  -- 'IS NULL'
  ```

  The following produce errors:

  ```googlesql {.bad .no-copy}
  -- Error: The property specification for n2 can't reference properties in
  -- e and n1.
  (n1:Person)-[e:Knows]->(n2:Person {id: e.since+n1.age})
  ```

  ```googlesql {.bad .no-copy}
  -- Error: Aggregate expressions aren't allowed.
  (n:Person {id: SUM(n.age)})
  ```

  ```googlesql {.bad .no-copy}
  -- Error: A property called unknown_property doesn't exist for Person.
  (n:Person {unknown_property: 100})
  ```

  ```googlesql {.bad .no-copy}
  -- Error: An element property filter list can't be empty
  (n:Person {})
  ```

#### Details

Nodes and edges matched by `element pattern` are referred to as graph elements.
Graph elements can be used in GQL [predicates][graph-predicates], [functions][graph-functions]
and subqueries within GQL.

Set operations support graph elements that have a common [supertype][supertypes].

#### Examples

Note: The examples in this section reference a property graph called
[`FinGraph`][fin-graph].

[fin-graph]: https://github.com/google/googlesql/blob/master/docs/graph-schema-statements.md#fin_graph

The following query matches all nodes in the graph. `n` is a graph pattern
variable that's bound to the matching nodes:

```googlesql
GRAPH FinGraph
MATCH (n)
RETURN LABELS(n) AS label

/*-----------+
 | label     |
 +-----------+
 | [Account] |
 | [Account] |
 | [Account] |
 | [Person]  |
 | [Person]  |
 | [Person]  |
 +-----------*/
```

The following query matches all edges in the graph.
`e` is a graph pattern variable that's bound to the matching edges:

```googlesql
GRAPH FinGraph
MATCH -[e]->
RETURN e.id

/*----+
 | id |
 +----+
 | 20 |
 | 7  |
 | 7  |
 | 20 |
 | 16 |
 | 1  |
 | 3  |
 | 2  |
 +----*/
```

The following queries matches all nodes with a given label in the graph. `n` is
a graph pattern variable that's bound to the matching nodes:

```googlesql
GRAPH FinGraph
MATCH (n:Person)
RETURN n.name, n.id

/*-----------+
 | name | id |
 +-----------+
 | Alex | 1  |
 | Dana | 2  |
 | Lee  | 3  |
 +-----------*/
```

```googlesql
GRAPH FinGraph
MATCH (n:Person|Account)
RETURN n.id, n.name, n.nick_name

/*----------------------------+
 | id | name | nick_name      |
 +----------------------------+
 | 7  | NULL | Vacation Fund  |
 | 16 | NULL | Vacation Fund  |
 | 20 | NULL | Rainy Day Fund |
 | 1  | Alex | NULL           |
 | 2  | Dana | NULL           |
 | 3  | Lee  | NULL           |
 +----------------------------*/
```

The following query matches all edges in the graph that have the `Owns` label.
`e` is a graph pattern variable that's bound to the matching edges:

```googlesql
GRAPH FinGraph
MATCH -[e:Owns]->
RETURN e.id

/*----+
 | id |
 +----+
 | 1  |
 | 3  |
 | 2  |
 +----*/
```

In the following query, the `WHERE` clause is used to filter out nodes whose
`birthday` property is no greater than `1990-01-10`:

```googlesql
GRAPH FinGraph
MATCH (n:Person WHERE n.birthday > '1990-01-10')
RETURN n.name

/*------+
 | name |
 +------+
 | Alex |
 +------*/
```

In the following query, the `WHERE` clause is used to only include edges whose
 `create_time` property is greater than `2020-01-14` and less than `2020-05-14`:

```googlesql
GRAPH FinGraph
MATCH -[e:Owns WHERE e.create_time > '2020-01-14'
                 AND e.create_time < '2020-05-14']->
RETURN e.id

/*----+
 | id |
 +----+
 | 2  |
 | 3  |
 +----*/
```

In the following query, the
[`PROPERTY_EXISTS` predicate][graph-predicates] is used to only include nodes
that have a `name` property:

```googlesql
GRAPH FinGraph
MATCH (n:Person|Account WHERE PROPERTY_EXISTS(n, name))
RETURN n.id, n.name

/*-----------+
 | id | name |
 +-----------+
 | 1  | Alex |
 | 2  | Dana |
 | 3  | Lee  |
 +-----------*/
```

You can filter graph elements with property filters. The following query
uses a property filter, `{is_blocked: false}`, to only include elements
that have the `is_blocked` property set as `false`:

```googlesql
GRAPH FinGraph
MATCH (a:Account {is_blocked: false})
RETURN a.id

/*----+
 | id |
 +----+
 | 7  |
 | 20 |
 +----*/
```

You can use multiple property element filters to filter results. The following
query uses the property element filter list,
`{is_blocked: false, nick_name: 'Vacation Fund'}`
to only include elements that have the `is_blocked` property set as `false`
and the `nick_name` property set as `Vacation Fund`:

```googlesql
GRAPH FinGraph
MATCH (a:Account {is_blocked: false, nick_name: 'Vacation Fund'})
RETURN a.id

/*----+
 | id |
 +----+
 | 7  |
 +----*/
```

The following query matches right directed `Transfers` edges connecting two
`Account` nodes.

```googlesql
GRAPH FinGraph
MATCH (src:Account)-[transfer:Transfers]->(dst:Account)
RETURN src.id AS src_id, transfer.amount, dst.id AS dst_id

/*--------------------------+
 | src_id | amount | dst_id |
 +--------------------------+
 | 7      | 300    | 16     |
 | 7      | 100    | 16     |
 | 16     | 300    | 20     |
 | 20     | 500    | 7      |
 | 20     | 200    | 16     |
 +--------------------------*/
```

The following query matches any direction `Transfers` edges connecting two
`Account` nodes.

```googlesql
GRAPH FinGraph
MATCH (src:Account)-[transfer:Transfers]-(dst:Account)
RETURN src.id AS src_id, transfer.amount, dst.id AS dst_id

/*--------------------------+
 | src_id | amount | dst_id |
 +--------------------------+
 | 16     | 300    | 7      |
 | 16     | 100    | 7      |
 | 20     | 300    | 7      |
 | 7      | 500    | 16     |
 | 7      | 200    | 16     |
 | 20     | 300    | 16     |
 | 20     | 100    | 16     |
 | 16     | 300    | 20     |
 | 7      | 500    | 20     |
 | 16     | 200    | 20     |
 +--------------------------*/
```

The following query matches left directed edges connecting `Person` nodes to
`Account` nodes, using the left directed abbreviated edge pattern.

```googlesql
GRAPH FinGraph
MATCH (account:Account)<-(person:Person)
RETURN account.id, person.name

/*------------+
 | id  | name |
 +------------+
 | 7   | Alex |
 | 20  | Dana |
 | 16  | Lee  |
 +------------*/
```

You can reuse variable names in patterns. The same variable name binds to the
same node or edge. The following query reuses a variable called `a`:

```googlesql
GRAPH FinGraph
MATCH (a:Account)-[t1:Transfers]->(mid:Account)-[t2:Transfers]->(a:Account)
RETURN a.id AS a_id

/*------+
 | a_id |
 +------+
 | 16   |
 | 20   |
 +------*/
```

In the following query, `a` and `a2` are different variable names but can match
the same node:

```googlesql
GRAPH FinGraph
MATCH (a:Account)-[t1:Transfers]->(mid:Account)-[t2:Transfers]->(a2)
RETURN a.id AS a_id, a2.id AS a2_id

/*--------------+
 | a_id | a2_id |
 +--------------+
 | 20   | 16    |
 | 20   | 16    |
 | 7    | 20    |
 | 7    | 20    |
 | 20   | 20    |
 | 16   | 7     |
 | 16   | 16    |
 +--------------*/
```

You need to explicitly apply the `WHERE` filter if you only want to match a path
if `a` and `a2` are different. For example:

```googlesql
GRAPH FinGraph
MATCH (a:Account)-[t1:Transfers]->(mid:Account)-[t2:Transfers]->(a2)
WHERE a.id != a2.id
RETURN a.id AS a_id, a2.id AS a2_id

/*--------------+
 | a_id | a2_id |
 +--------------+
 | 20   | 16    |
 | 20   | 16    |
 | 7    | 20    |
 | 7    | 20    |
 | 16   | 7     |
 +--------------*/
```

[graph-pattern-variables]: #graph_pattern_variables

[label-expression-definition]: #label_expression_definition

[supertypes]: https://github.com/google/googlesql/blob/master/docs/conversion_rules.md#supertypes

[graph-operators]: https://github.com/google/googlesql/blob/master/docs/operators.md#graph_logical_operators

[graph-predicates]: https://github.com/google/googlesql/blob/master/docs/operators.md#graph_predicates

[graph-functions]: https://github.com/google/googlesql/blob/master/docs/graph-gql-functions.md

[field-access-operator]: https://github.com/google/googlesql/blob/master/docs/operators.md#field_access_operator

[to-json-func]: https://github.com/google/googlesql/blob/master/docs/json_functions.md#to_json

[search-prefix]: #search_prefix

## Subpath pattern 
<a id="graph_subpaths"></a>

A subpath pattern matches a portion of a path. You can create a subpath pattern
by enclosing a portion of a path pattern within parentheses. A subpath pattern
can contain inner subpath patterns.

#### Rules

+   A subpath pattern can be combined with node patterns, edge patterns, or
    other subpaths on either end.
+   The portion of a path pattern enclosed within a subpath pattern must adhere
    to the same rules as a standard path pattern
+   A subpath pattern can contain subpath patterns. This results in
    outer subpath patterns and inner subpath patterns.
+   Inner subpath patterns are resolved first, followed by
    outer subpath patterns, and then the rest of the path pattern.
+   If a variable is declared outside of a subpath pattern, it
    can't be referenced inside the subpath pattern.
+   If a variable is declared inside of a subpath pattern, it can
    be referenced outside of the subpath pattern.

#### Details

When you execute a query, an empty node pattern is added to the beginning
and ending inside a subpath if the beginning and ending don't already have
node patterns. For example:

<table>
  <thead>
    <tr>
      <th>Before</th>
      <th>After</th>
    </tr>
  </thead>
  <tbody>
    <tr>
      <td>(node edge node)</td>
      <td>(node edge node)</td>
    </tr>
    <tr>
      <td>(edge node)</td>
      <td>(empty_node edge node)</td>
    </tr>
    <tr>
      <td>(node edge)</td>
      <td>(node edge empty_node)</td>
    </tr>
    <tr>
      <td>(edge)</td>
      <td>(empty_node edge empty_node)</td>
    </tr>
  </tbody>
</table>

If this results in two node patterns that are
next to each other or a node pattern is next to a subpath, a `SAME` operation
is performed on to the consecutive node patterns.

The following are examples of subpath patterns:

  ```googlesql {.no-copy}
  -- Success: e and p are both declared within the same subpath pattern and
  -- can be referenced in that subpath pattern.
  (-[e:LocatedIn]->(p:Person)->(c:City) WHERE p.id = e.id)
  ```

  ```googlesql {.no-copy}
  -- Success: e and p are both declared within the same subpath pattern
  -- hierarchy and can be referenced inside of that subpath pattern hierarchy.
  (-[e:LocatedIn]->((p:Person)->(c:City)) WHERE p.id = e.id)
  ```

  ```googlesql {.no-copy}
  -- Error: e is declared outside of the inner subpath pattern and therefore
  -- can't be referenced inside of the inner subpath pattern.
  (-[e:LocatedIn]->((p:Person)->(c:City) WHERE p.id = e.id))
  ```

  ```googlesql {.no-copy}
  -- Success: e and p are declared in a subpath pattern and can be used outside
  -- of the subpath pattern.
  (-[e:LocatedIn]->(p:Person))->(c:City) WHERE p.id = e.id
  ```

  ```googlesql {.no-copy}
  -- No subpath patterns:
  (p:Person)-[e:LocatedIn]->(c:City)-[s:StudyAt]->(u:School)
  ```

  ```googlesql {.no-copy}
  -- One subpath pattern on the left:
  ((p:Person)-[e:LocatedIn]->(c:City))-[s:StudyAt]->(u:School)
  ```

  ```googlesql {.no-copy}
  -- One subpath pattern on the right:
  (p:Person)-[e:LocatedIn]->((c:City)-[s:StudyAt]->(u:School))
  ```

  ```googlesql {.no-copy}
  -- One subpath pattern around the entire path pattern:
  ((p:Person)-[e:LocatedIn]->(c:City)-[s:StudyAt]->(u:School))
  ```

  ```googlesql {.no-copy}
  -- One subpath pattern that contains only a node pattern:
  ((p:Person))-[e:LocatedIn]->(c:City)-[s:StudyAt]->(u:School)
  ```

  ```googlesql {.no-copy}
  -- One subpath pattern that contains only an edge pattern:
  (p:Person)(-[e:LocatedIn]->)(c:City)-[s:StudyAt]->(u:School)
  ```

  ```googlesql {.no-copy}
  -- Two subpath patterns, one inside the other:
  ((p:Person)(-[e:LocatedIn]->(c:City)))-[s:StudyAt]->(u:School)
  ```

  ```googlesql {.no-copy}
  -- Three consecutive subpath patterns:
  ((p:Person))(-[e:LocatedIn]->(c:City))(-[s:StudyAt]->(u:School))
  ```

#### Examples

Note: The examples in this section reference a property graph called
[`FinGraph`][fin-graph].

[fin-graph]: https://github.com/google/googlesql/blob/master/docs/graph-schema-statements.md#fin_graph

In the following query, the subpath
`(src:Account)-[t1:Transfers]->(mid:Account)` is evaluated first, then the rest
of the path pattern:

```googlesql
GRAPH FinGraph
MATCH
  ((src:Account)-[t1:Transfers]->(mid:Account))-[t2:Transfers]->(dst:Account)
RETURN
  src.id AS src_account_id, mid.id AS mid_account_id, dst.id AS dst_account_id

/*--------------------------------------------------+
 | src_account_id | mid_account_id | dst_account_id |
 +--------------------------------------------------+
 | 20             | 7              | 16             |
 | 20             | 7              | 16             |
 | 7              | 16             | 20             |
 | 7              | 16             | 20             |
 | 20             | 16             | 20             |
 | 16             | 20             | 7              |
 | 16             | 20             | 16             |
 +--------------------------------------------------*/
```

## Quantified path pattern 
<a id="quantified_paths"></a>

Note: Syntax characters enclosed in double quotes (`""`) are literal and
required.

<pre>
<span class="var">quantified_path_primary</span>:
  <span class="var">path_primary</span>
  { <span class="var">fixed_quantifier</span> | <span class="var">bounded_quantifier</span> }

<span class="var">fixed_quantifier</span>:
  "{" <span class="var">bound</span> "}"

<span class="var">bounded_quantifier</span>:
  "{" [ <span class="var">lower_bound</span> ], <span class="var">upper_bound</span> "}"
</pre>

#### Description

A quantified path pattern is a path pattern with a portion that can repeat
within a specified range. You can specify the range, using a quantifier. A
quantified path pattern is commonly used to match variable-length paths.

#### Definitions

+ `quantified_path_primary`: The quantified path pattern to add to the graph
query.

+ `path_primary`: The portion of a path pattern to be quantified.
+ `fixed_quantifier`: The exact number of times the path pattern portion must
   repeat.

  + `bound`: A positive integer that represents the exact number of repetitions.
+ `bounded_quantifier`: The minimum and maximum number of times the path pattern
  portion can repeat.

  + `lower_bound`: A non-negative integer that represents the minimum number of
  times that the path pattern portion must repeat. If a lower bound isn't
  provided, 0 is used by default.

  + `upper_bound`: A positive integer that represents the maximum number of
    times that the path pattern portion can repeat. This number must be
    specified and be equal to or greater than `lower_bound`.

#### Details

+   A path must have a _minimum node count_ greater than 0. The minimum node
    count of a quantified portion within the path is calculated as:

    ```none
    min_node_count = lower_quantifier * node_count_of_quantified_path
    ```

    When `bound` or `lower_bound` of the quantified path pattern portion is 0,
    the path must contain other parts with _minimum node count_ greater than 0.
+   A quantified path must have a _minimum path length_ greater than 0. The
    minimum path length of a quantified path is calculated as:

    ```none
    min_path_length = lower_quantifier * length_of_quantified_path
    ```

    The path length of a node is 0. The path length of an edge is 1.
+   A quantified path pattern with `bounded_quantifier` matches paths of any
    length between the lower and the upper bound. This is equivalent to unioning
    match results from multiple quantified path patterns with `fixed_quantifier`,
    one for each number between the lower bound and upper bound.
+   Quantification is allowed on an edge or subpath. When quantifying an edge,
    the edge pattern is canonicalized into a subpath.

    ```none
    -[]->{1, 5}
    ```

    is canonicalized into

    ```none
    (()-[]->()){1, 5}
    ```
+   Multiple quantifications are allowed in the same graph pattern, however,
    quantifications may not be nested.
+   Only singleton variables can be multiply-declared. A singleton variable is a
    variable that binds exactly to one node or edge.

    In the following `MATCH` statement, the variables `p`, `knows`, and `f` are
    singleton variables, which bind exactly to one element each.

    ```googlesql {.no-copy}
    MATCH (p)-[knows]->(f)
    ```
+   Variables defined within a quantified path pattern bind to an array of
    elements outside of the quantified path pattern and are called group
    variables.

    In the following `MATCH` statement, the path pattern has the quantifier
    `{1, 3}`. The variables `p`, `knows`, and `f` are each bind to an array of
    elements in the `MATCH` statement result and are considered group variables:

    ```googlesql {.no-copy}
    MATCH ((p)-[knows]->(f)){1, 3}
    ```

    Within the quantified pattern, before the quantifier is applied, `p`, `knows`,
    and `f` each bind to exactly one element and are considered singleton
    variables.

Examples:

```googlesql {.no-copy}
-- Quantified path pattern with a fixed quantifier:
MATCH ((p:Person)-[k:Knows]->(f:Person)){2}

-- Equivalent:
MATCH ((p0:Person)-[k0:Knows]->(f0:Person)(p1:Person)-[k1:Knows]->(f1:Person))
```

```googlesql {.no-copy}
-- Quantified path pattern with a bounded quantifier:
MATCH ((p:Person)-[k:Knows]->(f:Person)){1,3}

-- Equivalent:
MATCH ((p:Person)-[k:Knows]->(f:Person)){1}
UNION ALL
MATCH ((p:Person)-[k:Knows]->(f:Person)){2}
UNION ALL
MATCH ((p:Person)-[k:Knows]->(f:Person)){3}
```

```googlesql {.no-copy}
-- Quantified subpath with default lower bound (0) and an upper bound.
-- When subpath is repeated 0 times, the path pattern is semantically equivalent
-- to (source_person:Person)(dest_person:Person).
MATCH (source_person:Person)((p:Person)-[k:Knows]->(f:Person)){, 4}(dest_person:Person)
```

```googlesql {.no-copy}
-- Edge quantification is canonicalized into subpath quantification:
MATCH (p:Person)-[k:Knows]->{1,2}(f:Person)

-- Equivalent:
MATCH (p:Person)(()-[k:Knows]->()){1,2}(f:Person)
```

```googlesql {.no-copy}
-- ERROR: Minimum path length for the quantified path is 0.
MATCH (p:Person){1, 3}
```

```googlesql {.no-copy}
-- ERROR: Minimum node count and minimum path length for the entire path is 0.
MATCH ((p:Person)-[k:Knows]->(f:Person)){0}
```

```googlesql {.no-copy}
-- ERROR: Minimum path length for the entire path is 0 when quantified portion
-- is repeated 0 times.
MATCH (:Person)((p:Person)-[k:Knows]->(f:Person)){0, 3}
```

```googlesql {.no-copy}
-- ERROR: `p` is declared once as a group variable and once as a singleton
-- variable.
MATCH (s:Person) ((p:Person)-[k:Knows]->(f:Person)){1, 3}->(p:Person)
```

```googlesql {.no-copy}
-- ERROR: `p` is declared twice as a group variable.
MATCH ((p:Person)-[k:Knows]->(f:Person)){1, 3}-[x.Knows]->((p:Person)-[z:Knows]-(d:Person)){2}
```

```googlesql {.no-copy}
-- Since both declarations of `p` are within the quantifier’s pattern,
-- they are treated as singleton variables and can be multiply-declared.
MATCH (s:person)((p:Person)-[k:Knows]->(p:Person)){1, 3}
```

#### Examples

Note: The examples in this section reference a property graph called
[`FinGraph`][fin-graph].

[fin-graph]: https://github.com/google/googlesql/blob/master/docs/graph-schema-statements.md#fin_graph

The following query uses a quantified path pattern to match all of the
destination accounts that are one to three transfers away from a source account
with `id` equal to `7`:

```googlesql
GRAPH FinGraph
MATCH (src:Account {id: 7})-[e:Transfers]->{1, 3}(dst:Account)
WHERE src != dst
RETURN ARRAY_LENGTH(e) AS hops, dst.id AS dst_account_id

/*-----------------------+
 | hops | dst_account_id |
 +-----------------------+
 | 1    | 16             |
 | 3    | 16             |
 | 3    | 16             |
 | 1    | 16             |
 | 2    | 20             |
 | 2    | 20             |
 +-----------------------*/
```

The following query uses a quantified path pattern to match paths between
accounts with one to two transfers through intermediate accounts that are
blocked:

```googlesql
GRAPH FinGraph
MATCH
  (src:Account)
  ((:Account)-[:Transfers]->(mid:Account {is_blocked:true})){1,2}
  -[:Transfers]->(dst:Account)
RETURN src.id AS src_account_id, dst.id AS dst_account_id

/*---------------------------------+
 | src_account_id | dst_account_id |
 +---------------------------------+
 | 7              | 20             |
 | 7              | 20             |
 | 20             | 20             |
 +---------------------------------*/
```

In the following query, `e` is declared in a quantified path pattern. When
referenced outside of that pattern, `e` is a group variable bound to an array
of `Transfers`. You can use the group variable in aggregate functions such
as `SUM` and `ARRAY_LENGTH`:

```googlesql
GRAPH FinGraph
MATCH
  (src:Account {id: 7})-[e:Transfers WHERE e.amount > 100]->{0,2}
  (dst:Account)
WHERE src.id != dst.id
LET total_amount = SUM(e.amount)
RETURN
  src.id AS src_account_id, dst.id AS dst_account_id,
  ARRAY_LENGTH(e) AS number_of_hops, total_amount

/*-----------------------------------------------------------------+
 | src_account_id | dst_account_id | number_of_hops | total_amount |
 +-----------------------------------------------------------------+
 | 7              | 16             | 1              | 300          |
 | 7              | 20             | 2              | 600          |
 +-----------------------------------------------------------------*/
```

[graph-pattern-definition]: #graph_pattern_definition

## Label expression 
<a id="label_expression_definition"></a>

<pre>
<span class="var">label_expression</span>:
  {
    <span class="var">label_name</span>
    | <span class="var">or_expression</span>
    | <span class="var">and_expression</span>
    | <span class="var">not_expression</span>
  }

</pre>

#### Description

A label expression is formed by combining one or more labels with logical
operators (AND, OR, NOT) and parentheses for grouping.

#### Definitions

+ `label_name`: The label to match. Use `%` to match any label in the
  graph. For example:

  ```googlesql {.no-copy}
  (p:Person)
  ```

  ```googlesql {.no-copy}
  (p:%)
  ```
+ `or_expression`: [GQL logical `OR` operation][graph-operators] for
  label expressions. For example:

  ```googlesql {.no-copy}
  (p:(Singer|Writer))
  ```

  ```googlesql {.no-copy}
  (p:(Singer|(Producer|Writer)))
  ```

  ```googlesql {.no-copy}
  (p:(Singer|(Producer&Writer)))
  ```
+ `and_expression`: [GQL logical `AND` operation][graph-operators] for
  label expressions. For example:

  ```googlesql {.no-copy}
  (p:(Singer&Producer))
  ```

  ```googlesql {.no-copy}
  (p:(Singer&(Writer|Producer)))
  ```

  ```googlesql {.no-copy}
  (p:(Singer&(Writer&Producer)))
  ```
+ `not_expression`: [GQL logical `NOT` operation][graph-operators] for
  label expressions. For example:

  ```googlesql {.no-copy}
  (p:!Singer)
  ```

  ```googlesql {.no-copy}
  (p:(!Singer&!Writer))
  ```

  ```googlesql {.no-copy}
  (p:(Singer|(!Writer&!Producer)))
  ```

#### Details

If a label in defined in the schema, it exposes a set of properties
with declared names and data types. When a node or edge carries this
label, the properties exposed by that label are always accessible through
the node or edge.

#### Examples

Note: The examples in this section reference a property graph called
[`FinGraph`][fin-graph].

[fin-graph]: https://github.com/google/googlesql/blob/master/docs/graph-schema-statements.md#fin_graph

The following query matches all nodes with the label `Person`
in the [`FinGraph`][fin-graph] property graph.

```googlesql
GRAPH FinGraph
MATCH (n:Person)
RETURN n.name

/*------+
 | name |
 +------+
 | Alex |
 | Dana |
 | Lee  |
 +------*/
```

The following query matches all nodes that have either a `Person`
or an `Account` label in the [`FinGraph`][fin-graph] property graph.

```googlesql
GRAPH FinGraph
MATCH (n:Person|Account)
RETURN n.id

/*----+
 | id |
 +----+
 | 7  |
 | 16 |
 | 20 |
 | 1  |
 | 2  |
 | 3  |
 +----*/
```

[graph-operators]: https://github.com/google/googlesql/blob/master/docs/operators.md#graph_logical_operators

## Path search prefix 
<a id="search_prefix"></a>

<pre>
<span class="var">path_search_prefix</span>:
  {
    ALL
    | ANY
    | ANY SHORTEST
    | ANY <span class="var">k</span>
    | SHORTEST <span class="var">k</span>
    | ANY CHEAPEST
    | CHEAPEST <span class="var">k</span>
  }
</pre>

#### Description

Restricts path pattern to return all paths, any path, or a shortest path from
each data partition.

#### Definitions

+ `k`: A non-negative integer literal or query parameter. If `k` is a negative
  integer literal, a compile-time error is produced. If `k` is a query
  parameter that's negative at runtime, a runtime error is produced.
+   `ALL` (default) : Returns all paths matching the path pattern. This is the
    default value when no search prefix is specified.
+   `ANY`: Returns any path matching the path pattern from each data partition.
+   `ANY SHORTEST`: Returns a shortest path (the path with the least number of
    edges) matching the path pattern from each data partition. If there are more
    than one shortest paths per partition, returns any one of them.
+   `ANY k`: Returns any `k` paths matching the path pattern from each data
    partition. If more than `k` paths exist per partition, returns any
    `k` of the paths.
+   `SHORTEST k`: Returns `k` shortest paths (the paths with the least number of
    edges) matching the path pattern from each data partition. If any
    ties exist for the `k` shortest paths, returns any `k` of the paths.
+   `ANY CHEAPEST`: Returns one of the cheapest paths matching the path pattern
    from each data partition. A cheapest path is one with the minimum total
    compute cost, calculated from `COST` expressions on edges in the path.
    If multiple cheapest paths exist per partition, returns any one of
    the paths.
+   `CHEAPEST k`: Returns `k` cheapest paths matching the path pattern from
    each data partition. A cheapest path is one with the minimum total compute
    cost, calculated from `COST` expressions on edges in the path. If any ties
    exist for the `k` cheapest paths, returns any `k` of the paths.

#### Details

The path search prefix first partitions the match results by their endpoints
(the first and last nodes) then selects paths from each group.

The path search prefixes other than `ALL` can return multiple paths, one
or more for each distinct pair of endpoints.

When using prefixes other than `ALL` in a path pattern, don't reuse
variables defined within that pattern elsewhere in the same `MATCH` statement,
unless the variable represents an endpoint. Each prefix needs to operate
independently on its associated path pattern.

##### Path cost expression

When you use the `ANY CHEAPEST`
or `CHEAPEST k` parameter, edge patterns in the
path expression can include a cost expression using `COST <expr>`. The total
compute cost of a path is the sum of costs of edges that have `COST`
expressions.

To use `ANY CHEAPEST` or `CHEAPEST k` in a query:

+ At least one `COST` expression must be defined within the path pattern.
+ All quantified edge patterns within the path pattern must include a
  `COST` expression.
+ The `COST` expression must be used only on edge patterns.
+ All variables used by `<expr>` must be local to the edge pattern.
+ `<expr>` must be a numeric type: `INT32`, `INT64`, `UINT32`, `UINT64`,
  `FLOAT`, `DOUBLE`, `NUMERIC`, or `BIGNUMERIC`.
+ `<expr>` must evaluate to a finite positive number. `NULL`, zero, negative
  values, `Inf`, and `NaN` produce a runtime error.

#### Examples

Note: The examples in this section reference a property graph called
[`FinGraph`][fin-graph].

[fin-graph]: https://github.com/google/googlesql/blob/master/docs/graph-schema-statements.md#fin_graph

The following query matches a shortest path between each pair of `[a, b]`.

```googlesql {.no-copy}
GRAPH FinGraph
MATCH ANY SHORTEST (a:Account {is_blocked:true})-[t:Transfers]->{1, 4} (b:Account)
LET path_length = COUNT(t)
RETURN a.id AS a_id, path_length, b.id AS b_id

/*------+-------------+------+
 | a_id | path_length | b_id |
 +------+-------------+------+
 |   16 |           2 |   16 |
 |   16 |           2 |    7 |
 |   16 |           1 |   20 |
 +------+-------------+------*/
```

The following query matches any path between each pair of `[a, b]`.

```googlesql {.no-copy}
GRAPH FinGraph
MATCH ANY (a:Account {is_blocked: true})->(mid:Account)->(b:Account)
RETURN a.id AS a_id, mid.id AS mid_id, b.id AS b_id

/*------+--------+------+
 | a_id | mid_id | b_id |
 +------+--------+------+
 | 16   | 20     | 16   |
 | 16   | 20     | 7    |
 +------+--------+------*/
```

The following query matches all paths between each pair of `[a, b]`. The `ALL`
prefix doesn't filter out any result.

```googlesql {.no-copy}
GRAPH FinGraph
MATCH ALL (a:Account {id: 20})-[t:Transfers]->(b:Account)
RETURN a.id AS a_id, t.amount, b.id AS b_id

-- Equivalent:
GRAPH FinGraph
MATCH (a:Account {id: 20})-[t:Transfers]->(b:Account)
RETURN a.id AS a_id, t.amount, b.id AS b_id

/*------+--------+------+
 | a_id | amount | b_id |
 +------+--------+------+
 | 20   | 500    | 7    |
 | 20   | 200    | 16   |
 +------+--------+------*/
```

The following query finds the middle account of any two-hop loops that starts and
ends with the same account with `id = 20`, and gets the middle account's owner.

```googlesql {.no-copy}
GRAPH FinGraph
MATCH ANY (a:Account {id: 20})->(mid:Account)->(a:Account)
MATCH ALL (p:Person)->(mid)
RETURN p.name, mid.id

/*------+----+
 | name | id |
 +------+----+
 | Lee  | 16 |
 +------+----*/
```

The following query produces an error because `mid`, defined within a path
pattern with the `ANY` prefix, can't be reused outside that pattern in the
same `MATCH` statement. This isn't permitted because `mid` isn't an endpoint.

```googlesql {.bad .no-copy}
-- Error
GRAPH FinGraph
MATCH
  ANY (a:Account {id: 20})->(mid:Account)->(a:Account)->(mid:Account)->(a:Account),
  ALL (p:Person)->(mid)
RETURN p.name
```

The following query succeeds because `a`, even though defined in a path pattern
with the `ANY` path search prefix, can be reused outside of the path pattern
within the same `MATCH` statement, since `a` is an endpoint.

```googlesql
-- Succeeds
GRAPH FinGraph
MATCH
  ANY (a:Account {id: 20})->(mid:Account)->(a:Account)->(mid:Account)->(a:Account),
  ALL (p:Person)->(a)
RETURN p.name

/*------+
 | name |
 +------+
 | Dana |
 +------*/
```

The following query succeeds because `mid` isn't reused outside of the path
pattern with the `ANY` prefix in the same `MATCH` statement.

```googlesql
-- Succeeds
GRAPH FinGraph
MATCH ANY (a:Account {id: 20})->(mid:Account)->(a:Account)->(mid:Account)->(a:Account)
MATCH ALL (p:Person)->(mid)
RETURN p.name

/*------+
 | name |
 +------+
 | Lee  |
 +------*/
```

All rules for [quantified path patterns][quantified-path-pattern] apply. In the
following examples, although `p` is on the boundary of the first path, it's a
group variable and still not allowed be declared again outside its parent
quantified path:

```googlesql {.bad .no-copy}
-- Error
GRAPH FinGraph
MATCH ANY ((p:Person)->(f:Person)){1, 3},
      ALL ->(p)->
RETURN p.name
```

```googlesql {.bad .no-copy}
-- Error
GRAPH FinGraph
MATCH ANY ((p:Person)->(f:Person)){1, 3},
      ALL ((p)->){1, 3}
RETURN p.name
```

The following query matches one of the cheapest paths between each pair of
`[a, b]` based on transfer amount:

```googlesql
GRAPH FinGraph
MATCH ANY CHEAPEST (a:Account)-[t:Transfers COST t.amount]->{1,3}(b:Account)
LET total_cost = sum(t.amount)
RETURN a.id AS a_id, b.id AS b_id, total_cost

/*------+------+------------+
 | a_id | b_id | total_cost |
 +------+------+------------+
 | 7    | 7    | 900        |
 | 7    | 16   | 100        |
 | 7    | 20   | 400        |
 | 16   | 7    | 800        |
 | 16   | 16   | 500        |
 | 16   | 20   | 300        |
 | 20   | 7    | 500        |
 | 20   | 16   | 200        |
 | 20   | 20   | 500        |
 +------+------+------------*/
```

The following query matches up to two cheapest paths between each pair of
`[a, b]` based on transfer amount:

```googlesql
GRAPH FinGraph
MATCH CHEAPEST 2 (a:Account)-[t:Transfers COST t.amount]->{1,3}(b:Account)
LET total_cost = sum(t.amount)
RETURN a.id AS a_id, b.id AS b_id, total_cost

/*------+------+------------+
 | a_id | b_id | total_cost |
 +------+------+------------+
 | 7    | 7    | 900        |
 | 7    | 7    | 1100       |
 | 7    | 16   | 100        |
 | 7    | 16   | 300        |
 | 7    | 20   | 400        |
 | 7    | 20   | 600        |
 | 16   | 7    | 800        |
 | 16   | 16   | 500        |
 | 16   | 16   | 900        |
 | 16   | 20   | 300        |
 | 16   | 20   | 800        |
 | 20   | 7    | 500        |
 | 20   | 7    | 1000       |
 | 20   | 16   | 200        |
 | 20   | 16   | 600        |
 | 20   | 20   | 500        |
 | 20   | 20   | 900        |
 +------+------+------------*/
```

[quantified-path-pattern]: #quantified_paths

## Path mode 
<a id="path_mode"></a>

<pre>
<span class="var">path_mode</span>:
  {
    WALK [ PATH | PATHS ]
    | ACYCLIC [ PATH | PATHS ]
    | TRAIL [ PATH | PATHS ]
  }
</pre>

#### Description

Includes or excludes paths that have repeating edges based on the specified
mode.

#### Definitions

+   `WALK` (default) : Keeps all paths. If the path mode isn't present, `WALK`
    is used by default.
+   `ACYCLIC`: Filters out paths that have repeating nodes.
+   `TRAIL`: Filters out paths that have repeating edges.
+   `PATH` and `PATHS`: Syntactic sugar that has no effect on execution.

#### Details

A path mode is typically added in order to filter out paths with duplicate
edges or nodes. It can be applied to any path or subpath pattern.

A path mode is applied to the whole path or subpath pattern that it restricts,
regardless of whether other modes are used on subpath patterns.

A path mode is applied to path patterns only, not graph patterns.

A path can have either a path mode or a [path search prefix][search-prefix], but
not both.

#### Examples

Note: The examples in this section reference a property graph called
[`FinGraph`][fin-graph].

[fin-graph]: https://github.com/google/googlesql/blob/master/docs/graph-schema-statements.md#fin_graph

The following query demonstrates the use of the `WALK` path mode on a
non-quantified path pattern. The first path in the results uses the same edge
for `t1` and `t3`.

```googlesql {.no-copy}
GRAPH FinGraph
MATCH
  WALK (a1:Account)-[t1:Transfers]->(a2:Account)-[t2:Transfers]->
  (a3:Account)-[t3:Transfers]->(a4:Account)
WHERE a1.id < a4.id
RETURN
  t1.id as transfer1_id, t2.id as transfer2_id, t3.id as transfer3_id

/*--------------+--------------+--------------+
 | transfer1_id | transfer2_id | transfer3_id |
 +--------------+--------------+--------------+
 | 16           | 20           | 16           |
 | 7            | 16           | 20           |
 | 7            | 16           | 20           |
 +--------------+--------------+--------------*/
```

The following queries demonstrate the use of the `ACYCLIC` path mode on a
non-quantified path pattern. Notice that the path whose `a1` and `a3` nodes are
equal has been filtered out.

```googlesql {.no-copy}
GRAPH FinGraph
MATCH
  ACYCLIC (a1:Account)-[t1:Transfers]->(a2:Account)-[t2:Transfers]->
  (a3:Account)
RETURN
  a1.id as account1_id, a2.id as account2_id,
  a3.id as account3_id

/*-------------+-------------+-------------+
 | account1_id | account2_id | account3_id |
 +-------------+-------------+-------------+
 | 20          | 7           | 16          |
 | 20          | 7           | 16          |
 | 7           | 16          | 20          |
 | 7           | 16          | 20          |
 | 16          | 20          | 7           |
 +-------------+-------------+-------------*/
```

The following queries demonstrate the use of the `TRAIL` path mode on a
non-quantified path pattern. Notice that the path whose `t1` and `t3` edges are
equal has been filtered out.

```googlesql {.no-copy}
GRAPH FinGraph
MATCH
  TRAIL (a1:Account)-[t1:Transfers]->(a2:Account)-[t2:Transfers]->
  (a3:Account)-[t3:Transfers]->(a4:Account)
WHERE a1.id < a4.id
RETURN
  t1.id as transfer1_id, t2.id as transfer2_id,
  t3.id as transfer3_id

/*--------------+--------------+--------------+
 | transfer1_id | transfer2_id | transfer3_id |
 +--------------+--------------+--------------+
 | 7            | 16           | 20           |
 | 7            | 16           | 20           |
 +--------------+--------------+-------------*/
```

The following query demonstrates that path modes are applied on path patterns
and not on graph patterns. Notice that, if `TRAIL` was applied on the graph
pattern then there would be zero results returned since edge `t1` is explicitly
duplicated. Instead, it's only applied on path pattern `(a1)-[t1]-(a2)`.

```googlesql {.no-copy}
GRAPH FinGraph
MATCH TRAIL (a1)-[t1]-(a2), (a2)-[t1]-(a3)
RETURN COUNT(1) as num_paths

/*-----------+
 | num_paths |
 +-----------+
 | 16        |
 +-----------*/

GRAPH FinGraph
MATCH TRAIL (a1)-[t1]-(a2)-[t1]-(a3)
RETURN COUNT(1) as num_paths

/*-----------+
 | num_paths |
 +-----------+
 | 0         |
 +-----------*/
```

The following query demonstrates the use of the `TRAIL` path mode on a
quantified path pattern. Notice that `TRAIL` is applied on a path pattern that
is the concatenation of four subpath patterns of the form `()-[:Transfers]->()`.

```googlesql {.no-copy}
GRAPH FinGraph
MATCH TRAIL (a1:Account)-[t1:Transfers]->{4}(a5:Account)
RETURN COUNT(1) as num_paths

/*-----------+
 | num_paths |
 +-----------+
 | 6         |
 +-----------*/
```

The following query demonstrates that path modes are applied individually on the
path or subpath pattern in which they are defined. In this example, the
existence of `WALK` doesn't negate the semantics of the outer `TRAIL`. Notice
that the result is the same with the previous example where `WALK` isn't
present.

```googlesql {.no-copy}
GRAPH FinGraph
MATCH TRAIL (WALK (a1:Account)-[t1:Transfers]->{4}(a5:Account))
RETURN COUNT(1) as num_paths

/*-----------+
 | num_paths |
 +-----------+
 | 6         |
 +-----------*/
```

The following query demonstrates the use of the `TRAIL` path mode on a subpath
pattern. Notice that `TRAIL` is applied on a path pattern that's the
concatenation of three subpath patterns of the form `()-[:Transfers]->()`. Since
edge `t4` is outside this path pattern, it can be equal to any of the edges on
it. Compare this result with the result of the previous query.

```googlesql {.no-copy}
GRAPH FinGraph
MATCH
  (TRAIL (a1:Account)-[t1:Transfers]->{3}(a4:Account))
  -[t4:Transfers]->(a5:Account)
RETURN COUNT(1) as num_paths

/*-----------+
 | num_paths |
 +-----------+
 | 14        |
 +-----------*/
```

The following query demonstrates the use of the `TRAIL` path mode within a
quantified path pattern. Notice that the resulting path is the concatenation of
two subpaths of the form
`()-[:Transfers]->()-[:Transfers]->()-[:Transfers]->()`. Therefore each path
includes six edges in total. `TRAIL` is applied separately on the edges of each
of the two subpaths. Specifically, the three edges on the first supath must be
distinct from each other. Similarly, the three edges on the second subpath must
also be distinct from each other. However, there may be edges that are equal
between the two subpaths.

```googlesql {.no-copy}
GRAPH FinGraph
MATCH (TRAIL -[t1:Transfers]->()-[t2:Transfers]->()-[t3:Transfers]->){2}
RETURN COUNT(1) as num_paths

/*-----------+
 | num_paths |
 +-----------+
 | 26        |
 +-----------*/
```

The following query demonstrates that there are no paths of length six with
non-repeating edges.

```googlesql {.no-copy}
GRAPH FinGraph
MATCH TRAIL -[:Transfers]->{6}
RETURN COUNT(1) as num_paths

/*-----------+
 | num_paths |
 +-----------+
 | 0         |
 +-----------*/
```

The following query demonstrates that a path can't have both a path mode and a
[path search prefix][search-prefix]:

```googlesql {.bad .no-copy}
-- Error
GRAPH FinGraph
MATCH ANY SHORTEST TRAIL ->{1,4}
RETURN COUNT(1) as num_paths
```

The following query demonstrates that path modes can coexist with
[path search prefixes][search-prefix] when the path mode is placed on a subpath.

```googlesql {.no-copy}
GRAPH FinGraph
MATCH ANY SHORTEST (TRAIL ->{1,4})
RETURN COUNT(1) as num_paths

/*-----------+
 | num_paths |
 +-----------+
 | 18        |
 +-----------*/
```

[quantified-path-pattern]: #quantified_paths

[search-prefix]: #search_prefix

