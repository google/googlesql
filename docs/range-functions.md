

<!-- mdlint off(WHITESPACE_LINE_LENGTH) -->

# Range functions

GoogleSQL supports the following range functions.

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
  <td><a href="https://github.com/google/googlesql/blob/master/docs/range-functions.md#generate_range_array"><code>GENERATE_RANGE_ARRAY</code></a>
</td>
  <td>
    Splits a range into an array of subranges.
    
  </td>
</tr>

<tr>
  <td><a href="https://github.com/google/googlesql/blob/master/docs/range-functions.md#range"><code>RANGE</code></a>
</td>
  <td>
    Constructs a range of <code>DATE</code>, <code>DATETIME</code>,
    or <code>TIMESTAMP</code> values.
  </td>
</tr>

<tr>
  <td><a href="https://github.com/google/googlesql/blob/master/docs/mathematical_functions.md#range_bucket"><code>RANGE_BUCKET</code></a>
</td>
  <td>
    Scans through a sorted array and returns the 0-based position
    of a point's upper bound.
    <br>For more information, see <a href="https://github.com/google/googlesql/blob/master/docs/mathematical_functions.md">Mathematical functions</a>.

  </td>
</tr>

<tr>
  <td><a href="https://github.com/google/googlesql/blob/master/docs/range-functions.md#range_contains"><code>RANGE_CONTAINS</code></a>
</td>
  <td>
  Signature 1: Checks if one range is in another range.
  <br /><br />
  Signature 2: Checks if a value is in a range.
  </td>
</tr>

<tr>
  <td><a href="https://github.com/google/googlesql/blob/master/docs/range-functions.md#range_end"><code>RANGE_END</code></a>
</td>
  <td>Gets the upper bound of a range.</td>
</tr>

<tr>
  <td><a href="https://github.com/google/googlesql/blob/master/docs/range-functions.md#range_intersect"><code>RANGE_INTERSECT</code></a>
</td>
  <td>Gets a segment of two ranges that intersect.</td>
</tr>

<tr>
  <td><a href="https://github.com/google/googlesql/blob/master/docs/range-functions.md#range_overlaps"><code>RANGE_OVERLAPS</code></a>
</td>
  <td>Checks if two ranges overlap.</td>
</tr>

<tr>
  <td><a href="https://github.com/google/googlesql/blob/master/docs/range-functions.md#range_sessionize"><code>RANGE_SESSIONIZE</code></a>
</td>
  <td>
    Produces a table of sessionized ranges.
    
  </td>
</tr>

<tr>
  <td><a href="https://github.com/google/googlesql/blob/master/docs/range-functions.md#range_start"><code>RANGE_START</code></a>
</td>
  <td>Gets the lower bound of a range.</td>
</tr>

  </tbody>
</table>

## `GENERATE_RANGE_ARRAY`

```googlesql
GENERATE_RANGE_ARRAY(range_to_split, step_interval)
```

```googlesql
GENERATE_RANGE_ARRAY(range_to_split, step_interval, include_last_partial_range)
```

**Description**

Splits a range into an array of subranges.

**Definitions**

+   `range_to_split`: The `RANGE<T>` value to split.
+   `step_interval`: The `INTERVAL` value, which determines the maximum size of
    each subrange in the resulting array. An
    [interval single date and time part][interval-single]
    is supported, but an interval range of date and time parts isn't.

    +   If `range_to_split` is `RANGE<DATE>`, these interval
        date parts are supported: `YEAR` to `DAY`.

    +   If `range_to_split` is `RANGE<DATETIME>`, these interval
        date and time parts are supported: `YEAR` to `SECOND`.

    +   If `range_to_split` is `RANGE<TIMESTAMP>`, these interval
        date and time parts are supported: `DAY` to `SECOND`.
