

<!-- mdlint off(WHITESPACE_LINE_LENGTH) -->

# Time functions

GoogleSQL supports the following time functions.

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
  <td><a href="https://github.com/google/googlesql/blob/master/docs/time_functions.md#current_time"><code>CURRENT_TIME</code></a>
</td>
  <td>
    Returns the current time as a <code>TIME</code> value.
  </td>
</tr>

<tr>
  <td><a href="https://github.com/google/googlesql/blob/master/docs/time_functions.md#extract"><code>EXTRACT</code></a>
</td>
  <td>
    Extracts part of a <code>TIME</code> value.
  </td>
</tr>

<tr>
  <td><a href="https://github.com/google/googlesql/blob/master/docs/time_functions.md#format_time"><code>FORMAT_TIME</code></a>
</td>
  <td>
    Formats a <code>TIME</code> value according to the specified format string.
  </td>
</tr>

<tr>
  <td><a href="https://github.com/google/googlesql/blob/master/docs/time_functions.md#parse_time"><code>PARSE_TIME</code></a>
</td>
  <td>
    Converts a <code>STRING</code> value to a <code>TIME</code> value.
    
  </td>
</tr>

<tr>
  <td><a href="https://github.com/google/googlesql/blob/master/docs/time_functions.md#time"><code>TIME</code></a>
</td>
  <td>
    Constructs a <code>TIME</code> value.
  </td>
</tr>

<tr>
  <td><a href="https://github.com/google/googlesql/blob/master/docs/time_functions.md#time_add"><code>TIME_ADD</code></a>
</td>
  <td>
    Adds a specified time interval to a <code>TIME</code> value.
  </td>
</tr>

<tr>
  <td><a href="https://github.com/google/googlesql/blob/master/docs/time_functions.md#time_diff"><code>TIME_DIFF</code></a>
</td>
  <td>
    Gets the number of unit boundaries between two <code>TIME</code> values at
    a particular time granularity.
  </td>
</tr>

<tr>
  <td><a href="https://github.com/google/googlesql/blob/master/docs/time_functions.md#time_sub"><code>TIME_SUB</code></a>
</td>
  <td>
    Subtracts a specified time interval from a <code>TIME</code> value.
  </td>
</tr>

<tr>
  <td><a href="https://github.com/google/googlesql/blob/master/docs/time_functions.md#time_trunc"><code>TIME_TRUNC</code></a>
</td>
  <td>
    Truncates a <code>TIME</code> value at a particular granularity.
  </td>
</tr>

  </tbody>
</table>

## `CURRENT_TIME`

```googlesql
CURRENT_TIME([time_zone])
```

```googlesql
CURRENT_TIME
```

**Description**

Returns the current time as a `TIME` object. Parentheses are optional when
called with no arguments.

This function supports an optional `time_zone` parameter.
See [Time zone definitions][time-link-to-timezone-definitions] for information
on how to specify a time zone.

The current time value is set at the start of the query statement that contains
this function. All invocations of `CURRENT_TIME()` within a query statement
yield the same value.

**Return Data Type**

`TIME`

**Example**

```googlesql
SELECT CURRENT_TIME() as now;

/*----------------------------+
 | now                        |
 +----------------------------+
 | 15:31:38.776361            |
 +----------------------------*/
```

[time-link-to-timezone-definitions]: https://github.com/google/googlesql/blob/master/docs/timestamp_functions.md#timezone_definitions

## `EXTRACT`

```googlesql
EXTRACT(part FROM time_expression)
```

**Description**

Returns a value that corresponds to the specified `part` from
a supplied `time_expression`.

Allowed `part` values are:

+ `NANOSECOND`
+ `MICROSECOND`
+ `MILLISECOND`
+ `SECOND`
+ `MINUTE`
+ `HOUR`

Returned values truncate lower order time periods. For example, when extracting
seconds, `EXTRACT` truncates the millisecond and microsecond values.

**Return Data Type**

`INT64`

**Example**

In the following example, `EXTRACT` returns a value corresponding to the `HOUR`
time part.

```googlesql
SELECT EXTRACT(HOUR FROM TIME "15:30:00") as hour;

/*------------------+
 | hour             |
 +------------------+
 | 15               |
 +------------------*/
```

## `FORMAT_TIME`

```googlesql
FORMAT_TIME(format_string, time_expr)
```

**Description**

Formats a `TIME` value according to the specified format string.

**Definitions**

+   `format_string`: A `STRING` value that contains the
    [format elements][time-format-elements] to use with `time_expr`.
