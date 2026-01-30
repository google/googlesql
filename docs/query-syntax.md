

<!-- mdlint off(WHITESPACE_LINE_LENGTH) -->

# Query syntax

Query statements scan one or more tables or expressions and return the computed
result rows. This topic describes the syntax for SQL queries in
GoogleSQL.

## SQL syntax notation rules

The following table lists and describes the syntax notation rules that GoogleSQL
documentation commonly uses.

<table>
  <thead>
    <tr>
      <th>Notation</th>
      <th>Example</th>
      <th>Description</th>
    </tr>
  </thead>
  <tbody>

    <tr>
      <td>Square brackets</td>
      <td><code>[ ]</code></td>
      <td>Optional clauses</td>
    </tr>

    <tr>
      <td>Parentheses</td>
      <td><code>( )</code></td>
      <td>Literal parentheses</td>
    </tr>
    <tr>
      <td>Vertical bar</td>
      <td><code>|</code></td>
      <td>Logical <code>XOR</code> (exclusive <code>OR</code>)</td>
    </tr>
     <tr>
      <td>Curly braces</td>
      <td><code>{ }</code></td>
      <td>A set of options, such as <code>{ a | b | c }</code>. Select one option.</td>
    </tr>
    <tr>
      <td>Ellipsis</td>
      <td><code>...</code></td>
      <td>The preceding item can repeat.</td>
    </tr>
    <tr>
      <td>Comma</td>
      <td><code>,</code></td>
      <td>Literal comma</td>
    </tr>
    <tr>
      <td>Comma followed by an ellipsis</td>
      <td><nobr><code>, ...</code></nobr></td>
      <td>The preceding item can repeat in a comma-separated list.</td>
    </tr>
    <tr>
      <td rowspan="2">Item list</td>
      <td><nobr><code>item [, ...]</code></nobr></td>
      <td>One or more items</td>
    </tr>
    <tr>
      <td><nobr><code>[item, ...]</code></nobr></td>
      <td>Zero or more items</td>
    </tr>
      <tr>
      <td>Double quotes</td>
      <td><code>""</code></td>
      <td>The enclosed syntax characters (for example, <code>"{"..."}"</code>) are literal
      and required.</td>
    </tr>
    <tr>
      <td>Angle brackets</td>
      <td><code>&lt;></code></td>
      <td>Literal angle brackets</td>
    </tr>
  </tbody>
</table>

## SQL syntax

<pre>
<span class="var">query_statement</span>:
  <span class="var">query_expr</span>

<span class="var">query_expr</span>:
  [ <a href="#with_clause">WITH</a> [ <a href="#recursive_keyword">RECURSIVE</a> ] { <a href="#simple_cte"><span class="var">non_recursive_cte</span></a> | <a href="#recursive_cte"><span class="var">recursive_cte</span></a> }[, ...] ]
  { <span class="var">select</span> | ( <span class="var">query_expr</span> ) | <a href="#set_clause"><span class="var">set_operation</span></a> }
  [ <a href="#order_by_clause">ORDER</a> <a href="#order_by_clause">BY</a> <span class="var">expression</span> [{ ASC | DESC }] [, ...] ]
  [ <a href="#limit_and_offset_clause">LIMIT</a> <span class="var">count</span> [ OFFSET <span class="var">skip_rows</span> ] ]

<span class="var">select</span>:
  <a href="#select_list">SELECT</a>
    [ <a href="#dp_clause">WITH</a> <a href="#dp_clause"><span class="var">differential_privacy_clause</span></a> ]
    [ { ALL | DISTINCT } ]
    [ AS { <span class="var"><a href="#select_as_typename">typename</a></span> | <a href="#select_as_struct">STRUCT</a> | <a href="#select_as_value">VALUE</a> } ]
    <a href="#select_list"><span class="var">select_list</span></a>
  [ <a href="#from_clause">FROM</a> <a href="#from_clause"><span class="var">from_clause</span></a>[, ...] ]
  [ <a href="#where_clause">WHERE</a> <span class="var">bool_expression</span> ]
  [ <a href="#group_by_clause">GROUP</a> BY <span class="var">group_by_specification</span> ]
  [ <a href="#having_clause">HAVING</a> <span class="var">bool_expression</span> ]
  [ <a href="#qualify_clause">QUALIFY</a> <span class="var">bool_expression</span> ]
  [ <a href="#window_clause">WINDOW</a> <a href="#window_clause"><span class="var">window_clause</span></a> ]
</pre>

## `SELECT` statement 
<a id="select_list"></a>

<pre>
SELECT
  [ <a href="#dp_clause">WITH</a> <a href="#dp_clause"><span class="var">differential_privacy_clause</span></a> ]
  [ { ALL | DISTINCT } ]
  [ AS { <span class="var"><a href="#select_as_typename">typename</a></span> | <a href="#select_as_struct">STRUCT</a> | <a href="#select_as_value">VALUE</a> } ]
  <span class="var">select_list</span>

<span class="var">select_list</span>:
  { <span class="var">select_all</span> | <span class="var">select_expression</span> } [, ...]

<span class="var">select_all</span>:
  [ <span class="var">expression</span>. ]*
  [ EXCEPT ( <span class="var">column_name</span> [, ...] ) ]
  [ REPLACE ( <span class="var">expression</span> AS <span class="var">column_name</span> [, ...] ) ]

<span class="var">select_expression</span>:
  <span class="var">expression</span> [ [ AS ] <span class="var">alias</span> ]
</pre>

The `SELECT` list defines the columns that the query will return. Expressions in
the `SELECT` list can refer to columns in any of the `from_item`s in its
corresponding `FROM` clause.

Each item in the `SELECT` list is one of:

+  `*`
+  `expression`
+  `expression.*`

### `SELECT *`

`SELECT *`, often referred to as *select star*, produces one output column for
each column that's visible after executing the full query.

```googlesql
SELECT * FROM (SELECT "apple" AS fruit, "carrot" AS vegetable);

/*-------+-----------+
 | fruit | vegetable |
 +-------+-----------+
 | apple | carrot    |
 +-------+-----------*/
```

### `SELECT expression` 
<a id="select_expression"></a>

Items in a `SELECT` list can be expressions. These expressions evaluate to a
single value and produce one output column, with an optional explicit `alias`.

If the expression doesn't have an explicit alias, it receives an implicit alias
according to the rules for [implicit aliases][implicit-aliases], if possible.
Otherwise, the column is anonymous and you can't refer to it by name elsewhere
in the query.

### `SELECT expression.*` 
<a id="select_expression_star"></a>

An item in a `SELECT` list can also take the form of `expression.*`. This
produces one output column for each column or top-level field of `expression`.
The expression must either be a table alias or evaluate to a single value of a
data type with fields, such as a STRUCT.

The following query produces one output column for each column in the table
`groceries`, aliased as `g`.

```googlesql
WITH groceries AS
  (SELECT "milk" AS dairy,
   "eggs" AS protein,
   "bread" AS grain)
SELECT g.*
FROM groceries AS g;

/*-------+---------+-------+
 | dairy | protein | grain |
 +-------+---------+-------+
 | milk  | eggs    | bread |
 +-------+---------+-------*/
```

More examples:

```googlesql
WITH locations AS
  (SELECT STRUCT("Seattle" AS city, "Washington" AS state) AS location
  UNION ALL
  SELECT STRUCT("Phoenix" AS city, "Arizona" AS state) AS location)
SELECT l.location.*
FROM locations l;

/*---------+------------+
 | city    | state      |
 +---------+------------+
 | Seattle | Washington |
 | Phoenix | Arizona    |
 +---------+------------*/
```

```googlesql
WITH locations AS
  (SELECT ARRAY<STRUCT<city STRING, state STRING>>[("Seattle", "Washington"),
    ("Phoenix", "Arizona")] AS location)
SELECT l.LOCATION[offset(0)].*
FROM locations l;

/*---------+------------+
 | city    | state      |
 +---------+------------+
 | Seattle | Washington |
 +---------+------------*/
```

### `SELECT * EXCEPT`

A `SELECT * EXCEPT` statement specifies the names of one or more columns to
exclude from the result. All matching column names are omitted from the output.

```googlesql
WITH orders AS
  (SELECT 5 as order_id,
  "sprocket" as item_name,
  200 as quantity)
SELECT * EXCEPT (order_id)
FROM orders;

/*-----------+----------+
 | item_name | quantity |
 +-----------+----------+
 | sprocket  | 200      |
 +-----------+----------*/
```

Note: `SELECT * EXCEPT` doesn't exclude columns that don't have names.

### `SELECT * REPLACE`

A `SELECT * REPLACE` statement specifies one or more
`expression AS identifier` clauses. Each identifier must match a column name
from the `SELECT *` statement. In the output column list, the column that
matches the identifier in a `REPLACE` clause is replaced by the expression in
that `REPLACE` clause.

A `SELECT * REPLACE` statement doesn't change the names or order of columns.
However, it can change the value and the value type.

```googlesql
WITH orders AS
  (SELECT 5 as order_id,
  "sprocket" as item_name,
  200 as quantity)
SELECT * REPLACE ("widget" AS item_name)
FROM orders;

/*----------+-----------+----------+
 | order_id | item_name | quantity |
 +----------+-----------+----------+
 | 5        | widget    | 200      |
 +----------+-----------+----------*/

WITH orders AS
  (SELECT 5 as order_id,
  "sprocket" as item_name,
  200 as quantity)
SELECT * REPLACE (quantity/2 AS quantity)
FROM orders;

/*----------+-----------+----------+
 | order_id | item_name | quantity |
 +----------+-----------+----------+
 | 5        | sprocket  | 100      |
 +----------+-----------+----------*/
```

Note: `SELECT * REPLACE` doesn't replace columns that don't have names.

### `SELECT DISTINCT`

A `SELECT DISTINCT` statement discards duplicate rows and returns only the
remaining rows. `SELECT DISTINCT` can't return columns of the following types:

+  `PROTO`
+  `GRAPH_ELEMENT`

In the following example, `SELECT DISTINCT` is used to produce distinct arrays:

```googlesql
WITH PlayerStats AS (
  SELECT ['Coolidge', 'Adams'] as Name, 3 as PointsScored UNION ALL
  SELECT ['Adams', 'Buchanan'], 0 UNION ALL
  SELECT ['Coolidge', 'Adams'], 1 UNION ALL
  SELECT ['Kiran', 'Noam'], 1)
SELECT DISTINCT Name
FROM PlayerStats;

/*------------------+
 | Name             |
 +------------------+
 | [Coolidge,Adams] |
 | [Adams,Buchanan] |
 | [Kiran,Noam]     |
 +------------------*/
```

In the following example, `SELECT DISTINCT` is used to produce distinct structs:

```googlesql
WITH
  PlayerStats AS (
    SELECT
      STRUCT<last_name STRING, first_name STRING, age INT64>(
        'Adams', 'Noam', 20) AS Player,
      3 AS PointsScored UNION ALL
    SELECT ('Buchanan', 'Jie', 19), 0 UNION ALL
    SELECT ('Adams', 'Noam', 20), 4 UNION ALL
    SELECT ('Buchanan', 'Jie', 19), 13
  )
SELECT DISTINCT Player
FROM PlayerStats;

/*--------------------------+
 | player                   |
 +--------------------------+
 | {                        |
 |   last_name: "Adams",    |
 |   first_name: "Noam",    |
 |   age: 20                |
 |  }                       |
 +--------------------------+
 | {                        |
 |   last_name: "Buchanan", |
 |   first_name: "Jie",     |
 |   age: 19                |
 |  }                       |
 +---------------------------*/
```

### `SELECT ALL`

A `SELECT ALL` statement returns all rows, including duplicate rows.
`SELECT ALL` is the default behavior of `SELECT`.

### `SELECT AS STRUCT`

```googlesql
SELECT AS STRUCT expr [[AS] struct_field_name1] [,...]
```

This produces a [value table][value-tables] with a
STRUCT row type, where the
STRUCT field names and types match the column names
and types produced in the `SELECT` list.

Example:

```googlesql
SELECT ARRAY(SELECT AS STRUCT 1 a, 2 b)
```

`SELECT AS STRUCT` can be used in a scalar or array subquery to produce a single
STRUCT type grouping multiple values together. Scalar
and array subqueries (see [Subqueries][subquery-concepts]) are normally not
allowed to return multiple columns, but can return a single column with
STRUCT type.

Anonymous columns are allowed.

Example:

```googlesql
SELECT AS STRUCT 1 x, 2, 3
```

The query above produces STRUCT values of type
`STRUCT<int64 x, int64, int64>.` The first field has the name `x` while the
second and third fields are anonymous.

The example above produces the same result as this `SELECT AS VALUE` query using
a struct constructor:

```googlesql
SELECT AS VALUE STRUCT(1 AS x, 2, 3)
```

Duplicate columns are allowed.

Example:

```googlesql
SELECT AS STRUCT 1 x, 2 y, 3 x
```

The query above produces STRUCT values of type
`STRUCT<int64 x, int64 y, int64 x>.` The first and third fields have the same
name `x` while the second field has the name `y`.

The example above produces the same result as this `SELECT AS VALUE` query
using a struct constructor:

```googlesql
SELECT AS VALUE STRUCT(1 AS x, 2 AS y, 3 AS x)
```

### `SELECT AS typename` 
<a id="select_as_typename"></a>

```googlesql
SELECT AS typename
  expr [[AS] field]
  [, ...]
```

A `SELECT AS typename` statement produces a value table where the row type
is a specific named type. Currently, [protocol buffers][proto-buffers] are the
only supported type that can be used with this syntax.

When selecting as a type that has fields, such as a proto message type,
the `SELECT` list may produce multiple columns. Each produced column must have
an explicit or [implicit][implicit-aliases] alias that matches a unique field of
the named type.

When used with `SELECT DISTINCT`, or `GROUP BY` or `ORDER BY` using column
ordinals, these operators are first applied on the columns in the `SELECT` list.
The value construction happens last. This means that `DISTINCT` can be applied
on the input columns to the value construction, including in
cases where `DISTINCT` wouldn't be allowed after value construction because
grouping isn't supported on the constructed type.

The following is an example of a `SELECT AS typename` query.

```googlesql
SELECT AS tests.TestProtocolBuffer mytable.key int64_val, mytable.name string_val
FROM mytable;
```

The query returns the output as a `tests.TestProtocolBuffer` protocol
buffer. `mytable.key int64_val` means that values from the `key` column are
stored in the `int64_val` field in the protocol buffer. Similarly, values from
the `mytable.name` column are stored in the `string_val` protocol buffer field.

To learn more about protocol buffers, see
[Work with protocol buffers][proto-buffers].

### `SELECT AS VALUE`

`SELECT AS VALUE` produces a [value table][value-tables] from any
`SELECT` list that produces exactly one column. Instead of producing an
output table with one column, possibly with a name, the output will be a
value table where the row type is just the value type that was produced in the
one `SELECT` column.  Any alias the column had will be discarded in the
value table.

Example:

```googlesql
SELECT AS VALUE 1
```

The query above produces a table with row type INT64.

Example:

```googlesql
SELECT AS VALUE STRUCT(1 AS a, 2 AS b) xyz
```

The query above produces a table with row type `STRUCT<a int64, b int64>`.

Example:

```googlesql
SELECT AS VALUE v FROM (SELECT AS STRUCT 1 a, true b) v WHERE v.b
```

Given a value table `v` as input, the query above filters out certain values in
the `WHERE` clause, and then produces a value table using the exact same value
that was in the input table. If the query above didn't use `SELECT AS VALUE`,
then the output table schema would differ from the input table schema because
the output table would be a regular table with a column named `v` containing the
input value.

## `FROM` clause

<pre>
FROM <span class="var">from_clause</span>[, ...]

<span class="var">from_clause</span>:
  <span class="var">from_item</span>
  [ { <a href="#pivot_operator"><span class="var">pivot_operator</span></a> | <a href="#unpivot_operator"><span class="var">unpivot_operator</span></a> } ]
  [ <a href="#tablesample_operator"><span class="var">tablesample_operator</span></a> ]

<span class="var">from_item</span>:
  {
    <span class="var">table_name</span> [ <span class="var">as_alias</span> ]
    | { <a href="#join_types"><span class="var">join_operation</span></a> | ( <a href="#join_types"><span class="var">join_operation</span></a> ) }
    | ( <span class="var">query_expr</span> ) [ <span class="var">as_alias</span> ]
    | <span class="var">field_path</span>
    | <a href="#unnest_operator"><span class="var">unnest_operator</span></a>
    | <span class="var"><a href="#cte_name">cte_name</a></span> [ <span class="var">as_alias</span> ]
    | <a href="#graph_table_operator_clause_from"><span class="var">graph_table_operator</span></a> [ <span class="var">as_alias</span> ]
  }

<span class="var">as_alias</span>:
  [ AS ] <span class="var">alias</span>
</pre>

The `FROM` clause indicates the table or tables from which to retrieve rows,
and specifies how to join those rows together to produce a single stream of
rows for processing in the rest of the query.

#### `pivot_operator` 
<a id="pivot_operator_stub"></a>

See [PIVOT operator][pivot-operator].

#### `unpivot_operator` 
<a id="unpivot_operator_stub"></a>

See [UNPIVOT operator][unpivot-operator].

#### `tablesample_operator` 
<a id="tablesample_operator_clause"></a>

See [TABLESAMPLE operator][tablesample-operator].

#### `graph_table_operator` 
<a id="graph_table_operator_clause_from"></a>

See [GRAPH_TABLE operator][graph-table-operator].

#### `table_name`

The name (optionally qualified) of an existing table.

<pre>
SELECT * FROM Roster;
SELECT * FROM db.Roster;
</pre>

#### `join_operation`

See [Join operation][query-joins].

#### `query_expr`

`( query_expr ) [ [ AS ] alias ]` is a [table subquery][table-subquery-concepts].

#### `field_path`

In the `FROM` clause, `field_path` is any path that
resolves to a field within a data type. `field_path` can go
arbitrarily deep into a nested data structure.

Some examples of valid `field_path` values include:

```googlesql
SELECT * FROM T1 t1, t1.array_column;

SELECT * FROM T1 t1, t1.struct_column.array_field;

SELECT (SELECT ARRAY_AGG(c) FROM t1.array_column c) FROM T1 t1;

SELECT a.struct_field1 FROM T1 t1, t1.array_of_structs a;

SELECT (SELECT STRING_AGG(a.struct_field1) FROM t1.array_of_structs a) FROM T1 t1;
```

Field paths in the `FROM` clause must end in an
array or a repeated field. In
addition, field paths can't contain arrays
or repeated fields before the end of the path. For example, the path
`array_column.some_array.some_array_field` is invalid because it
contains an array before the end of the path.

Note: If a path has only one name, it's interpreted as a table.
To work around this, wrap the path using `UNNEST`, or use the
fully-qualified path.

Note: If a path has more than one name, and it matches a field
name, it's interpreted as a field name. To force the path to be interpreted as
a table name, wrap the path using <code>`</code>.

#### `unnest_operator` 
<a id="unnest_operator_clause"></a>

See [UNNEST operator][unnest-operator].

#### `cte_name`

Common table expressions (CTEs) in a [`WITH` Clause][with-clause] act like
temporary tables that you can reference anywhere in the `FROM` clause.
In the example below, `subQ1` and `subQ2` are CTEs.

Example:

```googlesql
WITH
  subQ1 AS (SELECT * FROM Roster WHERE SchoolID = 52),
  subQ2 AS (SELECT SchoolID FROM subQ1)
SELECT DISTINCT * FROM subQ2;
```

The `WITH` clause hides any permanent tables with the same name
for the duration of the query, unless you qualify the table name, for example:

 `db.Roster`.

## `UNNEST` operator 
<a id="unnest_operator"></a>

<pre>
<span class="var">unnest_operator</span>:
  {
    <a href="#unnest">UNNEST</a>( <span class="var">array</span> ) [ <span class="var">as_alias</span> ]
    | <span class="var">array_path</span> [ <span class="var">as_alias</span> ]
  }
  [ WITH OFFSET [ <span class="var">as_alias</span> ] ]

<span class="var">array</span>:
  { <span class="var">array_expression</span> | <span class="var">array_path</span> }

<span class="var">as_alias</span>:
  [AS] <span class="var">alias</span>
</pre>

The `UNNEST` operator takes an array and returns a table with one row for each
element in the array. The output of `UNNEST` is one [value table][value-tables] column.
For these `ARRAY` element types, `SELECT *` against the value table column
returns multiple columns:

+ `STRUCT`
+ `PROTO`

Input values:

+ `array_expression`: An expression that produces an array and that's not an
  array path.
+ `array_path`: The path to an `ARRAY` or
  non-`ARRAY` type, which may or may not contain a flattening operation, using the
  [array elements field access operation][array-el-field-operator].
    + In an implicit `UNNEST` operation, the path
      must
      start with
      a
      [range variable][range-variables] name.
    + In an explicit `UNNEST` operation, the path can optionally start with a
      [range variable][range-variables] name.

  The `UNNEST` operation with any [correlated][correlated-join] `array_path` must
  be on the right side of a `CROSS JOIN`, `LEFT JOIN`, or
  `INNER JOIN` operation.
+ `as_alias`: If specified, defines the explicit name of the value table
  column containing the array element values. It can be used to refer to
  the column elsewhere in the query.
+ `WITH OFFSET`: `UNNEST` destroys the order of elements in the input
  array. Use this optional clause to return an additional column with
  the array element indexes, or _offsets_. Offset counting starts at zero for
  each row produced by the `UNNEST` operation. This column has an
  optional alias; If the optional alias isn't used, the default column name is
  `offset`.

  Example:

  ```googlesql
  SELECT * FROM UNNEST ([10,20,30]) as numbers WITH OFFSET;

  /*---------+--------+
   | numbers | offset |
   +---------+--------+
   | 10      | 0      |
   | 20      | 1      |
   | 30      | 2      |
   +---------+--------*/
  ```

  

You can also use `UNNEST` outside of the `FROM` clause with the
[`IN` operator][in-operator].

For several ways to use `UNNEST`, including construction, flattening, and
filtering, see [Work with arrays][working-with-arrays].

To learn more about the ways you can use `UNNEST` explicitly and implicitly,
see [Explicit and implicit `UNNEST`][explicit-implicit-unnest].

### `UNNEST` and structs

For an input array of structs, `UNNEST`
returns a row for each struct, with a separate column for each field in the
struct. The alias for each column is the name of the corresponding struct
field.

Example:

```googlesql
SELECT *
FROM UNNEST(
  ARRAY<
    STRUCT<
      x INT64,
      y STRING,
      z STRUCT<a INT64, b INT64>>>[
        (1, 'foo', (10, 11)),
        (3, 'bar', (20, 21))]);

/*---+-----+----------+
 | x | y   | z        |
 +---+-----+----------+
 | 1 | foo | {10, 11} |
 | 3 | bar | {20, 21} |
 +---+-----+----------*/
```

Because the `UNNEST` operator returns a
[value table][value-tables],
you can alias `UNNEST` to define a range variable that you can reference
elsewhere in the query. If you reference the range variable in the `SELECT`
list, the query returns a struct containing all of the fields of the original
struct in the input table.

Example:

```googlesql
SELECT *, struct_value
FROM UNNEST(
  ARRAY<
    STRUCT<
    x INT64,
    y STRING>>[
      (1, 'foo'),
      (3, 'bar')]) AS struct_value;

/*---+-----+--------------+
 | x | y   | struct_value |
 +---+-----+--------------+
 | 3 | bar | {3, bar}     |
 | 1 | foo | {1, foo}     |
 +---+-----+--------------*/
```

### `UNNEST` and protocol buffers

For an input array of protocol buffers, `UNNEST` returns a row for each
protocol buffer, with a separate column for each field in the
protocol buffer. The alias for each column is the name of the corresponding
protocol buffer field.

Example:

```googlesql
SELECT *
FROM UNNEST(
  ARRAY<googlesql.examples.music.Album>[
    NEW googlesql.examples.music.Album (
      'The Goldberg Variations' AS album_name,
      ['Aria', 'Variation 1', 'Variation 2'] AS song
    )
  ]
);

/*-------------------------+--------+----------------------------------+
 | album_name              | singer | song                             |
 +-------------------------+--------+----------------------------------+
 | The Goldberg Variations | NULL   | [Aria, Variation 1, Variation 2] |
 +-------------------------+--------+----------------------------------*/
```

As with structs, you can alias `UNNEST` to define a range variable. You
can reference this alias in the `SELECT` list to return a value table where each
row is a protocol buffer element from the array.

```googlesql
SELECT proto_value
FROM UNNEST(
  ARRAY<googlesql.examples.music.Album>[
    NEW googlesql.examples.music.Album (
      'The Goldberg Variations' AS album_name,
      ['Aria', 'Var. 1'] AS song
    )
  ]
) AS proto_value;

/*---------------------------------------------------------------------+
 | proto_value                                                         |
 +---------------------------------------------------------------------+
 | {album_name: "The Goldberg Variations" song: "Aria" song: "Var. 1"} |
 +---------------------------------------------------------------------*/
```

### Explicit and implicit `UNNEST` 
<a id="explicit_implicit_unnest"></a>

Array unnesting can be either explicit or implicit. To learn more, see the
following sections.

#### Explicit unnesting

The `UNNEST` keyword is required in explicit unnesting. For example:

```googlesql
WITH Coordinates AS (SELECT ARRAY<STRUCT<x INT64, y ARRAY<INT64>>>[(1, [2,3]), (4, [5,6])] AS position)
SELECT results FROM Coordinates, UNNEST(Coordinates.position.y) AS results;
```

This example and the following examples use the `array_path` called
`Coordinates.position` to illustrate unnesting.

##### Tables and explicit unnesting

When you use `array_path` with explicit `UNNEST`,
you can optionally prepend `array_path` with a table.

The following queries produce the same results:

```googlesql
WITH Coordinates AS (SELECT ARRAY<STRUCT<x INT64, y ARRAY<INT64>>>[(1, [2,3]), (4, [5,6])] AS position)
SELECT results FROM Coordinates, UNNEST(position.y) AS results;
```

```googlesql
WITH Coordinates AS (SELECT ARRAY<STRUCT<x INT64, y ARRAY<INT64>>>[(1, [2,3]), (4, [5,6])] AS position)
SELECT results FROM Coordinates, UNNEST(Coordinates.position.y) AS results;
```

#### Implicit unnesting

The `UNNEST` keyword isn't used in implicit unnesting.

For example:

```googlesql
WITH Coordinates AS (SELECT ARRAY<STRUCT<x INT64, y ARRAY<INT64>>>[(1, [2,3]), (4, [5,6])] AS position)
SELECT results FROM Coordinates, Coordinates.position.y AS results;
```

When you use `array_path` with `UNNEST`, the
[`FLATTEN` operator][flatten-operator] is used implicitly. These are equivalent:

```googlesql
-- In UNNEST, FLATTEN used explicitly:
WITH Coordinates AS (SELECT ARRAY<STRUCT<x INT64, y ARRAY<INT64>>>[(1, [2,3]), (4, [5,6])] AS position)
SELECT results FROM Coordinates, UNNEST(FLATTEN(Coordinates.position.y)) AS results;
```

```googlesql
-- In UNNEST, FLATTEN used implicitly:
WITH Coordinates AS (SELECT ARRAY<STRUCT<x INT64, y ARRAY<INT64>>>[(1, [2,3]), (4, [5,6])] AS position)
SELECT results FROM Coordinates, UNNEST(Coordinates.position.y) AS results;
```

```googlesql
-- In the FROM clause, UNNEST used implicitly:
WITH Coordinates AS (SELECT ARRAY<STRUCT<x INT64, y ARRAY<INT64>>>[(1, [2,3]), (4, [5,6])] AS position)
SELECT results FROM Coordinates, Coordinates.position.y AS results;
```

##### Tables and implicit unnesting

When you use `array_path` with implicit `UNNEST`, `array_path` must be prepended
with the table. For example:

```googlesql
WITH Coordinates AS (SELECT [1,2] AS position)
SELECT results FROM Coordinates, Coordinates.position AS results;
```

##### Array subscript operator limitations in implicit unnesting

You can use `UNNEST` with `array_path` implicitly
in the `FROM` clause, but only if the
[array subscript operator][array-subscript-operator] isn't included.

The following query is valid:

```googlesql
WITH Coordinates AS (SELECT ARRAY<STRUCT<x INT64, y ARRAY<INT64>>>[(1, [2,3]), (4, [5,6])] AS position)
SELECT results FROM Coordinates, UNNEST(Coordinates.position.y[SAFE_OFFSET(1)]) AS results;
```

The following query is invalid:

```googlesql {.bad}
-- Invalid
WITH Coordinates AS (SELECT ARRAY<STRUCT<x INT64, y ARRAY<INT64>>>[(1, [2,3]), (4, [5,6])] AS position)
SELECT results FROM Coordinates, Coordinates.position.y[SAFE_OFFSET(1)] AS results;
```

### `UNNEST` and `NULL` values 
<a id="unnest_and_nulls"></a>

`UNNEST` treats `NULL` values as follows:

+  `NULL` and empty arrays produce zero rows.
+  An array containing `NULL` values produces rows containing `NULL` values.

## `PIVOT` operator 
<a id="pivot_operator"></a>

<pre>
FROM <span class="var">from_item</span>[, ...] <span class="var">pivot_operator</span>

<span class="var">pivot_operator</span>:
  PIVOT(
    <span class="var">aggregate_function_call</span> [<span class="var">as_alias</span>][, ...]
    FOR <span class="var">input_column</span>
    IN ( <span class="var">pivot_column</span> [<span class="var">as_alias</span>][, ...] )
  ) [AS <span class="var">alias</span>]

<span class="var">as_alias</span>:
  [AS] <span class="var">alias</span>
</pre>

The `PIVOT` operator rotates rows into columns, using aggregation.
`PIVOT` is part of the `FROM` clause.

+ `PIVOT` can be used to modify any table expression.
+ A `WITH OFFSET` clause immediately preceding the `PIVOT` operator isn't
  allowed.

Conceptual example:

```googlesql
-- Before PIVOT is used to rotate sales and quarter into Q1, Q2, Q3, Q4 columns:
/*---------+-------+---------+------+
 | product | sales | quarter | year |
 +---------+-------+---------+------|
 | Kale    | 51    | Q1      | 2020 |
 | Kale    | 23    | Q2      | 2020 |
 | Kale    | 45    | Q3      | 2020 |
 | Kale    | 3     | Q4      | 2020 |
 | Kale    | 70    | Q1      | 2021 |
 | Kale    | 85    | Q2      | 2021 |
 | Apple   | 77    | Q1      | 2020 |
 | Apple   | 0     | Q2      | 2020 |
 | Apple   | 1     | Q1      | 2021 |
 +---------+-------+---------+------*/

