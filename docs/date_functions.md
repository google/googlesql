

<!-- mdlint off(WHITESPACE_LINE_LENGTH) -->

# Date functions

GoogleSQL supports the following date functions.

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
  <td><a href="https://github.com/google/googlesql/blob/master/docs/date_functions.md#current_date"><code>CURRENT_DATE</code></a>
</td>
  <td>
    Returns the current date as a <code>DATE</code> value.
  </td>
</tr>

<tr>
  <td><a href="https://github.com/google/googlesql/blob/master/docs/date_functions.md#date"><code>DATE</code></a>
</td>
  <td>
    Constructs a <code>DATE</code> value.
  </td>
</tr>

<tr>
  <td><a href="https://github.com/google/googlesql/blob/master/docs/date_functions.md#date_add"><code>DATE_ADD</code></a>
</td>
  <td>
    Adds a specified time interval to a <code>DATE</code> value.
  </td>
</tr>

<tr>
  <td><a href="https://github.com/google/googlesql/blob/master/docs/date_functions.md#date_diff"><code>DATE_DIFF</code></a>
</td>
  <td>
    Gets the number of unit boundaries between two <code>DATE</code> values
    at a particular time granularity.
  </td>
</tr>

<tr>
  <td><a href="https://github.com/google/googlesql/blob/master/docs/date_functions.md#date_from_unix_date"><code>DATE_FROM_UNIX_DATE</code></a>
</td>
  <td>
    Interprets an <code>INT64</code> expression as the number of days
    since 1970-01-01.
    
  </td>
</tr>

<tr>
  <td><a href="https://github.com/google/googlesql/blob/master/docs/date_functions.md#date_sub"><code>DATE_SUB</code></a>
</td>
  <td>
    Subtracts a specified time interval from a <code>DATE</code> value.
  </td>
</tr>

<tr>
  <td><a href="https://github.com/google/googlesql/blob/master/docs/date_functions.md#date_trunc"><code>DATE_TRUNC</code></a>
</td>
  <td>
    
    Truncates a <code>DATE</code>, <code>DATETIME</code>, or
    <code>TIMESTAMP</code> value at a particular
    granularity.
    
  </td>
</tr>

<tr>
  <td><a href="https://github.com/google/googlesql/blob/master/docs/date_functions.md#extract"><code>EXTRACT</code></a>
</td>
  <td>
    Extracts part of a date from a <code>DATE</code> value.
  </td>
</tr>

<tr>
  <td><a href="https://github.com/google/googlesql/blob/master/docs/date_functions.md#format_date"><code>FORMAT_DATE</code></a>
</td>
  <td>
    Formats a <code>DATE</code> value according to a specified format string.
  </td>
</tr>

<tr>
  <td><a href="https://github.com/google/googlesql/blob/master/docs/array_functions.md#generate_date_array"><code>GENERATE_DATE_ARRAY</code></a>
</td>
  <td>
    Generates an array of dates in a range.
    <br>For more information, see <a href="https://github.com/google/googlesql/blob/master/docs/array_functions.md">Array functions</a>.

  </td>
</tr>

<tr>
  <td><a href="https://github.com/google/googlesql/blob/master/docs/date_functions.md#last_day"><code>LAST_DAY</code></a>
</td>
  <td>
    Gets the last day in a specified time period that contains a
    <code>DATE</code> value.
  </td>
</tr>

<tr>
  <td><a href="https://github.com/google/googlesql/blob/master/docs/date_functions.md#parse_date"><code>PARSE_DATE</code></a>
</td>
  <td>
    Converts a <code>STRING</code> value to a <code>DATE</code> value.
    
  </td>
</tr>

<tr>
  <td><a href="https://github.com/google/googlesql/blob/master/docs/date_functions.md#unix_date"><code>UNIX_DATE</code></a>
</td>
  <td>
    Converts a <code>DATE</code> value to the number of days since 1970-01-01.
    
  </td>
</tr>

  </tbody>
</table>

## `CURRENT_DATE`

