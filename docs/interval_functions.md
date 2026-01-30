

<!-- mdlint off(WHITESPACE_LINE_LENGTH) -->

# Interval functions

GoogleSQL supports the following interval functions.

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
  <td><a href="https://github.com/google/googlesql/blob/master/docs/interval_functions.md#extract"><code>EXTRACT</code></a>
</td>
  <td>
    Extracts part of an <code>INTERVAL</code> value.
  </td>
</tr>

<tr>
  <td><a href="https://github.com/google/googlesql/blob/master/docs/interval_functions.md#justify_days"><code>JUSTIFY_DAYS</code></a>
</td>
  <td>
    Normalizes the day part of an <code>INTERVAL</code> value.
  </td>
</tr>

<tr>
  <td><a href="https://github.com/google/googlesql/blob/master/docs/interval_functions.md#justify_hours"><code>JUSTIFY_HOURS</code></a>
</td>
  <td>
    Normalizes the time part of an <code>INTERVAL</code> value.
  </td>
</tr>

<tr>
  <td><a href="https://github.com/google/googlesql/blob/master/docs/interval_functions.md#justify_interval"><code>JUSTIFY_INTERVAL</code></a>
</td>
  <td>
    Normalizes the day and time parts of an <code>INTERVAL</code> value.
  </td>
</tr>

<tr>
  <td><a href="https://github.com/google/googlesql/blob/master/docs/interval_functions.md#make_interval"><code>MAKE_INTERVAL</code></a>
</td>
  <td>
    Constructs an <code>INTERVAL</code> value.
  </td>
</tr>

  </tbody>
</table>

## `EXTRACT`

```googlesql
EXTRACT(part FROM interval_expression)
```

**Description**

Returns the value corresponding to the specified date part. The `part` must be
one of `YEAR`, `MONTH`, `DAY`, `HOUR`, `MINUTE`, `SECOND`, `MILLISECOND` or
`MICROSECOND`.

**Return Data Type**

`INTERVAL`

**Examples**

In the following example, different parts of two intervals are extracted.

```googlesql
SELECT
  EXTRACT(YEAR FROM i) AS year,
  EXTRACT(MONTH FROM i) AS month,
  EXTRACT(DAY FROM i) AS day,
  EXTRACT(HOUR FROM i) AS hour,
  EXTRACT(MINUTE FROM i) AS minute,
  EXTRACT(SECOND FROM i) AS second,
  EXTRACT(MILLISECOND FROM i) AS milli,
  EXTRACT(MICROSECOND FROM i) AS micro
FROM
  UNNEST([INTERVAL '1-2 3 4:5:6.789999' YEAR TO SECOND,
          INTERVAL '0-13 370 48:61:61' YEAR TO SECOND]) AS i

/*------+-------+-----+------+--------+--------+-------+--------+
 | year | month | day | hour | minute | second | milli | micro  |
 +------+-------+-----+------+--------+--------+-------+--------+
 | 1    | 2     | 3   | 4    | 5      | 6      | 789   | 789999 |
 | 1    | 1     | 370 | 49   | 2      | 1      | 0     | 0      |
 +------+-------+-----+------+--------+--------+-------+--------*/
```

When a negative sign precedes the time part in an interval, the negative sign
distributes over the hours, minutes, and seconds. For example:

```googlesql
SELECT
  EXTRACT(HOUR FROM i) AS hour,
  EXTRACT(MINUTE FROM i) AS minute
FROM
  UNNEST([INTERVAL '10 -12:30' DAY TO MINUTE]) AS i

/*------+--------+
 | hour | minute |
 +------+--------+
 | -12  | -30    |
 +------+--------*/
```

When a negative sign precedes the year and month part in an interval, the
negative sign distributes over the years and months. For example:

```googlesql
SELECT
  EXTRACT(YEAR FROM i) AS year,
  EXTRACT(MONTH FROM i) AS month
FROM
  UNNEST([INTERVAL '-22-6 10 -12:30' YEAR TO MINUTE]) AS i

/*------+--------+
 | year | month  |
 +------+--------+
 | -22  | -6     |
 +------+--------*/
```

