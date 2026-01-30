

<!-- mdlint off(WHITESPACE_LINE_LENGTH) -->

# Aggregate functions

GoogleSQL supports the following general aggregate functions.
To learn about the syntax for aggregate function calls, see
[Aggregate function calls][agg-function-calls].

## Function list

<table>
  <thead>
    <tr>
      <th>Name</th>
      <th>Summary</th>
    </tr>
  </thead>
  <tbody>

<tr>
  <td><a href="https://github.com/google/googlesql/blob/master/docs/aggregate_functions.md#any_value"><code>ANY_VALUE</code></a>
</td>
  <td>
    Gets an expression for some row.
  </td>
</tr>

<tr>
  <td><a href="https://github.com/google/googlesql/blob/master/docs/approximate_aggregate_functions.md#approx_count_distinct"><code>APPROX_COUNT_DISTINCT</code></a>
</td>
  <td>
    Gets the approximate result for <code>COUNT(DISTINCT expression)</code>.
    <br>For more information, see <a href="https://github.com/google/googlesql/blob/master/docs/approximate_aggregate_functions.md">Approximate aggregate functions</a>.

  </td>
</tr>

<tr>
  <td><a href="https://github.com/google/googlesql/blob/master/docs/approximate_aggregate_functions.md#approx_quantiles"><code>APPROX_QUANTILES</code></a>
</td>
  <td>
    Gets the approximate quantile boundaries.
    <br>For more information, see <a href="https://github.com/google/googlesql/blob/master/docs/approximate_aggregate_functions.md">Approximate aggregate functions</a>.

  </td>
</tr>

<tr>
  <td><a href="https://github.com/google/googlesql/blob/master/docs/approximate_aggregate_functions.md#approx_top_count"><code>APPROX_TOP_COUNT</code></a>
</td>
  <td>
    Gets the approximate top elements and their approximate count.
    <br>For more information, see <a href="https://github.com/google/googlesql/blob/master/docs/approximate_aggregate_functions.md">Approximate aggregate functions</a>.

  </td>
</tr>

<tr>
  <td><a href="https://github.com/google/googlesql/blob/master/docs/approximate_aggregate_functions.md#approx_top_sum"><code>APPROX_TOP_SUM</code></a>
</td>
  <td>
    Gets the approximate top elements and sum, based on the approximate sum
    of an assigned weight.
    <br>For more information, see <a href="https://github.com/google/googlesql/blob/master/docs/approximate_aggregate_functions.md">Approximate aggregate functions</a>.

  </td>
</tr>

<tr>
  <td><a href="https://github.com/google/googlesql/blob/master/docs/aggregate_functions.md#array_agg"><code>ARRAY_AGG</code></a>
</td>
  <td>
    Gets an array of values.
    
  </td>
</tr>

<tr>
  <td><a href="https://github.com/google/googlesql/blob/master/docs/aggregate_functions.md#array_concat_agg"><code>ARRAY_CONCAT_AGG</code></a>
</td>
  <td>
    Concatenates arrays and returns a single array as a result.
    
  </td>
</tr>

<tr>
  <td><a href="https://github.com/google/googlesql/blob/master/docs/aggregate_functions.md#avg"><code>AVG</code></a>
</td>
  <td>
    Gets the average of non-<code>NULL</code> values.
    
  </td>
</tr>

<tr>
  <td><a href="https://github.com/google/googlesql/blob/master/docs/aggregate-dp-functions.md#dp_avg"><code>AVG</code> (Differential Privacy)</a>
</td>
  <td>
    <code>DIFFERENTIAL_PRIVACY</code>-supported <code>AVG</code>.<br/><br/>
    Gets the differentially-private average of non-<code>NULL</code>,
    non-<code>NaN</code> values in a query with a
    <code>DIFFERENTIAL_PRIVACY</code> clause.
    <br><br>For more information, see <a href="https://github.com/google/googlesql/blob/master/docs/aggregate-dp-functions.md">Differential privacy functions</a>.

  </td>
</tr>

<tr>
  <td><a href="https://github.com/google/googlesql/blob/master/docs/aggregate_functions.md#bit_and"><code>BIT_AND</code></a>
</td>
  <td>
    Performs a bitwise AND operation on an expression.
    
  </td>
</tr>

<tr>
  <td><a href="https://github.com/google/googlesql/blob/master/docs/aggregate_functions.md#bit_or"><code>BIT_OR</code></a>
</td>
  <td>
    Performs a bitwise OR operation on an expression.
    
  </td>
</tr>

<tr>
  <td><a href="https://github.com/google/googlesql/blob/master/docs/aggregate_functions.md#bit_xor"><code>BIT_XOR</code></a>
</td>
  <td>
    Performs a bitwise XOR operation on an expression.
    
  </td>
</tr>

<tr>
  <td><a href="https://github.com/google/googlesql/blob/master/docs/statistical_aggregate_functions.md#corr"><code>CORR</code></a>
</td>
  <td>
    Computes the Pearson coefficient of correlation of a set of number pairs.
    <br>For more information, see <a href="https://github.com/google/googlesql/blob/master/docs/statistical_aggregate_functions.md">Statistical aggregate functions</a>.

  </td>
</tr>

<tr>
  <td><a href="https://github.com/google/googlesql/blob/master/docs/aggregate_functions.md#count"><code>COUNT</code></a>
</td>
  <td>
    Gets the number of rows in the input, or the number of rows with an
    expression evaluated to any value other than <code>NULL</code>.
  </td>
</tr>

<tr>
  <td><a href="https://github.com/google/googlesql/blob/master/docs/aggregate-dp-functions.md#dp_count"><code>COUNT</code> (Differential Privacy)</a>
</td>
  <td>
    <code>DIFFERENTIAL_PRIVACY</code>-supported <code>COUNT</code>.<br/><br/>
    Signature 1: Gets the differentially-private count of rows in a query with a
    <code>DIFFERENTIAL_PRIVACY</code> clause.
    <br/>
    <br/>
    Signature 2: Gets the differentially-private count of rows with a
    non-<code>NULL</code> expression in a query with a
    <code>DIFFERENTIAL_PRIVACY</code> clause.
    <br><br>For more information, see <a href="https://github.com/google/googlesql/blob/master/docs/aggregate-dp-functions.md">Differential privacy functions</a>.

  </td>
</tr>

<tr>
  <td><a href="https://github.com/google/googlesql/blob/master/docs/aggregate_functions.md#countif"><code>COUNTIF</code></a>
</td>
  <td>
    Gets the number of <code>TRUE</code> values for an expression.
  </td>
</tr>

<tr>
  <td><a href="https://github.com/google/googlesql/blob/master/docs/statistical_aggregate_functions.md#covar_pop"><code>COVAR_POP</code></a>
</td>
  <td>
    Computes the population covariance of a set of number pairs.
    <br>For more information, see <a href="https://github.com/google/googlesql/blob/master/docs/statistical_aggregate_functions.md">Statistical aggregate functions</a>.

  </td>
</tr>

<tr>
  <td><a href="https://github.com/google/googlesql/blob/master/docs/statistical_aggregate_functions.md#covar_samp"><code>COVAR_SAMP</code></a>
</td>
  <td>
    Computes the sample covariance of a set of number pairs.
    <br>For more information, see <a href="https://github.com/google/googlesql/blob/master/docs/statistical_aggregate_functions.md">Statistical aggregate functions</a>.

  </td>
</tr>

<tr>
  <td><a href="https://github.com/google/googlesql/blob/master/docs/aggregate_functions.md#grouping"><code>GROUPING</code></a>
</td>
  <td>
    Checks if a groupable value in the <code>GROUP BY</code> clause is
    aggregated.
  </td>
</tr>

<tr>
  <td><a href="https://github.com/google/googlesql/blob/master/docs/aggregate_functions.md#logical_and"><code>LOGICAL_AND</code></a>
</td>
  <td>
    Gets the logical AND of all non-<code>NULL</code> expressions.
  </td>
</tr>

<tr>
  <td><a href="https://github.com/google/googlesql/blob/master/docs/aggregate_functions.md#logical_or"><code>LOGICAL_OR</code></a>
</td>
  <td>
    Gets the logical OR of all non-<code>NULL</code> expressions.
  </td>
</tr>

<tr>
  <td><a href="https://github.com/google/googlesql/blob/master/docs/aggregate_functions.md#max"><code>MAX</code></a>
