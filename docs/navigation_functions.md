

<!-- mdlint off(WHITESPACE_LINE_LENGTH) -->

# Navigation functions

GoogleSQL supports navigation functions.
Navigation functions are a subset of window functions. To create a
window function call and learn about the syntax for window functions,
see [Window function_calls][window-function-calls].

Navigation functions generally compute some
`value_expression` over a different row in the window frame from the
current row. The `OVER` clause syntax varies across navigation functions.

For all navigation functions, the result data type is the same type as
`value_expression`.

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
  <td><a href="https://github.com/google/googlesql/blob/master/docs/navigation_functions.md#first_value"><code>FIRST_VALUE</code></a>
</td>
  <td>
    Gets a value for the first row in the current window frame.
    
  </td>
</tr>

<tr>
  <td><a href="https://github.com/google/googlesql/blob/master/docs/navigation_functions.md#lag"><code>LAG</code></a>
</td>
  <td>
    Gets a value for a preceding row.
    
  </td>
</tr>

<tr>
  <td><a href="https://github.com/google/googlesql/blob/master/docs/navigation_functions.md#last_value"><code>LAST_VALUE</code></a>
</td>
  <td>
    Gets a value for the last row in the current window frame.
    
  </td>
</tr>

<tr>
  <td><a href="https://github.com/google/googlesql/blob/master/docs/navigation_functions.md#lead"><code>LEAD</code></a>
</td>
  <td>
    Gets a value for a subsequent row.
    
  </td>
</tr>

<tr>
  <td><a href="https://github.com/google/googlesql/blob/master/docs/navigation_functions.md#nth_value"><code>NTH_VALUE</code></a>
</td>
  <td>
    Gets a value for the Nth row of the current window frame.
    
  </td>
</tr>

<tr>
  <td><a href="https://github.com/google/googlesql/blob/master/docs/navigation_functions.md#percentile_cont"><code>PERCENTILE_CONT</code></a>
</td>
  <td>
    Computes the specified percentile for a value, using
    linear interpolation.
    
  </td>
</tr>

<tr>
  <td><a href="https://github.com/google/googlesql/blob/master/docs/navigation_functions.md#percentile_disc"><code>PERCENTILE_DISC</code></a>
</td>
  <td>
    Computes the specified percentile for a discrete value.
    
  </td>
</tr>

  </tbody>
</table>

## `FIRST_VALUE`

```googlesql
FIRST_VALUE (value_expression [{RESPECT | IGNORE} NULLS])
OVER over_clause

over_clause:
  { named_window | ( [ window_specification ] ) }

window_specification:
  [ named_window ]
  [ PARTITION BY partition_expression [, ...] ]
  ORDER BY expression [ { ASC | DESC }  ] [, ...]
  [ window_frame_clause ]

```

**Description**

Returns the value of the `value_expression` for the first row in the current
window frame.

This function includes `NULL` values in the calculation unless `IGNORE NULLS` is
present. If `IGNORE NULLS` is present, the function excludes `NULL` values from
the calculation.

To learn more about the `OVER` clause and how to use it, see
[Window function calls][window-function-calls].

<!-- mdlint off(WHITESPACE_LINE_LENGTH) -->

[window-function-calls]: https://github.com/google/googlesql/blob/master/docs/window-function-calls.md

<!-- mdlint on -->

**Supported Argument Types**

`value_expression` can be any data type that an expression can return.

**Return Data Type**

Same type as `value_expression`.

**Examples**

The following example computes the fastest time for each division.

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
  UNION ALL SELECT 'Meghan Lederer', TIMESTAMP '2016-10-18 3:07:41', 'F30-34'
  UNION ALL SELECT 'Carly Forte', TIMESTAMP '2016-10-18 3:08:58', 'F25-29'
  UNION ALL SELECT 'Lauren Reasoner', TIMESTAMP '2016-10-18 3:10:14', 'F30-34')
SELECT name,
  FORMAT_TIMESTAMP('%X', finish_time) AS finish_time,
  division,
  FORMAT_TIMESTAMP('%X', fastest_time) AS fastest_time,
  TIMESTAMP_DIFF(finish_time, fastest_time, SECOND) AS delta_in_seconds
