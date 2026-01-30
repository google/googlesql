

<!-- mdlint off(WHITESPACE_LINE_LENGTH) -->

# Pipe query syntax

Pipe query syntax is an extension to GoogleSQL that's simpler and more
concise than [standard query syntax][query-syntax]. Pipe syntax supports the
same operations as standard syntax, and improves some areas of SQL query
functionality and usability.

For more background and details on pipe syntax design, see the research paper
[SQL Has Problems. We Can Fix Them: Pipe Syntax In SQL][pipe-syntax-paper]{: .external}.
For an introduction to pipe syntax, see
[Work with pipe syntax][pipe-syntax-guide].

## Pipe syntax 
<a id="pipe_syntax"></a>

Pipe syntax has the following key characteristics:

+   Each pipe operator in pipe syntax consists of the pipe symbol, `|>`,
   an operator name, and any arguments: \
    `|> operator_name argument_list`
+   Pipe operators can be added to the end of any valid query.
+   Pipe syntax works anywhere standard syntax is supported: in queries, views,
    table-valued functions (TVFs), and other contexts.
+   Pipe syntax can be mixed with standard syntax in the same query. For
    example, subqueries can use different syntax from the parent query.
+   A pipe operator can see every alias that exists in the table
    preceding the pipe.
+   A query can [start with a `FROM` clause][from-queries], and pipe
    operators can optionally be added after the `FROM` clause.

### Query comparison

Consider the following table called `Produce`:

```googlesql
CREATE OR REPLACE TABLE Produce AS (
  SELECT 'apples' AS item, 2 AS sales, 'fruit' AS category
  UNION ALL
  SELECT 'carrots' AS item, 8 AS sales, 'vegetable' AS category
  UNION ALL
  SELECT 'apples' AS item, 7 AS sales, 'fruit' AS category
  UNION ALL
  SELECT 'bananas' AS item, 5 AS sales, 'fruit' AS category
);

SELECT * FROM Produce;

/*---------+-------+-----------+
 | item    | sales | category  |
 +---------+-------+-----------+
 | apples  | 2     | fruit     |
 | carrots | 8     | vegetable |
 | apples  | 7     | fruit     |
 | bananas | 5     | fruit     |
 +---------+-------+-----------*/
```

Compare the following equivalent queries that compute the number and total
amount of sales for each item in the `Produce` table:

**Standard syntax**

```googlesql
SELECT item, COUNT(*) AS num_items, SUM(sales) AS total_sales
FROM Produce
WHERE
  item != 'bananas'
  AND category IN ('fruit', 'nut')
GROUP BY item
ORDER BY item DESC;

/*--------+-----------+-------------+
 | item   | num_items | total_sales |
 +--------+-----------+-------------+
 | apples | 2         | 9           |
 +--------+-----------+-------------*/
```

**Pipe syntax**

```googlesql
FROM Produce
|> WHERE
    item != 'bananas'
    AND category IN ('fruit', 'nut')
|> AGGREGATE COUNT(*) AS num_items, SUM(sales) AS total_sales
   GROUP BY item
|> ORDER BY item DESC;

/*--------+-----------+-------------+
 | item   | num_items | total_sales |
 +--------+-----------+-------------+
 | apples | 2         | 9           |
 +--------+-----------+-------------*/
```

## Pipe operator semantics 
<a id="pipe_semantics"></a>

Pipe operators have the following semantic behavior:

+   Each pipe operator performs a self-contained operation.
+   A pipe operator consumes the input table passed to it through the pipe
    symbol, `|>`, and produces a new table as output.
+   A pipe operator can reference only columns from its immediate input table.
    Columns from earlier in the same query aren't visible. Inside subqueries,
    correlated references to outer columns are still allowed.

### Terminal operators {: #terminal_operators}

Certain pipe operators are *terminal operators*. These operators appear at the
end of a pipe query and typically consume the final query result.

Terminal operators have the following semantic behavior:

+ They can only appear at the end of a pipe query.
+ They consume the pipe input table and return no result.
+ They are only allowed in the outermost query of a query
  statement. They can't be used inside a [subquery][subqueries] or in
  non-query statements.

For supported terminal operators, see [Pipe operators][pipe-operators].

### Semi-terminal operators {: #semi_terminal_operators}

Certain pipe operators are *semi-terminal operators*. Like
[terminal operators][terminal-operators], semi-terminal operators can only
appear in the outermost query of a query statement. Unlike terminal operators,
semi-terminal operators return a result table and can be followed by other
pipe operators.

Semi-terminal operators have the following semantic behavior:

+ They consume the pipe input table and return a result.
+ They are only allowed in the outermost query of a query
  statement. They can't be used inside a [subquery][subqueries] or in
  non-query statements.

For supported semi-terminal operators, see [Pipe operators][pipe-operators].

## Subpipelines 
<a id="subpipelines"></a>

The parts of a query that use pipe operators are sometimes called *pipelines*.
Nested pipelines within a pipeline are called *subpipelines*. Subpipelines are
similar to [subqueries][subqueries], but can only contain pipe operators rather
than a full query. A subpipeline doesn't begin with an initial `FROM` or
`SELECT` clause.

The syntax for subpipelines consists of zero or more pipe operators surrounded
by `()`:

<pre class="lang-sql prettyprint no-copy">
(
  |> pipe_operator1
  |> pipe_operator2
  ...
)
</pre>

Subpipelines have the following key characteristics:

+ Only specific pipe operators use subpipelines. Those operators take
  subpipelines for one or more of their arguments.
+ Operators that use subpipelines define a table that's implicitly passed as
  input to the subpipeline, as if that table were scanned with a `FROM` clause.
+ The operator defines whether that subpipeline operates over the input table
  just once or multiple times. If the subpipeline runs more than once, it might
  operate over different rows each time.
+ Empty subpipelines are allowed, and are written as `()`. These subpipelines
  return their input table without changes.

**Example**

This query shows a subpipeline used in the `IF` pipe operator:

```googlesql
FROM Produce
|> IF true THEN
  (
    |> AGGREGATE COUNT(*) AS total
  ) ELSE
  (
    |> SELECT *
  )
|> AS Inventory;
```

In this example, the first subpipeline case (`AGGREGATE`) runs because the
condition is true. If the condition were false, the second subpipeline
case (`SELECT`) would run.

The `IF` operator passes its input table to the case that runs. The subpipeline
output table is returned as the final output of the `IF` operator, which is
then passed to the `AS` operator.

## `FROM` queries 
<a id="from_queries"></a>

In pipe syntax, a query can start with a standard [`FROM` clause][from-clause]
and use any standard `FROM` syntax, including tables, joins, subqueries,
`UNNEST` operations, and
table-valued functions (TVFs). Table aliases can be
assigned to each input item using the [`AS alias` clause][using-aliases].

A query with only a `FROM` clause, like `FROM table_name`, is allowed in pipe
syntax and returns all rows from the table. For tables with columns,
`FROM table_name` in pipe syntax is similar to
[`SELECT * FROM table_name`][select-star] in standard syntax.
 For [value tables][value-tables], `FROM table_name` in
pipe syntax returns the row values without expanding fields, similar to
[`SELECT value FROM table_name AS value`][select-as-value] in standard
syntax.

**Examples**

The following queries use the [`Produce` table][query-comparison]:

```googlesql
FROM Produce;

/*---------+-------+-----------+
 | item    | sales | category  |
 +---------+-------+-----------+
 | apples  | 2     | fruit     |
 | carrots | 8     | vegetable |
 | apples  | 7     | fruit     |
 | bananas | 5     | fruit     |
 +---------+-------+-----------*/
```

```googlesql
-- Join tables in the FROM clause and then apply pipe operators.
FROM
  Produce AS p1
  JOIN Produce AS p2
    USING (item)
|> WHERE item = 'bananas'
|> SELECT p1.item, p2.sales;

/*---------+-------+
 | item    | sales |
 +---------+-------+
 | bananas | 5     |
 +---------+-------*/
```

## Pipe operators 
<a id="pipe_operators"></a>

GoogleSQL supports the following pipe operators. For operators that
correspond or relate to similar operations in standard syntax, the operator
descriptions highlight similarities and differences and link to more detailed
documentation on the corresponding syntax.

### Pipe operator list

<table>
  <thead>
    <tr>
      <th>Name</th>
      <th>Summary</th>
    </tr>
  </thead>
  <tbody>

<tr>
  <td><a href="#select_pipe_operator"><code>SELECT</code></a>
</td>
  <td>Produces a new table with the listed columns.</td>
</tr>

<tr>
  <td><a href="#extend_pipe_operator"><code>EXTEND</code></a>
</td>
  <td>Propagates the existing table and adds computed columns.</td>
</tr>

<tr>
  <td><a href="#set_pipe_operator"><code>SET</code></a>
</td>
  <td>Replaces the values of columns in the input table.</td>
</tr>

<tr>
  <td><a href="#drop_pipe_operator"><code>DROP</code></a>
</td>
  <td>Removes listed columns from the input table.</td>
</tr>

<tr>
  <td><a href="#rename_pipe_operator"><code>RENAME</code></a>
</td>
  <td>Renames specified columns.</td>
</tr>

<tr>
  <td><a href="#as_pipe_operator"><code>AS</code></a>
</td>
  <td>Introduces a table alias for the input table.</td>
</tr>

<tr>
  <td><a href="#where_pipe_operator"><code>WHERE</code></a>
</td>
  <td>Filters the results of the input table.</td>
</tr>

<tr>
  <td><a href="#aggregate_pipe_operator"><code>AGGREGATE</code></a>
</td>
  <td>
    Performs aggregation on data across groups of rows or the full
    input table.
  </td>
</tr>

<tr>
  <td><a href="#distinct_pipe_operator"><code>DISTINCT</code></a>
</td>
  <td>
    Returns distinct rows from the input table, while preserving table aliases.
  </td>
</tr>

<tr>
  <td><a href="#join_pipe_operator"><code>JOIN</code></a>
</td>
  <td>
    Joins rows from the input table with rows from a second table provided as an
    argument.
  </td>
</tr>

<tr>
  <td><a href="#call_pipe_operator"><code>CALL</code></a>
</td>
  <td>
    Calls a table-valued function (TVF), passing the pipe input table as a
    table argument.
  </td>
</tr>

<tr>
  <td><a href="#order_by_pipe_operator"><code>ORDER BY</code></a>
</td>
  <td>Sorts results by a list of expressions.</td>
</tr>

<tr>
  <td><a href="#limit_pipe_operator"><code>LIMIT</code></a>
</td>
  <td>
    Limits the number of rows to return in a query, with an optional
    <code>OFFSET</code> clause to skip over rows.
  </td>
</tr>

<tr>
  <td><a href="#union_pipe_operator"><code>UNION</code></a>
</td>
  <td>
    Returns the combined results of the input queries to the left and right of the pipe operator.
  </td>
</tr>

<tr>
  <td><a href="#recursive_union_pipe_operator"><code>RECURSIVE UNION</code></a>
</td>
  <td>Runs recursive queries.</td>
</tr>

<tr>
  <td><a href="#intersect_pipe_operator"><code>INTERSECT</code></a>
</td>
  <td>
    Returns rows that are found in the results of both the input query to the left
    of the pipe operator and all input queries to the right of the pipe
    operator.
  </td>
</tr>

<tr>
  <td><a href="#except_pipe_operator"><code>EXCEPT</code></a>
</td>
  <td>
    Returns rows from the input query to the left of the pipe operator that
    aren't present in any input queries to the right of the pipe operator.
  </td>
</tr>

<tr>
  <td><a href="#tablesample_pipe_operator"><code>TABLESAMPLE</code></a>
</td>
  <td>Selects a random sample of rows from the input table.</td>
</tr>

<tr>
  <td><a href="#with_pipe_operator"><code>WITH</code></a>
</td>
  <td>Introduces one or more common table expressions (CTEs).</td>
</tr>

<tr>
  <td><a href="#pivot_pipe_operator"><code>PIVOT</code></a>
</td>
  <td>Rotates rows into columns.</td>
</tr>

<tr>
  <td><a href="#unpivot_pipe_operator"><code>UNPIVOT</code></a>
</td>
  <td>Rotates columns into rows.</td>