</td>
  <td>
    Gets the maximum non-<code>NULL</code> value.
    
  </td>
</tr>

<tr>
  <td><a href="https://github.com/google/googlesql/blob/master/docs/aggregate_functions.md#min"><code>MIN</code></a>
</td>
  <td>
    Gets the minimum non-<code>NULL</code> value.
  </td>
</tr>

<tr>
  <td><a href="https://github.com/google/googlesql/blob/master/docs/aggregate-dp-functions.md#dp_percentile_cont"><code>PERCENTILE_CONT</code> (Differential Privacy)</a>
</td>
  <td>
    <code>DIFFERENTIAL_PRIVACY</code>-supported <code>PERCENTILE_CONT</code>.<br/><br/>
    Computes a differentially-private percentile across privacy unit columns
    in a query with a <code>DIFFERENTIAL_PRIVACY</code> clause.
    <br><br>For more information, see <a href="https://github.com/google/googlesql/blob/master/docs/aggregate-dp-functions.md">Differential privacy functions</a>.

  </td>
</tr>

<tr>
  <td><a href="https://github.com/google/googlesql/blob/master/docs/geography_functions.md#st_extent"><code>ST_EXTENT</code></a>
</td>
  <td>
    Gets the bounding box for a group of <code>GEOGRAPHY</code> values.
    <br>For more information, see <a href="https://github.com/google/googlesql/blob/master/docs/geography_functions.md">Geography functions</a>.

  </td>
</tr>

<tr>
  <td><a href="https://github.com/google/googlesql/blob/master/docs/geography_functions.md#st_union_agg"><code>ST_UNION_AGG</code></a>
</td>
  <td>
    Aggregates over <code>GEOGRAPHY</code> values and gets their
    point set union.
    <br>For more information, see <a href="https://github.com/google/googlesql/blob/master/docs/geography_functions.md">Geography functions</a>.

  </td>
</tr>

<tr>
  <td><a href="https://github.com/google/googlesql/blob/master/docs/statistical_aggregate_functions.md#stddev"><code>STDDEV</code></a>
</td>
  <td>
    An alias of the <code>STDDEV_SAMP</code> function.
    <br>For more information, see <a href="https://github.com/google/googlesql/blob/master/docs/statistical_aggregate_functions.md">Statistical aggregate functions</a>.

  </td>
</tr>

<tr>
  <td><a href="https://github.com/google/googlesql/blob/master/docs/statistical_aggregate_functions.md#stddev_pop"><code>STDDEV_POP</code></a>
</td>
  <td>
    Computes the population (biased) standard deviation of the values.
    <br>For more information, see <a href="https://github.com/google/googlesql/blob/master/docs/statistical_aggregate_functions.md">Statistical aggregate functions</a>.

  </td>
</tr>

<tr>
  <td><a href="https://github.com/google/googlesql/blob/master/docs/statistical_aggregate_functions.md#stddev_samp"><code>STDDEV_SAMP</code></a>
</td>
  <td>
    Computes the sample (unbiased) standard deviation of the values.
    <br>For more information, see <a href="https://github.com/google/googlesql/blob/master/docs/statistical_aggregate_functions.md">Statistical aggregate functions</a>.

  </td>
</tr>

<tr>
  <td><a href="https://github.com/google/googlesql/blob/master/docs/aggregate_functions.md#string_agg"><code>STRING_AGG</code></a>
</td>
  <td>
    Concatenates non-<code>NULL</code> <code>STRING</code> or
    <code>BYTES</code> values.
    
  </td>
</tr>

<tr>
  <td><a href="https://github.com/google/googlesql/blob/master/docs/aggregate_functions.md#sum"><code>SUM</code></a>
</td>
  <td>
    Gets the sum of non-<code>NULL</code> values.
    
  </td>
</tr>

<tr>
  <td><a href="https://github.com/google/googlesql/blob/master/docs/aggregate-dp-functions.md#dp_sum"><code>SUM</code> (Differential Privacy)</a>
</td>
  <td>
    <code>DIFFERENTIAL_PRIVACY</code>-supported <code>SUM</code>.<br/><br/>
    Gets the differentially-private sum of non-<code>NULL</code>,
    non-<code>NaN</code> values in a query with a
    <code>DIFFERENTIAL_PRIVACY</code> clause.
    <br><br>For more information, see <a href="https://github.com/google/googlesql/blob/master/docs/aggregate-dp-functions.md">Differential privacy functions</a>.

  </td>
</tr>

<tr>
  <td><a href="https://github.com/google/googlesql/blob/master/docs/statistical_aggregate_functions.md#var_pop"><code>VAR_POP</code></a>
</td>
  <td>
    Computes the population (biased) variance of the values.
    <br>For more information, see <a href="https://github.com/google/googlesql/blob/master/docs/statistical_aggregate_functions.md">Statistical aggregate functions</a>.

  </td>
</tr>

<tr>
  <td><a href="https://github.com/google/googlesql/blob/master/docs/aggregate-dp-functions.md#dp_var_pop"><code>VAR_POP</code> (Differential Privacy)</a>
</td>
  <td>
    <code>DIFFERENTIAL_PRIVACY</code>-supported <code>VAR_POP</code> (Differential Privacy).<br/><br/>
    Computes the differentially-private population (biased) variance of values
    in a query with a <code>DIFFERENTIAL_PRIVACY</code> clause.
    <br><br>For more information, see <a href="https://github.com/google/googlesql/blob/master/docs/aggregate-dp-functions.md">Differential privacy functions</a>.

  </td>
</tr>

<tr>
  <td><a href="https://github.com/google/googlesql/blob/master/docs/statistical_aggregate_functions.md#var_samp"><code>VAR_SAMP</code></a>
</td>
  <td>
    Computes the sample (unbiased) variance of the values.
    <br>For more information, see <a href="https://github.com/google/googlesql/blob/master/docs/statistical_aggregate_functions.md">Statistical aggregate functions</a>.

  </td>
</tr>

<tr>
  <td><a href="https://github.com/google/googlesql/blob/master/docs/statistical_aggregate_functions.md#variance"><code>VARIANCE</code></a>
</td>
  <td>
    An alias of <code>VAR_SAMP</code>.
    <br>For more information, see <a href="https://github.com/google/googlesql/blob/master/docs/statistical_aggregate_functions.md">Statistical aggregate functions</a>.

  </td>
</tr>

  </tbody>
</table>

## `ANY_VALUE`

```googlesql
ANY_VALUE(
  expression
  [ WHERE where_expression ]
  [ HAVING { MAX | MIN } having_expression ]
)
[ OVER over_clause ]

over_clause:
  { named_window | ( [ window_specification ] ) }

window_specification:
  [ named_window ]
  [ PARTITION BY partition_expression [, ...] ]
  [ ORDER BY expression [ { ASC | DESC }  ] [, ...] ]
  [ window_frame_clause ]

```

**Description**

Returns `expression` for some row chosen from the group. Which row is chosen is
nondeterministic, not random. Returns `NULL` when the input produces no
rows. Returns `NULL` when `expression`
or `having_expression` is
`NULL` for all rows in the group.

If `expression` contains any non-NULL values, then `ANY_VALUE` behaves as if
`IGNORE NULLS` is specified;
rows for which `expression` is `NULL` aren't considered and won't be
selected.

To learn more about the optional aggregate clauses that you can pass
into this function, see
[Aggregate function calls][aggregate-function-calls].

<!-- mdlint off(WHITESPACE_LINE_LENGTH) -->

[aggregate-function-calls]: https://github.com/google/googlesql/blob/master/docs/aggregate-function-calls.md

<!-- mdlint on -->

To learn more about the `OVER` clause and how to use it, see
[Window function calls][window-function-calls].

<!-- mdlint off(WHITESPACE_LINE_LENGTH) -->

[window-function-calls]: https://github.com/google/googlesql/blob/master/docs/window-function-calls.md

<!-- mdlint on -->

**Supported Argument Types**

Any

**Returned Data Types**

Matches the input data type.

**Examples**

```googlesql
SELECT ANY_VALUE(fruit) as any_value
FROM UNNEST(["apple", "banana", "pear"]) as fruit;

/*-----------+
 | any_value |
 +-----------+
 | apple     |
 +-----------*/
```