```googlesql
CURRENT_DATE()
```

```googlesql
CURRENT_DATE(time_zone_expression)
```

```googlesql
CURRENT_DATE
```

**Description**

Returns the current date as a `DATE` object. Parentheses are optional when
called with no arguments.

This function supports the following arguments:

+ `time_zone_expression`: A `STRING` expression that represents a
  [time zone][date-timezone-definitions]. If no time zone is specified, the
  default time zone, which is implementation defined, is used. If this expression is
  used and it evaluates to `NULL`, this function returns `NULL`.

The current date value is set at the start of the query statement that contains
this function. All invocations of `CURRENT_DATE()` within a query statement
yield the same value.

**Return Data Type**

`DATE`

**Examples**

The following query produces the current date in the default time zone:

```googlesql
SELECT CURRENT_DATE() AS the_date;

/*--------------+
 | the_date     |
 +--------------+
 | 2016-12-25   |
 +--------------*/
```

The following queries produce the current date in a specified time zone:

```googlesql
SELECT CURRENT_DATE('America/Los_Angeles') AS the_date;

/*--------------+
 | the_date     |
 +--------------+
 | 2016-12-25   |
 +--------------*/
```

```googlesql
SELECT CURRENT_DATE('-08') AS the_date;

/*--------------+
 | the_date     |
 +--------------+
 | 2016-12-25   |
 +--------------*/
```

The following query produces the current date in the default time zone.
Parentheses aren't needed if the function has no arguments.

```googlesql
SELECT CURRENT_DATE AS the_date;

/*--------------+
 | the_date     |
 +--------------+
 | 2016-12-25   |
 +--------------*/
```

[date-timezone-definitions]: https://github.com/google/googlesql/blob/master/docs/data-types.md#time_zones

## `DATE`

```googlesql
DATE(year, month, day)
```

```googlesql
DATE(timestamp_expression)
```

```googlesql
DATE(timestamp_expression, time_zone_expression)
```

```
DATE(datetime_expression)
```

**Description**

Constructs or extracts a date.

This function supports the following arguments:

+ `year`: The `INT64` value for year.
+ `month`: The `INT64` value for month.
+ `day`: The `INT64` value for day.
+ `timestamp_expression`: A `TIMESTAMP` expression that contains the date.
+ `time_zone_expression`: A `STRING` expression that represents a
  [time zone][date-timezone-definitions]. If no time zone is specified with
  `timestamp_expression`, the default time zone, which is implementation defined, is
  used.
+ `datetime_expression`: A `DATETIME` expression that contains the date.

**Return Data Type**

`DATE`

**Example**

```googlesql
SELECT
  DATE(2016, 12, 25) AS date_ymd,
  DATE(DATETIME '2016-12-25 23:59:59') AS date_dt,
  DATE(TIMESTAMP '2016-12-25 05:30:00+07', 'America/Los_Angeles') AS date_tstz;

/*------------+------------+------------+
 | date_ymd   | date_dt    | date_tstz  |
 +------------+------------+------------+
 | 2016-12-25 | 2016-12-25 | 2016-12-24 |
 +------------+------------+------------*/
```

[date-timezone-definitions]: https://github.com/google/googlesql/blob/master/docs/timestamp_functions.md#timezone_definitions

## `DATE_ADD`

```googlesql
DATE_ADD(date_expression, INTERVAL int64_expression date_part)
```

**Description**

Adds a specified time interval to a DATE.

`DATE_ADD` supports the following `date_part` values:

+  `DAY`
+  `WEEK`. Equivalent to 7 `DAY`s.
+  `MONTH`
+  `QUARTER`
+  `YEAR`

Special handling is required for MONTH, QUARTER, and YEAR parts when
the date is at (or near) the last day of the month. If the resulting
month has fewer days than the original date's day, then the resulting
date is the last date of that month.

**Return Data Type**

DATE

**Example**

```googlesql
SELECT DATE_ADD(DATE '2008-12-25', INTERVAL 5 DAY) AS five_days_later;

/*--------------------+
 | five_days_later    |
 +--------------------+
 | 2008-12-30         |
 +--------------------*/
```