FROM (
  SELECT name,
  finish_time,
  division,
  FIRST_VALUE(finish_time)
    OVER (PARTITION BY division ORDER BY finish_time ASC
    ROWS BETWEEN UNBOUNDED PRECEDING AND UNBOUNDED FOLLOWING) AS fastest_time
  FROM finishers);

/*-----------------+-------------+----------+--------------+------------------+
 | name            | finish_time | division | fastest_time | delta_in_seconds |
 +-----------------+-------------+----------+--------------+------------------+
 | Carly Forte     | 03:08:58    | F25-29   | 03:08:58     | 0                |
 | Sophia Liu      | 02:51:45    | F30-34   | 02:51:45     | 0                |
 | Nikki Leith     | 02:59:01    | F30-34   | 02:51:45     | 436              |
 | Jen Edwards     | 03:06:36    | F30-34   | 02:51:45     | 891              |
 | Meghan Lederer  | 03:07:41    | F30-34   | 02:51:45     | 956              |
 | Lauren Reasoner | 03:10:14    | F30-34   | 02:51:45     | 1109             |
 | Lisa Stelzner   | 02:54:11    | F35-39   | 02:54:11     | 0                |
 | Lauren Matthews | 03:01:17    | F35-39   | 02:54:11     | 426              |
 | Desiree Berry   | 03:05:42    | F35-39   | 02:54:11     | 691              |
 | Suzy Slane      | 03:06:24    | F35-39   | 02:54:11     | 733              |
 +-----------------+-------------+----------+--------------+------------------*/
```

## `LAG`

```googlesql
LAG (value_expression[, offset [, default_expression]])
OVER over_clause

over_clause:
  { named_window | ( [ window_specification ] ) }

window_specification:
  [ named_window ]
  [ PARTITION BY partition_expression [, ...] ]
  ORDER BY expression [ { ASC | DESC }  ] [, ...]

```

**Description**

Returns the value of the `value_expression` on a preceding row. Changing the
`offset` value changes which preceding row is returned; the default value is
`1`, indicating the previous row in the window frame. An error occurs if
`offset` is NULL or a negative value.

The optional `default_expression` is used if there isn't a row in the window
frame at the specified offset. This expression must be a constant expression and
its type must be implicitly coercible to the type of `value_expression`. If left
unspecified, `default_expression` defaults to NULL.

To learn more about the `OVER` clause and how to use it, see
[Window function calls][window-function-calls].

<!-- mdlint off(WHITESPACE_LINE_LENGTH) -->

[window-function-calls]: https://github.com/google/googlesql/blob/master/docs/window-function-calls.md

<!-- mdlint on -->

**Supported Argument Types**

+ `value_expression` can be any data type that can be returned from an
  expression.
+ `offset` must be a non-negative integer literal or parameter.
+ `default_expression` must be compatible with the value expression type.

**Return Data Type**

Same type as `value_expression`.

**Examples**

The following example illustrates a basic use of the `LAG` function.

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
  UNION ALL SELECT 'Meghan Lederer', TIMESTAMP '2016-10-18 3:07:41', 'F30-34'
  UNION ALL SELECT 'Carly Forte', TIMESTAMP '2016-10-18 3:08:58', 'F25-29'
  UNION ALL SELECT 'Lauren Reasoner', TIMESTAMP '2016-10-18 3:10:14', 'F30-34')
SELECT name,
  finish_time,
  division,
  LAG(name)
    OVER (PARTITION BY division ORDER BY finish_time ASC) AS preceding_runner
FROM finishers;

/*-----------------+-------------+----------+------------------+
 | name            | finish_time | division | preceding_runner |
 +-----------------+-------------+----------+------------------+
 | Carly Forte     | 03:08:58    | F25-29   | NULL             |
 | Sophia Liu      | 02:51:45    | F30-34   | NULL             |
 | Nikki Leith     | 02:59:01    | F30-34   | Sophia Liu       |
 | Jen Edwards     | 03:06:36    | F30-34   | Nikki Leith      |
 | Meghan Lederer  | 03:07:41    | F30-34   | Jen Edwards      |
 | Lauren Reasoner | 03:10:14    | F30-34   | Meghan Lederer   |
 | Lisa Stelzner   | 02:54:11    | F35-39   | NULL             |
 | Lauren Matthews | 03:01:17    | F35-39   | Lisa Stelzner    |
 | Desiree Berry   | 03:05:42    | F35-39   | Lauren Matthews  |
 | Suzy Slane      | 03:06:24    | F35-39   | Desiree Berry    |
 +-----------------+-------------+----------+------------------*/
```