```googlesql
SELECT
  fruit,
  ANY_VALUE(fruit) OVER (ORDER BY LENGTH(fruit) ROWS BETWEEN 1 PRECEDING AND CURRENT ROW) AS any_value
FROM UNNEST(["apple", "banana", "pear"]) as fruit;

/*--------+-----------+
 | fruit  | any_value |
 +--------+-----------+
 | pear   | pear      |
 | apple  | pear      |
 | banana | apple     |
 +--------+-----------*/
```

```googlesql
WITH
  Store AS (
    SELECT 20 AS sold, "apples" AS fruit
    UNION ALL
    SELECT 30 AS sold, "pears" AS fruit
    UNION ALL
    SELECT 30 AS sold, "bananas" AS fruit
    UNION ALL
    SELECT 10 AS sold, "oranges" AS fruit
  )
SELECT ANY_VALUE(fruit HAVING MAX sold) AS a_highest_selling_fruit FROM Store;

/*-------------------------+
 | a_highest_selling_fruit |
 +-------------------------+
 | pears                   |
 +-------------------------*/
```

```googlesql
WITH
  Store AS (
    SELECT 20 AS sold, "apples" AS fruit
    UNION ALL
    SELECT 30 AS sold, "pears" AS fruit
    UNION ALL
    SELECT 30 AS sold, "bananas" AS fruit
    UNION ALL
    SELECT 10 AS sold, "oranges" AS fruit
  )
SELECT ANY_VALUE(fruit HAVING MIN sold) AS a_lowest_selling_fruit FROM Store;

/*-------------------------+
 | a_lowest_selling_fruit  |
 +-------------------------+
 | oranges                 |
 +-------------------------*/
```

## `ARRAY_AGG`

```googlesql
ARRAY_AGG(
  [ DISTINCT ]
  expression
  [ { IGNORE | RESPECT } NULLS ]
  [ WHERE where_expression ]
  [ HAVING { MAX | MIN } having_expression ]
  [ ORDER BY key [ { ASC | DESC } ] [, ... ] ]
  [ LIMIT n ]
)
[ OVER over_clause ]

over_clause:
  { named_window | ( [ window_specification ] ) }

window_specification:
  [ named_window ]
  [ PARTITION BY partition_expression [, ...] ]
  [ ORDER BY expression [ { ASC | DESC }  ] [, ...] ]
  [ window_frame_clause ]

```

**Description**

Returns an ARRAY of `expression` values.

To learn more about the optional aggregate clauses that you can pass
into this function, see
[Aggregate function calls][aggregate-function-calls].

<!-- mdlint off(WHITESPACE_LINE_LENGTH) -->

[aggregate-function-calls]: https://github.com/google/googlesql/blob/master/docs/aggregate-function-calls.md

<!-- mdlint on -->

To learn more about the `OVER` clause and how to use it, see
[Window function calls][window-function-calls].

<!-- mdlint off(WHITESPACE_LINE_LENGTH) -->

[window-function-calls]: https://github.com/google/googlesql/blob/master/docs/window-function-calls.md

<!-- mdlint on -->

**Supported Argument Types**

All data types except ARRAY.

**Returned Data Types**

ARRAY

If there are zero input rows, this function returns `NULL`.

**Examples**

```googlesql
SELECT ARRAY_AGG(x) AS array_agg FROM UNNEST([2, 1,-2, 3, -2, 1, 2]) AS x;

/*-------------------------+
 | array_agg               |
 +-------------------------+
 | [2, 1, -2, 3, -2, 1, 2] |
 +-------------------------*/
```

```googlesql
SELECT ARRAY_AGG(DISTINCT x) AS array_agg
FROM UNNEST([2, 1, -2, 3, -2, 1, 2]) AS x;

/*---------------+
 | array_agg     |
 +---------------+
 | [2, 1, -2, 3] |
 +---------------*/
```

```googlesql
SELECT ARRAY_AGG(x IGNORE NULLS) AS array_agg
FROM UNNEST([NULL, 1, -2, 3, -2, 1, NULL]) AS x;

/*-------------------+
 | array_agg         |
 +-------------------+
 | [1, -2, 3, -2, 1] |
 +-------------------*/
```

```googlesql
SELECT ARRAY_AGG(x ORDER BY ABS(x)) AS array_agg
FROM UNNEST([2, 1, -2, 3, -2, 1, 2]) AS x;

/*-------------------------+
 | array_agg               |
 +-------------------------+
 | [1, 1, 2, -2, -2, 2, 3] |
 +-------------------------*/
```

```googlesql
SELECT ARRAY_AGG(x LIMIT 5) AS array_agg
FROM UNNEST([2, 1, -2, 3, -2, 1, 2]) AS x;

/*-------------------+
 | array_agg         |
 +-------------------+
 | [2, 1, -2, 3, -2] |
 +-------------------*/
```

```googlesql
WITH vals AS
  (
    SELECT 1 x UNION ALL
    SELECT -2 x UNION ALL
    SELECT 3 x UNION ALL
    SELECT -2 x UNION ALL
    SELECT 1 x
  )
SELECT ARRAY_AGG(DISTINCT x ORDER BY x) as array_agg
FROM vals;

/*------------+
 | array_agg  |
 +------------+
 | [-2, 1, 3] |
 +------------*/
```

```googlesql
WITH vals AS
  (
    SELECT 1 x, 'a' y UNION ALL
    SELECT 1 x, 'b' y UNION ALL
    SELECT 2 x, 'a' y UNION ALL
    SELECT 2 x, 'c' y
  )
SELECT x, ARRAY_AGG(y) as array_agg
FROM vals
GROUP BY x;

/*---------------+
 | x | array_agg |
 +---------------+
 | 1 | [a, b]    |
 | 2 | [a, c]    |
 +---------------*/
```

```googlesql
SELECT
  x,
  ARRAY_AGG(x) OVER (ORDER BY ABS(x)) AS array_agg
FROM UNNEST([2, 1, -2, 3, -2, 1, 2]) AS x;

/*----+-------------------------+
 | x  | array_agg               |
 +----+-------------------------+
 | 1  | [1, 1]                  |
 | 1  | [1, 1]                  |
 | 2  | [1, 1, 2, -2, -2, 2]    |
 | -2 | [1, 1, 2, -2, -2, 2]    |
 | -2 | [1, 1, 2, -2, -2, 2]    |
 | 2  | [1, 1, 2, -2, -2, 2]    |
 | 3  | [1, 1, 2, -2, -2, 2, 3] |
 +----+-------------------------*/
```

## `ARRAY_CONCAT_AGG`

```googlesql
ARRAY_CONCAT_AGG(
  expression
  [ WHERE where_expression ]
  [ HAVING { MAX | MIN } having_expression ]
  [ ORDER BY key [ { ASC | DESC } ] [, ... ] ]
  [ LIMIT n ]
)
```

**Description**

Concatenates elements from `expression` of type `ARRAY`, returning a single
array as a result.

This function ignores `NULL` input arrays, but respects the `NULL` elements in
non-`NULL` input arrays. Returns `NULL` if there are zero input rows or
`expression` evaluates to `NULL` for all rows.

To learn more about the optional aggregate clauses that you can pass
into this function, see
[Aggregate function calls][aggregate-function-calls].

<!-- mdlint off(WHITESPACE_LINE_LENGTH) -->

[aggregate-function-calls]: https://github.com/google/googlesql/blob/master/docs/aggregate-function-calls.md

<!-- mdlint on -->

**Supported Argument Types**

`ARRAY`

**Returned Data Types**

`ARRAY`

**Examples**

```googlesql
SELECT ARRAY_CONCAT_AGG(x) AS array_concat_agg FROM (
  SELECT [NULL, 1, 2, 3, 4] AS x
  UNION ALL SELECT NULL
  UNION ALL SELECT [5, 6]
  UNION ALL SELECT [7, 8, 9]
);

/*-----------------------------------+
 | array_concat_agg                  |
 +-----------------------------------+
 | [NULL, 1, 2, 3, 4, 5, 6, 7, 8, 9] |
 +-----------------------------------*/
```

```googlesql
SELECT ARRAY_CONCAT_AGG(x ORDER BY ARRAY_LENGTH(x)) AS array_concat_agg FROM (
  SELECT [1, 2, 3, 4] AS x
  UNION ALL SELECT [5, 6]
  UNION ALL SELECT [7, 8, 9]
);

/*-----------------------------------+
 | array_concat_agg                  |
 +-----------------------------------+
 | [5, 6, 7, 8, 9, 1, 2, 3, 4]       |
 +-----------------------------------*/
```

