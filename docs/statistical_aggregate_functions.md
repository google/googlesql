

<!-- mdlint off(WHITESPACE_LINE_LENGTH) -->

# Statistical aggregate functions

GoogleSQL supports statistical aggregate functions.
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
  <td><a href="https://github.com/google/googlesql/blob/master/docs/statistical_aggregate_functions.md#corr"><code>CORR</code></a>
</td>
  <td>
    Computes the Pearson coefficient of correlation of a set of number pairs.
    
  </td>
</tr>

<tr>
  <td><a href="https://github.com/google/googlesql/blob/master/docs/statistical_aggregate_functions.md#covar_pop"><code>COVAR_POP</code></a>
</td>
  <td>
    Computes the population covariance of a set of number pairs.
    
  </td>
</tr>

<tr>
  <td><a href="https://github.com/google/googlesql/blob/master/docs/statistical_aggregate_functions.md#covar_samp"><code>COVAR_SAMP</code></a>
</td>
  <td>
    Computes the sample covariance of a set of number pairs.
    
  </td>
</tr>

<tr>
  <td><a href="https://github.com/google/googlesql/blob/master/docs/statistical_aggregate_functions.md#stddev"><code>STDDEV</code></a>
</td>
  <td>
    An alias of the <code>STDDEV_SAMP</code> function.
    
  </td>
</tr>

<tr>
  <td><a href="https://github.com/google/googlesql/blob/master/docs/statistical_aggregate_functions.md#stddev_pop"><code>STDDEV_POP</code></a>
</td>
  <td>
    Computes the population (biased) standard deviation of the values.
    
  </td>
</tr>

<tr>
  <td><a href="https://github.com/google/googlesql/blob/master/docs/statistical_aggregate_functions.md#stddev_samp"><code>STDDEV_SAMP</code></a>
</td>
  <td>
    Computes the sample (unbiased) standard deviation of the values.
    
  </td>
</tr>

<tr>
  <td><a href="https://github.com/google/googlesql/blob/master/docs/statistical_aggregate_functions.md#var_pop"><code>VAR_POP</code></a>
</td>
  <td>
    Computes the population (biased) variance of the values.
    
  </td>
</tr>

<tr>
  <td><a href="https://github.com/google/googlesql/blob/master/docs/statistical_aggregate_functions.md#var_samp"><code>VAR_SAMP</code></a>
</td>
  <td>
    Computes the sample (unbiased) variance of the values.
    
  </td>
</tr>

<tr>
  <td><a href="https://github.com/google/googlesql/blob/master/docs/statistical_aggregate_functions.md#variance"><code>VARIANCE</code></a>
</td>
  <td>
    An alias of <code>VAR_SAMP</code>.
    
  </td>
</tr>

  </tbody>
</table>

## `CORR`

```googlesql
CORR(
  X1, X2
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

Returns the [Pearson coefficient][stat-agg-link-to-pearson-coefficient]
of correlation of a set of number pairs. For each number pair, the first number
is the dependent variable and the second number is the independent variable.
The return result is between `-1` and `1`. A result of `0` indicates no
correlation.

All numeric types are supported. If the
input is `NUMERIC` or `BIGNUMERIC` then the internal aggregation is
stable with the final output converted to a `DOUBLE`.
Otherwise the input is converted to a `DOUBLE`
before aggregation, resulting in a potentially unstable result.

This function ignores any input pairs that contain one or more `NULL` values. If
there are fewer than two input pairs without `NULL` values, this function
returns `NULL`.

`NaN` is produced if:

+ Any input value is `NaN`
+ Any input value is positive infinity or negative infinity.
+ The variance of `X1` or `X2` is `0`.
+ The covariance of `X1` and `X2` is `0`.

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

**Return Data Type**

`DOUBLE`

**Examples**

```googlesql
SELECT CORR(y, x) AS results
FROM
  UNNEST(
    [
      STRUCT(1.0 AS y, 5.0 AS x),
      (3.0, 9.0),
      (4.0, 7.0)]);

