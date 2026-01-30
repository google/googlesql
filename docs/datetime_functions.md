

<!-- mdlint off(WHITESPACE_LINE_LENGTH) -->

# Datetime functions

GoogleSQL supports the following datetime functions.

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
  <td><a href="https://github.com/google/googlesql/blob/master/docs/datetime_functions.md#current_datetime"><code>CURRENT_DATETIME</code></a>
</td>
  <td>
    Returns the current date and time as a <code>DATETIME</code> value.
  </td>
</tr>

<tr>
  <td><a href="https://github.com/google/googlesql/blob/master/docs/datetime_functions.md#datetime"><code>DATETIME</code></a>
</td>
  <td>
    Constructs a <code>DATETIME</code> value.
  </td>
</tr>

<tr>
  <td><a href="https://github.com/google/googlesql/blob/master/docs/datetime_functions.md#datetime_add"><code>DATETIME_ADD</code></a>
</td>
  <td>
    Adds a specified time interval to a <code>DATETIME</code> value.
  </td>
</tr>

<tr>
  <td><a href="https://github.com/google/googlesql/blob/master/docs/datetime_functions.md#datetime_diff"><code>DATETIME_DIFF</code></a>
</td>
  <td>
    Gets the number of unit boundaries between two <code>DATETIME</code> values
    at a particular time granularity.
  </td>
</tr>

<tr>
  <td><a href="https://github.com/google/googlesql/blob/master/docs/datetime_functions.md#datetime_sub"><code>DATETIME_SUB</code></a>
</td>
  <td>
    Subtracts a specified time interval from a <code>DATETIME</code> value.
  </td>
</tr>

<tr>
  <td><a href="https://github.com/google/googlesql/blob/master/docs/datetime_functions.md#datetime_trunc"><code>DATETIME_TRUNC</code></a>
</td>
  <td>
    
    Truncates a <code>DATETIME</code> or
    <code>TIMESTAMP</code> value at a particular
    granularity.
    
  </td>
</tr>

<tr>
  <td><a href="https://github.com/google/googlesql/blob/master/docs/datetime_functions.md#extract"><code>EXTRACT</code></a>
</td>
  <td>
    Extracts part of a date and time from a <code>DATETIME</code> value.
  </td>
</tr>

<tr>
  <td><a href="https://github.com/google/googlesql/blob/master/docs/datetime_functions.md#format_datetime"><code>FORMAT_DATETIME</code></a>
</td>
  <td>
    Formats a <code>DATETIME</code> value according to a specified
    format string.
  </td>
</tr>

<tr>
  <td><a href="https://github.com/google/googlesql/blob/master/docs/datetime_functions.md#last_day"><code>LAST_DAY</code></a>
</td>
  <td>
    Gets the last day in a specified time period that contains a
    <code>DATETIME</code> value.
  </td>
</tr>

<tr>
  <td><a href="https://github.com/google/googlesql/blob/master/docs/datetime_functions.md#parse_datetime"><code>PARSE_DATETIME</code></a>
</td>
  <td>
    Converts a <code>STRING</code> value to a <code>DATETIME</code> value.
    
  </td>
</tr>

  </tbody>
</table>

## `CURRENT_DATETIME`

```googlesql
CURRENT_DATETIME([time_zone])
```

```googlesql
CURRENT_DATETIME
```

**Description**

Returns the current time as a `DATETIME` object. Parentheses are optional when
called with no arguments.

This function supports an optional `time_zone` parameter.
See [Time zone definitions][datetime-timezone-definitions] for
information on how to specify a time zone.

The current date and time value is set at the start of the query statement that
contains this function. All invocations of `CURRENT_DATETIME()` within a query
statement yield the same value.

**Return Data Type**

`DATETIME`

**Example**

```googlesql
SELECT CURRENT_DATETIME() as now;

/*----------------------------+
 | now                        |
 +----------------------------+
 | 2016-05-19 10:38:47.046465 |
 +----------------------------*/
```

[datetime-timezone-definitions]: https://github.com/google/googlesql/blob/master/docs/timestamp_functions.md#timezone_definitions

## `DATETIME`

```googlesql
1. DATETIME(year, month, day, hour, minute, second)
2. DATETIME(date_expression[, time_expression])
3. DATETIME(timestamp_expression [, time_zone])
```