</tr>

<!-- disableFinding(LINK_ID) -->

<tr>
  <td><a href="#window_pipe_operator"><code>WINDOW</code></a>
</td>
  <td>
    Deprecated. Adds columns with the result of computing the function
    over some window of existing rows.
  </td>
</tr>

<tr>
  <td><a href="#match_recognize_pipe_operator"><code>MATCH_RECOGNIZE</code></a>
</td>
  <td>Filters and aggregates rows based on matches.</td>
</tr>

<tr>
  <td><a href="#assert_pipe_operator"><code>ASSERT</code></a>
</td>
  <td>
    Evaluates that an expression is true for all input rows, raising an error
    if not.
  </td>
</tr>

<tr>
  <td><a href="#describe_pipe_operator"><code>DESCRIBE</code></a>
</td>
  <td>Returns a textual description of the table schema and other details about the query.</td>
</tr>

<tr>
  <td><a href="#if_pipe_operator"><code>IF</code></a>
</td>
  <td>
    Run operators (written as a subpipeline) conditionally.
  </td>
</tr>

<tr>
  <td><a href="#create_table_pipe_operator"><code>CREATE TABLE</code></a>
</td>
  <td>Terminal pipe operator. Creates a table and populates it.</td>
</tr>

<tr>
  <td><a href="#export_data_pipe_operator"><code>EXPORT DATA</code></a>
</td>
  <td>Terminal pipe operator. Writes the pipe result to a file or stream.</td>
</tr>

<tr>
  <td><a href="#insert_pipe_operator"><code>INSERT</code></a>
</td>
  <td>Terminal pipe operator. Adds new rows to a table.</td>
</tr>

<tr>
  <td><a href="#fork_pipe_operator"><code>FORK</code></a>
</td>
  <td>
    Terminal pipe operator. Ends the main pipeline and splits it into one or
    more subpipelines.
  </td>
</tr>

<tr>
  <td><a href="#tee_pipe_operator"><code>TEE</code></a>
</td>
  <td>
    Semi-terminal pipe operator. Splits the main pipeline into one or more
    subpipelines and continues the main pipeline.
  </td>
</tr>

  </tbody>
</table>

### `SELECT` pipe operator 
<a id="select_pipe_operator"></a>

<pre class="lang-sql prettyprint no-copy">
|> SELECT expression [[AS] alias] [, ...]
   [<span class="kwd">WINDOW</span> name AS window_spec, ...]
</pre>

**Description**

Produces a new table with the listed columns, similar to the outermost
[`SELECT` clause][select-clause] in a table subquery in standard syntax. The
`SELECT` operator supports standard output modifiers like `SELECT AS STRUCT` and
`SELECT DISTINCT`. The `SELECT` operator
also supports [window functions][window-functions],
including [named windows][named-windows]. Named windows are defined using the
`WINDOW` keyword and are only visible to the current pipe `SELECT` operator.
The `SELECT` operator doesn't support aggregations or anonymization.

In pipe syntax, the `SELECT` operator in a query is optional. The `SELECT`
operator can be used near the end of a query to specify the list of output
columns. The final query result contains the columns returned from the last pipe
operator. If the `SELECT` operator isn't used to select specific columns, the
output includes the full row, similar to what the
[`SELECT *` statement][select-star] in standard syntax produces.
 For [value tables][value-tables], the result is the
row value, without field expansion.

In pipe syntax, the `SELECT` clause doesn't perform aggregation. Use the
[`AGGREGATE` operator][aggregate-pipe-operator] instead.

For cases where `SELECT` would be used in standard syntax to rearrange columns,
pipe syntax supports other operators:

+   The [`EXTEND` operator][extend-pipe-operator] adds columns.
+   The [`SET` operator][set-pipe-operator] updates the value of an existing
    column.
+   The [`DROP` operator][drop-pipe-operator] removes columns.
+   The [`RENAME` operator][rename-pipe-operator] renames columns.

**Examples**

```googlesql
FROM (SELECT 'apples' AS item, 2 AS sales)
|> SELECT item AS fruit_name;

/*------------+
 | fruit_name |
 +------------+
 | apples     |
 +------------*/
```

```googlesql
-- Window function with a named window
FROM Produce
|> SELECT item, sales, category, SUM(sales) OVER item_window AS category_total
   WINDOW item_window AS (PARTITION BY category);

/*---------+-------+-----------+----------------+
 | item    | sales | category  | category_total |
 +---------+-------+-----------+----------------+
 | apples  | 2     | fruit     | 14             |
 | apples  | 7     | fruit     | 14             |
 | bananas | 5     | fruit     | 14             |
 | carrots | 8     | vegetable | 8              |
 +---------+-------+-----------+----------------*/
```

[select-clause]: https://github.com/google/googlesql/blob/master/docs/query-syntax.md#select_list

[window-functions]: https://github.com/google/googlesql/blob/master/docs/window-function-calls.md

[named-windows]: https://github.com/google/googlesql/blob/master/docs/window-function-calls.md#def_use_named_window

[select-star]: https://github.com/google/googlesql/blob/master/docs/query-syntax.md#select_

[aggregate-pipe-operator]: #aggregate_pipe_operator

[extend-pipe-operator]: #extend_pipe_operator

[set-pipe-operator]: #set_pipe_operator

[drop-pipe-operator]: #drop_pipe_operator

[rename-pipe-operator]: #rename_pipe_operator

[value-tables]: https://github.com/google/googlesql/blob/master/docs/data-model.md#value_tables

### `EXTEND` pipe operator 
<a id="extend_pipe_operator"></a>

<pre class="lang-sql prettyprint no-copy">
|> <span class="kwd">EXTEND</span> expression [[AS] alias] [, ...]
   [<span class="kwd">WINDOW</span> name AS window_spec, ...]
</pre>

**Description**

Propagates the existing table and adds computed columns, similar to
[`SELECT *, new_column`][select-star] in standard syntax. The `EXTEND` operator supports
[window functions][window-functions]
, including [named windows][named-windows]. Named windows are defined using the
`WINDOW` keyword and are only visible to the current `EXTEND` operator.

**Examples**

```googlesql
(
  SELECT 'apples' AS item, 2 AS sales
  UNION ALL
  SELECT 'bananas' AS item, 8 AS sales
)
|> EXTEND item IN ('bananas', 'lemons') AS is_yellow;

/*---------+-------+------------+
 | item    | sales | is_yellow  |
 +---------+-------+------------+
 | apples  | 2     | FALSE      |
 | bananas | 8     | TRUE       |
 +---------+-------+------------*/
```

```googlesql
-- Window function, with `OVER`
(
  SELECT 'apples' AS item, 2 AS sales
  UNION ALL
  SELECT 'bananas' AS item, 5 AS sales
  UNION ALL
  SELECT 'carrots' AS item, 8 AS sales
)
|> EXTEND SUM(sales) OVER() AS total_sales;

/*---------+-------+-------------+
 | item    | sales | total_sales |
 +---------+-------+-------------+
 | apples  | 2     | 15          |
 | bananas | 5     | 15          |
 | carrots | 8     | 15          |
 +---------+-------+-------------*/
```

```googlesql
-- Window function with a named window
FROM Produce
|> EXTEND SUM(sales) OVER item_window AS category_total
   WINDOW item_window AS (PARTITION BY category);

/*-----------+-----------+----------------+
 | item      | category  | category_total |
 +----------------------------------------+
 | apples    | fruit     | 14             |
 | apples    | fruit     | 14             |
 | bananas   | fruit     | 14             |
 | carrots   | vegetable | 8              |
 +----------------------------------------*/
```

[select-star]: https://github.com/google/googlesql/blob/master/docs/query-syntax.md#select_

[window-functions]: https://github.com/google/googlesql/blob/master/docs/window-function-calls.md

[named-windows]: https://github.com/google/googlesql/blob/master/docs/window-function-calls.md#def_use_named_window

### `SET` pipe operator 
<a id="set_pipe_operator"></a>

<pre class="lang-sql prettyprint no-copy">
|> SET column_name = expression [, ...]
</pre>

**Description**

Replaces the value of a column in the input table, similar to
[`SELECT * REPLACE (expression AS column)`][select-replace] in standard syntax.
Each referenced column must exist exactly once in the input table.

After a `SET` operation, the referenced top-level columns (like `x`) are
updated, but table aliases (like `t`) still refer to the original row values.
Therefore, `t.x` will still refer to the original value.

**Example**

```googlesql
(
  SELECT 1 AS x, 11 AS y
  UNION ALL
  SELECT 2 AS x, 22 AS y
)
|> SET x = x * x, y = 3;

/*---+---+
 | x | y |
 +---+---+
 | 1 | 3 |
 | 4 | 3 |
 +---+---*/
```

```googlesql
FROM (SELECT 2 AS x, 3 AS y) AS t
|> SET x = x * x, y = 8
|> SELECT t.x AS original_x, x, y;

/*------------+---+---+
 | original_x | x | y |
 +------------+---+---+
 | 2          | 4 | 8 |
 +------------+---+---*/
```

[select-replace]: https://github.com/google/googlesql/blob/master/docs/query-syntax.md#select_replace

### `DROP` pipe operator 
<a id="drop_pipe_operator"></a>

<pre class="lang-sql prettyprint no-copy">
|> DROP column_name [, ...]
</pre>

**Description**

Removes listed columns from the input table, similar to
[`SELECT * EXCEPT (column)`][select-except] in standard syntax. Each
referenced column must exist at least once in the input table.

After a `DROP` operation, the referenced top-level columns (like `x`) are
removed, but table aliases (like `t`) still refer to the original row values.
Therefore, `t.x` will still refer to the original value.

The `DROP` operator doesn't correspond to the
[`DROP` statement][drop-statement] in data definition language (DDL), which
deletes persistent schema objects.

**Example**

```googlesql
SELECT 'apples' AS item, 2 AS sales, 'fruit' AS category
|> DROP sales, category;

/*--------+
 | item   |
 +--------+
 | apples |
 +--------*/
```

```googlesql
FROM (SELECT 1 AS x, 2 AS y) AS t
|> DROP x
|> SELECT t.x AS original_x, y;

/*------------+---+
 | original_x | y |
 +------------+---+
 | 1          | 2 |
 +------------+---*/
```

[select-except]: https://github.com/google/googlesql/blob/master/docs/query-syntax.md#select_except

[drop-statement]: https://github.com/google/googlesql/blob/master/docs/data-definition-language.md#drop

### `RENAME` pipe operator 
<a id="rename_pipe_operator"></a>

<pre class="lang-sql prettyprint no-copy">
|> <span class="kwd">RENAME</span> old_column_name [AS] new_column_name [, ...]
</pre>

**Description**

Renames specified columns. Each column to be renamed must exist exactly once in
the input table. The `RENAME` operator can't rename value table fields,
pseudo-columns, range variables, or objects that aren't columns in the input
table.

After a `RENAME` operation, the referenced top-level columns (like `x`) are
renamed, but table aliases (like `t`) still refer to the original row
values. Therefore, `t.x` will still refer to the original value.

**Example**

```googlesql
SELECT 1 AS x, 2 AS y, 3 AS z
|> AS t
|> RENAME y AS renamed_y
|> SELECT *, t.y AS t_y;

/*---+-----------+---+-----+
 | x | renamed_y | z | t_y |
 +---+-----------+---+-----+
 | 1 | 2         | 3 | 2   |
 +---+-----------+---+-----*/
```

### `AS` pipe operator 
<a id="as_pipe_operator"></a>

<pre class="lang-sql prettyprint no-copy">
|> AS alias
</pre>

**Description**

Introduces a table alias for the input table, similar to applying the
[`AS alias` clause][using-aliases] on a table subquery in standard syntax. Any
existing table aliases are removed and the new alias becomes the table alias for
all columns in the row.

The `AS` operator can be useful after operators like
[`SELECT`][select-pipe-operator], [`EXTEND`][extend-pipe-operator], or
[`AGGREGATE`][aggregate-pipe-operator] that add columns but can't give table
aliases to them. You can use the table alias to disambiguate columns after the
`JOIN` operator.

**Example**

