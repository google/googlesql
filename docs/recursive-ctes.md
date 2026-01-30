

<!-- mdlint off(WHITESPACE_LINE_LENGTH) -->

# Work with recursive CTEs

In GoogleSQL, a `WITH` clause contains one or more common table
expressions (CTEs) that you can reference in a query
expression. CTEs can be [non-recursive][non-recursive-cte],
[recursive][recursive-cte], or both. The [`RECURSIVE`][recursive-keyword]
keyword enables recursion in the `WITH` clause (`WITH RECURSIVE`).

A recursive CTE can reference itself, a preceding CTE, or a subsequent CTE. A
non-recursive CTE can reference only preceding CTEs and can't reference itself.
Recursive CTEs run continuously until no new results are found, while
non-recursive CTEs run once. For these reasons, recursive CTEs are commonly used
for querying hierarchical data and graph data.

For example, consider a graph where each row represents a node that can link to
other nodes. To find the transitive closure of all reachable nodes from a
particular start node without knowing the maximum number of hops, you would need
a recursive CTE in the query (`WITH RECURSIVE`). The recursive query would start
with the base case of the start node, and each step would compute the new unseen
nodes that can be reached from all the nodes seen so far up to the previous
step. The query concludes when no new nodes can be found.

However, recursive CTEs can be computationally expensive, so before you use
them, review this guide and the [`WITH` clause][with-clause] section of the
GoogleSQL reference documentation.

## Create a recursive CTE 
<a id="create-recursive-cte"></a>

To create a recursive CTE in GoogleSQL, use the
[`WITH RECURSIVE` clause][with-clause] as shown in the following example:

```googlesql
WITH RECURSIVE
  CTE_1 AS (
    (SELECT 1 AS iteration UNION ALL SELECT 1 AS iteration)
    UNION ALL
    SELECT iteration + 1 AS iteration FROM CTE_1 WHERE iteration < 3
  )
SELECT iteration FROM CTE_1
ORDER BY 1 ASC
```

The preceding example produces the following results:

```googlesql
/*-----------+
 | iteration |
 +-----------+
 | 1         |
 | 1         |
 | 2         |
 | 2         |
 | 3         |
 | 3         |
 +-----------*/
```

To avoid duplicate rows so that only distinct rows become part of the final CTE
result, use `UNION DISTINCT` instead of `UNION ALL`:

```googlesql
WITH RECURSIVE
  CTE_1 AS (
    (SELECT 1 AS iteration UNION ALL SELECT 1 AS iteration)
    UNION DISTINCT
    SELECT iteration + 1 AS iteration FROM CTE_1 WHERE iteration < 3
  )
SELECT iteration FROM CTE_1
ORDER BY 1 ASC
```

The preceding example produces the following results:

```googlesql
/*-----------+
 | iteration |
 +-----------+
 | 1         |
 | 2         |
 | 3         |
 +-----------*/
```

A recursive CTE includes a base term, a union operator, and a recursive term.
The base term runs the first iteration of the recursive union operation. The
recursive term runs the remaining iterations and must include one self-reference
to the recursive CTE. Only the recursive term can include a self-reference.

In the preceding example, the recursive CTE contains the following components:

+   Recursive CTE name: `CTE_1`
+   Base term: `SELECT 1 AS iteration`
+   Union operator: `UNION ALL` (or
    `UNION DISTINCT` for only distinct rows)
+   Recursive term: `SELECT iteration + 1 AS iteration FROM CTE_1 WHERE
    iteration < 3`

To learn more about the recursive CTE syntax, rules, and examples, see [`WITH`
clause][with-clause] in the GoogleSQL reference documentation.

## Explore reachability in a directed acyclic graph (DAG) 
<a id="explore-recursive-cte-dag"></a>

You can use a recursive query to explore reachability in a
directed acyclic graph (DAG). The following query finds all nodes that can be
reached from node `5` in a graph called `GraphData`:

```googlesql
WITH RECURSIVE
  GraphData AS (
    --    1          5
    --   / \        / \
    --  2 - 3      6   7
    --      |       \ /
    --      4        8
    SELECT 1 AS from_node, 2 AS to_node UNION ALL
    SELECT 1, 3 UNION ALL
    SELECT 2, 3 UNION ALL
    SELECT 3, 4 UNION ALL
    SELECT 5, 6 UNION ALL
    SELECT 5, 7 UNION ALL
    SELECT 6, 8 UNION ALL
    SELECT 7, 8
  ),
  R AS (
    (SELECT 5 AS node)
    UNION ALL
    (
      SELECT GraphData.to_node AS node
      FROM R
      INNER JOIN GraphData
        ON (R.node = GraphData.from_node)
    )
  )
SELECT DISTINCT node FROM R ORDER BY node;
```