**Description**

1. Constructs a `DATETIME` object using `INT64` values
   representing the year, month, day, hour, minute, and second.
2. Constructs a `DATETIME` object using a DATE object and an optional `TIME`
   object.
3. Constructs a `DATETIME` object using a `TIMESTAMP` object. It supports an
   optional parameter to
   [specify a time zone][datetime-timezone-definitions].
   If no time zone is specified, the default time zone, which is implementation defined,
   is used.

**Return Data Type**

`DATETIME`

**Example**

```googlesql
SELECT
  DATETIME(2008, 12, 25, 05, 30, 00) as datetime_ymdhms,
  DATETIME(TIMESTAMP "2008-12-25 05:30:00+00", "America/Los_Angeles") as datetime_tstz;

/*---------------------+---------------------+
 | datetime_ymdhms     | datetime_tstz       |
 +---------------------+---------------------+
 | 2008-12-25 05:30:00 | 2008-12-24 21:30:00 |
 +---------------------+---------------------*/
```

[datetime-timezone-definitions]: https://github.com/google/googlesql/blob/master/docs/timestamp_functions.md#timezone_definitions

## `DATETIME_ADD`

```googlesql
DATETIME_ADD(datetime_expression, INTERVAL int64_expression part)
```

**Description**

Adds `int64_expression` units of `part` to the `DATETIME` object.

`DATETIME_ADD` supports the following values for `part`:

+ `NANOSECOND`
+ `MICROSECOND`
+ `MILLISECOND`
+ `SECOND`
+ `MINUTE`
+ `HOUR`
+ `DAY`
+ `WEEK`. Equivalent to 7 `DAY`s.
+ `MONTH`
+ `QUARTER`
+ `YEAR`

Special handling is required for MONTH, QUARTER, and YEAR parts when the
date is at (or near) the last day of the month. If the resulting month has fewer
days than the original DATETIME's day, then the result day is the last day of
the new month.

**Return Data Type**

`DATETIME`

**Example**

```googlesql
SELECT
  DATETIME "2008-12-25 15:30:00" as original_date,
  DATETIME_ADD(DATETIME "2008-12-25 15:30:00", INTERVAL 10 MINUTE) as later;

/*-----------------------------+------------------------+
 | original_date               | later                  |
 +-----------------------------+------------------------+
 | 2008-12-25 15:30:00         | 2008-12-25 15:40:00    |
 +-----------------------------+------------------------*/
```

## `DATETIME_DIFF`

```googlesql
DATETIME_DIFF(end_datetime, start_datetime, granularity)
```

**Description**

Gets the number of unit boundaries between two `DATETIME` values
(`end_datetime` - `start_datetime`) at a particular time granularity.

**Definitions**

+   `start_datetime`: The starting `DATETIME` value.
+   `end_datetime`: The ending `DATETIME` value.
+   `granularity`: The datetime part that represents the granularity. If
    you have passed in `DATETIME` values for the first arguments, `granularity`
    can be:

      
      + `NANOSECOND`
      + `MICROSECOND`
      + `MILLISECOND`
      + `SECOND`
      + `MINUTE`
      + `HOUR`
      + `DAY`
      + `WEEK`: This date part begins on Sunday.
      + `WEEK(<WEEKDAY>)`: This date part begins on `WEEKDAY`. Valid values for
        `WEEKDAY` are `SUNDAY`, `MONDAY`, `TUESDAY`, `WEDNESDAY`, `THURSDAY`,
        `FRIDAY`, and `SATURDAY`.
      + `ISOWEEK`: Uses [ISO 8601 week][ISO-8601-week]
        boundaries. ISO weeks begin on Monday.
      + `MONTH`
      + `QUARTER`
      + `YEAR`
      + `ISOYEAR`: Uses the [ISO 8601][ISO-8601]
        week-numbering year boundary. The ISO year boundary is the Monday of the
        first week whose Thursday belongs to the corresponding Gregorian calendar
        year.

**Details**

If `end_datetime` is earlier than `start_datetime`, the output is negative.
Produces an error if the computation overflows, such as if the difference
in nanoseconds
between the two `DATETIME` values overflows.

