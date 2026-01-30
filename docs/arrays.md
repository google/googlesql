

<!-- mdlint off(WHITESPACE_LINE_LENGTH) -->

# Work with arrays 
<a id="working_with_arrays"></a>

In GoogleSQL, an array is an ordered list consisting of zero or more
values of the same data type. You can construct arrays of a simple data type,
such as `INT64`, or a complex data type, such as `STRUCT`. However,
arrays of arrays aren't supported. To learn more about the `ARRAY`
data type, including
`NULL` handling, see [Array type][array-data-type].

With GoogleSQL, you can construct array literals,
 build arrays from subqueries using the
[`ARRAY`][array-function] function,
 and aggregate values into an array using the
[`ARRAY_AGG`][array-agg-function]
function.

You can combine arrays using functions like
`ARRAY_CONCAT()`, and convert arrays to strings using `ARRAY_TO_STRING()`.

## Accessing array elements 
<a id="accessing_array_elements"></a>

Consider the following table called `Sequences`. This table contains
the column `some_numbers` of the `ARRAY` data type.

```googlesql
WITH
  Sequences AS (
    SELECT [0, 1, 1, 2, 3, 5] AS some_numbers UNION ALL
    SELECT [2, 4, 8, 16, 32] UNION ALL
    SELECT [5, 10]
  )
SELECT * FROM Sequences

/*---------------------+
 | some_numbers        |
 +---------------------+
 | [0, 1, 1, 2, 3, 5]  |
 | [2, 4, 8, 16, 32]   |
 | [5, 10]             |
 +---------------------*/
```

To access array elements in the `some_numbers` column, specify which
type of indexing you want to use:
either [`index`][array-subscript-operator]
or [`OFFSET(index)`][array-subscript-operator] for
zero-based indexes, or [`ORDINAL(index)`][array-subscript-operator] for
one-based indexes.

For example:

```googlesql
SELECT
  some_numbers,
  some_numbers[0] AS index_0,
  some_numbers[OFFSET(1)] AS offset_1,
  some_numbers[ORDINAL(1)] AS ordinal_1
FROM Sequences

/*--------------------+---------+----------+-----------+
 | some_numbers       | index_0 | offset_1 | ordinal_1 |
 +--------------------+---------+----------+-----------+
 | [0, 1, 1, 2, 3, 5] | 0       | 1        | 0         |
 | [2, 4, 8, 16, 32]  | 2       | 4        | 2         |
 | [5, 10]            | 5       | 10       | 5         |
 +--------------------+---------+----------+-----------*/
```

Note: `OFFSET` and `ORDINAL` will raise errors if the index is out of
range. To avoid this, you can use `SAFE_OFFSET` or `SAFE_ORDINAL` to return
`NULL` instead of raising an error.

## Finding lengths

The `ARRAY_LENGTH` function returns the length of an array.

```googlesql
WITH Sequences AS
  (SELECT [0, 1, 1, 2, 3, 5] AS some_numbers
   UNION ALL SELECT [2, 4, 8, 16, 32] AS some_numbers
   UNION ALL SELECT [5, 10] AS some_numbers)
SELECT some_numbers,
       ARRAY_LENGTH(some_numbers) AS len
FROM Sequences;

/*--------------------+--------+
 | some_numbers       | len    |
 +--------------------+--------+
 | [0, 1, 1, 2, 3, 5] | 6      |
 | [2, 4, 8, 16, 32]  | 5      |
 | [5, 10]            | 2      |
 +--------------------+--------*/
```

## Flattening nested data into an array 
<a id="flattening_nested_data_into_arrays"></a>

If you have an array with nested data, you can return a single, flat
array containing all elements in a specific part of the nested data.
To do this, you can use the [`FLATTEN`][flatten-operator] operator with an
array and the [array element field access operator][array-el-field-operator].

**Examples**

The examples in this section references nested data in an array called
`items` in a table called `ItemsTable`:

```googlesql
WITH ItemsTable AS (SELECT [
  STRUCT('red' AS color,
         2 AS inventory,
         [STRUCT('a' AS agent, [100.0, 50.0] AS prices),
          STRUCT('c' AS agent, [25.0] AS prices)] AS sales),
  STRUCT('green' AS color,
         NULL AS inventory,
         [STRUCT('a' AS agent, [75.0] AS prices),
          STRUCT('b' AS agent, [200.0] AS prices)] AS sales),
  STRUCT('orange' AS color,
         10 AS inventory,
         NULL AS sales)
] AS items)
SELECT * FROM ItemsTable
```

You can flatten nested data in an array called `items` with the
`FLATTEN` operator. Here are some examples:

```googlesql
SELECT FLATTEN(items.color) AS colors
FROM ItemsTable

/*----------------------+
 | colors               |
 +----------------------+
 | [red, green, orange] |
 +----------------------*/
```

```googlesql
SELECT FLATTEN(items.inventory) AS inventory
FROM ItemsTable

/*---------------+
 | inventory     |
 +---------------+
 | [2, NULL, 10] |
 +---------------*/
```

```googlesql
SELECT FLATTEN(items.sales.prices) AS all_prices
FROM ItemsTable

/*------------------------+
 | all_prices             |
 +------------------------+
 | [100, 50, 25, 75, 200] |
 +------------------------*/
```

```googlesql
SELECT FLATTEN(items.sales.prices[SAFE_OFFSET(1)]) AS second_prices
FROM ItemsTable

/*------------------------+
 | second_prices          |
 +------------------------+
 | [50, NULL, NULL, NULL] |
 +------------------------*/
```

## Flattening nested data into a table 
<a id="flattening_nested_data_into_table"></a>

If you have an array with nested data, you can get a specific part of the nested
data in the array and return it as a single, flat table with one row for each
element. To do this, you can use the [`UNNEST`][unnest-query] operator
explicitly or implicitly in the [`FROM` clause][from-clause] with the [array
element field access operator][array-el-field-operator].

**Examples**

The examples in this section references nested data in an array called `items`
in a table called `ItemsTable`:

```googlesql
WITH ItemsTable AS (SELECT [
  STRUCT('red' AS color,
         2 AS inventory,
         [STRUCT('a' AS agent, [100.0, 50.0] AS prices),
          STRUCT('c' AS agent, [25.0] AS prices)] AS sales),
  STRUCT('green' AS color,
         NULL AS inventory,
         [STRUCT('a' AS agent, [75.0] AS prices),
          STRUCT('b' AS agent, [200.0] AS prices)] AS sales),
  STRUCT('orange' AS color,
         10 AS inventory,
         NULL AS sales)
] AS items)
SELECT * FROM ItemsTable
```

You can flatten nested data in an array called `items` with the
`UNNEST` operator or directly in the `FROM` clause. Here are some examples:

```googlesql
-- In UNNEST (FLATTEN used explicitly):
SELECT colors
FROM ItemsTable
INNER JOIN UNNEST(FLATTEN(items.color)) AS colors;

-- In UNNEST (FLATTEN used implicitly):
SELECT colors
FROM ItemsTable
INNER JOIN UNNEST(items.color) AS colors;

-- In the FROM clause (UNNEST used implicitly):
SELECT colors
FROM ItemsTable
INNER JOIN ItemsTable.items.color AS colors;

/*--------+
 | colors |
 +--------+
 | red    |
 | green  |
 | orange |
 +--------*/
```

```googlesql
-- In UNNEST (FLATTEN used explicitly):
SELECT inventory
FROM ItemsTable
INNER JOIN UNNEST(FLATTEN(items.inventory)) AS inventory;

-- In UNNEST (FLATTEN used implicitly):
SELECT inventory
FROM ItemsTable
INNER JOIN UNNEST(items.inventory) AS inventory;

-- In the FROM clause (UNNEST used implicitly):
SELECT inventory
FROM ItemsTable
INNER JOIN ItemsTable.items.inventory AS inventory;

/*-----------+
 | inventory |
 +-----------+
 | 2         |
 | NULL      |
 | 10        |
 +-----------*/
```

```googlesql
-- In UNNEST (FLATTEN used explicitly):
SELECT all_prices
FROM ItemsTable
INNER JOIN UNNEST(FLATTEN(items.sales.prices)) AS all_prices;

-- In UNNEST (FLATTEN used implicitly):
SELECT all_prices
FROM ItemsTable
INNER JOIN UNNEST(items.sales.prices) AS all_prices;

-- In the FROM clause (UNNEST used implicitly):
SELECT all_prices
FROM ItemsTable
INNER JOIN ItemsTable.items.sales.prices AS all_prices;

/*------------+
 | all_prices |
 +------------+
 | 100        |
 | 50         |
 | 25         |
 | 75         |
 | 200        |
 +------------*/
```

```googlesql
-- In UNNEST (FLATTEN used explicitly):
SELECT second_prices
FROM ItemsTable
INNER JOIN UNNEST(FLATTEN(items.sales.prices[SAFE_OFFSET(1)])) AS second_prices;

-- In UNNEST (FLATTEN used implicitly):
SELECT second_prices
FROM ItemsTable
INNER JOIN UNNEST(items.sales.prices[SAFE_OFFSET(1)]) AS second_prices;

-- In the FROM clause (UNNEST used implicitly):
SELECT second_prices
FROM ItemsTable
INNER JOIN ItemsTable.items.sales.prices[SAFE_OFFSET(1)] AS second_prices;

/*---------------+
 | second_prices |
 +---------------+
 | 50            |
 | NULL          |
 | NULL          |
 | NULL          |
 +---------------*/
```

## Converting elements in an array to rows in a table 
<a id="flattening_arrays"></a>

To convert an `ARRAY` into a set of rows, also known as "flattening," use the
[`UNNEST`][unnest-query]
operator. `UNNEST` takes an `ARRAY` and returns a table with a single row for
each element in the `ARRAY`.

Because `UNNEST` destroys the order of the `ARRAY` elements, you may
wish to restore order to the table. To do so, use the optional `WITH OFFSET`
clause to return an additional column with the offset for each array element,
then use the `ORDER BY` clause to order the rows by their offset.

**Example**

```googlesql
SELECT *
FROM UNNEST(['foo', 'bar', 'baz', 'qux', 'corge', 'garply', 'waldo', 'fred'])
  AS element
WITH OFFSET AS offset
ORDER BY offset;

/*----------+--------+
 | element  | offset |
 +----------+--------+
 | foo      | 0      |
 | bar      | 1      |
 | baz      | 2      |
 | qux      | 3      |
 | corge    | 4      |
 | garply   | 5      |
 | waldo    | 6      |
 | fred     | 7      |
 +----------+--------*/
```

To flatten an entire column of type `ARRAY` while preserving the values of the other
columns in each row, use a correlated [`INNER JOIN`][inner-join-query] to join
the table containing the `ARRAY` column to the `UNNEST` output of that `ARRAY`
column.

With a [correlated][correlated-join-query] join, the `UNNEST` operator
references the `ARRAY` typed column from each row in the source table, which
appears previously in the `FROM` clause. For each row `N` in the source table,
`UNNEST` flattens the `ARRAY` from row `N` into a set of rows containing the
`ARRAY` elements, and then a correlated `INNER JOIN` or `CROSS JOIN` combines
this new set of rows with the single row `N` from the source table.

**Examples**

The following example uses [`UNNEST`][unnest-query] to return a row for each
element in the array column. Because of the `INNER JOIN`, the `id` column
contains the `id` values for the row in `Sequences` that contains each number.

```googlesql
WITH
  Sequences AS (
    SELECT 1 AS id, [0, 1, 1, 2, 3, 5] AS some_numbers
    UNION ALL SELECT 2 AS id, [2, 4, 8, 16, 32] AS some_numbers
    UNION ALL SELECT 3 AS id, [5, 10] AS some_numbers
  )
SELECT id, flattened_numbers
FROM Sequences
INNER JOIN UNNEST(Sequences.some_numbers) AS flattened_numbers;

/*------+-------------------+
 | id   | flattened_numbers |
 +------+-------------------+
 |    1 |                 0 |
 |    1 |                 1 |
 |    1 |                 1 |
 |    1 |                 2 |
 |    1 |                 3 |
 |    1 |                 5 |
 |    2 |                 2 |
 |    2 |                 4 |
 |    2 |                 8 |
 |    2 |                16 |
 |    2 |                32 |
 |    3 |                 5 |
 |    3 |                10 |
 +------+-------------------*/
```