## `JUSTIFY_DAYS`

```googlesql
JUSTIFY_DAYS(interval_expression)
```

**Description**

Normalizes the day part of the interval to the range from -29 to 29 by
incrementing/decrementing the month or year part of the interval.

**Return Data Type**

`INTERVAL`

**Example**

```googlesql
SELECT
  JUSTIFY_DAYS(INTERVAL 29 DAY) AS i1,
  JUSTIFY_DAYS(INTERVAL -30 DAY) AS i2,
  JUSTIFY_DAYS(INTERVAL 31 DAY) AS i3,
  JUSTIFY_DAYS(INTERVAL -65 DAY) AS i4,
  JUSTIFY_DAYS(INTERVAL 370 DAY) AS i5

/*--------------+--------------+-------------+---------------+--------------+
 | i1           | i2           | i3          | i4            | i5           |
 +--------------+--------------+-------------+---------------+--------------+
 | 0-0 29 0:0:0 | -0-1 0 0:0:0 | 0-1 1 0:0:0 | -0-2 -5 0:0:0 | 1-0 10 0:0:0 |
 +--------------+--------------+-------------+---------------+--------------*/
```

## `JUSTIFY_HOURS`

```googlesql
JUSTIFY_HOURS(interval_expression)
```

**Description**

Normalizes the time part of the interval to the range from -23:59:59.999999 to
23:59:59.999999 by incrementing/decrementing the day part of the interval.

**Return Data Type**

`INTERVAL`

**Example**

```googlesql
SELECT
  JUSTIFY_HOURS(INTERVAL 23 HOUR) AS i1,
  JUSTIFY_HOURS(INTERVAL -24 HOUR) AS i2,
  JUSTIFY_HOURS(INTERVAL 47 HOUR) AS i3,
  JUSTIFY_HOURS(INTERVAL -12345 MINUTE) AS i4

/*--------------+--------------+--------------+-----------------+
 | i1           | i2           | i3           | i4              |
 +--------------+--------------+--------------+-----------------+
 | 0-0 0 23:0:0 | 0-0 -1 0:0:0 | 0-0 1 23:0:0 | 0-0 -8 -13:45:0 |
 +--------------+--------------+--------------+-----------------*/
```

## `JUSTIFY_INTERVAL`

```googlesql
JUSTIFY_INTERVAL(interval_expression)
```

**Description**

Normalizes the days and time parts of the interval.

**Return Data Type**

`INTERVAL`

**Example**

```googlesql
SELECT JUSTIFY_INTERVAL(INTERVAL '29 49:00:00' DAY TO SECOND) AS i

/*-------------+
 | i           |
 +-------------+
 | 0-1 1 1:0:0 |
 +-------------*/
```

## `MAKE_INTERVAL`

```googlesql
MAKE_INTERVAL(
  [ [ year => ] value ]
  [, [ month => ] value ]
  [, [ day => ] value ]
  [, [ hour => ] value ]
  [, [ minute => ] value ]
  [, [ second => ] value ]
)
```

**Description**

Constructs an [`INTERVAL`][interval-type] object using `INT64` values
representing the year, month, day, hour, minute, and second. All arguments are
optional, `0` by default, and can be [named arguments][named-arguments].

**Return Data Type**

`INTERVAL`

**Example**

```googlesql
SELECT
  MAKE_INTERVAL(1, 6, 15) AS i1,
  MAKE_INTERVAL(hour => 10, second => 20) AS i2,
  MAKE_INTERVAL(1, minute => 5, day => 2) AS i3

/*--------------+---------------+-------------+
 | i1           | i2            | i3          |
 +--------------+---------------+-------------+
 | 1-6 15 0:0:0 | 0-0 0 10:0:20 | 1-0 2 0:5:0 |
 +--------------+---------------+-------------*/
```

[interval-type]: https://github.com/google/googlesql/blob/master/docs/data-types.md#interval_type

[named-arguments]: https://github.com/google/googlesql/blob/master/docs/functions-reference.md#named_arguments