Note: The behavior of the this function follows the type of arguments passed in.
For example, `DATETIME_DIFF(TIMESTAMP, TIMESTAMP, PART)`
behaves like `TIMESTAMP_DIFF(TIMESTAMP, TIMESTAMP, PART)`.

**Return Data Type**

`INT64`

**Example**

```googlesql
SELECT
  DATETIME "2010-07-07 10:20:00" as first_datetime,
  DATETIME "2008-12-25 15:30:00" as second_datetime,
  DATETIME_DIFF(DATETIME "2010-07-07 10:20:00",
    DATETIME "2008-12-25 15:30:00", DAY) as difference;

/*----------------------------+------------------------+------------------------+
 | first_datetime             | second_datetime        | difference             |
 +----------------------------+------------------------+------------------------+
 | 2010-07-07 10:20:00        | 2008-12-25 15:30:00    | 559                    |
 +----------------------------+------------------------+------------------------*/
```

```googlesql
SELECT
  DATETIME_DIFF(DATETIME '2017-10-15 00:00:00',
    DATETIME '2017-10-14 00:00:00', DAY) as days_diff,
  DATETIME_DIFF(DATETIME '2017-10-15 00:00:00',
    DATETIME '2017-10-14 00:00:00', WEEK) as weeks_diff;

/*-----------+------------+
 | days_diff | weeks_diff |
 +-----------+------------+
 | 1         | 1          |
 +-----------+------------*/
```

The example above shows the result of `DATETIME_DIFF` for two `DATETIME`s that
are 24 hours apart. `DATETIME_DIFF` with the part `WEEK` returns 1 because
`DATETIME_DIFF` counts the number of part boundaries in this range of
`DATETIME`s. Each `WEEK` begins on Sunday, so there is one part boundary between
Saturday, `2017-10-14 00:00:00` and Sunday, `2017-10-15 00:00:00`.

The following example shows the result of `DATETIME_DIFF` for two dates in
different years. `DATETIME_DIFF` with the date part `YEAR` returns 3 because it
counts the number of Gregorian calendar year boundaries between the two
`DATETIME`s. `DATETIME_DIFF` with the date part `ISOYEAR` returns 2 because the
second `DATETIME` belongs to the ISO year 2015. The first Thursday of the 2015
calendar year was 2015-01-01, so the ISO year 2015 begins on the preceding
Monday, 2014-12-29.

```googlesql
SELECT
  DATETIME_DIFF('2017-12-30 00:00:00',
    '2014-12-30 00:00:00', YEAR) AS year_diff,
  DATETIME_DIFF('2017-12-30 00:00:00',
    '2014-12-30 00:00:00', ISOYEAR) AS isoyear_diff;

/*-----------+--------------+
 | year_diff | isoyear_diff |
 +-----------+--------------+
 | 3         | 2            |
 +-----------+--------------*/
```

The following example shows the result of `DATETIME_DIFF` for two days in
succession. The first date falls on a Monday and the second date falls on a
Sunday. `DATETIME_DIFF` with the date part `WEEK` returns 0 because this time
part uses weeks that begin on Sunday. `DATETIME_DIFF` with the date part
`WEEK(MONDAY)` returns 1. `DATETIME_DIFF` with the date part
`ISOWEEK` also returns 1 because ISO weeks begin on Monday.

```googlesql
SELECT
  DATETIME_DIFF('2017-12-18', '2017-12-17', WEEK) AS week_diff,
  DATETIME_DIFF('2017-12-18', '2017-12-17', WEEK(MONDAY)) AS week_weekday_diff,
  DATETIME_DIFF('2017-12-18', '2017-12-17', ISOWEEK) AS isoweek_diff;

/*-----------+-------------------+--------------+
 | week_diff | week_weekday_diff | isoweek_diff |
 +-----------+-------------------+--------------+
 | 0         | 1                 | 1            |
 +-----------+-------------------+--------------*/
```

[ISO-8601]: https://en.wikipedia.org/wiki/ISO_8601

[ISO-8601-week]: https://en.wikipedia.org/wiki/ISO_week_date

## `DATETIME_SUB`

```googlesql
DATETIME_SUB(datetime_expression, INTERVAL int64_expression part)
```

**Description**

Subtracts `int64_expression` units of `part` from the `DATETIME`.

