

<!-- mdlint off(WHITESPACE_LINE_LENGTH) -->

# Numbering functions

GoogleSQL supports numbering functions.
Numbering functions are a subset of window functions. To create a
window function call and learn about the syntax for window functions,
see [Window function calls][window-function-calls].

Numbering functions assign values to each row based on their position
within the specified window. The `OVER` clause syntax varies across
numbering functions.

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
  <td><a href="https://github.com/google/googlesql/blob/master/docs/numbering_functions.md#cume_dist"><code>CUME_DIST</code></a>
</td>
  <td>
    Gets the cumulative distribution (relative position (0,1]) of each row
    within a window.
    
  </td>
</tr>

<tr>
  <td><a href="https://github.com/google/googlesql/blob/master/docs/numbering_functions.md#dense_rank"><code>DENSE_RANK</code></a>
</td>
  <td>
    Gets the dense rank (1-based, no gaps) of each row within a window.
    
  </td>
</tr>

<tr>
  <td>
    
    <a href="https://github.com/google/googlesql/blob/master/docs/numbering_functions.md#is_first"><code>IS_FIRST</code></a>

    
  </td>
  <td>
        Returns <code>true</code> if this row is in the first <code>k</code> rows (1-based) within the window.
    
  </td>
</tr>

<tr>
  <td><a href="https://github.com/google/googlesql/blob/master/docs/numbering_functions.md#IS_LAST"><code>IS_LAST</code></a>
</td>
  <td>
        Returns <code>true</code> if this row is in the last <code>k</code> rows (1-based) within the window.
    
  </td>
</tr>

<tr>
  <td><a href="https://github.com/google/googlesql/blob/master/docs/numbering_functions.md#ntile"><code>NTILE</code></a>
</td>
  <td>
    Gets the quantile bucket number (1-based) of each row within a window.
    
  </td>
</tr>

<tr>
  <td><a href="https://github.com/google/googlesql/blob/master/docs/numbering_functions.md#percent_rank"><code>PERCENT_RANK</code></a>
</td>
  <td>
    Gets the percentile rank (from 0 to 1) of each row within a window.
    
  </td>
</tr>

<tr>
  <td><a href="https://github.com/google/googlesql/blob/master/docs/numbering_functions.md#rank"><code>RANK</code></a>
</td>
  <td>
    Gets the rank (1-based) of each row within a window.
    
  </td>
</tr>

<tr>
  <td><a href="https://github.com/google/googlesql/blob/master/docs/numbering_functions.md#row_number"><code>ROW_NUMBER</code></a>
</td>
  <td>
    Gets the sequential row number (1-based) of each row within a window.
    
  </td>
</tr>

  </tbody>
</table>

## `CUME_DIST`

```googlesql
CUME_DIST()
OVER over_clause

over_clause:
  { named_window | ( [ window_specification ] ) }

window_specification:
  [ named_window ]
  [ PARTITION BY partition_expression [, ...] ]
  ORDER BY expression [ { ASC | DESC }  ] [, ...]

```

**Description**

Return the relative rank of a row defined as NP/NR. NP is defined to be the
number of rows that either precede or are peers with the current row. NR is the
number of rows in the partition.

To learn more about the `OVER` clause and how to use it, see
[Window function calls][window-function-calls].

<!-- mdlint off(WHITESPACE_LINE_LENGTH) -->

[window-function-calls]: https://github.com/google/googlesql/blob/master/docs/window-function-calls.md

<!-- mdlint on -->

**Return Type**

`DOUBLE`

**Example**

