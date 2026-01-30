

<!-- mdlint off(WHITESPACE_LINE_LENGTH) -->

# GQL subqueries

The following subqueries are supported in GQL query statements:

## Subquery list

<table>
  <thead>
    <tr>
      <th>Name</th>
      <th>Summary</th>
    </tr>
  </thead>
  <tbody>

<tr>
  <td><a href="#array_subquery"><code>ARRAY</code> subquery</a>
</td>
  <td>Subquery expression that produces an array.</td>
</tr>

<tr>
  <td><a href="#exists_subquery"><code>EXISTS</code> subquery</a>
</td>
  <td>Checks if a subquery produces at least one row.</td>
</tr>

<tr>
  <td><a href="#in_subquery"><code>IN</code> subquery</a>
</td>
  <td>Checks if a subquery produces a specified value.</td>
</tr>

<tr>
  <td><a href="#array_subquery"><code>VALUE</code> subquery</a>
</td>
  <td>Subquery expression that produces a scalar value.</td>
</tr>

  </tbody>
</table>

## `ARRAY` subquery 
<a id="array_subquery"></a>

<pre>
ARRAY <span class="syntax">{</span> <span class="var">gql_query_expr</span> <span class="syntax">}</span>
</pre>

#### Description

Subquery expression that produces an array. If the subquery produces zero rows,
an empty array is produced. Never produces a `NULL` array. This can be used
wherever a query expression is supported in a GQL query statement.

#### Definitions

+   `gql_query_expr`: A GQL query expression.

#### Return type

`ARRAY<T>`

#### Examples

Note: The examples in this section reference a property graph called
[`FinGraph`][fin-graph].

[fin-graph]: https://github.com/google/googlesql/blob/master/docs/graph-schema-statements.md#fin_graph

In the following query, an array of transfer amounts is produced for each
`Account` owned by each `Person` node:

```googlesql
GRAPH FinGraph
MATCH (p:Person)-[:Owns]->(account:Account)
RETURN
 p.name, account.id AS account_id,
 ARRAY {
   MATCH (a:Account)-[transfer:Transfers]->(:Account)
   WHERE a = account
   RETURN transfer.amount AS transfers
 } AS transfers;

/*-------------------------------+
 | name | account_id | transfers |
 +-------------------+-----------+
 | Alex | 7          | [300,100] |
 | Dana | 20         | [500,200] |
 | Lee  | 16         | [300]     |
 +-------------------------------*/
```

## `EXISTS` subquery 
<a id="exists_subquery"></a>

<pre>
EXISTS <span class="syntax">{</span> <span class="var">gql_query_expr</span> <span class="syntax">}</span>
</pre>

<pre>
EXISTS <span class="syntax">{</span> <span class="var">match_statement</span> <span class="syntax">}</span>
</pre>

<pre>
EXISTS <span class="syntax">{</span> <span class="var">graph_pattern</span> <span class="syntax">}</span>
</pre>

#### Description

Checks if the subquery produces at least one row. Returns `TRUE` if
at least one row is produced, otherwise returns `FALSE`. Never produces `NULL`.

#### Definitions

+   `gql_query_expr`: A GQL query expression.
+   `match_statement`: A pattern matching operation to perform on a graph.
     For more information, see [`MATCH` statement][match-statement].
+   `graph_pattern`: A pattern to match in a graph.
     For more information, see [graph pattern definition][graph-pattern-definition].

#### Return type

`BOOL`

#### Examples

Note: The examples in this section reference a property graph called
[`FinGraph`][fin-graph].

[fin-graph]: https://github.com/google/googlesql/blob/master/docs/graph-schema-statements.md#fin_graph

The following query checks whether any person named `"Lee"` owns an
account. The subquery contains a graph query expression.

```googlesql
GRAPH FinGraph
RETURN EXISTS {
  MATCH (p:Person {Name: "Lee"})-[o:Owns]->(a:Account)
  RETURN p.Name
  LIMIT 1
} AS results;

/*---------+
 | results |
 +---------+
 | true    |
 +---------*/
```

You can include a `MATCH` statement or a graph pattern in an `EXISTS`
subquery. The following examples include two ways to construct the subquery
and produce similar results:

```googlesql
GRAPH FinGraph
RETURN EXISTS {
  MATCH (p:Person {Name: "Lee"})-[o:Owns]->(a:Account)
} AS results;

/*---------+
 | results |
 +---------+
 | true    |
 +---------*/
```

```googlesql
GRAPH FinGraph
RETURN EXISTS {
  (p:Person {Name: "Lee"})-[o:Owns]->(a:Account)
} AS results;

/*---------+
 | results |
 +---------+
 | true    |
 +---------*/
```

[match-statement]: https://github.com/google/googlesql/blob/master/docs/graph-query-statements.md#gql_match

[graph-pattern-definition]: https://github.com/google/googlesql/blob/master/docs/graph-patterns.md#graph_pattern_definition

## `IN` subquery 
<a id="in_subquery"></a>

<pre>
value [ NOT ] IN <span class="syntax">{</span> <span class="var">gql_query_expr</span> <span class="syntax">}</span>
</pre>

#### Description

Checks if `value` is present in the subquery result. Returns `TRUE` if the
result contains the `value`, otherwise returns `FALSE`.

#### Definitions

+   `value`: The value look for in the subquery result.
+   `IN`: `TRUE` if the value is in the subquery result, otherwise
    `FALSE`.
+   `NOT IN`: `FALSE` if the value is in the subquery result,
    otherwise `TRUE`.
+   `gql_query_expr`: A GQL query expression.

#### Details

The subquery result must have a single column and that column type must
be comparable to the `value` type. If not, an error is returned.

#### Return type

`BOOL`

#### Examples

Note: The examples in this section reference a property graph called
[`FinGraph`][fin-graph].

[fin-graph]: https://github.com/google/googlesql/blob/master/docs/graph-schema-statements.md#fin_graph

The following query checks if `'Dana'` is a name of a person who owns an
account.

```googlesql
GRAPH FinGraph
RETURN 'Dana' IN {
  MATCH (p:Person)-[o:Owns]->(a:Account)
  RETURN p.name
} AS results;

/*---------+
 | results |
 +---------+
 | true    |
 +---------*/
```

## `VALUE` subquery 
<a id="value_subquery"></a>

<pre>
VALUE <span class="syntax">{</span> <span class="var">gql_query_expr</span> <span class="syntax">}</span>
</pre>

#### Description

A subquery expression that produces a scalar value.

#### Definitions

+   `gql_query_expr`: A GQL query expression.

#### Details

The result of the subquery must have a single column. If the subquery returns
more than one column, the query fails with an analysis error. The result type of
the subquery expression is the produced column type. If the subquery produces
exactly one row, that single value is the subquery expression result. If the
subquery returns zero rows, the subquery expression result is `NULL`. If the
subquery returns more than one row, the query fails with a runtime error.

#### Return type

The same as the column type in the subquery result.

#### Examples

Note: The examples in this section reference a property graph called
[`FinGraph`][fin-graph].

[fin-graph]: https://github.com/google/googlesql/blob/master/docs/graph-schema-statements.md#fin_graph

The following query returns the name of any person whose `country` property
is `"Australia"`:

```googlesql
GRAPH FinGraph
RETURN VALUE {
  MATCH (p:Person {country: "Australia"})
  RETURN p.name
  LIMIT 1
} AS results;

/*---------+
 | results |
 +---------+
 | Alex    |
 +---------*/
```