`DATETIME_SUB` supports the following values for `part`:

+ `NANOSECOND`
+ `MICROSECOND`
+ `MILLISECOND`
+ `SECOND`
+ `MINUTE`
+ `HOUR`
+ `DAY`
+ `WEEK`. Equivalent to 7 `DAY`s.
+ `MONTH`
+ `QUARTER`
+ `YEAR`

Special handling is required for `MONTH`, `QUARTER`, and `YEAR` parts when the
date is at (or near) the last day of the month. If the resulting month has fewer
days than the original `DATETIME`'s day, then the result day is the last day of
the new month.

**Return Data Type**

`DATETIME`

**Example**

```googlesql
SELECT
  DATETIME "2008-12-25 15:30:00" as original_date,
  DATETIME_SUB(DATETIME "2008-12-25 15:30:00", INTERVAL 10 MINUTE) as earlier;

/*-----------------------------+------------------------+
 | original_date               | earlier                |
 +-----------------------------+------------------------+
 | 2008-12-25 15:30:00         | 2008-12-25 15:20:00    |
 +-----------------------------+------------------------*/
```

## `DATETIME_TRUNC`

```googlesql
DATETIME_TRUNC(datetime_value, datetime_granularity)
```

```googlesql
DATETIME_TRUNC(timestamp_value, timestamp_granularity[, time_zone])
```

**Description**

Truncates a `DATETIME` or `TIMESTAMP` value at a particular granularity.

**Definitions**

+ `datetime_value`: A `DATETIME` value to truncate.
+ `datetime_granularity`: The truncation granularity for a `DATETIME` value.
  [Date granularities][datetime-trunc-granularity-date] and
  [time granularities][datetime-trunc-granularity-time] can be used.
+ `timestamp_value`: A `TIMESTAMP` value to truncate.
+ `timestamp_granularity`: The truncation granularity for a `TIMESTAMP` value.
  [Date granularities][datetime-trunc-granularity-date] and
  [time granularities][datetime-trunc-granularity-time] can be used.
+ `time_zone`: A time zone to use with the `TIMESTAMP` value.
  [Time zone parts][datetime-time-zone-parts] can be used.
  Use this argument if you want to use a time zone other than
  the default time zone, which is implementation defined, as part of the
  truncate operation.

      Note: When truncating a timestamp to `MINUTE`
    or `HOUR` parts, this function determines the civil time of the
    timestamp in the specified (or default) time zone
    and subtracts the minutes and seconds (when truncating to `HOUR`) or the
    seconds (when truncating to `MINUTE`) from that timestamp.
    While this provides intuitive results in most cases, the result is
    non-intuitive near daylight savings transitions that aren't hour-aligned.

<a id="datetime_trunc_granularity_date"></a>

**Date granularity definitions**

  + `DAY`: The day in the Gregorian calendar year that contains the
    value to truncate.

  + `WEEK`: The first day in the week that contains the
    value to truncate. Weeks begin on Sundays. `WEEK` is equivalent to
    `WEEK(SUNDAY)`.

  + `WEEK(WEEKDAY)`: The first day in the week that contains the
    value to truncate. Weeks begin on `WEEKDAY`. `WEEKDAY` must be one of the
     following: `SUNDAY`, `MONDAY`, `TUESDAY`, `WEDNESDAY`, `THURSDAY`, `FRIDAY`,
     or `SATURDAY`.

  + `ISOWEEK`: The first day in the [ISO 8601 week][ISO-8601-week] that contains
    the value to truncate. The ISO week begins on
    Monday. The first ISO week of each ISO year contains the first Thursday of the
    corresponding Gregorian calendar year.

  + `MONTH`: The first day in the month that contains the
    value to truncate.

  + `QUARTER`: The first day in the quarter that contains the
    value to truncate.

  + `YEAR`: The first day in the year that contains the
    value to truncate.

  + `ISOYEAR`: The first day in the [ISO 8601][ISO-8601] week-numbering year
    that contains the value to truncate. The ISO year is the
    Monday of the first week where Thursday belongs to the corresponding
    Gregorian calendar year.

<!-- mdlint off(WHITESPACE_LINE_LENGTH) -->

[ISO-8601]: https://en.wikipedia.org/wiki/ISO_8601