```googlesql
WITH finishers AS
 (SELECT 'Sophia Liu' as name,
  TIMESTAMP '2016-10-18 2:51:45' as finish_time,
  'F30-34' as division
  UNION ALL SELECT 'Lisa Stelzner', TIMESTAMP '2016-10-18 2:54:11', 'F35-39'
  UNION ALL SELECT 'Nikki Leith', TIMESTAMP '2016-10-18 2:59:01', 'F30-34'
  UNION ALL SELECT 'Lauren Matthews', TIMESTAMP '2016-10-18 3:01:17', 'F35-39'
  UNION ALL SELECT 'Desiree Berry', TIMESTAMP '2016-10-18 3:05:42', 'F35-39'
  UNION ALL SELECT 'Suzy Slane', TIMESTAMP '2016-10-18 3:06:24', 'F35-39'
  UNION ALL SELECT 'Jen Edwards', TIMESTAMP '2016-10-18 3:06:36', 'F30-34'
  UNION ALL SELECT 'Meghan Lederer', TIMESTAMP '2016-10-18 2:59:01', 'F30-34')
SELECT name,
  finish_time,
  division,
  CUME_DIST() OVER (PARTITION BY division ORDER BY finish_time ASC) AS finish_rank
FROM finishers;

/*-----------------+------------------------+----------+-------------+
 | name            | finish_time            | division | finish_rank |
 +-----------------+------------------------+----------+-------------+
 | Sophia Liu      | 2016-10-18 09:51:45+00 | F30-34   | 0.25        |
 | Meghan Lederer  | 2016-10-18 09:59:01+00 | F30-34   | 0.75        |
 | Nikki Leith     | 2016-10-18 09:59:01+00 | F30-34   | 0.75        |
 | Jen Edwards     | 2016-10-18 10:06:36+00 | F30-34   | 1           |
 | Lisa Stelzner   | 2016-10-18 09:54:11+00 | F35-39   | 0.25        |
 | Lauren Matthews | 2016-10-18 10:01:17+00 | F35-39   | 0.5         |
 | Desiree Berry   | 2016-10-18 10:05:42+00 | F35-39   | 0.75        |
 | Suzy Slane      | 2016-10-18 10:06:24+00 | F35-39   | 1           |
 +-----------------+------------------------+----------+-------------*/
```

## `DENSE_RANK`

```googlesql
DENSE_RANK()
OVER over_clause

over_clause:
  { named_window | ( [ window_specification ] ) }

window_specification:
  [ named_window ]
  [ PARTITION BY partition_expression [, ...] ]
  ORDER BY expression [ { ASC | DESC }  ] [, ...]

```

**Description**

Returns the ordinal (1-based) rank of each row within the window partition.
All peer rows receive the same rank value, and the subsequent rank value is
incremented by one.

To learn more about the `OVER` clause and how to use it, see
[Window function calls][window-function-calls].

<!-- mdlint off(WHITESPACE_LINE_LENGTH) -->

[window-function-calls]: https://github.com/google/googlesql/blob/master/docs/window-function-calls.md

<!-- mdlint on -->

**Return Type**

`INT64`

**Examples**

```googlesql
WITH Numbers AS
 (SELECT 1 as x
  UNION ALL SELECT 2
  UNION ALL SELECT 2
  UNION ALL SELECT 5
  UNION ALL SELECT 8
  UNION ALL SELECT 10
  UNION ALL SELECT 10
)
SELECT x,
  DENSE_RANK() OVER (ORDER BY x ASC) AS dense_rank
FROM Numbers

/*-------------------------+
 | x          | dense_rank |
 +-------------------------+
 | 1          | 1          |
 | 2          | 2          |
 | 2          | 2          |
 | 5          | 3          |
 | 8          | 4          |
 | 10         | 5          |
 | 10         | 5          |
 +-------------------------*/
```