```googlesql
SELECT ARRAY_CONCAT_AGG(x LIMIT 2) AS array_concat_agg FROM (
  SELECT [1, 2, 3, 4] AS x
  UNION ALL SELECT [5, 6]
  UNION ALL SELECT [7, 8, 9]
);

/*--------------------------+
 | array_concat_agg         |
 +--------------------------+
 | [1, 2, 3, 4, 5, 6]       |
 +--------------------------*/
```

```googlesql
SELECT ARRAY_CONCAT_AGG(x ORDER BY ARRAY_LENGTH(x) LIMIT 2) AS array_concat_agg FROM (
  SELECT [1, 2, 3, 4] AS x
  UNION ALL SELECT [5, 6]
  UNION ALL SELECT [7, 8, 9]
);

/*------------------+
 | array_concat_agg |
 +------------------+
 | [5, 6, 7, 8, 9]  |
 +------------------*/
```

## `AVG`

```googlesql
AVG(
  [ DISTINCT ]
  expression
  [ WHERE where_expression ]
  [ HAVING { MAX | MIN } having_expression ]
)
[ OVER over_clause ]

over_clause:
  { named_window | ( [ window_specification ] ) }

window_specification:
  [ named_window ]
  [ PARTITION BY partition_expression [, ...] ]
  [ ORDER BY expression [ { ASC | DESC }  ] [, ...] ]
  [ window_frame_clause ]

```

**Description**

Returns the average of non-`NULL` values in an aggregated group.

To learn more about the optional aggregate clauses that you can pass
into this function, see
[Aggregate function calls][aggregate-function-calls].

This function can be used with the
[`AGGREGATION_THRESHOLD` clause][agg-threshold-clause].

<!-- mdlint off(WHITESPACE_LINE_LENGTH) -->

[aggregate-function-calls]: https://github.com/google/googlesql/blob/master/docs/aggregate-function-calls.md

[agg-threshold-clause]: https://github.com/google/googlesql/blob/master/docs/query-syntax.md#agg_threshold_clause

<!-- mdlint on -->

To learn more about the `OVER` clause and how to use it, see
[Window function calls][window-function-calls].

<!-- mdlint off(WHITESPACE_LINE_LENGTH) -->

[window-function-calls]: https://github.com/google/googlesql/blob/master/docs/window-function-calls.md

<!-- mdlint on -->

`AVG` can be used with differential privacy. For more information, see
[Differentially private aggregate functions][dp-functions].

Caveats:

+ If the aggregated group is empty or the argument is `NULL` for all rows in
  the group, returns `NULL`.
+ If the argument is `NaN` for any row in the group, returns `NaN`.
+ If the argument is `[+|-]Infinity` for any row in the group, returns either
  `[+|-]Infinity` or `NaN`.
+ If there is numeric overflow, produces an error.
+ If a [floating-point type][floating-point-types] is returned, the result is
  [non-deterministic][non-deterministic], which means you might receive a
  different result each time you use this function.

[floating-point-types]: https://github.com/google/googlesql/blob/master/docs/data-types.md#floating_point_types

[non-deterministic]: https://github.com/google/googlesql/blob/master/docs/data-types.md#floating_point_semantics

**Supported Argument Types**

+ Any numeric input type
+ `INTERVAL`

**Returned Data Types**

<table>

<thead>
<tr>
<th>INPUT</th><th><code>INT32</code></th><th><code>INT64</code></th><th><code>UINT32</code></th><th><code>UINT64</code></th><th><code>NUMERIC</code></th><th><code>BIGNUMERIC</code></th><th><code>FLOAT</code></th><th><code>DOUBLE</code></th><th><code>INTERVAL</code></th>
</tr>
</thead>
<tbody>
<tr><th>OUTPUT</th><td style="vertical-align:middle"><code>DOUBLE</code></td><td style="vertical-align:middle"><code>DOUBLE</code></td><td style="vertical-align:middle"><code>DOUBLE</code></td><td style="vertical-align:middle"><code>DOUBLE</code></td><td style="vertical-align:middle"><code>NUMERIC</code></td><td style="vertical-align:middle"><code>BIGNUMERIC</code></td><td style="vertical-align:middle"><code>DOUBLE</code></td><td style="vertical-align:middle"><code>DOUBLE</code></td><td style="vertical-align:middle"><code>INTERVAL</code></td></tr>
</tbody>

</table>

**Examples**

```googlesql
SELECT AVG(x) as avg
FROM UNNEST([0, 2, 4, 4, 5]) as x;

/*-----+
 | avg |
 +-----+
 | 3   |
 +-----*/
```

```googlesql
SELECT AVG(DISTINCT x) AS avg
FROM UNNEST([0, 2, 4, 4, 5]) AS x;

/*------+
 | avg  |
 +------+
 | 2.75 |
 +------*/
```

```googlesql
SELECT
  x,
  AVG(x) OVER (ORDER BY x ROWS BETWEEN 1 PRECEDING AND CURRENT ROW) AS avg
FROM UNNEST([0, 2, NULL, 4, 4, 5]) AS x;

/*------+------+
 | x    | avg  |
 +------+------+
 | NULL | NULL |
 | 0    | 0    |
 | 2    | 1    |
 | 4    | 3    |
 | 4    | 4    |
 | 5    | 4.5  |
 +------+------*/
```

[dp-functions]: https://github.com/google/googlesql/blob/master/docs/aggregate-dp-functions.md

## `BIT_AND`

```googlesql
BIT_AND(
  [ DISTINCT ]
  expression
  [ WHERE where_expression ]
  [ HAVING { MAX | MIN } having_expression ]
)
```

**Description**

Performs a bitwise AND operation on `expression` and returns the result.

To learn more about the optional aggregate clauses that you can pass
into this function, see
[Aggregate function calls][aggregate-function-calls].

<!-- mdlint off(WHITESPACE_LINE_LENGTH) -->

[aggregate-function-calls]: https://github.com/google/googlesql/blob/master/docs/aggregate-function-calls.md

<!-- mdlint on -->

**Supported Argument Types**

+ UINT32
+ UINT64
+ INT32
+ INT64

**Returned Data Types**

INT64

**Examples**

```googlesql
SELECT BIT_AND(x) as bit_and FROM UNNEST([0xF001, 0x00A1]) as x;

/*---------+
 | bit_and |
 +---------+
 | 1       |
 +---------*/
```

## `BIT_OR`

```googlesql
BIT_OR(
  [ DISTINCT ]
  expression
  [ WHERE where_expression ]
  [ HAVING { MAX | MIN } having_expression ]
)
```

**Description**

Performs a bitwise OR operation on `expression` and returns the result.

To learn more about the optional aggregate clauses that you can pass
into this function, see
[Aggregate function calls][aggregate-function-calls].

<!-- mdlint off(WHITESPACE_LINE_LENGTH) -->

[aggregate-function-calls]: https://github.com/google/googlesql/blob/master/docs/aggregate-function-calls.md

<!-- mdlint on -->

**Supported Argument Types**

+ UINT32
+ UINT64
+ INT32
+ INT64

**Returned Data Types**

INT64

**Examples**

```googlesql
SELECT BIT_OR(x) as bit_or FROM UNNEST([0xF001, 0x00A1]) as x;

/*--------+
 | bit_or |
 +--------+
 | 61601  |
 +--------*/
```

## `BIT_XOR`

```googlesql
BIT_XOR(
  [ DISTINCT ]
  expression
  [ WHERE where_expression ]
  [ HAVING { MAX | MIN } having_expression ]
)
```

**Description**

Performs a bitwise XOR operation on `expression` and returns the result.

To learn more about the optional aggregate clauses that you can pass
into this function, see
[Aggregate function calls][aggregate-function-calls].

<!-- mdlint off(WHITESPACE_LINE_LENGTH) -->

[aggregate-function-calls]: https://github.com/google/googlesql/blob/master/docs/aggregate-function-calls.md

<!-- mdlint on -->

**Supported Argument Types**

+ UINT32
+ UINT64
+ INT32
+ INT64

**Returned Data Types**

INT64

**Examples**

```googlesql
SELECT BIT_XOR(x) AS bit_xor FROM UNNEST([5678, 1234]) AS x;

/*---------+
 | bit_xor |
 +---------+
 | 4860    |
 +---------*/
```

```googlesql
SELECT BIT_XOR(x) AS bit_xor FROM UNNEST([1234, 5678, 1234]) AS x;

/*---------+
 | bit_xor |
 +---------+
 | 5678    |
 +---------*/
```