/*--------------------+
 | results            |
 +--------------------+
 | 0.6546536707079772 |
 +--------------------*/
```

```googlesql
SELECT CORR(y, x) AS results
FROM
  UNNEST(
    [
      STRUCT(1.0 AS y, 5.0 AS x),
      (3.0, 9.0),
      (4.0, NULL)]);

/*---------+
 | results |
 +---------+
 | 1       |
 +---------*/
```

```googlesql
SELECT CORR(y, x) AS results
FROM UNNEST([STRUCT(1.0 AS y, NULL AS x),(9.0, 3.0)])

/*---------+
 | results |
 +---------+
 | NULL    |
 +---------*/
```

```googlesql
SELECT CORR(y, x) AS results
FROM UNNEST([STRUCT(1.0 AS y, NULL AS x),(9.0, NULL)])

/*---------+
 | results |
 +---------+
 | NULL    |
 +---------*/
```

```googlesql
SELECT CORR(y, x) AS results
FROM
  UNNEST(
    [
      STRUCT(1.0 AS y, 5.0 AS x),
      (3.0, 9.0),
      (4.0, 7.0),
      (5.0, 1.0),
      (7.0, CAST('Infinity' as DOUBLE))])

/*---------+
 | results |
 +---------+
 | NaN     |
 +---------*/
```

```googlesql
SELECT CORR(x, y) AS results
FROM
  (
    SELECT 0 AS x, 0 AS y
    UNION ALL
    SELECT 0 AS x, 0 AS y
  )

/*---------+
 | results |
 +---------+
 | NaN     |
 +---------*/