The preceding example produces the following results:

```googlesql
/*------+
 | node |
 +------+
 | 5    |
 | 6    |
 | 7    |
 | 8    |
 +------*/
```

## Troubleshoot iteration limit errors 
<a id="troubleshoot"></a>

Recursive CTEs can result in infinite recursion, which occurs when the recursive
term executes continuously without meeting a termination condition. To terminate
infinite recursions, a limit of iterations for each recursive CTE is
enforced. The iteration limit is determined by your
query engine. Once a recursive CTE reaches
the maximum number of iterations, the CTE execution is aborted with an error.

This limit exists because the computation of a recursive CTE can be expensive,
and running a CTE with a large number of iterations consumes a lot of system
resources and takes a much longer time to finish.

Queries that reach the iteration limit are usually missing a proper termination
condition, thus creating an infinite loop, or using recursive CTEs in
inappropriate scenarios.

If you experience a recursion iteration limit error, review the suggestions in
this section.

### Check for infinite recursion 
<a id="check-infinite-recursion"></a>

To prevent infinite recursion, make sure the recursive term is
able to produce an empty result after executing a certain number of iterations.

One way to check for infinite recursion is to
convert your recursive CTE to a `TEMP TABLE` with a `REPEAT` loop for the
first `100` iterations, as follows:

<pre class="lang-sql prettyprint notranslate">
DECLARE current_iteration INT64 DEFAULT 0;

CREATE TEMP TABLE <var>recursive_cte_name</var> AS
SELECT <var>base_expression</var>, current_iteration AS iteration;

REPEAT
  SET current_iteration = current_iteration + 1;
  INSERT INTO <var>recursive_cte_name</var>
    SELECT <var>recursive_expression</var>, current_iteration
    FROM <var>recursive_cte_name</var>
    WHERE <var>termination_condition_expression</var>
      AND iteration = current_iteration - 1
      AND current_iteration < 100;
  UNTIL NOT EXISTS(SELECT * FROM <var>recursive_cte_name</var> WHERE iteration = current_iteration)
END REPEAT;
</pre>

Replace the following values:

+   `recursive_cte_name`: The recursive CTE to debug.
+   `base_expression`: The base term of the recursive CTE.
+   `recursive_expression`: The recursive term of the recursive CTE.
+   `termination_condition_expression`: The termination expression of the
    recursive CTE.

For example, consider the following recursive CTE called `TestCTE`:

```googlesql
WITH RECURSIVE
  TestCTE AS (
    SELECT 1 AS n
    UNION ALL
    SELECT n + 3 FROM TestCTE WHERE MOD(n, 6) != 0
  )
```

This example uses the following values:

+   `recursive_cte_name`: `TestCTE`
+   `base_expression`: `SELECT 1`
+   `recursive_expression`: `n + 3`
+   `termination_condition_expression`: `MOD(n, 6) != 0`

The following code would therefore test the `TestCTE` for infinite recursion:

<pre class="lang-sql prettyprint notranslate">
DECLARE current_iteration INT64 DEFAULT 0;

CREATE TEMP TABLE TestCTE AS
SELECT 1 AS n, current_iteration AS iteration;

REPEAT
SET current_iteration = current_iteration + 1;

INSERT INTO TestCTE
SELECT n + 3, current_iteration
FROM TestCTE
WHERE
  MOD(n, 6) != 0
  AND iteration = current_iteration - 1
  AND current_iteration < 10;

UNTIL
  NOT EXISTS(SELECT * FROM TestCTE WHERE iteration = current_iteration)
    END REPEAT;

-- Print the number of rows produced by each iteration

SELECT iteration, COUNT(1) AS num_rows
FROM TestCTE
GROUP BY iteration
ORDER BY iteration;

-- Examine the actual result produced for a specific iteration

SELECT * FROM TestCTE WHERE iteration = 2;
</pre>

The preceding example produces the following results that include the
iteration ID and the number of rows that were produced during that iteration:

```googlesql
/*-----------+----------+
 | iteration | num_rows |
 +-----------+----------+
 | 0         | 1        |
 | 1         | 1        |
 | 2         | 1        |
 | 3         | 1        |
 | 4         | 1        |
 | 5         | 1        |
 | 6         | 1        |
 | 7         | 1        |
 | 8         | 1        |
 | 9         | 1        |
 | 10        | 1        |
 +-----------+----------*/
```

