

<!-- mdlint off(WHITESPACE_LINE_LENGTH) -->

# GQL within SQL

GoogleSQL supports the following syntax to use GQL
within SQL queries.

## Language list

<table>
  <thead>
    <tr>
      <th>Name</th>
      <th>Summary</th>
    </tr>
  </thead>
  <tbody>

<tr>
  <td><a href="https://github.com/google/googlesql/blob/master/docs/graph-sql-queries.md#graph_table_operator"><code>GRAPH_TABLE</code> operator</a>
</td>
  <td>
    Performs an operation on a graph in the <code>FROM</code> clause of a SQL
    query and then produces a table with the results.
  </td>
</tr>

  </tbody>
</table>

## `GRAPH_TABLE` operator 
<a id="graph_table_operator"></a>

<pre>
FROM GRAPH_TABLE (
  <span class="var">property_graph_name</span>
  <span class="var">multi_linear_query_statement</span>
) [ [ AS ] <span class="var">alias</span> ]
</pre>

#### Description

Performs an operation on a graph in the `FROM` clause of a SQL query and then
produces a table with the results.

With the `GRAPH_TABLE` operator, you can use the [GQL syntax][graph-query-statements]
to query a property graph. The result of this operation is produced as a table that
you can use in the rest of the query.

#### Definitions

+ `property_graph_name`: The name of the property graph to query for patterns.
+ `multi_linear_query_statement`: You can use GQL to query a property graph for
   patterns. For more information, see [Graph query language][graph-query-statements].
+ `alias`: An optional alias, which you can use to refer to the table
  produced by the `GRAPH_TABLE` operator elsewhere in the query.

#### Examples

Note: The examples in this section reference a property graph called
[`FinGraph`][fin-graph].

[fin-graph]: https://github.com/google/googlesql/blob/master/docs/graph-schema-statements.md#fin_graph

You can use the `RETURN` statement to return specific node and edge properties.
For example:

```googlesql
SELECT name, id
FROM GRAPH_TABLE(
  FinGraph
  MATCH (n:Person)
  RETURN n.name AS name, n.id AS id
);

/*-----------+
 | name | id |
 +-----------+
 | Alex | 1  |
 | Dana | 2  |
 | Lee  | 3  |
 +-----------*/
```

You can use the `RETURN` statement to produce output with graph pattern
variables. These variables can be referenced outside `GRAPH_TABLE`. For example,

```googlesql
SELECT n.name, n.id
FROM GRAPH_TABLE(
  FinGraph
  MATCH (n:Person)
  RETURN n
);

/*-----------+
 | name | id |
 +-----------+
 | Alex | 1  |
 | Dana | 2  |
 | Lee  | 3  |
 +-----------*/
```

The following query produces an error because `id` isn't
included in the `RETURN` statement, even though this property exists for
element `n`:

```googlesql {.bad}
SELECT name, id
FROM GRAPH_TABLE(
  FinGraph
  MATCH (n:Person)
  RETURN n.name
);
```

The following query produces an error because directly outputting the graph
element `n` is not supported. Convert `n` to its JSON representation using the
`SAFE_TO_JSON` for successful output.

```googlesql {.bad}
-- Error
SELECT n
FROM GRAPH_TABLE(
  FinGraph
  MATCH (n:Person)
  RETURN n
);
```

```googlesql
SELECT SAFE_TO_JSON(n) as json_node
FROM GRAPH_TABLE(
  FinGraph
  MATCH (n:Person)
  RETURN n
);

/*---------------------------+
 | json_node                 |
 +---------------------------+
 | {"identifier":"mUZpbk...} |
 | {"identifier":"mUZpbk...} |
 | {"identifier":"mUZpbk...} |
 +--------------------------*/
```

[graph-query-statements]: https://github.com/google/googlesql/blob/master/docs/graph-query-statements.md