## `DATE_DIFF`

```googlesql
DATE_DIFF(end_date, start_date, granularity)
```

**Description**

Gets the number of unit boundaries between two `DATE` values (`end_date` -
`start_date`) at a particular time granularity.

**Definitions**

+   `start_date`: The starting `DATE` value.
+   `end_date`: The ending `DATE` value.
+   `granularity`: The date part that represents the granularity. If
    you have passed in `DATE` values for the first arguments, `granularity` can
    be:

    +  `DAY`
    +  `WEEK` This date part begins on Sunday.
    +  `WEEK(<WEEKDAY>)`: This date part begins on `WEEKDAY`. Valid values for
       `WEEKDAY` are `SUNDAY`, `MONDAY`, `TUESDAY`, `WEDNESDAY`, `THURSDAY`,
       `FRIDAY`, and `SATURDAY`.
    +  `ISOWEEK`: Uses [ISO 8601 week][ISO-8601-week] boundaries. ISO weeks
       begin on Monday.
    +  `MONTH`
    +  `QUARTER`
    +  `YEAR`
    +  `ISOYEAR`: Uses the [ISO 8601][ISO-8601] week-numbering year boundary.
       The ISO year boundary is the Monday of the first week whose Thursday
       belongs to the corresponding Gregorian calendar year.

**Details**

If `end_date` is earlier than `start_date`, the output is negative.

Note: The behavior of the this function follows the type of arguments passed in.
For example, `DATE_DIFF(TIMESTAMP, TIMESTAMP, PART)`
behaves like `TIMESTAMP_DIFF(TIMESTAMP, TIMESTAMP, PART)`.

**Return Data Type**

`INT64`

**Example**

```googlesql
SELECT DATE_DIFF(DATE '2010-07-07', DATE '2008-12-25', DAY) AS days_diff;

/*-----------+
 | days_diff |
 +-----------+
 | 559       |
 +-----------*/
```

```googlesql
SELECT
  DATE_DIFF(DATE '2017-10-15', DATE '2017-10-14', DAY) AS days_diff,
  DATE_DIFF(DATE '2017-10-15', DATE '2017-10-14', WEEK) AS weeks_diff;

/*-----------+------------+
 | days_diff | weeks_diff |
 +-----------+------------+
 | 1         | 1          |
 +-----------+------------*/
```

The example above shows the result of `DATE_DIFF` for two days in succession.
`DATE_DIFF` with the date part `WEEK` returns 1 because `DATE_DIFF` counts the
number of date part boundaries in this range of dates. Each `WEEK` begins on
Sunday, so there is one date part boundary between Saturday, 2017-10-14
and Sunday, 2017-10-15.

The following example shows the result of `DATE_DIFF` for two dates in different
years. `DATE_DIFF` with the date part `YEAR` returns 3 because it counts the
number of Gregorian calendar year boundaries between the two dates. `DATE_DIFF`
with the date part `ISOYEAR` returns 2 because the second date belongs to the
ISO year 2015. The first Thursday of the 2015 calendar year was 2015-01-01, so
the ISO year 2015 begins on the preceding Monday, 2014-12-29.

```googlesql
SELECT
  DATE_DIFF('2017-12-30', '2014-12-30', YEAR) AS year_diff,
  DATE_DIFF('2017-12-30', '2014-12-30', ISOYEAR) AS isoyear_diff;

/*-----------+--------------+
 | year_diff | isoyear_diff |
 +-----------+--------------+
 | 3         | 2            |
 +-----------+--------------*/
```

The following example shows the result of `DATE_DIFF` for two days in
succession. The first date falls on a Monday and the second date falls on a
Sunday. `DATE_DIFF` with the date part `WEEK` returns 0 because this date part
uses weeks that begin on Sunday. `DATE_DIFF` with the date part `WEEK(MONDAY)`
returns 1. `DATE_DIFF` with the date part `ISOWEEK` also returns 1 because
ISO weeks begin on Monday.