```googlesql
SELECT BIT_XOR(DISTINCT x) AS bit_xor FROM UNNEST([1234, 5678, 1234]) AS x;

/*---------+
 | bit_xor |
 +---------+
 | 4860    |
 +---------*/
```

## `COUNT`

```googlesql
COUNT(*)
[ OVER over_clause ]
```

```googlesql
COUNT(
  [ DISTINCT ]
  expression
  [ WHERE where_expression ]
  [ HAVING { MAX | MIN } having_expression ]
)
[ OVER over_clause ]

over_clause:
  { named_window | ( [ window_specification ] ) }

window_specification:
  [ named_window ]
  [ PARTITION BY partition_expression [, ...] ]
  [ ORDER BY expression [ { ASC | DESC }  ] [, ...] ]
  [ window_frame_clause ]

```

**Description**

Gets the number of rows in the input or the number of rows with an
expression evaluated to any value other than `NULL`.

Note: If you're querying a large dataset, you can compute results faster and
save resources by using [HLL++ functions][hll-functions] for approximate
distinct counts. For more information, see
[Sketches][sketches].

**Definitions**

+ `*`: Use this value to get the number of all rows in the input.
+ `expression`: A value of any data type that represents the expression to
  evaluate. If `DISTINCT` is present,
  `expression` can only be a data type that is
  [groupable][groupable-data-types].
+   `DISTINCT`: To learn more, see
    [Aggregate function calls][aggregate-function-calls].
+   `WHERE`: To learn more, see
    [Aggregate function calls][aggregate-function-calls].
+   `HAVING { MAX | MIN }`: To learn more, see
    [Aggregate function calls][aggregate-function-calls].
+   `OVER`: To learn more, see
    [Aggregate function calls][aggregate-function-calls].
+   `over_clause`: To learn more, see
    [Aggregate function calls][aggregate-function-calls].
+   `window_specification`: To learn more, see
    [Window function calls][window-function-calls].

<!-- mdlint off(WHITESPACE_LINE_LENGTH) -->

[aggregate-function-calls]: https://github.com/google/googlesql/blob/master/docs/aggregate-function-calls.md

[agg-threshold-clause]: https://github.com/google/googlesql/blob/master/docs/query-syntax.md#agg_threshold_clause

[window-function-calls]: https://github.com/google/googlesql/blob/master/docs/window-function-calls.md

<!-- mdlint on -->

<!-- mdlint off(WHITESPACE_LINE_LENGTH) -->

[window-function-calls]: https://github.com/google/googlesql/blob/master/docs/window-function-calls.md

<!-- mdlint on -->

**Details**

To count the number of distinct values of an expression for which a
certain condition is satisfied, you can use the following recipe:

```googlesql
COUNT(DISTINCT IF(condition, expression, NULL))
```

`IF` returns the value of `expression` if `condition` is `TRUE`, or
`NULL` otherwise. The surrounding `COUNT(DISTINCT ...)` ignores the `NULL`
values, so it counts only the distinct values of `expression` for which
`condition` is `TRUE`.

To count the number of non-distinct values of an expression for which a
certain condition is satisfied, consider using the
[`COUNTIF`][countif] function.

This function with `DISTINCT` supports specifying [collation][collation].

[collation]: https://github.com/google/googlesql/blob/master/docs/collation-concepts.md

`COUNT` can be used with differential privacy. For more information, see
[Differentially private aggregate functions][dp-functions].

**Return type**

`INT64`

**Examples**

You can use the `COUNT` function to return the number of rows in a table or the
number of distinct values of an expression. For example:

```googlesql
SELECT
  COUNT(*) AS count_star,
  COUNT(DISTINCT x) AS count_dist_x
FROM UNNEST([1, 4, 4, 5]) AS x;

/*------------+--------------+
 | count_star | count_dist_x |
 +------------+--------------+
 | 4          | 3            |
 +------------+--------------*/
```

```googlesql
SELECT
  x,
  COUNT(*) OVER (PARTITION BY MOD(x, 3)) AS count_star,
  COUNT(DISTINCT x) OVER (PARTITION BY MOD(x, 3)) AS count_dist_x
FROM UNNEST([1, 4, 4, 5]) AS x;

/*------+------------+--------------+
 | x    | count_star | count_dist_x |
 +------+------------+--------------+
 | 1    | 3          | 2            |
 | 4    | 3          | 2            |
 | 4    | 3          | 2            |
 | 5    | 1          | 1            |
 +------+------------+--------------*/
```

```googlesql
SELECT
  x,
  COUNT(*) OVER (PARTITION BY MOD(x, 3)) AS count_star,
  COUNT(x) OVER (PARTITION BY MOD(x, 3)) AS count_x
FROM UNNEST([1, 4, NULL, 4, 5]) AS x;

/*------+------------+---------+
 | x    | count_star | count_x |
 +------+------------+---------+
 | NULL | 1          | 0       |
 | 1    | 3          | 3       |
 | 4    | 3          | 3       |
 | 4    | 3          | 3       |
 | 5    | 1          | 1       |
 +------+------------+---------*/
```

The following query counts the number of distinct positive values of `x`:

```googlesql
SELECT COUNT(DISTINCT IF(x > 0, x, NULL)) AS distinct_positive
FROM UNNEST([1, -2, 4, 1, -5, 4, 1, 3, -6, 1]) AS x;

/*-------------------+
 | distinct_positive |
 +-------------------+
 | 3                 |
 +-------------------*/
```

The following query counts the number of distinct dates on which a certain kind
of event occurred:

```googlesql
WITH Events AS (
  SELECT DATE '2021-01-01' AS event_date, 'SUCCESS' AS event_type
  UNION ALL
  SELECT DATE '2021-01-02' AS event_date, 'SUCCESS' AS event_type
  UNION ALL
  SELECT DATE '2021-01-02' AS event_date, 'FAILURE' AS event_type
  UNION ALL
  SELECT DATE '2021-01-03' AS event_date, 'SUCCESS' AS event_type
  UNION ALL
  SELECT DATE '2021-01-04' AS event_date, 'FAILURE' AS event_type
  UNION ALL
  SELECT DATE '2021-01-04' AS event_date, 'FAILURE' AS event_type
)
SELECT
  COUNT(DISTINCT IF(event_type = 'FAILURE', event_date, NULL))
    AS distinct_dates_with_failures
FROM Events;

/*------------------------------+
 | distinct_dates_with_failures |
 +------------------------------+
 | 2                            |
 +------------------------------*/
```

The following query counts the number of distinct `id`s that exist in both
the `customers` and `vendor` tables:

```googlesql
WITH
  customers AS (
    SELECT 1934 AS id, 'a' AS team UNION ALL
    SELECT 2991, 'b' UNION ALL
    SELECT 3988, 'c'),
  vendors AS (
    SELECT 1934 AS id, 'd' AS team UNION ALL
    SELECT 2991, 'e' UNION ALL
    SELECT 4366, 'f')
SELECT
  COUNT(DISTINCT IF(id IN (SELECT id FROM customers), id, NULL)) AS result
FROM vendors;

/*--------+
 | result |
 +--------+
 | 2      |
 +--------*/
```

[sketches]: https://github.com/google/googlesql/blob/master/docs/sketches.md

[hll-functions]: https://github.com/google/googlesql/blob/master/docs/hll_functions.md

[countif]: https://github.com/google/googlesql/blob/master/docs/aggregate_functions.md#countif

[groupable-data-types]: https://github.com/google/googlesql/blob/master/docs/data-types.md#groupable_data_types

[dp-functions]: https://github.com/google/googlesql/blob/master/docs/aggregate-dp-functions.md

## `COUNTIF`

```googlesql
COUNTIF(
  [ DISTINCT ]
  expression
  [ WHERE where_expression ]
  [ HAVING { MAX | MIN } having_expression ]
)
[ OVER over_clause ]

over_clause:
  { named_window | ( [ window_specification ] ) }

window_specification:
  [ named_window ]
  [ PARTITION BY partition_expression [, ...] ]
  [ ORDER BY expression [ { ASC | DESC }  ] [, ...] ]
  [ window_frame_clause ]

```

**Description**

Gets the number of `TRUE` values for an expression.

**Definitions**

+ `expression`: A `BOOL` value that represents the expression to evaluate.
+   `DISTINCT`: To learn more, see
    [Aggregate function calls][aggregate-function-calls].