[ISO-8601-week]: https://en.wikipedia.org/wiki/ISO_week_date

<!-- mdlint on -->

<a id="datetime_trunc_granularity_time"></a>

**Time granularity definitions**

  + `NANOSECOND`: If used, nothing is truncated from the value.

  + `MICROSECOND`: The nearest lesser than or equal microsecond.

  + `MILLISECOND`: The nearest lesser than or equal millisecond.

  + `SECOND`: The nearest lesser than or equal second.

  + `MINUTE`: The nearest lesser than or equal minute.

  + `HOUR`: The nearest lesser than or equal hour.

<a id="datetime_time_zone_parts"></a>

**Time zone part definitions**

+ `MINUTE`
+ `HOUR`
+ `DAY`
+ `WEEK`
+ `WEEK(<WEEKDAY>)`
+ `ISOWEEK`
+ `MONTH`
+ `QUARTER`
+ `YEAR`
+ `ISOYEAR`

**Details**

The resulting value is always rounded to the beginning of `granularity`.

**Return Data Type**

The same data type as the first argument passed into this function.

**Examples**

```googlesql
SELECT
  DATETIME "2008-12-25 15:30:00" as original,
  DATETIME_TRUNC(DATETIME "2008-12-25 15:30:00", DAY) as truncated;

/*----------------------------+------------------------+
 | original                   | truncated              |
 +----------------------------+------------------------+
 | 2008-12-25 15:30:00        | 2008-12-25 00:00:00    |
 +----------------------------+------------------------*/
```

In the following example, the original `DATETIME` falls on a Sunday. Because the
`part` is `WEEK(MONDAY)`, `DATE_TRUNC` returns the `DATETIME` for the
preceding Monday.

```googlesql
SELECT
 datetime AS original,
 DATETIME_TRUNC(datetime, WEEK(MONDAY)) AS truncated
FROM (SELECT DATETIME(TIMESTAMP "2017-11-05 00:00:00+00", "UTC") AS datetime);

/*---------------------+---------------------+
 | original            | truncated           |
 +---------------------+---------------------+
 | 2017-11-05 00:00:00 | 2017-10-30 00:00:00 |
 +---------------------+---------------------*/
```

In the following example, the original `datetime_expression` is in the Gregorian
calendar year 2015. However, `DATETIME_TRUNC` with the `ISOYEAR` date part
truncates the `datetime_expression` to the beginning of the ISO year, not the
Gregorian calendar year. The first Thursday of the 2015 calendar year was
2015-01-01, so the ISO year 2015 begins on the preceding Monday, 2014-12-29.
Therefore the ISO year boundary preceding the `datetime_expression`
2015-06-15 00:00:00 is 2014-12-29.

```googlesql
SELECT
  DATETIME_TRUNC('2015-06-15 00:00:00', ISOYEAR) AS isoyear_boundary,
  EXTRACT(ISOYEAR FROM DATETIME '2015-06-15 00:00:00') AS isoyear_number;

/*---------------------+----------------+
 | isoyear_boundary    | isoyear_number |
 +---------------------+----------------+
 | 2014-12-29 00:00:00 | 2015           |
 +---------------------+----------------*/
```

[datetime-trunc-granularity-date]: #datetime_trunc_granularity_date

[datetime-trunc-granularity-time]: #datetime_trunc_granularity_time

[datetime-time-zone-parts]: #datetime_time_zone_parts

## `EXTRACT`

```googlesql
EXTRACT(part FROM datetime_expression)
```

**Description**

Returns a value that corresponds to the
specified `part` from a supplied `datetime_expression`.

Allowed `part` values are:

+ `NANOSECOND`
+ `MICROSECOND`
+ `MILLISECOND`
+ `SECOND`
+ `MINUTE`
+ `HOUR`
+ `DAYOFWEEK`: Returns values in the range [1,7] with Sunday as the first day of
   of the week.
+ `DAY`
+ `DAYOFYEAR`
+ `WEEK`: Returns the week number of the date in the range [0, 53]. Weeks begin
  with Sunday, and dates prior to the first Sunday of the year are in week
  0.