Note that for correlated joins the `UNNEST` operator is optional and the
`INNER JOIN` can be expressed as a `CROSS JOIN` or a comma cross join. Using the
comma cross join shorthand notation, the previous example is consolidated as
follows:

```googlesql
WITH
  Sequences AS (
    SELECT 1 AS id, [0, 1, 1, 2, 3, 5] AS some_numbers
    UNION ALL SELECT 2 AS id, [2, 4, 8, 16, 32] AS some_numbers
    UNION ALL SELECT 3 AS id, [5, 10] AS some_numbers
  )
SELECT id, flattened_numbers
FROM Sequences, Sequences.some_numbers AS flattened_numbers;

/*------+-------------------+
 | id   | flattened_numbers |
 +------+-------------------+
 |    1 |                 0 |
 |    1 |                 1 |
 |    1 |                 1 |
 |    1 |                 2 |
 |    1 |                 3 |
 |    1 |                 5 |
 |    2 |                 2 |
 |    2 |                 4 |
 |    2 |                 8 |
 |    2 |                16 |
 |    2 |                32 |
 |    3 |                 5 |
 |    3 |                10 |
 +------+-------------------*/
```

## Querying nested and repeated fields

If a table contains an `ARRAY` of `STRUCT` or `PROTO` values, you can
[flatten the `ARRAY`][flattening-arrays] to query the fields of the `STRUCT` or
`PROTO`.
You can also flatten `ARRAY` type fields of `STRUCT` values and repeated fields
of `PROTO` values. GoogleSQL treats repeated `PROTO` fields as
`ARRAY`s.

### Querying `STRUCT` elements in an array 
<a id="query_structs_in_an_array"></a>

The following example uses `UNNEST` with `INNER JOIN` to flatten an `ARRAY` of
`STRUCT`s.

```googlesql
WITH
  Races AS (
    SELECT
      "800M" AS race,
      [
        STRUCT("Rudisha" AS name, [23.4, 26.3, 26.4, 26.1] AS laps),
        STRUCT("Makhloufi" AS name, [24.5, 25.4, 26.6, 26.1] AS laps),
        STRUCT("Murphy" AS name, [23.9, 26.0, 27.0, 26.0] AS laps),
        STRUCT("Bosse" AS name, [23.6, 26.2, 26.5, 27.1] AS laps),
        STRUCT("Rotich" AS name, [24.7, 25.6, 26.9, 26.4] AS laps),
        STRUCT("Lewandowski" AS name, [25.0, 25.7, 26.3, 27.2] AS laps),
        STRUCT("Kipketer" AS name, [23.2, 26.1, 27.3, 29.4] AS laps),
        STRUCT("Berian" AS name, [23.7, 26.1, 27.0, 29.3] AS laps)
      ] AS participants
    )
SELECT
  race,
  participant
FROM Races AS r
INNER JOIN UNNEST(r.participants) AS participant;

/*------+---------------------------------------+
 | race | participant                           |
 +------+---------------------------------------+
 | 800M | {Rudisha, [23.4, 26.3, 26.4, 26.1]}   |
 | 800M | {Makhloufi, [24.5, 25.4, 26.6, 26.1]} |
 | 800M | {Murphy, [23.9, 26, 27, 26]}          |
 | 800M | {Bosse, [23.6, 26.2, 26.5, 27.1]}     |
 | 800M | {Rotich, [24.7, 25.6, 26.9, 26.4]}    |
 | 800M | {Lewandowski, [25, 25.7, 26.3, 27.2]} |
 | 800M | {Kipketer, [23.2, 26.1, 27.3, 29.4]}  |
 | 800M | {Berian, [23.7, 26.1, 27, 29.3]}      |
 +------+---------------------------------------*/
```

You can find specific information from repeated fields. For example, the
following query returns the fastest racer in an 800M race.

Note: This example doesn't involve flattening an array, but does
represent a common way to get information from a repeated field.

**Example**

```googlesql
WITH
  Races AS (
    SELECT
      "800M" AS race,
      [
        STRUCT("Rudisha" AS name, [23.4, 26.3, 26.4, 26.1] AS laps),
        STRUCT("Makhloufi" AS name, [24.5, 25.4, 26.6, 26.1] AS laps),
        STRUCT("Murphy" AS name, [23.9, 26.0, 27.0, 26.0] AS laps),
        STRUCT("Bosse" AS name, [23.6, 26.2, 26.5, 27.1] AS laps),
        STRUCT("Rotich" AS name, [24.7, 25.6, 26.9, 26.4] AS laps),
        STRUCT("Lewandowski" AS name, [25.0, 25.7, 26.3, 27.2] AS laps),
        STRUCT("Kipketer" AS name, [23.2, 26.1, 27.3, 29.4] AS laps),
        STRUCT("Berian" AS name, [23.7, 26.1, 27.0, 29.3] AS laps)
      ] AS participants
  )
SELECT
  race,
  (
    SELECT name
    FROM UNNEST(participants)
    ORDER BY (SELECT SUM(duration) FROM UNNEST(laps) AS duration) ASC
    LIMIT 1
  ) AS fastest_racer
FROM Races;

/*------+---------------+
 | race | fastest_racer |
 +------+---------------+
 | 800M | Rudisha       |
 +------+---------------*/
```

### Querying `PROTO` elements in an array

To query the fields of `PROTO` elements in an `ARRAY`, use `UNNEST` and
`INNER JOIN`.

**Example**

The following query shows the contents of a table where one row contains an
`ARRAY` of `PROTO`s. All of the `PROTO` field values in the `ARRAY` appear in a
single row.