```googlesql
WITH finishers AS
 (SELECT 'Sophia Liu' as name,
  TIMESTAMP '2016-10-18 2:51:45' as finish_time,
  'F30-34' as division
  UNION ALL SELECT 'Lisa Stelzner', TIMESTAMP '2016-10-18 2:54:11', 'F35-39'
  UNION ALL SELECT 'Nikki Leith', TIMESTAMP '2016-10-18 2:59:01', 'F30-34'
  UNION ALL SELECT 'Lauren Matthews', TIMESTAMP '2016-10-18 3:01:17', 'F35-39'
  UNION ALL SELECT 'Desiree Berry', TIMESTAMP '2016-10-18 3:05:42', 'F35-39'
  UNION ALL SELECT 'Suzy Slane', TIMESTAMP '2016-10-18 3:06:24', 'F35-39'
  UNION ALL SELECT 'Jen Edwards', TIMESTAMP '2016-10-18 3:06:36', 'F30-34'
  UNION ALL SELECT 'Meghan Lederer', TIMESTAMP '2016-10-18 2:59:01', 'F30-34')
SELECT name,
  finish_time,
  division,
  DENSE_RANK() OVER (PARTITION BY division ORDER BY finish_time ASC) AS finish_rank
FROM finishers;

/*-----------------+------------------------+----------+-------------+
 | name            | finish_time            | division | finish_rank |
 +-----------------+------------------------+----------+-------------+
 | Sophia Liu      | 2016-10-18 09:51:45+00 | F30-34   | 1           |
 | Meghan Lederer  | 2016-10-18 09:59:01+00 | F30-34   | 2           |
 | Nikki Leith     | 2016-10-18 09:59:01+00 | F30-34   | 2           |
 | Jen Edwards     | 2016-10-18 10:06:36+00 | F30-34   | 3           |
 | Lisa Stelzner   | 2016-10-18 09:54:11+00 | F35-39   | 1           |
 | Lauren Matthews | 2016-10-18 10:01:17+00 | F35-39   | 2           |
 | Desiree Berry   | 2016-10-18 10:05:42+00 | F35-39   | 3           |
 | Suzy Slane      | 2016-10-18 10:06:24+00 | F35-39   | 4           |
 +-----------------+------------------------+----------+-------------*/
```

## `IS_FIRST`

```googlesql
IS_FIRST(k)
OVER over_clause

over_clause:
  { named_window | ( [ window_specification ] ) }

window_specification:
  [ named_window ]
  [ PARTITION BY partition_expression [, ...] ]
  [ ORDER BY expression [ { ASC | DESC }  ] [, ...] ]

```

**Description**

Returns `true` if the current row is in the first `k` rows (1-based) in the
window; otherwise, returns `false`. This function doesn't require the `ORDER BY`
clause.

**Details**

* The `k` value must be positive; otherwise, a runtime error is raised.
* If `k` is 0, the scenario is considered a degenerate case where the result is always `false`.
* If `k` is `NULL`, the result is `NULL`.
* Disallows the window framing clause, similar to the `ROW_NUMBER` function.
* If any rows are tied or if `ORDER BY` is omitted, the result is non-deterministic.
  If the `ORDER BY` clause is unspecified or if all rows are tied, the
  result is equivalent to `ANY-k`.

To learn more about the `OVER` clause and how to use it, see
[Window function calls][window-function-calls].

<!-- mdlint off(WHITESPACE_LINE_LENGTH) -->

[window-function-calls]: https://github.com/google/googlesql/blob/master/docs/window-function-calls.md

<!-- mdlint on -->

**Return Type**

`BOOL`

**Examples**

```googlesql
WITH Numbers AS
 (SELECT 1 as x
  UNION ALL SELECT 2
  UNION ALL SELECT 2
  UNION ALL SELECT 5
  UNION ALL SELECT 8
  UNION ALL SELECT 10
  UNION ALL SELECT 10
)
SELECT x,
  IS_FIRST(2) OVER (ORDER BY x) AS is_first
FROM Numbers

/*-------------------------+
 | x          | is_first   |
 +-------------------------+
 | 1          | true       |
 | 2          | true       |
 | 2          | false      |
 | 5          | false      |
 | 8          | false      |
 | 10         | false      |
 | 10         | false      |
 +-------------------------*/
```