-- After PIVOT is used to rotate sales and quarter into Q1, Q2, Q3, Q4 columns:
/*---------+------+----+------+------+------+
 | product | year | Q1 | Q2   | Q3   | Q4   |
 +---------+------+----+------+------+------+
 | Apple   | 2020 | 77 | 0    | NULL | NULL |
 | Apple   | 2021 | 1  | NULL | NULL | NULL |
 | Kale    | 2020 | 51 | 23   | 45   | 3    |
 | Kale    | 2021 | 70 | 85   | NULL | NULL |
 +---------+------+----+------+------+------*/
```

**Definitions**

Top-level definitions:

+ `from_item`: The table, subquery, or
  table-valued function (TVF) on which
  to perform a pivot operation. The `from_item` must
  [follow these rules](#rules_for_pivot_from_item).
+ `pivot_operator`: The pivot operation to perform on a `from_item`.
+ `alias`: An alias to use for an item in the query.

`pivot_operator` definitions:

+ `aggregate_function_call`: An aggregate function call that aggregates all
  input rows such that `input_column` matches a particular value in
  `pivot_column`. Each aggregation corresponding to a different `pivot_column`
  value produces a different column in the output.
  [Follow these rules](#rules_for_pivot_agg_function) when creating an
  aggregate function call.
+ `input_column`: Takes a column and retrieves the row values for the
  column, [following these rules](#rules_input_column).
+ `pivot_column`: A pivot column to create for each aggregate function
  call. If an alias isn't provided, a default alias is created. A pivot column
  value type must match the value type in `input_column` so that the values can
  be compared. It's possible to have a value in `pivot_column` that doesn't
  match a value in `input_column`. Must be a constant and
  [follow these rules](#rules_pivot_column).

**Rules**

<a id="rules_for_pivot_from_item"></a>
Rules for a `from_item` passed to `PIVOT`:

+ The `from_item` may consist of any
  table, subquery, or table-valued function
  (TVF) result.
+ The `from_item` may not produce a value table.
+ The `from_item` may not be a subquery using `SELECT AS STRUCT`.

<a id="rules_for_pivot_agg_function"></a>
Rules for `aggregate_function_call`:

+ Must be an aggregate function. For example, `SUM`.
+ You may reference columns in a table passed to `PIVOT`, as
  well as correlated columns, but may not access columns defined by the `PIVOT`
  clause itself.
+ A table passed to `PIVOT` may be accessed through its alias if one is
  provided.

<a id="rules_input_column"></a>
Rules for `input_column`:

+ May access columns from the input table, as well as correlated columns,
  not columns defined by the `PIVOT` clause, itself.
+ Evaluated against each row in the input table; aggregate and window function
  calls are prohibited.
+ Non-determinism is okay.
+ The type must be groupable.
+ The input table may be accessed through its alias if one is provided.

<a id="rules_pivot_column"></a>
Rules for `pivot_column`:

+ A `pivot_column` must be a constant.
+ Named constants, such as variables, aren't supported.
+ Query parameters aren't supported.
+ If a name is desired for a named constant or query parameter,
  specify it explicitly with an alias.
+ Corner cases exist where a distinct `pivot_column`s can end up with the same
  default column names. For example, an input column might contain both a
  `NULL` value and the string literal `"NULL"`. When this happens, multiple
  pivot columns are created with the same name. To avoid this situation,
  use aliases for pivot column names.
+ If a `pivot_column` doesn't specify an alias, a column name is constructed as
  follows:

<table>
  <thead>
    <tr>
      <th>From</th>
      <th>To</th>
      <th width='400px'>Example</th>
    </tr>
  </thead>
  <tbody>
    <tr>
      <td>NULL</td>
      <td>NULL</td>
      <td>
        Input: NULL<br />
        Output: "NULL"<br />
      </td>
    </tr>
    <tr>
      <td>

<span><code>INT32</code></span><br /><span><code>INT64</code></span><br /><span><code>UINT32</code></span><br /><span><code>UINT64</code></span><br /><span><code>NUMERIC</code></span><br /><span><code>BIGNUMERIC</code></span><br />
</td>
      <td>
        The number in string format with the following rules:
        <ul>
          <li>
            Positive numbers are preceded with <code>_</code>.
          </li>
          <li>
            Negative numbers are preceded with <code>minus_</code>.
          </li>
          <li>
            A decimal point is replaced with <code>_point_</code>.
          </li>
        </ul>
      </td>
      <td>
        Input: 1<br />
        Output: _1<br />
        <hr />
        Input: -1<br />
        Output: minus_1<br />
        <hr />
        Input: 1.0<br />
        Output: _1_point_0<br />
      </td>
    </tr>
    <tr>
      <td>BOOL</td>
      <td><code>TRUE</code> or <code>FALSE</code>.</td>
      <td>
        Input: TRUE<br />
        Output: TRUE<br />
        <hr />
        Input: FALSE<br />
        Output: FALSE<br />
      </td>
    </tr>
    <tr>
      <td>STRING</td>
      <td>The string value.</td>
      <td>
        Input: "PlayerName"<br />
        Output: PlayerName<br />
      </td>
    </tr>
    <tr>
      <td>DATE</td>
      <td>The date in <code>_YYYY_MM_DD</code> format.</td>
      <td>
        Input: DATE '2013-11-25'<br />
        Output: _2013_11_25<br />
      </td>
    </tr>
    <tr>
      <td>ENUM</td>
      <td>The name of the enumeration constant.</td>
      <td>
        Input: COLOR.RED<br />
        Output: RED<br />
      </td>
    </tr>
    <tr>
      <td>STRUCT</td>
      <td>
        A string formed by computing the <code>pivot_column</code> name for each
        field and joining the results together with an underscore. The following
        rules apply:
        <ul>
          <li>
            If the field is named:
            <code>&lt;field_name&gt;_&lt;pivot_column_name_for_field_name&gt;</code>.
          </li>
          <li>
            If the field is unnamed:
            <code>&lt;pivot_column_name_for_field_name&gt;</code>.
          </li>
        </ul>
        <p>
          <code>&lt;pivot_column_name_for_field_name&gt;</code> is determined by
          applying the rules in this table, recursively. If no rule is available
          for any <code>STRUCT</code> field, the entire pivot column is unnamed.
        </p>
        <p>
          Due to implicit type coercion from the <code>IN</code> list values to
          the type of <code>&lt;value-expression&gt;</code>, field names must be
          present in <code>input_column</code> to have an effect on the names of
          the pivot columns.
        </p>
      </td>
      <td>
        Input: STRUCT("one", "two")<br />
        Output: one_two<br />
        <hr />
        Input: STRUCT("one" AS a, "two" AS b)<br />
        Output: one_a_two_b<br />
      </td>
    </tr>
    <tr>
      <td>All other data types</td>
      <td>Not supported. You must provide an alias.</td>
      <td>
      </td>
    </tr>
  </tbody>
</table>

**Examples**

The following examples reference a table called `Produce` that looks like this:

```googlesql
WITH Produce AS (
  SELECT 'Kale' as product, 51 as sales, 'Q1' as quarter, 2020 as year UNION ALL
  SELECT 'Kale', 23, 'Q2', 2020 UNION ALL
  SELECT 'Kale', 45, 'Q3', 2020 UNION ALL
  SELECT 'Kale', 3, 'Q4', 2020 UNION ALL
  SELECT 'Kale', 70, 'Q1', 2021 UNION ALL
  SELECT 'Kale', 85, 'Q2', 2021 UNION ALL
  SELECT 'Apple', 77, 'Q1', 2020 UNION ALL
  SELECT 'Apple', 0, 'Q2', 2020 UNION ALL
  SELECT 'Apple', 1, 'Q1', 2021)
SELECT * FROM Produce

/*---------+-------+---------+------+
 | product | sales | quarter | year |
 +---------+-------+---------+------|
 | Kale    | 51    | Q1      | 2020 |
 | Kale    | 23    | Q2      | 2020 |
 | Kale    | 45    | Q3      | 2020 |
 | Kale    | 3     | Q4      | 2020 |
 | Kale    | 70    | Q1      | 2021 |
 | Kale    | 85    | Q2      | 2021 |
 | Apple   | 77    | Q1      | 2020 |
 | Apple   | 0     | Q2      | 2020 |
 | Apple   | 1     | Q1      | 2021 |
 +---------+-------+---------+------*/
```

With the `PIVOT` operator, the rows in the `quarter` column are rotated into
these new columns: `Q1`, `Q2`, `Q3`, `Q4`. The aggregate function `SUM` is
implicitly grouped by all unaggregated columns other than the `pivot_column`:
`product` and `year`.

```googlesql
SELECT * FROM
  Produce
  PIVOT(SUM(sales) FOR quarter IN ('Q1', 'Q2', 'Q3', 'Q4'))

/*---------+------+----+------+------+------+
 | product | year | Q1 | Q2   | Q3   | Q4   |
 +---------+------+----+------+------+------+
 | Apple   | 2020 | 77 | 0    | NULL | NULL |
 | Apple   | 2021 | 1  | NULL | NULL | NULL |
 | Kale    | 2020 | 51 | 23   | 45   | 3    |
 | Kale    | 2021 | 70 | 85   | NULL | NULL |
 +---------+------+----+------+------+------*/
```

If you don't include `year`, then `SUM` is grouped only by `product`.

```googlesql
SELECT * FROM
  (SELECT product, sales, quarter FROM Produce)
  PIVOT(SUM(sales) FOR quarter IN ('Q1', 'Q2', 'Q3', 'Q4'))

/*---------+-----+-----+------+------+
 | product | Q1  | Q2  | Q3   | Q4   |
 +---------+-----+-----+------+------+
 | Apple   | 78  | 0   | NULL | NULL |
 | Kale    | 121 | 108 | 45   | 3    |
 +---------+-----+-----+------+------*/
```

You can select a subset of values in the `pivot_column`:

```googlesql
SELECT * FROM
  (SELECT product, sales, quarter FROM Produce)
  PIVOT(SUM(sales) FOR quarter IN ('Q1', 'Q2', 'Q3'))

/*---------+-----+-----+------+
 | product | Q1  | Q2  | Q3   |
 +---------+-----+-----+------+
 | Apple   | 78  | 0   | NULL |
 | Kale    | 121 | 108 | 45   |
 +---------+-----+-----+------*/
```

```googlesql
SELECT * FROM
  (SELECT sales, quarter FROM Produce)
  PIVOT(SUM(sales) FOR quarter IN ('Q1', 'Q2', 'Q3'))

/*-----+-----+----+
 | Q1  | Q2  | Q3 |
 +-----+-----+----+
 | 199 | 108 | 45 |
 +-----+-----+----*/
```

You can include multiple aggregation functions in the `PIVOT`. In this case, you
must specify an alias for each aggregation. These aliases are used to construct
the column names in the resulting table.

```googlesql
SELECT * FROM
  (SELECT product, sales, quarter FROM Produce)
  PIVOT(SUM(sales) AS total_sales, COUNT(*) AS num_records FOR quarter IN ('Q1', 'Q2'))

/*--------+----------------+----------------+----------------+----------------+
 |product | total_sales_Q1 | num_records_Q1 | total_sales_Q2 | num_records_Q2 |
 +--------+----------------+----------------+----------------+----------------+
 | Kale   | 121            | 2              | 108            | 2              |
 | Apple  | 78             | 2              | 0              | 1              |
 +--------+----------------+----------------+----------------+----------------*/
```

## `UNPIVOT` operator 
<a id="unpivot_operator"></a>

<pre>
FROM <span class="var">from_item</span>[, ...] <span class="var">unpivot_operator</span>

<span class="var">unpivot_operator</span>:
  UNPIVOT [ { INCLUDE NULLS | EXCLUDE NULLS } ] (
    { <span class="var">single_column_unpivot</span> | <span class="var">multi_column_unpivot</span> }
  ) [<span class="var">unpivot_alias</span>]

<span class="var">single_column_unpivot</span>:
  <span class="var">values_column</span>
  FOR <span class="var">name_column</span>
  IN (<span class="var">columns_to_unpivot</span>)

<span class="var">multi_column_unpivot</span>:
  <span class="var">values_column_set</span>
  FOR <span class="var">name_column</span>
  IN (<span class="var">column_sets_to_unpivot</span>)

<span class="var">values_column_set</span>:
  (<span class="var">values_column</span>[, ...])

<span class="var">columns_to_unpivot</span>:
  <span class="var">unpivot_column</span> [<span class="var">row_value_alias</span>][, ...]

<span class="var">column_sets_to_unpivot</span>:
  (<span class="var">unpivot_column</span> [<span class="var">row_value_alias</span>][, ...])

<span class="var">unpivot_alias</span> and <span class="var">row_value_alias</span>:
  [AS] <span class="var">alias</span>
</pre>

The `UNPIVOT` operator rotates columns into rows. `UNPIVOT` is part of the
`FROM` clause.

+ `UNPIVOT` can be used to modify any table
  expression.
+ A `WITH OFFSET` clause immediately preceding the `UNPIVOT` operator isn't
  allowed.
+ `PIVOT` aggregations can't be reversed with `UNPIVOT`.

Conceptual example:

```googlesql
-- Before UNPIVOT is used to rotate Q1, Q2, Q3, Q4 into sales and quarter columns:
/*---------+----+----+----+----+
 | product | Q1 | Q2 | Q3 | Q4 |
 +---------+----+----+----+----+
 | Kale    | 51 | 23 | 45 | 3  |
 | Apple   | 77 | 0  | 25 | 2  |
 +---------+----+----+----+----*/

-- After UNPIVOT is used to rotate Q1, Q2, Q3, Q4 into sales and quarter columns:
/*---------+-------+---------+
 | product | sales | quarter |
 +---------+-------+---------+
 | Kale    | 51    | Q1      |
 | Kale    | 23    | Q2      |
 | Kale    | 45    | Q3      |
 | Kale    | 3     | Q4      |
 | Apple   | 77    | Q1      |
 | Apple   | 0     | Q2      |
 | Apple   | 25    | Q3      |
 | Apple   | 2     | Q4      |
 +---------+-------+---------*/
```

**Definitions**

Top-level definitions:

+ `from_item`: The table, subquery, or
  table-valued function (TVF) on which
  to perform a pivot operation. The `from_item` must
  [follow these rules](#rules_for_unpivot_from_item).
+ `unpivot_operator`: The pivot operation to perform on a `from_item`.

`unpivot_operator` definitions:

+ `INCLUDE NULLS`: Add rows with `NULL` values to the result.
+ `EXCLUDE NULLS`: don't add rows with `NULL` values to the result.
  By default, `UNPIVOT` excludes rows with `NULL` values.
+ `single_column_unpivot`: Rotates columns into one `values_column`
  and one `name_column`.
+ `multi_column_unpivot`: Rotates columns into multiple
  `values_column`s and one `name_column`.
+ `unpivot_alias`: An alias for the results of the `UNPIVOT` operation. This
  alias can be referenced elsewhere in the query.

`single_column_unpivot` definitions:

+ `values_column`: A column to contain the row values from `columns_to_unpivot`.
  [Follow these rules](#rules_for_values_column) when creating a values column.
+ `name_column`: A column to contain the column names from `columns_to_unpivot`.
  [Follow these rules](#rules_for_name_column) when creating a name column.
+ `columns_to_unpivot`: The columns from the `from_item` to populate
  `values_column` and `name_column`.
  [Follow these rules](#rules_for_unpivot_column) when creating an unpivot
  column.
  + `row_value_alias`: An optional alias for a column that's displayed for the
    column in `name_column`. If not specified, the string value of the
    column name is used.
    [Follow these rules](#rules_for_row_value_alias) when creating a
    row value alias.

`multi_column_unpivot` definitions:

+ `values_column_set`: A set of columns to contain the row values from
  `columns_to_unpivot`. [Follow these rules](#rules_for_values_column) when
   creating a values column.
+ `name_column`: A set of columns to contain the column names from
  `columns_to_unpivot`. [Follow these rules](#rules_for_name_column) when
  creating a name column.
+ `column_sets_to_unpivot`: The columns from the `from_item` to unpivot.
  [Follow these rules](#rules_for_unpivot_column) when creating an unpivot
  column.
  + `row_value_alias`: An optional alias for a column set that's displayed
    for the column set in `name_column`. If not specified, a string value for
    the column set is used and each column in the string is separated with an
    underscore (`_`). For example, `(col1, col2)` outputs `col1_col2`.
    [Follow these rules](#rules_for_row_value_alias) when creating a
    row value alias.

**Rules**

<a id="rules_for_unpivot_from_item"></a>
Rules for a `from_item` passed to `UNPIVOT`:

+ The `from_item` may consist of any
  table, subquery, or table-valued function
  (TVF) result.
+ The `from_item` may not produce a value table.
+ Duplicate columns in a `from_item` can't be referenced in the `UNPIVOT`
  clause.

<a id="rules_for_unpivot_operator"></a>
Rules for `unpivot_operator`:

+ Expressions aren't permitted.
+ Qualified names aren't permitted. For example, `mytable.mycolumn` isn't
  allowed.
+ In the case where the `UNPIVOT` result has duplicate column names:
    + `SELECT *` is allowed.
    + `SELECT values_column` causes ambiguity.

<a id="rules_for_values_column"></a>
Rules for `values_column`:

+ It can't be a name used for a `name_column` or an `unpivot_column`.
+ It can be the same name as a column from the `from_item`.

<a id="rules_for_name_column"></a>
Rules for `name_column`:

+ It can't be a name used for a `values_column` or an `unpivot_column`.
+ It can be the same name as a column from the `from_item`.

<a id="rules_for_unpivot_column"></a>
Rules for `unpivot_column`:

+ Must be a column name from the `from_item`.
+ It can't reference duplicate `from_item` column names.
+ All columns in a column set must have equivalent data types.
  + Data types can't be coerced to a common supertype.
  + If the data types are exact matches (for example, a struct with
    different field names), the data type of the first input is
    the data type of the output.
+ You can't have the same name in the same column set. For example,
  `(emp1, emp1)` results in an error.
+ You can have a the same name in different column sets. For example,
  `(emp1, emp2), (emp1, emp3)` is valid.

<a id="rules_for_row_value_alias"></a>
Rules for `row_value_alias`:

+ This can be a string or an `INT64` literal.
+ The data type for all `row_value_alias` clauses must be the same.
+ If the value is an `INT64`, the `row_value_alias` for each `unpivot_column`
  must be specified.

**Examples**

The following examples reference a table called `Produce` that looks like this:

```googlesql
WITH Produce AS (
  SELECT 'Kale' as product, 51 as Q1, 23 as Q2, 45 as Q3, 3 as Q4 UNION ALL
  SELECT 'Apple', 77, 0, 25, 2)
SELECT * FROM Produce

/*---------+----+----+----+----+
 | product | Q1 | Q2 | Q3 | Q4 |
 +---------+----+----+----+----+
 | Kale    | 51 | 23 | 45 | 3  |
 | Apple   | 77 | 0  | 25 | 2  |
 +---------+----+----+----+----*/
```

With the `UNPIVOT` operator, the columns `Q1`, `Q2`, `Q3`, and `Q4` are
rotated. The values of these columns now populate a new column called `Sales`
and the names of these columns now populate a new column called `Quarter`.
This is a single-column unpivot operation.

```googlesql
SELECT * FROM Produce
UNPIVOT(sales FOR quarter IN (Q1, Q2, Q3, Q4))

/*---------+-------+---------+
 | product | sales | quarter |
 +---------+-------+---------+
 | Kale    | 51    | Q1      |
 | Kale    | 23    | Q2      |
 | Kale    | 45    | Q3      |
 | Kale    | 3     | Q4      |
 | Apple   | 77    | Q1      |
 | Apple   | 0     | Q2      |
 | Apple   | 25    | Q3      |
 | Apple   | 2     | Q4      |
 +---------+-------+---------*/
```

In this example, we `UNPIVOT` four quarters into two semesters.
This is a multi-column unpivot operation.

```googlesql
SELECT * FROM Produce
UNPIVOT(
  (first_half_sales, second_half_sales)
  FOR semesters
  IN ((Q1, Q2) AS 'semester_1', (Q3, Q4) AS 'semester_2'))

/*---------+------------------+-------------------+------------+
 | product | first_half_sales | second_half_sales | semesters  |
 +---------+------------------+-------------------+------------+
 | Kale    | 51               | 23                | semester_1 |
 | Kale    | 45               | 3                 | semester_2 |
 | Apple   | 77               | 0                 | semester_1 |
 | Apple   | 25               | 2                 | semester_2 |
 +---------+------------------+-------------------+------------*/
```

## `TABLESAMPLE` operator 
<a id="tablesample_operator"></a>

<pre>
tablesample_clause:
  TABLESAMPLE sample_method (sample_size percent_or_rows [ partition_by ])
  [ REPEATABLE(repeat_argument) ]
  [ WITH WEIGHT [AS alias] ]

sample_method:
  { BERNOULLI | SYSTEM | RESERVOIR }

sample_size:
  numeric_value_expression

percent_or_rows:
  { PERCENT | ROWS }

partition_by:
  PARTITION BY partition_expression [, ...]
</pre>

**Description**

You can use the `TABLESAMPLE` operator to select a random sample of a dataset.
This operator is useful when you're working with tables that have large
amounts of data and you don't need precise answers.

+  `sample_method`: When using the `TABLESAMPLE` operator, you must specify the
   sampling algorithm to use:
   + `BERNOULLI`: Each row is independently selected with the probability
     given in the `percent` clause. As a result, you get approximately
     `N * percent/100` rows.
   + `SYSTEM`: Produces a sample using an
     unspecified engine-dependent method, which may be more efficient but less
     probabilistically random than other methods.  For example, it could choose
     random disk blocks and return data from those blocks.
   + `RESERVOIR`: Takes as parameter an actual sample size
     K (expressed as a number of rows). If the input is smaller than K, it
     outputs the entire input relation. If the input is larger than K,
     reservoir sampling outputs a sample of size exactly K, where any sample of
     size K is equally likely.
+  `sample_size`: The size of the sample.
+  `percent_or_rows`: The `TABLESAMPLE` operator requires that you choose either
   `ROWS` or `PERCENT`. If you choose `PERCENT`, the value must be between
   0 and 100. If you choose `ROWS`, the value must be greater than or equal
   to 0.
+  `partition_by`: Optional. Perform [stratified sampling][stratified-sampling]
   for each distinct group identified by the `PARTITION BY` clause. That is,
   if the number of rows in a particular group is less than the specified row
   count, all rows in that group are assigned to the sample. Otherwise, it
   randomly selects the specified number of rows for each group, where for a
   particular group, every sample of that size is equally
   likely.
+  `REPEATABLE`: Optional. When it's used, repeated
   executions of the sampling operation return a result table with identical
   rows for a given repeat argument, as long as the underlying data doesn't
   change. `repeat_argument` represents a sampling seed
   and must be a positive value of type `INT64`.
+  `WITH WEIGHT`: Optional. Retrieves [scaling weight][scaling-weight]. If
   specified, the `TABLESAMPLE` operator outputs one extra column of type
   `DOUBLE` that's greater than or equal 1.0 to represent the actual scaling
   weight. If an alias isn't provided, the default name _weight_ is used.
   +  In Bernoulli sampling, the weight is `1 / provided sampling probability`.
      For example, `TABLESAMPLE BERNOULLI (1 percent)` will expose the weight
      of `1 / 0.01`.
   +  In System sampling, the weight is approximated or computed exactly in
      some engine-defined way, as long as its type and value range is
      specified.
   +  In non-stratified fixed row count sampling,
      (RESERVOIR without the PARTITION BY clause), the weight is equal to the
      total number of input rows divided by the count of sampled rows.
   +  In stratified sampling,
      (RESERVOIR with the PARTITION BY clause), the weight for rows from a
      particular group is equal to the group cardinality divided by the count
      of sampled rows for that group.

**Examples**

The following examples illustrate the use of the `TABLESAMPLE` operator.

Select from a table using the `RESERVOIR` sampling method:

```googlesql
SELECT MessageId
FROM Messages TABLESAMPLE RESERVOIR (100 ROWS);
```

Select from a table using the `BERNOULLI` sampling method:

```googlesql
SELECT MessageId
FROM Messages TABLESAMPLE BERNOULLI (0.1 PERCENT);
```

Use `TABLESAMPLE` with a repeat argument:

```googlesql
SELECT MessageId
FROM Messages TABLESAMPLE RESERVOIR (100 ROWS) REPEATABLE(10);
```

Use `TABLESAMPLE` with a subquery:

```googlesql
SELECT Subject FROM
(SELECT MessageId, Subject FROM Messages WHERE ServerId="test")
TABLESAMPLE BERNOULLI(50 PERCENT)
WHERE MessageId > 3;
```

Use a `TABLESAMPLE` operation with a join to another table.

```googlesql
SELECT S.Subject
FROM
(SELECT MessageId, ThreadId FROM Messages WHERE ServerId="test") AS R
TABLESAMPLE RESERVOIR(5 ROWS),
Threads AS S
WHERE S.ServerId="test" AND R.ThreadId = S.ThreadId;
```

Group results by country, using stratified sampling:

```googlesql
SELECT country, SUM(click_cost) FROM ClickEvents
 TABLESAMPLE RESERVOIR (100 ROWS PARTITION BY country)
 GROUP BY country;
```

Add scaling weight to stratified sampling:

```googlesql
SELECT country, SUM(click_cost * sampling_weight) FROM ClickEvents
 TABLESAMPLE RESERVOIR (100 ROWS PARTITION BY country)
 WITH WEIGHT AS sampling_weight
 GROUP BY country;
```

This is equivalent to the previous example. Note that you don't have to use
an alias after `WITH WEIGHT`. If you don't, the default alias `weight` is used.

```googlesql
SELECT country, SUM(click_cost * weight) FROM ClickEvents
 TABLESAMPLE RESERVOIR (100 ROWS PARTITION BY country)
 WITH WEIGHT
 GROUP BY country;
```

### Stratified sampling 
<a id="stratified_sampling"></a>

If you want better quality generated samples for under-represented groups,
you can use stratified sampling. Stratified sampling helps you
avoid samples with missing groups. To allow stratified sampling per
distinct group, use `PARTITION BY` with `RESERVOIR` in the `TABLESAMPLE` clause.

Stratified sampling performs `RESERVOIR` sampling for each distinct group
identified by the `PARTITION BY` clause. If the number of rows in a particular
group is less than the specified row count, all rows in that group are assigned
to the sample. Otherwise, it randomly selects the specified number of rows for
each group, where for a particular group, every sample of that size is equally
likely.

**Example**

Let’s consider a table named `ClickEvents` representing a stream of
click events, each of which has two fields: `country` and `click_cost`.
`country` represents the country from which the click was originated
and `click_cost` represents how much the click costs. In this example,
100 rows are randomly selected for each country.

```googlesql
SELECT click_cost, country FROM ClickEvents
TABLESAMPLE RESERVOIR (100 ROWS PARTITION BY country)
```

### Scaling weight 
<a id="scaling_weight"></a>

With scaling weight, you can perform fast and reasonable population estimates
from generated samples or estimate the aggregate results from samples. You can
capture scaling weight for a tablesample with the `WITH WEIGHT` clause.

Scaling weight represents the reciprocal of the actual, observed sampling
rate for a tablesample, making it easier to estimate aggregate results for
samples. The exposition of scaling weight generally applies to all variations
of `TABLESAMPLE`, including stratified Reservoir, non-stratified Reservoir,
Bernoulli, and System.

Let’s consider a table named `ClickEvents` representing a stream of
click events, each of which has two fields: `country` and `click_cost`.
`country` represents the country from which the click was originated
and `click_cost` represents how much the click costs. To calculate the
total click cost per country, you can use the following query:

```googlesql
SELECT country, SUM(click_cost)
FROM ClickEvents
GROUP BY country;
```

You can leverage the existing uniform sampling with fixed probability, using
Bernoulli sampling and run this query to estimate the result of the previous
query:

```googlesql
SELECT country, SUM(click_cost * weight)
FROM ClickEvents TABLESAMPLE BERNOULLI (1 PERCENT)
WITH WEIGHT
GROUP BY country;
```

You can break the second query into two steps:

1. Materialize a sample for reuse.
1. Perform aggregate estimates of the materialized sample.

Instead of aggregating the entire table, you use a 1% uniform sample to
aggregate a fraction of the original table and to compute the total click cost.
Because only 1% of the rows flow into the aggregation operator, you need to
scale the aggregate with a certain weight. Specifically, we multiply the
aggregate with 100, the reciprocal of the provided sampling probability, for
each group. And because we use uniform sampling, the scaling weight for each
group is effectively equal to the scaling weight for each row of the table,
which is 100.

Even though this sample provides a statistically accurate representation
of the original table, it might miss an entire group of rows, such as countries
in the running example, with small cardinality. For example, suppose that
the `ClickEvents` table contains 10000 rows, with 9990 rows of value `US`
and 10 rows of value `VN`. The number of distinct countries in this example
is two. With 1% uniform sampling, it's statistically probable that all the
sampled rows are from the `US` and none of them are from the `VN` partition.
As a result, the output of the second query doesn't contain the `SUM` estimate
for the group `VN`. We refer to this as the _missing-group problem_, which
can be solved with [stratified sampling][stratified-sampling].

## `GRAPH_TABLE` operator 
<a id="graph_table_operator_redirect"></a>

To learn more about this operator, see
[`GRAPH_TABLE` operator][graph-table-operator] in the
Graph Query Language (GQL) reference guide.

## Join operation 
<a id="join_types"></a>

<pre>
<span class="var">join_operation</span>:
  { <span class="var">cross_join_operation</span> | <span class="var">condition_join_operation</span> }

<span class="var">cross_join_operation</span>:
  <span class="var"><a href="#from_clause">from_item</a></span> <span class="var">cross_join_operator</span> [ <a href="#lateral_join">LATERAL</a> ] <span class="var"><a href="#from_clause">from_item</a></span>

<span class="var">condition_join_operation</span>:
  <span class="var"><a href="#from_clause">from_item</a></span> <span class="var">condition_join_operator</span> [ <a href="#lateral_join">LATERAL</a> ] <span class="var"><a href="#from_clause">from_item</a></span> <span class="var">join_condition</span>

<span class="var">cross_join_operator</span>:
  { <a href="#cross_join">CROSS JOIN</a> | <a href="#comma_cross_join">,</a> }

<span class="var">condition_join_operator</span>:
  {
    <a href="#inner_join">[INNER] JOIN</a>
    | <a href="#full_join">FULL [OUTER] JOIN</a>
    | <a href="#left_join">LEFT [OUTER] JOIN</a>
    | <a href="#right_join">RIGHT [OUTER] JOIN</a>
  }

<a href="#join_conditions"><span class="var">join_condition</span></a>:
  { <a href="#on_clause"><span class="var">on_clause</span></a> | <a href="#using_clause"><span class="var">using_clause</span></a> }

<a href="#on_clause"><span class="var">on_clause</span></a>:
  ON <span class="var">bool_expression</span>

<a href="#using_clause"><span class="var">using_clause</span></a>:
  USING ( <span class="var">column_list</span> )
</pre>

The `JOIN` operation merges two `from_item`s so that the `SELECT` clause can
query them as one source. The join operator and join condition specify how to
combine and discard rows from the two `from_item`s to form a single source.

### `[INNER] JOIN` 
<a id="inner_join"></a>

An `INNER JOIN`, or simply `JOIN`, effectively calculates the Cartesian product
of the two `from_item`s and discards all rows that don't meet the join
condition. _Effectively_ means that it's possible to implement an `INNER JOIN`
without actually calculating the Cartesian product.

```googlesql
FROM A INNER JOIN B ON A.w = B.y