```googlesql
WITH
  Albums AS (
  SELECT
    'Let It Be' AS album_name,
    [
      NEW googlesql.examples.music.Chart(1 AS rank, 'US 100' AS chart_name),
      NEW googlesql.examples.music.Chart(1 AS rank, 'UK 40' AS chart_name),
      NEW googlesql.examples.music.Chart(2 AS rank, 'Oricon' AS chart_name)
    ] AS charts
  UNION ALL
  SELECT
    'Rubber Soul' AS album_name,
    [
      NEW googlesql.examples.music.Chart(1 AS rank, 'US 100' AS chart_name),
      NEW googlesql.examples.music.Chart(1 AS rank, 'UK 40' AS chart_name),
      NEW googlesql.examples.music.Chart(24 AS rank, 'Oricon' AS chart_name)
    ] AS charts
  )
SELECT *
FROM Albums;

/*-------------+---------------------------------+
 | album_name  | charts                          |
 +-------------+---------------------------------+
 | Let It Be   | [chart_name: "US 100", rank: 1, |
 |             | chart_name: "UK 40", rank: 1,   |
 |             | chart_name: "Oricon" rank: 2]   |
 +-------------+---------------------------------+
 | Rubber Soul | [chart_name: "US 100", rank: 1, |
 |             | chart_name: "UK 40", rank: 1,   |
 |             | chart_name: "Oricon" rank: 24]  |
 +-------------+---------------------------------*/
```

To return the value of the individual fields of the `PROTO` values inside of an
`ARRAY`, use `UNNEST` to flatten the `ARRAY`, then use an `INNER JOIN` to apply
the `UNNEST` operator to each row of the `ARRAY` column. The `INNER JOIN` also
joins the duplicated values of other columns to the result of `UNNEST`, so you
can query these columns together with the fields of the `PROTO` values in the
`ARRAY`.

**Example**

The following example uses `UNNEST` to flatten the `ARRAY` `charts`. The `INNER
JOIN` applies the `UNNEST` operator to every row in the `charts` column and
joins the duplicated value of `table.album_name` to the `chart` table. This
allows the query to include the `table.album_name` column in the `SELECT` list
together with the `PROTO` fields `chart.chart_name` and `chart.rank`.

```googlesql
WITH
  Albums AS (
    SELECT
      'Let It Be' AS album_name,
      [
        NEW googlesql.examples.music.Chart(1 AS rank, 'US 100' AS chart_name),
        NEW googlesql.examples.music.Chart(1 AS rank, 'UK 40' AS chart_name),
        NEW googlesql.examples.music.Chart(2 AS rank, 'Oricon' AS chart_name)
      ] AS charts
    UNION ALL
    SELECT
      'Rubber Soul' AS album_name,
      [
        NEW googlesql.examples.music.Chart(1 AS rank, 'US 100' AS chart_name),
        NEW googlesql.examples.music.Chart(1 AS rank, 'UK 40' AS chart_name),
        NEW googlesql.examples.music.Chart(24 AS rank, 'Oricon' AS chart_name)
      ] AS charts
  )
SELECT Albums.album_name, chart.chart_name, chart.rank
FROM Albums
INNER JOIN UNNEST(charts) AS chart;

/*-------------+------------+------+
 | album_name  | chart_name | rank |
 +-------------+------------+------+
 | Let It Be   | US 100     |    1 |
 | Let It Be   | UK 40      |    1 |
 | Let It Be   | Oricon     |    2 |
 | Rubber Soul | US 100     |    1 |
 | Rubber Soul | UK 40      |    1 |
 | Rubber Soul | Oricon     |   24 |
 +-------------+------------+------*/
```

### Querying `ARRAY`-type fields in a struct

You can also get information from nested repeated fields. For example, the
following statement returns the runner who had the fastest lap in an 800M race.

```googlesql
WITH
  Races AS (
    SELECT
      "800M" AS race,
      [
        STRUCT("Rudisha" AS name, [23.4, 26.3, 26.4, 26.1] AS laps),
        STRUCT("Makhloufi" AS name, [24.5, 25.4, 26.6, 26.1] AS laps),
        STRUCT("Murphy" AS name, [23.9, 26.0, 27.0, 26.0] AS laps),
        STRUCT("Bosse" AS name, [23.6, 26.2, 26.5, 27.1] AS laps),
        STRUCT("Rotich" AS name, [24.7, 25.6, 26.9, 26.4] AS laps),
        STRUCT("Lewandowski" AS name, [25.0, 25.7, 26.3, 27.2] AS laps),
        STRUCT("Kipketer" AS name, [23.2, 26.1, 27.3, 29.4] AS laps),
        STRUCT("Berian" AS name, [23.7, 26.1, 27.0, 29.3] AS laps)
      ]AS participants
  )
SELECT
  race,
  (
    SELECT name
    FROM UNNEST(participants), UNNEST(laps) AS duration
    ORDER BY duration ASC
    LIMIT 1
  ) AS runner_with_fastest_lap
FROM Races;

/*------+-------------------------+
 | race | runner_with_fastest_lap |
 +------+-------------------------+
 | 800M | Kipketer                |
 +------+-------------------------*/
```

Notice that the preceding query uses the comma operator (`,`) to perform a cross
join and flatten the array. This is equivalent to using an explicit
`CROSS JOIN`, or the following example which uses an explicit `INNER JOIN`:

```googlesql
WITH
  Races AS (
    SELECT "800M" AS race,
      [
        STRUCT("Rudisha" AS name, [23.4, 26.3, 26.4, 26.1] AS laps),
        STRUCT("Makhloufi" AS name, [24.5, 25.4, 26.6, 26.1] AS laps),
        STRUCT("Murphy" AS name, [23.9, 26.0, 27.0, 26.0] AS laps),
        STRUCT("Bosse" AS name, [23.6, 26.2, 26.5, 27.1] AS laps),
        STRUCT("Rotich" AS name, [24.7, 25.6, 26.9, 26.4] AS laps),
        STRUCT("Lewandowski" AS name, [25.0, 25.7, 26.3, 27.2] AS laps),
        STRUCT("Kipketer" AS name, [23.2, 26.1, 27.3, 29.4] AS laps),
        STRUCT("Berian" AS name, [23.7, 26.1, 27.0, 29.3] AS laps)
      ] AS participants
  )
SELECT
  race,
  (
    SELECT name
    FROM UNNEST(participants)
    INNER JOIN UNNEST(laps) AS duration
    ORDER BY duration ASC LIMIT 1
  ) AS runner_with_fastest_lap
FROM Races;

/*------+-------------------------+
 | race | runner_with_fastest_lap |
 +------+-------------------------+
 | 800M | Kipketer                |
 +------+-------------------------*/
```