+   `time_expr`: A `TIME` value that represents the time to format.

**Return Data Type**

`STRING`

**Example**

```googlesql
SELECT FORMAT_TIME("%R", TIME "15:30:00") as formatted_time;

/*----------------+
 | formatted_time |
 +----------------+
 | 15:30          |
 +----------------*/
```

[time-format-elements]: https://github.com/google/googlesql/blob/master/docs/format-elements.md#format_elements_date_time

## `PARSE_TIME`

```googlesql
PARSE_TIME(format_string, time_string)
```

**Description**

Converts a `STRING` value to a `TIME` value.

**Definitions**

+   `format_string`: A `STRING` value that contains the
    [format elements][time-format-elements] to use with `time_string`.
+   `time_string`: A `STRING` value that represents the time to parse.

**Details**

Each element in `time_string` must have a corresponding element in
`format_string`. The location of each element in `format_string` must match the
location of each element in `time_string`.

```googlesql
-- This works because elements on both sides match.
SELECT PARSE_TIME("%I:%M:%S", "07:30:00");

-- This produces an error because the seconds element is in different locations.
SELECT PARSE_TIME("%S:%I:%M", "07:30:00");

-- This produces an error because one of the seconds elements is missing.
SELECT PARSE_TIME("%I:%M", "07:30:00");

-- This works because %T can find all matching elements in time_string.
SELECT PARSE_TIME("%T", "07:30:00");
```

The following additional considerations apply when using the `PARSE_TIME`
function:

+ Unspecified fields. Any unspecified field is initialized from
  `00:00:00.0`. For instance, if `seconds` is unspecified then it
  defaults to `00`, and so on.
+ Whitespace. One or more consecutive white spaces in the format string
  matches zero or more consecutive white spaces in the `TIME` string. In
  addition, leading and trailing white spaces in the `TIME` string are always
  allowed, even if they aren't in the format string.
+ Format precedence. When two (or more) format elements have overlapping
  information, the last one generally overrides any earlier ones.
+ Format divergence. `%p` can be used with `am`, `AM`, `pm`, and `PM`.

**Return Data Type**

`TIME`

**Example**

```googlesql
SELECT PARSE_TIME("%H", "15") as parsed_time;

/*-------------+
 | parsed_time |
 +-------------+
 | 15:00:00    |
 +-------------*/
```

```googlesql
SELECT PARSE_TIME('%I:%M:%S %p', '2:23:38 pm') AS parsed_time;

/*-------------+
 | parsed_time |
 +-------------+
 | 14:23:38    |
 +-------------*/
```

[time-format-elements]: https://github.com/google/googlesql/blob/master/docs/format-elements.md#format_elements_date_time

## `TIME`

```googlesql
1. TIME(hour, minute, second)
2. TIME(timestamp, [time_zone])
3. TIME(datetime)
```

**Description**

1. Constructs a `TIME` object using `INT64`
   values representing the hour, minute, and second.
2. Constructs a `TIME` object using a `TIMESTAMP` object. It supports an
   optional
   parameter to [specify a time zone][time-link-to-timezone-definitions]. If no
   time zone is specified, the default time zone, which is implementation defined, is
   used.
3. Constructs a `TIME` object using a
   `DATETIME` object.

**Return Data Type**

`TIME`

**Example**

```googlesql
SELECT
  TIME(15, 30, 00) as time_hms,
  TIME(TIMESTAMP "2008-12-25 15:30:00+08", "America/Los_Angeles") as time_tstz;

/*----------+-----------+
 | time_hms | time_tstz |
 +----------+-----------+
 | 15:30:00 | 23:30:00  |
 +----------+-----------*/
```

```googlesql
SELECT TIME(DATETIME "2008-12-25 15:30:00.000000") AS time_dt;

/*----------+
 | time_dt  |
 +----------+
 | 15:30:00 |
 +----------*/
```

[time-link-to-timezone-definitions]: https://github.com/google/googlesql/blob/master/docs/timestamp_functions.md#timezone_definitions

## `TIME_ADD`

```googlesql
TIME_ADD(time_expression, INTERVAL int64_expression part)
```

**Description**

Adds `int64_expression` units of `part` to the `TIME` object.

`TIME_ADD` supports the following values for `part`:

+ `NANOSECOND`
+ `MICROSECOND`
+ `MILLISECOND`
+ `SECOND`
+ `MINUTE`
+ `HOUR`

This function automatically adjusts when values fall outside of the 00:00:00 to
24:00:00 boundary. For example, if you add an hour to `23:30:00`, the returned
value is `00:30:00`.