/*
Table A       Table B       Result
+-------+     +-------+     +---------------+
| w | x |  *  | y | z |  =  | w | x | y | z |
+-------+     +-------+     +---------------+
| 1 | a |     | 2 | k |     | 2 | b | 2 | k |
| 2 | b |     | 3 | m |     | 3 | c | 3 | m |
| 3 | c |     | 3 | n |     | 3 | c | 3 | n |
| 3 | d |     | 4 | p |     | 3 | d | 3 | m |
+-------+     +-------+     | 3 | d | 3 | n |
                            +---------------+
*/
```

```googlesql
FROM A INNER JOIN B USING (x)

/*
Table A       Table B       Result
+-------+     +-------+     +-----------+
| x | y |  *  | x | z |  =  | x | y | z |
+-------+     +-------+     +-----------+
| 1 | a |     | 2 | k |     | 2 | b | k |
| 2 | b |     | 3 | m |     | 3 | c | m |
| 3 | c |     | 3 | n |     | 3 | c | n |
| 3 | d |     | 4 | p |     | 3 | d | m |
+-------+     +-------+     | 3 | d | n |
                            +-----------+
*/
```

**Example**

This query performs an `INNER JOIN` on the [`Roster`][roster-table]
and [`TeamMascot`][teammascot-table] tables.

```googlesql
SELECT Roster.LastName, TeamMascot.Mascot
FROM Roster JOIN TeamMascot ON Roster.SchoolID = TeamMascot.SchoolID;

/*---------------------------+
 | LastName   | Mascot       |
 +---------------------------+
 | Adams      | Jaguars      |
 | Buchanan   | Lakers       |
 | Coolidge   | Lakers       |
 | Davis      | Knights      |
 +---------------------------*/
```

You can use a [correlated][correlated-join] `INNER JOIN` to flatten an array
into a set of rows. To learn more, see
[Convert elements in an array to rows in a table][flattening-arrays].

### `CROSS JOIN`

`CROSS JOIN` returns the Cartesian product of the two `from_item`s. In other
words, it combines each row from the first `from_item` with each row from the
second `from_item`.

If the rows of the two `from_item`s are independent, then the result has
_M * N_ rows, given _M_ rows in one `from_item` and _N_ in the other. Note that
this still holds for the case when either `from_item` has zero rows.

In a `FROM` clause, a `CROSS JOIN` can be written like this:

```googlesql
FROM A CROSS JOIN B

/*
Table A       Table B       Result
+-------+     +-------+     +---------------+
| w | x |  *  | y | z |  =  | w | x | y | z |
+-------+     +-------+     +---------------+
| 1 | a |     | 2 | c |     | 1 | a | 2 | c |
| 2 | b |     | 3 | d |     | 1 | a | 3 | d |
+-------+     +-------+     | 2 | b | 2 | c |
                            | 2 | b | 3 | d |
                            +---------------+
*/
```

You can use a [correlated][correlated-join] cross join to convert or
flatten an array into a set of rows, though the (equivalent) `INNER JOIN` is
preferred over `CROSS JOIN` for this case. To learn more, see
[Convert elements in an array to rows in a table][flattening-arrays].

**Examples**

This query performs an `CROSS JOIN` on the [`Roster`][roster-table]
and [`TeamMascot`][teammascot-table] tables.

```googlesql
SELECT Roster.LastName, TeamMascot.Mascot
FROM Roster CROSS JOIN TeamMascot;

/*---------------------------+
 | LastName   | Mascot       |
 +---------------------------+
 | Adams      | Jaguars      |
 | Adams      | Knights      |
 | Adams      | Lakers       |
 | Adams      | Mustangs     |
 | Buchanan   | Jaguars      |
 | Buchanan   | Knights      |
 | Buchanan   | Lakers       |
 | Buchanan   | Mustangs     |
 | ...                       |
 +---------------------------*/
```

### Comma cross join (,) 
<a id="comma_cross_join"></a>

[`CROSS JOIN`][cross-join]s can be written implicitly with a comma. This is
called a comma cross join.

A comma cross join looks like this in a `FROM` clause:

```googlesql
FROM A, B

/*
Table A       Table B       Result
+-------+     +-------+     +---------------+
| w | x |  *  | y | z |  =  | w | x | y | z |
+-------+     +-------+     +---------------+
| 1 | a |     | 2 | c |     | 1 | a | 2 | c |
| 2 | b |     | 3 | d |     | 1 | a | 3 | d |
+-------+     +-------+     | 2 | b | 2 | c |
                            | 2 | b | 3 | d |
                            +---------------+
*/
```

You can't write comma cross joins inside parentheses. To learn more, see
[Join operations in a sequence][sequences-of-joins].

```googlesql {.bad}
FROM (A, B)  // INVALID
```

You can use a [correlated][correlated-join] comma cross join to convert or
flatten an array into a set of rows. To learn more, see
[Convert elements in an array to rows in a table][flattening-arrays].

**Examples**

This query performs a comma cross join on the [`Roster`][roster-table]
and [`TeamMascot`][teammascot-table] tables.

```googlesql
SELECT Roster.LastName, TeamMascot.Mascot
FROM Roster, TeamMascot;

/*---------------------------+
 | LastName   | Mascot       |
 +---------------------------+
 | Adams      | Jaguars      |
 | Adams      | Knights      |
 | Adams      | Lakers       |
 | Adams      | Mustangs     |
 | Buchanan   | Jaguars      |
 | Buchanan   | Knights      |
 | Buchanan   | Lakers       |
 | Buchanan   | Mustangs     |
 | ...                       |
 +---------------------------*/
```

### `FULL [OUTER] JOIN` 
<a id="full_join"></a>

A `FULL OUTER JOIN` (or simply `FULL JOIN`) returns all fields for all matching
rows in both `from_items` that meet the join condition. If a given row from one
`from_item` doesn't join to any row in the other `from_item`, the row returns
with `NULL` values for all columns from the other `from_item`.

```googlesql
FROM A FULL OUTER JOIN B ON A.w = B.y

/*
Table A       Table B       Result
+-------+     +-------+     +---------------------------+
| w | x |  *  | y | z |  =  | w    | x    | y    | z    |
+-------+     +-------+     +---------------------------+
| 1 | a |     | 2 | k |     | 1    | a    | NULL | NULL |
| 2 | b |     | 3 | m |     | 2    | b    | 2    | k    |
| 3 | c |     | 3 | n |     | 3    | c    | 3    | m    |
| 3 | d |     | 4 | p |     | 3    | c    | 3    | n    |
+-------+     +-------+     | 3    | d    | 3    | m    |
                            | 3    | d    | 3    | n    |
                            | NULL | NULL | 4    | p    |
                            +---------------------------+
*/
```

```googlesql
FROM A FULL OUTER JOIN B USING (x)

/*
Table A       Table B       Result
+-------+     +-------+     +--------------------+
| x | y |  *  | x | z |  =  | x    | y    | z    |
+-------+     +-------+     +--------------------+
| 1 | a |     | 2 | k |     | 1    | a    | NULL |
| 2 | b |     | 3 | m |     | 2    | b    | k    |
| 3 | c |     | 3 | n |     | 3    | c    | m    |
| 3 | d |     | 4 | p |     | 3    | c    | n    |
+-------+     +-------+     | 3    | d    | m    |
                            | 3    | d    | n    |
                            | 4    | NULL | p    |
                            +--------------------+
*/
```

**Example**

This query performs a `FULL JOIN` on the [`Roster`][roster-table]
and [`TeamMascot`][teammascot-table] tables.

```googlesql
SELECT Roster.LastName, TeamMascot.Mascot
FROM Roster FULL JOIN TeamMascot ON Roster.SchoolID = TeamMascot.SchoolID;

/*---------------------------+
 | LastName   | Mascot       |
 +---------------------------+
 | Adams      | Jaguars      |
 | Buchanan   | Lakers       |
 | Coolidge   | Lakers       |
 | Davis      | Knights      |
 | Eisenhower | NULL         |
 | NULL       | Mustangs     |
 +---------------------------*/
```

### `LEFT [OUTER] JOIN` 
<a id="left_join"></a>

The result of a `LEFT OUTER JOIN` (or simply `LEFT JOIN`) for two
`from_item`s always retains all rows of the left `from_item` in the
`JOIN` operation, even if no rows in the right `from_item` satisfy the join
predicate.

All rows from the _left_ `from_item` are retained;
if a given row from the left `from_item` doesn't join to any row
in the _right_ `from_item`, the row will return with `NULL` values for all
columns exclusively from the right `from_item`. Rows from the right
`from_item` that don't join to any row in the left `from_item` are discarded.

```googlesql
FROM A LEFT OUTER JOIN B ON A.w = B.y

/*
Table A       Table B       Result
+-------+     +-------+     +---------------------------+
| w | x |  *  | y | z |  =  | w    | x    | y    | z    |
+-------+     +-------+     +---------------------------+
| 1 | a |     | 2 | k |     | 1    | a    | NULL | NULL |
| 2 | b |     | 3 | m |     | 2    | b    | 2    | k    |
| 3 | c |     | 3 | n |     | 3    | c    | 3    | m    |
| 3 | d |     | 4 | p |     | 3    | c    | 3    | n    |
+-------+     +-------+     | 3    | d    | 3    | m    |
                            | 3    | d    | 3    | n    |
                            +---------------------------+
*/
```

```googlesql
FROM A LEFT OUTER JOIN B USING (x)

/*
Table A       Table B       Result
+-------+     +-------+     +--------------------+
| x | y |  *  | x | z |  =  | x    | y    | z    |
+-------+     +-------+     +--------------------+
| 1 | a |     | 2 | k |     | 1    | a    | NULL |
| 2 | b |     | 3 | m |     | 2    | b    | k    |
| 3 | c |     | 3 | n |     | 3    | c    | m    |
| 3 | d |     | 4 | p |     | 3    | c    | n    |
+-------+     +-------+     | 3    | d    | m    |
                            | 3    | d    | n    |
                            +--------------------+
*/
```

**Example**

This query performs a `LEFT JOIN` on the [`Roster`][roster-table]
and [`TeamMascot`][teammascot-table] tables.

```googlesql
SELECT Roster.LastName, TeamMascot.Mascot
FROM Roster LEFT JOIN TeamMascot ON Roster.SchoolID = TeamMascot.SchoolID;

/*---------------------------+
 | LastName   | Mascot       |
 +---------------------------+
 | Adams      | Jaguars      |
 | Buchanan   | Lakers       |
 | Coolidge   | Lakers       |
 | Davis      | Knights      |
 | Eisenhower | NULL         |
 +---------------------------*/
```

### `RIGHT [OUTER] JOIN` 
<a id="right_join"></a>

The result of a `RIGHT OUTER JOIN` (or simply `RIGHT JOIN`) for two
`from_item`s always retains all rows of the right `from_item` in the
`JOIN` operation, even if no rows in the left `from_item` satisfy the join
predicate.

All rows from the _right_ `from_item` are returned;
if a given row from the right `from_item` doesn't join to any row
in the _left_ `from_item`, the row will return with `NULL` values for all
columns exclusively from the left `from_item`. Rows from the left `from_item`
that don't join to any row in the right `from_item` are discarded.

```googlesql
FROM A RIGHT OUTER JOIN B ON A.w = B.y

/*
Table A       Table B       Result
+-------+     +-------+     +---------------------------+
| w | x |  *  | y | z |  =  | w    | x    | y    | z    |
+-------+     +-------+     +---------------------------+
| 1 | a |     | 2 | k |     | 2    | b    | 2    | k    |
| 2 | b |     | 3 | m |     | 3    | c    | 3    | m    |
| 3 | c |     | 3 | n |     | 3    | c    | 3    | n    |
| 3 | d |     | 4 | p |     | 3    | d    | 3    | m    |
+-------+     +-------+     | 3    | d    | 3    | n    |
                            | NULL | NULL | 4    | p    |
                            +---------------------------+
*/
```

```googlesql
FROM A RIGHT OUTER JOIN B USING (x)

/*
Table A       Table B       Result
+-------+     +-------+     +--------------------+
| x | y |  *  | x | z |  =  | x    | y    | z    |
+-------+     +-------+     +--------------------+
| 1 | a |     | 2 | k |     | 2    | b    | k    |
| 2 | b |     | 3 | m |     | 3    | c    | m    |
| 3 | c |     | 3 | n |     | 3    | c    | n    |
| 3 | d |     | 4 | p |     | 3    | d    | m    |
+-------+     +-------+     | 3    | d    | n    |
                            | 4    | NULL | p    |
                            +--------------------+
*/
```

**Example**

This query performs a `RIGHT JOIN` on the [`Roster`][roster-table]
and [`TeamMascot`][teammascot-table] tables.

```googlesql
SELECT Roster.LastName, TeamMascot.Mascot
FROM Roster RIGHT JOIN TeamMascot ON Roster.SchoolID = TeamMascot.SchoolID;

/*---------------------------+
 | LastName   | Mascot       |
 +---------------------------+
 | Adams      | Jaguars      |
 | Buchanan   | Lakers       |
 | Coolidge   | Lakers       |
 | Davis      | Knights      |
 | NULL       | Mustangs     |
 +---------------------------*/
```

### `LATERAL` join 
<a id="lateral_join"></a>

<pre>
<a href="#from_clause"><span class="var">from_item</span></a> { <a href="#cross_join">CROSS JOIN</a> | <a href="#inner_join">[INNER] JOIN</a> | <a href="#left_join">LEFT [OUTER] JOIN</a> } LATERAL <a href="#from_clause"><span class="var">from_item</span></a> [ <a href="#join_conditions"><span class="var">join_condition</span></a> ]
<a href="#from_clause"><span class="var">from_item</span></a> , LATERAL <a href="#from_clause"><span class="var">from_item</span></a>
</pre>

A `LATERAL` join enables a right `from_item`
(typically a subquery, an
[`UNNEST` operator][unnest-operator] operation, or a
[table-valued function (TVF)][tvf-concepts])
to reference columns from a left `from_item` that precedes
it in the `FROM` clause. The right `from_item` is evaluated for each row of the
left `from_item`.

**Key Characteristics:**

*   **Correlation**: The primary purpose of `LATERAL` is to enable correlated
    subqueries in the `FROM` clause. The subquery or TVF on the right side of
    the `LATERAL` join can depend on values from the current row of the table on
    its left.
*   **Row-wise evaluation**: The right side is logically re-evaluated for each
    row of the left side. Note that re-evaluation is not guaranteed. For
    example, when multiple rows from the left input provide identical values for
    the columns referenced by the right input, engines are free to choose
    whether to re-evaluate the computed right side or reuse the same computed
    relation. In other words, the computed right input is not guaranteed to
    reuse or regenerate volatile expressions such as RAND().
*   **Join types**: You can use `LATERAL` with `INNER JOIN`, `LEFT OUTER JOIN`,
    and `CROSS JOIN` (often implied by a comma). `LATERAL` is **not** allowed
    with `RIGHT OUTER JOIN` nor `FULL OUTER JOIN`.

**Behavior with join types:**

*   `CROSS JOIN LATERAL` (or with comma: `, LATERAL`): If the lateral
    subquery or TVF produces no rows for a given row from the left input, that
    row is excluded from the final result.
*   `INNER JOIN LATERAL`: Similar to `CROSS JOIN`, but applies the condition in
    the `ON` clause as a filter on the `LATERAL` join.
*   `LEFT [OUTER] JOIN LATERAL`: If the lateral subquery/TVF produces no rows
    for a given row, the row is included in the result, with `NULL`s for columns
    originating from the lateral subquery/TVF. `LATERAL` allows `LEFT JOIN` to
    omit the `ON` clause (which is equivalent to `LEFT JOIN LATERAL ... ON
    true`)

**Example**

These examples include statements which perform queries on the
[`Roster`][roster-table] and [`TeamMascot`][teammascot-table], and
[`PlayerStats`][playerstats-table] tables.

The first query aims to find, for each school, the opponent player who scored
the highest points against this school.

```googlesql

SELECT R.SchoolID, OP.LastName AS TopOpPlayer, OP.PointsScored
FROM Roster AS R,
     LATERAL (
      SELECT PS.LastName, PS.PointsScored
      FROM PlayerStats AS PS
      WHERE PS.OpponentID = R.SchoolID
      ORDER BY PointsScored DESC
      LIMIT 1
     ) AS OP
ORDER BY R.SchoolID

/*
Result (using implicit CROSS JOIN with LATERAL):
+----------+---------------+--------------+
| SchoolID | TopOpPlayer   | PointsScored |
+----------+---------------+--------------+
| 50       | Buchanan      | 13           |
| 51       | Adams         | 3            |
| 52       | Adams         | 4            |
| 57       | Coolidge      | 1            |
+----------+---------------+--------------+
*/
```

Using `LEFT JOIN LATERAL`:

```googlesql

SELECT R.LastName, R.SchoolID, M.Mascot FROM Roster AS R LEFT JOIN LATERAL (
SELECT Mascot FROM TeamMascot m WHERE m.SchoolID = R.SchoolI ) AS M ORDER BY
R.LastName;

/* SchoolID 77 has no mascot listed in the TeamMascot table. Because the join is
`LEFT OUTER`, players from schoolID 77 still shows up in the output, with `NULL`
padding.

Result:
+------------+----------+---------+
| LastName   | SchoolID | Mascot  |
+------------+----------+---------+
| Adams      | 50       | Jaguars |
| Buchanan   | 52       | Lakers  |
| Coolidge   | 52       | Lakers  |
| Davis      | 51       | Knights |
| Eisenhower | 77       | NULL    |
+------------+----------+---------+
*/
```

**Restrictions and notes:**

*   The `LATERAL` keyword is necessary to enable the correlation for the
    subquery or TVF on the right.
*   The right side of `LATERAL` is typically a subquery or a TVF call. It can
    also be an [`UNNEST` operator][unnest-operator] referencing columns from the
    left side.
*   Ensure that the correlated columns are correctly scoped and available from
    the left `from_item`.
*   `LATERAL` cannot be used on the first or leftmost item in a join or a
    parenthesized join.
*   `LATERAL` cannot be used with RIGHT or FULL join.
*   The `LATERAL` input on the right side can't be followed by a postfix
    operator (`TABLESAMPLE`, `PIVOT`, etc.)

### Join conditions 
<a id="join_conditions"></a>

In a [join operation][query-joins], a join condition helps specify how to
combine rows in two `from_items` to form a single source.

The two types of join conditions are the [`ON` clause][on-clause] and
[`USING` clause][using-clause]. You must use a join condition when you perform a
conditional join operation. You can't use a join condition when you perform a
cross join operation.

#### `ON` clause 
<a id="on_clause"></a>

```googlesql
ON bool_expression
```

**Description**

Given a row from each table, if the `ON` clause evaluates to `TRUE`, the query
generates a consolidated row with the result of combining the given rows.

Definitions:

+ `bool_expression`: The boolean expression that specifies the condition for
  the join. This is frequently a [comparison operation][comparison-operators] or
  logical combination of comparison operators.

Details:

Similarly to `CROSS JOIN`, `ON` produces a column once for each column in each
input table.

A `NULL` join condition evaluation is equivalent to a `FALSE` evaluation.

If a column-order sensitive operation such as `UNION` or `SELECT *` is used with
the `ON` join condition, the resulting table contains all of the columns from
the left input in order, and then all of the columns from the right input in
order.

**Examples**

The following examples show how to use the `ON` clause:

```googlesql
WITH
  A AS ( SELECT 1 as x UNION ALL SELECT 2 UNION ALL SELECT 3),
  B AS ( SELECT 2 as x UNION ALL SELECT 3 UNION ALL SELECT 4)
SELECT * FROM A INNER JOIN B ON A.x = B.x;

WITH
  A AS ( SELECT 1 as x UNION ALL SELECT 2 UNION ALL SELECT 3),
  B AS ( SELECT 2 as x UNION ALL SELECT 3 UNION ALL SELECT 4)
SELECT A.x, B.x FROM A INNER JOIN B ON A.x = B.x;

/*
Table A   Table B   Result (A.x, B.x)
+---+     +---+     +-------+
| x |  *  | x |  =  | x | x |
+---+     +---+     +-------+
| 1 |     | 2 |     | 2 | 2 |
| 2 |     | 3 |     | 3 | 3 |
| 3 |     | 4 |     +-------+
+---+     +---+
*/
```

```googlesql
WITH
  A AS ( SELECT 1 as x UNION ALL SELECT 2 UNION ALL SELECT 3 UNION ALL SELECT NULL),
  B AS ( SELECT 2 as x UNION ALL SELECT 3 UNION ALL SELECT 4 UNION ALL SELECT 5)
SELECT * FROM A LEFT OUTER JOIN B ON A.x = B.x;

WITH
  A AS ( SELECT 1 as x UNION ALL SELECT 2 UNION ALL SELECT 3 UNION ALL SELECT NULL),
  B AS ( SELECT 2 as x UNION ALL SELECT 3 UNION ALL SELECT 4 UNION ALL SELECT 5)
SELECT A.x, B.x FROM A LEFT OUTER JOIN B ON A.x = B.x;

/*
Table A    Table B   Result
+------+   +---+     +-------------+
| x    | * | x |  =  | x    | x    |
+------+   +---+     +-------------+
| 1    |   | 2 |     | 1    | NULL |
| 2    |   | 3 |     | 2    | 2    |
| 3    |   | 4 |     | 3    | 3    |
| NULL |   | 5 |     | NULL | NULL |
+------+   +---+     +-------------+
*/
```

```googlesql
WITH
  A AS ( SELECT 1 as x UNION ALL SELECT 2 UNION ALL SELECT 3 UNION ALL SELECT NULL),
  B AS ( SELECT 2 as x UNION ALL SELECT 3 UNION ALL SELECT 4 UNION ALL SELECT 5)
SELECT * FROM A FULL OUTER JOIN B ON A.x = B.x;

WITH
  A AS ( SELECT 1 as x UNION ALL SELECT 2 UNION ALL SELECT 3 UNION ALL SELECT NULL),
  B AS ( SELECT 2 as x UNION ALL SELECT 3 UNION ALL SELECT 4 UNION ALL SELECT 5)
SELECT A.x, B.x FROM A FULL OUTER JOIN B ON A.x = B.x;

/*
Table A    Table B   Result
+------+   +---+     +-------------+
| x    | * | x |  =  | x    | x    |
+------+   +---+     +-------------+
| 1    |   | 2 |     | 1    | NULL |
| 2    |   | 3 |     | 2    | 2    |
| 3    |   | 4 |     | 3    | 3    |
| NULL |   | 5 |     | NULL | NULL |
+------+   +---+     | NULL | 4    |
                     | NULL | 5    |
                     +-------------+
*/
```

#### `USING` clause 
<a id="using_clause"></a>

```googlesql
USING ( column_name_list )

column_name_list:
    column_name[, ...]
```

**Description**

When you are joining two tables, `USING` performs an
[equality comparison operation][comparison-operators] on the columns named in
`column_name_list`. Each column name in `column_name_list` must appear in both
input tables. For each pair of rows from the input tables, if the
equality comparisons all evaluate to `TRUE`, one row is added to the resulting
column.

Definitions:

+ `column_name_list`: A list of columns to include in the join condition.
+ `column_name`: The column that exists in both of the tables that you are
  joining.

Details:

A `NULL` join condition evaluation is equivalent to a `FALSE` evaluation.

If a column-order sensitive operation such as `UNION` or `SELECT *` is used
with the `USING` join condition, the resulting table contains columns in this
order:

+   The columns from `column_name_list` in the order they appear in the `USING`
    clause.
+   All other columns of the left input in the order they appear in the input.
+   All other columns of the right input in the order they appear in the input.

A column name in the `USING` clause must not be qualified by a
table name.

If the join is an `INNER JOIN` or a `LEFT OUTER JOIN`, the output
columns are populated from the values in the first table. If the
join is a `RIGHT OUTER JOIN`, the output columns are populated from the values
in the second table. If the join is a `FULL OUTER JOIN`, the output columns
are populated by [coalescing][coalesce] the values from the left and right
tables in that order.

**Examples**

The following example shows how to use the `USING` clause with one
column name in the column name list:

```googlesql
WITH
  A AS ( SELECT 1 as x UNION ALL SELECT 2 UNION ALL SELECT 9 UNION ALL SELECT NULL),
  B AS ( SELECT 2 as x UNION ALL SELECT 9 UNION ALL SELECT 9 UNION ALL SELECT 5)
SELECT * FROM A INNER JOIN B USING (x);

/*
Table A    Table B   Result
+------+   +---+     +---+
| x    | * | x |  =  | x |
+------+   +---+     +---+
| 1    |   | 2 |     | 2 |
| 2    |   | 9 |     | 9 |
| 9    |   | 9 |     | 9 |
| NULL |   | 5 |     +---+
+------+   +---+
*/
```

The following example shows how to use the `USING` clause with
multiple column names in the column name list:

```googlesql
WITH
  A AS (
    SELECT 1 as x, 15 as y UNION ALL
    SELECT 2, 10 UNION ALL
    SELECT 9, 16 UNION ALL
    SELECT NULL, 12),
  B AS (
    SELECT 2 as x, 10 as y UNION ALL
    SELECT 9, 17 UNION ALL
    SELECT 9, 16 UNION ALL
    SELECT 5, 15)
SELECT * FROM A INNER JOIN B USING (x, y);

/*
Table A         Table B        Result
+-----------+   +---------+     +---------+
| x    | y  | * | x  | y  |  =  | x  | y  |
+-----------+   +---------+     +---------+
| 1    | 15 |   | 2  | 10 |     | 2  | 10 |
| 2    | 10 |   | 9  | 17 |     | 9  | 16 |
| 9    | 16 |   | 9  | 16 |     +---------+
| NULL | 12 |   | 5  | 15 |
+-----------+   +---------+
*/
```

The following examples show additional ways in which to use the `USING` clause
with one column name in the column name list:

```googlesql
WITH
  A AS ( SELECT 1 as x UNION ALL SELECT 2 UNION ALL SELECT 9 UNION ALL SELECT NULL),
  B AS ( SELECT 2 as x UNION ALL SELECT 9 UNION ALL SELECT 9 UNION ALL SELECT 5)
SELECT x, A.x, B.x FROM A INNER JOIN B USING (x)

/*
Table A    Table B   Result
+------+   +---+     +--------------------+
| x    | * | x |  =  | x    | A.x  | B.x  |
+------+   +---+     +--------------------+
| 1    |   | 2 |     | 2    | 2    | 2    |
| 2    |   | 9 |     | 9    | 9    | 9    |
| 9    |   | 9 |     | 9    | 9    | 9    |
| NULL |   | 5 |     +--------------------+
+------+   +---+
*/
```

```googlesql
WITH
  A AS ( SELECT 1 as x UNION ALL SELECT 2 UNION ALL SELECT 9 UNION ALL SELECT NULL),
  B AS ( SELECT 2 as x UNION ALL SELECT 9 UNION ALL SELECT 9 UNION ALL SELECT 5)
SELECT x, A.x, B.x FROM A LEFT OUTER JOIN B USING (x)

/*
Table A    Table B   Result
+------+   +---+     +--------------------+
| x    | * | x |  =  | x    | A.x  | B.x  |
+------+   +---+     +--------------------+
| 1    |   | 2 |     | 1    | 1    | NULL |
| 2    |   | 9 |     | 2    | 2    | 2    |
| 9    |   | 9 |     | 9    | 9    | 9    |
| NULL |   | 5 |     | 9    | 9    | 9    |
+------+   +---+     | NULL | NULL | NULL |
                     +--------------------+
*/
```

```googlesql
WITH
  A AS ( SELECT 1 as x UNION ALL SELECT 2 UNION ALL SELECT 2 UNION ALL SELECT NULL),
  B AS ( SELECT 2 as x UNION ALL SELECT 9 UNION ALL SELECT 9 UNION ALL SELECT 5)
SELECT x, A.x, B.x FROM A RIGHT OUTER JOIN B USING (x)

/*
Table A    Table B   Result
+------+   +---+     +--------------------+
| x    | * | x |  =  | x    | A.x  | B.x  |
+------+   +---+     +--------------------+
| 1    |   | 2 |     | 2    | 2    | 2    |
| 2    |   | 9 |     | 2    | 2    | 2    |
| 2    |   | 9 |     | 9    | NULL | 9    |
| NULL |   | 5 |     | 9    | NULL | 9    |
+------+   +---+     | 5    | NULL | 5    |
                     +--------------------+
*/
```

```googlesql
WITH
  A AS ( SELECT 1 as x UNION ALL SELECT 2 UNION ALL SELECT 2 UNION ALL SELECT NULL),
  B AS ( SELECT 2 as x UNION ALL SELECT 9 UNION ALL SELECT 9 UNION ALL SELECT 5)
SELECT x, A.x, B.x FROM A FULL OUTER JOIN B USING (x);

/*
Table A    Table B   Result
+------+   +---+     +--------------------+
| x    | * | x |  =  | x    | A.x  | B.x  |
+------+   +---+     +--------------------+
| 1    |   | 2 |     | 1    | 1    | NULL |
| 2    |   | 9 |     | 2    | 2    | 2    |
| 2    |   | 9 |     | 2    | 2    | 2    |
| NULL |   | 5 |     | NULL | NULL | NULL |
+------+   +---+     | 9    | NULL | 9    |
                     | 9    | NULL | 9    |
                     | 5    | NULL | 5    |
                     +--------------------+
*/
```

The following example shows how to use the `USING` clause with
only some column names in the column name list.

```googlesql
WITH
  A AS (
    SELECT 1 as x, 15 as y UNION ALL
    SELECT 2, 10 UNION ALL
    SELECT 9, 16 UNION ALL
    SELECT NULL, 12),
  B AS (
    SELECT 2 as x, 10 as y UNION ALL
    SELECT 9, 17 UNION ALL
    SELECT 9, 16 UNION ALL
    SELECT 5, 15)
SELECT * FROM A INNER JOIN B USING (x);

