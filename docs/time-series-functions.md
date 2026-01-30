

<!-- mdlint off(WHITESPACE_LINE_LENGTH) -->

# Time series functions

GoogleSQL supports the following time series functions.

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
  <td><a href="https://github.com/google/googlesql/blob/master/docs/time-series-functions.md#date_bucket"><code>DATE_BUCKET</code></a>
</td>
  <td>
    Gets the lower bound of the date bucket that contains a date.
  </td>
</tr>

<tr>
  <td><a href="https://github.com/google/googlesql/blob/master/docs/time-series-functions.md#datetime_bucket"><code>DATETIME_BUCKET</code></a>
</td>
  <td>
    Gets the lower bound of the datetime bucket that contains a datetime.
  </td>
</tr>

<tr>
  <td><a href="https://github.com/google/googlesql/blob/master/docs/time-series-functions.md#timestamp_bucket"><code>TIMESTAMP_BUCKET</code></a>
</td>
  <td>
    Gets the lower bound of the timestamp bucket that contains a timestamp.
  </td>
</tr>

  </tbody>
</table>

## `DATE_BUCKET`

```googlesql
DATE_BUCKET(date_in_bucket, bucket_width)
```

```googlesql
DATE_BUCKET(date_in_bucket, bucket_width, bucket_origin_date)
```

**Description**

Gets the lower bound of the date bucket that contains a date.

**Definitions**

+   `date_in_bucket`: A `DATE` value that you can use to look up a date bucket.
+   `bucket_width`: An `INTERVAL` value that represents the width of
    a date bucket. A [single interval][interval-single] with
    [date parts][interval-parts] is supported.
+   `bucket_origin_date`: A `DATE` value that represents a point in time. All
    buckets expand left and right from this point. If this argument isn't set,
    `1950-01-01` is used by default.

**Return type**

`DATE`

**Examples**

In the following example, the origin is omitted and the default origin,
`1950-01-01` is used. All buckets expand in both directions from the origin,
and the size of each bucket is two days. The lower bound of the bucket in
which `my_date` belongs is returned.

```googlesql
WITH some_dates AS (
  SELECT DATE '1949-12-29' AS my_date UNION ALL
  SELECT DATE '1949-12-30' UNION ALL
  SELECT DATE '1949-12-31' UNION ALL
  SELECT DATE '1950-01-01' UNION ALL
  SELECT DATE '1950-01-02' UNION ALL
  SELECT DATE '1950-01-03'
)
SELECT DATE_BUCKET(my_date, INTERVAL 2 DAY) AS bucket_lower_bound
FROM some_dates;

/*--------------------+
 | bucket_lower_bound |
 +--------------------+
 | 1949-12-28         |
 | 1949-12-30         |
 | 1949-12-30         |
 | 1950-01-01         |
 | 1950-01-01         |
 | 1950-01-03         |
 +--------------------*/

-- Some date buckets that originate from 1950-01-01:
-- + Bucket: ...
-- + Bucket: [1949-12-28, 1949-12-30)
-- + Bucket: [1949-12-30, 1950-01-01)
-- + Origin: [1950-01-01]
-- + Bucket: [1950-01-01, 1950-01-03)
-- + Bucket: [1950-01-03, 1950-01-05)
-- + Bucket: ...
```

In the following example, the origin has been changed to `2000-12-24`,
and all buckets expand in both directions from this point. The size of each
bucket is seven days. The lower bound of the bucket in which `my_date` belongs
is returned:

```googlesql
WITH some_dates AS (
  SELECT DATE '2000-12-20' AS my_date UNION ALL
  SELECT DATE '2000-12-21' UNION ALL
  SELECT DATE '2000-12-22' UNION ALL
  SELECT DATE '2000-12-23' UNION ALL
  SELECT DATE '2000-12-24' UNION ALL
  SELECT DATE '2000-12-25'
)
SELECT DATE_BUCKET(
  my_date,
  INTERVAL 7 DAY,
  DATE '2000-12-24') AS bucket_lower_bound
FROM some_dates;

/*--------------------+
 | bucket_lower_bound |
 +--------------------+
 | 2000-12-17         |
 | 2000-12-17         |
 | 2000-12-17         |
 | 2000-12-17         |
 | 2000-12-24         |
 | 2000-12-24         |
 +--------------------*/

-- Some date buckets that originate from 2000-12-24:
-- + Bucket: ...
-- + Bucket: [2000-12-10, 2000-12-17)
-- + Bucket: [2000-12-17, 2000-12-24)
-- + Origin: [2000-12-24]
-- + Bucket: [2000-12-24, 2000-12-31)
-- + Bucket: [2000-12-31, 2000-01-07)
-- + Bucket: ...
```

[interval-single]: https://github.com/google/googlesql/blob/master/docs/data-types.md#single_datetime_part_interval

[interval-range]: https://github.com/google/googlesql/blob/master/docs/data-types.md#range_datetime_part_interval

[interval-parts]: https://github.com/google/googlesql/blob/master/docs/data-types.md#interval_datetime_parts

## `DATETIME_BUCKET`

```googlesql
DATETIME_BUCKET(datetime_in_bucket, bucket_width)
```

```googlesql
DATETIME_BUCKET(datetime_in_bucket, bucket_width, bucket_origin_datetime)
```

**Description**

Gets the lower bound of the datetime bucket that contains a datetime.

**Definitions**

+   `datetime_in_bucket`: A `DATETIME` value that you can use to look up a
    datetime bucket.
+   `bucket_width`: An `INTERVAL` value that represents the width of
    a datetime bucket. A [single interval][interval-single] with
    [date and time parts][interval-parts] is supported.
+   `bucket_origin_datetime`: A `DATETIME` value that represents a point in
    time. All buckets expand left and right from this point. If this argument
    isn't set, `1950-01-01 00:00:00` is used by default.

**Return type**

`DATETIME`

**Examples**

In the following example, the origin is omitted and the default origin,
`1950-01-01 00:00:00` is used. All buckets expand in both directions from the
origin, and the size of each bucket is 12 hours. The lower bound of the bucket
in which `my_datetime` belongs is returned:

```googlesql
WITH some_datetimes AS (
  SELECT DATETIME '1949-12-30 13:00:00' AS my_datetime UNION ALL
  SELECT DATETIME '1949-12-31 00:00:00' UNION ALL
  SELECT DATETIME '1949-12-31 13:00:00' UNION ALL
  SELECT DATETIME '1950-01-01 00:00:00' UNION ALL
  SELECT DATETIME '1950-01-01 13:00:00' UNION ALL
  SELECT DATETIME '1950-01-02 00:00:00'
)
SELECT DATETIME_BUCKET(my_datetime, INTERVAL 12 HOUR) AS bucket_lower_bound
FROM some_datetimes;

/*---------------------+
 | bucket_lower_bound  |
 +---------------------+
 | 1949-12-30 12:00:00 |
 | 1949-12-31 00:00:00 |
 | 1949-12-31 12:00:00 |
 | 1950-01-01 00:00:00 |
 | 1950-01-01 12:00:00 |
 | 1950-01-02 00:00:00 |
 +---------------------*/

-- Some datetime buckets that originate from 1950-01-01 00:00:00:
-- + Bucket: ...
-- + Bucket: [1949-12-30 00:00:00, 1949-12-30 12:00:00)
-- + Bucket: [1949-12-30 12:00:00, 1950-01-01 00:00:00)
-- + Origin: [1950-01-01 00:00:00]
-- + Bucket: [1950-01-01 00:00:00, 1950-01-01 12:00:00)
-- + Bucket: [1950-01-01 12:00:00, 1950-02-00 00:00:00)
-- + Bucket: ...
```

In the following example, the origin has been changed to `2000-12-24 12:00:00`,
and all buckets expand in both directions from this point. The size of each
bucket is seven days. The lower bound of the bucket in which `my_datetime`
belongs is returned:

```googlesql
WITH some_datetimes AS (
  SELECT DATETIME '2000-12-20 00:00:00' AS my_datetime UNION ALL
  SELECT DATETIME '2000-12-21 00:00:00' UNION ALL
  SELECT DATETIME '2000-12-22 00:00:00' UNION ALL
  SELECT DATETIME '2000-12-23 00:00:00' UNION ALL
  SELECT DATETIME '2000-12-24 00:00:00' UNION ALL
  SELECT DATETIME '2000-12-25 00:00:00'
)
SELECT DATETIME_BUCKET(
  my_datetime,
  INTERVAL 7 DAY,
  DATETIME '2000-12-22 12:00:00') AS bucket_lower_bound
FROM some_datetimes;

/*--------------------+
 | bucket_lower_bound |
 +--------------------+
 | 2000-12-15 12:00:00 |
 | 2000-12-15 12:00:00 |
 | 2000-12-15 12:00:00 |
 | 2000-12-22 12:00:00 |
 | 2000-12-22 12:00:00 |
 | 2000-12-22 12:00:00 |
 +--------------------*/

-- Some datetime buckets that originate from 2000-12-22 12:00:00:
-- + Bucket: ...
-- + Bucket: [2000-12-08 12:00:00, 2000-12-15 12:00:00)
-- + Bucket: [2000-12-15 12:00:00, 2000-12-22 12:00:00)
-- + Origin: [2000-12-22 12:00:00]
-- + Bucket: [2000-12-22 12:00:00, 2000-12-29 12:00:00)
-- + Bucket: [2000-12-29 12:00:00, 2000-01-05 12:00:00)
-- + Bucket: ...
```

[interval-single]: https://github.com/google/googlesql/blob/master/docs/data-types.md#single_datetime_part_interval

[interval-parts]: https://github.com/google/googlesql/blob/master/docs/data-types.md#interval_datetime_parts

## `TIMESTAMP_BUCKET`

```googlesql
TIMESTAMP_BUCKET(timestamp_in_bucket, bucket_width)
```

```googlesql
TIMESTAMP_BUCKET(timestamp_in_bucket, bucket_width, bucket_origin_timestamp)
```

**Description**

Gets the lower bound of the timestamp bucket that contains a timestamp.

**Definitions**

+   `timestamp_in_bucket`: A `TIMESTAMP` value that you can use to look up a
    timestamp bucket.
+   `bucket_width`: An `INTERVAL` value that represents the width of
    a timestamp bucket. A [single interval][interval-single] with
    [date and time parts][interval-parts] is supported.
+   `bucket_origin_timestamp`: A `TIMESTAMP` value that represents a point in
    time. All buckets expand left and right from this point. If this argument
    isn't set, `1950-01-01 00:00:00` is used by default.

**Return type**

`TIMESTAMP`

**Examples**

In the following example, the origin is omitted and the default origin,
`1950-01-01 00:00:00` is used. All buckets expand in both directions from the
origin, and the size of each bucket is 12 hours. The default time zone,
which is implementation defined, is included in the results. The lower bound of the
bucket in which `my_timestamp` belongs is returned:

```googlesql
WITH some_timestamps AS (
  SELECT TIMESTAMP '1949-12-30 13:00:00.00' AS my_timestamp UNION ALL
  SELECT TIMESTAMP '1949-12-31 00:00:00.00' UNION ALL
  SELECT TIMESTAMP '1949-12-31 13:00:00.00' UNION ALL
  SELECT TIMESTAMP '1950-01-01 00:00:00.00' UNION ALL
  SELECT TIMESTAMP '1950-01-01 13:00:00.00' UNION ALL
  SELECT TIMESTAMP '1950-01-02 00:00:00.00'
)
SELECT TIMESTAMP_BUCKET(my_timestamp, INTERVAL 12 HOUR) AS bucket_lower_bound
FROM some_timestamps;

-- Display of results may differ, depending upon the environment and
-- time zone where this query was executed.
/*---------------------------------------------+
 | bucket_lower_bound                          |
 +---------------------------------------------+
 | 1949-12-30 12:00:00.000 America/Los_Angeles |
 | 1949-12-31 00:00:00.000 America/Los_Angeles |
 | 1949-12-31 12:00:00.000 America/Los_Angeles |
 | 1950-01-01 00:00:00.000 America/Los_Angeles |
 | 1950-01-01 12:00:00.000 America/Los_Angeles |
 | 1950-01-02 00:00:00.000 America/Los_Angeles |
 +---------------------------------------------*/

-- Some timestamp buckets that originate from 1950-01-01 00:00:00:
-- + Bucket: ...
-- + Bucket: [1949-12-30 00:00:00.00 UTC, 1949-12-30 12:00:00.00 UTC)
-- + Bucket: [1949-12-30 12:00:00.00 UTC, 1950-01-01 00:00:00.00 UTC)
-- + Origin: [1950-01-01 00:00:00.00 UTC]
-- + Bucket: [1950-01-01 00:00:00.00 UTC, 1950-01-01 12:00:00.00 UTC)
-- + Bucket: [1950-01-01 12:00:00.00 UTC, 1950-02-00 00:00:00.00 UTC)
-- + Bucket: ...
```

In the following example, the origin has been changed to `2000-12-24 12:00:00`,
and all buckets expand in both directions from this point. The size of each
bucket is seven days. The default time zone, which is implementation defined, is included
in the results. The lower bound of the bucket in which `my_timestamp`
belongs is returned:

```googlesql
WITH some_timestamps AS (
  SELECT TIMESTAMP '2000-12-20 00:00:00.00' AS my_timestamp UNION ALL
  SELECT TIMESTAMP '2000-12-21 00:00:00.00' UNION ALL
  SELECT TIMESTAMP '2000-12-22 00:00:00.00' UNION ALL
  SELECT TIMESTAMP '2000-12-23 00:00:00.00' UNION ALL
  SELECT TIMESTAMP '2000-12-24 00:00:00.00' UNION ALL
  SELECT TIMESTAMP '2000-12-25 00:00:00.00'
)
SELECT TIMESTAMP_BUCKET(
  my_timestamp,
  INTERVAL 7 DAY,
  TIMESTAMP '2000-12-22 12:00:00.00') AS bucket_lower_bound
FROM some_timestamps;

-- Display of results may differ, depending upon the environment and
-- time zone where this query was executed.
/*---------------------------------------------+
 | bucket_lower_bound                          |
 +---------------------------------------------+
 | 2000-12-15 12:00:00.000 America/Los_Angeles |
 | 2000-12-15 12:00:00.000 America/Los_Angeles |
 | 2000-12-15 12:00:00.000 America/Los_Angeles |
 | 2000-12-22 12:00:00.000 America/Los_Angeles |
 | 2000-12-22 12:00:00.000 America/Los_Angeles |
 | 2000-12-22 12:00:00.000 America/Los_Angeles |
 +---------------------------------------------*/

-- Some timestamp buckets that originate from 2000-12-22 12:00:00:
-- + Bucket: ...
-- + Bucket: [2000-12-08 12:00:00.00 UTC, 2000-12-15 12:00:00.00 UTC)
-- + Bucket: [2000-12-15 12:00:00.00 UTC, 2000-12-22 12:00:00.00 UTC)
-- + Origin: [2000-12-22 12:00:00.00 UTC]
-- + Bucket: [2000-12-22 12:00:00.00 UTC, 2000-12-29 12:00:00.00 UTC)
-- + Bucket: [2000-12-29 12:00:00.00 UTC, 2000-01-05 12:00:00.00 UTC)
-- + Bucket: ...
```

[interval-single]: https://github.com/google/googlesql/blob/master/docs/data-types.md#single_datetime_part_interval

[interval-parts]: https://github.com/google/googlesql/blob/master/docs/data-types.md#interval_datetime_parts