+ `WEEK(<WEEKDAY>)`: Returns the week number of `datetime_expression` in the
  range [0, 53]. Weeks begin on `WEEKDAY`.
  `datetime`s prior to the first `WEEKDAY` of the year are in week 0. Valid
  values for `WEEKDAY` are `SUNDAY`, `MONDAY`, `TUESDAY`, `WEDNESDAY`,
  `THURSDAY`, `FRIDAY`, and `SATURDAY`.
+ `ISOWEEK`: Returns the [ISO 8601 week][ISO-8601-week]
  number of the `datetime_expression`. `ISOWEEK`s begin on Monday. Return values
  are in the range [1, 53]. The first `ISOWEEK` of each ISO year begins on the
  Monday before the first Thursday of the Gregorian calendar year.
+ `MONTH`
+ `QUARTER`
+ `YEAR`
+ `ISOYEAR`: Returns the [ISO 8601][ISO-8601]
  week-numbering year, which is the Gregorian calendar year containing the
  Thursday of the week to which `date_expression` belongs.
+ `DATE`
+ `TIME`

Returned values truncate lower order time periods. For example, when extracting
seconds, `EXTRACT` truncates the millisecond and microsecond values.

**Return Data Type**

`INT64`, except in the following cases:

+ If `part` is `DATE`, returns a `DATE` object.
+ If `part` is `TIME`, returns a `TIME` object.

**Examples**

In the following example, `EXTRACT` returns a value corresponding to the `HOUR`
time part.

```googlesql
SELECT EXTRACT(HOUR FROM DATETIME(2008, 12, 25, 15, 30, 00)) as hour;

/*------------------+
 | hour             |
 +------------------+
 | 15               |
 +------------------*/
```

In the following example, `EXTRACT` returns values corresponding to different
time parts from a column of datetimes.

```googlesql
WITH Datetimes AS (
  SELECT DATETIME '2005-01-03 12:34:56' AS datetime UNION ALL
  SELECT DATETIME '2007-12-31' UNION ALL
  SELECT DATETIME '2009-01-01' UNION ALL
  SELECT DATETIME '2009-12-31' UNION ALL
  SELECT DATETIME '2017-01-02' UNION ALL
  SELECT DATETIME '2017-05-26'
)
SELECT
  datetime,
  EXTRACT(ISOYEAR FROM datetime) AS isoyear,
  EXTRACT(ISOWEEK FROM datetime) AS isoweek,
  EXTRACT(YEAR FROM datetime) AS year,
  EXTRACT(WEEK FROM datetime) AS week
FROM Datetimes
ORDER BY datetime;

/*---------------------+---------+---------+------+------+
 | datetime            | isoyear | isoweek | year | week |
 +---------------------+---------+---------+------+------+
 | 2005-01-03 12:34:56 | 2005    | 1       | 2005 | 1    |
 | 2007-12-31 00:00:00 | 2008    | 1       | 2007 | 52   |
 | 2009-01-01 00:00:00 | 2009    | 1       | 2009 | 0    |
 | 2009-12-31 00:00:00 | 2009    | 53      | 2009 | 52   |
 | 2017-01-02 00:00:00 | 2017    | 1       | 2017 | 1    |
 | 2017-05-26 00:00:00 | 2017    | 21      | 2017 | 21   |
 +---------------------+---------+---------+------+------*/
```

In the following example, `datetime_expression` falls on a Sunday. `EXTRACT`
calculates the first column using weeks that begin on Sunday, and it calculates
the second column using weeks that begin on Monday.

```googlesql
WITH table AS (SELECT DATETIME(TIMESTAMP "2017-11-05 00:00:00+00", "UTC") AS datetime)
SELECT
  datetime,
  EXTRACT(WEEK(SUNDAY) FROM datetime) AS week_sunday,
  EXTRACT(WEEK(MONDAY) FROM datetime) AS week_monday
FROM table;

/*---------------------+-------------+---------------+
 | datetime            | week_sunday | week_monday   |
 +---------------------+-------------+---------------+
 | 2017-11-05 00:00:00 | 45          | 44            |
 +---------------------+-------------+---------------*/
```

[ISO-8601]: https://en.wikipedia.org/wiki/ISO_8601

[ISO-8601-week]: https://en.wikipedia.org/wiki/ISO_week_date

## `FORMAT_DATETIME`