```googlesql
(
  SELECT "000123" AS id, "apples" AS item, 2 AS sales
  UNION ALL
  SELECT "000456" AS id, "bananas" AS item, 5 AS sales
) AS sales_table
|> AGGREGATE SUM(sales) AS total_sales GROUP BY id, item
-- AGGREGATE creates an output table, so the sales_table alias is now out of
-- scope. Add a t1 alias so the join can refer to its id column.
|> AS t1
|> JOIN (SELECT 456 AS id, "yellow" AS color) AS t2
   ON CAST(t1.id AS INT64) = t2.id
|> SELECT t2.id, total_sales, color;

/*-----+-------------+--------+
 | id  | total_sales | color  |
 +-----+-------------+--------+
 | 456 | 5           | yellow |
 +-----+-------------+--------*/
```

[using-aliases]: https://github.com/google/googlesql/blob/master/docs/query-syntax.md#using_aliases

[select-pipe-operator]: #select_pipe_operator

[extend-pipe-operator]: #extend_pipe_operator

[aggregate-pipe-operator]: #aggregate_pipe_operator

### `WHERE` pipe operator 
<a id="where_pipe_operator"></a>

<pre class="lang-sql prettyprint no-copy">
|> WHERE boolean_expression
</pre>

**Description**

Filters the results of the input table. The `WHERE` operator behaves the same
as the [`WHERE` clause][where-clause] in standard syntax.

In pipe syntax, the `WHERE` operator also replaces the
[`HAVING` clause][having-clause] and [`QUALIFY` clause][qualify-clause] in
standard syntax. For example, after performing aggregation with the
[`AGGREGATE` operator][aggregate-pipe-operator], use the `WHERE` operator
instead of the `HAVING` clause. For [window functions][window-functions] inside
a `QUALIFY` clause, use window functions inside a `WHERE` clause instead.

**Example**

```googlesql
(
  SELECT 'apples' AS item, 2 AS sales
  UNION ALL
  SELECT 'bananas' AS item, 5 AS sales
  UNION ALL
  SELECT 'carrots' AS item, 8 AS sales
)
|> WHERE sales >= 3;

/*---------+-------+
 | item    | sales |
 +---------+-------+
 | bananas | 5     |
 | carrots | 8     |
 +---------+-------*/
```

[where-clause]: https://github.com/google/googlesql/blob/master/docs/query-syntax.md#where_clause

[having-clause]: https://github.com/google/googlesql/blob/master/docs/query-syntax.md#having_clause

[qualify-clause]: https://github.com/google/googlesql/blob/master/docs/query-syntax.md#qualify_clause

[aggregate-pipe-operator]: #aggregate_pipe_operator

[window-functions]: https://github.com/google/googlesql/blob/master/docs/window-function-calls.md

### `AGGREGATE` pipe operator 
<a id="aggregate_pipe_operator"></a>

<pre class="lang-sql prettyprint no-copy">
-- Full-table aggregation
|> <span class="kwd">AGGREGATE</span> aggregate_expression [[AS] alias] [, ...]
</pre>
<pre class="lang-sql prettyprint no-copy">
-- Aggregation with grouping
|> <span class="kwd">AGGREGATE</span> [aggregate_expression [[AS] alias] [, ...]]
   GROUP BY groupable_items [[AS] alias] [, ...]
</pre>
<pre class="lang-sql prettyprint">
-- Aggregation with grouping and shorthand ordering syntax
|> <span class="kwd">AGGREGATE</span> [aggregate_expression [[AS] alias] [<span class="var">order_suffix</span>] [, ...]]
   GROUP [AND ORDER] BY groupable_item [[AS] alias] [<span class="var">order_suffix</span>] [, ...]

<span class="var">order_suffix</span>: {ASC | DESC} [{<span class="var">NULLS FIRST</span> | <span class="var">NULLS LAST</span>}]
</pre>

**Description**

Performs aggregation on data across grouped rows or an entire table. The
`AGGREGATE` operator is similar to a query in standard syntax that contains a
[`GROUP BY` clause][group-by-clause] or a `SELECT` list with
[aggregate functions][aggregate-functions] or both. In pipe syntax, the
`GROUP BY` clause is part of the `AGGREGATE` operator. Pipe syntax
doesn't support a standalone `GROUP BY` operator.

Without the `GROUP BY` clause, the `AGGREGATE` operator performs full-table
aggregation and produces one output row.

With the `GROUP BY` clause, the `AGGREGATE` operator performs aggregation with
grouping, producing one row for each set of distinct values for the grouping
expressions.

The `AGGREGATE` expression list corresponds to the aggregated expressions in a
`SELECT` list in standard syntax. Each expression in the `AGGREGATE` list must
include an aggregate function. Aggregate expressions can also include scalar
expressions (for example, `sqrt(SUM(x*x))`). Column aliases can be assigned
using the [`AS` operator][as-pipe-operator]. Window
functions aren't allowed, but the [`EXTEND` operator][extend-pipe-operator] can
be used before the `AGGREGATE` operator to compute window functions.

The `GROUP BY` clause in the `AGGREGATE` operator corresponds to the `GROUP BY`
clause in standard syntax. Unlike in standard syntax, aliases can be assigned to
`GROUP BY` items. Standard grouping operators like `GROUPING SETS`, `ROLLUP`,
and `CUBE` are supported.

The output columns from the `AGGREGATE` operator include all grouping columns
first, followed by all aggregate columns, using their assigned aliases as the
column names.

Unlike in standard syntax, grouping expressions aren't repeated across `SELECT`
and `GROUP BY` clauses. In pipe syntax, the grouping expressions are listed
once, in the `GROUP BY` clause, and are automatically included as output columns
for the `AGGREGATE` operator.

Because output columns are fully specified by the `AGGREGATE` operator, the
`SELECT` operator isn't needed after the `AGGREGATE` operator unless
you want to produce a list of columns different from the default.

**Standard syntax**

<pre class="lang-sql prettyprint">
-- Aggregation in standard syntax
SELECT SUM(col1) AS total, col2, col3, col4...
FROM table1
GROUP BY col2, col3, col4...
</pre>

**Pipe syntax**

<pre class="lang-sql prettyprint">
-- The same aggregation in pipe syntax
FROM table1
|> <span class="kwd">AGGREGATE</span> SUM(col1) AS total
   GROUP BY col2, col3, col4...
</pre>

**Examples**

```googlesql
-- Full-table aggregation
(
  SELECT 'apples' AS item, 2 AS sales
  UNION ALL
  SELECT 'bananas' AS item, 5 AS sales
  UNION ALL
  SELECT 'apples' AS item, 7 AS sales
)
|> AGGREGATE COUNT(*) AS num_items, SUM(sales) AS total_sales;

/*-----------+-------------+
 | num_items | total_sales |
 +-----------+-------------+
 | 3         | 14          |
 +-----------+-------------*/
```

```googlesql
-- Aggregation with grouping
(
  SELECT 'apples' AS item, 2 AS sales
  UNION ALL
  SELECT 'bananas' AS item, 5 AS sales
  UNION ALL
  SELECT 'apples' AS item, 7 AS sales
)
|> AGGREGATE COUNT(*) AS num_items, SUM(sales) AS total_sales
   GROUP BY item;

/*---------+-----------+-------------+
 | item    | num_items | total_sales |
 +---------+-----------+-------------+
 | apples  | 2         | 9           |
 | bananas | 1         | 5           |
 +---------+-----------+-------------*/
```

#### Shorthand ordering syntax with `AGGREGATE` 
<a id="shorthand_order_pipe_syntax"></a>

The `AGGREGATE` operator supports a shorthand ordering syntax, which is
equivalent to applying the [`ORDER BY` operator][order-by-pipe-operator] as part
of the `AGGREGATE` operator without repeating the column list:

<pre class="lang-sql prettyprint">
-- Aggregation with grouping and shorthand ordering syntax
|> <span class="kwd">AGGREGATE</span> [aggregate_expression [[AS] alias] [<span class="var">order_suffix</span>] [, ...]]
   GROUP [AND ORDER] BY groupable_item [[AS] alias] [<span class="var">order_suffix</span>] [, ...]

<span class="var">order_suffix</span>: {ASC | DESC} [{NULLS FIRST | NULLS LAST}]
</pre>

The `GROUP AND ORDER BY` clause is equivalent to an `ORDER BY` clause on all
`groupable_items`. By default, each `groupable_item` is sorted in ascending
order with `NULL` values first. Other ordering suffixes like `DESC` or `NULLS
LAST` can be used for other orders.

Without the `GROUP AND ORDER BY` clause, the `ASC` or `DESC` suffixes can be
added on individual columns in the `GROUP BY` list or `AGGREGATE` list or both.
The `NULLS FIRST` and `NULLS LAST` suffixes can be used to further modify `NULL`
sorting.

Adding these suffixes is equivalent to adding an `ORDER BY` clause that includes
all of the suffixed columns with the suffixed grouping columns first, matching
the left-to-right output column order.

**Examples**

Consider the following table called `Produce`:

```googlesql
/*---------+-------+-----------+
 | item    | sales | category  |
 +---------+-------+-----------+
 | apples  | 2     | fruit     |
 | carrots | 8     | vegetable |
 | apples  | 7     | fruit     |
 | bananas | 5     | fruit     |
 +---------+-------+-----------*/
```

The following two equivalent examples show you how to order by all grouping
columns using the `GROUP AND ORDER BY` clause or a separate `ORDER BY` clause:

```googlesql
-- Order by all grouping columns using GROUP AND ORDER BY.
FROM Produce
|> AGGREGATE SUM(sales) AS total_sales
   GROUP AND ORDER BY category, item DESC;

/*-----------+---------+-------------+
 | category  | item    | total_sales |
 +-----------+---------+-------------+
 | fruit     | bananas | 5           |
 | fruit     | apples  | 9           |
 | vegetable | carrots | 8           |
 +-----------+---------+-------------*/
```

```googlesql
--Order by columns using ORDER BY after performing aggregation.
FROM Produce
|> AGGREGATE SUM(sales) AS total_sales
   GROUP BY category, item
|> ORDER BY category, item DESC;
```

You can add an ordering suffix to a column in the `AGGREGATE` list. Although the
`AGGREGATE` list appears before the `GROUP BY` list in the query, ordering
suffixes on columns in the `GROUP BY` list are applied first.

```googlesql
FROM Produce
|> AGGREGATE SUM(sales) AS total_sales ASC
   GROUP BY item, category DESC;

/*---------+-----------+-------------+
 | item    | category  | total_sales |
 +---------+-----------+-------------+
 | carrots | vegetable | 8           |
 | bananas | fruit     | 5           |
 | apples  | fruit     | 9           |
 +---------+-----------+-------------*/
```

The previous query is equivalent to the following:

```googlesql
-- Order by specified grouping and aggregate columns.
FROM Produce
|> AGGREGATE SUM(sales) AS total_sales
   GROUP BY item, category
|> ORDER BY category DESC, total_sales;
```

[group-by-clause]: https://github.com/google/googlesql/blob/master/docs/query-syntax.md#group_by_clause

[aggregate-functions]: https://github.com/google/googlesql/blob/master/docs/aggregate_functions.md

[as-pipe-operator]: #as_pipe_operator

[extend-pipe-operator]: #extend_pipe_operator

[order-by-pipe-operator]: #order_by_pipe_operator

### `DISTINCT` pipe operator 
<a id="distinct_pipe_operator"></a>

<pre class="lang-sql prettyprint no-copy">
|> <span class="kwd">DISTINCT</span>
</pre>

**Description**

Returns distinct rows from the input table, while preserving table aliases.

Using the `DISTINCT` operator after a `SELECT` or `UNION ALL` clause is similar
to using a [`SELECT DISTINCT` clause][select-distinct] or
[`UNION DISTINCT` clause][union-operator] in standard syntax, but the `DISTINCT`
pipe operator can be applied anywhere. The `DISTINCT` operator computes distinct
rows based on the values of all visible columns. Pseudo-columns are ignored
while computing distinct rows and are dropped from the output.

The `DISTINCT` operator is similar to using a `|> SELECT DISTINCT *` clause, but
doesn't expand value table fields, and preserves table aliases from the input.

**Examples**