**Return Data Types**

`TIME`

**Example**

```googlesql
SELECT
  TIME "15:30:00" as original_time,
  TIME_ADD(TIME "15:30:00", INTERVAL 10 MINUTE) as later;

/*-----------------------------+------------------------+
 | original_time               | later                  |
 +-----------------------------+------------------------+
 | 15:30:00                    | 15:40:00               |
 +-----------------------------+------------------------*/
```

## `TIME_DIFF`

```googlesql
TIME_DIFF(end_time, start_time, granularity)
```

**Description**

Gets the number of unit boundaries between two `TIME` values (`end_time` -
`start_time`) at a particular time granularity.

**Definitions**

+   `start_time`: The starting `TIME` value.
+   `end_time`: The ending `TIME` value.
+   `granularity`: The time part that represents the granularity. If
    you passed in `TIME` values for the first arguments, `granularity` can
    be:

    
    + `NANOSECOND`
    + `MICROSECOND`
    + `MILLISECOND`
    + `SECOND`
    + `MINUTE`
    + `HOUR`

**Details**

If `end_time` is earlier than `start_time`, the output is negative.
Produces an error if the computation overflows, such as if the difference
in nanoseconds
between the two `TIME` values overflows.

Note: The behavior of the this function follows the type of arguments passed in.
For example, `TIME_DIFF(TIMESTAMP, TIMESTAMP, PART)`
behaves like `TIMESTAMP_DIFF(TIMESTAMP, TIMESTAMP, PART)`.

**Return Data Type**

`INT64`

**Example**

```googlesql
SELECT
  TIME "15:30:00" as first_time,
  TIME "14:35:00" as second_time,
  TIME_DIFF(TIME "15:30:00", TIME "14:35:00", MINUTE) as difference;

/*----------------------------+------------------------+------------------------+
 | first_time                 | second_time            | difference             |
 +----------------------------+------------------------+------------------------+
 | 15:30:00                   | 14:35:00               | 55                     |
 +----------------------------+------------------------+------------------------*/
```

## `TIME_SUB`

```googlesql
TIME_SUB(time_expression, INTERVAL int64_expression part)
```

**Description**

Subtracts `int64_expression` units of `part` from the `TIME` object.

`TIME_SUB` supports the following values for `part`:

+ `NANOSECOND`
+ `MICROSECOND`
+ `MILLISECOND`
+ `SECOND`
+ `MINUTE`
+ `HOUR`

This function automatically adjusts when values fall outside of the 00:00:00 to
24:00:00 boundary. For example, if you subtract an hour from `00:30:00`, the
returned value is `23:30:00`.

**Return Data Type**

`TIME`

**Example**

```googlesql
SELECT
  TIME "15:30:00" as original_date,
  TIME_SUB(TIME "15:30:00", INTERVAL 10 MINUTE) as earlier;

/*-----------------------------+------------------------+
 | original_date               | earlier                |
 +-----------------------------+------------------------+
 | 15:30:00                    | 15:20:00               |
 +-----------------------------+------------------------*/
```

## `TIME_TRUNC`

```googlesql
TIME_TRUNC(time_value, time_granularity)
```

**Description**

Truncates a `TIME` value at a particular granularity.

**Definitions**

+ `time_value`: The `TIME` value to truncate.
+ `time_granularity`: The truncation granularity for a `TIME` value.
  [Time granularities][time-trunc-granularity-time] can be used.

<a id="time_trunc_granularity_time"></a>

**Time granularity definitions**

  + `NANOSECOND`: If used, nothing is truncated from the value.

  + `MICROSECOND`: The nearest lesser than or equal microsecond.

  + `MILLISECOND`: The nearest lesser than or equal millisecond.

  + `SECOND`: The nearest lesser than or equal second.

  + `MINUTE`: The nearest lesser than or equal minute.

  + `HOUR`: The nearest lesser than or equal hour.

**Details**

The resulting value is always rounded to the beginning of `granularity`.

**Return Data Type**

`TIME`

**Example**

```googlesql
SELECT
  TIME "15:30:00" as original,
  TIME_TRUNC(TIME "15:30:00", HOUR) as truncated;

/*----------------------------+------------------------+
 | original                   | truncated              |
 +----------------------------+------------------------+
 | 15:30:00                   | 15:00:00               |
 +----------------------------+------------------------*/
```

[time-trunc-granularity-time]: #time_trunc_granularity_time

[time-to-string]: https://github.com/google/googlesql/blob/master/docs/conversion_functions.md#cast