+   `include_last_partial_range`: A `BOOL` value, which determines whether or
    not to include the last subrange if it's a partial subrange.
    If this argument isn't specified, the default value is `TRUE`.

    +   `TRUE` (default): The last subrange is included, even if it's
        smaller than `step_interval`.

    +   `FALSE`: Exclude the last subrange if it's smaller than
        `step_interval`.

**Details**

Returns `NULL` if any input is` NULL`.

**Return type**

`ARRAY<RANGE<T>>`

**Examples**

In the following example, a date range between `2020-01-01` and `2020-01-06`
is split into an array of subranges that are one day long. There are
no partial ranges.

```googlesql
SELECT GENERATE_RANGE_ARRAY(
  RANGE(DATE '2020-01-01', DATE '2020-01-06'),
  INTERVAL 1 DAY) AS results;

/*----------------------------+
 | results                    |
 +----------------------------+
 | [                          |
 |  [2020-01-01, 2020-01-02), |
 |  [2020-01-02, 2020-01-03), |
 |  [2020-01-03, 2020-01-04), |
 |  [2020-01-04, 2020-01-05), |
 |  [2020-01-05, 2020-01-06), |
 | ]                          |
 +----------------------------*/
```

In the following examples, a date range between `2020-01-01` and `2020-01-06`
is split into an array of subranges that are two days long. The final subrange
is smaller than two days:

```googlesql
SELECT GENERATE_RANGE_ARRAY(
  RANGE(DATE '2020-01-01', DATE '2020-01-06'),
  INTERVAL 2 DAY) AS results;

/*----------------------------+
 | results                    |
 +----------------------------+
 | [                          |
 |  [2020-01-01, 2020-01-03), |
 |  [2020-01-03, 2020-01-05), |
 |  [2020-01-05, 2020-01-06)  |
 | ]                          |
 +----------------------------*/
```

```googlesql
SELECT GENERATE_RANGE_ARRAY(
  RANGE(DATE '2020-01-01', DATE '2020-01-06'),
  INTERVAL 2 DAY,
  TRUE) AS results;

/*----------------------------+
 | results                    |
 +----------------------------+
 | [                          |
 |  [2020-01-01, 2020-01-03), |
 |  [2020-01-03, 2020-01-05), |
 |  [2020-01-05, 2020-01-06)  |
 | ]                          |
 +----------------------------*/
```

In the following example, a date range between `2020-01-01` and `2020-01-06`
is split into an array of subranges that are two days long, but the final
subrange is excluded because it's smaller than two days:

```googlesql
SELECT GENERATE_RANGE_ARRAY(
  RANGE(DATE '2020-01-01', DATE '2020-01-06'),
  INTERVAL 2 DAY,
  FALSE) AS results;

/*----------------------------+
 | results                    |
 +----------------------------+
 | [                          |
 |  [2020-01-01, 2020-01-03), |
 |  [2020-01-03, 2020-01-05)  |
 | ]                          |
 +----------------------------*/
```

[interval-single]: https://github.com/google/googlesql/blob/master/docs/data-types.md#single_datetime_part_interval

## `RANGE`

```googlesql
RANGE(lower_bound, upper_bound)
```

**Description**

Constructs a range of [`DATE`][date-type], [`DATETIME`][datetime-type], or
[`TIMESTAMP`][timestamp-type] values.

**Definitions**

+   `lower_bound`: The range starts from this value. This can be a
    `DATE`, `DATETIME`, or `TIMESTAMP` value. If this value is `NULL`, the range
    doesn't include a lower bound.
+   `upper_bound`: The range ends before this value. This can be a
    `DATE`, `DATETIME`, or `TIMESTAMP` value. If this value is `NULL`, the range
    doesn't include an upper bound.

**Details**

`lower_bound` and `upper_bound` must be of the same data type.

Produces an error if `lower_bound` is greater than or equal to `upper_bound`.
To return `NULL` instead, add the `SAFE.` prefix to the function name.

**Return type**

`RANGE<T>`, where `T` is the same data type as the input.

**Examples**

The following query constructs a date range:

```googlesql
SELECT RANGE(DATE '2022-12-01', DATE '2022-12-31') AS results;

/*--------------------------+
 | results                  |
 +--------------------------+
 | [2022-12-01, 2022-12-31) |
 +--------------------------*/
```

The following query constructs a datetime range:

```googlesql
SELECT RANGE(DATETIME '2022-10-01 14:53:27',
             DATETIME '2022-10-01 16:00:00') AS results;

/*---------------------------------------------+
 | results                                     |
 +---------------------------------------------+
 | [2022-10-01 14:53:27, 2022-10-01T16:00:00)  |
 +---------------------------------------------*/
```

The following query constructs a timestamp range:

```googlesql
SELECT RANGE(TIMESTAMP '2022-10-01 14:53:27 America/Los_Angeles',
             TIMESTAMP '2022-10-01 16:00:00 America/Los_Angeles') AS results;

-- Results depend upon where this query was executed.
/*-----------------------------------------------------------------+
 | results                                                         |
 +-----------------------------------------------------------------+
 | [2022-10-01 21:53:27.000000+00, 2022-10-01 23:00:00.000000+00)  |
 +-----------------------------------------------------------------*/
```

The following query constructs a date range with no lower bound:

```googlesql
SELECT RANGE(NULL, DATE '2022-12-31') AS results;

/*-------------------------+
 | results                 |
 +-------------------------+
 | [UNBOUNDED, 2022-12-31) |
 +-------------------------*/
```

The following query constructs a date range with no upper bound:

```googlesql
SELECT RANGE(DATE '2022-10-01', NULL) AS results;

/*--------------------------+
 | results                  |
 +--------------------------+
 | [2022-10-01, UNBOUNDED)  |
 +--------------------------*/
```

[timestamp-type]: https://github.com/google/googlesql/blob/master/docs/data-types.md#timestamp_type

[date-type]: https://github.com/google/googlesql/blob/master/docs/data-types.md#date_type

[datetime-type]: https://github.com/google/googlesql/blob/master/docs/data-types.md#datetime_type

## `RANGE_CONTAINS`

+   [Signature 1][range_contains-sig1]: Checks if every value in one range is
    in another range.
+   [Signature 2][range_contains-sig2]: Checks if a value is in a range.

#### Signature 1

```googlesql
RANGE_CONTAINS(outer_range, inner_range)
```

**Description**

Checks if the inner range is in the outer range.

**Definitions**

+   `outer_range`: The `RANGE<T>` value to search within.
+   `inner_range`: The `RANGE<T>` value to search for in `outer_range`.

**Details**

Returns `TRUE` if `inner_range` exists in `outer_range`.
Otherwise, returns `FALSE`.

`T` must be of the same type for all inputs.

**Return type**

`BOOL`

**Examples**

In the following query, the inner range is in the outer range:

```googlesql
SELECT RANGE_CONTAINS(
  RANGE<DATE> '[2022-01-01, 2023-01-01)',
  RANGE<DATE> '[2022-04-01, 2022-07-01)') AS results;

/*---------+
 | results |
 +---------+
 | TRUE    |
 +---------*/
```

In the following query, the inner range isn't in the outer range:

```googlesql
SELECT RANGE_CONTAINS(
  RANGE<DATE> '[2022-01-01, 2023-01-01)',
  RANGE<DATE> '[2023-01-01, 2023-04-01)') AS results;

/*---------+
 | results |
 +---------+
 | FALSE   |
 +---------*/
```

#### Signature 2

```googlesql
RANGE_CONTAINS(range_to_search, value_to_find)
```

**Description**

Checks if a value is in a range.

**Definitions**

+   `range_to_search`: The `RANGE<T>` value to search within.
+   `value_to_find`: The value to search for in `range_to_search`.

**Details**

Returns `TRUE` if `value_to_find` exists in `range_to_search`.
Otherwise, returns `FALSE`.

The data type for `value_to_find` must be the same data type as `T`in
`range_to_search`.

