

<!-- mdlint off(WHITESPACE_LINE_LENGTH) -->

# GQL data types

Graph Query Language (GQL) supports all GoogleSQL [data types][data-types],
including the following GQL-specific data type:

## Graph data types list

<table>
  <thead>
    <tr>
      <th>Name</th>
      <th>Summary</th>
    </tr>
  </thead>
  <tbody>

<tr>
  <td><a href="#graph_element_type">Graph element type</a>
</td>
  <td>
    An element in a property graph.<br/>
    SQL type name: <code>GRAPH_ELEMENT</code>
  </td>
</tr>

  </tbody>
</table>

## Graph element type 
<a id="graph_element_type"></a>

<table>
<thead>
<tr>
<th>Name</th>
<th>Description</th>
</tr>
</thead>
<tbody>
<tr>
<td><code>GRAPH_ELEMENT</code></td>
<td>
  An element in a property graph.
</td>
</tr>
</tbody>
</table>

A variable with a `GRAPH_ELEMENT` type is produced by a graph query.
The generated type has this format:

```
GRAPH_ELEMENT<T>
```

A graph element is either a node or an edge, representing data from a
matching node or edge table based on its label. Each graph element holds a
set of properties that can be accessed with a case-insensitive name,
similar to fields of a struct.

**Example**

In the following example, `n` represents a graph element in the
[`FinGraph`][fin-graph] property graph:

```googlesql
GRAPH FinGraph
MATCH (n:Person)
RETURN n.name
```

In the following example, the [`TYPEOF`][type-of] function is used to inspect the
set of properties defined in the graph element type.

```googlesql
GRAPH FinGraph
MATCH (n:Person)
RETURN TYPEOF(n) AS t
LIMIT 1

/*----------------------------------------------+
 | t                                            |
 +----------------------------------------------+
 | GRAPH_NODE(FinGraph)<Id INT64, ..., DYNAMIC> |
 +---------------------------------------------*/
```

[graph-query]: https://github.com/google/googlesql/blob/master/docs/graph-intro.md

[fin-graph]: https://github.com/google/googlesql/blob/master/docs/graph-schema-statements.md#fin_graph

[data-types]: https://github.com/google/googlesql/blob/master/docs/data-types.md