+   `WHERE`: To learn more, see
    [Aggregate function calls][aggregate-function-calls].
+   `HAVING { MAX | MIN }`: To learn more, see
    [Aggregate function calls][aggregate-function-calls].
+   `OVER`: To learn more, see
    [Aggregate function calls][aggregate-function-calls].
+   `over_clause`: To learn more, see
    [Aggregate function calls][aggregate-function-calls].
+   `window_specification`: To learn more, see
    [Window function calls][window-function-calls].

<!-- mdlint off(WHITESPACE_LINE_LENGTH) -->

[aggregate-function-calls]: https://github.com/google/googlesql/blob/master/docs/aggregate-function-calls.md

[agg-threshold-clause]: https://github.com/google/googlesql/blob/master/docs/query-syntax.md#agg_threshold_clause

[window-function-calls]: https://github.com/google/googlesql/blob/master/docs/window-function-calls.md

<!-- mdlint on -->

<!-- mdlint off(WHITESPACE_LINE_LENGTH) -->

[window-function-calls]: https://github.com/google/googlesql/blob/master/docs/window-function-calls.md

<!-- mdlint on -->

**Details**

The function signature `COUNTIF(DISTINCT ...)` is generally not useful. If you
would like to use `DISTINCT`, use `COUNT` with `DISTINCT IF`. For more
information, see the [`COUNT`][count] function.

**Return type**

`INT64`

**Examples**

```googlesql
SELECT COUNTIF(x<0) AS num_negative, COUNTIF(x>0) AS num_positive
FROM UNNEST([5, -2, 3, 6, -10, -7, 4, 0]) AS x;

/*--------------+--------------+
 | num_negative | num_positive |
 +--------------+--------------+
 | 3            | 4            |
 +--------------+--------------*/
```

```googlesql
SELECT
  x,
  COUNTIF(x<0) OVER (ORDER BY ABS(x) ROWS BETWEEN 1 PRECEDING AND 1 FOLLOWING) AS num_negative
FROM UNNEST([5, -2, 3, 6, -10, NULL, -7, 4, 0]) AS x;

/*------+--------------+
 | x    | num_negative |
 +------+--------------+
 | NULL | 0            |
 | 0    | 1            |
 | -2   | 1            |
 | 3    | 1            |
 | 4    | 0            |
 | 5    | 0            |
 | 6    | 1            |
 | -7   | 2            |
 | -10  | 2            |
 +------+--------------*/
```

[count]: https://github.com/google/googlesql/blob/master/docs/aggregate_functions.md#count

## `GROUPING`

```googlesql
GROUPING(groupable_value)
```

**Description**

If a groupable item in the [`GROUP BY` clause][group-by-clause] is aggregated
(and thus not grouped), this function returns `1`. Otherwise,
this function returns `0`.

Definitions:

+ `groupable_value`: An expression that represents a value that can be grouped
  in the `GROUP BY` clause.

Details:

The `GROUPING` function is helpful if you need to determine which rows are
produced by which grouping sets. A grouping set is a group of columns by which
rows can be grouped together. So, if you need to filter rows by
a few specific grouping sets, you can use the `GROUPING` function to identify
which grouping sets grouped which rows by creating a matrix of the results.

In addition, you can use the `GROUPING` function to determine the type of
`NULL` produced by the `GROUP BY` clause. In some cases, the `GROUP BY` clause
produces a `NULL` placeholder. This placeholder represents all groupable items
that are aggregated (not grouped) in the current grouping set. This is different
from a standard `NULL`, which can also be produced by a query.

For more information, see the following examples.

**Returned Data Type**

`INT64`

**Examples**

In the following example, it's difficult to determine which rows are grouped by
the grouping value `product_type` or `product_name`. The `GROUPING` function
makes this easier to determine.

Pay close attention to what's in the `product_type_agg` and
`product_name_agg` column matrix. This determines how the rows are grouped.

`product_type_agg` | `product_name_agg` | Notes
------------------ | -------------------| ------
1                  | 0                  | Rows are grouped by `product_name`.
0                  | 1                  | Rows are grouped by `product_type`.
0                  | 0                  | Rows are grouped by `product_type` and `product_name`.
1                  | 1                  | Grand total row.

```googlesql
WITH
  Products AS (
    SELECT 'shirt' AS product_type, 't-shirt' AS product_name, 3 AS product_count UNION ALL
    SELECT 'shirt', 't-shirt', 8 UNION ALL
    SELECT 'shirt', 'polo', 25 UNION ALL
    SELECT 'pants', 'jeans', 6
  )
SELECT
  product_type,
  product_name,
  SUM(product_count) AS product_sum,
  GROUPING(product_type) AS product_type_agg,
  GROUPING(product_name) AS product_name_agg,
FROM Products
GROUP BY GROUPING SETS(product_type, product_name, ())
ORDER BY product_name;

/*--------------+--------------+-------------+------------------+------------------+
 | product_type | product_name | product_sum | product_type_agg | product_name_agg |
 +--------------+--------------+-------------+------------------+------------------+
 | NULL         | NULL         | 42          | 1                | 1                |
 | shirt        | NULL         | 36          | 0                | 1                |
 | pants        | NULL         | 6           | 0                | 1                |
 | NULL         | jeans        | 6           | 1                | 0                |
 | NULL         | polo         | 25          | 1                | 0                |
 | NULL         | t-shirt      | 11          | 1                | 0                |
 +--------------+--------------+-------------+------------------+------------------*/
```

In the following example, it's difficult to determine
if `NULL` represents a `NULL` placeholder or a standard `NULL` value in the
`product_type` column. The `GROUPING` function makes it easier to
determine what type of `NULL` is being produced. If
`product_type_is_aggregated` is `1`, the `NULL` value for
the `product_type` column is a `NULL` placeholder.

```googlesql
WITH
  Products AS (
    SELECT 'shirt' AS product_type, 't-shirt' AS product_name, 3 AS product_count UNION ALL
    SELECT 'shirt', 't-shirt', 8 UNION ALL
    SELECT NULL, 'polo', 25 UNION ALL
    SELECT 'pants', 'jeans', 6
  )
SELECT
  product_type,
  product_name,
  SUM(product_count) AS product_sum,
  GROUPING(product_type) AS product_type_is_aggregated
FROM Products
GROUP BY GROUPING SETS(product_type, product_name)
ORDER BY product_name;

/*--------------+--------------+-------------+----------------------------+
 | product_type | product_name | product_sum | product_type_is_aggregated |
 +--------------+--------------+-------------+----------------------------+
 | shirt        | NULL         | 11          | 0                          |
 | NULL         | NULL         | 25          | 0                          |
 | pants        | NULL         | 6           | 0                          |
 | NULL         | jeans        | 6           | 1                          |
 | NULL         | polo         | 25          | 1                          |
 | NULL         | t-shirt      | 11          | 1                          |
 +--------------+--------------+-------------+----------------------------*/
```

[group-by-clause]: https://github.com/google/googlesql/blob/master/docs/query-syntax.md#group_by_clause

## `LOGICAL_AND`

```googlesql
LOGICAL_AND(
  expression
  [ WHERE where_expression ]
  [ HAVING { MAX | MIN } having_expression ]
)
[ OVER over_clause ]

over_clause:
  { named_window | ( [ window_specification ] ) }

window_specification:
  [ named_window ]
  [ PARTITION BY partition_expression [, ...] ]
  [ ORDER BY expression [ { ASC | DESC }  ] [, ...] ]
  [ window_frame_clause ]

```

**Description**

Returns the logical AND of all non-`NULL` expressions. Returns `NULL` if there
are zero input rows or `expression` evaluates to `NULL` for all rows.

To learn more about the optional aggregate clauses that you can pass
into this function, see
[Aggregate function calls][aggregate-function-calls].

This function can be used with the
[`AGGREGATION_THRESHOLD` clause][agg-threshold-clause].

<!-- mdlint off(WHITESPACE_LINE_LENGTH) -->

[aggregate-function-calls]: https://github.com/google/googlesql/blob/master/docs/aggregate-function-calls.md

[agg-threshold-clause]: https://github.com/google/googlesql/blob/master/docs/query-syntax.md#agg_threshold_clause

<!-- mdlint on -->

To learn more about the `OVER` clause and how to use it, see
[Window function calls][window-function-calls].

<!-- mdlint off(WHITESPACE_LINE_LENGTH) -->