```googlesql
(
  SELECT 'apples' AS item, 2 AS sales
  UNION ALL
  SELECT 'bananas' AS item, 5 AS sales
  UNION ALL
  SELECT 'bananas' AS item, 5 AS sales
  UNION ALL
  SELECT 'carrots' AS item, 8 AS sales
)
|> DISTINCT
|> WHERE sales >= 3;

/*---------+-------+
 | item    | sales |
 +---------+-------+
 | bananas | 5     |
 | carrots | 8     |
 +---------+-------*/
```

In the following example, the table alias `Produce` can be used in
expressions after the `DISTINCT` pipe operator.

```googlesql
(
  SELECT 'apples' AS item, 2 AS sales
  UNION ALL
  SELECT 'bananas' AS item, 5 AS sales
  UNION ALL
  SELECT 'bananas' AS item, 5 AS sales
  UNION ALL
  SELECT 'carrots' AS item, 8 AS sales
)
|> AS Produce
|> DISTINCT
|> SELECT Produce.item;

/*---------+
 | item    |
 +---------+
 | apples  |
 | bananas |
 | carrots |
 +---------*/
```

By contrast, the table alias isn't visible after a `|> SELECT DISTINCT *`
clause.

```googlesql {.bad}
-- Error, unrecognized name: Produce
(
  SELECT 'apples' AS item, 2 AS sales
  UNION ALL
  SELECT 'bananas' AS item, 5 AS sales
  UNION ALL
  SELECT 'bananas' AS item, 5 AS sales
  UNION ALL
  SELECT 'carrots' AS item, 8 AS sales
)
|> AS Produce
|> SELECT DISTINCT *
|> SELECT Produce.item;
```

In the following examples, the `DISTINCT` operator doesn't expand value table
fields and retains the `STRUCT` type in the result. By contrast, the
`|> SELECT DISTINCT *` clause expands the `STRUCT` type into two columns.

```googlesql
SELECT AS STRUCT 1 x, 2 y
|> DISTINCT;

/*---------+
 | $struct |
 +---------+
  {
    x: 1,
    y: 2
  }
 +----------*/
```

```googlesql
SELECT AS STRUCT 1 x, 2 y
|> SELECT DISTINCT *;

/*---+---+
 | x | y |
 +---+---+
 | 1 | 2 |
 +---+---*/
```

The following examples show equivalent ways to generate the same results with
distinct values from columns `a`, `b`, and `c`.

```googlesql
FROM table
|> SELECT DISTINCT a, b, c;

FROM table
|> SELECT a, b, c
|> DISTINCT;

FROM table
|> AGGREGATE
   GROUP BY a, b, c;
```

[select-distinct]: https://github.com/google/googlesql/blob/master/docs/query-syntax.md#select_distinct

[union-operator]: https://github.com/google/googlesql/blob/master/docs/query-syntax.md#union

[aggregate-pipe-operator]: https://github.com/google/googlesql/blob/master/docs/pipe-syntax.md#aggregate_pipe_operator

[using-clause]: https://github.com/google/googlesql/blob/master/docs/query-syntax.md#using_clause

[full-join]: https://github.com/google/googlesql/blob/master/docs/query-syntax.md#full_join

[inner-join]: https://github.com/google/googlesql/blob/master/docs/query-syntax.md#inner_join

### `JOIN` pipe operator 
<a id="join_pipe_operator"></a>

<pre class="lang-sql prettyprint no-copy">
|> [join_type] JOIN from_item [[AS] alias] [{on_clause | using_clause}]
</pre>

**Description**

Joins rows from the input table with rows from a second table provided as an
argument. The `JOIN` operator behaves the same as the
[`JOIN` operation][join-operation] in standard syntax. The input table is the
left side of the join and the `JOIN` argument is the right side of the join.
Standard join inputs are supported, including tables, subqueries, `UNNEST`
operations, and table-valued function (TVF) calls. Standard join modifiers like
`LEFT`, `INNER`, and `CROSS` are allowed before the `JOIN` keyword.

An alias can be assigned to the input table on the right side of the join, but
not to the input table on the left side of the join. If an alias on the
input table is needed, perhaps to disambiguate columns in an
[`ON` expression][on-clause], then an alias can be added using the
[`AS` operator][as-pipe-operator] before the `JOIN` arguments.

**Example**

```googlesql
(
  SELECT 'apples' AS item, 2 AS sales
  UNION ALL
  SELECT 'bananas' AS item, 5 AS sales
)
|> AS produce_sales
|> LEFT JOIN
     (
       SELECT "apples" AS item, 123 AS id
     ) AS produce_data
   ON produce_sales.item = produce_data.item
|> SELECT produce_sales.item, sales, id;

/*---------+-------+------+
 | item    | sales | id   |
 +---------+-------+------+
 | apples  | 2     | 123  |
 | bananas | 5     | NULL |
 +---------+-------+------*/
```

[join-operation]: https://github.com/google/googlesql/blob/master/docs/query-syntax.md#join_types

[on-clause]: https://github.com/google/googlesql/blob/master/docs/query-syntax.md#on_clause

[as-pipe-operator]: #as_pipe_operator

### `CALL` pipe operator 
<a id="call_pipe_operator"></a>

<pre class="lang-sql prettyprint no-copy">
|> <span class="kwd">CALL</span> table_function (argument [, ...]) [[AS] alias]
</pre>

**Description**

Calls a [table-valued function][tvf] (TVF) that accepts at least one table as
an argument, similar to
[table function calls][table-function-calls] in standard syntax.

TVFs in standard syntax can be called in the `FROM` clause or in a `JOIN`
operation. These are both allowed in pipe syntax as well.

In pipe syntax, TVFs that take a table argument can also be called with the
`CALL` operator. The first table argument comes from the input table and
must be omitted in the arguments. An optional table alias can be added for the
output table.

Multiple TVFs can be called sequentially without using nested subqueries.

**Examples**

Suppose you have TVFs with the following parameters:

+  `tvf1(inputTable1 ANY TABLE, arg1 ANY TYPE)` and
+  `tvf2(arg2 ANY TYPE, arg3 ANY TYPE, inputTable2 ANY TABLE)`.

The following examples compare calling both TVFs on an input table
by using standard syntax and by using the `CALL` pipe operator:

```googlesql
-- Call the TVFs without using the CALL operator.
SELECT *
FROM
  tvf2(arg2, arg3, TABLE tvf1(TABLE input_table, arg1));
```

```googlesql
-- Call the same TVFs with the CALL operator.
FROM input_table
|> CALL tvf1(arg1)
|> CALL tvf2(arg2, arg3);
```

[tvf]: https://github.com/google/googlesql/blob/master/docs/table-functions.md#tvfs

[table-function-calls]: https://github.com/google/googlesql/blob/master/docs/query-syntax.md#table_function_calls

### `ORDER BY` pipe operator 
<a id="order_by_pipe_operator"></a>

<pre class="lang-sql prettyprint no-copy">
|> ORDER BY expression [sort_options] [, ...]
</pre>

**Description**

Sorts results by a list of expressions. The `ORDER BY` operator behaves the same
as the [`ORDER BY` clause][order-by-clause] in standard syntax. Suffixes like
`ASC`, `DESC`, and `NULLS LAST` are supported for customizing the ordering for
each expression.

In pipe syntax, the [`AGGREGATE` operator][aggregate-pipe-operator] also
supports [shorthand ordering suffixes][shorthand-order-pipe-syntax] to
apply `ORDER BY` behavior more concisely as part of aggregation.

**Example**

```googlesql
(
  SELECT 1 AS x
  UNION ALL
  SELECT 3 AS x
  UNION ALL
  SELECT 2 AS x
)
|> ORDER BY x DESC;

/*---+
 | x |
 +---+
 | 3 |
 | 2 |
 | 1 |
 +---*/
```

[order-by-clause]: https://github.com/google/googlesql/blob/master/docs/query-syntax.md#order_by_clause

[aggregate-pipe-operator]: #aggregate_pipe_operator

[shorthand-order-pipe-syntax]: #shorthand_order_pipe_syntax

### `LIMIT` pipe operator 
<a id="limit_pipe_operator"></a>

<pre class="lang-sql prettyprint no-copy">
|> <span class="kwd">LIMIT</span> count [OFFSET skip_rows]
</pre>

**Description**

Limits the number of rows to return in a query, with an optional `OFFSET` clause
to skip over rows. The `LIMIT` operator behaves the same as the
[`LIMIT` and `OFFSET` clause][limit-offset-clause] in standard syntax.

**Examples**

```googlesql
(
  SELECT 'apples' AS item, 2 AS sales
  UNION ALL
  SELECT 'bananas' AS item, 5 AS sales
  UNION ALL
  SELECT 'carrots' AS item, 8 AS sales
)
|> ORDER BY item
|> LIMIT 1;

/*---------+-------+
 | item    | sales |
 +---------+-------+
 | apples  | 2     |
 +---------+-------*/
```

```googlesql
(
  SELECT 'apples' AS item, 2 AS sales
  UNION ALL
  SELECT 'bananas' AS item, 5 AS sales
  UNION ALL
  SELECT 'carrots' AS item, 8 AS sales
)
|> ORDER BY item
|> LIMIT 1 OFFSET 2;

/*---------+-------+
 | item    | sales |
 +---------+-------+
 | carrots | 8     |
 +---------+-------*/
```

[limit-offset-clause]: https://github.com/google/googlesql/blob/master/docs/query-syntax.md#limit_and_offset_clause

### `UNION` pipe operator 
<a id="union_pipe_operator"></a>

<pre class="lang-sql prettyprint no-copy">
query
|> <span class="kwd">UNION</span> {ALL | DISTINCT} (query) [, (query), ...]
</pre>

**Description**

Returns the combined results of the input queries to the left and right of the
pipe operator. Columns are matched and rows are concatenated vertically.

The `UNION` pipe operator behaves the same as the
[`UNION` set operator][union-operator] in standard syntax. However, in pipe
syntax, the `UNION` pipe operator can include multiple comma-separated queries
without repeating the `UNION` syntax. Queries following the operator
are enclosed in parentheses.

For example, compare the following equivalent queries:

```googlesql
-- Standard syntax
SELECT * FROM ...
UNION ALL
SELECT 1
UNION ALL
SELECT 2;

-- Pipe syntax
SELECT * FROM ...
|> UNION ALL
    (SELECT 1),
    (SELECT 2);
```

The `UNION` pipe operator supports the same modifiers as the
`UNION` set operator in standard syntax, such as the
`BY NAME` modifier (or `CORRESPONDING`) and `LEFT | FULL [OUTER]` mode prefixes.

**Examples**

```googlesql
SELECT * FROM UNNEST(ARRAY<INT64>[1, 2, 3]) AS number
|> UNION ALL (SELECT 1);

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
|> UNION DISTINCT (SELECT 1);

/*--------+
 | number |
 +--------+
 | 1      |
 | 2      |
 | 3      |
 +--------*/
```

The following example shows multiple input queries to the right of the pipe
operator:

```googlesql
SELECT * FROM UNNEST(ARRAY<INT64>[1, 2, 3]) AS number
|> UNION DISTINCT
    (SELECT 1),
    (SELECT 2);

/*--------+
 | number |
 +--------+
 | 1      |
 | 2      |
 | 3      |
 +--------*/
```

The following example uses the `BY NAME`
modifier to match results by column name instead of in the
order that the columns are given in the input queries.

```googlesql
SELECT 1 AS one_digit, 10 AS two_digit
|> UNION ALL BY NAME
    (SELECT 20 AS two_digit, 2 AS one_digit);

/*-----------+-----------+
 | one_digit | two_digit |
 +-----------+-----------+
 | 1         | 10        |
 | 2         | 20        |
 +-----------+-----------*/
```

Without the `BY NAME` modifier,
the results are matched by column position in the input query and the column
names are ignored.

```googlesql
SELECT 1 AS one_digit, 10 AS two_digit
|> UNION ALL
    (SELECT 20 AS two_digit, 2 AS one_digit);

-- Results follow column order from queries and ignore column names.
/*-----------+-----------+
 | one_digit | two_digit |
 +-----------+-----------+
 | 1         | 10        |
 | 20        | 2         |
 +-----------+-----------*/
```

[union-operator]: https://github.com/google/googlesql/blob/master/docs/query-syntax.md#union