```googlesql
SELECT
  DATE_DIFF('2017-12-18', '2017-12-17', WEEK) AS week_diff,
  DATE_DIFF('2017-12-18', '2017-12-17', WEEK(MONDAY)) AS week_weekday_diff,
  DATE_DIFF('2017-12-18', '2017-12-17', ISOWEEK) AS isoweek_diff;

/*-----------+-------------------+--------------+
 | week_diff | week_weekday_diff | isoweek_diff |
 +-----------+-------------------+--------------+
 | 0         | 1                 | 1            |
 +-----------+-------------------+--------------*/
```

[ISO-8601]: https://en.wikipedia.org/wiki/ISO_8601

[ISO-8601-week]: https://en.wikipedia.org/wiki/ISO_week_date

## `DATE_FROM_UNIX_DATE`

```googlesql
DATE_FROM_UNIX_DATE(int64_expression)
```

**Description**

Interprets `int64_expression` as the number of days since 1970-01-01.

**Return Data Type**

DATE

**Example**

```googlesql
SELECT DATE_FROM_UNIX_DATE(14238) AS date_from_epoch;

/*-----------------+
 | date_from_epoch |
 +-----------------+
 | 2008-12-25      |
 +-----------------+*/
```

## `DATE_SUB`

```googlesql
DATE_SUB(date_expression, INTERVAL int64_expression date_part)
```

**Description**

Subtracts a specified time interval from a DATE.

`DATE_SUB` supports the following `date_part` values:

+  `DAY`
+  `WEEK`. Equivalent to 7 `DAY`s.
+  `MONTH`
+  `QUARTER`
+  `YEAR`

Special handling is required for MONTH, QUARTER, and YEAR parts when
the date is at (or near) the last day of the month. If the resulting
month has fewer days than the original date's day, then the resulting
date is the last date of that month.

**Return Data Type**

DATE

**Example**

```googlesql
SELECT DATE_SUB(DATE '2008-12-25', INTERVAL 5 DAY) AS five_days_ago;

/*---------------+
 | five_days_ago |
 +---------------+
 | 2008-12-20    |
 +---------------*/
```

## `DATE_TRUNC`

```googlesql
DATE_TRUNC(date_value, date_granularity)
```

```googlesql
DATE_TRUNC(datetime_value, datetime_granularity)
```

```googlesql
DATE_TRUNC(timestamp_value, timestamp_granularity[, time_zone])
```

**Description**

Truncates a `DATE`, `DATETIME`, or `TIMESTAMP` value at a particular
granularity.

**Definitions**

+ `date_value`: A `DATE` value to truncate.
+ `date_granularity`: The truncation granularity for a `DATE` value.
  [Date granularities][date-trunc-granularity-date] can be used.
+ `datetime_value`: A `DATETIME` value to truncate.
+ `datetime_granularity`: The truncation granularity for a `DATETIME` value.
  [Date granularities][date-trunc-granularity-date] and
  [time granularities][date-trunc-granularity-time] can be used.
+ `timestamp_value`: A `TIMESTAMP` value to truncate.
+ `timestamp_granularity`: The truncation granularity for a `TIMESTAMP` value.
  [Date granularities][date-trunc-granularity-date] and
  [time granularities][date-trunc-granularity-time] can be used.
+ `time_zone`: A time zone to use with the `TIMESTAMP` value.
  [Time zone parts][date-time-zone-parts] can be used.
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

<a id="date_trunc_granularity_date"></a>

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

<a id="date_trunc_granularity_time"></a>

**Time granularity definitions**

  + `NANOSECOND`: If used, nothing is truncated from the value.

  + `MICROSECOND`: The nearest lesser than or equal microsecond.

  + `MILLISECOND`: The nearest lesser than or equal millisecond.

  + `SECOND`: The nearest lesser than or equal second.

  + `MINUTE`: The nearest lesser than or equal minute.

  + `HOUR`: The nearest lesser than or equal hour.