```googlesql
WITH finishers AS
 (SELECT 'Sophia Liu' as name,
  TIMESTAMP '2016-10-18 2:51:45' as finish_time,
  'F30-34' as division
  UNION ALL SELECT 'Lisa Stelzner', TIMESTAMP '2016-10-18 2:54:11', 'F35-39'
  UNION ALL SELECT 'Nikki Leith', TIMESTAMP '2016-10-18 2:59:01', 'F30-34'
  UNION ALL SELECT 'Lauren Matthews', TIMESTAMP '2016-10-18 3:01:17', 'F35-39'
  UNION ALL SELECT 'Desiree Berry', TIMESTAMP '2016-10-18 3:05:42', 'F35-39'
  UNION ALL SELECT 'Suzy Slane', TIMESTAMP '2016-10-18 3:06:24', 'F35-39'
  UNION ALL SELECT 'Jen Edwards', TIMESTAMP '2016-10-18 3:06:36', 'F30-34'
  UNION ALL SELECT 'Meghan Lederer', TIMESTAMP '2016-10-18 2:59:01', 'F30-34')
SELECT name,
  finish_time,
  division,
  IS_FIRST(2) OVER (PARTITION BY division ORDER BY finish_time ASC) AS is_first
FROM finishers;

/*-----------------+------------------------+----------+-------------+
 | name            | finish_time            | division | finish_rank |
 +-----------------+------------------------+----------+-------------+
 | Sophia Liu      | 2016-10-18 09:51:45+00 | F30-34   | true        |
 | Meghan Lederer  | 2016-10-18 09:59:01+00 | F30-34   | true        |
 | Nikki Leith     | 2016-10-18 09:59:01+00 | F30-34   | false       |
 | Jen Edwards     | 2016-10-18 10:06:36+00 | F30-34   | false       |
 | Lisa Stelzner   | 2016-10-18 09:54:11+00 | F35-39   | true        |
 | Lauren Matthews | 2016-10-18 10:01:17+00 | F35-39   | true        |
 | Desiree Berry   | 2016-10-18 10:05:42+00 | F35-39   | false       |
 | Suzy Slane      | 2016-10-18 10:06:24+00 | F35-39   | false       |
 +-----------------+------------------------+----------+-------------*/
```

## `IS_LAST`

```googlesql
IS_LAST(k)
OVER over_clause

over_clause:
  { named_window | ( [ window_specification ] ) }

window_specification:
  [ named_window ]
  [ PARTITION BY partition_expression [, ...] ]
  [ ORDER BY expression [ { ASC | DESC }  ] [, ...] ]

```

**Description**

Returns `true` if the current row is in the last `k` rows (1-based) in the
window; otherwise, returns `false`. This function doesn't require the `ORDER BY`
clause.

**Details**

* The `k` value must be positive; otherwise, a runtime error is raised.
* If `k` is 0, the scenario is considered a degenerate case where the result is always `false`.
* If `k` is `NULL`, the result is `NULL`.
* Disallows the window framing clause, similar to the `ROW_NUMBER` function.
* If any rows are tied or if `ORDER BY` is omitted, the result is non-deterministic.
  If the `ORDER BY` clause is unspecified or if all rows are tied, the
  result is equivalent to `ANY-k`.

To learn more about the `OVER` clause and how to use it, see
[Window function calls][window-function-calls].

<!-- mdlint off(WHITESPACE_LINE_LENGTH) -->

[window-function-calls]: https://github.com/google/googlesql/blob/master/docs/window-function-calls.md

<!-- mdlint on -->

**Return Type**

`BOOL`

**Examples**

```googlesql
WITH Numbers AS
 (SELECT 1 as x
  UNION ALL SELECT 2
  UNION ALL SELECT 2
  UNION ALL SELECT 5
  UNION ALL SELECT 10
  UNION ALL SELECT 10
  UNION ALL SELECT 10
)
SELECT x,
  IS_LAST(2) OVER (ORDER BY x) AS is_last
FROM Numbers

/*-------------------------+
 | x          | is_last    |
 +-------------------------+
 | 1          | false      |
 | 2          | false      |
 | 2          | false      |
 | 5          | false      |
 | 10         | false      |
 | 10         | true       |
 | 10         | true       |
 +-------------------------*/
```