[window-function-calls]: https://github.com/google/googlesql/blob/master/docs/window-function-calls.md

<!-- mdlint on -->

**Supported Argument Types**

`BOOL`

**Return Data Types**

`BOOL`

**Examples**

`LOGICAL_AND` returns `FALSE` because not all of the values in the array are
less than 3.

```googlesql
SELECT LOGICAL_AND(x < 3) AS logical_and FROM UNNEST([1, 2, 4]) AS x;

/*-------------+
 | logical_and |
 +-------------+
 | FALSE       |
 +-------------*/
```

## `LOGICAL_OR`

```googlesql
LOGICAL_OR(
  expression
  [ WHERE where_expression ]
  [ HAVING { MAX | MIN } having_expression ]
)
[ OVER over_clause ]

over_clause:
  { named_window | ( [ window_specification ] ) }

window_specification:
  [ named_window ]
  [ PARTITION BY partition_expression [, ...] ]
  [ ORDER BY expression [ { ASC | DESC }  ] [, ...] ]
  [ window_frame_clause ]

```

**Description**

Returns the logical OR of all non-`NULL` expressions. Returns `NULL` if there
are zero input rows or `expression` evaluates to `NULL` for all rows.

To learn more about the optional aggregate clauses that you can pass
into this function, see
[Aggregate function calls][aggregate-function-calls].

This function can be used with the
[`AGGREGATION_THRESHOLD` clause][agg-threshold-clause].

<!-- mdlint off(WHITESPACE_LINE_LENGTH) -->

[aggregate-function-calls]: https://github.com/google/googlesql/blob/master/docs/aggregate-function-calls.md

[agg-threshold-clause]: https://github.com/google/googlesql/blob/master/docs/query-syntax.md#agg_threshold_clause

<!-- mdlint on -->

To learn more about the `OVER` clause and how to use it, see
[Window function calls][window-function-calls].

<!-- mdlint off(WHITESPACE_LINE_LENGTH) -->

[window-function-calls]: https://github.com/google/googlesql/blob/master/docs/window-function-calls.md

<!-- mdlint on -->

**Supported Argument Types**

`BOOL`

**Return Data Types**

`BOOL`

**Examples**

`LOGICAL_OR` returns `TRUE` because at least one of the values in the array is
less than 3.

```googlesql
SELECT LOGICAL_OR(x < 3) AS logical_or FROM UNNEST([1, 2, 4]) AS x;

/*------------+
 | logical_or |
 +------------+
 | TRUE       |
 +------------*/
```

## `MAX`

```googlesql
MAX(
  expression
  [ WHERE where_expression ]
  [ HAVING { MAX | MIN } having_expression ]
)
[ OVER over_clause ]

over_clause:
  { named_window | ( [ window_specification ] ) }

window_specification:
  [ named_window ]
  [ PARTITION BY partition_expression [, ...] ]
  [ ORDER BY expression [ { ASC | DESC }  ] [, ...] ]
  [ window_frame_clause ]

```

**Description**

Returns the maximum non-`NULL` value in an aggregated group.

Caveats:

+ If the aggregated group is empty or the argument is `NULL` for all rows in
  the group, returns `NULL`.
+ If the argument is `NaN` for any row in the group, returns `NaN`.

To learn more about the optional aggregate clauses that you can pass
into this function, see
[Aggregate function calls][aggregate-function-calls].

<!-- mdlint off(WHITESPACE_LINE_LENGTH) -->

[aggregate-function-calls]: https://github.com/google/googlesql/blob/master/docs/aggregate-function-calls.md

<!-- mdlint on -->

To learn more about the `OVER` clause and how to use it, see
[Window function calls][window-function-calls].

<!-- mdlint off(WHITESPACE_LINE_LENGTH) -->

[window-function-calls]: https://github.com/google/googlesql/blob/master/docs/window-function-calls.md

<!-- mdlint on -->

This function supports specifying [collation][collation].

[collation]: https://github.com/google/googlesql/blob/master/docs/collation-concepts.md

**Supported Argument Types**

Any [orderable data type][agg-data-type-properties] except for `ARRAY`.

**Return Data Types**

The data type of the input values.

**Examples**

```googlesql
SELECT MAX(x) AS max
FROM UNNEST([8, 37, 55, 4]) AS x;

/*-----+
 | max |
 +-----+
 | 55  |
 +-----*/
```

```googlesql
SELECT x, MAX(x) OVER (PARTITION BY MOD(x, 2)) AS max
FROM UNNEST([8, NULL, 37, 55, NULL, 4]) AS x;

/*------+------+
 | x    | max  |
 +------+------+
 | NULL | NULL |
 | NULL | NULL |
 | 8    | 8    |
 | 4    | 8    |
 | 37   | 55   |
 | 55   | 55   |
 +------+------*/
```

[agg-data-type-properties]: https://github.com/google/googlesql/blob/master/docs/data-types.md#data_type_properties

## `MIN`

```googlesql
MIN(
  expression
  [ WHERE where_expression ]
  [ HAVING { MAX | MIN } having_expression ]
)
[ OVER over_clause ]

over_clause:
  { named_window | ( [ window_specification ] ) }

window_specification:
  [ named_window ]
  [ PARTITION BY partition_expression [, ...] ]
  [ ORDER BY expression [ { ASC | DESC }  ] [, ...] ]
  [ window_frame_clause ]

```

**Description**

Returns the minimum non-`NULL` value in an aggregated group.

Caveats:

+ If the aggregated group is empty or the argument is `NULL` for all rows in
  the group, returns `NULL`.
+ If the argument is `NaN` for any row in the group, returns `NaN`.

To learn more about the optional aggregate clauses that you can pass
into this function, see
[Aggregate function calls][aggregate-function-calls].

<!-- mdlint off(WHITESPACE_LINE_LENGTH) -->

[aggregate-function-calls]: https://github.com/google/googlesql/blob/master/docs/aggregate-function-calls.md

<!-- mdlint on -->

To learn more about the `OVER` clause and how to use it, see
[Window function calls][window-function-calls].

<!-- mdlint off(WHITESPACE_LINE_LENGTH) -->

[window-function-calls]: https://github.com/google/googlesql/blob/master/docs/window-function-calls.md

<!-- mdlint on -->

This function supports specifying [collation][collation].

[collation]: https://github.com/google/googlesql/blob/master/docs/collation-concepts.md

**Supported Argument Types**

Any [orderable data type][agg-data-type-properties] except for `ARRAY`.

**Return Data Types**

The data type of the input values.

**Examples**

```googlesql
SELECT MIN(x) AS min
FROM UNNEST([8, 37, 4, 55]) AS x;

/*-----+
 | min |
 +-----+
 | 4   |
 +-----*/
```

```googlesql
SELECT x, MIN(x) OVER (PARTITION BY MOD(x, 2)) AS min
FROM UNNEST([8, NULL, 37, 4, NULL, 55]) AS x;

/*------+------+
 | x    | min  |
 +------+------+
 | NULL | NULL |
 | NULL | NULL |
 | 8    | 4    |
 | 4    | 4    |
 | 37   | 37   |
 | 55   | 37   |
 +------+------*/
```

[agg-data-type-properties]: https://github.com/google/googlesql/blob/master/docs/data-types.md#data_type_properties

## `STRING_AGG`

```googlesql
STRING_AGG(
  [ DISTINCT ]
  expression [, delimiter]
  [ WHERE where_expression ]
  [ HAVING { MAX | MIN } having_expression ]
  [ ORDER BY key [ { ASC | DESC } ] [, ... ] ]
  [ LIMIT n ]
)
[ OVER over_clause ]

over_clause:
  { named_window | ( [ window_specification ] ) }

window_specification:
  [ named_window ]
  [ PARTITION BY partition_expression [, ...] ]
  [ ORDER BY expression [ { ASC | DESC }  ] [, ...] ]
  [ window_frame_clause ]

```

**Description**

Returns a value (either `STRING` or `BYTES`) obtained by concatenating
non-`NULL` values. Returns `NULL` if there are zero input rows or `expression`
evaluates to `NULL` for all rows.

If a `delimiter` is specified, concatenated values are separated by that
delimiter; otherwise, a comma is used as a delimiter.

To learn more about the optional aggregate clauses that you can pass
into this function, see
[Aggregate function calls][aggregate-function-calls].

<!-- mdlint off(WHITESPACE_LINE_LENGTH) -->