/*
Table A         Table B         Result
+-----------+   +---------+     +-----------------+
| x    | y  | * | x  | y  |  =  | x   | A.y | B.y |
+-----------+   +---------+     +-----------------+
| 1    | 15 |   | 2  | 10 |     | 2   | 10  | 10  |
| 2    | 10 |   | 9  | 17 |     | 9   | 16  | 17  |
| 9    | 16 |   | 9  | 16 |     | 9   | 16  | 16  |
| NULL | 12 |   | 5  | 15 |     +-----------------+
+-----------+   +---------+
*/
```

The following query performs an `INNER JOIN` on the
[`Roster`][roster-table] and [`TeamMascot`][teammascot-table] table.
The query returns the rows from `Roster` and `TeamMascot` where
`Roster.SchoolID` is the same as `TeamMascot.SchoolID`. The results include a
single `SchoolID` column.

```googlesql
SELECT * FROM Roster INNER JOIN TeamMascot USING (SchoolID);

/*----------------------------------------+
 | SchoolID   | LastName   | Mascot       |
 +----------------------------------------+
 | 50         | Adams      | Jaguars      |
 | 52         | Buchanan   | Lakers       |
 | 52         | Coolidge   | Lakers       |
 | 51         | Davis      | Knights      |
 +----------------------------------------*/
```

#### `ON` and `USING` equivalency

The [`ON`][on-clause] and [`USING`][using-clause] join conditions aren't
equivalent, but they share some rules and sometimes they can produce similar
results.

In the following examples, observe what is returned when all rows
are produced for inner and outer joins. Also, look at how
each join condition handles `NULL` values.

```googlesql
WITH
  A AS (SELECT 1 as x UNION ALL SELECT 2 UNION ALL SELECT 3),
  B AS (SELECT 2 as x UNION ALL SELECT 3 UNION ALL SELECT 4)
SELECT * FROM A INNER JOIN B ON A.x = B.x;

WITH
  A AS (SELECT 1 as x UNION ALL SELECT 2 UNION ALL SELECT 3),
  B AS (SELECT 2 as x UNION ALL SELECT 3 UNION ALL SELECT 4)
SELECT * FROM A INNER JOIN B USING (x);

/*
Table A   Table B   Result ON     Result USING
+---+     +---+     +-------+     +---+
| x |  *  | x |  =  | x | x |     | x |
+---+     +---+     +-------+     +---+
| 1 |     | 2 |     | 2 | 2 |     | 2 |
| 2 |     | 3 |     | 3 | 3 |     | 3 |
| 3 |     | 4 |     +-------+     +---+
+---+     +---+
*/
```

```googlesql
WITH
  A AS (SELECT 1 as x UNION ALL SELECT 2 UNION ALL SELECT 3 UNION ALL SELECT NULL),
  B AS (SELECT 2 as x UNION ALL SELECT 3 UNION ALL SELECT 4 UNION ALL SELECT 5)
SELECT * FROM A LEFT OUTER JOIN B ON A.x = B.x;

WITH
  A AS (SELECT 1 as x UNION ALL SELECT 2 UNION ALL SELECT 3 UNION ALL SELECT NULL),
  B AS (SELECT 2 as x UNION ALL SELECT 3 UNION ALL SELECT 4 UNION ALL SELECT 5)
SELECT * FROM A LEFT OUTER JOIN B USING (x);

/*
Table A    Table B   Result ON           Result USING
+------+   +---+     +-------------+     +------+
| x    | * | x |  =  | x    | x    |     | x    |
+------+   +---+     +-------------+     +------+
| 1    |   | 2 |     | 1    | NULL |     | 1    |
| 2    |   | 3 |     | 2    | 2    |     | 2    |
| 3    |   | 4 |     | 3    | 3    |     | 3    |
| NULL |   | 5 |     | NULL | NULL |     | NULL |
+------+   +---+     +-------------+     +------+
*/
```

```googlesql
WITH
  A AS (SELECT 1 as x UNION ALL SELECT 2 UNION ALL SELECT 3),
  B AS (SELECT 2 as x UNION ALL SELECT 3 UNION ALL SELECT 4)
SELECT * FROM A FULL OUTER JOIN B ON A.x = B.x;

WITH
  A AS (SELECT 1 as x UNION ALL SELECT 2 UNION ALL SELECT 3),
  B AS (SELECT 2 as x UNION ALL SELECT 3 UNION ALL SELECT 4)
SELECT * FROM A FULL OUTER JOIN B USING (x);

/*
Table A   Table B   Result ON           Result USING
+---+     +---+     +-------------+     +---+
| x |  *  | x |  =  | x    | x    |     | x |
+---+     +---+     +-------------+     +---+
| 1 |     | 2 |     | 1    | NULL |     | 1 |
| 2 |     | 3 |     | 2    | 2    |     | 2 |
| 3 |     | 4 |     | 3    | 3    |     | 3 |
+---+     +---+     | NULL | 4    |     | 4 |
                    +-------------+     +---+
*/
```

Although `ON` and `USING` aren't equivalent, they can return the same
results in some situations if you specify the columns you want to return.

In the following examples, observe what is returned when a specific row
is produced for inner and outer joins. Also, look at how each
join condition handles `NULL` values.

```googlesql
WITH
  A AS (SELECT 1 as x UNION ALL SELECT 2 UNION ALL SELECT 3 UNION ALL SELECT NULL),
  B AS (SELECT 2 as x UNION ALL SELECT 3 UNION ALL SELECT 4 UNION ALL SELECT 5)
SELECT A.x FROM A INNER JOIN B ON A.x = B.x;

WITH
  A AS (SELECT 1 as x UNION ALL SELECT 2 UNION ALL SELECT 3 UNION ALL SELECT NULL),
  B AS (SELECT 2 as x UNION ALL SELECT 3 UNION ALL SELECT 4 UNION ALL SELECT 5)
SELECT x FROM A INNER JOIN B USING (x);

/*
Table A    Table B   Result ON     Result USING
+------+   +---+     +---+         +---+
| x    | * | x |  =  | x |         | x |
+------+   +---+     +---+         +---+
| 1    |   | 2 |     | 2 |         | 2 |
| 2    |   | 3 |     | 3 |         | 3 |
| 3    |   | 4 |     +---+         +---+
| NULL |   | 5 |
+------+   +---+
*/
```

```googlesql
WITH
  A AS (SELECT 1 as x UNION ALL SELECT 2 UNION ALL SELECT 3 UNION ALL SELECT NULL),
  B AS (SELECT 2 as x UNION ALL SELECT 3 UNION ALL SELECT 4 UNION ALL SELECT 5)
SELECT A.x FROM A LEFT OUTER JOIN B ON A.x = B.x;

WITH
  A AS (SELECT 1 as x UNION ALL SELECT 2 UNION ALL SELECT 3 UNION ALL SELECT NULL),
  B AS (SELECT 2 as x UNION ALL SELECT 3 UNION ALL SELECT 4 UNION ALL SELECT 5)
SELECT x FROM A LEFT OUTER JOIN B USING (x);

/*
Table A    Table B   Result ON    Result USING
+------+   +---+     +------+     +------+
| x    | * | x |  =  | x    |     | x    |
+------+   +---+     +------+     +------+
| 1    |   | 2 |     | 1    |     | 1    |
| 2    |   | 3 |     | 2    |     | 2    |
| 3    |   | 4 |     | 3    |     | 3    |
| NULL |   | 5 |     | NULL |     | NULL |
+------+   +---+     +------+     +------+
*/
```

```googlesql
WITH
  A AS (SELECT 1 as x UNION ALL SELECT 2 UNION ALL SELECT 3 UNION ALL SELECT NULL),
  B AS (SELECT 2 as x UNION ALL SELECT 3 UNION ALL SELECT 4 UNION ALL SELECT 5)
SELECT A.x FROM A FULL OUTER JOIN B ON A.x = B.x;

WITH
  A AS (SELECT 1 as x UNION ALL SELECT 2 UNION ALL SELECT 3 UNION ALL SELECT NULL),
  B AS (SELECT 2 as x UNION ALL SELECT 3 UNION ALL SELECT 4 UNION ALL SELECT 5)
SELECT x FROM A FULL OUTER JOIN B USING (x);

/*
Table A    Table B   Result ON    Result USING
+------+   +---+     +------+     +------+
| x    | * | x |  =  | x    |     | x    |
+------+   +---+     +------+     +------+
| 1    |   | 2 |     | 1    |     | 1    |
| 2    |   | 3 |     | 2    |     | 2    |
| 3    |   | 4 |     | 3    |     | 3    |
| NULL |   | 5 |     | NULL |     | NULL |
+------+   +---+     | NULL |     | 4    |
                     | NULL |     | 5    |
                     +------+     +------+
*/
```

```googlesql
WITH
  A AS (SELECT 1 as x UNION ALL SELECT 2 UNION ALL SELECT 3 UNION ALL SELECT NULL),
  B AS (SELECT 2 as x UNION ALL SELECT 3 UNION ALL SELECT 4 UNION ALL SELECT 5)
SELECT B.x FROM A FULL OUTER JOIN B ON A.x = B.x;

WITH
  A AS (SELECT 1 as x UNION ALL SELECT 2 UNION ALL SELECT 3 UNION ALL SELECT NULL),
  B AS (SELECT 2 as x UNION ALL SELECT 3 UNION ALL SELECT 4 UNION ALL SELECT 5)
SELECT x FROM A FULL OUTER JOIN B USING (x);

/*
Table A    Table B   Result ON    Result USING
+------+   +---+     +------+     +------+
| x    | * | x |  =  | x    |     | x    |
+------+   +---+     +------+     +------+
| 1    |   | 2 |     | 2    |     | 1    |
| 2    |   | 3 |     | 3    |     | 2    |
| 3    |   | 4 |     | NULL |     | 3    |
| NULL |   | 5 |     | NULL |     | NULL |
+------+   +---+     | 4    |     | 4    |
                     | 5    |     | 5    |
                     +------+     +------+
*/
```

In the following example, observe what is returned when `COALESCE` is used
with the `ON` clause. It provides the same results as a query
with the `USING` clause.

```googlesql
WITH
  A AS (SELECT 1 as x UNION ALL SELECT 2 UNION ALL SELECT 3 UNION ALL SELECT NULL),
  B AS (SELECT 2 as x UNION ALL SELECT 3 UNION ALL SELECT 4 UNION ALL SELECT 5)
SELECT COALESCE(A.x, B.x) FROM A FULL OUTER JOIN B ON A.x = B.x;

WITH
  A AS (SELECT 1 as x UNION ALL SELECT 2 UNION ALL SELECT 3 UNION ALL SELECT NULL),
  B AS (SELECT 2 as x UNION ALL SELECT 3 UNION ALL SELECT 4 UNION ALL SELECT 5)
SELECT x FROM A FULL OUTER JOIN B USING (x);

/*
Table A    Table B   Result ON    Result USING
+------+   +---+     +------+     +------+
| x    | * | x |  =  | x    |     | x    |
+------+   +---+     +------+     +------+
| 1    |   | 2 |     | 1    |     | 1    |
| 2    |   | 3 |     | 2    |     | 2    |
| 3    |   | 4 |     | 3    |     | 3    |
| NULL |   | 5 |     | NULL |     | NULL |
+------+   +---+     | 4    |     | 4    |
                     | 5    |     | 5    |
                     +------+     +------+
*/
```

### Join operations in a sequence 
<a id="sequences_of_joins"></a>

The `FROM` clause can contain multiple `JOIN` operations in a sequence.
`JOIN`s are bound from left to right. For example:

```googlesql
FROM A JOIN B USING (x) JOIN C USING (x)

-- A JOIN B USING (x)        = result_1
-- result_1 JOIN C USING (x) = result_2
-- result_2                  = return value
```

You can also insert parentheses to group `JOIN`s:

```googlesql
FROM ( (A JOIN B USING (x)) JOIN C USING (x) )

-- A JOIN B USING (x)        = result_1
-- result_1 JOIN C USING (x) = result_2
-- result_2                  = return value
```

With parentheses, you can group `JOIN`s so that they are bound in a different
order:

```googlesql
FROM ( A JOIN (B JOIN C USING (x)) USING (x) )

-- B JOIN C USING (x)       = result_1
-- A JOIN result_1          = result_2
-- result_2                 = return value
```

A `FROM` clause can have multiple joins. Provided there are no comma cross joins
in the `FROM` clause, joins don't require parenthesis, though parenthesis can
help readability:

```googlesql
FROM A JOIN B JOIN C JOIN D USING (w) ON B.x = C.y ON A.z = B.x
```

If your clause contains comma cross joins, you must use parentheses:

```googlesql {.bad}
FROM A, B JOIN C JOIN D ON C.x = D.y ON B.z = C.x    // INVALID
```

```googlesql
FROM A, B JOIN (C JOIN D ON C.x = D.y) ON B.z = C.x  // VALID
```

When comma cross joins are present in a query with a sequence of JOINs, they
group from left to right like other `JOIN` types:

```googlesql
FROM A JOIN B USING (x) JOIN C USING (x), D

-- A JOIN B USING (x)        = result_1
-- result_1 JOIN C USING (x) = result_2
-- result_2 CROSS JOIN D     = return value
```

There can't be a `RIGHT JOIN` or `FULL JOIN` after a comma cross join unless
it's parenthesized:

```googlesql {.bad}
FROM A, B RIGHT JOIN C ON TRUE // INVALID
```

```googlesql {.bad}
FROM A, B FULL JOIN C ON TRUE  // INVALID
```

```googlesql
FROM A, B JOIN C ON TRUE       // VALID
```

```googlesql
FROM A, (B RIGHT JOIN C ON TRUE) // VALID
```

```googlesql
FROM A, (B FULL JOIN C ON TRUE)  // VALID
```

### Correlated join operation 
<a id="correlated_join"></a>

A join operation is _correlated_ when the right `from_item` contains a
reference to at least one range variable or
column name introduced by the left `from_item`.

In a correlated join operation, rows from the right `from_item` are determined
by a row from the left `from_item`. Consequently, `RIGHT OUTER` and `FULL OUTER`
joins can't be correlated because right `from_item` rows can't be determined
in the case when there is no row from the left `from_item`.

All correlated join operations must reference an array in the right `from_item`.

This is a conceptual example of a correlated join operation that includes
a [correlated subquery][correlated-subquery]:

```googlesql
FROM A JOIN UNNEST(ARRAY(SELECT AS STRUCT * FROM B WHERE A.ID = B.ID)) AS C
```

+ Left `from_item`: `A`
+ Right `from_item`: `UNNEST(...) AS C`
+ A correlated subquery: `(SELECT AS STRUCT * FROM B WHERE A.ID = B.ID)`

This is another conceptual example of a correlated join operation.
`array_of_IDs` is part of the left `from_item` but is referenced in the
right `from_item`.

```googlesql
FROM A JOIN UNNEST(A.array_of_IDs) AS C
```

The [`UNNEST` operator][unnest-operator] can be explicit or implicit.
These are both allowed:

```googlesql
FROM A JOIN UNNEST(A.array_of_IDs) AS IDs
```

```googlesql
FROM A JOIN A.array_of_IDs AS IDs
```

In a correlated join operation, the right `from_item` is re-evaluated
against each distinct row from the left `from_item`. In the following
conceptual example, the correlated join operation first
evaluates `A` and `B`, then `A` and `C`:

```googlesql
FROM
  A
  JOIN
  UNNEST(ARRAY(SELECT AS STRUCT * FROM B WHERE A.ID = B.ID)) AS C
  ON A.Name = C.Name
```

**Caveats**

+   In a correlated `LEFT JOIN`, when the input table on the right side is empty
    for some row from the left side, it's as if no rows from the right side
    satisfied the join condition in a regular `LEFT JOIN`. When there are no
    joining rows, a row with `NULL` values for all columns on the right side is
    generated to join with the row from the left side.
+   In a correlated `CROSS JOIN`, when the input table on the right side is
    empty for some row from the left side, it's as if no rows from the right
    side satisfied the join condition in a regular correlated `INNER JOIN`. This
    means that the row is dropped from the results.

**Examples**

This is an example of a correlated join, using the
[Roster][roster-table] and [PlayerStats][playerstats-table] tables:

```googlesql
SELECT *
FROM
  Roster
JOIN
  UNNEST(
    ARRAY(
      SELECT AS STRUCT *
      FROM PlayerStats
      WHERE PlayerStats.OpponentID = Roster.SchoolID
    )) AS PlayerMatches
  ON PlayerMatches.LastName = 'Buchanan'

/*------------+----------+----------+------------+--------------+
 | LastName   | SchoolID | LastName | OpponentID | PointsScored |
 +------------+----------+----------+------------+--------------+
 | Adams      | 50       | Buchanan | 50         | 13           |
 | Eisenhower | 77       | Buchanan | 77         | 0            |
 +------------+----------+----------+------------+--------------*/
```

A common pattern for a correlated `LEFT JOIN` is to have an `UNNEST` operation
on the right side that references an array from some column introduced by
input on the left side. For rows where that array is empty or `NULL`,
the `UNNEST` operation produces no rows on the right input. In that case, a row
with a `NULL` entry in each column of the right input is created to join with
the row from the left input. For example:

```googlesql
SELECT A.name, item, ARRAY_LENGTH(A.items) item_count_for_name
FROM
  UNNEST(
    [
      STRUCT(
        'first' AS name,
        [1, 2, 3, 4] AS items),
      STRUCT(
        'second' AS name,
        [] AS items)]) AS A
LEFT JOIN
  A.items AS item;

/*--------+------+---------------------+
 | name   | item | item_count_for_name |
 +--------+------+---------------------+
 | first  | 1    | 4                   |
 | first  | 2    | 4                   |
 | first  | 3    | 4                   |
 | first  | 4    | 4                   |
 | second | NULL | 0                   |
 +--------+------+---------------------*/
```

In the case of a correlated `INNER JOIN` or `CROSS JOIN`, when the input on the
right side is empty for some row from the left side, the final row is dropped
from the results. For example:

```googlesql
SELECT A.name, item
FROM
  UNNEST(
    [
      STRUCT(
        'first' AS name,
        [1, 2, 3, 4] AS items),
      STRUCT(
        'second' AS name,
        [] AS items)]) AS A
INNER JOIN
  A.items AS item;

/*-------+------+
 | name  | item |
 +-------+------+
 | first | 1    |
 | first | 2    |
 | first | 3    |
 | first | 4    |
 +-------+------*/
```

## `WHERE` clause 
<a id="where_clause"></a>

<pre>
WHERE bool_expression
</pre>

The `WHERE` clause filters the results of the `FROM` clause.

Only rows whose `bool_expression` evaluates to `TRUE` are included. Rows
whose `bool_expression` evaluates to `NULL` or `FALSE` are
discarded.

The evaluation of a query with a `WHERE` clause is typically completed in this
order:

+ `FROM`
+ `WHERE`
+ `GROUP BY` and aggregation
+ `HAVING`
+ `WINDOW`
+ `QUALIFY`
+ `DISTINCT`
+ `ORDER BY`
+ `LIMIT`

Evaluation order doesn't always match syntax order.

The `WHERE` clause only references columns available via the `FROM` clause;
it can't reference `SELECT` list aliases.

**Examples**

This query returns returns all rows from the [`Roster`][roster-table] table
where the `SchoolID` column has the value `52`:

```googlesql
SELECT * FROM Roster
WHERE SchoolID = 52;
```

The `bool_expression` can contain multiple sub-conditions:

```googlesql
SELECT * FROM Roster
WHERE STARTS_WITH(LastName, "Mc") OR STARTS_WITH(LastName, "Mac");
```

Expressions in an `INNER JOIN` have an equivalent expression in the
`WHERE` clause. For example, a query using `INNER` `JOIN` and `ON` has an
equivalent expression using `CROSS JOIN` and `WHERE`. For example,
the following two queries are equivalent:

```googlesql
SELECT Roster.LastName, TeamMascot.Mascot
FROM Roster INNER JOIN TeamMascot
ON Roster.SchoolID = TeamMascot.SchoolID;
```

```googlesql
SELECT Roster.LastName, TeamMascot.Mascot
FROM Roster CROSS JOIN TeamMascot
WHERE Roster.SchoolID = TeamMascot.SchoolID;
```

## `GROUP BY` clause 
<a id="group_by_clause"></a>

<pre>
GROUP BY <span class="var">group_by_specification</span>

<span class="var">group_by_specification</span>:
  {
    <span class="var">groupable_items</span>
    | ALL
    | <span class="var">grouping_sets_specification</span>
    | <span class="var">rollup_specification</span>
    | <span class="var">cube_specification</span>
    | ()
  }
</pre>

**Description**

The `GROUP BY` clause groups together rows in a table that share common values
for certain columns. For a group of rows in the source table with
non-distinct values, the `GROUP BY` clause aggregates them into a single
combined row. This clause is commonly used when aggregate functions are
present in the `SELECT` list, or to eliminate redundancy in the output.

**Definitions**

+ `groupable_items`: Group rows in a table that share common values
  for certain columns. To learn more, see
  [Group rows by groupable items][group-by-groupable-item].
+ `ALL`: Automatically group rows. To learn more, see
  [Group rows automatically][group-by-all].
+ `grouping_sets_specification`: Group rows with the
  `GROUP BY GROUPING SETS` clause. To learn more, see
  [Group rows by `GROUPING SETS`][group-by-grouping-sets].
+ `rollup_specification`: Group rows with the `GROUP BY ROLLUP` clause.
  To learn more, see [Group rows by `ROLLUP`][group-by-rollup].
+ `cube_specification`: Group rows with the `GROUP BY CUBE` clause.
  To learn more, see [Group rows by `CUBE`][group-by-cube].
+ `()`: Group all rows and produce a grand total. Equivalent to no
  `group_by_specification`.

### Group rows by groupable items 
<a id="group_by_grouping_item"></a>

<pre>
GROUP BY <span class="var">groupable_item</span>[, ...]

<span class="var">groupable_item</span>:
  {
    <span class="var">value</span>
    | <span class="var">value_alias</span>
    | <span class="var">column_ordinal</span>
  }
</pre>

**Description**

The `GROUP BY` clause can include [groupable][data-type-properties] expressions
and their ordinals.

**Definitions**

+ `value`: An expression that represents a non-distinct, groupable value.
  To learn more, see [Group rows by values][group-by-values].
+ `value_alias`: An alias for `value`.
  To learn more, see [Group rows by values][group-by-values].
+ `column_ordinal`: An `INT64` value that represents the ordinal assigned to a
  groupable expression in the `SELECT` list.
  To learn more, see [Group rows by column ordinals][group-by-col-ordinals].

#### Group rows by values 
<a id="group_by_values"></a>

The `GROUP BY` clause can group rows in a table with non-distinct
values in the `GROUP BY` clause. For example:

```googlesql
WITH PlayerStats AS (
  SELECT 'Adams' as LastName, 'Noam' as FirstName, 3 as PointsScored UNION ALL
  SELECT 'Buchanan', 'Jie', 0 UNION ALL
  SELECT 'Coolidge', 'Kiran', 1 UNION ALL
  SELECT 'Adams', 'Noam', 4 UNION ALL
  SELECT 'Buchanan', 'Jie', 13)
SELECT SUM(PointsScored) AS total_points, LastName
FROM PlayerStats
GROUP BY LastName;

/*--------------+----------+
 | total_points | LastName |
 +--------------+----------+
 | 7            | Adams    |
 | 13           | Buchanan |
 | 1            | Coolidge |
 +--------------+----------*/
```

`GROUP BY` clauses may also refer to aliases. If a query contains aliases in
the `SELECT` clause, those aliases override names in the corresponding `FROM`
clause. For example:

```googlesql
WITH PlayerStats AS (
  SELECT 'Adams' as LastName, 'Noam' as FirstName, 3 as PointsScored UNION ALL
  SELECT 'Buchanan', 'Jie', 0 UNION ALL
  SELECT 'Coolidge', 'Kiran', 1 UNION ALL
  SELECT 'Adams', 'Noam', 4 UNION ALL
  SELECT 'Buchanan', 'Jie', 13)
SELECT SUM(PointsScored) AS total_points, LastName AS last_name
FROM PlayerStats
GROUP BY last_name;

/*--------------+-----------+
 | total_points | last_name |
 +--------------+-----------+
 | 7            | Adams     |
 | 13           | Buchanan  |
 | 1            | Coolidge  |
 +--------------+-----------*/
```

You can use the `GROUP BY` clause with arrays. The following query executes
because the array elements being grouped are the same length and group type:

```googlesql
WITH PlayerStats AS (
  SELECT ['Coolidge', 'Adams'] as Name, 3 as PointsScored UNION ALL
  SELECT ['Adams', 'Buchanan'], 0 UNION ALL
  SELECT ['Coolidge', 'Adams'], 1 UNION ALL
  SELECT ['Kiran', 'Noam'], 1)
SELECT SUM(PointsScored) AS total_points, name
FROM PlayerStats
GROUP BY Name;

/*--------------+------------------+
 | total_points | name             |
 +--------------+------------------+
 | 4            | [Coolidge,Adams] |
 | 0            | [Adams,Buchanan] |
 | 1            | [Kiran,Noam]     |
 +--------------+------------------*/
```

You can use the `GROUP BY` clause with structs. The following query executes
because the struct fields being grouped have the same group types:

```googlesql
WITH
  TeamStats AS (
    SELECT
      ARRAY<STRUCT<last_name STRING, first_name STRING, age INT64>>[
        ('Adams', 'Noam', 20), ('Buchanan', 'Jie', 19)] AS Team,
      3 AS PointsScored
    UNION ALL
    SELECT [('Coolidge', 'Kiran', 21), ('Yang', 'Jason', 22)], 4
    UNION ALL
    SELECT [('Adams', 'Noam', 20), ('Buchanan', 'Jie', 19)], 10
    UNION ALL
    SELECT [('Coolidge', 'Kiran', 21), ('Yang', 'Jason', 22)], 7
  )
SELECT
  SUM(PointsScored) AS total_points,
  Team
FROM TeamStats
GROUP BY Team;

/*--------------+--------------------------+
 | total_points | teams                    |
 +--------------+--------------------------+
 | 13           | [{                       |
 |              |    last_name: "Adams",   |
 |              |    first_name: "Noam",   |
 |              |    age: 20               |
 |              |  },{                     |
 |              |    last_name: "Buchanan",|
 |              |    first_name: "Jie",    |
 |              |    age: 19               |
 |              |  }]                      |
 +-----------------------------------------+
 | 11           | [{                       |
 |              |    last_name: "Coolidge",|
 |              |    first_name: "Kiran",  |
 |              |    age: 21               |
 |              |  },{                     |
 |              |    last_name: "Yang",    |
 |              |    first_name: "Jason",  |
 |              |    age: 22               |
 |              |  }]                      |
 +--------------+--------------------------*/
```

To learn more about the data types that are supported for values in the
`GROUP BY` clause, see [Groupable data types][data-type-properties].

#### Group rows by column ordinals 
<a id="group_by_col_ordinals"></a>

The `GROUP BY` clause can refer to expression names in the `SELECT` list. The
`GROUP BY` clause also allows ordinal references to expressions in the `SELECT`
list, using integer values. `1` refers to the first value in the
`SELECT` list, `2` the second, and so forth. The value list can combine
ordinals and value names. The following queries are equivalent:

```googlesql
WITH PlayerStats AS (
  SELECT 'Adams' as LastName, 'Noam' as FirstName, 3 as PointsScored UNION ALL
  SELECT 'Buchanan', 'Jie', 0 UNION ALL
  SELECT 'Coolidge', 'Kiran', 1 UNION ALL
  SELECT 'Adams', 'Noam', 4 UNION ALL
  SELECT 'Buchanan', 'Jie', 13)
SELECT SUM(PointsScored) AS total_points, LastName, FirstName
FROM PlayerStats
GROUP BY LastName, FirstName;

/*--------------+----------+-----------+
 | total_points | LastName | FirstName |
 +--------------+----------+-----------+
 | 7            | Adams    | Noam      |
 | 13           | Buchanan | Jie       |
 | 1            | Coolidge | Kiran     |
 +--------------+----------+-----------*/
```

```googlesql
WITH PlayerStats AS (
  SELECT 'Adams' as LastName, 'Noam' as FirstName, 3 as PointsScored UNION ALL
  SELECT 'Buchanan', 'Jie', 0 UNION ALL
  SELECT 'Coolidge', 'Kiran', 1 UNION ALL
  SELECT 'Adams', 'Noam', 4 UNION ALL
  SELECT 'Buchanan', 'Jie', 13)
SELECT SUM(PointsScored) AS total_points, LastName, FirstName
FROM PlayerStats
GROUP BY 2, 3;

/*--------------+----------+-----------+
 | total_points | LastName | FirstName |
 +--------------+----------+-----------+
 | 7            | Adams    | Noam      |
 | 13           | Buchanan | Jie       |
 | 1            | Coolidge | Kiran     |
 +--------------+----------+-----------*/
```

### Group rows by `ALL` 
<a id="group_by_all"></a>

<pre>
GROUP BY ALL
</pre>

**Description**

The `GROUP BY ALL` clause groups rows by inferring grouping keys from the
`SELECT` items.

The following `SELECT` items are excluded from the `GROUP BY ALL` clause:

+   Expressions that include [aggregate functions][aggregate-function-calls].
+   Expressions that include [window functions][window-function-calls].
+   Expressions that don't reference a name from the `FROM` clause.
    This includes:
    +   Constants
    +   Query parameters
    +   Correlated column references
    +   Expressions that only reference `GROUP BY` keys inferred from
        other `SELECT` items.

After exclusions are applied, an error is produced if any remaining `SELECT`
item includes a volatile function or has a non-groupable type.

If the set of inferred grouping keys is empty after exclusions are applied, all
input rows are considered a single group for aggregation. This
behavior is equivalent to writing `GROUP BY ()`.

**Examples**

In the following example, the query groups rows by `first_name` and
`last_name`. `total_points` is excluded because it represents an
aggregate function.

```googlesql
WITH PlayerStats AS (
  SELECT 'Adams' as LastName, 'Noam' as FirstName, 3 as PointsScored UNION ALL
  SELECT 'Buchanan', 'Jie', 0 UNION ALL
  SELECT 'Coolidge', 'Kiran', 1 UNION ALL
  SELECT 'Adams', 'Noam', 4 UNION ALL
  SELECT 'Buchanan', 'Jie', 13)
SELECT
  SUM(PointsScored) AS total_points,
  FirstName AS first_name,
  LastName AS last_name
FROM PlayerStats
GROUP BY ALL;

/*--------------+------------+-----------+
 | total_points | first_name | last_name |
 +--------------+------------+-----------+
 | 7            | Noam       | Adams     |
 | 13           | Jie        | Buchanan  |
 | 1            | Kiran      | Coolidge  |
 +--------------+------------+-----------*/