```googlesql
WITH finishers AS
 (SELECT 'Sophia Liu' as name,
  TIMESTAMP '2016-10-18 2:51:45' as finish_time,
  'F30-34' as division
  UNION ALL SELECT 'Lisa Stelzner', TIMESTAMP '2016-10-18 2:54:11', 'F35-39'
  UNION ALL SELECT 'Nikki Leith', TIMESTAMP '2016-10-18 2:59:01', 'F30-34'
  UNION ALL SELECT 'Lauren Matthews', TIMESTAMP '2016-10-18 3:01:17', 'F35-39'
  UNION ALL SELECT 'Desiree Berry', TIMESTAMP '2016-10-18 3:05:42', 'F35-39'
  UNION ALL SELECT 'Suzy Slane', TIMESTAMP '2016-10-18 3:06:24', 'F35-39'
  UNION ALL SELECT 'Jen Edwards', TIMESTAMP '2016-10-18 3:06:36', 'F30-34'
  UNION ALL SELECT 'Meghan Lederer', TIMESTAMP '2016-10-18 2:59:01', 'F30-34')
SELECT name,
  finish_time,
  division,
  IS_LAST(2) OVER (PARTITION BY division ORDER BY finish_time ASC) AS is_last
FROM finishers;

/*-----------------+------------------------+----------+-------------+
 | name            | finish_time            | division | finish_rank |
 +-----------------+------------------------+----------+-------------+
 | Sophia Liu      | 2016-10-18 09:51:45+00 | F30-34   | false       |
 | Meghan Lederer  | 2016-10-18 09:59:01+00 | F30-34   | false       |
 | Nikki Leith     | 2016-10-18 09:59:01+00 | F30-34   | true        |
 | Jen Edwards     | 2016-10-18 10:06:36+00 | F30-34   | true        |
 | Lisa Stelzner   | 2016-10-18 09:54:11+00 | F35-39   | false       |
 | Lauren Matthews | 2016-10-18 10:01:17+00 | F35-39   | false       |
 | Desiree Berry   | 2016-10-18 10:05:42+00 | F35-39   | true        |
 | Suzy Slane      | 2016-10-18 10:06:24+00 | F35-39   | true        |
 +-----------------+------------------------+----------+-------------*/
```

## `NTILE`

```googlesql
NTILE(constant_integer_expression)
OVER over_clause

over_clause:
  { named_window | ( [ window_specification ] ) }

window_specification:
  [ named_window ]
  [ PARTITION BY partition_expression [, ...] ]
  ORDER BY expression [ { ASC | DESC }  ] [, ...]

```

**Description**

This function divides the rows into `constant_integer_expression`
buckets based on row ordering and returns the 1-based bucket number that is
assigned to each row. The number of rows in the buckets can differ by at most 1.
The remainder values (the remainder of number of rows divided by buckets) are
distributed one for each bucket, starting with bucket 1. If
`constant_integer_expression` evaluates to NULL, 0 or negative, an
error is provided.

To learn more about the `OVER` clause and how to use it, see
[Window function calls][window-function-calls].

<!-- mdlint off(WHITESPACE_LINE_LENGTH) -->

[window-function-calls]: https://github.com/google/googlesql/blob/master/docs/window-function-calls.md

<!-- mdlint on -->

**Return Type**

`INT64`

**Example**

```googlesql
WITH finishers AS
 (SELECT 'Sophia Liu' as name,
  TIMESTAMP '2016-10-18 2:51:45' as finish_time,
  'F30-34' as division
  UNION ALL SELECT 'Lisa Stelzner', TIMESTAMP '2016-10-18 2:54:11', 'F35-39'
  UNION ALL SELECT 'Nikki Leith', TIMESTAMP '2016-10-18 2:59:01', 'F30-34'
  UNION ALL SELECT 'Lauren Matthews', TIMESTAMP '2016-10-18 3:01:17', 'F35-39'
  UNION ALL SELECT 'Desiree Berry', TIMESTAMP '2016-10-18 3:05:42', 'F35-39'
  UNION ALL SELECT 'Suzy Slane', TIMESTAMP '2016-10-18 3:06:24', 'F35-39'
  UNION ALL SELECT 'Jen Edwards', TIMESTAMP '2016-10-18 3:06:36', 'F30-34'
  UNION ALL SELECT 'Meghan Lederer', TIMESTAMP '2016-10-18 2:59:01', 'F30-34')
SELECT name,
  finish_time,
  division,
  NTILE(3) OVER (PARTITION BY division ORDER BY finish_time ASC) AS finish_rank
FROM finishers;

/*-----------------+------------------------+----------+-------------+
 | name            | finish_time            | division | finish_rank |
 +-----------------+------------------------+----------+-------------+
 | Sophia Liu      | 2016-10-18 09:51:45+00 | F30-34   | 1           |
 | Meghan Lederer  | 2016-10-18 09:59:01+00 | F30-34   | 1           |
 | Nikki Leith     | 2016-10-18 09:59:01+00 | F30-34   | 2           |
 | Jen Edwards     | 2016-10-18 10:06:36+00 | F30-34   | 3           |
 | Lisa Stelzner   | 2016-10-18 09:54:11+00 | F35-39   | 1           |
 | Lauren Matthews | 2016-10-18 10:01:17+00 | F35-39   | 1           |
 | Desiree Berry   | 2016-10-18 10:05:42+00 | F35-39   | 2           |
 | Suzy Slane      | 2016-10-18 10:06:24+00 | F35-39   | 3           |
 +-----------------+------------------------+----------+-------------*/
```