**Return type**

`BOOL`

**Examples**

In the following query, the value `2022-04-01` is found in the range
`[2022-01-01, 2023-01-01)`:

```googlesql
SELECT RANGE_CONTAINS(
  RANGE<DATE> '[2022-01-01, 2023-01-01)',
  DATE '2022-04-01') AS results;

/*---------+
 | results |
 +---------+
 | TRUE    |
 +---------*/
```

In the following query, the value `2023-04-01` isn't found in the range
`[2022-01-01, 2023-01-01)`:

```googlesql
SELECT RANGE_CONTAINS(
  RANGE<DATE> '[2022-01-01, 2023-01-01)',
  DATE '2023-04-01') AS results;

/*---------+
 | results |
 +---------+
 | FALSE   |
 +---------*/
```

[range_contains-sig1]: #signature_1

[range_contains-sig2]: #signature_2

## `RANGE_END`

```googlesql
RANGE_END(range_to_check)
```

**Description**

Gets the upper bound of a range.

**Definitions**

+   `range_to_check`: The `RANGE<T>` value.

**Details**

Returns `NULL` if the upper bound in `range_value` is `UNBOUNDED`.

Returns `NULL` if `range_to_check` is `NULL`.

**Return type**

`T` in `range_value`

**Examples**

In the following query, the upper bound of the range is retrieved:

```googlesql
SELECT RANGE_END(RANGE<DATE> '[2022-12-01, 2022-12-31)') AS results;

/*------------+
 | results    |
 +------------+
 | 2022-12-31 |
 +------------*/
```

In the following query, the upper bound of the range is unbounded, so
`NULL` is returned:

```googlesql
SELECT RANGE_END(RANGE<DATE> '[2022-12-01, UNBOUNDED)') AS results;

/*------------+
 | results    |
 +------------+
 | NULL       |
 +------------*/
```

## `RANGE_INTERSECT`

```googlesql
RANGE_INTERSECT(range_a, range_b)
```

**Description**

Gets a segment of two ranges that intersect.

**Definitions**

+   `range_a`: The first `RANGE<T>` value.
+   `range_b`: The second `RANGE<T>` value.

**Details**

Returns `NULL` if any input is` NULL`.

Produces an error if `range_a` and `range_b` don't overlap. To return
`NULL` instead, add the `SAFE.` prefix to the function name.

`T` must be of the same type for all inputs.

**Return type**

`RANGE<T>`

**Examples**

```googlesql
SELECT RANGE_INTERSECT(
  RANGE<DATE> '[2022-02-01, 2022-09-01)',
  RANGE<DATE> '[2021-06-15, 2022-04-15)') AS results;

/*--------------------------+
 | results                  |
 +--------------------------+
 | [2022-02-01, 2022-04-15) |
 +--------------------------*/
```

```googlesql
SELECT RANGE_INTERSECT(
  RANGE<DATE> '[2022-02-01, UNBOUNDED)',
  RANGE<DATE> '[2021-06-15, 2022-04-15)') AS results;

/*--------------------------+
 | results                  |
 +--------------------------+
 | [2022-02-01, 2022-04-15) |
 +--------------------------*/
```

```googlesql
SELECT RANGE_INTERSECT(
  RANGE<DATE> '[2022-02-01, UNBOUNDED)',
  RANGE<DATE> '[2021-06-15, UNBOUNDED)') AS results;

/*-------------------------+
 | results                 |
 +-------------------------+
 | [2022-02-01, UNBOUNDED) |
 +-------------------------*/
```

## `RANGE_OVERLAPS`

```googlesql
RANGE_OVERLAPS(range_a, range_b)
```

**Description**

Checks if two ranges overlap.

**Definitions**

+   `range_a`: The first `RANGE<T>` value.
+   `range_b`: The second `RANGE<T>` value.

**Details**

Returns `TRUE` if a part of `range_a` intersects with `range_b`, otherwise
returns `FALSE`.