```

If the select list contains an analytic function, the query groups rows by
`first_name` and `last_name`. `total_people` is excluded because it
contains a window function.

```googlesql
WITH PlayerStats AS (
  SELECT 'Adams' as LastName, 'Noam' as FirstName, 3 as PointsScored UNION ALL
  SELECT 'Buchanan', 'Jie', 0 UNION ALL
  SELECT 'Coolidge', 'Kiran', 1 UNION ALL
  SELECT 'Adams', 'Noam', 4 UNION ALL
  SELECT 'Buchanan', 'Jie', 13)
SELECT
  COUNT(*) OVER () AS total_people,
  FirstName AS first_name,
  LastName AS last_name
FROM PlayerStats
GROUP BY ALL;

/*--------------+------------+-----------+
 | total_people | first_name | last_name |
 +--------------+------------+-----------+
 | 3            | Noam       | Adams     |
 | 3            | Jie        | Buchanan  |
 | 3            | Kiran      | Coolidge  |
 +--------------+------------+-----------*/
```

If multiple `SELECT` items reference the same `FROM` item, and any of them is
a path expression prefix of another, only the prefix path is used for grouping.
In the following example, `coordinates` is excluded because `x_coordinate` and
`y_coordinate` have already referenced `Values.x` and `Values.y` in the
`FROM` clause, and they are prefixes of the path expression used in
`x_coordinate`:

```googlesql
WITH Values AS (
  SELECT 1 AS x, 2 AS y
  UNION ALL SELECT 1 AS x, 4 AS y
  UNION ALL SELECT 2 AS x, 5 AS y
)
SELECT
  Values.x AS x_coordinate,
  Values.y AS y_coordinate,
  [Values.x, Values.y] AS coordinates
FROM Values
GROUP BY ALL

/*--------------+--------------+-------------+
 | x_coordinate | y_coordinate | coordinates |
 +--------------+--------------+-------------+
 | 1            | 4            | [1, 4]      |
 | 1            | 2            | [1, 2]      |
 | 2            | 5            | [2, 5]      |
 +--------------+--------------+-------------*/
```

In the following example, the inferred set of grouping keys is empty. The query
returns one row even when the input contains zero rows.

```googlesql
SELECT COUNT(*) AS num_rows
FROM UNNEST([])
GROUP BY ALL

/*----------+
 | num_rows |
 +----------+
 | 0        |
 +----------*/
```

### Group rows by `GROUPING SETS` 
<a id="group_by_grouping_sets"></a>

<pre>
GROUP BY GROUPING SETS ( <span class="var">grouping_list</span> )

<span class="var">grouping_list</span>:
  {
    <span class="var">rollup_specification</span>
    | <span class="var">cube_specification</span>
    | <span class="var">groupable_item</span>
    | <span class="var">groupable_item_set</span>
  }[, ...]

<span class="var">groupable_item_set</span>:
  ( [ <span class="var">groupable_item</span>[, ...] ] )
</pre>

**Description**

The `GROUP BY GROUPING SETS` clause produces aggregated data for one or more
_grouping sets_. A grouping set is a group of columns by which rows can
be grouped together. This clause is helpful if you want to produce
aggregated data for sets of data without using the `UNION` operation.
For example, `GROUP BY GROUPING SETS(x,y)` is roughly equivalent to
`GROUP BY x UNION ALL GROUP BY y`.

**Definitions**

+ `grouping_list`: A list of items that you can add to the
  `GROUPING SETS` clause. Grouping sets are generated based upon what is in
  this list.
+ `rollup_specification`: Group rows with the `ROLLUP` clause.
  Don't include `GROUP BY` if you use this inside the `GROUPING SETS` clause.
  To learn more, see [Group rows by `ROLLUP`][group-by-rollup].
+ `cube_specification`: Group rows with the `CUBE` clause.
  Don't include `GROUP BY` if you use this inside the `GROUPING SETS` clause.
  To learn more, see [Group rows by `CUBE`][group-by-cube].
+ `groupable_item`: Group rows in a table that share common values
  for certain columns. To learn more, see
  [Group rows by groupable items][group-by-groupable-item].
  [Anonymous `STRUCT` values][tuple-struct] aren't allowed.
+ `groupable_item_set`: Group rows by a set of
  [groupable items][group-by-groupable-item]. If the set contains no
  groupable items, group all rows and produce a grand total.

**Details**

`GROUP BY GROUPING SETS` works by taking a grouping list, generating
grouping sets from it, and then producing a table as a union of queries
grouped by each grouping set.

For example, `GROUP BY GROUPING SETS (a, b, c)` generates the
following grouping sets from the grouping list, `a, b, c`, and
then produces aggregated rows for each of them:

+ `(a)`
+ `(b)`
+ `(c)`

Here is an example that includes groupable item sets in
`GROUP BY GROUPING SETS (a, (b, c), d)`:

Conceptual grouping sets | Actual grouping sets
------------------------ | --------------
`(a)`                    | `(a)`
`((b, c))`               | `(b, c)`
`(d)`                    | `(d)`

`GROUP BY GROUPING SETS` can include `ROLLUP` and `CUBE` operations, which
generate grouping sets. If `ROLLUP` is added, it generates rolled up
grouping sets. If `CUBE` is added, it generates grouping set permutations.

The following grouping sets are generated for
`GROUP BY GROUPING SETS (a, ROLLUP(b, c), d)`.

Conceptual grouping sets | Actual grouping sets
------------------------ | --------------
`(a)`                    | `(a)`
`((b, c))`               | `(b, c)`
`((b))`                  | `(b)`
`(())`                   | `()`
`(d)`                    | `(d)`

The following grouping sets are generated for
`GROUP BY GROUPING SETS (a, CUBE(b, c), d)`:

Conceptual grouping sets | Actual grouping sets
------------------------ | --------------
`(a)`                    | `(a)`
`((b, c))`               | `(b, c)`
`((b))`                  | `(b)`
`((c))`                  | `(c)`
`(())`                   | `()`
`(d)`                    | `(d)`

When evaluating the results for a particular grouping set,
expressions that aren't in the grouping set are aggregated and produce a
`NULL` placeholder.

You can filter results for specific groupable items. To learn more, see the
[`GROUPING` function][grouping-function]

**Examples**

The following queries produce the same results, but
the first one uses `GROUP BY GROUPING SETS` and the second one doesn't:

```googlesql
-- GROUP BY with GROUPING SETS
WITH
  Products AS (
    SELECT 'shirt' AS product_type, 't-shirt' AS product_name, 3 AS product_count UNION ALL
    SELECT 'shirt', 't-shirt', 8 UNION ALL
    SELECT 'shirt', 'polo', 25 UNION ALL
    SELECT 'pants', 'jeans', 6
  )
SELECT product_type, product_name, SUM(product_count) AS product_sum
FROM Products
GROUP BY GROUPING SETS (product_type, product_name)
ORDER BY product_name

/*--------------+--------------+-------------+
 | product_type | product_name | product_sum |
 +--------------+--------------+-------------+
 | shirt        | NULL         | 36          |
 | pants        | NULL         | 6           |
 | NULL         | jeans        | 6           |
 | NULL         | polo         | 25          |
 | NULL         | t-shirt      | 11          |
 +--------------+--------------+-------------*/
```

```googlesql
-- GROUP BY without GROUPING SETS
-- (produces the same results as GROUPING SETS)
WITH
  Products AS (
    SELECT 'shirt' AS product_type, 't-shirt' AS product_name, 3 AS product_count UNION ALL
    SELECT 'shirt', 't-shirt', 8 UNION ALL
    SELECT 'shirt', 'polo', 25 UNION ALL
    SELECT 'pants', 'jeans', 6
  )
SELECT product_type, NULL, SUM(product_count) AS product_sum
FROM Products
GROUP BY product_type
UNION ALL
SELECT NULL, product_name, SUM(product_count) AS product_sum
FROM Products
GROUP BY product_name
ORDER BY product_name
```

You can include groupable item sets in a `GROUP BY GROUPING SETS` clause.
In the example below, `(product_type, product_name)` is a groupable item set.

```googlesql
-- GROUP BY with GROUPING SETS and a groupable item set
WITH
  Products AS (
    SELECT 'shirt' AS product_type, 't-shirt' AS product_name, 3 AS product_count UNION ALL
    SELECT 'shirt', 't-shirt', 8 UNION ALL
    SELECT 'shirt', 'polo', 25 UNION ALL
    SELECT 'pants', 'jeans', 6
  )
SELECT product_type, product_name, SUM(product_count) AS product_sum
FROM Products
GROUP BY GROUPING SETS (
  product_type,
  (product_type, product_name))
ORDER BY product_type, product_name;

/*--------------+--------------+-------------+
 | product_type | product_name | product_sum |
 +--------------+--------------+-------------+
 | pants        | NULL         | 6           |
 | pants        | jeans        | 6           |
 | shirt        | NULL         | 36          |
 | shirt        | polo         | 25          |
 | shirt        | t-shirt      | 11          |
 +--------------+--------------+-------------*/
```

```googlesql
-- GROUP BY with GROUPING SETS but without a groupable item set
-- (produces the same results as GROUPING SETS with a groupable item set)
WITH
  Products AS (
    SELECT 'shirt' AS product_type, 't-shirt' AS product_name, 3 AS product_count UNION ALL
    SELECT 'shirt', 't-shirt', 8 UNION ALL
    SELECT 'shirt', 'polo', 25 UNION ALL
    SELECT 'pants', 'jeans', 6
  )
SELECT product_type, NULL, SUM(product_count) AS product_sum
FROM Products
GROUP BY product_type
UNION ALL
SELECT product_type, product_name, SUM(product_count) AS product_sum
FROM Products
GROUP BY product_type, product_name
ORDER BY product_type, product_name;
```

You can include [`ROLLUP`][group-by-rollup] in a
`GROUP BY GROUPING SETS` clause. For example:

```googlesql
-- GROUP BY with GROUPING SETS and ROLLUP
WITH
  Products AS (
    SELECT 'shirt' AS product_type, 't-shirt' AS product_name, 3 AS product_count UNION ALL
    SELECT 'shirt', 't-shirt', 8 UNION ALL
    SELECT 'shirt', 'polo', 25 UNION ALL
    SELECT 'pants', 'jeans', 6
  )
SELECT product_type, product_name, SUM(product_count) AS product_sum
FROM Products
GROUP BY GROUPING SETS (
  product_type,
  ROLLUP (product_type, product_name))
ORDER BY product_type, product_name;

/*--------------+--------------+-------------+
 | product_type | product_name | product_sum |
 +--------------+--------------+-------------+
 | NULL         | NULL         | 42          |
 | pants        | NULL         | 6           |
 | pants        | NULL         | 6           |
 | pants        | jeans        | 6           |
 | shirt        | NULL         | 36          |
 | shirt        | NULL         | 36          |
 | shirt        | polo         | 25          |
 | shirt        | t-shirt      | 11          |
 +--------------+--------------+-------------*/
```

```googlesql
-- GROUP BY with GROUPING SETS, but without ROLLUP
-- (produces the same results as GROUPING SETS with ROLLUP)
WITH
  Products AS (
    SELECT 'shirt' AS product_type, 't-shirt' AS product_name, 3 AS product_count UNION ALL
    SELECT 'shirt', 't-shirt', 8 UNION ALL
    SELECT 'shirt', 'polo', 25 UNION ALL
    SELECT 'pants', 'jeans', 6
  )
SELECT product_type, product_name, SUM(product_count) AS product_sum
FROM Products
GROUP BY GROUPING SETS(
  product_type,
  (product_type, product_name),
  product_type,
  ())
ORDER BY product_type, product_name;
```

You can include [`CUBE`][group-by-cube] in a `GROUP BY GROUPING SETS` clause.
For example:

```googlesql
-- GROUP BY with GROUPING SETS and CUBE
WITH
  Products AS (
    SELECT 'shirt' AS product_type, 't-shirt' AS product_name, 3 AS product_count UNION ALL
    SELECT 'shirt', 't-shirt', 8 UNION ALL
    SELECT 'shirt', 'polo', 25 UNION ALL
    SELECT 'pants', 'jeans', 6
  )
SELECT product_type, product_name, SUM(product_count) AS product_sum
FROM Products
GROUP BY GROUPING SETS (
  product_type,
  CUBE (product_type, product_name))
ORDER BY product_type, product_name;

/*--------------+--------------+-------------+
 | product_type | product_name | product_sum |
 +--------------+--------------+-------------+
 | NULL         | NULL         | 42          |
 | NULL         | jeans        | 6           |
 | NULL         | polo         | 25          |
 | NULL         | t-shirt      | 11          |
 | pants        | NULL         | 6           |
 | pants        | NULL         | 6           |
 | pants        | jeans        | 6           |
 | shirt        | NULL         | 36          |
 | shirt        | NULL         | 36          |
 | shirt        | polo         | 25          |
 | shirt        | t-shirt      | 11          |
 +--------------+--------------+-------------*/
```

```googlesql
-- GROUP BY with GROUPING SETS, but without CUBE
-- (produces the same results as GROUPING SETS with CUBE)
WITH
  Products AS (
    SELECT 'shirt' AS product_type, 't-shirt' AS product_name, 3 AS product_count UNION ALL
    SELECT 'shirt', 't-shirt', 8 UNION ALL
    SELECT 'shirt', 'polo', 25 UNION ALL
    SELECT 'pants', 'jeans', 6
  )
SELECT product_type, product_name, SUM(product_count) AS product_sum
FROM Products
GROUP BY GROUPING SETS(
  product_type,
  (product_type, product_name),
  product_type,
  product_name,
  ())
ORDER BY product_type, product_name;
```

### Group rows by `ROLLUP` 
<a id="group_by_rollup"></a>

<pre>
GROUP BY ROLLUP ( <span class="var">grouping_list</span> )

<span class="var">grouping_list</span>:
  { <span class="var">groupable_item</span> | <span class="var">groupable_item_set</span> }[, ...]

<span class="var">groupable_item_set</span>:
  ( <span class="var">groupable_item</span>[, ...] )
</pre>

**Description**

The `GROUP BY ROLLUP` clause produces aggregated data for rolled up
_grouping sets_. A grouping set is a group of columns by which rows can
be grouped together. This clause is helpful if you need to roll up totals
in a set of data.

**Definitions**

+ `grouping_list`: A list of items that you can add to the
  `GROUPING SETS` clause. This is used to create a generated list
  of grouping sets when the query is run.
+ `groupable_item`: Group rows in a table that share common values
  for certain columns. To learn more, see
  [Group rows by groupable items][group-by-groupable-item].[anonymous `STRUCT` values][tuple-struct]
  aren't allowed.
+ `groupable_item_set`: Group rows by a subset of
  [groupable items][group-by-groupable-item].

**Details**

`GROUP BY ROLLUP` works by taking a grouping list, generating
grouping sets from the prefixes inside this list, and then producing a
table as a union of queries grouped by each grouping set. The resulting
grouping sets include an empty grouping set. In the empty grouping set, all
rows are aggregated down to a single group.

For example, `GROUP BY ROLLUP (a, b, c)` generates the
following grouping sets from the grouping list, `a, b, c`, and then produces
aggregated rows for each of them:

+ `(a, b, c)`
+ `(a, b)`
+ `(a)`
+ `()`

Here is an example that includes groupable item sets in
`GROUP BY ROLLUP (a, (b, c), d)`:

Conceptual grouping sets | Actual grouping sets
------------------------ | --------------
`(a, (b, c), d)`         | `(a, b, c, d)`
`(a, (b, c))`            | `(a, b, c)`
`(a)`                    | `(a)`
`()`                     | `()`

When evaluating the results for a particular grouping set,
expressions that aren't in the grouping set are aggregated and produce a
`NULL` placeholder.

You can filter results by specific groupable items. To learn more, see the
[`GROUPING` function][grouping-function]

**Examples**

The following queries produce the same subtotals and a grand total, but
the first one uses `GROUP BY` with `ROLLUP` and the second one doesn't:

```googlesql
-- GROUP BY with ROLLUP
WITH
  Products AS (
    SELECT 'shirt' AS product_type, 't-shirt' AS product_name, 3 AS product_count UNION ALL
    SELECT 'shirt', 't-shirt', 8 UNION ALL
    SELECT 'shirt', 'polo', 25 UNION ALL
    SELECT 'pants', 'jeans', 6
  )
SELECT product_type, product_name, SUM(product_count) AS product_sum
FROM Products
GROUP BY ROLLUP (product_type, product_name)
ORDER BY product_type, product_name;

/*--------------+--------------+-------------+
 | product_type | product_name | product_sum |
 +--------------+--------------+-------------+
 | NULL         | NULL         | 42          |
 | pants        | NULL         | 6           |
 | pants        | jeans        | 6           |
 | shirt        | NULL         | 36          |
 | shirt        | t-shirt      | 11          |
 | shirt        | polo         | 25          |
 +--------------+--------------+-------------*/
```

```googlesql
-- GROUP BY without ROLLUP (produces the same results as ROLLUP)
WITH
  Products AS (
    SELECT 'shirt' AS product_type, 't-shirt' AS product_name, 3 AS product_count UNION ALL
    SELECT 'shirt', 't-shirt', 8 UNION ALL
    SELECT 'shirt', 'polo', 25 UNION ALL
    SELECT 'pants', 'jeans', 6
  )
SELECT product_type, product_name, SUM(product_count) AS product_sum
FROM Products
GROUP BY product_type, product_name
UNION ALL
SELECT product_type, NULL, SUM(product_count)
FROM Products
GROUP BY product_type
UNION ALL
SELECT NULL, NULL, SUM(product_count) FROM Products
ORDER BY product_type, product_name;
```

You can include groupable item sets in a `GROUP BY ROLLUP` clause.
In the following example, `(product_type, product_name)` is a
groupable item set.

```googlesql
WITH
  Products AS (
    SELECT 'shirt' AS product_type, 't-shirt' AS product_name, 3 AS product_count UNION ALL
    SELECT 'shirt', 't-shirt', 8 UNION ALL
    SELECT 'shirt', 'polo', 25 UNION ALL
    SELECT 'pants', 'jeans', 6
  )
SELECT product_type, product_name, SUM(product_count) AS product_sum
FROM Products
GROUP BY ROLLUP (product_type, (product_type, product_name))
ORDER BY product_type, product_name;

/*--------------+--------------+-------------+
 | product_type | product_name | product_sum |
 +--------------+--------------+-------------+
 | NULL         | NULL         | 42          |
 | pants        | NULL         | 6           |
 | pants        | jeans        | 6           |
 | shirt        | NULL         | 36          |
 | shirt        | polo         | 25          |
 | shirt        | t-shirt      | 11          |
 +--------------+--------------+-------------*/
```

### Group rows by `CUBE` 
<a id="group_by_cube"></a>

<pre>
GROUP BY CUBE ( <span class="var">grouping_list</span> )

<span class="var">grouping_list</span>:
  { <span class="var">groupable_item</span> | <span class="var">groupable_item_set</span> }[, ...]

<span class="var">groupable_item_set</span>:
  ( <span class="var">groupable_item</span>[, ...] )
</pre>

**Description**

The `GROUP BY CUBE` clause produces aggregated data for all _grouping set_
permutations. A grouping set is a group of columns by which rows
can be grouped together. This clause is helpful if you need to create a
[contingency table][contingency-table]{: .external} to find interrelationships
between items in a set of data.

**Definitions**

+ `grouping_list`: A list of items that you can add to the
  `GROUPING SETS` clause. This is used to create a generated list
  of grouping sets when the query is run.
+ `groupable_item`: Group rows in a table that share common values
  for certain columns. To learn more, see
  [Group rows by groupable items][group-by-groupable-item].
  [Anonymous `STRUCT` values][tuple-struct] aren't allowed.
+ `groupable_item_set`: Group rows by a set of
  [groupable items][group-by-groupable-item].

**Details**

`GROUP BY CUBE` is similar to `GROUP BY ROLLUP`, except that it takes a
grouping list and generates grouping sets from all permutations in this
list, including an empty grouping set. In the empty grouping set, all rows
are aggregated down to a single group.

For example, `GROUP BY CUBE (a, b, c)` generates the following
grouping sets from the grouping list, `a, b, c`, and then produces
aggregated rows for each of them:

+ `(a, b, c)`
+ `(a, b)`
+ `(a, c)`
+ `(a)`
+ `(b, c)`
+ `(b)`
+ `(c)`
+ `()`

Here is an example that includes groupable item sets in
`GROUP BY CUBE (a, (b, c), d)`:

Conceptual grouping sets | Actual grouping sets
------------------------ | --------------
`(a, (b, c), d)`         | `(a, b, c, d)`
`(a, (b, c))`            | `(a, b, c)`
`(a, d)`                 | `(a, d)`
`(a)`                    | `(a)`
`((b, c), d)`            | `(b, c, d)`
`((b, c))`               | `(b, c)`
`(d)`                    | `(d)`
`()`                     | `()`

When evaluating the results for a particular grouping set,
expressions that aren't in the grouping set are aggregated and produce a
`NULL` placeholder.

You can filter results by specific groupable items. To learn more, see the
[`GROUPING` function][grouping-function]

**Examples**

The following query groups rows by all combinations of `product_type` and
`product_name` to produce a contingency table:

```googlesql
-- GROUP BY with CUBE
WITH
  Products AS (
    SELECT 'shirt' AS product_type, 't-shirt' AS product_name, 3 AS product_count UNION ALL
    SELECT 'shirt', 't-shirt', 8 UNION ALL
    SELECT 'shirt', 'polo', 25 UNION ALL
    SELECT 'pants', 'jeans', 6
  )
SELECT product_type, product_name, SUM(product_count) AS product_sum
FROM Products
GROUP BY CUBE (product_type, product_name)
ORDER BY product_type, product_name;

/*--------------+--------------+-------------+
 | product_type | product_name | product_sum |
 +--------------+--------------+-------------+
 | NULL         | NULL         | 42          |
 | NULL         | jeans        | 6           |
 | NULL         | polo         | 25          |
 | NULL         | t-shirt      | 11          |
 | pants        | NULL         | 6           |
 | pants        | jeans        | 6           |
 | shirt        | NULL         | 36          |
 | shirt        | polo         | 25          |
 | shirt        | t-shirt      | 11          |
 +--------------+--------------+-------------*/
```

You can include groupable item sets in a `GROUP BY CUBE` clause.
In the following example, `(product_type, product_name)` is a
groupable item set.

```googlesql
WITH
  Products AS (
    SELECT 'shirt' AS product_type, 't-shirt' AS product_name, 3 AS product_count UNION ALL
    SELECT 'shirt', 't-shirt', 8 UNION ALL
    SELECT 'shirt', 'polo', 25 UNION ALL
    SELECT 'pants', 'jeans', 6
  )
SELECT product_type, product_name, SUM(product_count) AS product_sum
FROM Products
GROUP BY CUBE (product_type, (product_type, product_name))
ORDER BY product_type, product_name;

/*--------------+--------------+-------------+
 | product_type | product_name | product_sum |
 +--------------+--------------+-------------+
 | NULL         | NULL         | 42          |
 | pants        | NULL         | 6           |
 | pants        | jeans        | 6           |
 | pants        | jeans        | 6           |
 | shirt        | NULL         | 36          |
 | shirt        | polo         | 25          |
 | shirt        | polo         | 25          |
 | shirt        | t-shirt      | 11          |
 | shirt        | t-shirt      | 11          |
 +--------------+--------------+-------------*/
```

## `HAVING` clause 
<a id="having_clause"></a>

<pre>
HAVING bool_expression
</pre>

The `HAVING` clause filters the results produced by `GROUP BY` or
aggregation. `GROUP BY` or aggregation must be present in the query. If
aggregation is present, the `HAVING` clause is evaluated once for every
aggregated row in the result set.

Only rows whose `bool_expression` evaluates to `TRUE` are included. Rows
whose `bool_expression` evaluates to `NULL` or `FALSE` are
discarded.

The evaluation of a query with a `HAVING` clause is typically completed in this
order:

+ `FROM`
+ `WHERE`
+ `GROUP BY` and aggregation
+ `HAVING`
+ `WINDOW`
+ `QUALIFY`
+ `DISTINCT`
+ `ORDER BY`
+ `LIMIT`

Evaluation order doesn't always match syntax order.

The `HAVING` clause references columns available via the `FROM` clause, as
well as `SELECT` list aliases. Expressions referenced in the `HAVING` clause
must either appear in the `GROUP BY` clause or they must be the result of an
aggregate function:

```googlesql
SELECT LastName
FROM Roster
GROUP BY LastName
HAVING SUM(PointsScored) > 15;
```

If a query contains aliases in the `SELECT` clause, those aliases override names
in a `FROM` clause.

```googlesql
SELECT LastName, SUM(PointsScored) AS ps
FROM Roster
GROUP BY LastName
HAVING ps > 0;
```

### Mandatory aggregation 
<a id="mandatory_aggregation"></a>

Aggregation doesn't have to be present in the `HAVING` clause itself, but
aggregation must be present in at least one of the following forms:

#### Aggregation function in the `SELECT` list.

```googlesql
SELECT LastName, SUM(PointsScored) AS total
FROM PlayerStats
GROUP BY LastName
HAVING total > 15;
```

#### Aggregation function in the `HAVING` clause.

```googlesql
SELECT LastName
FROM PlayerStats
GROUP BY LastName
HAVING SUM(PointsScored) > 15;
```

#### Aggregation in both the `SELECT` list and `HAVING` clause.

When aggregation functions are present in both the `SELECT` list and `HAVING`
clause, the aggregation functions and the columns they reference don't need
to be the same. In the example below, the two aggregation functions,
`COUNT()` and `SUM()`, are different and also use different columns.

```googlesql
SELECT LastName, COUNT(*)
FROM PlayerStats
GROUP BY LastName
HAVING SUM(PointsScored) > 15;
```

<a id="collate_clause"></a>
## `ORDER BY` clause 
<a id="order_by_clause"></a>

<pre>
ORDER BY expression
  [COLLATE collation_specification]
  [{ ASC | DESC }]
  [{ NULLS FIRST | NULLS LAST }]
  [, ...]

collation_specification:
  language_tag[:collation_attribute]
</pre>

The `ORDER BY` clause specifies a column or expression as the sort criterion for
the result set. If an `ORDER BY` clause isn't present, the order of the results
of a query isn't defined. Column aliases from a `FROM` clause or `SELECT` list
are allowed. If a query contains aliases in the `SELECT` clause, those aliases
override names in the corresponding `FROM` clause. The data type of
`expression` must be [orderable][orderable-data-types].

**Optional Clauses**

+ `COLLATE`: You can use the `COLLATE` clause to refine how data is ordered
  by an `ORDER BY` clause. _Collation_ refers to a set of rules that determine
  how strings are compared according to the conventions and
  standards of a particular written language, region, or country. These rules
  might define the correct character sequence, with options for specifying
  case-insensitivity. You can use `COLLATE` only on columns of type `STRING`.

  `collation_specification` represents the collation specification for the
  `COLLATE` clause. The collation specification can be a string literal or
   a query parameter. To learn more see
   [collation specification details][collation-spec].
+  `NULLS FIRST | NULLS LAST`:
    +  `NULLS FIRST`: Sort null values before non-null values.
    +  `NULLS LAST`. Sort null values after non-null values.
+  `ASC | DESC`: Sort the results in ascending or descending
    order of `expression` values. `ASC` is the
    default value. If null ordering isn't specified
    with `NULLS FIRST` or `NULLS LAST`:
    +  `NULLS FIRST` is applied by default if the sort order is ascending.
    +  `NULLS LAST` is applied by default if the sort order is
       descending.

**Examples**

Use the default sort order (ascending).

```googlesql
SELECT x, y
FROM (SELECT 1 AS x, true AS y UNION ALL
      SELECT 9, true UNION ALL
      SELECT NULL, false)
ORDER BY x;

/*------+-------+
 | x    | y     |
 +------+-------+
 | NULL | false |
 | 1    | true  |
 | 9    | true  |
 +------+-------*/
```

Use the default sort order (ascending), but return null values last.

```googlesql
SELECT x, y
FROM (SELECT 1 AS x, true AS y UNION ALL
      SELECT 9, true UNION ALL
      SELECT NULL, false)
ORDER BY x NULLS LAST;

/*------+-------+
 | x    | y     |
 +------+-------+
 | 1    | true  |
 | 9    | true  |
 | NULL | false |
 +------+-------*/
```

Use descending sort order.

```googlesql
SELECT x, y
FROM (SELECT 1 AS x, true AS y UNION ALL
      SELECT 9, true UNION ALL
      SELECT NULL, false)
ORDER BY x DESC;

/*------+-------+
 | x    | y     |
 +------+-------+
 | 9    | true  |
 | 1    | true  |
 | NULL | false |
 +------+-------*/
```

Use descending sort order, but return null values first.

```googlesql
SELECT x, y
FROM (SELECT 1 AS x, true AS y UNION ALL
      SELECT 9, true UNION ALL
      SELECT NULL, false)
ORDER BY x DESC NULLS FIRST;

/*------+-------+
 | x    | y     |
 +------+-------+
 | NULL | false |
 | 9    | true  |
 | 1    | true  |
 +------+-------*/
```

It's possible to order by multiple columns. In the example below, the result
set is ordered first by `SchoolID` and then by `LastName`:

```googlesql
SELECT LastName, PointsScored, OpponentID
FROM PlayerStats
ORDER BY SchoolID, LastName;
```

When used in conjunction with
[set operators][set-operators],
the `ORDER BY` clause applies to the result set of the entire query; it doesn't
apply only to the closest `SELECT` statement. For this reason, it can be helpful
(though it isn't required) to use parentheses to show the scope of the `ORDER
BY`.

This query without parentheses:

```googlesql
SELECT * FROM Roster
UNION ALL
SELECT * FROM TeamMascot
ORDER BY SchoolID;
```

is equivalent to this query with parentheses:

```googlesql
( SELECT * FROM Roster
  UNION ALL
  SELECT * FROM TeamMascot )
ORDER BY SchoolID;
```

but isn't equivalent to this query, where the `ORDER BY` clause applies only to
the second `SELECT` statement:

```googlesql
SELECT * FROM Roster
UNION ALL
( SELECT * FROM TeamMascot
  ORDER BY SchoolID );
```

You can also use integer literals as column references in `ORDER BY` clauses. An
integer literal becomes an ordinal (for example, counting starts at 1) into
the `SELECT` list.

Example - the following two queries are equivalent:

```googlesql
SELECT SUM(PointsScored), LastName
FROM PlayerStats
GROUP BY LastName
ORDER BY LastName;
```

```googlesql
SELECT SUM(PointsScored), LastName
FROM PlayerStats
GROUP BY 2
ORDER BY 2;
```

Collate results using English - Canada:

```googlesql
SELECT Place
FROM Locations
ORDER BY Place COLLATE "en_CA"
```

Collate results using a parameter:

```googlesql
#@collate_param = "arg_EG"
SELECT Place
FROM Locations
ORDER BY Place COLLATE @collate_param
```

Using multiple `COLLATE` clauses in a statement:

```googlesql
SELECT APlace, BPlace, CPlace
FROM Locations
ORDER BY APlace COLLATE "en_US" ASC,
         BPlace COLLATE "ar_EG" DESC,
         CPlace COLLATE "en" DESC