### `RECURSIVE UNION` pipe operator 
<a id="recursive_union_pipe_operator"></a>

<pre class="lang-sql prettyprint no-copy">
|> RECURSIVE UNION
   { ALL | DISTINCT }
   [ BY NAME ]
   ( query | subpipeline )
   [ AS alias ]
</pre>

**Description**

Runs a recursive query or [subpipeline][subpipelines], similar to the
[`WITH RECURSIVE` clause][recursive-cte] but as a pipe operator. You can use the
`RECURSIVE UNION` operator anywhere in a query.

The `RECURSIVE UNION` operator does the following when run:

1. Adds the pipe input table to the combined output. If
   the `DISTINCT` keyword is specified, duplicate rows are excluded from the
   output.
   If the `ALL` keyword is specified, duplicate rows are included in the
   output.
1. If any new rows were added, the operator does the following:
   1.  Runs the specified query or subpipeline on the new rows.
   1.  Adds the resulting output rows to the combined output. If
       the `DISTINCT` keyword is specified, duplicate rows are
       excluded.
   1.  Repeats this process as long as new rows continue to be
       generated.
1. Returns the final union of added rows.

The specified query or subpipeline runs repeatedly to potentially generate new
rows of output. On each iteration, the `RECURSIVE UNION` pipe operator scans
the *recursive input table* that provides the rows added by the previous
iteration. The operator stops running the recursion when no new rows are
produced.

The recursive input to the query is referenced as a table, using the `alias`
value as its name. This recursive input table must be referenced exactly once,
in a `FROM` clause or `JOIN` operation inside the query.

If a [subpipeline][subpipelines] is provided in place of a query, the recursive
input is passed in automatically as the subpipeline's input table. If an
`alias` value is included, the value works as a table alias for the input
table, allowing the subpipeline to reference its columns in the format
`alias.column_name`.

The output schema has the same column names and types as the pipe input table.
The recursive query or subpipeline must produce columns with compatible
types. If the [`BY NAME` modifier][by-name-or-corresponding]
is specified, the columns in the recursive input table are matched by column
name; otherwise the columns are matched positionally.

If the input to the `RECURSIVE UNION` pipe operator is a
[value table][datamodel-value-tables], the output is also a value table. As with
the [`UNION` pipe operator][union-pipe-operator], the `RECURSIVE UNION` operator
can combine one-column tables with value tables.

**Syntax comparison**

The `RECURSIVE UNION` operator syntax is shorter, clearer, and easier to read
and maintain than the corresponding `WITH RECURSIVE` clause syntax.

The following comparison shows the basic structure of a `WITH RECURSIVE` clause
and a `RECURSIVE UNION` operator equivalent:

```googlesql
-- Standard syntax
WITH RECURSIVE name AS (
  base_query
  UNION ALL
  recursive_query
)
final_query

-- Pipe syntax equivalent with RECURSIVE UNION operator
WITH name AS (
  base_query
  |> RECURSIVE UNION ALL (
    recursive_query
  ) AS name
)
final_query
```

Converting recursive queries to pipe syntax is usually just a matter of
replacing the `UNION ALL` clause with the `RECURSIVE UNION` operator. After
that, if the recursive query is in pipe syntax and starts with a `FROM` clause,
the `FROM` clause can be removed and replaced with the subpipeline form.

In addition, you can also remove the initial `WITH` clause if the rest of the
query previously started with a `FROM` clause:

```googlesql
-- Example query with no WITH clause
base_query
|> RECURSIVE UNION ALL (
     recursive_query
   ) AS name
final_query
```

**Style guidance**

+ Use the `RECURSIVE UNION` operator instead of the `WITH RECURSIVE` clause.
+ Use `RECURSIVE UNION` inline, without using `WITH`, unless:
  + `WITH` is used because a common table is referenced multiple times, or
  + `WITH` is helpful to break a large query into named fragments.
+ Use the subpipeline form because it's shorter and represents
  the typical input pattern.

**Examples**

The following examples assume this is the input table, representing an
employee hierarchy:

```googlesql
CREATE TEMP TABLE Employees(
  employee_id INT64,
  manager_id INT64,
  state STRING);
```

The following example looks up a manager with `employee_id = 123456`,
performs a recursive traversal to retrieve all employees who report transitively
to that manager, and counts how many of those reports are in one state.

Here is the standard syntax that accomplishes this, using the
`WITH RECURSIVE` clause:

```googlesql
WITH RECURSIVE
  AllReportees AS (
    SELECT employee_id, manager_id, state
    FROM Employees
    WHERE employee_id = 123456
    UNION ALL
    SELECT e.employee_id, e.manager_id, e.state
    FROM AllReportees r  -- Recursive input table
    JOIN Employees e
      ON e.manager_id = r.employee_id
  )
SELECT COUNT(*) AS num_employees
FROM AllReportees
WHERE State = 'AK';

-- Example of recursive count of employees reporting to 123456 in AK.
/*-----------------+
 | num_employees   |
 +-----------------+
 | 46              |
 +-----------------*/
```

The following example accomplishes the same task using the `RECURSIVE UNION`
pipe operator with a subquery:

```googlesql
FROM Employees
|> WHERE employee_id = 123456
|> RECURSIVE UNION ALL
(
  FROM AllReportees r  -- Recursive input table
  |> JOIN Employees e ON e.manager_id = r.employee_id
  |> SELECT e.*
) AS AllReportees
|> WHERE State = 'AK'
|> AGGREGATE COUNT(*) AS num_employees;

-- Example of recursive count of employees reporting to 123456 in AK.
/*-----------------+
 | num_employees   |
 +-----------------+
 | 46              |
 +-----------------*/
```

You can also define recursive queries using the `RECURSIVE UNION` pipe operator
using a subpipeline, which removes the need for an inner `FROM` clause. The
recursive input table is scanned by the subpipeline automatically and isn't
explicitly written in the pipe syntax:

```googlesql
FROM Employees
|> WHERE employee_id = 123456
|> RECURSIVE UNION ALL
   (
     |> JOIN Employees e ON e.manager_id = AllReportees.employee_id
     |> SELECT e.*
   ) AS AllReportees
|> WHERE State = 'AK'
|> AGGREGATE COUNT(*) AS num_employees;

-- Example of recursive count of employees reporting to 123456 in AK.
/*-----------------+
 | num_employees   |
 +-----------------+
 | 46              |
 +-----------------*/
```

It's also still possible to define recursive queries in `WITH` using pipe
syntax. Using `RECURSIVE UNION` inside `WITH` can be clearer than using
`WITH RECURSIVE` syntax:

```googlesql
WITH
  AllReportees AS (
    FROM
      Employees
    |> WHERE employee_id = 123456
    |> RECURSIVE UNION ALL   -- Clearer than using a WITH RECURSIVE equivalent.
    (
      |> JOIN Employees e ON e.manager_id = r.employee_id
      |> SELECT e.*
    ) AS r -- Recursive input table
  )
SELECT COUNT(*) AS num_employees
FROM AllReportees
WHERE State = 'AK';

-- Example of recursive count of employees reporting to 123456 in AK.
/*-----------------+
 | num_employees   |
 +-----------------+
 | 46              |
 +-----------------*/
```

[datamodel-value-tables]: https://github.com/google/googlesql/blob/master/docs/data-model.md#value_tables

[union-pipe-operator]: #union_pipe_operator

[subpipelines]: #subpipelines

[recursive-cte]: https://github.com/google/googlesql/blob/master/docs/query-syntax.md#recursive_cte

[by-name-or-corresponding]: https://github.com/google/googlesql/blob/master/docs/query-syntax.md#by_name_or_corresponding

### `INTERSECT` pipe operator 
<a id="intersect_pipe_operator"></a>

<pre class="lang-sql prettyprint no-copy">
query
|> <span class="kwd">INTERSECT</span> {ALL | DISTINCT} (query) [, (query), ...]
</pre>

**Description**

Returns rows that are found in the results of both the input query to the left
of the pipe operator and all input queries to the right of the pipe
operator.

The `INTERSECT` pipe operator behaves the same as the
[`INTERSECT` set operator][intersect-operator] in standard syntax. However, in
pipe syntax, the `INTERSECT` pipe operator can include multiple
comma-separated queries without repeating the `INTERSECT` syntax. Queries
following the operator are enclosed in parentheses.

For example, compare the following equivalent queries:

```googlesql
-- Standard syntax
SELECT * FROM ...
INTERSECT ALL
SELECT 1
INTERSECT ALL
SELECT 2;

-- Pipe syntax
SELECT * FROM ...
|> INTERSECT ALL
    (SELECT 1),
    (SELECT 2);
```

The `INTERSECT` pipe operator supports the same modifiers as the
`INTERSECT` set operator in standard syntax, such as the
`BY NAME` modifier (or `CORRESPONDING`)
 and `LEFT | FULL [OUTER]` mode prefixes.

**Examples**

```googlesql
SELECT * FROM UNNEST(ARRAY<INT64>[1, 2, 3, 3, 4]) AS number
|> INTERSECT ALL
    (SELECT * FROM UNNEST(ARRAY<INT64>[2, 3, 3, 5]) AS number);

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
|> INTERSECT DISTINCT
    (SELECT * FROM UNNEST(ARRAY<INT64>[2, 3, 3, 5]) AS number);

/*--------+
 | number |
 +--------+
 | 2      |
 | 3      |
 +--------*/
```

The following example shows multiple input queries to the right of the pipe
operator:

```googlesql
SELECT * FROM UNNEST(ARRAY<INT64>[1, 2, 3, 3, 4]) AS number
|> INTERSECT DISTINCT
    (SELECT * FROM UNNEST(ARRAY<INT64>[2, 3, 3, 5]) AS number),
    (SELECT * FROM UNNEST(ARRAY<INT64>[3, 3, 4, 5]) AS number);

/*--------+
 | number |
 +--------+
 | 3      |
 +--------*/
```

The following example uses the `BY NAME`
modifier to return the intersecting row from the columns despite the differing
column order in the input queries.

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
|> INTERSECT ALL BY NAME
    (SELECT 10 AS two_digit, 1 AS one_digit);

/*-----------+-----------+
 | one_digit | two_digit |
 +-----------+-----------+
 | 1         | 10        |
 +-----------+-----------*/
```

Without the `BY NAME` modifier, the same
columns in differing order are considered different columns, so the query
doesn't detect any intersecting row values.

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
|> INTERSECT ALL
    (SELECT 10 AS two_digit, 1 AS one_digit);

-- No intersecting values detected because columns aren't recognized as the same.
/*-----------+-----------+

 +-----------+-----------*/
```

[intersect-operator]: https://github.com/google/googlesql/blob/master/docs/query-syntax.md#intersect

### `EXCEPT` pipe operator 
<a id="except_pipe_operator"></a>

<pre class="lang-sql prettyprint no-copy">
query
|> <span class="kwd">EXCEPT</span> {ALL | DISTINCT} (query) [, (query), ...]
</pre>

**Description**

Returns rows from the input query to the left of the pipe operator that aren't
present in any input queries to the right of the pipe operator.

The `EXCEPT` pipe operator behaves the same as the
[`EXCEPT` set operator][except-operator] in standard syntax. However, in pipe
syntax, the `EXCEPT` pipe operator can include multiple comma-separated
queries without repeating the `EXCEPT` syntax. Queries following the
operator are enclosed in parentheses.

For example, compare the following equivalent queries:

```googlesql
-- Standard syntax
SELECT * FROM ...
EXCEPT ALL
SELECT 1
EXCEPT ALL
SELECT 2;

-- Pipe syntax
SELECT * FROM ...
|> EXCEPT ALL
    (SELECT 1),
    (SELECT 2);
```

Parentheses can be used to group set operations and control order of operations.
In `EXCEPT` set operations, query results can vary depending on the operation
grouping.

```googlesql
-- Default operation grouping
(
  SELECT * FROM ...
  EXCEPT ALL
  SELECT 1
)
EXCEPT ALL
SELECT 2;

-- Modified operation grouping
SELECT * FROM ...
EXCEPT ALL
(
  SELECT 1
  EXCEPT ALL
  SELECT 2
);

-- Same modified operation grouping in pipe syntax
SELECT * FROM ...
|> EXCEPT ALL
(
  SELECT 1
  |> EXCEPT ALL (SELECT 2)
);
```