## `PERCENT_RANK`

```googlesql
PERCENT_RANK()
OVER over_clause

over_clause:
  { named_window | ( [ window_specification ] ) }

window_specification:
  [ named_window ]
  [ PARTITION BY partition_expression [, ...] ]
  ORDER BY expression [ { ASC | DESC }  ] [, ...]

```

**Description**

Return the percentile rank of a row defined as (RK-1)/(NR-1), where RK is
the `RANK` of the row and NR is the number of rows in the partition.
Returns 0 if NR=1.

To learn more about the `OVER` clause and how to use it, see
[Window function calls][window-function-calls].

<!-- mdlint off(WHITESPACE_LINE_LENGTH) -->

[window-function-calls]: https://github.com/google/googlesql/blob/master/docs/window-function-calls.md

<!-- mdlint on -->

**Return Type**

`DOUBLE`

**Example**

```googlesql
WITH finishers AS
 (SELECT 'Sophia Liu' as name,
  TIMESTAMP '2016-10-18 2:51:45' as finish_time,
  'F30-34' as division
  UNION ALL SELECT 'Lisa Stelzner', TIMESTAMP '2016-10-18 2:54:11', 'F35-39'
  UNION ALL SELECT 'Nikki Leith', TIMESTAMP '2016-10-18 2:59:01', 'F30-34'
  UNION ALL SELECT 'Lauren Matthews', TIMESTAMP '2016-10-18 3:01:17', 'F35-39'
  UNION ALL SELECT 'Desiree Berry', TIMESTAMP '2016-10-18 3:05:42', 'F35-39'
  UNION ALL SELECT 'Suzy Slane', TIMESTAMP '2016-10-18 3:06:24', 'F35-39'
  UNION ALL SELECT 'Jen Edwards', TIMESTAMP '2016-10-18 3:06:36', 'F30-34'
  UNION ALL SELECT 'Meghan Lederer', TIMESTAMP '2016-10-18 2:59:01', 'F30-34')
SELECT name,
  finish_time,
  division,
  PERCENT_RANK() OVER (PARTITION BY division ORDER BY finish_time ASC) AS finish_rank
FROM finishers;

/*-----------------+------------------------+----------+---------------------+
 | name            | finish_time            | division | finish_rank         |
 +-----------------+------------------------+----------+---------------------+
 | Sophia Liu      | 2016-10-18 09:51:45+00 | F30-34   | 0                   |
 | Meghan Lederer  | 2016-10-18 09:59:01+00 | F30-34   | 0.33333333333333331 |
 | Nikki Leith     | 2016-10-18 09:59:01+00 | F30-34   | 0.33333333333333331 |
 | Jen Edwards     | 2016-10-18 10:06:36+00 | F30-34   | 1                   |
 | Lisa Stelzner   | 2016-10-18 09:54:11+00 | F35-39   | 0                   |
 | Lauren Matthews | 2016-10-18 10:01:17+00 | F35-39   | 0.33333333333333331 |
 | Desiree Berry   | 2016-10-18 10:05:42+00 | F35-39   | 0.66666666666666663 |
 | Suzy Slane      | 2016-10-18 10:06:24+00 | F35-39   | 1                   |
 +-----------------+------------------------+----------+---------------------*/
```