This next example uses the optional `offset` parameter.

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
  UNION ALL SELECT 'Meghan Lederer', TIMESTAMP '2016-10-18 3:07:41', 'F30-34'
  UNION ALL SELECT 'Carly Forte', TIMESTAMP '2016-10-18 3:08:58', 'F25-29'
  UNION ALL SELECT 'Lauren Reasoner', TIMESTAMP '2016-10-18 3:10:14', 'F30-34')
SELECT name,
  finish_time,
  division,
  LAG(name, 2)
    OVER (PARTITION BY division ORDER BY finish_time ASC) AS two_runners_ahead
FROM finishers;

/*-----------------+-------------+----------+-------------------+
 | name            | finish_time | division | two_runners_ahead |
 +-----------------+-------------+----------+-------------------+
 | Carly Forte     | 03:08:58    | F25-29   | NULL              |
 | Sophia Liu      | 02:51:45    | F30-34   | NULL              |
 | Nikki Leith     | 02:59:01    | F30-34   | NULL              |
 | Jen Edwards     | 03:06:36    | F30-34   | Sophia Liu        |
 | Meghan Lederer  | 03:07:41    | F30-34   | Nikki Leith       |
 | Lauren Reasoner | 03:10:14    | F30-34   | Jen Edwards       |
 | Lisa Stelzner   | 02:54:11    | F35-39   | NULL              |
 | Lauren Matthews | 03:01:17    | F35-39   | NULL              |
 | Desiree Berry   | 03:05:42    | F35-39   | Lisa Stelzner     |
 | Suzy Slane      | 03:06:24    | F35-39   | Lauren Matthews   |
 +-----------------+-------------+----------+-------------------*/
```

The following example replaces NULL values with a default value.

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
  UNION ALL SELECT 'Meghan Lederer', TIMESTAMP '2016-10-18 3:07:41', 'F30-34'
  UNION ALL SELECT 'Carly Forte', TIMESTAMP '2016-10-18 3:08:58', 'F25-29'
  UNION ALL SELECT 'Lauren Reasoner', TIMESTAMP '2016-10-18 3:10:14', 'F30-34')
SELECT name,
  finish_time,
  division,
  LAG(name, 2, 'Nobody')
    OVER (PARTITION BY division ORDER BY finish_time ASC) AS two_runners_ahead
FROM finishers;

/*-----------------+-------------+----------+-------------------+
 | name            | finish_time | division | two_runners_ahead |
 +-----------------+-------------+----------+-------------------+
 | Carly Forte     | 03:08:58    | F25-29   | Nobody            |
 | Sophia Liu      | 02:51:45    | F30-34   | Nobody            |
 | Nikki Leith     | 02:59:01    | F30-34   | Nobody            |
 | Jen Edwards     | 03:06:36    | F30-34   | Sophia Liu        |
 | Meghan Lederer  | 03:07:41    | F30-34   | Nikki Leith       |
 | Lauren Reasoner | 03:10:14    | F30-34   | Jen Edwards       |
 | Lisa Stelzner   | 02:54:11    | F35-39   | Nobody            |
 | Lauren Matthews | 03:01:17    | F35-39   | Nobody            |
 | Desiree Berry   | 03:05:42    | F35-39   | Lisa Stelzner     |
 | Suzy Slane      | 03:06:24    | F35-39   | Lauren Matthews   |
 +-----------------+-------------+----------+-------------------*/
```

## `LAST_VALUE`