```

Case insensitive collation:

```googlesql
SELECT Place
FROM Locations
ORDER BY Place COLLATE "en_US:ci"
```

Default Unicode case-insensitive collation:

```googlesql
SELECT Place
FROM Locations
ORDER BY Place COLLATE "und:ci"
```

## `QUALIFY` clause

<pre>
QUALIFY bool_expression
</pre>

The `QUALIFY` clause filters the results of window functions.
A window function is required to be present in the `QUALIFY` clause or the
`SELECT` list.

Only rows whose `bool_expression` evaluates to `TRUE` are included. Rows
whose `bool_expression` evaluates to `NULL` or `FALSE` are
discarded.

The evaluation of a query with a `QUALIFY` clause is typically completed in this
order:

+ `FROM`
+ `WHERE`
+ `GROUP BY` and aggregation
+ `HAVING`
+ `WINDOW`
+ `QUALIFY`
+ `DISTINCT`
+ `ORDER BY`
+ `LIMIT`

Evaluation order doesn't always match syntax order.

**Examples**

The following query returns the most popular vegetables in the
[`Produce`][produce-table] table and their rank.

```googlesql
SELECT
  item,
  RANK() OVER (PARTITION BY category ORDER BY purchases DESC) as rank
FROM Produce
WHERE Produce.category = 'vegetable'
QUALIFY rank <= 3

/*---------+------+
 | item    | rank |
 +---------+------+
 | kale    | 1    |
 | lettuce | 2    |
 | cabbage | 3    |
 +---------+------*/
```

You don't have to include a window function in the `SELECT` list to use
`QUALIFY`. The following query returns the most popular vegetables in the
[`Produce`][produce-table] table.

```googlesql
SELECT item
FROM Produce
WHERE Produce.category = 'vegetable'
QUALIFY RANK() OVER (PARTITION BY category ORDER BY purchases DESC) <= 3

/*---------+
 | item    |
 +---------+
 | kale    |
 | lettuce |
 | cabbage |
 +---------*/
```

## `WINDOW` clause 
<a id="window_clause"></a>

<pre>
WINDOW named_window_expression [, ...]

named_window_expression:
  named_window AS { named_window | ( [ window_specification ] ) }
</pre>

A `WINDOW` clause defines a list of named windows.
A named window represents a group of rows in a table upon which to use a
[window function][window-function-calls]. A named window can be defined with
a [window specification][query-window-specification] or reference another
named window. If another named window is referenced, the definition of the
referenced window must precede the referencing window.

**Examples**

These examples reference a table called [`Produce`][produce-table].
They all return the same [result][named-window-example]. Note the different
ways you can combine named windows and use them in a window function's
`OVER` clause.

```googlesql
SELECT item, purchases, category, LAST_VALUE(item)
  OVER (item_window) AS most_popular
FROM Produce
WINDOW item_window AS (
  PARTITION BY category
  ORDER BY purchases
  ROWS BETWEEN 2 PRECEDING AND 2 FOLLOWING)
```

```googlesql
SELECT item, purchases, category, LAST_VALUE(item)
  OVER (d) AS most_popular
FROM Produce
WINDOW
  a AS (PARTITION BY category),
  b AS (a ORDER BY purchases),
  c AS (b ROWS BETWEEN 2 PRECEDING AND 2 FOLLOWING),
  d AS (c)
```

```googlesql
SELECT item, purchases, category, LAST_VALUE(item)
  OVER (c ROWS BETWEEN 2 PRECEDING AND 2 FOLLOWING) AS most_popular
FROM Produce
WINDOW
  a AS (PARTITION BY category),
  b AS (a ORDER BY purchases),
  c AS b
```

<a id="set_clause"></a>

## Set operators 
<a id="set_operators"></a>

<pre>
  <a href="#sql_syntax"><span class="var">query_expr</span></a>
  [ { INNER | [ { FULL | LEFT } [ OUTER ] ] } ]
  {
    <a href="#union">UNION</a> { ALL | DISTINCT } |
    <a href="#intersect">INTERSECT</a> { ALL | DISTINCT } |
    <a href="#except">EXCEPT</a> { ALL | DISTINCT }
  }
  [ { <a href="#by_name_or_corresponding">BY NAME</a> [ ON (<span class="var">column_list</span>) ] | [ STRICT ] <a href="#by_name_or_corresponding">CORRESPONDING</a> [ BY (<span class="var">column_list</span>) ] } ]
  <a href="#sql_syntax"><span class="var">query_expr</span></a>
</pre>

Set operators combine or filter
results from two or more input queries into a single result set.

**Definitions**

+   `query_expr`: One of two input queries whose results are combined or filtered
    into a single result set.
+   `UNION`: Returns the combined results of the left and right input queries.
    Values in columns that are matched by position are concatenated vertically.
+   `INTERSECT`: Returns rows that are found in the results of both the left and
    right input queries.
+   `EXCEPT`: Returns rows from the left input query that aren't present in the
    right input query.
+   `ALL`: Executes the set operation on all rows.
+   `DISTINCT`: Excludes duplicate rows in the set operation.
+   `BY NAME`, `CORRESPONDING`: Matches
    columns by name instead of by position. The `BY NAME` modifier is equivalent to `STRICT CORRESPONDING`.
    For details, see [`BY NAME` or `CORRESPONDING`][by-name-or-corresponding].
+   `INNER`, `FULL | LEFT [OUTER]`, `STRICT`, `ON`, `BY`:
    Adjust how the `BY NAME` or `CORRESPONDING` modifier behaves when
    the column names don't match exactly. For details, see
    [`BY NAME` or  `CORRESPONDING`][by-name-or-corresponding].

**Positional column matching**

By default, columns are matched positionally and follow these rules. If the
`BY NAME` or `CORRESPONDING` modifier is
used, columns are matched by name, as described in the next section.

+  Columns from input queries are matched by their position in the queries. That
   is, the first column in the first input query is paired with the first column
   in the second input query and so on.
+  The input queries on each side of the operator must return the same number of
   columns.

**Name-based column matching**

To make set operations match columns by name instead of by column position,
use the [`BY NAME` or `CORRESPONDING`][by-name-or-corresponding] modifier.

With `BY NAME` or `STRICT CORRESPONDING`, the same column names
must exist in each input, but they can be in different orders. Additional
modifiers can be used to handle cases where the columns don't exactly match.

The `BY NAME` modifier is equivalent to `STRICT CORRESPONDING`, but
the `BY NAME` modifier is recommended because it's shorter and clearer.

Example:

```googlesql
SELECT 1 AS one_digit, 10 AS two_digit
UNION ALL BY NAME
SELECT 20 AS two_digit, 2 AS one_digit;

-- Column values match by name and not position in query.
/*-----------+-----------+
 | one_digit | two_digit |
 +-----------+-----------+
 | 1         | 10        |
 | 2         | 20        |
 +-----------+-----------*/
```

**Other column-related rules**

+  For set operations other than `UNION
   ALL`, all column types must support
   equality comparison.
+  The results of the set operation always use the column names from the
   first input query.
+  The results of the set operation always use the supertypes of input types
   in corresponding columns, so paired columns must also have either the same
   data type or a common supertype.

**Parenthesized set operators**

+   Parentheses must be used to separate different set operations.
    Set operations like `UNION ALL` and `UNION DISTINCT` are considered different.
+   Parentheses are also used to group set operations and control order of
    operations. In `EXCEPT` set operations, for example,
    query results can vary depending on the operation grouping.

The following examples illustrate the use of parentheses with set
operations:

```googlesql
-- Same set operations, no parentheses.
query1
UNION ALL
query2
UNION ALL
query3;
```

```googlesql
-- Different set operations, parentheses needed.
query1
UNION ALL
(
  query2
  UNION DISTINCT
  query3
);
```

```googlesql {.bad}
-- Invalid
query1
UNION ALL
query2
UNION DISTINCT
query3;
```

```googlesql
-- Same set operations, no parentheses.
query1
EXCEPT ALL
query2
EXCEPT ALL
query3;

-- Equivalent query with optional parentheses, returns same results.
(
  query1
  EXCEPT ALL
  query2
)
EXCEPT ALL
query3;
```

```googlesql
-- Different execution order with a subquery, parentheses needed.
query1
EXCEPT ALL
(
  query2
  EXCEPT ALL
  query3
);
```

**Set operator behavior with duplicate rows**

Consider a given row `R` that appears exactly `m` times in the first input query
and `n` times in the second input query, where `m >= 0` and `n >= 0`:

+  For `UNION ALL`, row `R` appears exactly `m + n` times in the
   result.
+  For `INTERSECT ALL`, row `R` appears exactly `MIN(m, n)` times in the
   result.
+  For `EXCEPT ALL`, row `R` appears exactly `MAX(m - n, 0)` times in the
   result.
+  For `UNION DISTINCT`, the `DISTINCT`
   is computed after the `UNION` is computed, so row `R` appears exactly
   one time.
+  For `INTERSECT DISTINCT`, row `R` appears once in the output if `m > 0` and
   `n > 0`.
+  For `EXCEPT DISTINCT`, row `R` appears once in the output if
   `m > 0` and `n = 0`.
+  If more than two input queries are used, the above operations generalize
   and the output is the same as if the input queries were combined
   incrementally from left to right.

<span id="by_name_or_corresponding"><b>BY NAME or CORRESPONDING</b></span>

Use the `BY NAME` or `CORRESPONDING` modifier
with set operations to match columns by name instead of by position.
The `BY NAME` modifier is equivalent to `STRICT CORRESPONDING`, but the
`BY NAME` modifier is recommended because it's shorter and clearer.
You can use mode prefixes to adjust how
the `BY NAME` or `CORRESPONDING` modifier
behaves when the column names don't match exactly.

+   `BY NAME`: Matches
    columns by name instead of by position.
    + Both input queries must have the same set of column names, but column order
    can be different. If a column in one input query doesn't appear in the other
    query, an error is raised.
    + Input queries can't contain duplicate columns.
    + Input queries that produce [value tables][value-tables] aren't supported.
+   `INNER`: Adjusts the `BY NAME` modifier behavior so that columns that appear
    in both input queries are included in the query results and any other
    columns are excluded.
    +  No error is raised for the excluded columns that appear in one input
    query but not in the other input query.
    + At least one column must be common in both left and right input queries.
+   `FULL [OUTER]`: Adjusts the `BY NAME` modifier behavior so that all columns
    from both input queries are included in the query results, even if some
    columns aren't present in both queries.
    +  Columns from the left input query are returned
    first, followed by unique columns from the right input query.
    + For columns in one input query that aren't present in the other query,
    a `NULL` value is added as its column value for the other query in the results.
+   `LEFT [OUTER]`: Adjusts the `BY NAME` modifier so that all columns from the
    left input query are included in the results, even if some columns in the
    left query aren't present in the right query.
    + For columns in the left query that aren't in the right query, a `NULL`
    value is added as its column value for the right query in the results.
    + At least one column name must be common in both left and right input queries.
+   `OUTER`: If used alone, equivalent to `FULL OUTER`.
+   `ON (column_list)`: Used after the `BY NAME` modifier to
    specify a comma-separated list of column names and the column order to
    return from the input queries.
    + If `BY NAME ON (column_list)` is used
    alone without mode prefixes like 
    `INNER` or `FULL | LEFT [OUTER]`, then both the left and right
    input queries must contain all the columns in the `column_list`.
    + If any mode prefixes are used, then any column names not in the
    `column_list` are excluded from the results according to the mode used.
+   `CORRESPONDING`: Equivalent to `INNER...BY NAME`.
    + Supports `FULL | LEFT [OUTER]` modes the same way they're supported by the `BY NAME` modifier.
    + Supports `INNER` mode, but this mode has no effect. The `INNER` mode is used with the `BY NAME`
    modifier to exclude unmatched columns between input queries, which is
    the default behavior of the `CORRESPONDING` modifier. Therefore, using
    `INNER...CORRESPONDING` produces the same results as `CORRESPONDING`.
+   `STRICT`: Adjusts the `CORRESPONDING` modifier to be equivalent to the default `BY NAME` modifier, where input
    queries must have the same set of column names.
+   `BY (column_list)`: Equivalent to `ON (column_list)` with `BY NAME`.

The following table shows the equivalent syntaxes between the `BY NAME` and
`CORRESPONDING` modifiers, using the `UNION ALL` set operator as an example:

<table>
  <thead>
    <tr>
      <th>BY NAME syntax</th>
      <th>Equivalent CORRESPONDING syntax</th>
    </tr>
  </thead>
  <tbody>

    <tr>
      <td><code>UNION ALL BY NAME</code></td>
      <td><code>UNION ALL STRICT CORRESPONDING</code></td>
    </tr>

    <tr>
      <td><code>INNER UNION ALL BY NAME</code></td>
      <td><code>UNION ALL CORRESPONDING</code></td>
    </tr>
    <tr>
      <td><code>{LEFT | FULL} [OUTER] UNION ALL BY NAME</code></td>
      <td><code>{LEFT | FULL} [OUTER] UNION ALL CORRESPONDING</code></td>
    </tr>
    <tr>
      <td><code>[FULL] OUTER UNION ALL BY NAME</code></td>
      <td><code>[FULL] OUTER UNION ALL CORRESPONDING</code></td>
    </tr>
    <tr>
      <td><code>UNION ALL BY NAME ON (col1, col2, ...)</code></td>
      <td><code>UNION ALL STRICT CORRESPONDING BY (col1, col2, ...)</code></td>
    </tr>
  </tbody>
</table>

The following table shows the behavior of the mode prefixes for the
`BY NAME` and `CORRESPONDING` modifiers
when left and right input columns don't match:

<table style="width:100%">
  <thead>
    <tr>
      <th style="width:30%">Mode prefix and modifier</th>
      <th>Behavior when left and right input columns don't match</th>
    </tr>
  </thead>
  <tbody>
    <tr>
      <td><code>BY NAME</code> (no prefix) or<br/><code>STRICT CORRESPONDING</code></td>
      <td>Error, all columns must match in both inputs.</td>
     </tr>
    <tr>
      <td><code>INNER BY NAME</code> or<br/><code>CORRESPONDING</code> (no prefix)</td>
      <td>Drop all unmatched columns in both inputs.</td>
    </tr>
    <tr>
      <td><code>FULL [OUTER] BY NAME</code> or<br/><code>FULL [OUTER] CORRESPONDING</code></td>
      <td>Include all columns from both inputs. For column values that exist in one input but not in another, add <code>NULL</code> values.</td>
    </tr>
    <tr>
      <td><code>LEFT [OUTER] BY NAME</code> or<br/><code>LEFT [OUTER] CORRESPONDING</code></td>
      <td>Include all columns from the left input. For column values that exist in the left input but not in the right input, add <code>NULL</code> values. Drop any columns from the right input that don't exist in the left input.</td>
    </tr>
  </tbody>
</table>

For example set operations with modifiers, see the sections for each set
operator, such as [`UNION`][union-syntax].

### `UNION` 
<a id="union"></a>

The `UNION` operator returns the combined results of the left and right input
queries. Columns are matched according to the rules described previously and
rows are concatenated vertically.

**Examples**

```googlesql
SELECT * FROM UNNEST(ARRAY<INT64>[1, 2, 3]) AS number
UNION ALL
SELECT 1;

/*--------+
 | number |
 +--------+
 | 1      |
 | 2      |
 | 3      |
 | 1      |
 +--------*/
```

```googlesql
SELECT * FROM UNNEST(ARRAY<INT64>[1, 2, 3]) AS number
UNION DISTINCT
SELECT 1;

/*--------+
 | number |
 +--------+
 | 1      |
 | 2      |
 | 3      |
 +--------*/
```

The following example shows multiple chained operators:

```googlesql
SELECT * FROM UNNEST(ARRAY<INT64>[1, 2, 3]) AS number
UNION DISTINCT
SELECT 1
UNION DISTINCT
SELECT 2;

/*--------+
 | number |
 +--------+
 | 1      |
 | 2      |
 | 3      |
 +--------*/
```

The following example shows input queries with multiple columns. Both
queries specify the same column names but in different orders. As a result, the
column values are matched by column position in the input query and the column
names are ignored.

```googlesql
SELECT 1 AS one_digit, 10 AS two_digit
UNION ALL
SELECT 20 AS two_digit, 2 AS one_digit;

-- Column values are matched by position and not column name.
/*-----------+-----------+
 | one_digit | two_digit |
 +-----------+-----------+
 | 1         | 10        |
 | 20        | 2         |
 +-----------+-----------*/
```

To resolve this ordering issue, the following example uses the
`BY NAME` modifier to match
the columns by name instead of by position in the query results.

```googlesql
SELECT 1 AS one_digit, 10 AS two_digit
UNION ALL BY NAME
SELECT 20 AS two_digit, 2 AS one_digit;

-- Column values now match.
/*-----------+-----------+
 | one_digit | two_digit |
 +-----------+-----------+
 | 1         | 10        |
 | 2         | 20        |
 +-----------+-----------*/
```

The previous set operation with `BY NAME` is equivalent to using the `STRICT
CORRESPONDING` modifier. The `BY NAME` modifier is recommended because it's
shorter and clearer than the `STRICT CORRESPONDING` modifier.

```googlesql
SELECT 1 AS one_digit, 10 AS two_digit
UNION ALL STRICT CORRESPONDING
SELECT 20 AS two_digit, 2 AS one_digit;

/*-----------+-----------+
 | one_digit | two_digit |
 +-----------+-----------+
 | 1         | 10        |
 | 2         | 20        |
 +-----------+-----------*/
```

The following example adds a `three_digit` column to the left input query and a
`four_digit` column to the right input query. Because these columns aren't
present in both queries, the `BY NAME`
modifier would trigger an error. Therefore, the example
adds the `INNER`
mode prefix so that the new columns are excluded from the results, executing the
query successfully.

```googlesql
SELECT 1 AS one_digit, 10 AS two_digit, 100 AS three_digit
INNER UNION ALL BY NAME
SELECT 20 AS two_digit, 2 AS one_digit, 1000 AS four_digit;

/*-----------+-----------+
 | one_digit | two_digit |
 +-----------+-----------+
 | 1         | 10        |
 | 2         | 20        |
 +-----------+-----------*/
```

To include the differing columns in the results, the following example uses
the `FULL OUTER` mode prefix to populate `NULL` values for the missing column in
each query.

```googlesql
SELECT 1 AS one_digit, 10 AS two_digit, 100 AS three_digit
FULL OUTER UNION ALL BY NAME
SELECT 20 AS two_digit, 2 AS one_digit, 1000 AS four_digit;

/*-----------+-----------+-------------+------------+
 | one_digit | two_digit | three_digit | four_digit |
 +-----------+-----------+-------------+------------+
 | 1         | 10        | 100         | NULL       |
 | 2         | 20        | NULL        | 1000       |
 +-----------+-----------+-------------+------------*/
```

Similarly, the following example uses the `LEFT OUTER` mode prefix to include
the new column from only the left input query and populate a `NULL` value for
the missing column in the right input query.

```googlesql
SELECT 1 AS one_digit, 10 AS two_digit, 100 AS three_digit
LEFT OUTER UNION ALL BY NAME
SELECT 20 AS two_digit, 2 AS one_digit, 1000 AS four_digit;

/*-----------+-----------+-------------+
 | one_digit | two_digit | three_digit |
 +-----------+-----------+-------------+
 | 1         | 10        | 100         |
 | 2         | 20        | NULL        |
 +-----------+-----------+-------------*/
```

The following example adds the modifier `ON (column_list)`
to return only the specified columns in the specified order.

```googlesql
SELECT 1 AS one_digit, 10 AS two_digit, 100 AS three_digit
FULL OUTER UNION ALL BY NAME ON (three_digit, two_digit)
SELECT 20 AS two_digit, 2 AS one_digit, 1000 AS four_digit;

/*-------------+-----------+
 | three_digit | two_digit |
 +-------------+-----------+
 | 100         | 10        |
 | NULL        | 20        |
 +-----------+-------------*/
```

### `INTERSECT` 
<a id="intersect"></a>

The `INTERSECT` operator returns rows that are found in the results of both the
left and right input queries.

**Examples**

```googlesql
SELECT * FROM UNNEST(ARRAY<INT64>[1, 2, 3, 3, 4]) AS number
INTERSECT ALL
SELECT * FROM UNNEST(ARRAY<INT64>[2, 3, 3, 5]) AS number;

/*--------+
 | number |
 +--------+
 | 2      |
 | 3      |
 | 3      |
 +--------*/
```

```googlesql
SELECT * FROM UNNEST(ARRAY<INT64>[1, 2, 3, 3, 4]) AS number
INTERSECT DISTINCT
SELECT * FROM UNNEST(ARRAY<INT64>[2, 3, 3, 5]) AS number;

/*--------+
 | number |
 +--------+
 | 2      |
 | 3      |
 +--------*/
```

The following example shows multiple chained operations:

```googlesql
SELECT * FROM UNNEST(ARRAY<INT64>[1, 2, 3, 3, 4]) AS number
INTERSECT DISTINCT
SELECT * FROM UNNEST(ARRAY<INT64>[2, 3, 3, 5]) AS number
INTERSECT DISTINCT
SELECT * FROM UNNEST(ARRAY<INT64>[3, 3, 4, 5]) AS number;

/*--------+
 | number |
 +--------+
 | 3      |
 +--------*/
```

The following example shows input queries that specify multiple columns. Both
queries specify the same column names but in different orders. As a result, the
same columns in differing order are considered different columns, so the query
doesn't detect any intersecting row values. Therefore, no results are returned.

```googlesql
WITH
  NumbersTable AS (
    SELECT 1 AS one_digit, 10 AS two_digit
    UNION ALL
    SELECT 2, 20
    UNION ALL
    SELECT 3, 30
  )
SELECT one_digit, two_digit FROM NumbersTable
INTERSECT DISTINCT
SELECT 10 AS two_digit, 1 AS one_digit;

-- No intersecting values detected because columns aren't recognized as the same.
/*-----------+-----------+

 +-----------+-----------*/
```

To resolve this ordering issue, the following example uses the
`BY NAME` modifier to match
the columns by name instead of by position in the query results.

```googlesql
WITH
  NumbersTable AS (
    SELECT 1 AS one_digit, 10 AS two_digit
    UNION ALL
    SELECT 2, 20
    UNION ALL
    SELECT 3, 30
  )
SELECT one_digit, two_digit FROM NumbersTable
INTERSECT DISTINCT BY NAME
SELECT 10 AS two_digit, 1 AS one_digit;

/*-----------+-----------+
 | one_digit | two_digit |
 +-----------+-----------+
 | 1         | 10        |
 +-----------+-----------*/
```

The previous set operation with `BY NAME` is equivalent to using the `STRICT
CORRESPONDING` modifier. The `BY NAME` modifier is recommended because it's
shorter and clearer than the `STRICT CORRESPONDING` modifier.

```googlesql
WITH
  NumbersTable AS (
    SELECT 1 AS one_digit, 10 AS two_digit
    UNION ALL
    SELECT 2, 20
    UNION ALL
    SELECT 3, 30
  )
SELECT one_digit, two_digit FROM NumbersTable
INTERSECT DISTINCT STRICT CORRESPONDING
SELECT 10 AS two_digit, 1 AS one_digit;

/*-----------+-----------+
 | one_digit | two_digit |
 +-----------+-----------+
 | 1         | 10        |
 +-----------+-----------*/
```

For more syntax examples with the `BY NAME`
modifier, see the [`UNION`][union] set operator.

### `EXCEPT` 
<a id="except"></a>

The `EXCEPT` operator returns rows from the left input query that aren't present
in the right input query.

**Examples**

```googlesql
SELECT * FROM UNNEST(ARRAY<INT64>[1, 2, 3, 3, 4]) AS number
EXCEPT ALL
SELECT * FROM UNNEST(ARRAY<INT64>[1, 2]) AS number;

/*--------+
 | number |
 +--------+
 | 3      |
 | 3      |
 | 4      |
 +--------*/
```

```googlesql
SELECT * FROM UNNEST(ARRAY<INT64>[1, 2, 3, 3, 4]) AS number
EXCEPT DISTINCT
SELECT * FROM UNNEST(ARRAY<INT64>[1, 2]) AS number;

/*--------+
 | number |
 +--------+
 | 3      |
 | 4      |
 +--------*/
```

The following example shows multiple chained operations:

```googlesql
SELECT * FROM UNNEST(ARRAY<INT64>[1, 2, 3, 3, 4]) AS number
EXCEPT DISTINCT
SELECT * FROM UNNEST(ARRAY<INT64>[1, 2]) AS number
EXCEPT DISTINCT
SELECT * FROM UNNEST(ARRAY<INT64>[1, 4]) AS number;

/*--------+
 | number |
 +--------+
 | 3      |
 +--------*/
```

The following example modifies the execution behavior of the set operations. The
first input query is used against the result of the last two input queries
instead of the values of the last two queries individually. In this example,
the `EXCEPT` result of the last two input queries is `2`. Therefore, the
`EXCEPT` results of the entire query are any values other than `2` in the first
input query.

```googlesql
SELECT * FROM UNNEST(ARRAY<INT64>[1, 2, 3, 3, 4]) AS number
EXCEPT DISTINCT
(
  SELECT * FROM UNNEST(ARRAY<INT64>[1, 2]) AS number
  EXCEPT DISTINCT
  SELECT * FROM UNNEST(ARRAY<INT64>[1, 4]) AS number
);

/*--------+
 | number |
 +--------+
 | 1      |
 | 3      |
 | 4      |
 +--------*/
```

The following example shows input queries that specify multiple columns. Both
queries specify the same column names but in different orders. As a result, the
same columns in differing order are considered different columns, so the query
doesn't detect any common rows that should be excluded. Therefore, all column
values from the left input query are returned with no exclusions.

```googlesql
WITH
  NumbersTable AS (
    SELECT 1 AS one_digit, 10 AS two_digit
    UNION ALL
    SELECT 2, 20
    UNION ALL
    SELECT 3, 30
  )
SELECT one_digit, two_digit FROM NumbersTable
EXCEPT DISTINCT
SELECT 10 AS two_digit, 1 AS one_digit;

-- No values excluded because columns aren't recognized as the same.
/*-----------+-----------+
 | one_digit | two_digit |
 +-----------+-----------+
 | 1         | 10        |
 | 2         | 20        |
 | 3         | 30        |
 +-----------+-----------*/
```

To resolve this ordering issue, the following example uses the
`BY NAME` modifier to match
the columns by name instead of by position in the query results.

```googlesql
WITH
  NumbersTable AS (
    SELECT 1 AS one_digit, 10 AS two_digit
    UNION ALL
    SELECT 2, 20
    UNION ALL
    SELECT 3, 30
  )
SELECT one_digit, two_digit FROM NumbersTable
EXCEPT DISTINCT BY NAME
SELECT 10 AS two_digit, 1 AS one_digit;

/*-----------+-----------+
 | one_digit | two_digit |
 +-----------+-----------+
 | 2         | 20        |
 | 3         | 30        |
 +-----------+-----------*/
```

The previous set operation with `BY NAME` is equivalent to using the `STRICT
CORRESPONDING` modifier. The `BY NAME` modifier is recommended because it's
shorter and clearer than the `STRICT CORRESPONDING` modifier.

```googlesql
WITH
  NumbersTable AS (
    SELECT 1 AS one_digit, 10 AS two_digit
    UNION ALL
    SELECT 2, 20
    UNION ALL
    SELECT 3, 30
  )
SELECT one_digit, two_digit FROM NumbersTable
EXCEPT DISTINCT STRICT CORRESPONDING
SELECT 10 AS two_digit, 1 AS one_digit;

/*-----------+-----------+
 | one_digit | two_digit |
 +-----------+-----------+
 | 2         | 20        |
 | 3         | 30        |
 +-----------+-----------*/
```

For more syntax examples with the `BY NAME`
modifier, see the [`UNION`][union] set operator.

## `LIMIT` and `OFFSET` clause 
<a id="limit_and_offset_clause"></a>

```googlesql
LIMIT count [ OFFSET skip_rows ]
```

Limits the number of rows to return in a query. Optionally includes
the ability to skip over rows.

**Definitions**

+   `LIMIT`: Limits the number of rows to produce.

    `count` is an `INT64` constant expression that represents the
    non-negative, non-`NULL` limit. No more than `count` rows are produced.
    `LIMIT 0` returns 0 rows.

    
    If there is a set operation, `LIMIT` is applied after the set operation is
    evaluated.
    

    
+   `OFFSET`: Skips a specific number of rows before applying `LIMIT`.

    `skip_rows` is an `INT64` constant expression that represents
    the non-negative, non-`NULL` number of rows to skip.

**Details**

The rows that are returned by `LIMIT` and `OFFSET` have undefined order unless
these clauses are used after `ORDER BY`.

A constant expression can be represented by a general expression, literal, or
parameter value.

Note: Although the `LIMIT` clause limits the rows that a query produces, it
doesn't limit the amount of data processed by that query.

**Examples**

```googlesql
SELECT *
FROM UNNEST(ARRAY<STRING>['a', 'b', 'c', 'd', 'e']) AS letter
ORDER BY letter ASC LIMIT 2;

/*---------+
 | letter  |
 +---------+
 | a       |
 | b       |
 +---------*/
```

```googlesql
SELECT *
FROM UNNEST(ARRAY<STRING>['a', 'b', 'c', 'd', 'e']) AS letter
ORDER BY letter ASC LIMIT 3 OFFSET 1;

/*---------+
 | letter  |
 +---------+
 | b       |
 | c       |
 | d       |
 +---------*/