## `RANK`

```googlesql
RANK()
OVER over_clause

over_clause:
  { named_window | ( [ window_specification ] ) }

window_specification:
  [ named_window ]
  [ PARTITION BY partition_expression [, ...] ]
  ORDER BY expression [ { ASC | DESC }  ] [, ...]

```

**Description**

Returns the ordinal (1-based) rank of each row within the ordered partition.
All peer rows receive the same rank value. The next row or set of peer rows
receives a rank value which increments by the number of peers with the previous
rank value, instead of `DENSE_RANK`, which always increments by 1.

To learn more about the `OVER` clause and how to use it, see
[Window function calls][window-function-calls].

<!-- mdlint off(WHITESPACE_LINE_LENGTH) -->

[window-function-calls]: https://github.com/google/googlesql/blob/master/docs/window-function-calls.md

<!-- mdlint on -->

**Return Type**

`INT64`

**Examples**

```googlesql
WITH Numbers AS
 (SELECT 1 as x
  UNION ALL SELECT 2
  UNION ALL SELECT 2
  UNION ALL SELECT 5
  UNION ALL SELECT 8
  UNION ALL SELECT 10
  UNION ALL SELECT 10
)
SELECT x,
  RANK() OVER (ORDER BY x ASC) AS rank
FROM Numbers

/*-------------------------+
 | x          | rank       |
 +-------------------------+
 | 1          | 1          |
 | 2          | 2          |
 | 2          | 2          |
 | 5          | 4          |
 | 8          | 5          |
 | 10         | 6          |
 | 10         | 6          |
 +-------------------------*/
```

```googlesql
WITH finishers AS
 (SELECT 'Sophia Liu' as name,
  TIMESTAMP '2016-10-18 2:51:45' as finish_time,
  'F30-34' as division
  UNION ALL SELECT 'Lisa Stelzner', TIMESTAMP '2016-10-18 2:54:11', 'F35-39'
  UNION ALL SELECT 'Nikki Leith', TIMESTAMP '2016-10-18 2:59:01', 'F30-34'
  UNION ALL SELECT 'Lauren Matthews', TIMESTAMP '2016-10-18 3:01:17', 'F35-39'
  UNION ALL SELECT 'Desiree Berry', TIMESTAMP '2016-10-18 3:05:42', 'F35-39'
  UNION ALL SELECT 'Suzy Slane', TIMESTAMP '2016-10-18 3:06:24', 'F35-39'
  UNION ALL SELECT 'Jen Edwards', TIMESTAMP '2016-10-18 3:06:36', 'F30-34'
  UNION ALL SELECT 'Meghan Lederer', TIMESTAMP '2016-10-18 2:59:01', 'F30-34')
SELECT name,
  finish_time,
  division,
  RANK() OVER (PARTITION BY division ORDER BY finish_time ASC) AS finish_rank
FROM finishers;

/*-----------------+------------------------+----------+-------------+
 | name            | finish_time            | division | finish_rank |
 +-----------------+------------------------+----------+-------------+
 | Sophia Liu      | 2016-10-18 09:51:45+00 | F30-34   | 1           |
 | Meghan Lederer  | 2016-10-18 09:59:01+00 | F30-34   | 2           |
 | Nikki Leith     | 2016-10-18 09:59:01+00 | F30-34   | 2           |
 | Jen Edwards     | 2016-10-18 10:06:36+00 | F30-34   | 4           |
 | Lisa Stelzner   | 2016-10-18 09:54:11+00 | F35-39   | 1           |
 | Lauren Matthews | 2016-10-18 10:01:17+00 | F35-39   | 2           |
 | Desiree Berry   | 2016-10-18 10:05:42+00 | F35-39   | 3           |
 | Suzy Slane      | 2016-10-18 10:06:24+00 | F35-39   | 4           |
 +-----------------+------------------------+----------+-------------*/
```

## `ROW_NUMBER`