```googlesql
LAST_VALUE (value_expression [{RESPECT | IGNORE} NULLS])
OVER over_clause

over_clause:
  { named_window | ( [ window_specification ] ) }

window_specification:
  [ named_window ]
  [ PARTITION BY partition_expression [, ...] ]
  ORDER BY expression [ { ASC | DESC }  ] [, ...]
  [ window_frame_clause ]

```

**Description**

Returns the value of the `value_expression` for the last row in the current
window frame.

This function includes `NULL` values in the calculation unless `IGNORE NULLS` is
present. If `IGNORE NULLS` is present, the function excludes `NULL` values from
the calculation.

To learn more about the `OVER` clause and how to use it, see
[Window function calls][window-function-calls].

<!-- mdlint off(WHITESPACE_LINE_LENGTH) -->

[window-function-calls]: https://github.com/google/googlesql/blob/master/docs/window-function-calls.md

<!-- mdlint on -->

**Supported Argument Types**

`value_expression` can be any data type that an expression can return.

**Return Data Type**

Same type as `value_expression`.

**Examples**

The following example computes the slowest time for each division.

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
  UNION ALL SELECT 'Meghan Lederer', TIMESTAMP '2016-10-18 3:07:41', 'F30-34'
  UNION ALL SELECT 'Carly Forte', TIMESTAMP '2016-10-18 3:08:58', 'F25-29'
  UNION ALL SELECT 'Lauren Reasoner', TIMESTAMP '2016-10-18 3:10:14', 'F30-34')
SELECT name,
  FORMAT_TIMESTAMP('%X', finish_time) AS finish_time,
  division,
  FORMAT_TIMESTAMP('%X', slowest_time) AS slowest_time,
  TIMESTAMP_DIFF(slowest_time, finish_time, SECOND) AS delta_in_seconds
FROM (
  SELECT name,
  finish_time,
  division,
  LAST_VALUE(finish_time)
    OVER (PARTITION BY division ORDER BY finish_time ASC
    ROWS BETWEEN UNBOUNDED PRECEDING AND UNBOUNDED FOLLOWING) AS slowest_time
  FROM finishers);

/*-----------------+-------------+----------+--------------+------------------+
 | name            | finish_time | division | slowest_time | delta_in_seconds |
 +-----------------+-------------+----------+--------------+------------------+
 | Carly Forte     | 03:08:58    | F25-29   | 03:08:58     | 0                |
 | Sophia Liu      | 02:51:45    | F30-34   | 03:10:14     | 1109             |
 | Nikki Leith     | 02:59:01    | F30-34   | 03:10:14     | 673              |
 | Jen Edwards     | 03:06:36    | F30-34   | 03:10:14     | 218              |
 | Meghan Lederer  | 03:07:41    | F30-34   | 03:10:14     | 153              |
 | Lauren Reasoner | 03:10:14    | F30-34   | 03:10:14     | 0                |
 | Lisa Stelzner   | 02:54:11    | F35-39   | 03:06:24     | 733              |
 | Lauren Matthews | 03:01:17    | F35-39   | 03:06:24     | 307              |
 | Desiree Berry   | 03:05:42    | F35-39   | 03:06:24     | 42               |
 | Suzy Slane      | 03:06:24    | F35-39   | 03:06:24     | 0                |
 +-----------------+-------------+----------+--------------+------------------*/
```

## `LEAD`

```googlesql
LEAD (value_expression[, offset [, default_expression]])
OVER over_clause

over_clause:
  { named_window | ( [ window_specification ] ) }

window_specification:
  [ named_window ]
  [ PARTITION BY partition_expression [, ...] ]
  ORDER BY expression [ { ASC | DESC }  ] [, ...]