<a id="date_time_zone_parts"></a>

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
SELECT DATE_TRUNC(DATE '2008-12-25', MONTH) AS month;

/*------------+
 | month      |
 +------------+
 | 2008-12-01 |
 +------------*/
```

In the following example, the original date falls on a Sunday. Because
the `date_part` is `WEEK(MONDAY)`, `DATE_TRUNC` returns the `DATE` for the
preceding Monday.

```googlesql
SELECT date AS original, DATE_TRUNC(date, WEEK(MONDAY)) AS truncated
FROM (SELECT DATE('2017-11-05') AS date);

/*------------+------------+
 | original   | truncated  |
 +------------+------------+
 | 2017-11-05 | 2017-10-30 |
 +------------+------------*/
```

In the following example, the original `date_expression` is in the Gregorian
calendar year 2015. However, `DATE_TRUNC` with the `ISOYEAR` date part
truncates the `date_expression` to the beginning of the ISO year, not the
Gregorian calendar year. The first Thursday of the 2015 calendar year was
2015-01-01, so the ISO year 2015 begins on the preceding Monday, 2014-12-29.
Therefore the ISO year boundary preceding the `date_expression` 2015-06-15 is
2014-12-29.

```googlesql
SELECT
  DATE_TRUNC('2015-06-15', ISOYEAR) AS isoyear_boundary,
  EXTRACT(ISOYEAR FROM DATE '2015-06-15') AS isoyear_number;

/*------------------+----------------+
 | isoyear_boundary | isoyear_number |
 +------------------+----------------+
 | 2014-12-29       | 2015           |
 +------------------+----------------*/
```

[date-trunc-granularity-date]: #date_trunc_granularity_date

[date-trunc-granularity-time]: #date_trunc_granularity_time

[date-time-zone-parts]: #date_time_zone_parts

## `EXTRACT`

```googlesql
EXTRACT(part FROM date_expression)
```

**Description**

Returns the value corresponding to the specified date part. The `part` must
be one of:

+   `DAYOFWEEK`: Returns values in the range [1,7] with Sunday as the first day
    of the week.
+   `DAY`
+   `DAYOFYEAR`
+ `WEEK`: Returns the week number of the date in the range [0, 53]. Weeks begin
  with Sunday, and dates prior to the first Sunday of the year are in week
  0.
+ `WEEK(<WEEKDAY>)`: Returns the week number of the date in the range [0, 53].
  Weeks begin on `WEEKDAY`. Dates prior to
  the first `WEEKDAY` of the year are in week 0. Valid values for `WEEKDAY` are
  `SUNDAY`, `MONDAY`, `TUESDAY`, `WEDNESDAY`, `THURSDAY`, `FRIDAY`, and
  `SATURDAY`.
+ `ISOWEEK`: Returns the [ISO 8601 week][ISO-8601-week]
  number of the `date_expression`. `ISOWEEK`s begin on Monday. Return values
  are in the range [1, 53]. The first `ISOWEEK` of each ISO year begins on the
  Monday before the first Thursday of the Gregorian calendar year.
+   `MONTH`
+   `QUARTER`: Returns values in the range [1,4].
+   `YEAR`
+   `ISOYEAR`: Returns the [ISO 8601][ISO-8601]
    week-numbering year, which is the Gregorian calendar year containing the
    Thursday of the week to which `date_expression` belongs.

**Return Data Type**

INT64

**Examples**

In the following example, `EXTRACT` returns a value corresponding to the `DAY`
date part.

```googlesql
SELECT EXTRACT(DAY FROM DATE '2013-12-25') AS the_day;

/*---------+
 | the_day |
 +---------+
 | 25      |
 +---------*/
```

In the following example, `EXTRACT` returns values corresponding to different
date parts from a column of dates near the end of the year.

```googlesql
SELECT
  date,
  EXTRACT(ISOYEAR FROM date) AS isoyear,
  EXTRACT(ISOWEEK FROM date) AS isoweek,
  EXTRACT(YEAR FROM date) AS year,
  EXTRACT(WEEK FROM date) AS week