Flattening arrays with `INNER JOIN` excludes rows that have empty or `NULL`
arrays. If you want to include these rows, use `LEFT JOIN`.

```googlesql
WITH
  Races AS (
    SELECT
      "800M" AS race,
      [
        STRUCT("Rudisha" AS name, [23.4, 26.3, 26.4, 26.1] AS laps),
        STRUCT("Makhloufi" AS name, [24.5, 25.4, 26.6, 26.1] AS laps),
        STRUCT("Murphy" AS name, [23.9, 26.0, 27.0, 26.0] AS laps),
        STRUCT("Bosse" AS name, [23.6, 26.2, 26.5, 27.1] AS laps),
        STRUCT("Rotich" AS name, [24.7, 25.6, 26.9, 26.4] AS laps),
        STRUCT("Lewandowski" AS name, [25.0, 25.7, 26.3, 27.2] AS laps),
        STRUCT("Kipketer" AS name, [23.2, 26.1, 27.3, 29.4] AS laps),
        STRUCT("Berian" AS name, [23.7, 26.1, 27.0, 29.3] AS laps),
        STRUCT("Nathan" AS name, ARRAY<DOUBLE>[] AS laps),
        STRUCT("David" AS name, NULL AS laps)
      ] AS participants
  )
SELECT
  Participant.name,
  SUM(duration) AS finish_time
FROM Races
INNER JOIN Races.participants AS Participant
LEFT JOIN Participant.laps AS duration
GROUP BY name;

/*-------------+--------------------+
 | name        | finish_time        |
 +-------------+--------------------+
 | Murphy      | 102.9              |
 | Rudisha     | 102.19999999999999 |
 | David       | NULL               |
 | Rotich      | 103.6              |
 | Makhloufi   | 102.6              |
 | Berian      | 106.1              |
 | Bosse       | 103.4              |
 | Kipketer    | 106                |
 | Nathan      | NULL               |
 | Lewandowski | 104.2              |
 +-------------+--------------------*/
```

### Querying repeated fields

GoogleSQL represents a repeated field of a `PROTO` as an `ARRAY`. You
can query this `ARRAY` using `UNNEST` and `INNER JOIN`.

The following example queries a table containing a column of type `PROTO` with
the alias `album` and the repeated field `song`. All values of `song` for each
`album` appear on the same row.

**Example**

```googlesql
WITH
  Bands AS (
    SELECT
      'The Beatles' AS band_name,
      NEW googlesql.examples.music.Album(
        'Let It Be' AS album_name,
        ['Across the Universe', 'Get Back', 'Dig It'] AS song) AS album
    UNION ALL
    SELECT
      'The Beatles' AS band_name,
      NEW googlesql.examples.music.Album(
        'Rubber Soul' AS album_name,
        ['Drive My Car', 'The Word', 'Michelle'] AS song) AS album
  )
SELECT band_name, album.album_name, album.song
FROM Bands;

/*-------------+------------------+-----------------------------------------+
 | band_name   | album_name       | song                                    |
 +-------------+------------------+-----------------------------------------+
 | The Beatles | Let It Be        | [Across the Universe, Get Back, Dig It] |
 | The Beatles | Rubber Soul      | [Drive My Car, The Word, Michelle]      |
 +-------------+------------------+-----------------------------------------*/
```

To query the individual values of a repeated field, reference the field name
using dot notation to return an `ARRAY`, and
[flatten the `ARRAY` using `UNNEST`][flattening-arrays]. Use `INNER JOIN` to
apply the `UNNEST` operator to each row and join the flattened `ARRAY` to the
duplicated value of any non-repeated fields or columns in that row.

**Example**

The following example queries the table from the previous example and returns
the values of the repeated field as an `ARRAY`. The `UNNEST` operator flattens
the `ARRAY` that represents the repeated field `song`. `INNER JOIN` applies the
`UNNEST` operator to each row and joins the output of `UNNEST` to the duplicated
value of the column `band_name` and the non-repeated field `album_name` within
that row.

```googlesql
WITH Bands AS (
  SELECT
    'The Beatles' AS band_name,
    NEW googlesql.examples.music.Album(
      'Let It Be' AS album_name,
      ['Across the Universe', 'Get Back', 'Dig It'] AS song
    ) AS album
    UNION ALL
  SELECT
    'The Beatles' AS band_name,
    NEW googlesql.examples.music.Album(
      'Rubber Soul' AS album_name,
      ['Drive My Car', 'The Word', 'Michelle'] AS song
    ) AS album
)
SELECT band_name, album.album_name, song_name
FROM Bands
INNER JOIN UNNEST(album.song) AS song_name;

/*-------------+-------------+---------------------+
 | band_name   | album_name  | song_name           |
 +-------------+-------------+---------------------+
 | The Beatles | Let It Be   | Across the Universe |
 | The Beatles | Let It Be   | Get Back            |
 | The Beatles | Let It Be   | Dig It              |
 | The Beatles | Rubber Soul | Drive My Car        |
 | The Beatles | Rubber Soul | The Word            |
 | The Beatles | Rubber Soul | Michelle            |
 +-------------+-------------+---------------------*/
```

## Casting arrays

You can use the [`CAST AS ARRAY`][cast-as-array] function
to cast arrays from one element type to another. The element types of the input
`ARRAY` must be castable to the element types of the target `ARRAY`. For
example, casting from type `ARRAY<INT32>` to `ARRAY<INT64>` or `ARRAY<STRING>`
is valid; casting from type `ARRAY<INT32>` to `ARRAY<BYTES>` isn't valid.

**Example**