```googlesql
FORMAT_DATETIME(format_string, datetime_expr)
```

**Description**

Formats a `DATETIME` value according to a specified format string.

**Definitions**

+   `format_string`: A `STRING` value that contains the
    [format elements][datetime-format-elements] to use with
    `datetime_expr`.
+   `datetime_expr`: A `DATETIME` value that represents the date and time to
    format.

**Return Data Type**

`STRING`

**Examples**

```googlesql
SELECT
  FORMAT_DATETIME("%c", DATETIME "2008-12-25 15:30:00")
  AS formatted;

/*--------------------------+
 | formatted                |
 +--------------------------+
 | Thu Dec 25 15:30:00 2008 |
 +--------------------------*/
```

```googlesql
SELECT
  FORMAT_DATETIME("%b-%d-%Y", DATETIME "2008-12-25 15:30:00")
  AS formatted;

/*-------------+
 | formatted   |
 +-------------+
 | Dec-25-2008 |
 +-------------*/
```

```googlesql
SELECT
  FORMAT_DATETIME("%b %Y", DATETIME "2008-12-25 15:30:00")
  AS formatted;

/*-------------+
 | formatted   |
 +-------------+
 | Dec 2008    |
 +-------------*/
```

[datetime-format-elements]: https://github.com/google/googlesql/blob/master/docs/format-elements.md#format_elements_date_time

## `LAST_DAY`

```googlesql
LAST_DAY(datetime_expression[, date_part])
```

**Description**

Returns the last day from a datetime expression that contains the date.
This is commonly used to return the last day of the month.

You can optionally specify the date part for which the last day is returned.
If this parameter isn't used, the default value is `MONTH`.
`LAST_DAY` supports the following values for `date_part`:

+  `YEAR`
+  `QUARTER`
+  `MONTH`
+  `WEEK`. Equivalent to 7 `DAY`s.
+  `WEEK(<WEEKDAY>)`. `<WEEKDAY>` represents the starting day of the week.
   Valid values are `SUNDAY`, `MONDAY`, `TUESDAY`, `WEDNESDAY`, `THURSDAY`,
   `FRIDAY`, and `SATURDAY`.
+  `ISOWEEK`. Uses [ISO 8601][ISO-8601-week] week boundaries. ISO weeks begin
   on Monday.
+  `ISOYEAR`. Uses the [ISO 8601][ISO-8601] week-numbering year boundary.
   The ISO year boundary is the Monday of the first week whose Thursday belongs
   to the corresponding Gregorian calendar year.

**Return Data Type**

`DATE`

**Example**

These both return the last day of the month:

```googlesql
SELECT LAST_DAY(DATETIME '2008-11-25', MONTH) AS last_day

/*------------+
 | last_day   |
 +------------+
 | 2008-11-30 |
 +------------*/
```

```googlesql
SELECT LAST_DAY(DATETIME '2008-11-25') AS last_day

/*------------+
 | last_day   |
 +------------+
 | 2008-11-30 |
 +------------*/
```

This returns the last day of the year:

```googlesql
SELECT LAST_DAY(DATETIME '2008-11-25 15:30:00', YEAR) AS last_day

/*------------+
 | last_day   |
 +------------+
 | 2008-12-31 |
 +------------*/
```

This returns the last day of the week for a week that starts on a Sunday:

```googlesql
SELECT LAST_DAY(DATETIME '2008-11-10 15:30:00', WEEK(SUNDAY)) AS last_day

/*------------+
 | last_day   |
 +------------+
 | 2008-11-15 |
 +------------*/
```

This returns the last day of the week for a week that starts on a Monday:

```googlesql
SELECT LAST_DAY(DATETIME '2008-11-10 15:30:00', WEEK(MONDAY)) AS last_day

/*------------+
 | last_day   |
 +------------+
 | 2008-11-16 |
 +------------*/
```

[ISO-8601]: https://en.wikipedia.org/wiki/ISO_8601

[ISO-8601-week]: https://en.wikipedia.org/wiki/ISO_week_date

## `PARSE_DATETIME`

```googlesql
PARSE_DATETIME(format_string, datetime_string)
```

**Description**

Converts a `STRING` value to a `DATETIME` value.

**Definitions**

