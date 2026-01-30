

<!-- mdlint off(WHITESPACE_LINE_LENGTH) -->

# Approximate aggregate functions

GoogleSQL supports approximate aggregate functions.
To learn about the syntax for aggregate function calls, see
[Aggregate function calls][agg-function-calls].

Approximate aggregate functions are scalable in terms of memory usage and time,
but produce approximate results instead of exact results. These functions
typically require less memory than [exact aggregation functions][aggregate-functions-reference]
like `COUNT(DISTINCT ...)`, but also introduce statistical uncertainty.
This makes approximate aggregation appropriate for large data streams for
which linear memory usage is impractical, as well as for data that is
already approximate.

The approximate aggregate functions in this section work directly on the
input data, rather than an intermediate estimation of the data. These functions
_don't allow_ users to specify the precision for the estimation with
sketches. If you would like to specify precision with sketches, see:

+  [HyperLogLog++ functions][hll-functions] to estimate cardinality.

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
  <td><a href="https://github.com/google/googlesql/blob/master/docs/approximate_aggregate_functions.md#approx_count_distinct"><code>APPROX_COUNT_DISTINCT</code></a>
</td>
  <td>
    Gets the approximate result for <code>COUNT(DISTINCT expression)</code>.
    
  </td>
</tr>

<tr>
  <td><a href="https://github.com/google/googlesql/blob/master/docs/approximate_aggregate_functions.md#approx_quantiles"><code>APPROX_QUANTILES</code></a>
</td>
  <td>
    Gets the approximate quantile boundaries.
    
  </td>
</tr>

<tr>
  <td><a href="https://github.com/google/googlesql/blob/master/docs/approximate_aggregate_functions.md#approx_top_count"><code>APPROX_TOP_COUNT</code></a>
</td>
  <td>
    Gets the approximate top elements and their approximate count.
    
  </td>
</tr>

<tr>
  <td><a href="https://github.com/google/googlesql/blob/master/docs/approximate_aggregate_functions.md#approx_top_sum"><code>APPROX_TOP_SUM</code></a>
</td>
  <td>
    Gets the approximate top elements and sum, based on the approximate sum
    of an assigned weight.
    
  </td>
</tr>

  </tbody>
</table>

## `APPROX_COUNT_DISTINCT`

```googlesql
APPROX_COUNT_DISTINCT(
  expression
  [ WHERE where_expression ]
)
```

**Description**

Returns the approximate result for `COUNT(DISTINCT expression)`. The value
returned is a statistical estimate, not necessarily the actual value.

This function is less accurate than `COUNT(DISTINCT expression)`, but performs
better on huge input.

**Supported Argument Types**

Any data type **except**:

+ `ARRAY`
+ `STRUCT`
+ `PROTO`

**Returned Data Types**

`INT64`

**Examples**

```googlesql
SELECT APPROX_COUNT_DISTINCT(x) as approx_distinct
FROM UNNEST([0, 1, 1, 2, 3, 5]) as x;

/*-----------------+
 | approx_distinct |
 +-----------------+
 | 5               |
 +-----------------*/
```

## `APPROX_QUANTILES`

```googlesql
APPROX_QUANTILES(
  [ DISTINCT ]
  expression, number
  [ { IGNORE | RESPECT } NULLS ]
  [ WHERE where_expression ]
  [ HAVING { MAX | MIN } having_expression ]
)
```

**Description**

Returns the approximate boundaries for a group of `expression` values, where
`number` represents the number of quantiles to create. This function returns an
array of `number` + 1 elements, sorted in ascending order, where the
first element is the approximate minimum and the last element is the approximate
maximum.

Returns `NULL` if there are zero input rows or `expression` evaluates to
`NULL` for all rows.

To learn more about the optional aggregate clauses that you can pass
into this function, see
[Aggregate function calls][aggregate-function-calls].

<!-- mdlint off(WHITESPACE_LINE_LENGTH) -->

[aggregate-function-calls]: https://github.com/google/googlesql/blob/master/docs/aggregate-function-calls.md

<!-- mdlint on -->

**Supported Argument Types**

+ `expression`: Any supported data type **except**:

  + `ARRAY`
  + `STRUCT`
  + `PROTO`
+ `number`: `INT64` literal or query parameter.

**Returned Data Types**

`ARRAY<T>` where `T` is the type specified by `expression`.

**Examples**

```googlesql
SELECT APPROX_QUANTILES(x, 2) AS approx_quantiles
FROM UNNEST([1, 1, 1, 4, 5, 6, 7, 8, 9, 10]) AS x;

/*------------------+
 | approx_quantiles |
 +------------------+
 | [1, 5, 10]       |
 +------------------*/
```

```googlesql
SELECT APPROX_QUANTILES(x, 100)[OFFSET(90)] AS percentile_90
FROM UNNEST([1, 2, 3, 4, 5, 6, 7, 8, 9, 10]) AS x;

/*---------------+
 | percentile_90 |
 +---------------+
 | 9             |
 +---------------*/
```

```googlesql
SELECT APPROX_QUANTILES(DISTINCT x, 2) AS approx_quantiles
FROM UNNEST([1, 1, 1, 4, 5, 6, 7, 8, 9, 10]) AS x;

/*------------------+
 | approx_quantiles |
 +------------------+
 | [1, 6, 10]       |
 +------------------*/
```

```googlesql
SELECT APPROX_QUANTILES(x, 2 RESPECT NULLS) AS approx_quantiles
FROM UNNEST([NULL, NULL, 1, 1, 1, 4, 5, 6, 7, 8, 9, 10]) AS x;

/*------------------+
 | approx_quantiles |
 +------------------+
 | [NULL, 4, 10]    |
 +------------------*/
```