FROM UNNEST(GENERATE_DATE_ARRAY('2015-12-23', '2016-01-09')) AS date
ORDER BY date;

/*------------+---------+---------+------+------+
 | date       | isoyear | isoweek | year | week |
 +------------+---------+---------+------+------+
 | 2015-12-23 | 2015    | 52      | 2015 | 51   |
 | 2015-12-24 | 2015    | 52      | 2015 | 51   |
 | 2015-12-25 | 2015    | 52      | 2015 | 51   |
 | 2015-12-26 | 2015    | 52      | 2015 | 51   |
 | 2015-12-27 | 2015    | 52      | 2015 | 52   |
 | 2015-12-28 | 2015    | 53      | 2015 | 52   |
 | 2015-12-29 | 2015    | 53      | 2015 | 52   |
 | 2015-12-30 | 2015    | 53      | 2015 | 52   |
 | 2015-12-31 | 2015    | 53      | 2015 | 52   |
 | 2016-01-01 | 2015    | 53      | 2016 | 0    |
 | 2016-01-02 | 2015    | 53      | 2016 | 0    |
 | 2016-01-03 | 2015    | 53      | 2016 | 1    |
 | 2016-01-04 | 2016    | 1       | 2016 | 1    |
 | 2016-01-05 | 2016    | 1       | 2016 | 1    |
 | 2016-01-06 | 2016    | 1       | 2016 | 1    |
 | 2016-01-07 | 2016    | 1       | 2016 | 1    |
 | 2016-01-08 | 2016    | 1       | 2016 | 1    |
 | 2016-01-09 | 2016    | 1       | 2016 | 1    |
 +------------+---------+---------+------+------*/
```

In the following example, `date_expression` falls on a Sunday. `EXTRACT`
calculates the first column using weeks that begin on Sunday, and it calculates
the second column using weeks that begin on Monday.

```googlesql
WITH table AS (SELECT DATE('2017-11-05') AS date)
SELECT
  date,
  EXTRACT(WEEK(SUNDAY) FROM date) AS week_sunday,
  EXTRACT(WEEK(MONDAY) FROM date) AS week_monday FROM table;

/*------------+-------------+-------------+
 | date       | week_sunday | week_monday |
 +------------+-------------+-------------+
 | 2017-11-05 | 45          | 44          |
 +------------+-------------+-------------*/
```

[ISO-8601]: https://en.wikipedia.org/wiki/ISO_8601

[ISO-8601-week]: https://en.wikipedia.org/wiki/ISO_week_date

## `FORMAT_DATE`

```googlesql
FORMAT_DATE(format_string, date_expr)
```

**Description**

Formats a `DATE` value according to a specified format string.

**Definitions**

+   `format_string`: A `STRING` value that contains the
    [format elements][date-format-elements] to use with `date_expr`.
+   `date_expr`: A `DATE` value that represents the date to format.

**Return Data Type**

`STRING`

**Examples**

```googlesql
SELECT FORMAT_DATE('%x', DATE '2008-12-25') AS US_format;

/*------------+
 | US_format  |
 +------------+
 | 12/25/08   |
 +------------*/
```

```googlesql
SELECT FORMAT_DATE('%b-%d-%Y', DATE '2008-12-25') AS formatted;

/*-------------+
 | formatted   |
 +-------------+
 | Dec-25-2008 |
 +-------------*/
```

```googlesql
SELECT FORMAT_DATE('%b %Y', DATE '2008-12-25') AS formatted;

/*-------------+
 | formatted   |
 +-------------+
 | Dec 2008    |
 +-------------*/
```

[date-format-elements]: https://github.com/google/googlesql/blob/master/docs/format-elements.md#format_elements_date_time

## `LAST_DAY`

```googlesql
LAST_DAY(date_expression[, date_part])
```

**Description**

Returns the last day from a date expression. This is commonly used to return
the last day of the month.

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
SELECT LAST_DAY(DATE '2008-11-25', MONTH) AS last_day

/*------------+
 | last_day   |
 +------------+
 | 2008-11-30 |
 +------------*/
```