```

**Description**

Returns the value of the `value_expression` on a subsequent row. Changing the
`offset` value changes which subsequent row is returned; the default value is
`1`, indicating the next row in the window frame. An error occurs if `offset` is
NULL or a negative value.

The optional `default_expression` is used if there isn't a row in the window
frame at the specified offset. This expression must be a constant expression and
its type must be implicitly coercible to the type of `value_expression`. If left
unspecified, `default_expression` defaults to NULL.

To learn more about the `OVER` clause and how to use it, see
[Window function calls][window-function-calls].

<!-- mdlint off(WHITESPACE_LINE_LENGTH) -->

[window-function-calls]: https://github.com/google/googlesql/blob/master/docs/window-function-calls.md

<!-- mdlint on -->

**Supported Argument Types**

+ `value_expression` can be any data type that can be returned from an
  expression.
+ `offset` must be a non-negative integer literal or parameter.
+ `default_expression` must be compatible with the value expression type.

**Return Data Type**

Same type as `value_expression`.

**Examples**

The following example illustrates a basic use of the `LEAD` function.

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
  UNION ALL SELECT 'Meghan Lederer', TIMESTAMP '2016-10-18 3:07:41', 'F30-34'
  UNION ALL SELECT 'Carly Forte', TIMESTAMP '2016-10-18 3:08:58', 'F25-29'
  UNION ALL SELECT 'Lauren Reasoner', TIMESTAMP '2016-10-18 3:10:14', 'F30-34')
SELECT name,
  finish_time,
  division,
  LEAD(name)
    OVER (PARTITION BY division ORDER BY finish_time ASC) AS followed_by
FROM finishers;

/*-----------------+-------------+----------+-----------------+
 | name            | finish_time | division | followed_by     |
 +-----------------+-------------+----------+-----------------+
 | Carly Forte     | 03:08:58    | F25-29   | NULL            |
 | Sophia Liu      | 02:51:45    | F30-34   | Nikki Leith     |
 | Nikki Leith     | 02:59:01    | F30-34   | Jen Edwards     |
 | Jen Edwards     | 03:06:36    | F30-34   | Meghan Lederer  |
 | Meghan Lederer  | 03:07:41    | F30-34   | Lauren Reasoner |
 | Lauren Reasoner | 03:10:14    | F30-34   | NULL            |
 | Lisa Stelzner   | 02:54:11    | F35-39   | Lauren Matthews |
 | Lauren Matthews | 03:01:17    | F35-39   | Desiree Berry   |
 | Desiree Berry   | 03:05:42    | F35-39   | Suzy Slane      |
 | Suzy Slane      | 03:06:24    | F35-39   | NULL            |
 +-----------------+-------------+----------+-----------------*/
```

This next example uses the optional `offset` parameter.

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
  UNION ALL SELECT 'Meghan Lederer', TIMESTAMP '2016-10-18 3:07:41', 'F30-34'
  UNION ALL SELECT 'Carly Forte', TIMESTAMP '2016-10-18 3:08:58', 'F25-29'
  UNION ALL SELECT 'Lauren Reasoner', TIMESTAMP '2016-10-18 3:10:14', 'F30-34')
SELECT name,
  finish_time,
  division,
  LEAD(name, 2)
    OVER (PARTITION BY division ORDER BY finish_time ASC) AS two_runners_back
FROM finishers;

/*-----------------+-------------+----------+------------------+
 | name            | finish_time | division | two_runners_back |
 +-----------------+-------------+----------+------------------+
 | Carly Forte     | 03:08:58    | F25-29   | NULL             |
 | Sophia Liu      | 02:51:45    | F30-34   | Jen Edwards      |
 | Nikki Leith     | 02:59:01    | F30-34   | Meghan Lederer   |
 | Jen Edwards     | 03:06:36    | F30-34   | Lauren Reasoner  |
 | Meghan Lederer  | 03:07:41    | F30-34   | NULL             |
 | Lauren Reasoner | 03:10:14    | F30-34   | NULL             |
 | Lisa Stelzner   | 02:54:11    | F35-39   | Desiree Berry    |
 | Lauren Matthews | 03:01:17    | F35-39   | Suzy Slane       |
 | Desiree Berry   | 03:05:42    | F35-39   | NULL             |
 | Suzy Slane      | 03:06:24    | F35-39   | NULL             |
 +-----------------+-------------+----------+------------------*/