The `EXCEPT` pipe operator supports the same modifiers as the
`EXCEPT` set operator in standard syntax, such as the
`BY NAME` modifier (or `CORRESPONDING`) and `LEFT | FULL [OUTER]` mode prefixes.

**Examples**

```googlesql
SELECT * FROM UNNEST(ARRAY<INT64>[1, 2, 3, 3, 4]) AS number
|> EXCEPT ALL
    (SELECT * FROM UNNEST(ARRAY<INT64>[1, 2]) AS number);

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
|> EXCEPT DISTINCT
    (SELECT * FROM UNNEST(ARRAY<INT64>[1, 2]) AS number);

/*--------+
 | number |
 +--------+
 | 3      |
 | 4      |
 +--------*/
```

The following example shows multiple input queries to the right of the pipe
operator:

```googlesql
SELECT * FROM UNNEST(ARRAY<INT64>[1, 2, 3, 3, 4]) AS number
|> EXCEPT DISTINCT
    (SELECT * FROM UNNEST(ARRAY<INT64>[1, 2]) AS number),
    (SELECT * FROM UNNEST(ARRAY<INT64>[1, 4]) AS number);

/*--------+
 | number |
 +--------+
 | 3      |
 +--------*/
```

The following example groups the set operations to modify the order of
operations. The first input query is used against the result of the last two
queries instead of the values of the last two queries individually.

```googlesql
SELECT * FROM UNNEST(ARRAY<INT64>[1, 2, 3, 3, 4]) AS number
|> EXCEPT DISTINCT
(
  SELECT * FROM UNNEST(ARRAY<INT64>[1, 2]) AS number
  |> EXCEPT DISTINCT
      (SELECT * FROM UNNEST(ARRAY<INT64>[1, 4]) AS number)
);

/*--------+
 | number |
 +--------+
 | 1      |
 | 3      |
 | 4      |
 +--------*/
```

The following example uses the `BY NAME`
modifier to return unique rows from the input query to the left of the pipe
operator despite the differing column order in the input queries.

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
|> EXCEPT ALL BY NAME
    (SELECT 10 AS two_digit, 1 AS one_digit);

/*-----------+-----------+
 | one_digit | two_digit |
 +-----------+-----------+
 | 2         | 20        |
 | 3         | 30        |
 +-----------+-----------*/
```

Without the `BY NAME` modifier, the same columns in
differing order are considered different columns, so the query doesn't detect
any common rows that should be excluded.

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
|> EXCEPT ALL
    (SELECT 10 AS two_digit, 1 AS one_digit);

-- No values excluded because columns aren't recognized as the same.
/*-----------+-----------+
 | one_digit | two_digit |
 +-----------+-----------+
 | 1         | 10        |
 | 2         | 20        |
 | 3         | 30        |
 +-----------+-----------*/
```

[except-operator]: https://github.com/google/googlesql/blob/master/docs/query-syntax.md#except

### `TABLESAMPLE` pipe operator 
<a id="tablesample_pipe_operator"></a>

<pre class="lang-sql prettyprint no-copy">
|> TABLESAMPLE sample_method (sample_size {PERCENT | ROWS}) [, ...]
</pre>

**Description**

Selects a random sample of rows from the input table. The `TABLESAMPLE` pipe
operator behaves the same as [`TABLESAMPLE` operator][tablesample-operator] in
standard syntax.

**Example**

The following example samples approximately 1% of data from a table called
`LargeTable`:

```googlesql
FROM LargeTable
|> TABLESAMPLE SYSTEM (1 PERCENT);
```

[tablesample-operator]: https://github.com/google/googlesql/blob/master/docs/query-syntax.md#tablesample_operator

### `WITH` pipe operator 
<a id="with_pipe_operator"></a>

<pre class="lang-sql prettyprint no-copy">
|> <span class="kwd">WITH</span> [<span class="kwd">RECURSIVE</span>] alias <span class="kwd">AS</span> query, ...
</pre>

**Description**

Defines one or more common table expressions (CTEs) that the rest of the query
can reference, similar to standard [`WITH` clauses][with-clause]. Ignores the
pipe input table and passes it through as the input to the next pipe operation.

**Examples**

```googlesql
SELECT 1 AS key
|> WITH t AS (
    SELECT 1 AS key, 'my_value' AS value
  )
|> INNER JOIN t USING (key)

/*---------+---------+
 | key     | value   |
 +---------+---------+
 | 1       | my_value|
 +---------+---------*/
```

```googlesql
SELECT 1 AS key
-- Define multiple CTEs.
|> WITH t1 AS (
    SELECT 2
  ), t2 AS (
    SELECT 3
  )
|> UNION ALL (FROM t1), (FROM t2)

/*-----+
 | key |
 +-----+
 | 1   |
 | 2   |
 | 3   |
 +-----*/
```

The pipe `WITH` operator allows a trailing comma:

```googlesql
SELECT 1 AS key
|> WITH t1 AS (
     SELECT 2
   ), t2 AS (
     SELECT 3
   ),
|> UNION ALL (FROM t1), (FROM t2)

/*-----+
 | key |
 +-----+
 | 1   |
 | 2   |
 | 3   |
 +-----*/
```

[Recursive queries][recursive-queries] are also supported:

```googlesql
SELECT 1 AS key
|> WITH RECURSIVE t AS (
    SELECT 2 AS key
    |> UNION ALL (
      SELECT key + 1 AS key
      FROM t
      WHERE key <= 3
    )
  )
|> UNION ALL (FROM t)

/*-----+
 | key |
 +-----+
 | 1   |
 | 2   |
 | 3   |
 | 4   |
 +-----*/
```

[with-clause]: https://github.com/google/googlesql/blob/master/docs/query-syntax.md#with_clause

[recursive-queries]: https://github.com/google/googlesql/blob/master/docs/query-syntax.md#recursive_cte

### `PIVOT` pipe operator 
<a id="pivot_pipe_operator"></a>

<pre class="lang-sql prettyprint no-copy">
|> <span class="kwd">PIVOT</span> (aggregate_expression FOR input_column IN (pivot_column [, ...])) [[AS] alias]
</pre>

**Description**

Rotates rows into columns. The `PIVOT` pipe operator behaves the same as the
[`PIVOT` operator][pivot-operator] in standard syntax.

**Example**

```googlesql
(
  SELECT "kale" AS product, 51 AS sales, "Q1" AS quarter
  UNION ALL
  SELECT "kale" AS product, 4 AS sales, "Q1" AS quarter
  UNION ALL
  SELECT "kale" AS product, 45 AS sales, "Q2" AS quarter
  UNION ALL
  SELECT "apple" AS product, 8 AS sales, "Q1" AS quarter
  UNION ALL
  SELECT "apple" AS product, 10 AS sales, "Q2" AS quarter
)
|> PIVOT(SUM(sales) FOR quarter IN ('Q1', 'Q2'));

/*---------+----+------+
 | product | Q1 | Q2   |
 +---------+-----------+
 | kale    | 55 | 45   |
 | apple   | 8  | 10   |
 +---------+----+------*/
```

[pivot-operator]: https://github.com/google/googlesql/blob/master/docs/query-syntax.md#pivot_operator

### `UNPIVOT` pipe operator 
<a id="unpivot_pipe_operator"></a>

<pre class="lang-sql prettyprint no-copy">
|> <span class="kwd">UNPIVOT</span> (values_column FOR name_column IN (column_to_unpivot [, ...])) [[AS] alias]
</pre>

**Description**

Rotates columns into rows. The `UNPIVOT` pipe operator behaves the same as the
[`UNPIVOT` operator][unpivot-operator] in standard syntax.

**Example**

```googlesql
(
  SELECT 'kale' as product, 55 AS Q1, 45 AS Q2
  UNION ALL
  SELECT 'apple', 8, 10
)
|> UNPIVOT(sales FOR quarter IN (Q1, Q2));

/*---------+-------+---------+
 | product | sales | quarter |
 +---------+-------+---------+
 | kale    | 55    | Q1      |
 | kale    | 45    | Q2      |
 | apple   | 8     | Q1      |
 | apple   | 10    | Q2      |
 +---------+-------+---------*/
```

[unpivot-operator]: https://github.com/google/googlesql/blob/master/docs/query-syntax.md#unpivot_operator

<!-- disableFinding(LINK_ID) -->

### `WINDOW` pipe operator (DEPRECATED) 
<a id="window_pipe_operator"></a>

Warning: `WINDOW` pipe operator has been deprecated. Use
`EXTEND` pipe operator instead.

<pre class="lang-sql prettyprint no-copy">
|> <span class="kwd">WINDOW</span> window_expression [[AS] alias] [, ...]
</pre>

**Description**

Adds a column with the result of computing the function over some window of
existing rows, similar to calling [window functions][window-functions] in a
`SELECT` list in standard syntax. Existing rows and columns are unchanged. The
window expression must include a window function with an
[`OVER` clause][over-clause].

Alternatively, you can use the [`EXTEND` operator][extend-pipe-operator] for
window functions.

**Example**

```googlesql
(
  SELECT 'apples' AS item, 2 AS sales
  UNION ALL
  SELECT 'bananas' AS item, 5 AS sales
  UNION ALL
  SELECT 'carrots' AS item, 8 AS sales
)
|> WINDOW SUM(sales) OVER() AS total_sales;

/*---------+-------+-------------+
 | item    | sales | total_sales |
 +---------+-------+-------------+
 | apples  | 2     | 15          |
 | bananas | 5     | 15          |
 | carrots | 8     | 15          |
 +---------+-------+-------------*/
```

[window-functions]: https://github.com/google/googlesql/blob/master/docs/window-function-calls.md

[over-clause]: https://github.com/google/googlesql/blob/master/docs/window-function-calls.md#def_over_clause

[extend-pipe-operator]: #extend_pipe_operator

### `MATCH_RECOGNIZE` pipe operator 
<a id="match_recognize_pipe_operator"></a>

<pre class="lang-sql prettyprint no-copy">
|> <span class="kwd">MATCH_RECOGNIZE</span> (
   [ <span class="kwd">PARTITION BY</span> partition_expr [, ... ] ]
   ORDER BY order_expr [ { ASC | DESC } ] [ { <span class="kwd">NULLS FIRST</span> | <span class="kwd">NULLS LAST</span> } ] [, ...]
   <span class="kwd">MEASURES</span> { measures_expr [AS] alias } [, ... ]
   [ <span class="kwd">AFTER MATCH SKIP</span> { <span class="kwd">PAST LAST ROW</span> | <span class="kwd">TO NEXT ROW</span> } ]
   <span class="kwd">PATTERN</span> (pattern)
   <span class="kwd">DEFINE</span> symbol AS boolean_expr [, ... ]
   [ OPTIONS ( [ use_longest_match = { TRUE | FALSE } ] ) ]
)
</pre>

**Description**

Filters and aggregates rows based on matches. A *match* is an ordered sequence
of rows that match a pattern that you specify.
Matching rows works similarly to matching with regular expressions, but
instead of matching characters in a string, the `MATCH_RECOGNIZE` operator finds
matches across rows in a table. The `MATCH_RECOGNIZE` pipe operator behaves the
same as the
[`MATCH_RECOGNIZE` clause][match-recognize-clause] in standard syntax.

**Example**

```googlesql
(
  SELECT 1 as x
  UNION ALL
  SELECT 2
  UNION ALL
  SELECT 3
)
|> MATCH_RECOGNIZE(
   ORDER BY x
   MEASURES
     ARRAY_AGG(high.x) AS high_agg,
     ARRAY_AGG(low.x) AS low_agg
   AFTER MATCH SKIP TO NEXT ROW
   PATTERN (low | high)
   DEFINE
     low AS x <= 2,
     high AS x >= 2
);

/*----------+---------+
 | high_agg | low_agg |
 +----------+---------+
 | NULL     | [1]     |
 | NULL     | [2]     |
 | [3]      | NULL    |
 +----------+---------*/
```

[match-recognize-clause]: https://github.com/google/googlesql/blob/master/docs/query-syntax.md#match_recognize_clause

### `ASSERT` pipe operator 
<a id="assert_pipe_operator"></a>