```googlesql
SELECT CAST(int_array AS ARRAY<DOUBLE>) AS double_array
FROM (SELECT ARRAY<INT32>[1, 2, 3] AS int_array);

/*--------------+
 | double_array |
 +--------------+
 | [1, 2, 3]    |
 +--------------*/
```

## Constructing arrays

You can construct an array using array literals or array functions. To learn
more about constructing arrays, see [Array type][array-data-type-construct].

## Creating arrays from subqueries

A common task when working with arrays is turning a subquery result into an
array. In GoogleSQL, you can accomplish this using the
[`ARRAY()`][array-function] function.

For example, consider the following operation on the `Sequences` table:

```googlesql
WITH Sequences AS
  (SELECT [0, 1, 1, 2, 3, 5] AS some_numbers
  UNION ALL SELECT [2, 4, 8, 16, 32] AS some_numbers
  UNION ALL SELECT [5, 10] AS some_numbers)
SELECT some_numbers,
  ARRAY(SELECT x * 2
        FROM UNNEST(some_numbers) AS x) AS doubled
FROM Sequences;

/*--------------------+---------------------+
 | some_numbers       | doubled             |
 +--------------------+---------------------+
 | [0, 1, 1, 2, 3, 5] | [0, 2, 2, 4, 6, 10] |
 | [2, 4, 8, 16, 32]  | [4, 8, 16, 32, 64]  |
 | [5, 10]            | [10, 20]            |
 +--------------------+---------------------*/
```

This example starts with a table named Sequences. This table contains a column,
`some_numbers`, of type `ARRAY<INT64>`.

The query itself contains a subquery. This subquery selects each row in the
`some_numbers` column and uses
[`UNNEST`][unnest-query] to return the
array as a set of rows. Next, it multiplies each value by two, and then
re-combines the rows back into an array using the `ARRAY()` operator.

## Filtering arrays
The following example uses a `WHERE` clause in the `ARRAY()` operator's subquery
to filter the returned rows.

Note: In the following examples, the resulting rows are
not ordered.

```googlesql
WITH Sequences AS
  (SELECT [0, 1, 1, 2, 3, 5] AS some_numbers
   UNION ALL SELECT [2, 4, 8, 16, 32] AS some_numbers
   UNION ALL SELECT [5, 10] AS some_numbers)
SELECT
  ARRAY(SELECT x * 2
        FROM UNNEST(some_numbers) AS x
        WHERE x < 5) AS doubled_less_than_five
FROM Sequences;

/*------------------------+
 | doubled_less_than_five |
 +------------------------+
 | [0, 2, 2, 4, 6]        |
 | [4, 8]                 |
 | []                     |
 +------------------------*/
```

Notice that the third row contains an empty array, because the elements in the
corresponding original row (`[5, 10]`) didn't meet the filter requirement of
`x < 5`.

You can also filter arrays by using `SELECT DISTINCT` to return only
unique elements within an array.

```googlesql
WITH Sequences AS
  (SELECT [0, 1, 1, 2, 3, 5] AS some_numbers)
SELECT ARRAY(SELECT DISTINCT x
             FROM UNNEST(some_numbers) AS x) AS unique_numbers
FROM Sequences;

/*-----------------+
 | unique_numbers  |
 +-----------------+
 | [0, 1, 2, 3, 5] |
 +-----------------*/
```

You can also filter rows of arrays by using the
[`IN`][in-operators] keyword. This
keyword filters rows containing arrays by determining if a specific
value matches an element in the array.

```googlesql
WITH Sequences AS
  (SELECT [0, 1, 1, 2, 3, 5] AS some_numbers
   UNION ALL SELECT [2, 4, 8, 16, 32] AS some_numbers
   UNION ALL SELECT [5, 10] AS some_numbers)
SELECT
   ARRAY(SELECT x
         FROM UNNEST(some_numbers) AS x
         WHERE 2 IN UNNEST(some_numbers)) AS contains_two
FROM Sequences;

/*--------------------+
 | contains_two       |
 +--------------------+
 | [0, 1, 1, 2, 3, 5] |
 | [2, 4, 8, 16, 32]  |
 | []                 |
 +--------------------*/
```

Notice again that the third row contains an empty array, because the array in
the corresponding original row (`[5, 10]`) didn't contain `2`.

## Scanning arrays

To check if an array contains a specific value, use the [`IN`][in-operators]
operator with [`UNNEST`][unnest-query]. To check if an array contains a value
matching a condition, use the [`EXISTS`][exists-operator] operator with
`UNNEST`.

### Scanning for specific values

To scan an array for a specific value, use the `IN` operator with `UNNEST`.

**Example**

The following example returns `true` if the array contains the number 2.

```googlesql
SELECT 2 IN UNNEST([0, 1, 1, 2, 3, 5]) AS contains_value;

/*----------------+
 | contains_value |
 +----------------+
 | true           |
 +----------------*/
```

To return the rows of a table where the array column contains a specific value,
filter the results of `IN UNNEST` using the `WHERE` clause.

**Example**

The following example returns the `id` value for the rows where the array
column contains the value 2.

```googlesql
WITH Sequences AS
  (SELECT 1 AS id, [0, 1, 1, 2, 3, 5] AS some_numbers
   UNION ALL SELECT 2 AS id, [2, 4, 8, 16, 32] AS some_numbers
   UNION ALL SELECT 3 AS id, [5, 10] AS some_numbers)
SELECT id AS matching_rows
FROM Sequences
WHERE 2 IN UNNEST(Sequences.some_numbers)
ORDER BY matching_rows;

/*---------------+
 | matching_rows |
 +---------------+
 | 1             |
 | 2             |
 +---------------*/
```

### Scanning for values that satisfy a condition

To scan an array for values that match a condition, use `UNNEST` to return a
table of the elements in the array, use `WHERE` to filter the resulting table in
a subquery, and use `EXISTS` to check if the filtered table contains any rows.

**Example**

The following example returns the `id` value for the rows where the array
column contains values greater than 5.