```

The following example replaces NULL values with a default value.

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
  UNION ALL SELECT 'Meghan Lederer', TIMESTAMP '2016-10-18 3:07:41', 'F30-34'
  UNION ALL SELECT 'Carly Forte', TIMESTAMP '2016-10-18 3:08:58', 'F25-29'
  UNION ALL SELECT 'Lauren Reasoner', TIMESTAMP '2016-10-18 3:10:14', 'F30-34')
SELECT name,
  finish_time,
  division,
  LEAD(name, 2, 'Nobody')
    OVER (PARTITION BY division ORDER BY finish_time ASC) AS two_runners_back
FROM finishers;

/*-----------------+-------------+----------+------------------+
 | name            | finish_time | division | two_runners_back |
 +-----------------+-------------+----------+------------------+
 | Carly Forte     | 03:08:58    | F25-29   | Nobody           |
 | Sophia Liu      | 02:51:45    | F30-34   | Jen Edwards      |
 | Nikki Leith     | 02:59:01    | F30-34   | Meghan Lederer   |
 | Jen Edwards     | 03:06:36    | F30-34   | Lauren Reasoner  |
 | Meghan Lederer  | 03:07:41    | F30-34   | Nobody           |
 | Lauren Reasoner | 03:10:14    | F30-34   | Nobody           |
 | Lisa Stelzner   | 02:54:11    | F35-39   | Desiree Berry    |
 | Lauren Matthews | 03:01:17    | F35-39   | Suzy Slane       |
 | Desiree Berry   | 03:05:42    | F35-39   | Nobody           |
 | Suzy Slane      | 03:06:24    | F35-39   | Nobody           |
 +-----------------+-------------+----------+------------------*/
```

## `NTH_VALUE`

```googlesql
NTH_VALUE (value_expression, constant_integer_expression [{RESPECT | IGNORE} NULLS])
OVER over_clause

over_clause:
  { named_window | ( [ window_specification ] ) }

window_specification:
  [ named_window ]
  [ PARTITION BY partition_expression [, ...] ]
  ORDER BY expression [ { ASC | DESC }  ] [, ...]
  [ window_frame_clause ]

```

**Description**

Returns the value of `value_expression` at the Nth row of the current window
frame, where Nth is defined by `constant_integer_expression`. Returns NULL if
there is no such row.

This function includes `NULL` values in the calculation unless `IGNORE NULLS` is
present. If `IGNORE NULLS` is present, the function excludes `NULL` values from
the calculation.

To learn more about the `OVER` clause and how to use it, see
[Window function calls][window-function-calls].

<!-- mdlint off(WHITESPACE_LINE_LENGTH) -->

[window-function-calls]: https://github.com/google/googlesql/blob/master/docs/window-function-calls.md

<!-- mdlint on -->

**Supported Argument Types**

+ `value_expression` can be any data type that can be returned from an
  expression.
+ `constant_integer_expression` can be any constant expression that returns an
  integer.

**Return Data Type**

Same type as `value_expression`.

**Examples**

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
  UNION ALL SELECT 'Meghan Lederer', TIMESTAMP '2016-10-18 3:07:41', 'F30-34'
  UNION ALL SELECT 'Carly Forte', TIMESTAMP '2016-10-18 3:08:58', 'F25-29'
  UNION ALL SELECT 'Lauren Reasoner', TIMESTAMP '2016-10-18 3:10:14', 'F30-34')
SELECT name,
  FORMAT_TIMESTAMP('%X', finish_time) AS finish_time,
  division,
  FORMAT_TIMESTAMP('%X', fastest_time) AS fastest_time,
  FORMAT_TIMESTAMP('%X', second_fastest) AS second_fastest
FROM (
  SELECT name,
  finish_time,
  division,finishers,
  FIRST_VALUE(finish_time)
    OVER w1 AS fastest_time,
  NTH_VALUE(finish_time, 2)
    OVER w1 as second_fastest
  FROM finishers
  WINDOW w1 AS (
    PARTITION BY division ORDER BY finish_time ASC
    ROWS BETWEEN UNBOUNDED PRECEDING AND UNBOUNDED FOLLOWING));