<pre class="lang-sql prettyprint no-copy">
|> ASSERT expression [, payload_expression [, ...]]
</pre>

**Description**

Evaluates an expression on every row of an input table to verify that the
expression is true for every row. If it is false for at least one row, it
produces an assertion error.

The expression must evaluate to a boolean value. When the expression evaluates
to `TRUE`, the input table passes through the `ASSERT` operator unchanged. When
the expression evaluates to `FALSE` or `NULL`, the query fails with an
`Assertion failed` error.

One or more optional payload expressions can be provided. If the assertion
fails, the payload expression values are computed, converted to strings, and
included in the error message, separated by spaces.

If no payload is provided, the error message includes the SQL text of the
assertion expression.

The `ASSERT` operator has no equivalent operation in standard syntax.
 The [`ASSERT` statement][assert-statement] is
a related feature that verifies that a single expression is true.

**Example**

```googlesql
FROM table
|> ASSERT count != 0, "Count is zero for user", userId
|> SELECT total / count AS average
```

[assert-statement]: https://github.com/google/googlesql/blob/master/docs/debugging-statements.md#assert

### `DESCRIBE` pipe operator 
<a id="describe_pipe_operator"></a>

<pre class="lang-sql prettyprint no-copy">
|> DESCRIBE
</pre>

**Description**

Returns a textual description of the table schema, including column names and
types, and other details about the intermediate state of the query up to that
point. The `DESCRIBE` operator is similar to the [`DESCRIBE`
statement][describe-statement], but the `DESCRIBE` pipe operator describes query
metadata rather a specified object. The `DESCRIBE` operator is useful for
inspecting and debugging a query.

The input query isn't executed. The `DESCRIBE` operator returns metadata about
the query immediately.

The returned description includes the following information, when
applicable:

+   Columns in the current table, with their names, types, and table aliases.
    This result lists the columns that would be returned if the query were
    executed up to this point. Value table columns display `<value>` in each
    table field.
+   Table aliases, with a list of column names available under each.
+   [Pseudocolumns][pseudocolumns],
     meaning columns that are available to
    query but not included by default in the result table or in `SELECT *`
    results.
+   Common table expression (CTE) names, meaning table names from `WITH`
    clauses, and a list of columns available in each.
+   Whether the input table is a value table.
+   Whether the input table is ordered, meaning when the input table has an
    `ORDER BY` clause that's still in effect.

The output is returned as a table with one string column and one row containing
the description. The output formatting and content can change, so be sure not to
rely on stable formatting in the results.

The `DESCRIBE` pipe operator can also be added to the end of a query in standard
syntax to see the output schema.

**Examples**

As a baseline example without the `DESCRIBE` operator, the following query
returns a variety of column values, including unnamed columns:

```googlesql
-- Baseline example without DESCRIBE.

FROM
  (SELECT 1 AS col1, 2 AS col2) AS table1,
  (SELECT 'abc' AS col3, 1.0, 123 AS col5) AS table2
|> EXTEND col1 + col5 AS computed, 'anonymous column';

/*------+------+------+---+------+----------+------------------+
 | col1 | col2 | col3 |   | col5 | computed |                  |
 +------+------+------+---+------+----------+------------------+
 | 1    | 2    | abc  | 1 | 123  | 124      | anonymous column |
 +------+------+------+---+------+----------+------------------*/
```

The following is the same example query, but uses the `DESCRIBE` operator to
provide details about the tables, columns, and types:

```googlesql
-- Same example with DESCRIBE to describe table aliases and columns.

FROM
  (SELECT 1 AS col1, 2 AS col2) AS table1,
  (SELECT 'abc' AS col3, 1.0, 123 AS col5) AS table2
|> EXTEND col1 + col5 AS computed, 'anonymous column'
|> DESCRIBE;

/*------------------------------------+
 | Describe                           |
 +------------------------------------+
 | **Columns**:                       |
 | Table Alias  Column Name  Type     |
 | -----------  -----------  ------   |
 | table1       col1         INT64    |
 | table1       col2         INT64    |
 | table2       col3         STRING   |
 | table2       <unnamed>    DOUBLE   |
 | table2       col5         INT64    |
 |              computed     INT64    |
 |              <unnamed>    STRING   |
 |                                    |
 | **Table Aliases**:                 |
 | Table Alias  Columns               |
 | -----------  --------------------- |
 | table1       col1, col2            |
 | table2       col3, <unnamed>, col5 |
 +------------------------------------*/
```

The following example describes a query that uses a CTE and ordering:

```googlesql
-- Describe CTEs and ordering.

WITH cte AS (SELECT 1 AS col1, 'abc' AS col2)
FROM cte AS table1
|> SELECT col1
|> ORDER BY col1
|> DESCRIBE;

/*------------------------------------+
 | Describe                           |
 +------------------------------------+
 | **Columns**:                       |
 | Column Name  Type                  |
 | -----------  -----                 |
 | col1         INT64                 |
 |                                    |
 | **Common table expressions**:      |
 | Name  Columns                      |
 | ----  ----------                   |
 | cte   col1, col2                   |
 |                                    |
 | Result is ordered.                 |
 +------------------------------------*/
```

The following example describes a query that uses a value table:

```googlesql
-- Describe a value table.

FROM (SELECT AS STRUCT 1 AS x, 'abc' AS y) AS value_table
|> DESCRIBE;

/*-----------------------------------------------------+
 | Describe                                            |
 +-----------------------------------------------------+
 | **Columns**:                                        |
 | Table Alias  Column Name  Type                      |
 | -----------  -----------  ------------------------- |
 | value_table  <value>      STRUCT<x INT64, y STRING> |
 |                                                     |
 | **Table Aliases**:                                  |
 | Table Alias  Columns                                |
 | -----------  -------                                |
 | value_table  <value>                                |
 |                                                     |
 | Result is a value table.                            |
 +-----------------------------------------------------*/
```

[describe-statement]: https://github.com/google/googlesql/blob/master/docs/utility-statements.md#describedesc

[pseudocolumns]: https://github.com/google/googlesql/blob/master/docs/data-model.md#pseudo_columns

### `IF` pipe operator 
<a id="if_pipe_operator"></a>

<pre class="lang-sql prettyprint no-copy">
|> IF condition THEN ( subpipeline )
   [ ELSEIF condition THEN ( subpipeline ) ] ...
   [ ELSE ( subpipeline ) ]
</pre>

**Description**

Runs operators (written as a [subpipeline][subpipelines]) conditionally,
optionally including `ELSEIF` or `ELSE` clauses that define additional cases.
Each case has an associated subpipeline that defines what to do if that
case is selected.

The first case with a condition that evaluates to `true` is selected. If no
conditions are true, the `ELSE` case, if present, is selected. The subpipeline
for the selected case is executed, and its output is passed on as the output of
the `IF` operator. If no case is selected, the input table is passed on
without changes.

The output schema depends on which case is selected by the condition
evaluation.

The subpipelines for unselected cases aren't validated. For example, unselected
cases can contain references to invalid column names without causing an error.

The conditions used in the `IF` operator must be constant values available
during query analysis. The following are valid value types:

+ `true` or `false` literals
+ [Scripting variables][script-variables]
+ SQL constants created with the [`CREATE CONSTANT`][create-constant] statement
+ Expressions using the `NOT x` [logical operator][logical-operator], where
  `x` is a value type from this list

The following value types aren't supported:

+ TVF arguments
+ Expressions more complex than `NOT x`
+ [Query parameters][query-parameters]

**Examples**

```googlesql
FROM Produce
|> IF true THEN
  (
    |> AGGREGATE COUNT(*) AS total
  ) ELSE
  (
    |> SELECT *  -- Never evaluated
  )
|> AS Inventory;

-- The first subpipeline is selected, resulting in this AGGREGATE output.
/*---------+
 | total   |
 +---------+
 | 4       |
 +---------*/
```

```googlesql
FROM Produce
-- No case is selected, causing IF to be no-op.
|> IF false THEN
    (
      -- This doesn't raise an error, because the case isn't evaluated.
      |> SELECT non_existent_column
    )
|> SELECT *;

-- The input to IF is passed on without change to the SELECT pipe operator,
-- resulting in this output.
/*---------+-------+-----------+
 | item    | sales | category  |
 +---------+-------+-----------+
 | apples  | 2     | fruit     |
 | carrots | 8     | vegetable |
 | apples  | 7     | fruit     |
 | bananas | 5     | fruit     |
 +---------+-------+-----------*/
```

[subpipelines]: https://github.com/google/googlesql/blob/master/docs/pipe-syntax.md#subpipelines

[script-variables]: https://github.com/google/googlesql/blob/master/docs/variables.md

[query-parameters]: https://github.com/google/googlesql/blob/master/docs/variables.md#query-parameters

[create-constant]: https://github.com/google/googlesql/blob/master/docs/data-definition-language.md#create_constant

[logical-operator]: https://github.com/google/googlesql/blob/master/docs/operators.md#logical_operators

### `CREATE TABLE` terminal pipe operator 
<a id="create_table_pipe_operator"></a>

<pre class="lang-sql prettyprint no-copy">
|> CREATE
   [ OR REPLACE ]
   [ TEMP | TEMPORARY ]
   TABLE
   [ IF NOT EXISTS ]
   table_name [ ( <span class="var">table_element</span>, ... ) ]
   [ PARTITION [ hints ] BY partition_expression, ... ]
   [ CLUSTER [ hints ] BY cluster_expression, ... ]
   [ OPTIONS (key=value, ...) ]
</pre>

**Description**

Creates a table and populates it with the input pipe table content. The
`CREATE TABLE` operator has the same syntax and behaves the same as the
[`CREATE TABLE ... AS query` statement][create-table] in standard syntax.
However, the final `AS query` is omitted in pipe syntax because the data
instead comes from the pipe input table.

The `CREATE TABLE` operator is a [terminal pipe operator][terminal-operators].

**Example**

```googlesql
FROM Produce
|> AGGREGATE SUM(sales) AS total_sales GROUP BY category
|> CREATE TEMP TABLE ShopSales;

-- No output is produced. The results of AGGREGATE are collected in the
-- temporary table ShopSales.
```

The previous example is equivalent to the following query that uses a
standard `CREATE TABLE` statement:

```googlesql
-- Equivalent query with standard CREATE TABLE statement

CREATE TEMP TABLE ShopSales AS
FROM Produce
|> AGGREGATE SUM(sales) AS total_sales GROUP BY category;
```

[create-table]: https://github.com/google/googlesql/blob/master/docs/data-definition-language.md#create_table

[terminal-operators]: #terminal_operators

### `EXPORT DATA` terminal pipe operator 
<a id="export_data_pipe_operator"></a>

<pre class="lang-sql prettyprint no-copy">
|> EXPORT DATA
   [ OPTIONS (key=value, ...) ]
</pre>

**Description**

Writes the pipe table contents to a target specified by the options. The
`EXPORT DATA` operator operator has the same syntax and behaves the same as the
[`EXPORT DATA ... AS query` statement][export-data] in standard syntax.
However, the final `AS query` is omitted in pipe syntax because the data
instead comes from the pipe input table.

The `EXPORT DATA` operator is a [terminal pipe operator][terminal-operators].

[export-data]: https://github.com/google/googlesql/blob/master/docs/import-export.md#export_data_statement

[terminal-operators]: https://github.com/google/googlesql/blob/master/docs/pipe-syntax.md#terminal_operators

### `INSERT` terminal pipe operator 
<a id="insert_pipe_operator"></a>

<pre class="lang-sql prettyprint no-copy">
|> INSERT [ [ OR ] IGNORE | REPLACE | UPDATE ] [ INTO ] target_name
   ( column[, ...] )
   [ ASSERT_ROWS_MODIFIED m ] [ then_return_clause ]
</pre>

**Description**

Inserts rows from the pipe input table into a target table. The `INSERT`
operator has the same syntax and behaves the same as the
[`INSERT ... SELECT` statement][insert] in standard syntax.
However, the final query or `VALUES` is omitted in pipe syntax because the data
instead comes from the pipe input table.

The `INSERT` operator is a [terminal pipe operator][terminal-operators].

**Example**