```googlesql
WITH
  Sequences AS (
    SELECT 1 AS id, [0, 1, 1, 2, 3, 5] AS some_numbers
    UNION ALL
    SELECT 2 AS id, [2, 4, 8, 16, 32] AS some_numbers
    UNION ALL
    SELECT 3 AS id, [5, 10] AS some_numbers
  )
SELECT id AS matching_rows
FROM Sequences
WHERE EXISTS(SELECT * FROM UNNEST(some_numbers) AS x WHERE x > 5);

/*---------------+
 | matching_rows |
 +---------------+
 | 2             |
 | 3             |
 +---------------*/
```

#### Scanning for `STRUCT` field values that satisfy a condition

To search an array of `STRUCT` values for a field whose value matches a condition, use
`UNNEST` to return a table with a column for each `STRUCT` field, then filter
non-matching rows from the table using `WHERE EXISTS`.

**Example**

The following example returns the rows where the array column contains a
`STRUCT` whose field `b` has a value greater than 3.

```googlesql
WITH
  Sequences AS (
    SELECT 1 AS id, [STRUCT(0 AS a, 1 AS b)] AS some_numbers
    UNION ALL
    SELECT 2 AS id, [STRUCT(2 AS a, 4 AS b)] AS some_numbers
    UNION ALL
    SELECT 3 AS id, [STRUCT(5 AS a, 3 AS b), STRUCT(7 AS a, 4 AS b)] AS some_numbers
  )
SELECT id AS matching_rows
FROM Sequences
WHERE EXISTS(SELECT 1 FROM UNNEST(some_numbers) WHERE b > 3);

/*---------------+
 | matching_rows |
 +---------------+
 | 2             |
 | 3             |
 +---------------*/
```

## Arrays and aggregation

With GoogleSQL, you can aggregate values into an array using
`ARRAY_AGG()`.

```googlesql
WITH Fruits AS
  (SELECT "apple" AS fruit
   UNION ALL SELECT "pear" AS fruit
   UNION ALL SELECT "banana" AS fruit)
SELECT ARRAY_AGG(fruit) AS fruit_basket
FROM Fruits;

/*-----------------------+
 | fruit_basket          |
 +-----------------------+
 | [apple, pear, banana] |
 +-----------------------*/
```

The array returned by `ARRAY_AGG()` is in an arbitrary order, since the order in
which the function concatenates values isn't guaranteed. To order the array
elements, use `ORDER BY`. For example:

```googlesql
WITH Fruits AS
  (SELECT "apple" AS fruit
   UNION ALL SELECT "pear" AS fruit
   UNION ALL SELECT "banana" AS fruit)
SELECT ARRAY_AGG(fruit ORDER BY fruit) AS fruit_basket
FROM Fruits;

/*-----------------------+
 | fruit_basket          |
 +-----------------------+
 | [apple, banana, pear] |
 +-----------------------*/
```

You can also apply aggregate functions such as `SUM()` to the elements in an
array. For example, the following query returns the sum of array elements for
each row of the `Sequences` table.

```googlesql
WITH Sequences AS
  (SELECT [0, 1, 1, 2, 3, 5] AS some_numbers
   UNION ALL SELECT [2, 4, 8, 16, 32] AS some_numbers
   UNION ALL SELECT [5, 10] AS some_numbers)
SELECT some_numbers,
  (SELECT SUM(x)
   FROM UNNEST(s.some_numbers) AS x) AS sums
FROM Sequences AS s;

/*--------------------+------+
 | some_numbers       | sums |
 +--------------------+------+
 | [0, 1, 1, 2, 3, 5] | 12   |
 | [2, 4, 8, 16, 32]  | 62   |
 | [5, 10]            | 15   |
 +--------------------+------*/
```

GoogleSQL also supports an aggregate function, `ARRAY_CONCAT_AGG()`,
which concatenates the elements of an array column across rows.

```googlesql
WITH Aggregates AS
  (SELECT [1,2] AS numbers
   UNION ALL SELECT [3,4] AS numbers
   UNION ALL SELECT [5, 6] AS numbers)
SELECT ARRAY_CONCAT_AGG(numbers) AS count_to_six_agg
FROM Aggregates;

/*--------------------------------------------------+
 | count_to_six_agg                                 |
 +--------------------------------------------------+
 | [1, 2, 3, 4, 5, 6]                               |
 +--------------------------------------------------*/
```

Note: The array returned by `ARRAY_CONCAT_AGG()` is
non-deterministic, since the order in which the function concatenates values is
not guaranteed.

## Converting arrays to strings

The `ARRAY_TO_STRING()` function allows you to convert an `ARRAY<STRING>` to a
single `STRING` value or an `ARRAY<BYTES>` to a single `BYTES` value where the
resulting value is the ordered concatenation of the array elements.

The second argument is the separator that the function will insert between
inputs to produce the output; this second argument must be of the same
type as the elements of the first argument.

Example:

```googlesql
WITH Words AS
  (SELECT ["Hello", "World"] AS greeting)
SELECT ARRAY_TO_STRING(greeting, " ") AS greetings
FROM Words;

/*-------------+
 | greetings   |
 +-------------+
 | Hello World |
 +-------------*/
```

The optional third argument takes the place of `NULL` values in the input
array.

+ If you omit this argument, then the function ignores `NULL` array
elements.

+ If you provide an empty string, the function inserts a
separator for `NULL` array elements.

Example:

```googlesql
SELECT
  ARRAY_TO_STRING(arr, ".", "N") AS non_empty_string,
  ARRAY_TO_STRING(arr, ".", "") AS empty_string,
  ARRAY_TO_STRING(arr, ".") AS omitted
FROM (SELECT ["a", NULL, "b", NULL, "c", NULL] AS arr);

/*------------------+--------------+---------+
 | non_empty_string | empty_string | omitted |
 +------------------+--------------+---------+
 | a.N.b.N.c.N      | a..b..c.     | a.b.c   |
 +------------------+--------------+---------*/
```

## Combining arrays

In some cases, you might want to combine multiple arrays into a single array.
You can accomplish this using the `ARRAY_CONCAT()` function.

```googlesql
SELECT ARRAY_CONCAT([1, 2], [3, 4], [5, 6]) AS count_to_six;

/*--------------------------------------------------+
 | count_to_six                                     |
 +--------------------------------------------------+
 | [1, 2, 3, 4, 5, 6]                               |
 +--------------------------------------------------*/
```