```googlesql
SELECT APPROX_QUANTILES(DISTINCT x, 2 RESPECT NULLS) AS approx_quantiles
FROM UNNEST([NULL, NULL, 1, 1, 1, 4, 5, 6, 7, 8, 9, 10]) AS x;

/*------------------+
 | approx_quantiles |
 +------------------+
 | [NULL, 6, 10]    |
 +------------------*/
```

## `APPROX_TOP_COUNT`

```googlesql
APPROX_TOP_COUNT(
  expression, number
  [ WHERE where_expression ]
  [ HAVING { MAX | MIN } having_expression ]
)
```

**Description**

Returns the approximate top elements of `expression` as an array of `STRUCT`s.
The `number` parameter specifies the number of elements returned.

Each `STRUCT` contains two fields. The first field (named `value`) contains an
input value. The second field (named `count`) contains an `INT64` specifying the
number of times the value was returned.

Returns `NULL` if there are zero input rows.

To learn more about the optional aggregate clauses that you can pass
into this function, see
[Aggregate function calls][aggregate-function-calls].

<!-- mdlint off(WHITESPACE_LINE_LENGTH) -->

[aggregate-function-calls]: https://github.com/google/googlesql/blob/master/docs/aggregate-function-calls.md

<!-- mdlint on -->

**Supported Argument Types**

+ `expression`: Any data type that the `GROUP BY` clause supports.
+ `number`: `INT64` literal or query parameter.

**Returned Data Types**

`ARRAY<STRUCT>`

**Examples**

```googlesql
SELECT APPROX_TOP_COUNT(x, 2) as approx_top_count
FROM UNNEST(["apple", "apple", "pear", "pear", "pear", "banana"]) as x;

/*-------------------------+
 | approx_top_count        |
 +-------------------------+
 | [{pear, 3}, {apple, 2}] |
 +-------------------------*/
```

**NULL handling**

`APPROX_TOP_COUNT` doesn't ignore `NULL`s in the input. For example:

```googlesql
SELECT APPROX_TOP_COUNT(x, 2) as approx_top_count
FROM UNNEST([NULL, "pear", "pear", "pear", "apple", NULL]) as x;

/*------------------------+
 | approx_top_count       |
 +------------------------+
 | [{pear, 3}, {NULL, 2}] |
 +------------------------*/
```

## `APPROX_TOP_SUM`

```googlesql
APPROX_TOP_SUM(
  expression, weight, number
  [ WHERE where_expression ]
  [ HAVING { MAX | MIN } having_expression ]
)
```

**Description**

Returns the approximate top elements of `expression`, ordered by the sum of the
`weight` values provided for each unique value of `expression`. The `number`
parameter specifies the number of elements returned.

If the `weight` input is negative or `NaN`, this function returns an error.

The elements are returned as an array of `STRUCT`s.
Each `STRUCT` contains two fields: `value` and `sum`.
The `value` field contains the value of the input expression. The `sum` field is
the same type as `weight`, and is the approximate sum of the input weight
associated with the `value` field.

Returns `NULL` if there are zero input rows.

To learn more about the optional aggregate clauses that you can pass
into this function, see
[Aggregate function calls][aggregate-function-calls].

<!-- mdlint off(WHITESPACE_LINE_LENGTH) -->

[aggregate-function-calls]: https://github.com/google/googlesql/blob/master/docs/aggregate-function-calls.md

<!-- mdlint on -->

**Supported Argument Types**

+ `expression`: Any data type that the `GROUP BY` clause supports.
+ `weight`: One of the following:

  + `INT64`
  + `UINT64`
  + `NUMERIC`
  + `BIGNUMERIC`
  + `DOUBLE`
+ `number`: `INT64` literal or query parameter.

**Returned Data Types**

`ARRAY<STRUCT>`

**Examples**

```googlesql
SELECT APPROX_TOP_SUM(x, weight, 2) AS approx_top_sum FROM
UNNEST([
  STRUCT("apple" AS x, 3 AS weight),
  ("pear", 2),
  ("apple", 0),
  ("banana", 5),
  ("pear", 4)
]);

/*--------------------------+
 | approx_top_sum           |
 +--------------------------+
 | [{pear, 6}, {banana, 5}] |
 +--------------------------*/
```

**NULL handling**

`APPROX_TOP_SUM` doesn't ignore `NULL` values for the `expression` and `weight`
parameters.

```googlesql
SELECT APPROX_TOP_SUM(x, weight, 2) AS approx_top_sum FROM
UNNEST([STRUCT("apple" AS x, NULL AS weight), ("pear", 0), ("pear", NULL)]);

/*----------------------------+
 | approx_top_sum             |
 +----------------------------+
 | [{pear, 0}, {apple, NULL}] |
 +----------------------------*/
```

```googlesql
SELECT APPROX_TOP_SUM(x, weight, 2) AS approx_top_sum FROM
UNNEST([STRUCT("apple" AS x, 0 AS weight), (NULL, 2)]);

/*-------------------------+
 | approx_top_sum          |
 +-------------------------+
 | [{NULL, 2}, {apple, 0}] |
 +-------------------------*/
```

```googlesql
SELECT APPROX_TOP_SUM(x, weight, 2) AS approx_top_sum FROM
UNNEST([STRUCT("apple" AS x, 0 AS weight), (NULL, NULL)]);

/*----------------------------+
 | approx_top_sum             |
 +----------------------------+
 | [{apple, 0}, {NULL, NULL}] |
 +----------------------------*/
```

[hll-functions]: https://github.com/google/googlesql/blob/master/docs/hll_functions.md

[aggregate-functions-reference]: https://github.com/google/googlesql/blob/master/docs/aggregate_functions.md

[agg-function-calls]: https://github.com/google/googlesql/blob/master/docs/aggregate-function-calls.md