```

## `WITH` clause 
<a id="with_clause"></a>

<pre>
WITH [ <a href="#recursive_keyword">RECURSIVE</a> ] { <a href="#simple_cte"><span class="var">non_recursive_cte</span></a> | <a href="#recursive_cte"><span class="var">recursive_cte</span></a> }[, ...]
</pre>

A `WITH` clause contains one or more common table expressions (CTEs).
A CTE acts like a temporary table that you can reference within a single
query expression. Each CTE binds the results of a [subquery][subquery-concepts]
to a table name, which can be used elsewhere in the same query expression,
but [rules apply][cte-rules].

CTEs can be [non-recursive][non-recursive-cte] or
[recursive][recursive-cte] and you can include both of these in your
`WITH` clause. A recursive CTE references itself, where a
non-recursive CTE doesn't. If a recursive CTE is included in the `WITH` clause,
the `RECURSIVE` keyword must also be included.

You can include the `RECURSIVE` keyword in a `WITH` clause even if no
recursive CTEs are present. You can learn more about the `RECURSIVE` keyword
[here][recursive-keyword].

### `RECURSIVE` keyword 
<a id="recursive_keyword"></a>

A `WITH` clause can optionally include the `RECURSIVE` keyword, which does
two things:

+ Enables recursion in the `WITH` clause. If this keyword isn't present,
  you can only include non-recursive common table expressions (CTEs).
  If this keyword is present, you can use both [recursive][recursive-cte] and
  [non-recursive][non-recursive-cte] CTEs.
+ [Changes the visibility][cte-visibility] of CTEs in the `WITH` clause. If this
  keyword isn't present, a CTE is only visible to CTEs defined after it in the
  `WITH` clause. If this keyword is present, a CTE is visible to all CTEs in the
  `WITH` clause where it was defined.

### Non-recursive CTEs 
<a id="simple_cte"></a>

<pre>
non_recursive_cte:
  <a href="#cte_name">cte_name</a> AS ( <a href="#sql_syntax">query_expr</a> )
</pre>

A non-recursive common table expression (CTE) contains
a non-recursive [subquery][subquery-concepts]
and a name associated with the CTE.

+ A non-recursive CTE can't reference itself.
+ A non-recursive CTE can be referenced by the query expression that
  contains the `WITH` clause, but [rules apply][cte-rules].

##### Examples

In this example, a `WITH` clause defines two non-recursive CTEs that
are referenced in the related set operation, where one CTE is referenced by
each of the set operation's input query expressions:

```googlesql
WITH subQ1 AS (SELECT SchoolID FROM Roster),
     subQ2 AS (SELECT OpponentID FROM PlayerStats)
SELECT * FROM subQ1
UNION ALL
SELECT * FROM subQ2
```

You can break up more complex queries into a `WITH` clause and
`WITH` `SELECT` statement instead of writing nested table subqueries.
For example:

```googlesql
WITH q1 AS (my_query)
SELECT *
FROM
  (WITH q2 AS (SELECT * FROM q1) SELECT * FROM q2)
```

```googlesql
WITH q1 AS (my_query)
SELECT *
FROM
  (WITH q2 AS (SELECT * FROM q1),  # q1 resolves to my_query
        q3 AS (SELECT * FROM q1),  # q1 resolves to my_query
        q1 AS (SELECT * FROM q1),  # q1 (in the query) resolves to my_query
        q4 AS (SELECT * FROM q1)   # q1 resolves to the WITH subquery on the previous line.
    SELECT * FROM q1)              # q1 resolves to the third inner WITH subquery.
```

### Recursive CTEs 
<a id="recursive_cte"></a>

<pre>
recursive_cte:
  <a href="#cte_name">cte_name</a> AS ( recursive_union_operation )

recursive_union_operation:
  base_term union_operator recursive_term

base_term:
  <a href="#sql_syntax">query_expr</a>

recursive_term:
  <a href="#sql_syntax">query_expr</a>

union_operator:
  { UNION ALL | UNION DISTINCT }
</pre>

A recursive common table expression (CTE) contains a
recursive [subquery][subquery-concepts] and a name associated with the CTE.

+ A recursive CTE references itself.
+ A recursive CTE can be referenced in the query expression that contains the
  `WITH` clause, but [rules apply][cte-rules].
+ When a recursive CTE is defined in a `WITH` clause, the
  [`RECURSIVE`][recursive-keyword] keyword must be present.

A recursive CTE is defined by a _recursive union operation_. The
recursive union operation defines how input is recursively processed
to produce the final CTE result. The recursive union operation has the
following parts:

+ `base_term`: Runs the first iteration of the
  recursive union operation. This term must follow the
  [base term rules][base-term-rules].
+ `union_operator`: The `UNION` operator returns the rows that are from
  the union of the base term and recursive term. With `UNION ALL`,
  each row produced in iteration `N` becomes part of the final CTE result and
  input for iteration `N+1`. With
  `UNION DISTINCT`, only distinct rows become part of the final CTE result,
  and only new distinct rows move into iteration `N+1`. Iteration
  stops when an iteration produces no rows to move into the next iteration.
+ `recursive_term`: Runs the remaining iterations.
  It must include one self-reference (recursive reference) to the recursive CTE.
  Only this term can include a self-reference. This term
  must follow the [recursive term rules][recursive-cte-rules].

A recursive CTE looks like this:

```googlesql
WITH RECURSIVE
  T1 AS ( (SELECT 1 AS n) UNION ALL (SELECT n + 1 AS n FROM T1 WHERE n < 3) )
SELECT n FROM T1

/*---+
 | n |
 +---+
 | 2 |
 | 1 |
 | 3 |
 +---*/
```

The first iteration of a recursive union operation runs the base term.
Then, each subsequent iteration runs the recursive term and produces
_new rows_ which are unioned with the previous iteration. The recursive
union operation terminates when a recursive term iteration produces no new
rows.

If recursion doesn't terminate, the query will not terminate.

To avoid a non-terminating iteration in a recursive union operation, you can
use the `LIMIT` clause in a query.

A recursive CTE can include nested `WITH` clauses, however, you can't reference
`recursive_term` inside of an inner `WITH` clause. An inner `WITH`
clause can't be recursive unless it includes its own `RECURSIVE` keyword.
The `RECURSIVE` keyword affects only the particular `WITH` clause to which it
belongs.

To learn more about recursive CTEs and troubleshooting iteration limit errors,
see [Work with recursive CTEs][work-with-recursive-ctes].

##### Examples of allowed recursive CTEs

This is a simple recursive CTE:

```googlesql
WITH RECURSIVE
  T1 AS (
    (SELECT 1 AS n) UNION ALL
    (SELECT n + 2 FROM T1 WHERE n < 4))
SELECT * FROM T1 ORDER BY n

/*---+
 | n |
 +---+
 | 1 |
 | 3 |
 | 5 |
 +---*/
```

Multiple subqueries in the same recursive CTE are okay, as
long as each recursion has a cycle length of 1. It's also okay for recursive
entries to depend on non-recursive entries and vice-versa:

```googlesql
WITH RECURSIVE
  T0 AS (SELECT 1 AS n),
  T1 AS ((SELECT * FROM T0) UNION ALL (SELECT n + 1 FROM T1 WHERE n < 4)),
  T2 AS ((SELECT 1 AS n) UNION ALL (SELECT n + 1 FROM T2 WHERE n < 4)),
  T3 AS (SELECT * FROM T1 INNER JOIN T2 USING (n))
SELECT * FROM T3 ORDER BY n

/*---+
 | n |
 +---+
 | 1 |
 | 2 |
 | 3 |
 | 4 |
 +---*/
```

Aggregate functions can be invoked in subqueries, as long as they aren't
aggregating on the table being defined:

```googlesql
WITH RECURSIVE
  T0 AS (SELECT * FROM UNNEST ([60, 20, 30])),
  T1 AS ((SELECT 1 AS n) UNION ALL (SELECT n + (SELECT COUNT(*) FROM T0) FROM T1 WHERE n < 4))
SELECT * FROM T1 ORDER BY n

/*---+
 | n |
 +---+
 | 1 |
 | 4 |
 +---*/
```

`INNER JOIN` can be used inside subqueries:

```googlesql
WITH RECURSIVE
  T0 AS (SELECT 1 AS n),
  T1 AS ((SELECT 1 AS n) UNION ALL (SELECT n + 1 FROM T1 INNER JOIN T0 USING (n)))
SELECT * FROM T1 ORDER BY n

/*---+
 | n |
 +---+
 | 1 |
 | 2 |
 +---*/
```

`CROSS JOIN` can be used inside subqueries:

```googlesql
WITH RECURSIVE
  T0 AS (SELECT 2 AS p),
  T1 AS ((SELECT 1 AS n) UNION ALL (SELECT T1.n + T0.p FROM T1 CROSS JOIN T0 WHERE T1.n < 4))
SELECT * FROM T1 CROSS JOIN T0 ORDER BY n

/*---+---+
 | n | p |
 +---+---+
 | 1 | 2 |
 | 3 | 2 |
 | 5 | 2 |
 +---+---*/
```

In the following query, if `UNION DISTINCT` was replaced with `UNION ALL`,
this query would never terminate; it would keep generating rows
`0, 1, 2, 3, 4, 0, 1, 2, 3, 4...`. With `UNION DISTINCT`, however, the only row
produced by iteration `5` is a duplicate, so the query terminates.

```googlesql
WITH RECURSIVE
  T1 AS ( (SELECT 0 AS n) UNION DISTINCT (SELECT MOD(n + 1, 5) FROM T1) )
SELECT * FROM T1 ORDER BY n

/*---+
 | n |
 +---+
 | 0 |
 | 1 |
 | 2 |
 | 3 |
 | 4 |
 +---*/
```

##### Examples of disallowed recursive CTEs

The following recursive CTE is disallowed because the
self-reference doesn't include a set operator, base term, and
recursive term.

```googlesql {.bad}
WITH RECURSIVE
  T1 AS (SELECT * FROM T1)
SELECT * FROM T1

-- Error
```

The following recursive CTE is disallowed because the self-reference to `T1`
is in the base term. The self reference is only allowed in the recursive term.

```googlesql {.bad}
WITH RECURSIVE
  T1 AS ((SELECT * FROM T1) UNION ALL (SELECT 1))
SELECT * FROM T1

-- Error
```

The following recursive CTE is disallowed because there are multiple
self-references in the recursive term when there must only be one.

```googlesql {.bad}
WITH RECURSIVE
  T1 AS ((SELECT 1 AS n) UNION ALL ((SELECT * FROM T1) UNION ALL (SELECT * FROM T1)))
SELECT * FROM T1

-- Error
```

The following recursive CTE is disallowed because the self-reference is
inside an [expression subquery][expression-subquery-concepts]

```googlesql {.bad}
WITH RECURSIVE
  T1 AS ((SELECT 1 AS n) UNION ALL (SELECT (SELECT n FROM T1)))
SELECT * FROM T1

-- Error
```

The following recursive CTE is disallowed because there is a
self-reference as an argument to a table-valued function (TVF).

```googlesql {.bad}
WITH RECURSIVE
  T1 AS (
    (SELECT 1 AS n) UNION ALL
    (SELECT * FROM MY_TVF(T1)))
SELECT * FROM T1;

-- Error
```

The following recursive CTE is disallowed because there is a
self-reference as input to an outer join.

```googlesql {.bad}
WITH RECURSIVE
  T0 AS (SELECT 1 AS n),
  T1 AS ((SELECT 1 AS n) UNION ALL (SELECT * FROM T1 FULL OUTER JOIN T0 USING (n)))
SELECT * FROM T1;

-- Error
```

The following recursive CTE is disallowed because you can't use aggregation
with a self-reference.

```googlesql {.bad}
WITH RECURSIVE
  T1 AS (
    (SELECT 1 AS n) UNION ALL
    (SELECT COUNT(*) FROM T1))
SELECT * FROM T1;

-- Error
```

The following recursive CTE is disallowed because you can't use the
window function `OVER` clause with a self-reference.

```googlesql {.bad}
WITH RECURSIVE
  T1 AS (
    (SELECT 1.0 AS n) UNION ALL
    SELECT 1 + AVG(n) OVER(ROWS between 2 PRECEDING and 0 FOLLOWING) FROM T1 WHERE n < 10)
SELECT n FROM T1;

-- Error
```

The following recursive CTE is disallowed because you can't use a
`LIMIT` clause with a self-reference.

```googlesql {.bad}
WITH RECURSIVE
  T1 AS ((SELECT 1 AS n) UNION ALL (SELECT n FROM T1 LIMIT 3))
SELECT * FROM T1;

-- Error
```

The following recursive CTEs are disallowed because you can't use an
`ORDER BY` clause with a self-reference.

```googlesql {.bad}
WITH RECURSIVE
  T1 AS ((SELECT 1 AS n) UNION ALL (SELECT n + 1 FROM T1 ORDER BY n))
SELECT * FROM T1;

-- Error
```

The following recursive CTE is disallowed because table `T1` can't be
recursively referenced from inside an inner `WITH` clause

```googlesql {.bad}
WITH RECURSIVE
  T1 AS ((SELECT 1 AS n) UNION ALL (WITH t AS (SELECT n FROM T1) SELECT * FROM t))
SELECT * FROM T1

-- Error
```

### CTE rules and constraints 
<a id="cte_rules"></a>

Common table expressions (CTEs) can be referenced inside the query expression
that contains the `WITH` clause.

##### General rules 
<a id="cte_general_rules"></a>

Here are some general rules and constraints to consider when working with CTEs:

+ Each CTE in the same `WITH` clause must have a unique name.
+ You must include the [`RECURSIVE` keyword][recursive-keyword] keyword if the
  `WITH` clause contains a recursive CTE.
+ The [`RECURSIVE` keyword][recursive-keyword] in
  the `WITH` clause changes the visibility of CTEs to other CTEs in the
  same `WITH` clause. You can learn more [here][recursive-keyword].
+ A local CTE overrides an outer CTE or table with the same name.
+ A CTE on a subquery may not reference correlated columns from the outer query.

##### Base term rules 
<a id="base_term_rules"></a>

The following rules apply to the base term in a recursive CTE:

+ The base term is required to be non-recursive.
+ The base term determines the names and types of all of the
  table columns.

##### Recursive term rules 
<a id="recursive_cte_rules"></a>

The following rules apply to the recursive term in a recursive CTE:

+ The recursive term must include exactly one reference to the
  recursively-defined table in the base term.
+ The recursive term must contain the same number of columns as the
  base term, and the type of each column must be implicitly coercible to
  the type of the corresponding column in the base term.
+ A recursive table reference can't be used as an operand to a `FULL JOIN`,
  a right operand to a `LEFT JOIN`, or a left operand to a `RIGHT JOIN`.
+ A recursive table reference can't be used with the `TABLESAMPLE` operator.
+ A recursive table reference can't be used as an operand to a
  table-valued function (TVF).

The following rules apply to a subquery inside a recursive term:

+ A subquery with a recursive table reference must be a `SELECT` expression,
  not a set operation, such as `UNION ALL`.
+ A subquery can't contain, directly or indirectly, a
  recursive table reference anywhere outside of its `FROM` clause.
+ A subquery with a recursive table reference can't contain an `ORDER BY` or
  `LIMIT` clause.
+ A subquery with a recursive table reference can't invoke aggregate functions.
+ A subquery with a recursive table reference can't invoke window functions.
+ A subquery with a recursive table reference can't contain the
  `DISTINCT` keyword or
  `GROUP BY` clause. To filter
  duplicates, use `UNION DISTINCT` in the top-level set operation,
  instead.

### CTE visibility 
<a id="cte_visibility"></a>

The visibility of a common table expression (CTE) within a query expression
is determined by whether or not you add the `RECURSIVE` keyword to the
`WITH` clause where the CTE was defined. You can learn more about these
differences in the following sections.

#### Visibility of CTEs in a `WITH` clause with the `RECURSIVE` keyword

When you include the `RECURSIVE` keyword, references between CTEs in the `WITH`
clause can go backwards and forwards. Cycles aren't allowed.

This is what happens when you have two CTEs that reference
themselves or each other in a `WITH` clause with the `RECURSIVE`
keyword. Assume that `A` is the first CTE and `B` is the second
CTE in the clause:

+ A references A = Valid
+ A references B = Valid
+ B references A = Valid
+ A references B references A = Invalid (cycles aren't allowed)

`A` can reference itself because self-references are supported:

```googlesql
WITH RECURSIVE
  A AS (SELECT 1 AS n UNION ALL (SELECT n + 1 FROM A WHERE n < 3))
SELECT * FROM A

/*---+
 | n |
 +---+
 | 1 |
 | 2 |
 | 3 |
 +---*/
```

`A` can reference `B` because references between CTEs can go forwards:

```googlesql
WITH RECURSIVE
  A AS (SELECT * FROM B),
  B AS (SELECT 1 AS n)
SELECT * FROM B

/*---+
 | n |
 +---+
 | 1 |
 +---*/
```

`B` can reference `A` because references between CTEs can go backwards:

```googlesql
WITH RECURSIVE
  A AS (SELECT 1 AS n),
  B AS (SELECT * FROM A)
SELECT * FROM B

/*---+
 | n |
 +---+
 | 1 |
 +---*/
```

This produces an error. `A` and `B` reference each other, which creates a cycle:

```googlesql
WITH RECURSIVE
  A AS (SELECT * FROM B),
  B AS (SELECT * FROM A)
SELECT * FROM B

-- Error
```

#### Visibility of CTEs in a `WITH` clause without the `RECURSIVE` keyword

When you don't include the `RECURSIVE` keyword in the `WITH` clause,
references between CTEs in the clause can go backward but not forward.

This is what happens when you have two CTEs that reference
themselves or each other in a `WITH` clause without
the `RECURSIVE` keyword. Assume that `A` is the first CTE and `B`
is the second CTE in the clause:

+ A references A = Invalid
+ A references B = Invalid
+ B references A = Valid
+ A references B references A = Invalid (cycles aren't allowed)

This produces an error. `A` can't reference itself because self-references
aren't supported:

```googlesql
WITH
  A AS (SELECT 1 AS n UNION ALL (SELECT n + 1 FROM A WHERE n < 3))
SELECT * FROM A

-- Error
```

This produces an error. `A` can't reference `B` because references between
CTEs can go backwards but not forwards:

```googlesql
WITH
  A AS (SELECT * FROM B),
  B AS (SELECT 1 AS n)
SELECT * FROM B

-- Error
```

`B` can reference `A` because references between CTEs can go backwards:

```googlesql
WITH
  A AS (SELECT 1 AS n),
  B AS (SELECT * FROM A)
SELECT * FROM B

/*---+
 | n |
 +---+
 | 1 |
 +---*/
```

This produces an error. `A` and `B` reference each other, which creates a
cycle:

```googlesql
WITH
  A AS (SELECT * FROM B),
  B AS (SELECT * FROM A)
SELECT * FROM B

-- Error
```

## `AGGREGATION_THRESHOLD` clause 
<a id="agg_threshold_clause"></a>

<pre>
WITH AGGREGATION_THRESHOLD OPTIONS (
  <span class="var">threshold</span> = <span class="var">threshold_amount</span>,
  <span class="var">privacy_unit_column</span> = <span class="var">column_name</span>
)
</pre>

**Description**

Use the `AGGREGATION_THRESHOLD` clause to enforce an
aggregation threshold. This clause counts the number of distinct privacy units
(represented by the privacy unit column) for each group, and only outputs the
groups where the distinct privacy unit count satisfies the
aggregation threshold.

**Definitions:**

+ `threshold`: The minimum number of distinct
  privacy units (privacy unit column values) that need to contribute to each row
  in the query results. If a potential row doesn't satisfy this threshold,
  that row is omitted from the query results. `threshold_amount` must be
  a positive `INT64` value.
+ `privacy_unit_column`: The column that represents the
  privacy unit column. Replace `column_name` with the
  path expression for the column. The first identifier in the path can start
  with either a table name or a column name that's visible in the
  `FROM` clause.

**Details**

<a id="agg_threshold_policy_functions"></a>

The following functions can be used on any column in a query with the
`AGGREGATION_THRESHOLD` clause, including the commonly used
`COUNT`, `SUM`, and `AVG` functions:

+ `APPROX_COUNT_DISTINCT`
+ `AVG`
+ `COUNT`
+ `COUNTIF`
+ `LOGICAL_OR`
+ `LOGICAL_AND`
+ `SUM`
+ `COVAR_SAMP`
+ `STDDEV_POP`
+ `STDDEV_SAMP`
+ `VAR_POP`
+ `VAR_SAMP`

**Example**

In the following example, an aggregation threshold is enforced
on a query. Notice that some privacy units are dropped because
there aren't enough distinct instances.

```googlesql
WITH ExamTable AS (
  SELECT "Hansen" AS last_name, "P91" AS test_id, 510 AS test_score UNION ALL
  SELECT "Wang", "U25", 500 UNION ALL
  SELECT "Wang", "C83", 520 UNION ALL
  SELECT "Wang", "U25", 460 UNION ALL
  SELECT "Hansen", "C83", 420 UNION ALL
  SELECT "Hansen", "C83", 560 UNION ALL
  SELECT "Devi", "U25", 580 UNION ALL
  SELECT "Devi", "P91", 480 UNION ALL
  SELECT "Ivanov", "U25", 490 UNION ALL
  SELECT "Ivanov", "P91", 540 UNION ALL
  SELECT "Silva", "U25", 550)
SELECT WITH AGGREGATION_THRESHOLD
  OPTIONS(threshold=3, privacy_unit_column=last_name)
  test_id,
  COUNT(DISTINCT last_name) AS student_count,
  AVG(test_score) AS avg_test_score
FROM ExamTable
GROUP BY test_id;

/*---------+---------------+----------------+
 | test_id | student_count | avg_test_score |
 +---------+---------------+----------------+
 | P91     | 3             | 510.0          |
 | U25     | 4             | 516.0          |
 +---------+---------------+----------------*/
```

## Differential privacy clause 
<a id="dp_clause"></a>

Warning: `ANONYMIZATION` has been deprecated. Use
`DIFFERENTIAL_PRIVACY` instead.

Warning: `kappa` has been deprecated. Use
`max_groups_contributed` instead.

Syntax for queries that support differential privacy with views:

<pre>
WITH DIFFERENTIAL_PRIVACY OPTIONS( <a href="#dp_privacy_parameters"><span class="var">privacy_parameters</span></a> )

<a href="#dp_privacy_parameters"><span class="var">privacy_parameters</span></a>:
  <span class="var">epsilon</span> = <span class="var">expression</span>,
  <span class="var">delta</span> = <span class="var">expression</span>,
  [ <span class="var">max_groups_contributed</span> = <span class="var">expression</span> ],
  [ <span class="var">group_selection_strategy</span> = { LAPLACE_THRESHOLD | PUBLIC_GROUPS } ]
</pre>

Syntax for queries that support differential privacy without views:

<pre>
WITH DIFFERENTIAL_PRIVACY OPTIONS( <a href="#dp_privacy_parameters"><span class="var">privacy_parameters</span></a> )

<a href="#dp_privacy_parameters"><span class="var">privacy_parameters</span></a>:
  <span class="var">epsilon</span> = <span class="var">expression</span>,
  <span class="var">delta</span> = <span class="var">expression</span>,
  [ <span class="var">max_groups_contributed</span> = <span class="var">expression</span> ],
  [ <span class="var">privacy_unit_column</span> = <span class="var">column_name</span> ]
</pre>

**Description**

This clause lets you transform the results of a query with
[differentially private aggregations][dp-functions]. To learn more about
differential privacy, see [Differential privacy][dp-concepts].

You can use the following syntax to build a differential privacy clause:

+  [`epsilon`][dp-epsilon]: Controls the amount of noise added to the results.
   A higher epsilon means less noise. `expression` must be a literal and
   return a `DOUBLE`.
+  [`delta`][dp-delta]: The probability that any row in the result fails to
   be epsilon-differentially private. `expression` must be a literal and return
   a `DOUBLE`.
+  [`max_groups_contributed`][dp-max-groups]: A positive integer identifying the
   limit on the number of groups that an entity is allowed to contribute to.
   This number is also used to scale the noise for each group. `expression` must
   be a literal and return an `INT64`.
+ [`privacy_unit_column`][dp-privacy-unit-id]: The column that represents the
  privacy unit column. Replace `column_name` with the path expression for the
  column. The first identifier in the path can start with either a table name
  or a column name that's visible in the `FROM` clause.
+ [`group_selection_strategy`][dp-group-selection-strategy]: Determines how
  differential privacy is applied to groups.

If you want to use this syntax, add it after the `SELECT` keyword with one or
more differentially private aggregate functions in the `SELECT` list.
To learn more about the privacy parameters in this syntax,
see [Privacy parameters][dp-privacy-parameters].

### Privacy parameters 
<a id="dp_privacy_parameters"></a>

Privacy parameters control how the results of a query are transformed.
Appropriate values for these settings can depend on many things such
as the characteristics of your data, the exposure level, and the
privacy level.

In this section, you can learn more about how you can use
privacy parameters to control how the results are transformed.

#### `epsilon` 
<a id="dp_epsilon"></a>

Noise is added primarily based on the specified `epsilon`
differential privacy parameter. The higher the epsilon the less noise is added.
More noise corresponding to smaller epsilons equals more privacy protection.

Noise can be eliminated by setting `epsilon` to `1e20`, which can be
useful during initial data exploration and experimentation with
differential privacy. Extremely large `epsilon` values, such as `1e308`,
cause query failure.

GoogleSQL splits `epsilon` between the differentially private
aggregates in the query. In addition to the explicit
differentially private aggregate functions, the differential privacy process
also injects an implicit differentially private aggregate into the plan for
removing small groups that computes a noisy entity count per group. If you have
`n` explicit differentially private aggregate functions in your query, then each
aggregate individually gets `epsilon/(n+1)` for its computation. If used with
`max_groups_contributed`, the effective `epsilon` per function per groups is
further split by `max_groups_contributed`. Additionally, if implicit clamping is
used for an aggregate differentially private function, then half of the
function's epsilon is applied towards computing implicit bounds, and half of the
function's epsilon is applied towards the differentially private aggregation
itself.

#### `delta` 
<a id="dp_delta"></a>

The `delta` differential privacy parameter represents the probability that any
row fails to be `epsilon`-differentially private in the result of a
differentially private query.

While doing testing or initial data exploration, it's often useful to set
`delta` to a value where all groups, including small groups, are
preserved. This removes privacy protection and should only be done when it
isn't necessary to protect query results, such as when working with non-private
test data. `delta` roughly corresponds to the probability of keeping a small
group.  In order to avoid losing small groups, set `delta` very close to 1,
for example `0.99999`.

#### `max_groups_contributed` 
<a id="dp_max_groups"></a>

The `max_groups_contributed` differential privacy parameter is a
positive integer that, if specified, scales the noise and limits the number of
groups that each entity can contribute to.

The default value for `max_groups_contributed` is determined by the
query engine.

If `max_groups_contributed` is unspecified, then there is no limit to the
number of groups that each entity can contribute to.

If `max_groups_contributed` is unspecified, the language can't guarantee that
the results will be differentially private. We recommend that you specify
`max_groups_contributed`. If you don't specify `max_groups_contributed`, the
results might still be differentially private if certain preconditions are met.
For example, if you know that the privacy unit column in a table or view is
unique in the `FROM` clause, the entity can't contribute to more than one group
and therefore the results will be the same regardless of whether
`max_groups_contributed` is set.

Tip: We recommend that engines require `max_groups_contributed` to be set.

#### `privacy_unit_column` 
<a id="dp_privacy_unit_id"></a>

To learn about the privacy unit and how to define a privacy unit column, see
[Define a privacy unit column][dp-define-privacy-unit-id].

#### `group_selection_strategy` 
<a id="dp_group_selection_strategy"></a>

Differential privacy queries can include a `GROUP BY` clause. By default,
including groups in a differentially private query requires the
differential privacy algorithm to protect the entities in these groups.

The `group_selection_strategy` privacy parameter determines how groups are
anonymized in a query. The choices are:

+   `LAPLACE_THRESHOLD` (default): Groups are anonymized by counting distinct
    privacy units that contributed to the group, adding laplace noise to the
    count, and then adding the group from the results if the noised value is
    above a certain threshold. This process depends on epsilon and delta.

    If the `group_selection_strategy` parameter isn't included in
    the `WITH DIFFERENTIAL_PRIVACY` clause, this is the default setting.
+   `PUBLIC_GROUPS`: Groups won't be anonymized because they don't depend upon
    protected data or they have already been anonymized. Use this option if the
    groups in the `GROUP BY` clause don't depend on any private data, and no
    further anonymization is needed for the groups. For example, if the groups
    contain fixed data such as the days in a week or operating system types,
    and this data doesn't need to be private, use this option.

Semantic rules for `PUBLIC_GROUPS`:

+   In the query, there should be a _public table_, which is a table
    without a privacy unit ID column. This table should contain
    data that's common knowledge or data that has already been anonymized.
+   In the query, there must be a _private table_, which is a table with a
    privacy unit ID column. This table might contain identifying data.
+   In the `GROUP BY` clause, all grouping items must come from a
    public table.
+   You must join a public table and a private table with a _public group join_,
    but conditions apply.

    +   Only `LEFT OUTER JOIN` and `RIGHT OUTER JOIN` are supported.

    +   You must include a `SELECT DISTINCT` subquery on the side of the join
        with the public group table.

    +   The public group join must join a public table with a private table.

    +   The public group join must join on each grouping item.

    +   If a join doesn't fulfill the previous requirements, it's treated as a
        normal join in the query and not counted as a public group join for
        any of the joined columns.
+   For every grouping item there must be a public group join,
    otherwise, an error is returned.
+   To obtain accurate results in a differentially private query with
    public groups, `NULL` privacy units shouldn't be present in the query.

### Differential privacy examples

This section contains examples that illustrate how to work with
differential privacy in GoogleSQL.

#### Tables for examples 
<a id="dp_example_tables"></a>

The examples in this section reference the following tables:

```googlesql
CREATE OR REPLACE TABLE {{USERNAME}}.professors AS (
  SELECT 101 AS id, "pencil" AS item, 24 AS quantity UNION ALL
  SELECT 123, "pen", 16 UNION ALL
  SELECT 123, "pencil", 10 UNION ALL
  SELECT 123, "pencil", 38 UNION ALL
  SELECT 101, "pen", 19 UNION ALL
  SELECT 101, "pen", 23 UNION ALL
  SELECT 130, "scissors", 8 UNION ALL
  SELECT 150, "pencil", 72);
```

```googlesql
CREATE OR REPLACE TABLE {{USERNAME}}.students AS (
  SELECT 1 AS id, "pencil" AS item, 5 AS quantity UNION ALL
  SELECT 1, "pen", 2 UNION ALL
  SELECT 2, "pen", 1 UNION ALL
  SELECT 3, "pen", 4);
```

#### Views for examples 
<a id="dp_example_views"></a>

The examples in this section reference these views:

```googlesql
CREATE OR REPLACE VIEW {{USERNAME}}.view_on_professors
OPTIONS(anonymization_userid_column='id')
AS (SELECT * FROM {{USERNAME}}.professors);
```

```googlesql
CREATE OR REPLACE VIEW {{USERNAME}}.view_on_students
OPTIONS(anonymization_userid_column='id')
AS (SELECT * FROM {{USERNAME}}.students);
```

These views reference the [professors][dp-example-tables] and
[students][dp-example-tables] example tables.

#### Add noise 
<a id="add_noise"></a>

You can add noise to a differentially private query. Smaller groups might not be
included. Smaller epsilons and more noise will provide greater
privacy protection.

```googlesql
-- This gets the average number of items requested per professor and adds
-- noise to the results
SELECT
  WITH DIFFERENTIAL_PRIVACY
    OPTIONS(epsilon=10, delta=.01, max_groups_contributed=2)
    item,
    AVG(quantity, contribution_bounds_per_group => (0,100)) AS average_quantity