These are the actual results produced during iteration `2`:

```googlesql
/*----------+-----------+
 | n        | iteration |
 +----------+-----------+
 | 7        | 2         |
 +----------+-----------*/
```

If the number of rows is always greater than zero, which is true in this
example, then the sample likely has an infinite recursion.

### Verify the appropriate usage of the recursive CTE 
<a id="verify-cte-usage"></a>

Verify that you're using the recursive CTE in an appropriate scenario.
Recursive CTEs can be expensive to compute because they're designed to query
hierarchical data and graph data. If you aren't querying these two kinds of
data, consider alternatives, such as using the
[`LOOP` statement][loop-statement] with a non-recursive CTE.

### Split a recursive CTE into multiple recursive CTEs 
<a id="split-ctes"></a>

If you think your recursive CTE needs more than the maximum allowed
iterations, you might be able to break down your recursive CTE into multiple
recursive CTEs.

You can split a recursive CTE with a query structure similar to the following:

<pre class="lang-sql prettyprint notranslate">
WITH RECURSIVE
  CTE_1 AS (
    SELECT <var>base_expression</var>
    UNION ALL
    SELECT <var>recursive_expression</var> FROM CTE_1 WHERE iteration < 500
  ),
  CTE_2 AS (
    SELECT * FROM CTE_1 WHERE iteration = 500
    UNION ALL
    SELECT <var>recursive_expression</var> FROM CTE_2 WHERE iteration < 500 * 2
  ),
  CTE_3 AS (
    SELECT * FROM CTE_2 WHERE iteration = 500 * 2
    UNION ALL
    SELECT <var>recursive_expression</var> FROM CTE_3 WHERE iteration < 500 * 3
  ),
  <var>[, ...]</var>
SELECT * FROM CTE_1
UNION ALL SELECT * FROM CTE_2 WHERE iteration > 500
UNION ALL SELECT * FROM CTE_3 WHERE iteration > 500 * 2
<var>[...]</var>
</pre>

Replace the following values:

* `base_expression`: The base term expression for the current CTE.
* `recursive_expression`: The recursive term expression for the current CTE.

For example, the following code splits a CTE into three distinct CTEs:

```googlesql
WITH RECURSIVE
  CTE_1 AS (
    SELECT 1 AS iteration
    UNION ALL
    SELECT iteration + 1 AS iteration FROM CTE_1 WHERE iteration < 10
  ),
  CTE_2 AS (
    SELECT * FROM CTE_1 WHERE iteration = 10
    UNION ALL
    SELECT iteration + 1 AS iteration FROM CTE_2 WHERE iteration < 10 * 2
  ),
  CTE_3 AS (
    SELECT * FROM CTE_2 WHERE iteration = 10 * 2
    UNION ALL
    SELECT iteration + 1 AS iteration FROM CTE_3 WHERE iteration < 10 * 3
  )
SELECT iteration FROM CTE_1
UNION ALL
SELECT iteration FROM CTE_2 WHERE iteration > 10
UNION ALL
SELECT iteration FROM CTE_3 WHERE iteration > 20
ORDER BY 1 ASC
```

In the preceding example, 500 iterations is replaced with 10 iterations
so that it's faster to see the results of the query. The query produces 30 rows,
but each recursive CTE only iterates 10 times. The output looks like the
following:

```googlesql
/*-----------+
 | iteration |
 +-----------+
 | 2         |
 | ...       |
 | 30        |
 +-----------*/
```

You could test the previous query on much larger iterations.

### Use a loop instead of a recursive CTE 
<a id="use-a-loop"></a>

To avoid iteration limits, consider using a loop instead of a recursive CTE.
You can create a loop with one of several loop statements,
such as `LOOP`, `REPEAT`, or `WHILE`. For more information, see
[Loops][loops].

<!-- mdlint off(WHITESPACE_LINE_LENGTH) -->

[with-clause]: https://github.com/google/googlesql/blob/master/docs/query-syntax.md#with_clause

[recursive-cte]: https://github.com/google/googlesql/blob/master/docs/query-syntax.md#recursive_cte

[recursive-keyword]: https://github.com/google/googlesql/blob/master/docs/query-syntax.md#recursive_keyword

[non-recursive-cte]: https://github.com/google/googlesql/blob/master/docs/query-syntax.md#simple_cte

[loop-statement]: https://github.com/google/googlesql/blob/master/docs/procedural-language.md#loop

[loops]: https://github.com/google/googlesql/blob/master/docs/procedural-language.md#loops

<!-- mdlint on -->