/*-----------------+-------------+----------+--------------+----------------+
 | name            | finish_time | division | fastest_time | second_fastest |
 +-----------------+-------------+----------+--------------+----------------+
 | Carly Forte     | 03:08:58    | F25-29   | 03:08:58     | NULL           |
 | Sophia Liu      | 02:51:45    | F30-34   | 02:51:45     | 02:59:01       |
 | Nikki Leith     | 02:59:01    | F30-34   | 02:51:45     | 02:59:01       |
 | Jen Edwards     | 03:06:36    | F30-34   | 02:51:45     | 02:59:01       |
 | Meghan Lederer  | 03:07:41    | F30-34   | 02:51:45     | 02:59:01       |
 | Lauren Reasoner | 03:10:14    | F30-34   | 02:51:45     | 02:59:01       |
 | Lisa Stelzner   | 02:54:11    | F35-39   | 02:54:11     | 03:01:17       |
 | Lauren Matthews | 03:01:17    | F35-39   | 02:54:11     | 03:01:17       |
 | Desiree Berry   | 03:05:42    | F35-39   | 02:54:11     | 03:01:17       |
 | Suzy Slane      | 03:06:24    | F35-39   | 02:54:11     | 03:01:17       |
 +-----------------+-------------+----------+--------------+----------------*/
```

## `PERCENTILE_CONT`

```googlesql
PERCENTILE_CONT (value_expression, percentile [{RESPECT | IGNORE} NULLS])
OVER over_clause

over_clause:
  { named_window | ( [ window_specification ] ) }

window_specification:
  [ named_window ]
  [ PARTITION BY partition_expression [, ...] ]

```

**Description**

Computes the specified percentile value for the value_expression, with linear
interpolation.

This function ignores NULL
values if
`RESPECT NULLS` is absent. If `RESPECT NULLS` is present:

+ Interpolation between two `NULL` values returns `NULL`.
+ Interpolation between a `NULL` value and a non-`NULL` value returns the
  non-`NULL` value.

To learn more about the `OVER` clause and how to use it, see
[Window function calls][window-function-calls].

<!-- mdlint off(WHITESPACE_LINE_LENGTH) -->

[window-function-calls]: https://github.com/google/googlesql/blob/master/docs/window-function-calls.md

<!-- mdlint on -->

`PERCENTILE_CONT` can be used with differential privacy. To learn more, see
[Differentially private aggregate functions][dp-functions].

**Supported Argument Types**

+ `value_expression` and `percentile` must have one of the following types:
   + `NUMERIC`
   + `BIGNUMERIC`
   + `DOUBLE`
+ `percentile` must be a literal in the range `[0, 1]`.

**Return Data Type**

The return data type is determined by the argument types with the following
table.
<table>

<thead>
<tr>
<th>INPUT</th><th><code>NUMERIC</code></th><th><code>BIGNUMERIC</code></th><th><code>DOUBLE</code></th>
</tr>
</thead>
<tbody>
<tr><th><code>NUMERIC</code></th><td style="vertical-align:middle"><code>NUMERIC</code></td><td style="vertical-align:middle"><code>BIGNUMERIC</code></td><td style="vertical-align:middle"><code>DOUBLE</code></td></tr>
<tr><th><code>BIGNUMERIC</code></th><td style="vertical-align:middle"><code>BIGNUMERIC</code></td><td style="vertical-align:middle"><code>BIGNUMERIC</code></td><td style="vertical-align:middle"><code>DOUBLE</code></td></tr>
<tr><th><code>DOUBLE</code></th><td style="vertical-align:middle"><code>DOUBLE</code></td><td style="vertical-align:middle"><code>DOUBLE</code></td><td style="vertical-align:middle"><code>DOUBLE</code></td></tr>
</tbody>

</table>

**Examples**

The following example computes the value for some percentiles from a column of
values while ignoring nulls.

```googlesql
SELECT
  PERCENTILE_CONT(x, 0) OVER() AS min,
  PERCENTILE_CONT(x, 0.01) OVER() AS percentile1,
  PERCENTILE_CONT(x, 0.5) OVER() AS median,
  PERCENTILE_CONT(x, 0.9) OVER() AS percentile90,
  PERCENTILE_CONT(x, 1) OVER() AS max