```

[stat-agg-link-to-pearson-coefficient]: https://en.wikipedia.org/wiki/Pearson_product-moment_correlation_coefficient

## `COVAR_POP`

```googlesql
COVAR_POP(
  X1, X2
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

Returns the population [covariance][stat-agg-link-to-covariance] of
a set of number pairs. The first number is the dependent variable; the second
number is the independent variable. The return result is between `-Inf` and
`+Inf`.

All numeric types are supported. If the
input is `NUMERIC` or `BIGNUMERIC` then the internal aggregation is
stable with the final output converted to a `DOUBLE`.
Otherwise the input is converted to a `DOUBLE`
before aggregation, resulting in a potentially unstable result.

This function ignores any input pairs that contain one or more `NULL` values. If
there is no input pair without `NULL` values, this function returns `NULL`.
If there is exactly one input pair without `NULL` values, this function returns
`0`.

`NaN` is produced if:

+ Any input value is `NaN`
+ Any input value is positive infinity or negative infinity.

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

**Return Data Type**

`DOUBLE`

**Examples**

```googlesql
SELECT COVAR_POP(y, x) AS results
FROM
  UNNEST(
    [
      STRUCT(1.0 AS y, 1.0 AS x),
      (2.0, 6.0),
      (9.0, 3.0),
      (2.0, 6.0),
      (9.0, 3.0)])

/*---------------------+
 | results             |
 +---------------------+
 | -1.6800000000000002 |
 +---------------------*/
```

```googlesql
SELECT COVAR_POP(y, x) AS results
FROM UNNEST([STRUCT(1.0 AS y, NULL AS x),(9.0, 3.0)])

/*---------+
 | results |
 +---------+
 | 0       |
 +---------*/
```

```googlesql
SELECT COVAR_POP(y, x) AS results
FROM UNNEST([STRUCT(1.0 AS y, NULL AS x),(9.0, NULL)])

/*---------+
 | results |
 +---------+
 | NULL    |
 +---------*/
```

```googlesql
SELECT COVAR_POP(y, x) AS results
FROM
  UNNEST(
    [
      STRUCT(1.0 AS y, 1.0 AS x),
      (2.0, 6.0),
      (9.0, 3.0),
      (2.0, 6.0),
      (NULL, 3.0)])

/*---------+
 | results |
 +---------+
 | -1      |
 +---------*/
```

```googlesql
SELECT COVAR_POP(y, x) AS results
FROM
  UNNEST(
    [
      STRUCT(1.0 AS y, 1.0 AS x),
      (2.0, 6.0),
      (9.0, 3.0),
      (2.0, 6.0),
      (CAST('Infinity' as DOUBLE), 3.0)])

/*---------+
 | results |
 +---------+
 | NaN     |
 +---------*/
```

[stat-agg-link-to-covariance]: https://en.wikipedia.org/wiki/Covariance

## `COVAR_SAMP`

```googlesql
COVAR_SAMP(
  X1, X2
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

Returns the sample [covariance][stat-agg-link-to-covariance] of a
set of number pairs. The first number is the dependent variable; the second
number is the independent variable. The return result is between `-Inf` and
`+Inf`.

All numeric types are supported. If the
input is `NUMERIC` or `BIGNUMERIC` then the internal aggregation is
stable with the final output converted to a `DOUBLE`.
Otherwise the input is converted to a `DOUBLE`
before aggregation, resulting in a potentially unstable result.

This function ignores any input pairs that contain one or more `NULL` values. If
there are fewer than two input pairs without `NULL` values, this function
returns `NULL`.

`NaN` is produced if:

+ Any input value is `NaN`
+ Any input value is positive infinity or negative infinity.

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

**Return Data Type**

`DOUBLE`

**Examples**

```googlesql
SELECT COVAR_SAMP(y, x) AS results
FROM
  UNNEST(
    [
      STRUCT(1.0 AS y, 1.0 AS x),
      (2.0, 6.0),
      (9.0, 3.0),
      (2.0, 6.0),
      (9.0, 3.0)])

/*---------+
 | results |
 +---------+
 | -2.1    |
 +---------*/
```

```googlesql
SELECT COVAR_SAMP(y, x) AS results
FROM
  UNNEST(
    [
      STRUCT(1.0 AS y, 1.0 AS x),
      (2.0, 6.0),
      (9.0, 3.0),
      (2.0, 6.0),
      (NULL, 3.0)])

/*----------------------+
 | results              |
 +----------------------+
 | --1.3333333333333333 |
 +----------------------*/
```

```googlesql
SELECT COVAR_SAMP(y, x) AS results
FROM UNNEST([STRUCT(1.0 AS y, NULL AS x),(9.0, 3.0)])

/*---------+
 | results |
 +---------+
 | NULL    |
 +---------*/
```

```googlesql
SELECT COVAR_SAMP(y, x) AS results
FROM UNNEST([STRUCT(1.0 AS y, NULL AS x),(9.0, NULL)])

/*---------+
 | results |
 +---------+
 | NULL    |
 +---------*/
```

```googlesql
SELECT COVAR_SAMP(y, x) AS results
FROM
  UNNEST(
    [
      STRUCT(1.0 AS y, 1.0 AS x),
      (2.0, 6.0),
      (9.0, 3.0),
      (2.0, 6.0),
      (CAST('Infinity' as DOUBLE), 3.0)])

/*---------+
 | results |
 +---------+
 | NaN     |
 +---------*/
```

[stat-agg-link-to-covariance]: https://en.wikipedia.org/wiki/Covariance

## `STDDEV`

```googlesql
STDDEV(
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

An alias of [STDDEV_SAMP][stat-agg-link-to-stddev-samp].

[stat-agg-link-to-stddev-samp]: #stddev_samp

## `STDDEV_POP`

```googlesql
STDDEV_POP(
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

Returns the population (biased) standard deviation of the values. The return
result is between `0` and `+Inf`.

All numeric types are supported. If the
input is `NUMERIC` or `BIGNUMERIC` then the internal aggregation is
stable with the final output converted to a `DOUBLE`.
Otherwise the input is converted to a `DOUBLE`
before aggregation, resulting in a potentially unstable result.

This function ignores any `NULL` inputs. If all inputs are ignored, this
function returns `NULL`. If this function receives a single non-`NULL` input,
it returns `0`.

`NaN` is produced if:

+ Any input value is `NaN`
+ Any input value is positive infinity or negative infinity.

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

`STDDEV_POP` can be used with differential privacy. To learn more, see
[Differentially private aggregate functions][dp-functions].

**Return Data Type**

`DOUBLE`

**Examples**

```googlesql
SELECT STDDEV_POP(x) AS results FROM UNNEST([10, 14, 18]) AS x

/*-------------------+
 | results           |
 +-------------------+
 | 3.265986323710904 |
 +-------------------*/
```

```googlesql
SELECT STDDEV_POP(x) AS results FROM UNNEST([10, 14, NULL]) AS x

/*---------+
 | results |
 +---------+
 | 2       |
 +---------*/
```

```googlesql
SELECT STDDEV_POP(x) AS results FROM UNNEST([10, NULL]) AS x

/*---------+
 | results |
 +---------+
 | 0       |
 +---------*/
```

```googlesql
SELECT STDDEV_POP(x) AS results FROM UNNEST([NULL]) AS x

/*---------+
 | results |
 +---------+
 | NULL    |
 +---------*/
```

```googlesql
SELECT STDDEV_POP(x) AS results FROM UNNEST([10, 14, CAST('Infinity' as DOUBLE)]) AS x

/*---------+
 | results |
 +---------+
 | NaN     |
 +---------*/
```

[dp-functions]: https://github.com/google/googlesql/blob/master/docs/aggregate-dp-functions.md

## `STDDEV_SAMP`

```googlesql
STDDEV_SAMP(
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

Returns the sample (unbiased) standard deviation of the values. The return
result is between `0` and `+Inf`.

All numeric types are supported. If the
input is `NUMERIC` or `BIGNUMERIC` then the internal aggregation is
stable with the final output converted to a `DOUBLE`.
Otherwise the input is converted to a `DOUBLE`
before aggregation, resulting in a potentially unstable result.

This function ignores any `NULL` inputs. If there are fewer than two non-`NULL`
inputs, this function returns `NULL`.

`NaN` is produced if:

+ Any input value is `NaN`
+ Any input value is positive infinity or negative infinity.

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

**Return Data Type**

`DOUBLE`

**Examples**

```googlesql
SELECT STDDEV_SAMP(x) AS results FROM UNNEST([10, 14, 18]) AS x

/*---------+
 | results |
 +---------+
 | 4       |
 +---------*/
```

```googlesql
SELECT STDDEV_SAMP(x) AS results FROM UNNEST([10, 14, NULL]) AS x

/*--------------------+
 | results            |
 +--------------------+
 | 2.8284271247461903 |
 +--------------------*/
```

```googlesql
SELECT STDDEV_SAMP(x) AS results FROM UNNEST([10, NULL]) AS x

/*---------+
 | results |
 +---------+
 | NULL    |
 +---------*/
```

```googlesql
SELECT STDDEV_SAMP(x) AS results FROM UNNEST([NULL]) AS x

/*---------+
 | results |
 +---------+
 | NULL    |
 +---------*/
```

```googlesql
SELECT STDDEV_SAMP(x) AS results FROM UNNEST([10, 14, CAST('Infinity' as DOUBLE)]) AS x

/*---------+
 | results |
 +---------+
 | NaN     |
 +---------*/
```

## `VAR_POP`

```googlesql
VAR_POP(
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

Returns the population (biased) variance of the values. The return result is
between `0` and `+Inf`.

All numeric types are supported. If the
input is `NUMERIC` or `BIGNUMERIC` then the internal aggregation is
stable with the final output converted to a `DOUBLE`.
Otherwise the input is converted to a `DOUBLE`
before aggregation, resulting in a potentially unstable result.

This function ignores any `NULL` inputs. If all inputs are ignored, this
function returns `NULL`. If this function receives a single non-`NULL` input,
it returns `0`.

`NaN` is produced if:

+ Any input value is `NaN`
+ Any input value is positive infinity or negative infinity.

To learn more about the `OVER` clause and how to use it, see
[Window function calls][window-function-calls].

<!-- mdlint off(WHITESPACE_LINE_LENGTH) -->

[window-function-calls]: https://github.com/google/googlesql/blob/master/docs/window-function-calls.md

<!-- mdlint on -->

`VAR_POP` can be used with differential privacy. To learn more, see
[Differentially private aggregate functions][dp-functions].

**Return Data Type**

`DOUBLE`

**Examples**

```googlesql
SELECT VAR_POP(x) AS results FROM UNNEST([10, 14, 18]) AS x

/*--------------------+
 | results            |
 +--------------------+
 | 10.666666666666666 |
 +--------------------*/
```

```googlesql
SELECT VAR_POP(x) AS results FROM UNNEST([10, 14, NULL]) AS x

/*----------+
 | results |
 +---------+
 | 4       |
 +---------*/
```

```googlesql
SELECT VAR_POP(x) AS results FROM UNNEST([10, NULL]) AS x

/*----------+
 | results |
 +---------+
 | 0       |
 +---------*/
```

```googlesql
SELECT VAR_POP(x) AS results FROM UNNEST([NULL]) AS x

/*---------+
 | results |
 +---------+
 | NULL    |
 +---------*/
```

```googlesql
SELECT VAR_POP(x) AS results FROM UNNEST([10, 14, CAST('Infinity' as DOUBLE)]) AS x

/*---------+
 | results |
 +---------+
 | NaN     |
 +---------*/
```

[dp-functions]: https://github.com/google/googlesql/blob/master/docs/aggregate-dp-functions.md

## `VAR_SAMP`

```googlesql
VAR_SAMP(
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

Returns the sample (unbiased) variance of the values. The return result is
between `0` and `+Inf`.

All numeric types are supported. If the
input is `NUMERIC` or `BIGNUMERIC` then the internal aggregation is
stable with the final output converted to a `DOUBLE`.
Otherwise the input is converted to a `DOUBLE`
before aggregation, resulting in a potentially unstable result.

This function ignores any `NULL` inputs. If there are fewer than two non-`NULL`
inputs, this function returns `NULL`.

`NaN` is produced if:

+ Any input value is `NaN`
+ Any input value is positive infinity or negative infinity.

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

**Return Data Type**

`DOUBLE`

**Examples**

```googlesql
SELECT VAR_SAMP(x) AS results FROM UNNEST([10, 14, 18]) AS x

/*---------+
 | results |
 +---------+
 | 16      |
 +---------*/
```

```googlesql
SELECT VAR_SAMP(x) AS results FROM UNNEST([10, 14, NULL]) AS x

/*---------+
 | results |
 +---------+
 | 8       |
 +---------*/
```

```googlesql
SELECT VAR_SAMP(x) AS results FROM UNNEST([10, NULL]) AS x

/*---------+
 | results |
 +---------+
 | NULL    |
 +---------*/
```

```googlesql
SELECT VAR_SAMP(x) AS results FROM UNNEST([NULL]) AS x

/*---------+
 | results |
 +---------+
 | NULL    |
 +---------*/
```

```googlesql
SELECT VAR_SAMP(x) AS results FROM UNNEST([10, 14, CAST('Infinity' as DOUBLE)]) AS x

/*---------+
 | results |
 +---------+
 | NaN     |
 +---------*/
```

## `VARIANCE`

```googlesql
VARIANCE(
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

An alias of [VAR_SAMP][stat-agg-link-to-var-samp].

[stat-agg-link-to-var-samp]: #var_samp

[agg-function-calls]: https://github.com/google/googlesql/blob/master/docs/aggregate-function-calls.md