`T` must be of the same type for all inputs.

To get the part of the range that overlaps, use the
[`RANGE_INTERSECT`][range-intersect] function.

**Return type**

`BOOL`

**Examples**

In the following query, the first and second ranges overlap between
`2022-02-01` and `2022-04-15`:

```googlesql
SELECT RANGE_OVERLAPS(
  RANGE<DATE> '[2022-02-01, 2022-09-01)',
  RANGE<DATE> '[2021-06-15, 2022-04-15)') AS results;

/*---------+
 | results |
 +---------+
 | TRUE    |
 +---------*/
```

In the following query, the first and second ranges don't overlap:

```googlesql
SELECT RANGE_OVERLAPS(
  RANGE<DATE> '[2020-02-01, 2020-09-01)',
  RANGE<DATE> '[2021-06-15, 2022-04-15)') AS results;

/*---------+
 | results |
 +---------+
 | FALSE   |
 +---------*/
```

In the following query, the first and second ranges overlap between
`2022-02-01` and `UNBOUNDED`:

```googlesql
SELECT RANGE_OVERLAPS(
  RANGE<DATE> '[2022-02-01, UNBOUNDED)',
  RANGE<DATE> '[2021-06-15, UNBOUNDED)') AS results;

/*---------+
 | results |
 +---------+
 | TRUE    |
 +---------*/
```

[range-intersect]: #range_intersect

## `RANGE_SESSIONIZE`

```googlesql
RANGE_SESSIONIZE(
  TABLE table_name,
  range_column,
  partitioning_columns
)
```

```googlesql
RANGE_SESSIONIZE(
  TABLE table_name,
  range_column,
  partitioning_columns,
  sessionize_option
)
```

**Description**

Produces a table of sessionized ranges.

**Definitions**

+   `table_name`: A table expression that represents the name of the table to
    construct. This can represent any relation with `range_column`.
+   `range_column`: A `STRING` literal that indicates which `RANGE` column
    in a table contains the data to sessionize.
+   `partitioning_columns`: An `ARRAY<STRING>` literal that indicates which
    columns should partition the data before the data is sessionized.
+   `sessionize_option`: A `STRING` value that describes how order-adjacent
    ranges are sessionized. Your choices are as follows:

    +   `MEETS` (default): Ranges that meet or overlap are sessionized.

    +   `OVERLAPS`: Only a range that's overlapped by another range is
        sessionized.

    If this argument isn't provided, `MEETS` is used by default.

**Details**

This function produces a table that includes all columns in the
input table and an additional `RANGE` column called
`session_range`, which indicates the start and end of a session. The
start and end of each session is determined by the `sessionize_option`
argument.

**Return type**

`TABLE`

**Examples**

The examples in this section reference the following table called
`my_sessionized_range_table` in a dataset called `mydataset`:

```googlesql
INSERT mydataset.my_sessionized_range_table (emp_id, dept_id, duration)
VALUES(10, 1000, RANGE<DATE> '[2010-01-10, 2010-03-10)'),
      (10, 2000, RANGE<DATE> '[2010-03-10, 2010-07-15)'),
      (10, 2000, RANGE<DATE> '[2010-06-15, 2010-08-18)'),
      (20, 2000, RANGE<DATE> '[2010-03-10, 2010-07-20)'),
      (20, 1000, RANGE<DATE> '[2020-05-10, 2020-09-20)');

SELECT * FROM mydataset.my_sessionized_range_table ORDER BY emp_id;

/*--------+---------+--------------------------+
 | emp_id | dept_id | duration                 |
 +--------+---------+--------------------------+
 | 10     | 1000    | [2010-01-10, 2010-03-10) |
 | 10     | 2000    | [2010-03-10, 2010-07-15) |
 | 10     | 2000    | [2010-06-15, 2010-08-18) |
 | 20     | 2000    | [2010-03-10, 2010-07-20) |
 | 20     | 1000    | [2020-05-10, 2020-09-20) |
 +--------+---------+--------------------------*/
```