```googlesql
ROW_NUMBER()
OVER over_clause

over_clause:
  { named_window | ( [ window_specification ] ) }

window_specification:
  [ named_window ]
  [ PARTITION BY partition_expression [, ...] ]
  [ ORDER BY expression [ { ASC | DESC }  ] [, ...] ]

```

**Description**

Returns the sequential row ordinal (1-based) of each row for each ordered
partition. The order of row numbers within their peer group is
non-deterministic.

Doesn't require the `ORDER BY` clause. If the `ORDER BY` clause is unspecified
then the result is non-deterministic.

To learn more about the `OVER` clause and how to use it, see
[Window function calls][window-function-calls].

<!-- mdlint off(WHITESPACE_LINE_LENGTH) -->

[window-function-calls]: https://github.com/google/googlesql/blob/master/docs/window-function-calls.md

<!-- mdlint on -->

**Return Type**

`INT64`

**Examples**

```googlesql
WITH Numbers AS
 (SELECT 1 as x
  UNION ALL SELECT 2
  UNION ALL SELECT 2
  UNION ALL SELECT 5
  UNION ALL SELECT 8
  UNION ALL SELECT 10
  UNION ALL SELECT 10
)
SELECT x,
  ROW_NUMBER() OVER (ORDER BY x) AS row_num
FROM Numbers

/*-------------------------+
 | x          | row_num    |
 +-------------------------+
 | 1          | 1          |
 | 2          | 2          |
 | 2          | 3          |
 | 5          | 4          |
 | 8          | 5          |
 | 10         | 6          |
 | 10         | 7          |
 +-------------------------*/
```

```googlesql
WITH finishers AS
 (SELECT 'Sophia Liu' as name,
  TIMESTAMP '2016-10-18 2:51:45' as finish_time,
  'F30-34' as division
  UNION ALL SELECT 'Lisa Stelzner', TIMESTAMP '2016-10-18 2:54:11', 'F35-39'
  UNION ALL SELECT 'Nikki Leith', TIMESTAMP '2016-10-18 2:59:01', 'F30-34'
  UNION ALL SELECT 'Lauren Matthews', TIMESTAMP '2016-10-18 3:01:17', 'F35-39'
  UNION ALL SELECT 'Desiree Berry', TIMESTAMP '2016-10-18 3:05:42', 'F35-39'
  UNION ALL SELECT 'Suzy Slane', TIMESTAMP '2016-10-18 3:06:24', 'F35-39'
  UNION ALL SELECT 'Jen Edwards', TIMESTAMP '2016-10-18 3:06:36', 'F30-34'
  UNION ALL SELECT 'Meghan Lederer', TIMESTAMP '2016-10-18 2:59:01', 'F30-34')
SELECT name,
  finish_time,
  division,
  ROW_NUMBER() OVER (PARTITION BY division ORDER BY finish_time ASC) AS finish_rank
FROM finishers;

/*-----------------+------------------------+----------+-------------+
 | name            | finish_time            | division | finish_rank |
 +-----------------+------------------------+----------+-------------+
 | Sophia Liu      | 2016-10-18 09:51:45+00 | F30-34   | 1           |
 | Meghan Lederer  | 2016-10-18 09:59:01+00 | F30-34   | 2           |
 | Nikki Leith     | 2016-10-18 09:59:01+00 | F30-34   | 3           |
 | Jen Edwards     | 2016-10-18 10:06:36+00 | F30-34   | 4           |
 | Lisa Stelzner   | 2016-10-18 09:54:11+00 | F35-39   | 1           |
 | Lauren Matthews | 2016-10-18 10:01:17+00 | F35-39   | 2           |
 | Desiree Berry   | 2016-10-18 10:05:42+00 | F35-39   | 3           |
 | Suzy Slane      | 2016-10-18 10:06:24+00 | F35-39   | 4           |
 +-----------------+------------------------+----------+-------------*/
```

<!-- mdlint off(WHITESPACE_LINE_LENGTH) -->

[window-function-calls]: https://github.com/google/googlesql/blob/master/docs/window-function-calls.md

<!-- mdlint on -->