[aggregate-function-calls]: https://github.com/google/googlesql/blob/master/docs/aggregate-function-calls.md

<!-- mdlint on -->

To learn more about the `OVER` clause and how to use it, see
[Window function calls][window-function-calls].

<!-- mdlint off(WHITESPACE_LINE_LENGTH) -->

[window-function-calls]: https://github.com/google/googlesql/blob/master/docs/window-function-calls.md

<!-- mdlint on -->

**Supported Argument Types**

Either `STRING` or `BYTES`.

**Return Data Types**

Either `STRING` or `BYTES`.

**Examples**

```googlesql
SELECT STRING_AGG(fruit) AS string_agg
FROM UNNEST(["apple", NULL, "pear", "banana", "pear"]) AS fruit;

/*------------------------+
 | string_agg             |
 +------------------------+
 | apple,pear,banana,pear |
 +------------------------*/
```

```googlesql
SELECT STRING_AGG(fruit, " & ") AS string_agg
FROM UNNEST(["apple", "pear", "banana", "pear"]) AS fruit;

/*------------------------------+
 | string_agg                   |
 +------------------------------+
 | apple & pear & banana & pear |
 +------------------------------*/
```

```googlesql
SELECT STRING_AGG(DISTINCT fruit, " & ") AS string_agg
FROM UNNEST(["apple", "pear", "banana", "pear"]) AS fruit;

/*-----------------------+
 | string_agg            |
 +-----------------------+
 | apple & pear & banana |
 +-----------------------*/
```

```googlesql
SELECT STRING_AGG(fruit, " & " ORDER BY LENGTH(fruit)) AS string_agg
FROM UNNEST(["apple", "pear", "banana", "pear"]) AS fruit;

/*------------------------------+
 | string_agg                   |
 +------------------------------+
 | pear & pear & apple & banana |
 +------------------------------*/
```

```googlesql
SELECT STRING_AGG(fruit, " & " LIMIT 2) AS string_agg
FROM UNNEST(["apple", "pear", "banana", "pear"]) AS fruit;

/*--------------+
 | string_agg   |
 +--------------+
 | apple & pear |
 +--------------*/
```

```googlesql
SELECT STRING_AGG(DISTINCT fruit, " & " ORDER BY fruit DESC LIMIT 2) AS string_agg
FROM UNNEST(["apple", "pear", "banana", "pear"]) AS fruit;

/*---------------+
 | string_agg    |
 +---------------+
 | pear & banana |
 +---------------*/
```

```googlesql
SELECT
  fruit,
  STRING_AGG(fruit, " & ") OVER (ORDER BY LENGTH(fruit)) AS string_agg
FROM UNNEST(["apple", NULL, "pear", "banana", "pear"]) AS fruit;

/*--------+------------------------------+
 | fruit  | string_agg                   |
 +--------+------------------------------+
 | NULL   | NULL                         |
 | pear   | pear & pear                  |
 | pear   | pear & pear                  |
 | apple  | pear & pear & apple          |
 | banana | pear & pear & apple & banana |
 +--------+------------------------------*/
```

## `SUM`

```googlesql
SUM(
  [ DISTINCT ]
  expression
  [ WHERE where_expression ]
  [ HAVING { MAX | MIN } having_expression ]
)
[ OVER over_clause ]

over_clause:
  { named_window | ( [ window_specification ] ) }

window_specification:
  [ named_window ]
  [ PARTITION BY partition_expression [, ...] ]
  [ ORDER BY expression [ { ASC | DESC }  ] [, ...] ]
  [ window_frame_clause ]

```

**Description**

Returns the sum of non-`NULL` values in an aggregated group.

To learn more about the optional aggregate clauses that you can pass
into this function, see
[Aggregate function calls][aggregate-function-calls].

This function can be used with the
[`AGGREGATION_THRESHOLD` clause][agg-threshold-clause].

<!-- mdlint off(WHITESPACE_LINE_LENGTH) -->

[aggregate-function-calls]: https://github.com/google/googlesql/blob/master/docs/aggregate-function-calls.md

[agg-threshold-clause]: https://github.com/google/googlesql/blob/master/docs/query-syntax.md#agg_threshold_clause

<!-- mdlint on -->

To learn more about the `OVER` clause and how to use it, see
[Window function calls][window-function-calls].

<!-- mdlint off(WHITESPACE_LINE_LENGTH) -->

[window-function-calls]: https://github.com/google/googlesql/blob/master/docs/window-function-calls.md

<!-- mdlint on -->

`SUM` can be used with differential privacy. For more information, see
[Differentially private aggregate functions][dp-functions].

Caveats:

+ If the aggregated group is empty or the argument is `NULL` for all rows in
  the group, returns `NULL`.
+ If the argument is `NaN` for any row in the group, returns `NaN`.
+ If the argument is `[+|-]Infinity` for any row in the group, returns either
  `[+|-]Infinity` or `NaN`.
+ If there is numeric overflow, produces an error.
+ If a [floating-point type][floating-point-types] is returned, the result is
  [non-deterministic][non-deterministic], which means you might receive a
  different result each time you use this function.

[floating-point-types]: https://github.com/google/googlesql/blob/master/docs/data-types.md#floating_point_types

[non-deterministic]: https://github.com/google/googlesql/blob/master/docs/data-types.md#floating_point_semantics

**Supported Argument Types**

+ Any supported numeric data type
+ `INTERVAL`

**Return Data Types**

<table>

<thead>
<tr>
<th>INPUT</th><th><code>INT32</code></th><th><code>INT64</code></th><th><code>UINT32</code></th><th><code>UINT64</code></th><th><code>NUMERIC</code></th><th><code>BIGNUMERIC</code></th><th><code>FLOAT</code></th><th><code>DOUBLE</code></th><th><code>INTERVAL</code></th>
</tr>
</thead>
<tbody>
<tr><th>OUTPUT</th><td style="vertical-align:middle"><code>INT64</code></td><td style="vertical-align:middle"><code>INT64</code></td><td style="vertical-align:middle"><code>UINT64</code></td><td style="vertical-align:middle"><code>UINT64</code></td><td style="vertical-align:middle"><code>NUMERIC</code></td><td style="vertical-align:middle"><code>BIGNUMERIC</code></td><td style="vertical-align:middle"><code>DOUBLE</code></td><td style="vertical-align:middle"><code>DOUBLE</code></td><td style="vertical-align:middle"><code>INTERVAL</code></td></tr>
</tbody>

</table>

**Examples**

```googlesql
SELECT SUM(x) AS sum
FROM UNNEST([1, 2, 3, 4, 5, 4, 3, 2, 1]) AS x;

/*-----+
 | sum |
 +-----+
 | 25  |
 +-----*/
```

```googlesql
SELECT SUM(DISTINCT x) AS sum
FROM UNNEST([1, 2, 3, 4, 5, 4, 3, 2, 1]) AS x;

/*-----+
 | sum |
 +-----+
 | 15  |
 +-----*/
```

```googlesql
SELECT
  x,
  SUM(x) OVER (PARTITION BY MOD(x, 3)) AS sum
FROM UNNEST([1, 2, 3, 4, 5, 4, 3, 2, 1]) AS x;

/*---+-----+
 | x | sum |
 +---+-----+
 | 3 | 6   |
 | 3 | 6   |
 | 1 | 10  |
 | 4 | 10  |
 | 4 | 10  |
 | 1 | 10  |
 | 2 | 9   |
 | 5 | 9   |
 | 2 | 9   |
 +---+-----*/
```

```googlesql
SELECT
  x,
  SUM(DISTINCT x) OVER (PARTITION BY MOD(x, 3)) AS sum
FROM UNNEST([1, 2, 3, 4, 5, 4, 3, 2, 1]) AS x;

/*---+-----+
 | x | sum |
 +---+-----+
 | 3 | 3   |
 | 3 | 3   |
 | 1 | 5   |
 | 4 | 5   |
 | 4 | 5   |
 | 1 | 5   |
 | 2 | 7   |
 | 5 | 7   |
 | 2 | 7   |
 +---+-----*/
```

```googlesql
SELECT SUM(x) AS sum
FROM UNNEST([]) AS x;

/*------+
 | sum  |
 +------+
 | NULL |
 +------*/
```

[dp-functions]: https://github.com/google/googlesql/blob/master/docs/aggregate-dp-functions.md

[agg-function-calls]: https://github.com/google/googlesql/blob/master/docs/aggregate-function-calls.md