```googlesql
SELECT LAST_DAY(DATE '2008-11-25') AS last_day

/*------------+
 | last_day   |
 +------------+
 | 2008-11-30 |
 +------------*/
```

This returns the last day of the year:

```googlesql
SELECT LAST_DAY(DATE '2008-11-25', YEAR) AS last_day

/*------------+
 | last_day   |
 +------------+
 | 2008-12-31 |
 +------------*/
```

This returns the last day of the week for a week that starts on a Sunday:

```googlesql
SELECT LAST_DAY(DATE '2008-11-10', WEEK(SUNDAY)) AS last_day

/*------------+
 | last_day   |
 +------------+
 | 2008-11-15 |
 +------------*/
```

This returns the last day of the week for a week that starts on a Monday:

```googlesql
SELECT LAST_DAY(DATE '2008-11-10', WEEK(MONDAY)) AS last_day

/*------------+
 | last_day   |
 +------------+
 | 2008-11-16 |
 +------------*/
```

[ISO-8601]: https://en.wikipedia.org/wiki/ISO_8601

[ISO-8601-week]: https://en.wikipedia.org/wiki/ISO_week_date

## `PARSE_DATE`

```googlesql
PARSE_DATE(format_string, date_string)
```

**Description**

Converts a `STRING` value to a `DATE` value.

**Definitions**

+   `format_string`: A `STRING` value that contains the
    [format elements][date-format-elements] to use with `date_string`.
+   `date_string`: A `STRING` value that represents the date to parse.

**Details**

Each element in `date_string` must have a corresponding element in
`format_string`. The location of each element in `format_string` must match the
location of each element in `date_string`.

```googlesql
-- This works because elements on both sides match.
SELECT PARSE_DATE('%A %b %e %Y', 'Thursday Dec 25 2008');

-- This produces an error because the year element is in different locations.
SELECT PARSE_DATE('%Y %A %b %e', 'Thursday Dec 25 2008');

-- This produces an error because one of the year elements is missing.
SELECT PARSE_DATE('%A %b %e', 'Thursday Dec 25 2008');

-- This works because %F can find all matching elements in date_string.
SELECT PARSE_DATE('%F', '2000-12-30');
```

The following additional considerations apply when using the `PARSE_DATE`
function:

+ Unspecified fields. Any unspecified field is initialized from `1970-01-01`.
+ Case insensitivity. Names, such as `Monday`, `February`, and so on, are
  case insensitive.
+ Whitespace. One or more consecutive white spaces in the format string
  matches zero or more consecutive white spaces in the date string. In
  addition, leading and trailing white spaces in the date string are always
  allowed, even if they aren't in the format string.
+ Format precedence. When two (or more) format elements have overlapping
  information (for example both `%F` and `%Y` affect the year), the last one
  generally overrides any earlier ones.
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

`DATE`

**Examples**

This example converts a `MM/DD/YY` formatted string to a `DATE` object:

```googlesql
SELECT PARSE_DATE('%x', '12/25/08') AS parsed;

/*------------+
 | parsed     |
 +------------+
 | 2008-12-25 |
 +------------*/
```

This example converts a `YYYYMMDD` formatted string to a `DATE` object:

```googlesql
SELECT PARSE_DATE('%Y%m%d', '20081225') AS parsed;

/*------------+
 | parsed     |
 +------------+
 | 2008-12-25 |
 +------------*/
```

[date-format-elements]: https://github.com/google/googlesql/blob/master/docs/format-elements.md#format_elements_date_time

## `UNIX_DATE`

```googlesql
UNIX_DATE(date_expression)
```

**Description**

Returns the number of days since `1970-01-01`.

**Return Data Type**

INT64

**Example**

```googlesql
SELECT UNIX_DATE(DATE '2008-12-25') AS days_from_epoch;

/*-----------------+
 | days_from_epoch |
 +-----------------+
 | 14238           |
 +-----------------*/
```