+   `format_string`: A `STRING` value that contains the
    [format elements][datetime-format-elements] to use with `datetime_string`.
+   `datetime_string`: A `STRING` value that represents the date and time to
    parse.

**Details**

Each element in `datetime_string` must have a corresponding element in
`format_string`. The location of each element in `format_string` must match the
location of each element in `datetime_string`.

```googlesql
-- This works because elements on both sides match.
SELECT PARSE_DATETIME("%a %b %e %I:%M:%S %Y", "Thu Dec 25 07:30:00 2008");

-- This produces an error because the year element is in different locations.
SELECT PARSE_DATETIME("%a %b %e %Y %I:%M:%S", "Thu Dec 25 07:30:00 2008");

-- This produces an error because one of the year elements is missing.
SELECT PARSE_DATETIME("%a %b %e %I:%M:%S", "Thu Dec 25 07:30:00 2008");

-- This works because %c can find all matching elements in datetime_string.
SELECT PARSE_DATETIME("%c", "Thu Dec 25 07:30:00 2008");
```

The following additional considerations apply when using the `PARSE_DATETIME`
function:

+ Unspecified fields. Any unspecified field is initialized from
  `1970-01-01 00:00:00.0`. For example, if the year is unspecified then it
  defaults to `1970`.
+ Case insensitivity. Names, such as `Monday` and `February`,
  are case insensitive.
+ Whitespace. One or more consecutive white spaces in the format string
  matches zero or more consecutive white spaces in the
  `DATETIME` string. Leading and trailing
  white spaces in the `DATETIME` string are always
  allowed, even if they aren't in the format string.
+ Format precedence. When two or more format elements have overlapping
  information, the last one generally overrides any earlier ones, with some
  exceptions. For example, both `%F` and `%Y` affect the year, so the earlier
  element overrides the later. See the descriptions
  of `%s`, `%C`, and `%y` in
  [Supported Format Elements For DATETIME][datetime-format-elements].
+ Format divergence. `%p` can be used with `am`, `AM`, `pm`, and `PM`.
+   Mixed ISO and non-ISO elements. The ISO format elements are `%G`, `%g`,
    `%J`, and `%V`. When these ISO elements are used together with other non-ISO
    elements, the ISO elements are ignored, resulting in different values. For
    example, the function arguments `('%g %J', '8405')` return a value with the
    year `1984`, whereas the arguments `('%g %j', '8405')` return a value with
    the year `1970` because the ISO element `%g` is ignored.
+   Numeric values after `%G` input values. Any input string value that
    corresponds to the `%G` format element requires a whitespace or non-digit
    character as a separator from numeric values that follow. This is a known
    issue in GoogleSQL. For example, the function arguments `('%G
    %V','2020 50')` or `('%G-%V','2020-50')` work, but not `('%G%V','202050')`.
    For input values before the corresponding `%G` value, no separator is
    needed. For example, the arguments `('%V%G','502020')` work. The separator
    after the `%G` values identifies the end of the specified ISO year value so
    that the function can parse properly.

**Return Data Type**

`DATETIME`

**Examples**

The following examples parse a `STRING` literal as a
`DATETIME`.

```googlesql
SELECT PARSE_DATETIME('%Y-%m-%d %H:%M:%S', '1998-10-18 13:45:55') AS datetime;

/*---------------------+
 | datetime            |
 +---------------------+
 | 1998-10-18 13:45:55 |
 +---------------------*/
```

```googlesql
SELECT PARSE_DATETIME('%m/%d/%Y %I:%M:%S %p', '8/30/2018 2:23:38 pm') AS datetime;

/*---------------------+
 | datetime            |
 +---------------------+
 | 2018-08-30 14:23:38 |
 +---------------------*/
```

The following example parses a `STRING` literal
containing a date in a natural language format as a
`DATETIME`.

```googlesql
SELECT PARSE_DATETIME('%A, %B %e, %Y','Wednesday, December 19, 2018')
  AS datetime;

/*---------------------+
 | datetime            |
 +---------------------+
 | 2018-12-19 00:00:00 |
 +---------------------*/
```

[datetime-format-elements]: https://github.com/google/googlesql/blob/master/docs/format-elements.md#format_elements_date_time

[ISO-8601]: https://en.wikipedia.org/wiki/ISO_8601