FROM UNNEST([0, 3, NULL, 1, 2]) AS x LIMIT 1;

 /*-----+-------------+--------+--------------+-----+
  | min | percentile1 | median | percentile90 | max |
  +-----+-------------+--------+--------------+-----+
  | 0   | 0.03        | 1.5    | 2.7          | 3   |
  +-----+-------------+--------+--------------+-----*/
```

The following example computes the value for some percentiles from a column of
values while respecting nulls.

```googlesql
SELECT
  PERCENTILE_CONT(x, 0 RESPECT NULLS) OVER() AS min,
  PERCENTILE_CONT(x, 0.01 RESPECT NULLS) OVER() AS percentile1,
  PERCENTILE_CONT(x, 0.5 RESPECT NULLS) OVER() AS median,
  PERCENTILE_CONT(x, 0.9 RESPECT NULLS) OVER() AS percentile90,
  PERCENTILE_CONT(x, 1 RESPECT NULLS) OVER() AS max
FROM UNNEST([0, 3, NULL, 1, 2]) AS x LIMIT 1;

/*------+-------------+--------+--------------+-----+
 | min  | percentile1 | median | percentile90 | max |
 +------+-------------+--------+--------------+-----+
 | NULL | 0           | 1      | 2.6          | 3   |
 +------+-------------+--------+--------------+-----*/
```

[dp-functions]: https://github.com/google/googlesql/blob/master/docs/aggregate-dp-functions.md

## `PERCENTILE_DISC`

```googlesql
PERCENTILE_DISC (value_expression, percentile [{RESPECT | IGNORE} NULLS])
OVER over_clause

over_clause:
  { named_window | ( [ window_specification ] ) }

window_specification:
  [ named_window ]
  [ PARTITION BY partition_expression [, ...] ]

```

**Description**

Computes the specified percentile value for a discrete `value_expression`. The
returned value is the first sorted value of `value_expression` with cumulative
distribution greater than or equal to the given `percentile` value.

This function ignores `NULL`
values unless
`RESPECT NULLS` is present.

To learn more about the `OVER` clause and how to use it, see
[Window function calls][window-function-calls].

<!-- mdlint off(WHITESPACE_LINE_LENGTH) -->

[window-function-calls]: https://github.com/google/googlesql/blob/master/docs/window-function-calls.md

<!-- mdlint on -->

**Supported Argument Types**

+ `value_expression` can be any orderable type.
+ `percentile` must be a literal in the range `[0, 1]`, with one of the
  following types:
   + `NUMERIC`
   + `BIGNUMERIC`
   + `DOUBLE`

**Return Data Type**

Same type as `value_expression`.

**Examples**

The following example computes the value for some percentiles from a column of
values while ignoring nulls.

```googlesql
SELECT
  x,
  PERCENTILE_DISC(x, 0) OVER() AS min,
  PERCENTILE_DISC(x, 0.5) OVER() AS median,
  PERCENTILE_DISC(x, 1) OVER() AS max
FROM UNNEST(['c', NULL, 'b', 'a']) AS x;

/*------+-----+--------+-----+
 | x    | min | median | max |
 +------+-----+--------+-----+
 | c    | a   | b      | c   |
 | NULL | a   | b      | c   |
 | b    | a   | b      | c   |
 | a    | a   | b      | c   |
 +------+-----+--------+-----*/
```

The following example computes the value for some percentiles from a column of
values while respecting nulls.

```googlesql
SELECT
  x,
  PERCENTILE_DISC(x, 0 RESPECT NULLS) OVER() AS min,
  PERCENTILE_DISC(x, 0.5 RESPECT NULLS) OVER() AS median,
  PERCENTILE_DISC(x, 1 RESPECT NULLS) OVER() AS max
FROM UNNEST(['c', NULL, 'b', 'a']) AS x;

/*------+------+--------+-----+
 | x    | min  | median | max |
 +------+------+--------+-----+
 | c    | NULL | a      | c   |
 | NULL | NULL | a      | c   |
 | b    | NULL | a      | c   |
 | a    | NULL | a      | c   |
 +------+------+--------+-----*/

```

[sketches]: https://github.com/google/googlesql/blob/master/docs/sketches.md

[window-function-calls]: https://github.com/google/googlesql/blob/master/docs/window-function-calls.md