## Updating arrays

Consider the following table called `arrays_table`. The first column in the
table is an array of integers and the second column contains two nested arrays
of integers.

```googlesql
WITH arrays_table AS (
  SELECT
    [1, 2] AS regular_array,
    STRUCT([10, 20] AS first_array, [100, 200] AS second_array) AS nested_arrays
  UNION ALL SELECT
    [3, 4] AS regular_array,
    STRUCT([30, 40] AS first_array, [300, 400] AS second_array) AS nested_arrays
)
SELECT * FROM arrays_table;

/*---------------*---------------------------*----------------------------+
 | regular_array | nested_arrays.first_array | nested_arrays.second_array |
 +---------------+---------------------------+----------------------------+
 | [1, 2]        | [10, 20]                  | [100, 200]                 |
 | [3, 4]        | [30, 40]                  | [130, 400]                 |
 +---------------*---------------------------*----------------------------*/
```

You can update arrays in a table by using the `UPDATE` statement. The following
example inserts the number 5 into the `regular_array` column,
and inserts the elements from the `first_array` field of the `nested_arrays`
column into the `second_array` field:

```googlesql
UPDATE
  arrays_table
SET
  regular_array = ARRAY_CONCAT(regular_array, [5]),
  nested_arrays.second_array = ARRAY_CONCAT(nested_arrays.second_array,
                                            nested_arrays.first_array)
WHERE TRUE;
SELECT * FROM arrays_table;

/*---------------*---------------------------*----------------------------+
 | regular_array | nested_arrays.first_array | nested_arrays.second_array |
 +---------------+---------------------------+----------------------------+
 | [1, 2, 5]     | [10, 20]                  | [100, 200, 10, 20]         |
 | [3, 4, 5]     | [30, 40]                  | [130, 400, 30, 40]         |
 +---------------*---------------------------*----------------------------*/
```

## Zipping arrays

Given two arrays of equal size, you can merge them into a single array
consisting of pairs of elements from input arrays, taken from their
corresponding positions. This operation is sometimes called
[zipping][convolution].

You can zip arrays with the function [`ARRAY_ZIP`][array-zip].

## Building arrays of arrays

GoogleSQL doesn't support building
[arrays of arrays][array-data-type]
directly. Instead, you must create an array of structs, with each struct
containing a field of type `ARRAY`. To illustrate this, consider the following
`Points` table:

```googlesql
/*----------+
 | point    |
 +----------+
 | [1, 5]   |
 | [2, 8]   |
 | [3, 7]   |
 | [4, 1]   |
 | [5, 7]   |
 +----------*/
```

Now, let's say you wanted to create an array consisting of each `point` in the
`Points` table. To accomplish this, wrap the array returned from each row in a
`STRUCT`, as shown below.

```googlesql
WITH Points AS
  (SELECT [1, 5] AS point
   UNION ALL SELECT [2, 8] AS point
   UNION ALL SELECT [3, 7] AS point
   UNION ALL SELECT [4, 1] AS point
   UNION ALL SELECT [5, 7] AS point)
SELECT ARRAY(
  SELECT STRUCT(point)
  FROM Points)
  AS coordinates;

/*-------------------+
 | coordinates       |
 +-------------------+
 | [{point: [1,5]},  |
 |  {point: [2,8]},  |
 |  {point: [5,7]},  |
 |  {point: [3,7]},  |
 |  {point: [4,1]}]  |
 +-------------------*/
```

You can use this DML statement to insert the example data:

```googlesql
INSERT Points
  (point, id)
VALUES
  ([1, 5], 1),
  ([2, 8], 2),
  ([3, 7], 3),
  ([4, 1], 4),
  ([5, 7], 5);
```

```googlesql
SELECT ARRAY(
  SELECT STRUCT(point)
  FROM Points)
  AS coordinates;

/*--------------+
 | coordinates  |
 +--------------+
 | point: [1,5] |
 | point: [2,8] |
 | point: [3,7] |
 | point: [4,1] |
 | point: [5,7] |
 +--------------*/
```

<!-- mdlint off(WHITESPACE_LINE_LENGTH) -->

[convolution]: https://en.wikipedia.org/wiki/Convolution_(computer_science)

[flattening-arrays]: #flattening_arrays

[array-data-type]: https://github.com/google/googlesql/blob/master/docs/data-types.md#array_type

[array-data-type-construct]: https://github.com/google/googlesql/blob/master/docs/data-types.md#constructing_an_array

[from-clause]: https://github.com/google/googlesql/blob/master/docs/query-syntax.md#from_clause

[unnest-query]: https://github.com/google/googlesql/blob/master/docs/query-syntax.md#unnest_operator

[inner-join-query]: https://github.com/google/googlesql/blob/master/docs/query-syntax.md#inner_join

[comma-cross-join-query]: https://github.com/google/googlesql/blob/master/docs/query-syntax.md#comma_cross_join

[correlated-join-query]: https://github.com/google/googlesql/blob/master/docs/query-syntax.md#correlated_join

[in-operators]: https://github.com/google/googlesql/blob/master/docs/operators.md#in_operators

[exists-operator]: https://github.com/google/googlesql/blob/master/docs/operators.md#exists_operator

[cast-as-array]: https://github.com/google/googlesql/blob/master/docs/conversion_functions.md#cast-as-array

[array-function]: https://github.com/google/googlesql/blob/master/docs/array_functions.md

[array-agg-function]: https://github.com/google/googlesql/blob/master/docs/aggregate_functions.md#array_agg

[array-subscript-operator]: https://github.com/google/googlesql/blob/master/docs/operators.md#array_subscript_operator

[flatten-operator]: https://github.com/google/googlesql/blob/master/docs/array_functions.md#flatten

[array-el-field-operator]: https://github.com/google/googlesql/blob/master/docs/operators.md#array_el_field_operator

[array-zip]: https://github.com/google/googlesql/blob/master/docs/array_functions.md#array-zip

<!-- mdlint on -->