FROM {{USERNAME}}.view_on_professors
GROUP BY item;

-- These results will change each time you run the query.
-- The scissors group was removed this time, but might not be
-- removed the next time.
/*----------+------------------+
 | item     | average_quantity |
 +----------+------------------+
 | pencil   | 38.5038356810269 |
 | pen      | 13.4725028762032 |
 +----------+------------------*/
```

```googlesql
-- This gets the average number of items requested per professor and adds
-- noise to the results
SELECT
  WITH DIFFERENTIAL_PRIVACY
    OPTIONS(epsilon=10, delta=.01, max_groups_contributed=2, privacy_unit_column=id)
    item,
    AVG(quantity, contribution_bounds_per_group => (0,100)) AS average_quantity
FROM professors
GROUP BY item;

-- These results will change each time you run the query.
-- The scissors group was removed this time, but might not be
-- removed the next time.
/*----------+------------------+
 | item     | average_quantity |
 +----------+------------------+
 | pencil   | 38.5038356810269 |
 | pen      | 13.4725028762032 |
 +----------+------------------*/
```

#### Remove noise 
<a id="eliminate_noise"></a>

Removing noise removes privacy protection. Only remove noise for
testing queries on non-private data. When `epsilon` is high, noise is removed
from the results.

```googlesql
-- This gets the average number of items requested per professor and removes
-- noise from the results
SELECT
  WITH DIFFERENTIAL_PRIVACY
    OPTIONS(epsilon=1e20, delta=.01, max_groups_contributed=2)
    item,
    AVG(quantity, contribution_bounds_per_group => (0,100)) AS average_quantity
FROM {{USERNAME}}.view_on_professors
GROUP BY item;

/*----------+------------------+
 | item     | average_quantity |
 +----------+------------------+
 | pencil   | 40               |
 | pen      | 18.5             |
 | scissors | 8                |
 +----------+------------------*/
```

```googlesql
-- This gets the average number of items requested per professor and removes
-- noise from the results
SELECT
  WITH DIFFERENTIAL_PRIVACY
    OPTIONS(epsilon=1e20, delta=.01, max_groups_contributed=2, privacy_unit_column=id)
    item,
    AVG(quantity, contribution_bounds_per_group => (0,100)) AS average_quantity
FROM professors
GROUP BY item;

/*----------+------------------+
 | item     | average_quantity |
 +----------+------------------+
 | pencil   | 40               |
 | pen      | 18.5             |
 | scissors | 8                |
 +----------+------------------*/
```

#### Limit the groups in which a privacy unit ID can exist 
<a id="limit_groups_for_privacy_unit"></a>

A privacy unit column can exist within multiple groups. For example, in the
`professors` table, the privacy unit column `123` exists in the `pencil` and
`pen` group. You can set `max_groups_contributed` to different values to limit how many
groups each privacy unit column will be included in.

```googlesql
SELECT
  WITH DIFFERENTIAL_PRIVACY
    OPTIONS(epsilon=1e20, delta=.01, max_groups_contributed=1)
    item,
    AVG(quantity, contribution_bounds_per_group => (0,100)) AS average_quantity
FROM {{USERNAME}}.view_on_professors
GROUP BY item;

-- The privacy unit ID 123 was only included in the pen group in this example.
-- Noise was removed from this query for demonstration purposes only.
/*----------+------------------+
 | item     | average_quantity |
 +----------+------------------+
 | pencil   | 40               |
 | pen      | 18.5             |
 | scissors | 8                |
 +----------+------------------*/
```

```googlesql
SELECT
  WITH DIFFERENTIAL_PRIVACY
    OPTIONS(epsilon=1e20, delta=.01, privacy_unit_column=id)
    item,
    AVG(quantity, contribution_bounds_per_group => (0,100)) AS average_quantity
FROM professors
GROUP BY item;

-- The privacy unit column 123 was only included in the pen group in this example.
-- Noise was removed from this query for demonstration purposes only.
/*----------+------------------+
 | item     | average_quantity |
 +----------+------------------+
 | pencil   | 40               |
 | pen      | 18.5             |
 | scissors | 8                |
 +----------+------------------*/
```

#### Use public groups in a differentially private query 
<a id="use_public_groups"></a>

In the following example, `UNNEST(["pen", "pencil", "book"])` isn't
anonymized because it's public knowledge and doesn't reveal any information
about user data. In the results, `scissors` is excluded because it's not in the
public table that's generated by the `UNNEST` operation.

```googlesql
-- Create the professors table (table to protect)
CREATE OR REPLACE TABLE {{USERNAME}}.professors AS (
  SELECT 101 AS id, "pencil" AS item, 24 AS quantity UNION ALL
  SELECT 123, "pen", 16 UNION ALL
  SELECT 123, "pencil", 10 UNION ALL
  SELECT 123, "pencil", 38 UNION ALL
  SELECT 101, "pen", 19 UNION ALL
  SELECT 101, "pen", 23 UNION ALL
  SELECT 130, "scissors", 8 UNION ALL
  SELECT 150, "pencil", 72);

-- Create the professors view
CREATE OR REPLACE VIEW {{USERNAME}}.view_on_professors
OPTIONS(anonymization_userid_column='id')
AS (SELECT * FROM {{USERNAME}}.professors);

-- Run the DIFFERENTIAL_PRIVACY query
SELECT WITH DIFFERENTIAL_PRIVACY
  OPTIONS (
    epsilon = 1e20,
    delta = .01,
    max_groups_contributed = 1,
    group_selection_strategy = PUBLIC_GROUPS
  )
  item,
  AVG(quantity) AS average_quantity
FROM {{USERNAME}}.view_on_professors
RIGHT OUTER JOIN
(SELECT DISTINCT item FROM UNNEST(['pen', 'pencil', 'book']))
USING (item)
GROUP BY item;

-- The privacy unit ID 123 was only included in the pen group in this example.
-- Noise was removed from this query for demonstration purposes only.
/*----------+------------------+
 | item     | average_quantity |
 +----------+------------------+
 | pencil   | 40               |
 | pen      | 18.5             |
 | book     | 0                |
 +----------+------------------*/
```

## Using aliases 
<a id="using_aliases"></a>

An alias is a temporary name given to a table, column, or expression present in
a query. You can introduce explicit aliases in the `SELECT` list or `FROM`
clause, or GoogleSQL infers an implicit alias for some expressions.
Expressions with neither an explicit nor implicit alias are anonymous and the
query can't reference them by name.

### Explicit aliases 
<a id="explicit_alias_syntax"></a>

You can introduce explicit aliases in either the `FROM` clause or the `SELECT`
list.

In a `FROM` clause, you can introduce explicit aliases for any item, including
tables, arrays, subqueries, and `UNNEST` clauses, using `[AS] alias`.  The `AS`
keyword is optional.

Example:

```googlesql
SELECT s.FirstName, s2.SongName
FROM Singers AS s, (SELECT * FROM Songs) AS s2;
```

You can introduce explicit aliases for any expression in the `SELECT` list using
`[AS] alias`. The `AS` keyword is optional.

Example:

```googlesql
SELECT s.FirstName AS name, LOWER(s.FirstName) AS lname
FROM Singers s;
```

### Implicit aliases 
<a id="implicit_aliases"></a>

In the `SELECT` list, if there is an expression that doesn't have an explicit
alias, GoogleSQL assigns an implicit alias according to the following
rules. There can be multiple columns with the same alias in the `SELECT` list.

+  For identifiers, the alias is the identifier. For example, `SELECT abc`
   implies `AS abc`.
+  For path expressions, the alias is the last identifier in the path. For
   example, `SELECT abc.def.ghi` implies `AS ghi`.
+  For field access using the "dot" member field access operator, the alias is
   the field name. For example, `SELECT (struct_function()).fname` implies `AS
   fname`.

In all other cases, there is no implicit alias, so the column is anonymous and
can't be referenced by name. The data from that column will still be returned
and the displayed query results may have a generated label for that column, but
the label can't be used like an alias.

In a `FROM` clause, `from_item`s aren't required to have an alias. The
following rules apply:

<ul>
  <li>
    If there is an expression that doesn't have an explicit alias,
    GoogleSQL assigns an implicit alias in these cases:
    <ul>
    <li>
      For identifiers, the alias is the identifier. For example,
      <code>FROM abc</code> implies <code>AS abc</code>.
    </li>
    <li>
      For path expressions, the alias is the last identifier in the path. For
      example, <code>FROM abc.def.ghi</code> implies <code>AS ghi</code>
    </li>
    <li>
      The column produced using <code>WITH OFFSET</code> has the implicit alias
      <code>offset</code>.
    </li>
    </ul>
  </li>
  <li>
    Table subqueries don't have implicit aliases.
  </li>
  <li>
    <code>FROM UNNEST(x)</code> doesn't have an implicit alias.
  </li>
</ul>

### Alias visibility 
<a id="alias_visibility"></a>

After you introduce an explicit alias in a query, there are restrictions on
where else in the query you can reference that alias. These restrictions on
alias visibility are the result of GoogleSQL name scoping rules.

#### Visibility in the `FROM` clause 
<a id="from_clause_aliases"></a>

GoogleSQL processes aliases in a `FROM` clause from left to right,
and aliases are visible only to subsequent path expressions in a `FROM`
clause.

Example:

Assume the `Singers` table had a `Concerts` column of `ARRAY` type.

```googlesql
SELECT FirstName
FROM Singers AS s, s.Concerts;
```

Invalid:

```googlesql {.bad}
SELECT FirstName
FROM s.Concerts, Singers AS s;  // INVALID.
```

`FROM` clause aliases are **not** visible to subqueries in the same `FROM`
clause. Subqueries in a `FROM` clause can't contain correlated references to
other tables in the same `FROM` clause.

Invalid:

```googlesql {.bad}
SELECT FirstName
FROM Singers AS s, (SELECT (2020 - ReleaseDate) FROM s)  // INVALID.
```

You can use any column name from a table in the `FROM` as an alias anywhere in
the query, with or without qualification with the table name.

Example:

```googlesql
SELECT FirstName, s.ReleaseDate
FROM Singers s WHERE ReleaseDate = 1975;
```

If the `FROM` clause contains an explicit alias, you must use the explicit alias
instead of the implicit alias for the remainder of the query (see
[Implicit Aliases][implicit-aliases]). A table alias is useful for brevity or
to eliminate ambiguity in cases such as self-joins, where the same table is
scanned multiple times during query processing.

Example:

```googlesql
SELECT * FROM Singers as s, Songs as s2
ORDER BY s.LastName
```

Invalid &mdash; `ORDER BY` doesn't use the table alias:

```googlesql {.bad}
SELECT * FROM Singers as s, Songs as s2
ORDER BY Singers.LastName;  // INVALID.
```

#### Visibility in the `SELECT` list 
<a id="select-list_aliases"></a>

Aliases in the `SELECT` list are visible only to the following clauses:

+  `GROUP BY` clause
+  `ORDER BY` clause
+  `HAVING` clause

Example:

```googlesql
SELECT LastName AS last, SingerID
FROM Singers
ORDER BY last;
```

#### Visibility in the `GROUP BY`, `ORDER BY`, and `HAVING` clauses 
<a id="aliases_clauses"></a>

These three clauses, `GROUP BY`, `ORDER BY`, and `HAVING`, can refer to only the
following values:

+  Tables in the `FROM` clause and any of their columns.
+  Aliases from the `SELECT` list.

`GROUP BY` and `ORDER BY` can also refer to a third group:

+  Integer literals, which refer to items in the `SELECT` list. The integer `1`
   refers to the first item in the `SELECT` list, `2` refers to the second item,
   etc.

Example:

```googlesql
SELECT SingerID AS sid, COUNT(Songid) AS s2id
FROM Songs
GROUP BY 1
ORDER BY 2 DESC;
```

The previous query is equivalent to:

```googlesql
SELECT SingerID AS sid, COUNT(Songid) AS s2id
FROM Songs
GROUP BY sid
ORDER BY s2id DESC;
```

### Duplicate aliases 
<a id="duplicate_aliases"></a>

A `SELECT` list or subquery containing multiple explicit or implicit aliases
of the same name is allowed, as long as the alias name isn't referenced
elsewhere in the query, since the reference would be
[ambiguous][ambiguous-aliases].

Example:

```googlesql
SELECT 1 AS a, 2 AS a;

/*---+---+
 | a | a |
 +---+---+
 | 1 | 2 |
 +---+---*/
```

### Ambiguous aliases 
<a id="ambiguous_aliases"></a>

GoogleSQL provides an error if accessing a name is ambiguous, meaning
it can resolve to more than one unique object in the query or in a table schema,
including the schema of a destination table.

The following query contains column names that conflict between tables, since
both `Singers` and `Songs` have a column named `SingerID`:

```googlesql
SELECT SingerID
FROM Singers, Songs;
```

The following query contains aliases that are ambiguous in the `GROUP BY` clause
because they are duplicated in the `SELECT` list:

```googlesql {.bad}
SELECT FirstName AS name, LastName AS name,
FROM Singers
GROUP BY name;
```

The following query contains aliases that are ambiguous in the `SELECT` list and
`FROM` clause because they share a column and field with same name.

+ Assume the `Person` table has three columns: `FirstName`,
  `LastName`, and `PrimaryContact`.
+ Assume the `PrimaryContact` column represents a struct with these fields:
  `FirstName` and `LastName`.

The alias `P` is ambiguous and will produce an error because `P.FirstName` in
the `GROUP BY` clause could refer to either `Person.FirstName` or
`Person.PrimaryContact.FirstName`.

```googlesql {.bad}
SELECT FirstName, LastName, PrimaryContact AS P
FROM Person AS P
GROUP BY P.FirstName;
```

A name is _not_ ambiguous in `GROUP BY`, `ORDER BY` or `HAVING` if it's both
a column name and a `SELECT` list alias, as long as the name resolves to the
same underlying object. In the following example, the alias `BirthYear` isn't
ambiguous because it resolves to the same underlying column,
`Singers.BirthYear`.

```googlesql
SELECT LastName, BirthYear AS BirthYear
FROM Singers
GROUP BY BirthYear;
```

### Range variables 
<a id="range_variables"></a>

In GoogleSQL, a range variable is a table expression alias in the
`FROM` clause. Sometimes a range variable is known as a `table alias`. A
range variable lets you reference rows being scanned from a table expression.
A table expression represents an item in the `FROM` clause that returns a table.
Common items that this expression can represent include
tables,
[value tables][value-tables],
[subqueries][subquery-concepts],
[table-valued functions (TVFs)][tvf-concepts],
[joins][query-joins], and [parenthesized joins][query-joins].

In general, a range variable provides a reference to the rows of a table
expression. A range variable can be used to qualify a column reference and
unambiguously identify the related table, for example `range_variable.column_1`.

When referencing a range variable on its own without a specified column suffix,
the result of a table expression is the row type of the related table.
Value tables have explicit row types, so for range variables related
to value tables, the result type is the value table's row type. Other tables
don't have explicit row types, and for those tables, the range variable
type is a dynamically defined struct that includes all of the
columns in the table.

**Examples**

In these examples, the `WITH` clause is used to emulate a temporary table
called `Grid`. This table has columns `x` and `y`. A range variable called
`Coordinate` refers to the current row as the table is scanned. `Coordinate`
can be used to access the entire row or columns in the row.

The following example selects column `x` from range variable `Coordinate`,
which in effect selects column `x` from table `Grid`.

```googlesql
WITH Grid AS (SELECT 1 x, 2 y)
SELECT Coordinate.x FROM Grid AS Coordinate;

/*---+
 | x |
 +---+
 | 1 |
 +---*/
```

The following example selects all columns from range variable `Coordinate`,
which in effect selects all columns from table `Grid`.

```googlesql
WITH Grid AS (SELECT 1 x, 2 y)
SELECT Coordinate.* FROM Grid AS Coordinate;

/*---+---+
 | x | y |
 +---+---+
 | 1 | 2 |
 +---+---*/
```

The following example selects the range variable `Coordinate`, which is a
reference to rows in table `Grid`. Since `Grid` isn't a value table,
the result type of `Coordinate` is a struct that contains all the columns
from `Grid`.

```googlesql
WITH Grid AS (SELECT 1 x, 2 y)
SELECT Coordinate FROM Grid AS Coordinate;

/*--------------+
 | Coordinate   |
 +--------------+
 | {x: 1, y: 2} |
 +--------------*/
```

## Table function calls

To call a TVF, use the function call in place of the table name in a `FROM`
clause.

There are two ways to pass a table as an argument to a TVF. You can use a
subquery for the table argument, or you can use the name of a table, preceded by
the keyword `TABLE`.

**Examples**

The following query calls the `CustomerRangeWithCustomerType` function to
return a table with rows for customers with a CustomerId between 100
and 200. The call doesn't include the `customer_type` argument, so the function
uses the default `CUSTOMER_TYPE_ADVERTISER`.

```googlesql
SELECT CustomerId, Info
FROM CustomerRangeWithCustomerType(100, 200);
```

The following query calls the `CustomerCreationTimeRange` function defined
previously, passing the result of a subquery as the table argument.

```googlesql
SELECT *
FROM
  CustomerCreationTimeRange(
    1577836800,  -- 2020-01-01 00:00:00 UTC
    1609459199,  -- 2020-12-31 23:59:59 UTC
    (
      SELECT customer_id, customer_name, creation_time
      FROM MyCustomerTable
      WHERE customer_name LIKE '%Hernández'
    ))
```

The following query calls `CustomerCreationTimeRange`, passing the table
`MyCustomerTable` as an argument.

```googlesql
SELECT *
FROM
  CustomerCreationTimeRange(
    1577836800,  -- 2020-01-01 00:00:00 UTC
    1609459199,  -- 2020-12-31 23:59:59 UTC
    TABLE MyCustomerTable)
```

Note: For definitions for `CustomerRangeWithCustomerType` and
`CustomerCreationTimeRange`, see [Specify TVF arguments][tvf-arguments].

## Appendix A: examples with sample data 
<a id="appendix_a_examples_with_sample_data"></a>

These examples include statements which perform queries on the
[`Roster`][roster-table] and [`TeamMascot`][teammascot-table],
and [`PlayerStats`][playerstats-table] tables.

### Sample tables 
<a id="sample_tables"></a>

The following tables are used to illustrate the behavior of different
query clauses in this reference.

#### Roster table

The `Roster` table includes a list of player names (`LastName`) and the
unique ID assigned to their school (`SchoolID`). It looks like this:

```googlesql
/*-----------------------+
 | LastName   | SchoolID |
 +-----------------------+
 | Adams      | 50       |
 | Buchanan   | 52       |
 | Coolidge   | 52       |
 | Davis      | 51       |
 | Eisenhower | 77       |
 +-----------------------*/
```

You can use this `WITH` clause to emulate a temporary table name for the
examples in this reference:

```googlesql
WITH Roster AS
 (SELECT 'Adams' as LastName, 50 as SchoolID UNION ALL
  SELECT 'Buchanan', 52 UNION ALL
  SELECT 'Coolidge', 52 UNION ALL
  SELECT 'Davis', 51 UNION ALL
  SELECT 'Eisenhower', 77)
SELECT * FROM Roster
```

#### PlayerStats table

The `PlayerStats` table includes a list of player names (`LastName`) and the
unique ID assigned to the opponent they played in a given game (`OpponentID`)
and the number of points scored by the athlete in that game (`PointsScored`).

```googlesql
/*----------------------------------------+
 | LastName   | OpponentID | PointsScored |
 +----------------------------------------+
 | Adams      | 51         | 3            |
 | Buchanan   | 77         | 0            |
 | Coolidge   | 77         | 1            |
 | Adams      | 52         | 4            |
 | Buchanan   | 50         | 13           |
 +----------------------------------------*/
```

You can use this `WITH` clause to emulate a temporary table name for the
examples in this reference:

```googlesql
WITH PlayerStats AS
 (SELECT 'Adams' as LastName, 51 as OpponentID, 3 as PointsScored UNION ALL
  SELECT 'Buchanan', 77, 0 UNION ALL
  SELECT 'Coolidge', 77, 1 UNION ALL
  SELECT 'Adams', 52, 4 UNION ALL
  SELECT 'Buchanan', 50, 13)
SELECT * FROM PlayerStats
```

#### TeamMascot table

The `TeamMascot` table includes a list of unique school IDs (`SchoolID`) and the
mascot for that school (`Mascot`).

```googlesql
/*---------------------+
 | SchoolID | Mascot   |
 +---------------------+
 | 50       | Jaguars  |
 | 51       | Knights  |
 | 52       | Lakers   |
 | 53       | Mustangs |
 +---------------------*/
```

You can use this `WITH` clause to emulate a temporary table name for the
examples in this reference:

```googlesql
WITH TeamMascot AS
 (SELECT 50 as SchoolID, 'Jaguars' as Mascot UNION ALL
  SELECT 51, 'Knights' UNION ALL
  SELECT 52, 'Lakers' UNION ALL
  SELECT 53, 'Mustangs')
SELECT * FROM TeamMascot
```

### `GROUP BY` clause 
<a id="group_by_clause_example"></a>

Example:

```googlesql
SELECT LastName, SUM(PointsScored)
FROM PlayerStats
GROUP BY LastName;
```

<table>
<thead>
<tr>
<th>LastName</th>
<th>SUM</th>
</tr>
</thead>
<tbody>
<tr>
<td>Adams</td>
<td>7</td>
</tr>
<tr>
<td>Buchanan</td>
<td>13</td>
</tr>
<tr>
<td>Coolidge</td>
<td>1</td>
</tr>
</tbody>
</table>

### `UNION` 
<a id="union_example"></a>

The `UNION` operator combines the result sets of two or more `SELECT` statements
by pairing columns from the result set of each `SELECT` statement and vertically
concatenating them.

Example:

```googlesql
SELECT Mascot AS X, SchoolID AS Y
FROM TeamMascot
UNION ALL
SELECT LastName, PointsScored
FROM PlayerStats;
```

Results:

<table>
<thead>
<tr>
<th>X</th>
<th>Y</th>
</tr>
</thead>
<tbody>
<tr>
<td>Jaguars</td>
<td>50</td>
</tr>
<tr>
<td>Knights</td>
<td>51</td>
</tr>
<tr>
<td>Lakers</td>
<td>52</td>
</tr>
<tr>
<td>Mustangs</td>
<td>53</td>
</tr>
<tr>
<td>Adams</td>
<td>3</td>
</tr>
<tr>
<td>Buchanan</td>
<td>0</td>
</tr>
<tr>
<td>Coolidge</td>
<td>1</td>
</tr>
<tr>
<td>Adams</td>
<td>4</td>
</tr>
<tr>
<td>Buchanan</td>
<td>13</td>
</tr>
</tbody>
</table>

### `INTERSECT` 
<a id="intersect_example"></a>

This query returns the last names that are present in both Roster and
PlayerStats.

```googlesql
SELECT LastName
FROM Roster
INTERSECT ALL
SELECT LastName
FROM PlayerStats;
```

Results:

<table>
<thead>
<tr>
<th>LastName</th>
</tr>
</thead>
<tbody>
<tr>
<td>Adams</td>
</tr>
<tr>
<td>Coolidge</td>
</tr>
<tr>
<td>Buchanan</td>
</tr>
</tbody>
</table>

### `EXCEPT` 
<a id="except_example"></a>

The query below returns last names in Roster that are **not** present in
PlayerStats.

```googlesql
SELECT LastName
FROM Roster
EXCEPT DISTINCT
SELECT LastName
FROM PlayerStats;
```

Results:

<table>
<thead>
<tr>
<th>LastName</th>
</tr>
</thead>
<tbody>
<tr>
<td>Eisenhower</td>
</tr>
<tr>
<td>Davis</td>
</tr>
</tbody>
</table>

Reversing the order of the `SELECT` statements will return last names in
PlayerStats that are **not** present in Roster:

```googlesql
SELECT LastName
FROM PlayerStats
EXCEPT DISTINCT
SELECT LastName
FROM Roster;
```

Results:

```googlesql
(empty)
```

<!-- mdlint off(WHITESPACE_LINE_LENGTH) -->

[iana-language-subtag-registry]: https://www.iana.org/assignments/language-subtag-registry/language-subtag-registry

[unicode-locale-identifier]: https://www.unicode.org/reports/tr35/#Unicode_locale_identifier

[tr35-collation-settings]: http://www.unicode.org/reports/tr35/tr35-collation.html#Setting_Options

[tr10-collation-algorithm]: http://www.unicode.org/reports/tr10/

[implicit-aliases]: #implicit_aliases

[using-aliases]: #using_aliases

[sequences-of-joins]: #sequences_of_joins

[set-operators]: #set_operators

[union-syntax]: #union

[join-hints]: #join_hints

[roster-table]: #roster_table

[playerstats-table]: #playerstats_table

[teammascot-table]: #teammascot_table

[stratified-sampling]: #stratified_sampling

[scaling-weight]: #scaling_weight

[query-joins]: #join_types

[ambiguous-aliases]: #ambiguous_aliases

[with-clause]: #with_clause

[cte-rules]: #cte_rules

[non-recursive-cte]: #simple_cte

[unnest-operator]: #unnest_operator

[cte-visibility]: #cte_visibility

[comma-cross-join]: #comma_cross_join

[cross-join]: #cross_join

[correlated-join]: #correlated_join

[on-clause]: #on_clause

[using-clause]: #using_clause

[explicit-implicit-unnest]: #explicit_implicit_unnest

[range-variables]: #range_variables

[group-by-groupable-item]: #group_by_grouping_item

[group-by-values]: #group_by_values

[group-by-col-ordinals]: #group_by_col_ordinals

[union]: #union

[by-name-or-corresponding]: #by_name_or_corresponding

[group-by-all]: #group_by_all

[aggregate-function-calls]: https://github.com/google/googlesql/blob/master/docs/aggregate-function-calls.md

[group-by-rollup]: #group_by_rollup

[group-by-cube]: #group_by_cube

[group-by-grouping-sets]: #group_by_grouping_sets

[contingency-table]: https://en.wikipedia.org/wiki/Contingency_table

[tuple-struct]: https://github.com/google/googlesql/blob/master/docs/data-types.md#tuple_syntax

[grouping-function]: https://github.com/google/googlesql/blob/master/docs/aggregate_functions.md#grouping

[pivot-operator]: #pivot_operator

[unpivot-operator]: #unpivot_operator

[tablesample-operator]: #tablesample_operator

[work-with-recursive-ctes]: https://github.com/google/googlesql/blob/master/docs/recursive-ctes.md

[recursive-keyword]: #recursive_keyword

[base-term-rules]: #base_term_rules

[recursive-cte-rules]: #recursive_cte_rules

[recursive-cte]: #recursive_cte

[window-function-calls]: https://github.com/google/googlesql/blob/master/docs/window-function-calls.md

[query-window-specification]: https://github.com/google/googlesql/blob/master/docs/window-function-calls.md#def_window_spec

[named-window-example]: https://github.com/google/googlesql/blob/master/docs/window-function-calls.md#def_use_named_window

[produce-table]: https://github.com/google/googlesql/blob/master/docs/window-function-calls.md#produce_table

[tvf-concepts]: https://github.com/google/googlesql/blob/master/docs/table-functions.md#tvfs

[tvf-arguments]: https://github.com/google/googlesql/blob/master/docs/table-functions.md#tvf_arguments

[dp-concepts]: https://github.com/google/googlesql/blob/master/docs/differential-privacy.md

[flattening-arrays]: https://github.com/google/googlesql/blob/master/docs/arrays.md#flattening_arrays

[working-with-arrays]: https://github.com/google/googlesql/blob/master/docs/arrays.md

[data-type-properties]: https://github.com/google/googlesql/blob/master/docs/data-types.md#data_type_properties

[orderable-data-types]: https://github.com/google/googlesql/blob/master/docs/data-types.md#orderable_data_types

[subquery-concepts]: https://github.com/google/googlesql/blob/master/docs/subqueries.md

[correlated-subquery]: https://github.com/google/googlesql/blob/master/docs/subqueries.md#correlated_subquery_concepts

[table-subquery-concepts]: https://github.com/google/googlesql/blob/master/docs/subqueries.md#table_subquery_concepts

[expression-subquery-concepts]: https://github.com/google/googlesql/blob/master/docs/subqueries.md#expression_subquery_concepts

[create-view-statement]: https://github.com/google/googlesql/blob/master/docs/data-definition-language.md#create_view_statement

[in-operator]: https://github.com/google/googlesql/blob/master/docs/operators.md#in_operators

[array-subscript-operator]: https://github.com/google/googlesql/blob/master/docs/operators.md#array_subscript_operator

[field-access-operator]: https://github.com/google/googlesql/blob/master/docs/operators.md#field_access_operator

[comparison-operators]: https://github.com/google/googlesql/blob/master/docs/operators.md#comparison_operators

[proto-buffers]: https://github.com/google/googlesql/blob/master/docs/protocol-buffers.md

[flattening-trees-into-table]: https://github.com/google/googlesql/blob/master/docs/arrays.md#flattening_nested_data_into_table

[flatten-operator]: https://github.com/google/googlesql/blob/master/docs/array_functions.md#flatten

[array-el-field-operator]: https://github.com/google/googlesql/blob/master/docs/operators.md#array_el_field_operator

[collation-spec]: https://github.com/google/googlesql/blob/master/docs/collation-concepts.md#collate_spec_details

[value-tables]: https://github.com/google/googlesql/blob/master/docs/data-model.md#value_tables

[dp-example-tables]: #dp_example_tables

[dp-privacy-parameters]: #dp_privacy_parameters

[dp-epsilon]: #dp_epsilon

[dp-max-groups]: #dp_max_groups

[dp-delta]: #dp_delta

[dp-privacy-unit-id]: #dp_privacy_unit_id

[dp-group-selection-strategy]: #dp_group_selection_strategy

[dp-define-privacy-unit-id]: https://github.com/google/googlesql/blob/master/docs/differential-privacy.md#dp_define_privacy_unit_id

[dp-functions]: https://github.com/google/googlesql/blob/master/docs/aggregate-dp-functions.md

[analysis-rules]: https://github.com/google/googlesql/blob/master/docs/analysis-rules.md

[privacy-view]: https://github.com/google/googlesql/blob/master/docs/analysis-rules.md#privacy_view

[graph-table-operator]: https://github.com/google/googlesql/blob/master/docs/graph-sql-queries.md#graph_table_operator

[graph-hints-gql]: https://github.com/google/googlesql/blob/master/docs/graph-query-statements.md#graph_hints

[coalesce]: https://github.com/google/googlesql/blob/master/docs/conditional_expressions.md#coalesce

<!-- mdlint on -->