```googlesql
FROM Produce
|> AGGREGATE SUM(sales) AS total_sales GROUP BY category
|> INSERT INTO ShopSales;

-- No output is produced. The results of AGGREGATE are inserted into the
-- existing table ShopSales as new rows.
```

[insert]: https://github.com/google/googlesql/blob/master/docs/data-manipulation-language.md#insert_statement

[terminal-operators]: https://github.com/google/googlesql/blob/master/docs/pipe-syntax.md#terminal_operators

### `FORK` terminal pipe operator 
<a id="fork_pipe_operator"></a>

<pre class="lang-sql prettyprint no-copy">
|> FORK ( subpipeline ) [, ( subpipeline )]...
</pre>

**Description**

Ends the main pipeline and splits it into one or more
[subpipelines][subpipelines]. The input table is computed once, and a copy of
the results is passed to each subpipeline. The `FORK` operator is a [terminal
pipe operator][terminal-operators].

The `FORK` operator behaves as if the subpipelines are run sequentially, but
without exposing any side effects from the subpipelines until the end. For
example, if one `FORK` subpipeline creates or updates a table, other
subpipelines won't see those changes. Side effects from terminal operators in
subpipelines, such as the [`CREATE TABLE`][create-table-pipe-operator] or
[`INSERT`][insert-pipe-operator] operators, are applied atomically for the
entire statement.

The output tables from the subpipelines are returned sequentially in the order
the `FORK` subpipelines are written in the query.

The `FORK` operator can't be followed by other pipe operators. The `FORK`
operator can't be used in a subquery or anywhere a single output table is
expected, even if the `FORK` operator has only one subpipeline.

Row order in the input table is not preserved in the subpipelines.

**Examples**

The following example performs multiple aggregations on a common input and also
returns the unaggregated rows, producing three separate output tables. This
example is similar to using `GROUPING SETS` in standard syntax.

```googlesql
WITH
  Fruit AS (
    SELECT 'apple' AS item, 100 AS sales
    UNION ALL
    SELECT 'banana' AS item, 150 AS sales
  )
FROM Fruit
|> FORK
    (|> AGGREGATE COUNT(*) AS total_fruits),
    (|> AGGREGATE COUNT(*) AS count_by_item GROUP BY item),
    (|> SELECT item, sales);

-- This query produces three output tables, one from each subpipeline:
-- 1. The total count of fruits
/*--------------+
 | total_fruits |
 +--------------+
 | 2            |
 +--------------*/

-- 2. The count of each fruit item
/*--------+---------------+
 | item   | count_by_item |
 +--------+---------------+
 | apple  | 1             |
 | banana | 1             |
 +--------+---------------*/

-- 3. The item and sales for each fruit
/*--------+-------+
 | item   | sales |
 +--------+-------+
 | apple  | 100   |
 | banana | 150   |
 +--------+-------*/
```

The `FORK` operator provides a convenient way to generate multiple outputs from
a single statement. Without the `FORK` operator, you would need to use temporary
tables to achieve the same result, as illustrated in the following example:

```googlesql
-- Equivalent query using temporary tables in standard syntax.

-- Statement 0: Create a common table.
CREATE TEMP TABLE Fruit
SELECT 'apple' AS item, 100 AS sales
UNION ALL
SELECT 'banana' AS item, 150 AS sales;

-- Statement 1: Get the total count.
SELECT AGGREGATE COUNT(*) AS total_fruits
FROM Fruit;

-- Statement 2: Get the count of each fruit item.
SELECT AGGREGATE COUNT(*) AS count_by_item
GROUP BY item
FROM Fruit;

-- Statement 3: Get the raw data.
SELECT item, sales
FROM Fruit;
```

The following example uses the `FORK` operator to create a table in one
subpipeline, while returning unaggregated rows as a query result in the other
subpipeline:

```googlesql
WITH
  Fruit AS (
    SELECT 'apple' AS item, 100 AS sales
    UNION ALL
    SELECT 'banana' AS item, 150 AS sales
  )
FROM Fruit
|> FORK
    (
      |> AGGREGATE COUNT(*) AS count_by_item GROUP BY item
      |> CREATE TABLE FruitCounts),
    (|> SELECT *);
```

The first subpipeline creates a `FruitCounts` table with the following rows:

```googlesql
-- Initial FruitCounts table
/*--------+---------------+
 | item   | count_by_item |
 +--------+---------------+
 | apple  | 1             |
 | banana | 1             |
 +--------+---------------*/
```

The second subpipeline produces the following result table:

```googlesql
-- Final result table
/*--------+-------+
 | item   | sales |
 +--------+-------+
 | apple  | 100   |
 | banana | 150   |
 +--------+-------*/
```

[subpipelines]: https://github.com/google/googlesql/blob/master/docs/pipe-syntax.md#subpipelines

[terminal-operators]: #terminal_operators

[create-table-pipe-operator]: #create_table_pipe_operator

[insert-pipe-operator]: #insert_pipe_operator

### `TEE` semi-terminal pipe operator 
<a id="tee_pipe_operator"></a>

<pre class="lang-sql prettyprint no-copy">
-- Return the current pipe input table as an additional result table.
|> TEE
</pre>

<pre class="lang-sql prettyprint no-copy">
-- Run one or more subpipelines over the pipe input table and return their output as additional result tables.
|> TEE ( subpipeline ) [, ( subpipeline ) ] ...
</pre>

**Description**

Splits the main pipeline into one or more [subpipelines][subpipelines] using a
copy of the input table, and passes the original input table to the next
operator in the main pipeline. If the `TEE` operator is the last operator in a
pipeline, the query returns the `TEE` output table, which is a copy of the pipe
input table, as the query result. The `TEE` operator is a [semi-terminal pipe
operator][semi-terminal-operators].

The `TEE` operator with no arguments (`|> TEE`) outputs the current input table
as a side output and then continues the main pipeline. This syntax is
equivalent to using an empty subpipeline (`|> TEE ()`) and is useful for query
debugging.

The `TEE` operator behaves as if the subpipelines are run sequentially, but
without exposing any side effects from the subpipelines until the end of
the execution. For example, if one `TEE` subpipeline creates or updates a table,
other subpipelines won't see those changes. Side effects from terminal operators
in subpipelines, such as the [`CREATE TABLE`][create-table-pipe-operator] or
[`INSERT`][insert-pipe-operator] operators, are applied atomically for the
entire statement.

The output tables from the subpipelines are returned sequentially in the order
the `TEE` subpipelines are written in the query, before the output of the main
pipeline that continues after `TEE`.

The input table's order isn't preserved in the subpipelines or in the output of
the `TEE` operator to the main pipeline.

**Examples**

The following example produces two output tables for two different aggregations
of an input table, and then a third output table with the unaggregated rows from
the main pipeline. This example is similar to using `GROUPING SETS` in
standard syntax.

```googlesql
WITH
  Fruit AS (
    SELECT 'apple' AS item, 100 AS sales
    UNION ALL
    SELECT 'banana' AS item, 150 AS sales
  )
FROM Fruit
|> TEE
    (|> AGGREGATE COUNT(*) AS total_fruits),
    (|> AGGREGATE COUNT(*) AS count_by_item GROUP BY item)
|> SELECT *;

-- This query produces three output tables, one from each subpipeline and one from the main pipeline:
-- 1. The total count of fruits (from the first subpipeline)
/*--------------+
 | total_fruits |
 +--------------+
 | 2            |
 +--------------*/

-- 2. The count of each fruit item (from the second subpipeline)
/*--------+---------------+
 | item   | count_by_item |
 +--------+---------------+
 | apple  | 1             |
 | banana | 1             |
 +--------+---------------*/

-- 3. The item and sales for each fruit (from the main pipeline)
/*--------+-------+
 | item   | sales |
 +--------+-------+
 | apple  | 100   |
 | banana | 150   |
 +--------+-------*/
```

The `TEE` operator provides a convenient way to generate multiple outputs from a
single statement. Without the `TEE` operator, you would need to use temporary
tables to achieve the same result, as illustrated in the following example:

```googlesql
-- Equivalent query using temporary tables in standard syntax.

-- Statement 0: Create a common table.
CREATE TEMP TABLE Fruit
SELECT 'apple' AS item, 100 AS sales
UNION ALL
SELECT 'banana' AS item, 150 AS sales;

-- Statement 1: Get the total count.
SELECT AGGREGATE COUNT(*) AS total_fruits
FROM Fruit;

-- Statement 2: Get the count of each fruit item.
SELECT AGGREGATE COUNT(*) AS count_by_item
GROUP BY item
FROM Fruit;

-- Statement 3: Get the raw data.
SELECT *
FROM Fruit;
```

The following example uses the `TEE` operator for data inspection. The first
`TEE` operator uses the no-argument form, which outputs the intermediate table
from before the aggregation for inspection. You can also use the `TEE` operator
with subpipelines for custom data inspection, as shown by the second `TEE`
operator.

```googlesql
WITH
  Fruit AS (
    SELECT 'apple' AS item, 100 AS sales
    UNION ALL
    SELECT 'banana' AS item, 150 AS sales
  )
FROM Fruit
|> TEE
|> TEE
    (|> AGGREGATE COUNT(*) AS num_rows)
|> AGGREGATE SUM(sales) AS total_sales;

-- This query produces three output tables, one from each TEE operator and one from the main pipeline:
-- 1. The full Fruit table (from the first TEE operator)
/*--------+-------+
 | item   | sales |
 +--------+-------+
 | apple  | 100   |
 | banana | 150   |
 +--------+-------*/

-- 2. The number of rows (from the second TEE operator)
/*----------+
 | num_rows |
 +----------+
 | 2        |
 +----------*/

-- 3. The total sales (from the main pipeline)
/*-------------+
 | total_sales |
 +-------------+
 | 250         |
 +-------------*/
```

The following example uses the `TEE` operator to create a table in a
subpipeline, while the main pipeline continues and returns the unaggregated
rows as a query result:

```googlesql
WITH
  Fruit AS (
    SELECT 'apple' AS item, 100 AS sales
    UNION ALL
    SELECT 'banana' AS item, 150 AS sales
  )
FROM Fruit
|> TEE (
     |> AGGREGATE COUNT(*) AS count_by_item GROUP BY item
     |> CREATE TABLE FruitCounts
   )
|> SELECT *;
```

The subpipeline creates a `FruitCounts` table with the following rows:

```googlesql
-- Initial FruitCounts table
/*--------+---------------+
 | item   | count_by_item |
 +--------+---------------+
 | apple  | 1             |
 | banana | 1             |
 +--------+---------------*/
```

The main pipeline produces the following result table:

```googlesql
/*--------+-------+
 | item   | sales |
 +--------+-------+
 | apple  | 100   |
 | banana | 150   |
 +--------+-------*/
```

[subpipelines]: https://github.com/google/googlesql/blob/master/docs/pipe-syntax.md#subpipelines

[terminal-operators]: https://github.com/google/googlesql/blob/master/docs/pipe-syntax.md#terminal_operators

[semi-terminal-operators]: https://github.com/google/googlesql/blob/master/docs/pipe-syntax.md#semi_terminal_operators

[create-table-pipe-operator]: #create_table_pipe_operator

[insert-pipe-operator]: #insert_pipe_operator

[query-syntax]: https://github.com/google/googlesql/blob/master/docs/query-syntax.md

[pipe-syntax-guide]: https://github.com/google/googlesql/blob/master/docs/pipe-syntax-guide.md

[pipe-syntax-paper]: https://research.google/pubs/sql-has-problems-we-can-fix-them-pipe-syntax-in-sql/

[from-queries]: #from_queries

[from-clause]: https://github.com/google/googlesql/blob/master/docs/query-syntax.md#from_clause

[using-aliases]: https://github.com/google/googlesql/blob/master/docs/query-syntax.md#using_aliases

[select-star]: https://github.com/google/googlesql/blob/master/docs/query-syntax.md#select_

[query-comparison]: #query_comparison

[pipe-operators]: #pipe_operators

[value-tables]: https://github.com/google/googlesql/blob/master/docs/data-model.md#value_tables

[select-as-value]: https://github.com/google/googlesql/blob/master/docs/query-syntax.md#select_as_value

[subqueries]: https://github.com/google/googlesql/blob/master/docs/subqueries.md