In the following query, a table of sessionized data is produced for
`my_sessionized_range_table`, and only ranges that meet or overlap are
sessionized:

```googlesql
SELECT
  emp_id, duration, session_range
FROM
  RANGE_SESSIONIZE(
    TABLE mydataset.my_sessionized_range_table,
    'duration',
    ['emp_id'])
ORDER BY emp_id;

/*--------+--------------------------+--------------------------+
 | emp_id | duration                 | session_range            |
 +--------+--------------------------+--------------------------+
 | 10     | [2010-01-10, 2010-03-10) | [2010-01-10, 2010-08-18) |
 | 10     | [2010-03-10, 2010-07-15) | [2010-01-10, 2010-08-18) |
 | 10     | [2010-06-15, 2010-08-18) | [2010-01-10, 2010-08-18) |
 | 20     | [2010-03-10, 2010-07-20) | [2010-03-10, 2010-07-20) |
 | 20     | [2020-05-10, 2020-09-20) | [2020-05-10, 2020-09-20) |
 +--------+-----------------------------------------------------*/
```

In the following query, a table of sessionized data is produced for
`my_sessionized_range_table`, and only a range that's overlapped by another
range is sessionized:

```googlesql
SELECT
  emp_id, duration, session_range
FROM
  RANGE_SESSIONIZE(
    TABLE mydataset.my_sessionized_range_table,
    'duration',
    ['emp_id'],
    'OVERLAPS')
ORDER BY emp_id;

/*--------+--------------------------+--------------------------+
 | emp_id | duration                 | session_range            |
 +--------+--------------------------+--------------------------+
 | 10     | [2010-03-10, 2010-07-15) | [2010-03-10, 2010-08-18) |
 | 10     | [2010-06-15, 2010-08-18) | [2010-03-10, 2010-08-18) |
 | 10     | [2010-01-10, 2010-03-10) | [2010-01-10, 2010-03-10) |
 | 20     | [2020-05-10, 2020-09-20) | [2020-05-10, 2020-09-20) |
 | 20     | [2010-03-10, 2010-07-20) | [2010-03-10, 2010-07-20) |
 +--------+-----------------------------------------------------*/
```

If you need to normalize sessionized data, you can use a query similar to the
following:

```googlesql
SELECT emp_id, session_range AS normalized FROM (
  SELECT emp_id, session_range
  FROM RANGE_SESSIONIZE(
    TABLE mydataset.my_sessionized_range_table,
    'duration',
    ['emp_id'],
    'MEETS')
)
GROUP BY emp_id, normalized;

/*--------+--------------------------+
 | emp_id | normalized               |
 +--------+--------------------------+
 | 20     | [2010-03-10, 2010-07-20) |
 | 10     | [2010-01-10, 2010-08-18) |
 | 20     | [2020-05-10, 2020-09-20) |
 +--------+--------------------------*/
```

## `RANGE_START`

```googlesql
RANGE_START(range_to_check)
```

**Description**

Gets the lower bound of a range.

**Definitions**

+   `range_to_check`: The `RANGE<T>` value.

**Details**

Returns `NULL` if the lower bound of `range_value` is `UNBOUNDED`.

Returns `NULL` if `range_to_check` is `NULL`.

**Return type**

`T` in `range_value`

**Examples**

In the following query, the lower bound of the range is retrieved:

```googlesql
SELECT RANGE_START(RANGE<DATE> '[2022-12-01, 2022-12-31)') AS results;

/*------------+
 | results    |
 +------------+
 | 2022-12-01 |
 +------------*/
```

In the following query, the lower bound of the range is unbounded, so
`NULL` is returned:

```googlesql
SELECT RANGE_START(RANGE<DATE> '[UNBOUNDED, 2022-12-31)') AS results;

/*------------+
 | results    |
 +------------+
 | NULL       |
 +------------*/
```

