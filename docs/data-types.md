

<!-- mdlint off(WHITESPACE_LINE_LENGTH) -->

# Data types

This page provides an overview of all GoogleSQL
data types, including information about their value
domains. For
information on data type literals and constructors, see
[Lexical Structure and Syntax][lexical-literals].

## Data type list

<table>
  <thead>
    <tr>
      <th>Name</th>
      <th>Summary</th>
    </tr>
  </thead>
  <tbody>

<tr>
  <td><a href="#array_type">Array type</a>
</td>
  <td>
    An ordered list of zero or more elements of non-array values.<br/>
    SQL type name: <code>ARRAY</code>
  </td>
</tr>

<tr>
  <td><a href="#boolean_type">Boolean type</a>
</td>
  <td>
    A value that can be either <code>TRUE</code> or <code>FALSE</code>.<br/>
    SQL type name: <code>BOOL</code><br/>
    SQL aliases: <code>BOOLEAN</code>
  </td>
</tr>

<tr>
  <td><a href="#bytes_type">Bytes type</a>
</td>
  <td>
    Variable-length binary data.<br/>
    SQL type name: <code>BYTES</code>
  </td>
</tr>

<tr>
  <td><a href="#date_type">Date type</a>
</td>
  <td>
    A Gregorian calendar date, independent of time zone.<br/>
    SQL type name: <code>DATE</code>
  </td>
</tr>

<tr>
  <td><a href="#datetime_type">Datetime type</a>
</td>
  <td>
    A Gregorian date and a time, as they might be displayed on a watch,
    independent of time zone.<br/>
    SQL type name: <code>DATETIME</code>
  </td>
</tr>

<tr>
  <td><a href="#enum_type">Enum type</a>
</td>
  <td>
    Named type that enumerates a list of possible values.<br/>
    SQL type name: <code>ENUM</code>
  </td>
</tr>

<tr>
  <td><a href="#geography_type">Geography type</a>
</td>
  <td>
    A collection of points, linestrings, and polygons, which is represented as a
    point set, or a subset of the surface of the Earth.<br/>
    SQL type name: <code>GEOGRAPHY</code>
  </td>
</tr>

<tr>
  <td><a href="#graph_element_type">Graph element type</a>
</td>
  <td>
    An element in a property graph.<br/>
    SQL type name: <code>GRAPH_ELEMENT</code>
  </td>
</tr>

<tr>
  <td><a href="#interval_type">Interval type</a>
</td>
  <td>
    A duration of time, without referring to any specific point in time.<br/>
    SQL type name: <code>INTERVAL</code>
  </td>
</tr>

<tr>
  <td><a href="#json_type">JSON type</a>
</td>
  <td>
    Represents JSON, a lightweight data-interchange format.<br/>
    SQL type name: <code>JSON</code>
  </td>
</tr>

<tr>
  <td><a href="#numeric_types">Numeric types</a>
</td>
  <td>
    <p>
      A numeric value. Several types are supported.
    </p>
    
    <p>
      A 32-bit integer.<br/>
      SQL type name: <code>INT32</code>
    </p>
    
    
    <p>
      An unsigned 32-bit integer.<br/>
      SQL type name: <code>UINT32</code>
    </p>
    
    
    <p>
      A 64-bit integer.<br/>
      SQL type name: <code>INT64</code>
      
    </p>
    
    
    <p>
      An unsigned 64-bit integer.<br/>
      SQL type name: <code>UINT64</code>
    </p>
    
    
    <p>
      A decimal value with precision of 38 digits.<br/>
      SQL type name: <code>NUMERIC</code>
      <br/>
      SQL aliases: <code>DECIMAL</code>
      
    </p>
    
    
    <p>
      A decimal value with precision of approximately 76.8 digits (the 77th digit is partial).<br/>
      SQL type name: <code>BIGNUMERIC</code>
      <br/>
      SQL aliases: <code>BIGDECIMAL</code>
      
    </p>
    
    
    <p>
      An approximate single precision numeric value.<br/>
      SQL type name: <code>FLOAT</code>
      <br/>
      SQL aliases: <code>FLOAT32</code>
      
    </p>
    
    
    <p>
      An approximate double precision numeric value.<br/>
      SQL type name: <code>DOUBLE</code>
      <br/>
      SQL aliases: <code>FLOAT64</code>
      
    </p>
    
  </td>
</tr>

<tr>
  <td><a href="#protocol_buffer_type">Protocol buffer type</a>
</td>
  <td>
    A protocol buffer.<br/>
    SQL type name: <code>PROTO</code>
  </td>
</tr>

<tr>
  <td><a href="#range_type">Range type</a>
</td>
  <td>
    Contiguous range between two dates, datetimes, or timestamps.<br/>
    SQL type name: <code>RANGE</code>
  </td>
</tr>

<tr>
  <td><a href="#string_type">String type</a>
</td>
  <td>
    Variable-length character data.<br/>
    SQL type name: <code>STRING</code>
  </td>
</tr>

<tr>
  <td><a href="#struct_type">Struct type</a>
</td>
  <td>
    Container of ordered fields.<br/>
    SQL type name: <code>STRUCT</code>
  </td>
</tr>

<tr>
  <td><a href="#time_type">Time type</a>
</td>
  <td>
    A time of day, as might be displayed on a clock, independent of a specific
    date and time zone.<br/>
    SQL type name: <code>TIME</code>
  </td>
</tr>

<tr>
  <td><a href="#timestamp_type">Timestamp type</a>
</td>
  <td>
    A timestamp value represents an absolute point in time,
    independent of any time zone or convention such as
    daylight saving time (DST).<br/>
    SQL type name: <code>TIMESTAMP</code>
  </td>
</tr>

<tr>
  <td><a href="#uuid_type">UUID type</a>
</td>
  <td>
     A universally unique identifier (UUID) represented as a 128-bit number.
  </td>
</tr>

  </tbody>
</table>

## Data type properties

When storing and querying data, it's helpful to keep the following data type
properties in mind:

### Nullable data types

For nullable data types, `NULL` is a valid value. Currently, all existing
data types are nullable. Conditions apply for
[arrays][array-nulls].

### Orderable data types

Expressions of orderable data types can be used in an `ORDER BY` clause.
Applies to all data types except for:

+ `PROTO`
+ `STRUCT`
+ `GEOGRAPHY`
+ `JSON`
+ `GRAPH_ELEMENT`

#### Ordering `NULL`s 
<a id="orderable_nulls"></a>

In the context of the `ORDER BY` clause, `NULL`s are the minimum
possible value; that is, `NULL`s appear first in `ASC` sorts and last in
`DESC` sorts.

`NULL` values can be specified as the first or last values for a column
irrespective of `ASC` or `DESC` by using the `NULLS FIRST` or `NULLS LAST`
modifiers respectively.

To learn more about using `ASC`, `DESC`, `NULLS FIRST` and `NULLS LAST`, see
the [`ORDER BY` clause][order-by-clause].

#### Ordering floating points 
<a id="orderable_floating_points"></a>

Floating point values are sorted in this order, from least to greatest:

1. `NULL`
2. `NaN` &mdash; All `NaN` values are considered equal when sorting.
3. `-inf`
4. Negative numbers
5. 0 or -0 &mdash; All zero values are considered equal when sorting.
6. Positive numbers
7. `+inf`

#### Ordering arrays 
<a id="orderable_arrays"></a>

`ARRAY<T>` is orderable if its type, `T`, is orderable. Empty arrays are
sorted before non-empty arrays. Non-empty arrays are sorted
lexicographically by element. An array that's a strict prefix of another array
orders less than the longer array.

Lexicographical ordering for arrays first compares the elements of each array
from the first element to the last. If an element orders before a corresponding
element in another array, then the arrays are ordered accordingly. Subsequent
array elements are ignored.

For example:

```googlesql
WITH
  t AS (
    SELECT [1, 2] a UNION ALL
    SELECT [1, NULL] a UNION ALL
    SELECT [0, 1] UNION ALL
    SELECT [0, 1, 4] UNION ALL
    SELECT [0, 1, 5] UNION ALL
    SELECT [3] UNION ALL
    SELECT [] UNION ALL
    SELECT CAST(NULL AS ARRAY<INT64>)
  )
SELECT a FROM t ORDER BY a

/*-----------+
 | a         |
 +-----------+
 | NULL      |
 | []        |
 | [0, 1]    |
 | [0, 1, 4] |
 | [0, 1, 5] |
 | [1, NULL] |
 | [1, 2]    |
 | [3]       |
 +-----------*/
```

### Groupable data types

Groupable data types can generally appear in an expression following `GROUP BY`,
`DISTINCT`, and `PARTITION BY`. All data types are supported except for:

+ `PROTO`
+ `GEOGRAPHY`
+ `JSON`

#### Grouping with floating point types

Groupable floating point types can appear in an expression following `GROUP BY`
and `DISTINCT`. `PARTITION BY` expressions can't
include [floating point types][floating-point-types].

Special floating point values are grouped in the following way, including
both grouping done by a `GROUP BY` clause and grouping done by the
`DISTINCT` keyword:

+ `NULL`
+ `NaN` &mdash; All `NaN` values are considered equal when grouping.
+ `-inf`
+ 0 or -0 &mdash; All zero values are considered equal when grouping.
+ `+inf`

#### Grouping with arrays 
<a id="group_with_arrays"></a>

An `ARRAY` type is groupable if its element type is
groupable.

Two arrays are in the same group if and only if one of the following statements
is true:

+ The two arrays are both `NULL`.
+ The two arrays have the same number of elements and all corresponding
  elements are in the same groups.

#### Grouping with structs 
<a id="group_with_structs"></a>

A `STRUCT` type is groupable if its field types are
groupable.

Two structs are in the same group if and only if one of the following statements
is true:

+ The two structs are both `NULL`.
+ All corresponding field values between the structs are in the same groups.

### Comparable data types

Values of the same comparable data type can be compared to each other.
All data types are supported except for:

+ `PROTO`
+ `GEOGRAPHY`
+ `JSON`

Notes:

+ Equality comparisons for array data types are supported as long as the
  element types are the same, and the element types are comparable. Less than
  and greater than comparisons aren't supported.
+ Equality comparisons for structs are supported field by field, in
  field order. Field names are ignored. Less than and greater than comparisons
  aren't supported.
+ To compare geography values, use [ST_Equals][st-equals].
+ When comparing ranges, the lower bounds are compared. If the lower bounds are
  equal, the upper bounds are compared, instead.
+ When comparing ranges, `NULL` values are handled as follows:
  + `NULL` lower bounds are sorted before non-`NULL` lower bounds.
  + `NULL` upper bounds are sorted after non-`NULL` upper bounds.
  + If two bounds that are being compared are `NULL`, the comparison is `TRUE`.
  + An `UNBOUNDED` bound is treated as a `NULL` bound.
+ All types that support comparisons can be used in a `JOIN` condition.
 See [JOIN Types][join-types] for an explanation of join conditions.

### Collatable data types

Collatable data types support collation, which determines how to sort and
compare strings. These data types support collation:

+ String
+ String fields in a struct
+ String elements in an array

## Array type 
<a id="array_type"></a>

<table>
<thead>
<tr>
<th>Name</th>
<th>Description</th>
</tr>
</thead>
<tbody>
<tr>
<td><code>ARRAY</code></td>
<td>Ordered list of zero or more elements of any non-array type.</td>
</tr>
</tbody>
</table>

An array is an ordered list of zero or more elements of non-array values.
Elements in an array must share the same type.

Arrays of arrays aren't allowed. Queries that would produce an array of
arrays return an error. Instead, a struct must be inserted between the
arrays using the `SELECT AS STRUCT` construct.

To learn more about the literal representation of an array type,
see [Array literals][array-literals].

To learn more about using arrays in GoogleSQL, see [Work with
arrays][working-with-arrays].

### `NULL`s and the array type 
<a id="array_nulls"></a>

Currently, GoogleSQL has the following rules with respect to `NULL`s and
arrays:

+ An array can be `NULL`.

  For example:

  ```googlesql
  SELECT CAST(NULL AS ARRAY<INT64>) IS NULL AS array_is_null;

  /*---------------+
   | array_is_null |
   +---------------+
   | TRUE          |
   +---------------*/
  ```
+ GoogleSQL translates a `NULL` array into an empty array in the query
  result, although inside the query, `NULL` and empty arrays are two distinct
  values.

  For example:

  ```googlesql
  WITH Items AS (
    SELECT [] AS numbers, "Empty array in query" AS description UNION ALL
    SELECT CAST(NULL AS ARRAY<INT64>), "NULL array in query")
  SELECT numbers, description, numbers IS NULL AS numbers_null
  FROM Items;

  /*---------+----------------------+--------------+
   | numbers | description          | numbers_null |
   +---------+----------------------+--------------+
   | []      | Empty array in query | false        |
   | []      | NULL array in query  | true         |
   +---------+----------------------+--------------*/
  ```

  When you write a `NULL` array to a table, it's converted to an
  empty array. If you write `Items` to a table from the previous query,
  then each array is written as an empty array:

  ```googlesql
  SELECT numbers, description, numbers IS NULL AS numbers_null
  FROM Items;

  /*---------+----------------------+--------------+
   | numbers | description          | numbers_null |
   +---------+----------------------+--------------+
   | []      | Empty array in query | false        |
   | []      | NULL array in query  | false        |
   +---------+----------------------+--------------*/
  ```

### Declaring an array type

```
ARRAY<T>
```

Array types are declared using the angle brackets (`<` and `>`). The type
of the elements of an array can be arbitrarily complex with the exception that
an array can't directly contain another array.

**Examples**

<table>
<thead>
<tr>
<th>Type Declaration</th>
<th>Meaning</th>
</tr>
</thead>
<tbody>
<tr>
<td>
<code>
ARRAY&lt;INT64&gt;
</code>
</td>
<td>Simple array of 64-bit integers.</td>
</tr>
<tr>
<td style="white-space:nowrap">
<code>
ARRAY&lt;STRUCT&lt;INT64, INT64&gt;&gt;
</code>
</td>
<td>An array of structs, each of which contains two 64-bit integers.</td>
</tr>
<tr>
<td style="white-space:nowrap">
<code>
ARRAY&lt;ARRAY&lt;INT64&gt;&gt;
</code><br/>
(not supported)
</td>
<td>This is an <strong>invalid</strong> type declaration which is included here
just in case you came looking for how to create a multi-level array. Arrays
can't contain arrays directly. Instead see the next example.</td>
</tr>
<tr>
<td style="white-space:nowrap">
<code>
ARRAY&lt;STRUCT&lt;ARRAY&lt;INT64&gt;&gt;&gt;
</code>
</td>
<td>An array of arrays of 64-bit integers. Notice that there is a struct between
the two arrays because arrays can't hold other arrays directly.</td>
</tr>
<tbody>
</table>

### Constructing an array 
<a id="constructing_an_array"></a>

You can construct an array using array literals or array functions.

#### Using array literals

You can build an array literal in GoogleSQL using brackets (`[` and
`]`). Each element in an array is separated by a comma.

```googlesql
SELECT [1, 2, 3] AS numbers;

SELECT ["apple", "pear", "orange"] AS fruit;

SELECT [true, false, true] AS booleans;
```

You can also create arrays from any expressions that have compatible types. For
example:

```googlesql
SELECT [a, b, c]
FROM
  (SELECT 5 AS a,
          37 AS b,
          406 AS c);

SELECT [a, b, c]
FROM
  (SELECT CAST(5 AS INT64) AS a,
          CAST(37 AS DOUBLE) AS b,
          406 AS c);
```

Notice that the second example contains three expressions: one that returns an
`INT64`, one that returns a `DOUBLE`, and one that
declares a literal. This expression works because all three expressions share
`DOUBLE` as a supertype.

To declare a specific data type for an array, use angle
brackets (`<` and `>`). For example:

```googlesql
SELECT ARRAY<DOUBLE>[1, 2, 3] AS floats;
```

Arrays of most data types, such as `INT64` or `STRING`, don't require
that you declare them first.

```googlesql
SELECT [1, 2, 3] AS numbers;
```

You can write an empty array of a specific type using `ARRAY<type>[]`. You can
also write an untyped empty array using `[]`, in which case GoogleSQL
attempts to infer the array type from the surrounding context. If
GoogleSQL can't infer a type, the default type `ARRAY<INT64>` is used.

#### Using generated values

You can also construct an `ARRAY` with generated values.

##### Generating arrays of integers

[`GENERATE_ARRAY`][generate-array-function]
generates an array of values from a starting and ending value and a step value.
For example, the following query generates an array that contains all of the odd
integers from 11 to 33, inclusive:

```googlesql
SELECT GENERATE_ARRAY(11, 33, 2) AS odds;

/*--------------------------------------------------+
 | odds                                             |
 +--------------------------------------------------+
 | [11, 13, 15, 17, 19, 21, 23, 25, 27, 29, 31, 33] |
 +--------------------------------------------------*/
```

You can also generate an array of values in descending order by giving a
negative step value:

```googlesql
SELECT GENERATE_ARRAY(21, 14, -1) AS countdown;

/*----------------------------------+
 | countdown                        |
 +----------------------------------+
 | [21, 20, 19, 18, 17, 16, 15, 14] |
 +----------------------------------*/
```

##### Generating arrays of dates

[`GENERATE_DATE_ARRAY`][generate-date-array]
generates an array of `DATE`s from a starting and ending `DATE` and a step
`INTERVAL`.

You can generate a set of `DATE` values using `GENERATE_DATE_ARRAY`. For
example, this query returns the current `DATE` and the following
`DATE`s at 1 `WEEK` intervals up to and including a later `DATE`:

```googlesql
SELECT
  GENERATE_DATE_ARRAY('2017-11-21', '2017-12-31', INTERVAL 1 WEEK)
    AS date_array;

/*--------------------------------------------------------------------------+
 | date_array                                                               |
 +--------------------------------------------------------------------------+
 | [2017-11-21, 2017-11-28, 2017-12-05, 2017-12-12, 2017-12-19, 2017-12-26] |
 +--------------------------------------------------------------------------*/
```

[array-literals]: https://github.com/google/googlesql/blob/master/docs/lexical.md#array_literals

[working-with-arrays]: https://github.com/google/googlesql/blob/master/docs/arrays.md#constructing_arrays

[generate-array-function]: https://github.com/google/googlesql/blob/master/docs/array_functions.md#generate_array

[generate-date-array]: https://github.com/google/googlesql/blob/master/docs/array_functions.md#generate_date_array

## Boolean type 
<a id="boolean_type"></a>

<table>
<thead>
<tr>
<th>Name</th>
<th>Description</th>
</tr>
</thead>
<tbody>
<tr>
<td>
  <code>BOOL</code><br/>
  <code>BOOLEAN</code>
</td>
<td>Boolean values are represented by the keywords <code>TRUE</code> and
<code>FALSE</code> (case-insensitive).</td>
</tr>
</tbody>
</table>

`BOOLEAN` is an alias for `BOOL`.

Boolean values are sorted in this order, from least to greatest:

  1. `NULL`
  1. `FALSE`
  1. `TRUE`

## Bytes type 
<a id="bytes_type"></a>

<table>
<thead>
<tr>
<th>Name</th>
<th>Description</th>
</tr>
</thead>
<tbody>
<tr>
<td><code>BYTES</code></td>
<td>Variable-length binary data.</td>
</tr>
</tbody>
</table>

String and bytes are separate types that can't be used interchangeably.
Most functions on strings are also defined on bytes. The bytes version
operates on raw bytes rather than Unicode characters. Casts between string and
bytes enforce that the bytes are encoded using UTF-8.

You can convert a base64-encoded `STRING` expression into the `BYTES` format
using the
[`FROM_BASE64` function][from-base].
You can also convert a sequence of `BYTES` into a base64-encoded `STRING`
expression using the
[`TO_BASE64` function][to-base].

To learn more about the literal representation of a bytes type,
see [Bytes literals][bytes-literals].

[bytes-literals]: https://github.com/google/googlesql/blob/master/docs/lexical.md#string_and_bytes_literals

[from-base]: https://github.com/google/googlesql/blob/master/docs/string_functions.md#from_base64

[to-base]: https://github.com/google/googlesql/blob/master/docs/string_functions.md#to_base64

## Date type 
<a id="date_type"></a>

<table>
<thead>
<tr>
<th>Name</th>
<th>Range</th>
</tr>
</thead>
<tbody>
<tr>
<td><code>DATE</code></td>
<td>0001-01-01 to 9999-12-31.</td>
</tr>
</tbody>
</table>

The date type represents a Gregorian calendar date, independent of time zone. A
date value doesn't represent a specific 24-hour time period. Rather, a given
date value represents a different 24-hour period when interpreted in different
time zones, and may represent a shorter or longer day during daylight saving
time (DST) transitions.
To represent an absolute point in time,
use a [timestamp][timestamp-type].

##### Canonical format 
<a id="canonical_format_for_date_literals"></a>

```
YYYY-[M]M-[D]D
```

+ `YYYY`: Four-digit year.
+ `[M]M`: One or two digit month.
+ `[D]D`: One or two digit day.

To learn more about the literal representation of a date type,
see [Date literals][date-literals].

[timestamp-type]: #timestamp_type

[date-literals]: https://github.com/google/googlesql/blob/master/docs/lexical.md#date_literals

## Datetime type 
<a id="datetime_type"></a>

<table>
<thead>
<tr>
<th>Name</th>
<th>Range</th>
</tr>
</thead>
<tbody>
<tr>
<td><code>DATETIME</code></td>
<td>
    
        0001-01-01 00:00:00 to 9999-12-31 23:59:59.999999999<br/>
        <hr/>
        0001-01-01 00:00:00 to 9999-12-31 23:59:59.999999<br/>
    
</td>
</tr>
</tbody>
</table>

A datetime value represents a Gregorian date and a time,
as they might be displayed on a watch, independent of time zone.
It includes the year, month, day, hour, minute, second,
and subsecond.
The range of subsecond precision is determined by the SQL engine.
To represent an absolute point in time,
use a [timestamp][timestamp-type].

##### Canonical format 
<a id="canonical_format_for_datetime_literals"></a>

```
civil_date_part[time_part]

civil_date_part:
    YYYY-[M]M-[D]D

time_part:
    { |T|t}[H]H:[M]M:[S]S[.F]
```

+ <code>YYYY</code>: Four-digit year.
+ <code>[M]M</code>: One or two digit month.
+ <code>[D]D</code>: One or two digit day.
+ <code>{ |T|t}</code>: A space or a `T` or `t` separator. The `T` and `t`
  separators are flags for time.
+ <code>[H]H</code>: One or two digit hour (valid values from 00 to 23).
+ <code>[M]M</code>: One or two digit minutes (valid values from 00 to 59).
+ <code>[S]S</code>: One or two digit seconds (valid values from 00 to 60).
+ <code>[.F]</code>: Up to nine fractional
  digits (nanosecond precision).

To learn more about the literal representation of a datetime type,
see [Datetime literals][datetime-literals].

[timestamp-type]: #timestamp_type

[datetime-literals]: https://github.com/google/googlesql/blob/master/docs/lexical.md#datetime_literals

## Enum type 
<a id="enum_type"></a>

<table>
<thead>
<tr>
<th>Name</th>
<th>Description</th>
</tr>
</thead>
<tbody>
<tr>
<td><code>ENUM</code></td>
<td>Named type that maps string constants to <code>INT32</code> constants.</td>
</tr>
</tbody>
</table>

An enum is a named type that enumerates a list of possible values, each of which
contains:

+ An integer value: Integers are used for comparison and ordering enum values.
There is no requirement that these integers start at zero or that they be
contiguous.
+ A string value for its name: Strings are case sensitive. In the case of
protocol buffer open enums, this name is optional.
+ Optional alias values: One or more additional string values that act as
aliases.

Enum values are referenced using their integer value or their string value.
You reference an enum type, such as when using CAST, by using its fully
qualified name.

You can't create new enum types using GoogleSQL.

To learn more about the literal representation of an enum type,
see [Enum literals][enum-literals].

[enum-literals]: https://github.com/google/googlesql/blob/master/docs/lexical.md#enum_literals

## Geography type 
<a id="geography_type"></a>

<table>
<thead>
<tr>
<th>Name</th>
<th>Description</th>
</tr>
</thead>
<tbody>
<tr>
<td><code>GEOGRAPHY</code></td>
<td>
  A collection of points, linestrings, and polygons, which is represented as a
  point set, or a subset of the surface of the Earth.
</td>
</tr>
</tbody>
</table>

The geography type is based on the [OGC Simple
Features specification (SFS)][ogc-sfs]{: class=external target=_blank },
and can contain the following objects:

<table>
  <thead>
    <tr>
      <th>Geography object</th>
      <th>Description</th>
    </tr>
  </thead>
  <tbody>
    <tr>
      <td><code>Point</code></td>
      <td>
        <p>
          A single location in coordinate space known as a point. A point has an
          x-coordinate value and a y-coordinate value, where the x-coordinate is
          longitude and the y-coordinate is latitude of the point
          on the
          <a href="https://en.wikipedia.org/wiki/World_Geodetic_System">WGS84 reference ellipsoid</a>.

        </p>
        <p>
          Syntax:
<pre class="lang-sql prettyprint">
POINT(x_coordinate y_coordinate)
</pre>
          Examples:
<pre class="lang-sql prettyprint">
POINT(32 210)
</pre>
<pre class="lang-sql prettyprint">
POINT EMPTY
</pre>
        </p>
      </td>
    </tr>
    <tr>
      <td><code>LineString</code></td>
      <td>
        <p>
          Represents a linestring, which is a one-dimensional geometric object,
          with a sequence of points and geodesic edges between them.
        </p>
        <p>
          Syntax:
<pre class="lang-sql prettyprint">
LINESTRING(point[, ...])
</pre>
          Examples:
<pre class="lang-sql prettyprint">
LINESTRING(1 1, 2 1, 3.1 2.88, 3 -3)
</pre>
<pre class="lang-sql prettyprint">
LINESTRING EMPTY
</pre>
        </p>
      </td>
    </tr>
    <tr>
      <td><code>Polygon</code></td>
      <td>
        <p>
          A polygon, which is represented as a planar surface defined by 1
          exterior boundary and 0 or more interior boundaries. Each
          interior boundary defines a hole in the polygon. The boundary loops of
          polygons are oriented so that if you traverse the boundary vertices in
          order, the interior of the polygon is on the left.
        </p>
        <p>
          Syntax:
<pre class="lang-sql prettyprint">
POLYGON(interior_ring[, ...])

interior_ring:
  (point[, ...])
</pre>
          Examples:
<pre class="lang-sql prettyprint">
POLYGON((0 0, 2 2, 2 0, 0 0), (2 2, 3 4, 2 4, 2 2))
</pre>
<pre class="lang-sql prettyprint">
POLYGON EMPTY
</pre>
        </p>
      </td>
    </tr>
    <tr>
      <td><code>MultiPoint</code></td>
      <td>
        <p>
          A collection of points.
        </p>
        <p>
          Syntax:
<pre class="lang-sql prettyprint">
MULTIPOINT(point[, ...])
</pre>
          Examples:
<pre class="lang-sql prettyprint">
MULTIPOINT(0 32, 123 9, 48 67)
</pre>
<pre class="lang-sql prettyprint">
MULTIPOINT EMPTY
</pre>
        </p>
      </td>
    </tr>
    <tr>
      <td><code>MultiLineString</code></td>
      <td>
        <p>
          Represents a multilinestring, which is a collection of linestrings.
        </p>
        <p>
          Syntax:
<pre class="lang-sql prettyprint">
MULTILINESTRING((linestring)[, ...])
</pre>
          Examples:
<pre class="lang-sql prettyprint">
MULTILINESTRING((2 2, 3 4), (5 6, 7 7))
</pre>
<pre class="lang-sql prettyprint">
MULTILINESTRING EMPTY
</pre>
        </p>
      </td>
    </tr>
    <tr>
      <td><code>MultiPolygon</code></td>
      <td>
        <p>
          Represents a multipolygon, which is a collection of polygons.
        </p>
        <p>
          Syntax:
<pre class="lang-sql prettyprint">
MULTIPOLYGON((polygon)[, ...])
</pre>
          Examples:
<pre class="lang-sql prettyprint">
MULTIPOLYGON(((0 -1, 1 0, 1 1, 0 -1)), ((0 0, 2 2, 3 0, 0 0), (2 2, 3 4, 2 4, 1 9)))
</pre>
<pre class="lang-sql prettyprint">
MULTIPOLYGON EMPTY
</pre>
        </p>
      </td>
    </tr>
    <tr>
      <td><code>GeometryCollection</code></td>
      <td>
        <p>
          Represents a geometry collection with elements of different dimensions
          or an empty geography.
        </p>
        <p>
          Syntax:
<pre class="lang-sql prettyprint">
GEOMETRYCOLLECTION(geography_object[, ...])
</pre>
          Examples:
<pre class="lang-sql prettyprint">
GEOMETRYCOLLECTION(MULTIPOINT(-1 2, 0 12), LINESTRING(-2 4, 0 6))
</pre>
<pre class="lang-sql prettyprint">
GEOMETRYCOLLECTION EMPTY
</pre>
        </p>
      </td>
    </tr>
  </tbody>
</table>

The points, linestrings and polygons of a geography value form a simple
arrangement on the [WGS84 reference ellipsoid][WGS84-reference-ellipsoid].
A simple arrangement is one where no point on the WGS84 surface is contained
by multiple elements of the collection. If self intersections exist, they
are automatically removed.

The geography that contains no points, linestrings or polygons is called an
empty geography. An empty geography isn't associated with a particular
geometry shape. For example, the following query produces the same results:

```googlesql
SELECT
  ST_GEOGFROMTEXT('POINT EMPTY') AS a,
  ST_GEOGFROMTEXT('GEOMETRYCOLLECTION EMPTY') AS b

/*--------------------------+--------------------------+
 | a                        | b                        |
 +--------------------------+--------------------------+
 | GEOMETRYCOLLECTION EMPTY | GEOMETRYCOLLECTION EMPTY |
 +--------------------------+--------------------------*/
```

The structure of compound geometry objects isn't preserved if a
simpler type can be produced. For example, in column `b`,
`GEOMETRYCOLLECTION` with `(POINT(1 1)` and `POINT(2 2)` is converted into the
simplest possible geometry, `MULTIPOINT(1 1, 2 2)`.

```googlesql
SELECT
  ST_GEOGFROMTEXT('MULTIPOINT(1 1, 2 2)') AS a,
  ST_GEOGFROMTEXT('GEOMETRYCOLLECTION(POINT(1 1), POINT(2 2))') AS b

/*----------------------+----------------------+
 | a                    | b                    |
 +----------------------+----------------------+
 | MULTIPOINT(1 1, 2 2) | MULTIPOINT(1 1, 2 2) |
 +----------------------+----------------------*/
```

A geography is the result of, or an argument to, a
[Geography Function][geography-functions].

[ogc-sfs]: http://www.opengeospatial.org/standards/sfs#downloads

[WGS84-reference-ellipsoid]: https://en.wikipedia.org/wiki/World_Geodetic_System

[geography-functions]: https://github.com/google/googlesql/blob/master/docs/geography_functions.md

## Graph element type 
<a id="graph_element_type"></a>

<table>
<thead>
<tr>
<th>Name</th>
<th>Description</th>
</tr>
</thead>
<tbody>
<tr>
<td><code>GRAPH_ELEMENT</code></td>
<td>
  An element in a property graph.
</td>
</tr>
</tbody>
</table>

A variable with a `GRAPH_ELEMENT` type is produced by a graph query.
The generated type has this format:

```
GRAPH_ELEMENT<T>
```

A graph element is either a node or an edge, representing data from a
matching node or edge table based on its label. Each graph element holds a
set of properties that can be accessed with a case-insensitive name,
similar to fields of a struct.

**Example**

In the following example, `n` represents a graph element in the
[`FinGraph`][fin-graph] property graph:

```googlesql
GRAPH FinGraph
MATCH (n:Person)
RETURN n.name
```

In the following example, the [`TYPEOF`][type-of] function is used to inspect the
set of properties defined in the graph element type.

```googlesql
GRAPH FinGraph
MATCH (n:Person)
RETURN TYPEOF(n) AS t
LIMIT 1

/*----------------------------------------------+
 | t                                            |
 +----------------------------------------------+
 | GRAPH_NODE(FinGraph)<Id INT64, ..., DYNAMIC> |
 +---------------------------------------------*/
```

[graph-query]: https://github.com/google/googlesql/blob/master/docs/graph-intro.md

[fin-graph]: https://github.com/google/googlesql/blob/master/docs/graph-schema-statements.md#fin_graph

## Interval type 
<a id="interval_type"></a>

<table>
<thead>
<tr>
<th>Name</th>
<th>Range</th>
</tr>
</thead>
<tbody>
<tr>
<td><code>INTERVAL</code></td>
<td>
-10000-0 -3660000 -87840000:0:0 to 10000-0 3660000 87840000:0:0
</td>
</tr>
</tbody>
</table>

An `INTERVAL` object represents duration or amount of time, without referring
to any specific point in time.

##### Canonical format

```
[sign]Y-M [sign]D [sign]H:M:S[.F]
```

+ `sign`: `+` or `-`
+ `Y`: Year
+ `M`: Month
+ `D`: Day
+ `H`: Hour
+ `M`: Minute
+ `S`: Second
+ `[.F]`: Up to nine fractional
  digits (nanosecond precision)

To learn more about the literal representation of an interval type,
see [Interval literals][interval-literals].

### Constructing an interval 
<a id="construct_interval"></a>

You can construct an interval with an interval literal that supports
a [single datetime part][single-datetime-part-interval] or a
[datetime part range][range-datetime-part-interval].

#### Construct an interval with a single datetime part 
<a id="single_datetime_part_interval"></a>

```googlesql
INTERVAL int64_expression datetime_part
```

You can construct an `INTERVAL` object with an `INT64` expression and one
[interval-supported datetime part][interval-datetime-parts]. For example:

```googlesql
-- 1 year, 0 months, 0 days, 0 hours, 0 minutes, and 0 seconds (1-0 0 0:0:0)
INTERVAL 1 YEAR
INTERVAL 4 QUARTER
INTERVAL 12 MONTH

-- 0 years, 3 months, 0 days, 0 hours, 0 minutes, and 0 seconds (0-3 0 0:0:0)
INTERVAL 1 QUARTER
INTERVAL 3 MONTH

-- 0 years, 0 months, 42 days, 0 hours, 0 minutes, and 0 seconds (0-0 42 0:0:0)
INTERVAL 6 WEEK
INTERVAL 42 DAY

-- 0 years, 0 months, 0 days, 25 hours, 0 minutes, and 0 seconds (0-0 0 25:0:0)
INTERVAL 25 HOUR
INTERVAL 1500 MINUTE
INTERVAL 90000 SECOND

-- 0 years, 0 months, 0 days, 1 hours, 30 minutes, and 0 seconds (0-0 0 1:30:0)
INTERVAL 90 MINUTE

-- 0 years, 0 months, 0 days, 0 hours, 1 minutes, and 30 seconds (0-0 0 0:1:30)
INTERVAL 90 SECOND

-- 0 years, 0 months, -5 days, 0 hours, 0 minutes, and 0 seconds (0-0 -5 0:0:0)
INTERVAL -5 DAY
```

For additional examples, see [Interval literals][interval-literal-single].

#### Construct an interval with a datetime part range 
<a id="range_datetime_part_interval"></a>

```googlesql
INTERVAL datetime_parts_string starting_datetime_part TO ending_datetime_part
```

You can construct an `INTERVAL` object with a `STRING` that contains the
datetime parts that you want to include, a starting datetime part, and an ending
datetime part. The resulting `INTERVAL` object only includes datetime parts in
the specified range.

You can use one of the following formats with the
[interval-supported datetime parts][interval-datetime-parts]:

<table>
  <thead>
    <tr>
      <th>Datetime part string</th>
      <th>Datetime parts</th>
      <th>Example</th>
    </tr>
  </thead>
  <tbody>
    <tr>
      <td><code>Y-M</code></td>
      <td><code>YEAR TO MONTH</code></td>
      <td><code>INTERVAL '2-11' YEAR TO MONTH</code></td>
    </tr>
    <tr>
      <td><code>Y-M D</code></td>
      <td><code>YEAR TO DAY</code></td>
      <td><code>INTERVAL '2-11 28' YEAR TO DAY</code></td>
    </tr>
    <tr>
      <td><code>Y-M D H</code></td>
      <td><code>YEAR TO HOUR</code></td>
      <td><code>INTERVAL '2-11 28 16' YEAR TO HOUR</code></td>
    </tr>
    <tr>
      <td><code>Y-M D H:M</code></td>
      <td><code>YEAR TO MINUTE</code></td>
      <td><code>INTERVAL '2-11 28 16:15' YEAR TO MINUTE</code></td>
    </tr>
    <tr>
      <td><code>Y-M D H:M:S</code></td>
      <td><code>YEAR TO SECOND</code></td>
      <td><code>INTERVAL '2-11 28 16:15:14' YEAR TO SECOND</code></td>
    </tr>
    <tr>
      <td><code>M D</code></td>
      <td><code>MONTH TO DAY</code></td>
      <td><code>INTERVAL '11 28' MONTH TO DAY</code></td>
    </tr>
    <tr>
      <td><code>M D H</code></td>
      <td><code>MONTH TO HOUR</code></td>
      <td><code>INTERVAL '11 28 16' MONTH TO HOUR</code></td>
    </tr>
    <tr>
      <td><code>M D H:M</code></td>
      <td><code>MONTH TO MINUTE</code></td>
      <td><code>INTERVAL '11 28 16:15' MONTH TO MINUTE</code></td>
    </tr>
    <tr>
      <td><code>M D H:M:S</code></td>
      <td><code>MONTH TO SECOND</code></td>
      <td><code>INTERVAL '11 28 16:15:14' MONTH TO SECOND</code></td>
    </tr>
    <tr>
      <td><code>D H</code></td>
      <td><code>DAY TO HOUR</code></td>
      <td><code>INTERVAL '28 16' DAY TO HOUR</code></td>
    </tr>
    <tr>
      <td><code>D H:M</code></td>
      <td><code>DAY TO MINUTE</code></td>
      <td><code>INTERVAL '28 16:15' DAY TO MINUTE</code></td>
    </tr>
    <tr>
      <td><code>D H:M:S</code></td>
      <td><code>DAY TO SECOND</code></td>
      <td><code>INTERVAL '28 16:15:14' DAY TO SECOND</code></td>
    </tr>
    <tr>
      <td><code>H:M</code></td>
      <td><code>HOUR TO MINUTE</code></td>
      <td><code>INTERVAL '16:15' HOUR TO MINUTE</code></td>
    </tr>
    <tr>
      <td><code>H:M:S</code></td>
      <td><code>HOUR TO SECOND</code></td>
      <td><code>INTERVAL '16:15:14' HOUR TO SECOND</code></td>
    </tr>
    <tr>
      <td><code>M:S</code></td>
      <td><code>MINUTE TO SECOND</code></td>
      <td><code>INTERVAL '15:14' MINUTE TO SECOND</code></td>
    </tr>
  </tbody>
</table>

For example:

```googlesql
-- 0 years, 8 months, 20 days, 17 hours, 0 minutes, and 0 seconds (0-8 20 17:0:0)
INTERVAL '8 20 17' MONTH TO HOUR

-- 0 years, 8 months, -20 days, 17 hours, 0 minutes, and 0 seconds (0-8 -20 17:0:0)
INTERVAL '8 -20 17' MONTH TO HOUR
```

For additional examples, see [Interval literals][interval-literal-range].

#### Interval-supported date and time parts 
<a id="interval_datetime_parts"></a>

You can use the following date parts to construct an interval:

+ `YEAR`: Number of years, `Y`.
+ `QUARTER`: Number of quarters; each quarter is converted to `3` months, `M`.
+ `MONTH`: Number of months, `M`. Each `12` months is converted to `1` year.
+ `WEEK`: Number of weeks; Each week is converted to `7` days, `D`.
+ `DAY`: Number of days, `D`.

You can use the following time parts to construct an interval:

+ `HOUR`: Number of hours, `H`.
+ `MINUTE`: Number of minutes, `M`. Each `60` minutes is converted to `1` hour.
+ `SECOND`: Number of seconds, `S`. Each `60` seconds is converted to
  `1` minute. Can include up to nine fractional
  digits (nanosecond precision).
+ `MILLISECOND`: Number of milliseconds.
+ `MICROSECOND`: Number of microseconds.
+ `NANOSECOND`: Number of nanoseconds.

[interval-datetime-parts]: #interval_datetime_parts

[single-datetime-part-interval]: #single_datetime_part_interval

[range-datetime-part-interval]: #range_datetime_part_interval

[interval-literals]: https://github.com/google/googlesql/blob/master/docs/lexical.md#interval_literals

[interval-literal-single]: https://github.com/google/googlesql/blob/master/docs/lexical.md#interval_literal_single

[interval-literal-range]: https://github.com/google/googlesql/blob/master/docs/lexical.md#interval_literal_range

## JSON type 
<a id="json_type"></a>

<table>
<thead>
<tr>
<th>Name</th>
<th>Description</th>
</tr>
</thead>
<tbody>
<tr>
<td><code>JSON</code></td>
<td>Represents JSON, a lightweight data-interchange format.</td>
</tr>
</tbody>
</table>

Expect these canonicalization behaviors when creating a value of JSON type:

+  Booleans, strings, and nulls are preserved exactly.
+  Whitespace characters aren't preserved.
+  A JSON value can store integers in the range of
   -9,223,372,036,854,775,808 (minimum signed 64-bit integer) to
   18,446,744,073,709,551,615 (maximum unsigned 64-bit integer) and
   floating point numbers within a domain of
   `DOUBLE`.
+  The order of elements in an array is preserved exactly.
+  The order of the members of an object isn't guaranteed or preserved.
+  If an object has duplicate keys, the first key that's found is preserved.
+  The format of the original string representation of a JSON number may not be
   preserved.

To learn more about the literal representation of a JSON type,
see [JSON literals][json-literals].

[json-literals]: https://github.com/google/googlesql/blob/master/docs/lexical.md#json_literals

## Numeric types 
<a id="numeric_types"></a>

Numeric types include the following types:

+ `INT32`
+ `UINT32`
+ `INT64`
+ `UINT64`
+ `NUMERIC` with alias `DECIMAL`
+ `BIGNUMERIC` with alias `BIGDECIMAL`
+ `FLOAT`
+ `DOUBLE`

### Integer types 
<a id="integer_types"></a>

Integers are numeric values that don't have fractional components.

<table>
<thead>
<tr>
<th>Name</th>
<th>Range</th>
</tr>
</thead>
<tbody>

<tr>
<td><code>INT32</code></td>
<td>-2,147,483,648 to 2,147,483,647</td>
</tr>

<tr>
<td><code>UINT32</code></td>
<td>0 to 4,294,967,295</td>
</tr>

<tr>
<td><code>INT64</code>
</td>
<td>-9,223,372,036,854,775,808 to 9,223,372,036,854,775,807</td>
</tr>

<tr>
<td><code>UINT64</code></td>
<td>0 to 18,446,744,073,709,551,615</td>
</tr>

</tbody>
</table>

To learn more about the literal representation of an integer type,
see [Integer literals][integer-literals].

### Decimal types 
<a id="decimal_types"></a>

Decimal type values are numeric values with fixed decimal precision and scale.
Precision is the number of digits that the number contains. Scale is
how many of these digits appear after the decimal point.

This type can represent decimal fractions exactly, and is suitable for financial
calculations.

<table>
<thead>
<tr>
  <th>Name</th>
  <th>Precision, Scale, and Range</th>
</tr>
</thead>
<tbody>

<tr id="numeric_type">
  <td id="numeric-type" style="vertical-align:middle"><code>NUMERIC</code>
    <br><code>DECIMAL</code></td>
  <td style="vertical-align:middle">
    Precision: 38<br>
    Scale: 9<br>
    Minimum value greater than 0 that can be handled: 1e-9<br>
    Min: -9.9999999999999999999999999999999999999E+28<br>
    Max: 9.9999999999999999999999999999999999999E+28<br>
  </td>
</tr>

<tr id="bignumeric_type">
  <td id="bignumeric-type" style="vertical-align:middle"><code>BIGNUMERIC</code>
    <br><code>BIGDECIMAL</code></td>
  <td style="vertical-align:middle">
    Precision: approximately 76.8 digits (the 77th digit is partial)<br>
    Scale: 38<br>
    Minimum value greater than 0 that can be handled: 1e-38<br>
    Min: <small>-5.7896044618658097711785492504343953926634992332820282019728792003956564819968E+38</small><br>
    Max: <small>5.7896044618658097711785492504343953926634992332820282019728792003956564819967E+38</small><br>
  </td>
</tr>

</tbody>
</table>

`DECIMAL` is an alias for `NUMERIC`.

`BIGDECIMAL` is an alias for `BIGNUMERIC`.

To learn more about the literal representation of a `NUMERIC` type,
see [`NUMERIC` literals][numeric-literals].

To learn more about the literal representation of a `BIGNUMERIC` type,
see [`BIGNUMERIC` literals][bignumeric-literals].

### Floating point types 
<a id="floating_point_types"></a>

Floating point values are approximate numeric values with fractional components.

<table>
<thead>
<tr>
<th>Name</th>
<th>Description</th>
</tr>
</thead>
<tbody>

<tr id="float_type">
  <td id="float-type" style="vertical-align:middle"><code>FLOAT</code>
    <br><code>FLOAT32</code></td>
<td>
  Single precision (approximate) numeric values.
</td>
</tr>

<tr id="double_type">
  <td id="double-type" style="vertical-align:middle"><code>DOUBLE</code>
    <br><code>FLOAT64</code></td>
<td>Double precision (approximate) numeric values.</td>
</tr>
</tbody>
</table>

`FLOAT32` is an alias for `FLOAT`.

`FLOAT64` is an alias for `DOUBLE`.

To learn more about the literal representation of a floating point type,
see [Floating point literals][floating-point-literals].

#### Floating point semantics

When working with floating point numbers, there are special non-numeric values
that need to be considered: `NaN` and `+/-inf`

Arithmetic operators provide standard IEEE-754 behavior for all finite input
values that produce finite output and for all operations for which at least one
input is non-finite.

Function calls and operators return an overflow error if the input is finite
but the output would be non-finite. If the input contains non-finite values, the
output can be non-finite. In general functions don't introduce `NaN`s or
`+/-inf`. However, specific functions like `IEEE_DIVIDE` can return non-finite
values on finite input. All such cases are noted explicitly in
[Mathematical functions][mathematical-functions].

Floating point values are approximations.

+ The binary format used to represent floating point values can only represent
  a subset of the numbers between the most positive number and most
  negative number in the value range. This enables efficient handling of a
  much larger range than would be possible otherwise.
  Numbers that aren't exactly representable are approximated by utilizing a
  close value instead. For example, `0.1` can't be represented as an integer
  scaled by a power of `2`. When this value is displayed as a string, it's
  rounded to a limited number of digits, and the value approximating `0.1`
  might appear as `"0.1"`, hiding the fact that the value isn't precise.
  In other situations, the approximation can be visible.
+ Summation of floating point values might produce surprising results because
  of [limited precision][floating-point-accuracy]. For example,
  `(1e30 + 1) - 1e30 = 0`, while `(1e30 - 1e30) + 1 = 1.0`. This is
  because the floating point value doesn't have enough precision to
  represent `(1e30 + 1)`, and the result is rounded to `1e30`.
  This example also shows that the result of the `SUM` aggregate function of
  floating points values depends on the order in which the values are
  accumulated. In general, this order isn't deterministic and therefore the
  result isn't deterministic. Thus, the resulting `SUM` of
  floating point values might not be deterministic and two executions of the
  same query on the same tables might produce different results.
+ If the above points are concerning, use a
  [decimal type][decimal-types] instead.

##### Mathematical function examples

<table>
<thead>
<tr>
<th>Left Term</th>
<th>Operator</th>
<th>Right Term</th>
<th>Returns</th>
</tr>
</thead>
<tbody>
<tr>
<td>Any value</td>
<td><code>+</code></td>
<td><code>NaN</code></td>
<td><code>NaN</code></td>
</tr>
<tr>
<td>1.0</td>
<td><code>+</code></td>
<td><code>+inf</code></td>
<td><code>+inf</code></td>
</tr>
<tr>
<td>1.0</td>
<td><code>+</code></td>
<td><code>-inf</code></td>
<td><code>-inf</code></td>
</tr>
<tr>
<td><code>-inf</code></td>
<td><code>+</code></td>
<td><code>+inf</code></td>
<td><code>NaN</code></td>
</tr>
<tr>
<td>Maximum <code>DOUBLE</code> value</td>
<td><code>+</code></td>
<td>Maximum <code>DOUBLE</code> value</td>
<td>Overflow error</td>
</tr>
<tr>
<td>Minimum <code>DOUBLE</code> value</td>
<td><code>/</code></td>
<td>2.0</td>
<td>0.0</td>
</tr>
<tr>
<td>1.0</td>
<td><code>/</code></td>
<td><code>0.0</code></td>
<td>"Divide by zero" error</td>
</tr>
</tbody>
</table>

Comparison operators provide standard IEEE-754 behavior for floating point
input.

##### Comparison operator examples

<table>
<thead>
<tr>
<th>Left Term</th>
<th>Operator</th>
<th>Right Term</th>
<th>Returns</th>
</tr>
</thead>
<tbody>
<tr>
<td><code>NaN</code></td>
<td><code>=</code></td>
<td>Any value</td>
<td><code>FALSE</code></td>
</tr>
<tr>
<td><code>NaN</code></td>
<td><code>&lt;</code></td>
<td>Any value</td>
<td><code>FALSE</code></td>
</tr>
<tr>
<td>Any value</td>
<td><code>&lt;</code></td>
<td><code>NaN</code></td>
<td><code>FALSE</code></td>
</tr>
<tr>
<td>-0.0</td>
<td><code>=</code></td>
<td>0.0</td>
<td><code>TRUE</code></td>
</tr>
<tr>
<td>-0.0</td>
<td><code>&lt;</code></td>
<td>0.0</td>
<td><code>FALSE</code></td>
</tr>
</tbody>
</table>

For more information on how these values are ordered and grouped so they
can be compared,
see [Ordering floating point values][orderable-floating-points].

[floating-point-accuracy]: https://en.wikipedia.org/wiki/Floating-point_arithmetic#Accuracy_problems

[decimal-types]: #decimal_types

[orderable-floating-points]: #orderable_floating_points

[integer-literals]: https://github.com/google/googlesql/blob/master/docs/lexical.md#integer_literals

[floating-point-literals]: https://github.com/google/googlesql/blob/master/docs/lexical.md#floating_point_literals

[mathematical-functions]: https://github.com/google/googlesql/blob/master/docs/mathematical_functions.md

[numeric-literals]: https://github.com/google/googlesql/blob/master/docs/lexical.md#numeric_literals

[bignumeric-literals]: https://github.com/google/googlesql/blob/master/docs/lexical.md#bignumeric_literals

## Protocol buffer type 
<a id="protocol_buffer_type"></a>

<table>
<thead>
<tr>
<th>Name</th>
<th>Description</th>
</tr>
</thead>
<tbody>
<tr>
<td><code>PROTO</code></td>
<td>An instance of protocol buffer.</td>
</tr>
</tbody>
</table>

Protocol buffers provide structured data types with a defined serialization
format and cross-language support libraries. Protocol buffer message types can
contain optional, required, or repeated fields, including nested messages. For
more information, see the [Protocol Buffers Developer Guide][protocol-buffers-dev-guide].

Protocol buffer message types behave similarly to [struct types](#struct_type),
and support similar operations like reading field values by name. Protocol
buffer types are always named types, and can be referred to by their
fully-qualified protocol buffer name (i.e. `package.ProtoName`). Protocol
buffers support some additional behavior beyond structs, like default field
values, defining a column type, and checking for the presence of
optional fields.

Protocol buffer [enum types](#enum_type) are also available and can be
referenced using the fully-qualified enum type name.

To learn more about using protocol buffers in GoogleSQL, see
[Work with protocol buffers][protocol-buffers].

### Constructing a protocol buffer 
<a id="constructing_a_proto"></a>

You can construct a protocol buffer using the [`NEW`][new-operator] operator or
the [`SELECT AS typename`][select-as-typename] statement. Regardless of the
method that you choose, the resulting protocol buffer is the same.

#### `NEW protocol_buffer {...}` {: #using_new_map_constructor }

You can create a protocol buffer using the [`NEW`][new-operator]
operator with a map constructor:

```googlesql
NEW protocol_buffer {
  field_name: literal_or_expression
  field_name { ... }
  repeated_field_name: [literal_or_expression, ... ]
  map_field_name: [{key: literal_or_expression value: literal_or_expression}, ...],
  (extension_name): literal_or_expression
}
```

Where:

+ `protocol_buffer`: The full protocol buffer name including the package name.
+ `field_name`: The name of a field.
+ `literal_or_expression`: The field value.
+  `map_field_name`: The name of a map-typed field. The value is a list of
   key/value pair entries for the map.
+  `extension_name`: The name of the proto extension, including the package
   name.

**Example**

```googlesql
NEW googlesql.examples.astronomy.Planet {
  planet_name: 'Jupiter'
  facts: {
    length_of_day: 9.93
    distance_to_sun: 5.2 * ASTRONOMICAL_UNIT
    has_rings: TRUE
  }
  major_moons: [
    { moon_name: 'Io' },
    { moon_name: 'Europa' },
    { moon_name: 'Ganymede' },
    { moon_name: 'Callisto'}
  ]
  minor_moons: (
    SELECT ARRAY_AGG(moon_name)
    FROM SolarSystemMoons
    WHERE
      planet_name = 'Jupiter'
      AND circumference < 3121
  )
  count_of_space_probe_photos: (
    GALILEO_PHOTOS
    + JUNO_PHOTOS
    + NEW_HORIZONS_PHOTOS
    + CASSINI_PHOTOS
    + ULYSSES_PHOTOS
    + VOYAGER_1_PHOTOS
    + VOYAGER_2_PHOTOS
    + PIONEER_10_PHOTOS
    + PIONEER_11_PHOTOS
  ),
  (UniverseExtraInfo.extension) {
    ...
  }
}
```

<!-- mdlint off(WHITESPACE_LINE_LENGTH) -->

> NOTE: The syntax is very similar to the Protocol Buffer Text Format
>  syntax.
> The differences are:
>
> +   Values can be arbitrary SQL expressions instead of having to be literals.
> +   Repeated fields are written as `x_array: [1, 2, 3]` instead of `x_array:`
>     appearing multiple times.
> +   Extensions use parentheses instead of square brackets.

<!-- mdlint on -->

When using this syntax, the following rules apply:

+   The field values must be expressions that are implicitly coercible or
    literal-coercible to the type of the corresponding protocol buffer field.
+   Commas between fields are optional.
+   Extension names must have parentheses around the path and must have a comma
    preceding the extension field (unless it's the first field).
+   A colon is required between field name and values unless the value is a map
    constructor.
+   The `NEW protocol_buffer` prefix is optional if the protocol buffer type can
    be inferred from the context.
+   The type of submessages inside the map constructor can be inferred.

**Examples**

Simple:

```googlesql
SELECT
  key,
  name,
  NEW googlesql.examples.music.Chart { rank: 1 chart_name: '2' }
```

Nested messages and arrays:

```googlesql
SELECT
  NEW googlesql.examples.music.Album {
    album_name: 'New Moon'
    singer {
      nationality: 'Canadian'
      residence: [ { city: 'Victoria' }, { city: 'Toronto' } ]
    }
    song: ['Sandstorm', 'Wait']
  }
```

With an extension field (note a comma is required before the extension field):

```googlesql
SELECT
  NEW googlesql.examples.music.Album {
    album_name: 'New Moon',
    (googlesql.examples.music.downloads): 30
  }
```

Non-literal expressions as values:

```googlesql
SELECT
  NEW googlesql.examples.music.Chart {
    rank: (SELECT COUNT(*) FROM TableName WHERE foo = 'bar')
    chart_name: CONCAT('best', 'hits')
  }
```

The following examples infers the protocol buffer data type from context:

+   From `ARRAY` constructor:

    ```googlesql
    SELECT
      ARRAY<googlesql.examples.music.Chart>[
        { rank: 1 chart_name: '2' },
        { rank: 2 chart_name: '3' }]
    ```
+   From `STRUCT` constructor:

    ```googlesql
    SELECT
      STRUCT<STRING, googlesql.examples.music.Chart, INT64>(
        'foo', { rank: 1 chart_name: '2' }, 7)[1]
    ```
+   From column names through `SET`:

    +   Simple column:

    ```googlesql
    UPDATE TableName SET proto_column = { rank: 1 chart_name: '2' }
    ```

    +   Array column:

    ```googlesql
    UPDATE TableName
    SET proto_array_column = [
      { rank: 1 chart_name: '2' }, { rank: 2 chart_name: '3' }]
    ```

    +   Struct column:

    ```googlesql
    UPDATE TableName
    SET proto_struct_column = ('foo', { rank: 1 chart_name: '2' }, 7)
    ```
+   From generated column names in `CREATE`:

    ```googlesql
    CREATE TABLE TableName (
      proto_column googlesql.examples.music.Chart GENERATED ALWAYS AS (
        { rank: 1 chart_name: '2' }))
    ```
+   From column names in default values in `CREATE`:

    ```googlesql
    CREATE TABLE TableName(
      proto_column googlesql.examples.music.Chart DEFAULT (
        { rank: 1 chart_name: '2' }))
    ```
+   From return types in SQL function body:

    ```googlesql
    CREATE FUNCTION MyFunc()
    RETURNS googlesql.examples.music.Chart
    AS (
      { rank: 1 chart_name: '2' }
    )
    ```

#### `NEW protocol_buffer (...)` 
<a id="using_new"></a>

You can create a protocol buffer using the [`NEW`][new-operator] operator with a
parenthesized list of arguments and aliases to specify field names:

```googlesql
NEW protocol_buffer(field [AS alias], ...)
```

**Example**

```googlesql
SELECT
  key,
  name,
  NEW googlesql.examples.music.Chart(key AS rank, name AS chart_name)
FROM
  (SELECT 1 AS key, "2" AS name);
```

<!-- mdlint off(WHITESPACE_LINE_LENGTH) -->

When using this syntax, the following rules apply:

+   All field expressions must have an [explicit alias][explicit-alias] or end
    with an identifier. For example, the expression `a.b.c` has the [implicit
    alias][implicit-alias] `c`.
+   `NEW` matches fields by alias to the field names of the protocol buffer.
    Aliases must be unique.
+   The expressions must be implicitly coercible or literal-coercible to the
    type of the corresponding protocol buffer field.

To create a protocol buffer with an extension, use this syntax:

```googlesql
NEW protocol_buffer(expression AS (path.to.extension), ...)
```

+   For `path.to.extension`, provide the path to the extension. Place the
    extension path inside parentheses.
+   `expression` provides the value to set for the extension. `expression` must
    be of the same type as the extension or [coercible to that
    type][conversion-rules].

    Example:

    ```googlesql
    SELECT
     NEW googlesql.examples.music.Album (
       album AS album_name,
       count AS (googlesql.examples.music.downloads)
     )
     FROM (SELECT 'New Moon' AS album, 30 AS count);

    /*---------------------------------------------------+
     | $col1                                             |
     +---------------------------------------------------+
     | {album_name: 'New Moon' [...music.downloads]: 30} |
     +---------------------------------------------------*/
    ```
+   If `path.to.extension` points to a nested protocol buffer extension, `expr1`
    provides an instance or a text format string of that protocol buffer.

    Example:

    ```googlesql
    SELECT
     NEW googlesql.examples.music.Album(
       'New Moon' AS album_name,
       NEW googlesql.examples.music.AlbumExtension(
        DATE(1956,1,1) AS release_date
       )
     AS (googlesql.examples.music.AlbumExtension.album_extension));

    /*---------------------------------------------+
     | $col1                                       |
     +---------------------------------------------+
     | album_name: "New Moon"                      |
     | [...music.AlbumExtension.album_extension] { |
     |   release_date: -5114                       |
     | }                                           |
     +---------------------------------------------*/
    ```

#### `SELECT AS typename` 
<a id="select_as_typename_proto"></a>

The [`SELECT AS typename`][select-as-typename] statement can produce a
value table where the row type is a specific named protocol buffer type.

`SELECT AS` doesn't support setting protocol buffer extensions. To do so, use
the [`NEW`][new-keyword] keyword instead. For example,  to create a
protocol buffer with an extension, change a query like this:

```googlesql
SELECT AS typename field1, field2, ...
```

to a query like this:

```googlesql
SELECT AS VALUE NEW ProtoType(field1, field2, field3 AS (path.to.extension), ...)
```

### Limited comparisons for protocol buffer values

Direct comparison of protocol buffers isn't supported. There are a few
alternative solutions:

+ One way to compare protocol buffers is to do a pair-wise
  comparison between the fields of the protocol buffers. This can also be used
  to `GROUP BY` or `ORDER BY` protocol buffer fields.
+ To get a simple approximation comparison, cast protocol buffer to
  string. This applies lexicographical ordering for numeric fields.

[protocol-buffers-dev-guide]: https://developers.google.com/protocol-buffers/docs/overview

[protocol-buffers]: https://github.com/google/googlesql/blob/master/docs/protocol-buffers.md

[new-operator]: https://github.com/google/googlesql/blob/master/docs/operators.md#new_operator

[select-as-typename]: https://github.com/google/googlesql/blob/master/docs/query-syntax.md#select_as_typename

[new-keyword]: #using_new

[explicit-alias]: https://github.com/google/googlesql/blob/master/docs/query-syntax.md#explicit_alias_syntax

[implicit-alias]: https://github.com/google/googlesql/blob/master/docs/query-syntax.md#implicit_aliases

[conversion-rules]: https://github.com/google/googlesql/blob/master/docs/conversion_rules.md

## Range type 
<a id="range_type"></a>

<table>
  <thead>
    <tr>
      <th>Name</th>
      <th>Range</th>
    </tr>
  </thead>
  <tbody>
    <tr>
      <td><code>RANGE</code></td>
      <td>
        Contiguous range between two dates, datetimes, or timestamps.
        The lower and upper bound for the range are optional. The lower bound
        is inclusive and the upper bound is exclusive.
      </td>
    </tr>
  </tbody>
</table>

### Declare a range type

A range type can be declared as follows:

<table>
  <thead>
    <tr>
      <th>Type Declaration</th>
      <th>Meaning</th>
    </tr>
  </thead>
  <tbody>
    <tr>
      <td><code>RANGE&lt;DATE&gt;</code></td>
      <td>Contiguous range between two dates.</td>
    </tr>
    <tr>
      <td><code>RANGE&lt;DATETIME&gt;</code></td>
      <td>Contiguous range between two datetimes.</td>
    </tr>
    <tr>
      <td><code>RANGE&lt;TIMESTAMP&gt;</code></td>
      <td>Contiguous range between two timestamps.</td>
    </tr>
  </tbody>
</table>

### Construct a range 
<a id="constructing_a_range"></a>

You can construct a range with the [`RANGE` constructor][range-with-constructor]
or a [range literal][range-with-literal].

#### Construct a range with a constructor 
<a id="range_with_constructor"></a>

You can construct a range with the `RANGE` constructor. To learn more,
see [`RANGE`][range-constructor].

#### Construct a range with a literal 
<a id="range_with_literal"></a>

You can construct a range with a range literal. The canonical format for a
range literal has the following parts:

```googlesql
RANGE<T> '[lower_bound, upper_bound)'
```

+   `T`: The type of range. This can be `DATE`, `DATETIME`, or `TIMESTAMP`.
+   `lower_bound`: The range starts from this value. This can be a
    [date][date-literals], [datetime][datetime-literals], or
    [timestamp][timestamp-literals] literal. If this value is `UNBOUNDED` or
    `NULL`, the range doesn't include a lower bound.
+   `upper_bound`: The range ends before this value. This can be a
    [date][date-literals], [datetime][datetime-literals], or
    [timestamp][timestamp-literals] literal. If this value is `UNBOUNDED` or
    `NULL`, the range doesn't include an upper bound.

`T`, `lower_bound`, and `upper_bound` must be of the same data type.

To learn more about the literal representation of a range type,
see [Range literals][range-literals].

### Additional details

The range type doesn't support arithmetic operators.

[range-literals]: https://github.com/google/googlesql/blob/master/docs/lexical.md#range_literals

[range-with-constructor]: #range_with_constructor

[range-constructor]: https://github.com/google/googlesql/blob/master/docs/range-functions.md#range

[range-with-literal]: #range_with_literal

[date-literals]: https://github.com/google/googlesql/blob/master/docs/lexical.md#date_literals

[datetime-literals]: https://github.com/google/googlesql/blob/master/docs/lexical.md#datetime_literals

[timestamp-literals]: https://github.com/google/googlesql/blob/master/docs/lexical.md#timestamp_literals

## String type 
<a id="string_type"></a>

<table>
<thead>
<tr>
<th>Name</th>
<th>Description</th>
</tr>
</thead>
<tbody>
<tr>
<td><code>STRING</code></td>
<td>Variable-length character (Unicode) data.</td>
</tr>
</tbody>
</table>

Input string values must be UTF-8 encoded and output string values will be UTF-8
encoded. Alternate encodings like CESU-8 and Modified UTF-8 aren't treated as
valid UTF-8.

All functions and operators that act on string values operate on Unicode
characters rather than bytes. For example, functions like `SUBSTR` and `LENGTH`
applied to string input count the number of characters, not bytes.

Each Unicode character has a numeric value called a code point assigned to it.
Lower code points are assigned to lower characters. When characters are
compared, the code points determine which characters are less than or greater
than other characters.

Most functions on strings are also defined on bytes. The bytes version
operates on raw bytes rather than Unicode characters. Strings and bytes are
separate types that can't be used interchangeably. There is no implicit casting
in either direction. Explicit casting between string and bytes does
UTF-8 encoding and decoding. Casting bytes to string returns an error if the
bytes aren't valid UTF-8.

To learn more about the literal representation of a string type,
see [String literals][string-literals].

[string-literals]: https://github.com/google/googlesql/blob/master/docs/lexical.md#string_and_bytes_literals

## Struct type 
<a id="struct_type"></a>

<table>
<thead>
<tr>
<th>Name</th>
<th>Description</th>
</tr>
</thead>
<tbody>
<tr>
<td><code>STRUCT</code></td>
<td>Container of ordered fields each with a type (required) and field name
(optional).</td>
</tr>
</tbody>
</table>

To learn more about the literal representation of a struct type,
see [Struct literals][struct-literals].

### Declaring a struct type

```
STRUCT<T>
```

Struct types are declared using the angle brackets (`<` and `>`). The type of
the elements of a struct can be arbitrarily complex.

**Examples**

<table>
<thead>
<tr>
<th>Type Declaration</th>
<th>Meaning</th>
</tr>
</thead>
<tbody>
<tr>
<td>
<code>
STRUCT&lt;INT64&gt;
</code>
</td>
<td>Simple struct with a single unnamed 64-bit integer field.</td>
</tr>
<tr>
<td style="white-space:nowrap">
<code>
STRUCT&lt;x STRUCT&lt;y INT64, z INT64&gt;&gt;
</code>
</td>
<td>A struct with a nested struct named <code>x</code> inside it. The struct
<code>x</code> has two fields, <code>y</code> and <code>z</code>, both of which
are 64-bit integers.</td>
</tr>
<tr>
<td style="white-space:nowrap">
<code>
STRUCT&lt;inner_array ARRAY&lt;INT64&gt;&gt;
</code>
</td>
<td>A struct containing an array named <code>inner_array</code> that holds
64-bit integer elements.</td>
</tr>
<tbody>
</table>

### Constructing a struct 
<a id="constructing_a_struct"></a>

#### Tuple syntax

```
(expr1, expr2 [, ... ])
```

The output type is an anonymous struct type with anonymous fields with types
matching the types of the input expressions. There must be at least two
expressions specified. Otherwise this syntax is indistinguishable from an
expression wrapped with parentheses.

**Examples**

<table>
<thead>
<tr>
<th>Syntax</th>
<th>Output Type</th>
<th>Notes</th>
</tr>
</thead>
<tbody>
<tr>
<td style="white-space:nowrap"><code>(x, x+y)</code></td>
<td style="white-space:nowrap"><code>STRUCT&lt;?,?&gt;</code></td>
<td>If column names are used (unquoted strings), the struct field data type is
derived from the column data type. <code>x</code> and <code>y</code> are
columns, so the data types of the struct fields are derived from the column
types and the output type of the addition operator.</td>
</tr>
</tbody>
</table>

This syntax can also be used with struct comparison for comparison expressions
using multi-part keys, e.g., in a `WHERE` clause:

```
WHERE (Key1,Key2) IN ( (12,34), (56,78) )
```

#### Typeless struct syntax

```
STRUCT( expr1 [AS field_name] [, ... ])
```

Duplicate field names are allowed. Fields without names are considered anonymous
fields and can't be referenced by name. struct values can be `NULL`, or can
have `NULL` field values.

**Examples**

<table>
<thead>
<tr>
<th>Syntax</th>
<th>Output Type</th>
</tr>
</thead>
<tbody>
<tr>
<td><code>STRUCT(1,2,3)</code></td>
<td><code>STRUCT&lt;int64,int64,int64&gt;</code></td>
</tr>
<tr>
<td><code>STRUCT()</code></td>
<td><code>STRUCT&lt;&gt;</code></td>
</tr>
<tr>
<td><code>STRUCT('abc')</code></td>
<td><code>STRUCT&lt;string&gt;</code></td>
</tr>
<tr>
<td><code>STRUCT(1, t.str_col)</code></td>
<td><code>STRUCT&lt;int64, str_col string&gt;</code></td>
</tr>
<tr>
<td><code>STRUCT(1 AS a, 'abc' AS b)</code></td>
<td><code>STRUCT&lt;a int64, b string&gt;</code></td>
</tr>
<tr>
<td><code>STRUCT(str_col AS abc)</code></td>
<td><code>STRUCT&lt;abc string&gt;</code></td>
</tr>
</tbody>
</table>

#### Typed struct syntax

```
STRUCT<[field_name] field_type, ...>( expr1 [, ... ])
```

Typed syntax allows constructing structs with an explicit struct data type. The
output type is exactly the `field_type` provided. The input expression is
coerced to `field_type` if the two types aren't the same, and an error is
produced if the types aren't compatible. `AS alias` isn't allowed on the input
expressions. The number of expressions must match the number of fields in the
type, and the expression types must be coercible or literal-coercible to the
field types.

**Examples**

<table>
<thead>
<tr>
<th>Syntax</th>
<th>Output Type</th>
</tr>
</thead>
<tbody>
<tr>
<td><code>STRUCT&lt;int64&gt;(5)</code></td>
<td><code>STRUCT&lt;int64&gt;</code></td>
</tr>

<tr>
<td><code>STRUCT&lt;date&gt;("2011-05-05")</code></td>
<td><code>STRUCT&lt;date&gt;</code></td>
</tr>

<tr>
<td><code>STRUCT&lt;x int64, y string&gt;(1, t.str_col)</code></td>
<td><code>STRUCT&lt;x int64, y string&gt;</code></td>
</tr>
<tr>
<td><code>STRUCT&lt;int64&gt;(int_col)</code></td>
<td><code>STRUCT&lt;int64&gt;</code></td>
</tr>
<tr>
<td><code>STRUCT&lt;x int64&gt;(5 AS x)</code></td>
<td>Error - Typed syntax doesn't allow <code>AS</code></td>
</tr>
</tbody>
</table>

### Limited comparisons for structs

Structs can be directly compared using equality operators:

+ Equal (`=`)
+ Not Equal (`!=` or `<>`)
+ [`NOT`] `IN`

Notice, though, that these direct equality comparisons compare the fields of
the struct pairwise in ordinal order ignoring any field names. If instead you
want to compare identically named fields of a struct, you can compare the
individual fields directly.

[struct-literals]: https://github.com/google/googlesql/blob/master/docs/lexical.md#struct_literals

## Time type 
<a id="time_type"></a>

<table>
<thead>
<tr>
<th>Name</th>
<th>Range</th>
</tr>
</thead>
<tbody>
<tr>
<td><code>TIME</code></td>

    <td>
        00:00:00 to 23:59:59.999999999<br/>
        <hr/>
        00:00:00 to 23:59:59.999999<br/>
    </td>

</tr>
</tbody>
</table>

A time value represents a time of day, as might be displayed on a clock,
independent of a specific date and time zone.
The range of
subsecond precision is determined by the
SQL engine. To represent
an absolute point in time, use a [timestamp][timestamp-type].

##### Canonical format 
<a id="canonical_format_for_time_literals"></a>

```
[H]H:[M]M:[S]S[.F]
```

+ <code>[H]H</code>: One or two digit hour (valid values from 00 to 23).
+ <code>[M]M</code>: One or two digit minutes (valid values from 00 to 59).
+ <code>[S]S</code>: One or two digit seconds (valid values from 00 to 60).
+ <code>[.F]</code>: Up to nine fractional
  digits (nanosecond precision).

To learn more about the literal representation of a time type,
see [Time literals][time-literals].

[timestamp-type]: #timestamp_type

[time-literals]: https://github.com/google/googlesql/blob/master/docs/lexical.md#time_literals

## Timestamp type 
<a id="timestamp_type"></a>

<table>
<thead>
<tr>
<th>Name</th>
<th>Range</th>
</tr>
</thead>
<tbody>
<tr>
<td><code>TIMESTAMP</code></td>

    <td>
      0001-01-01 00:00:00 to 9999-12-31 23:59:59.999999999 UTC<br/>
      <hr/>
      0001-01-01 00:00:00 to 9999-12-31 23:59:59.999999 UTC<br/>
    </td>

</tr>
</tbody>
</table>

A timestamp value represents an absolute point in time,
independent of any time zone or convention such as daylight saving time (DST),
with
microsecond, nanosecond, or picosecond
precision.
The range of subsecond precision is determined by the query engine.

A timestamp is typically represented internally as the number of elapsed
microseconds, nanoseconds, or picoseconds since a fixed initial point in time.

Note that a timestamp itself doesn't have a time zone; it represents the same
instant in time globally. However, the _display_ of a timestamp for human
readability usually includes a Gregorian date, a time, and a time zone, in an
implementation-dependent format. For example, the displayed values "2020-01-01
00:00:00 UTC", "2019-12-31 19:00:00 America/New_York", and "2020-01-01 05:30:00
Asia/Kolkata" all represent the same instant in time and therefore represent the
same timestamp value.

+  To represent a Gregorian date as it might appear on a calendar
   (a civil date), use a [date][date-type] value.
+  To represent a time as it might appear on a clock (a civil time),
   use a [time][time-type] value.
+  To represent a Gregorian date and time as they might appear on a watch,
   use a [datetime][datetime-type] value.

##### Canonical format 
<a id="canonical_format_for_timestamp_literals"></a>

The canonical format for a timestamp literal has the following parts:

```
{
  civil_date_part[time_part [time_zone]] |
  civil_date_part[time_part[time_zone_offset]] |
  civil_date_part[time_part[utc_time_zone]]
}

civil_date_part:
    YYYY-[M]M-[D]D

time_part:
    { |T|t}[H]H:[M]M:[S]S[.F]
```

+   <code>YYYY</code>: Four-digit year.
+   <code>[M]M</code>: One or two digit month.
+   <code>[D]D</code>: One or two digit day.
+   <code>{ |T|t}</code>: A space or a `T` or `t` separator. The `T` and `t`
    separators are flags for time.
+   <code>[H]H</code>: One or two digit hour (valid values from 00 to 23).
+   <code>[M]M</code>: One or two digit minutes (valid values from 00 to 59).
+   <code>[S]S</code>: One or two digit seconds (valid values from 00 to 60).
+   <code>[.F]</code>: Up to 12 fractional
    digits (picosecond precision).
+   <code>[time_zone]</code>: String representing the time zone. When a time
    zone isn't explicitly specified, the default time zone,
    which is implementation defined, is used. For details, see <a href="#time_zones">time
    zones</a>.
+   <code>[time_zone_offset]</code>: String representing the offset from the
    Coordinated Universal Time (UTC) time zone. For details, see
    <a href="#time_zones">time zones</a>.
+   <code>[utc_time_zone]</code>: String representing the Coordinated Universal
    Time (UTC), usually the letter `Z` or `z`. For details, see
    <a href="#time_zones">time zones</a>.

To learn more about the literal representation of a timestamp type,
see [Timestamp literals][timestamp-literals].

### Time zones 
<a id="time_zones"></a>

A time zone is used when converting from a civil date or time (as might appear
on a calendar or clock) to a timestamp (an absolute time), or vice versa. This
includes the operation of parsing a string containing a civil date and time like
"2020-01-01 00:00:00" and converting it to a timestamp. The resulting timestamp
value itself doesn't store a specific time zone, because it represents one
instant in time globally.

Time zones are represented by strings in one of these canonical formats:

+ Offset from Coordinated Universal Time (UTC), or the letter `Z` or `z` for
  UTC.
+ Time zone name from the
  [tz database][tz-database]{: class=external target=_blank }.

The following timestamps are identical because the time zone offset
for `America/Los_Angeles` is `-08` for the specified date and time.

```googlesql
SELECT UNIX_MILLIS(TIMESTAMP '2008-12-25 15:30:00 America/Los_Angeles') AS millis;
```

```googlesql
SELECT UNIX_MILLIS(TIMESTAMP '2008-12-25 15:30:00-08:00') AS millis;
```

#### Specify Coordinated Universal Time (UTC) 
<a id="utc"></a>

You can specify UTC using the following suffix:

```
{Z|z}
```

You can also specify UTC using the following time zone name:

```
{Etc/UTC}
```

The `Z` suffix is a placeholder that implies UTC when converting an [RFC
3339-format][rfc-3339-format] value to a `TIMESTAMP` value. The value `Z` isn't
a valid time zone for functions that accept a time zone. If you're specifying a
time zone, or you're unsure of the format to use to specify UTC, we recommend
using the `Etc/UTC` time zone name.

The `Z` suffix isn't case sensitive. When using the `Z` suffix, no space is
allowed between the `Z` and the rest of the timestamp. The following are
examples of using the `Z` suffix and the `Etc/UTC` time zone name:

```
SELECT TIMESTAMP '2014-09-27T12:30:00.45Z'
SELECT TIMESTAMP '2014-09-27 12:30:00.45z'
SELECT TIMESTAMP '2014-09-27T12:30:00.45 Etc/UTC'
```

#### Specify an offset from Coordinated Universal Time (UTC) 
<a id="utc_offset"></a>

You can specify the offset from UTC using the following format:

```
{+|-}H[H][:M[M]]
```

Examples:

```
-08:00
-8:15
+3:00
+07:30
-7
```

When using this format, no space is allowed between the time zone and the rest
of the timestamp.

```
2014-09-27 12:30:00.45-8:00
```

#### Time zone name {: #time_zone_name}

Format:

```
tz_identifier
```

A time zone name is a tz identifier from the
[tz database][tz-database]{: class=external target=_blank }.
For a less comprehensive but simpler reference, see the
[List of tz database time zones][tz-database-list]{: class=external target=_blank }
on Wikipedia.

Examples:

```
America/Los_Angeles
America/Argentina/Buenos_Aires
Etc/UTC
Pacific/Auckland
```

When using a time zone name, a space is required between the name and the rest
of the timestamp:

```
2014-09-27 12:30:00.45 America/Los_Angeles
```

Note that not all time zone names are interchangeable even if they do happen to
report the same time during a given part of the year. For example,
`America/Los_Angeles` reports the same time as `UTC-7:00` during daylight
saving time (DST), but reports the same time as `UTC-8:00` outside of DST.

If a time zone isn't specified, the default time zone value is used.

#### Leap seconds

A timestamp is simply an offset from 1970-01-01 00:00:00 UTC, assuming there are
exactly 60 seconds per minute. Leap seconds aren't represented as part of a
stored timestamp.

If the input contains values that use ":60" in the seconds field to represent a
leap second, that leap second isn't preserved when converting to a timestamp
value. Instead that value is interpreted as a timestamp with ":00" in the
seconds field of the following minute.

Leap seconds don't affect timestamp computations. All timestamp computations
are done using Unix-style timestamps, which don't reflect leap seconds. Leap
seconds are only observable through functions that measure real-world time. In
these functions, it's possible for a timestamp second to be skipped or repeated
when there is a leap second.

#### Daylight saving time

A timestamp is unaffected by daylight saving time (DST) because it represents a
point in time. When you display a timestamp as a civil time,
with a timezone that observes DST, the following rules apply:

+ During the transition from standard time to DST, one hour is skipped. A
  civil time from the skipped hour is treated the same as if it were written
  an hour later. For example, in the `America/Los_Angeles` time zone, the hour
  between 2 AM and 3 AM on March 10, 2024 is skipped on a clock. The times
  2:30 AM and 3:30 AM on that date are treated as the same point in time:

  ```googlesql
  SELECT
  FORMAT_TIMESTAMP("%c %Z", "2024-03-10 02:30:00 America/Los_Angeles", "UTC") AS two_thirty,
  FORMAT_TIMESTAMP("%c %Z", "2024-03-10 03:30:00 America/Los_Angeles", "UTC") AS three_thirty;

  /*------------------------------+------------------------------+
   | two_thirty                   | three_thirty                 |
   +------------------------------+------------------------------+
   | Sun Mar 10 10:30:00 2024 UTC | Sun Mar 10 10:30:00 2024 UTC |
   +------------------------------+------------------------------*/
  ```
+ When there's ambiguity in how to represent a civil time in a particular
  timezone because of DST, the later time is chosen:

  ```googlesql
  SELECT
  FORMAT_TIMESTAMP("%c %Z", "2024-03-10 10:30:00 UTC", "America/Los_Angeles") as ten_thirty;

  /*--------------------------------+
   | ten_thirty                     |
   +--------------------------------+
   | Sun Mar 10 03:30:00 2024 UTC-7 |
   +--------------------------------*/
  ```
+ During the transition from DST to standard time, one hour is repeated. A
  civil time that shows a time during that hour is treated as if it's the
  earlier instance of that time. For example, in the `America/Los_Angeles` time
  zone, the hour between 1 AM and 2 AM on November 3, 2024, is repeated on a
  clock. The time 1:30 AM on that date is treated as the earlier (DST) instance
  of that time.

  ```googlesql
  SELECT
  FORMAT_TIMESTAMP("%c %Z", "2024-11-03 01:30:00 America/Los_Angeles", "UTC") as one_thirty,
  FORMAT_TIMESTAMP("%c %Z", "2024-11-03 02:30:00 America/Los_Angeles", "UTC") as two_thirty;

  /*------------------------------+------------------------------+
   | one_thirty                   | two_thirty                   |
   +------------------------------+------------------------------+
   | Sun Nov 3 08:30:00 2024 UTC  | Sun Nov 3 10:30:00 2024 UTC  |
   +------------------------------+------------------------------*/
  ```

[rfc-3339-format]: https://datatracker.ietf.org/doc/html/rfc3339#page-10

[tz-database]: http://www.iana.org/time-zones

[tz-database-list]: http://en.wikipedia.org/wiki/List_of_tz_database_time_zones

[time-type]: #time_type

[date-type]: #date_type

[datetime-type]: #datetime_type

[timestamp-literals]: https://github.com/google/googlesql/blob/master/docs/lexical.md#timestamp_literals

## UUID type 
<a id="uuid_type"></a>

<table>
<thead>
<tr>
<th>Name</th>
<th>Description</th>
</tr>
</thead>
<tbody>
<tr>
<td><code>UUID</code></td>
<td>A universally unique identifier (UUID) represented as a 128-bit number.</td>
</tr>
</tbody>
</table>

The following ASCII string format of lowercase hexadecimal digits is used to
represent a UUID:

`[8 digits]-[4 digits]-[4 digits]-[4 digits]-[12 digits]`

**Example**

`f81d4fae-7dec-11d0-a765-00a0c91e6bf6`

### Cast a UUID to a string

You can cast a UUID to a string by using the following syntax:

```googlesql
  SELECT CAST(NEW_UUID() AS STRING) AS UUID_STR;
```

You can also cast a string to a UUID, either explicitly or by using an
implicit coercion of a literal or parameter.

**Examples**

```googlesql
  SELECT UUID_id >= CAST("00000000-0000-0000-0000-000000000000" AS UUID) FROM T1;
```

```googlesql
  SELECT UUID_id >= "00000000-0000-0000-0000-000000000000" FROM T1;
```

### Cast a UUID to bytes

You can cast a UUID to bytes by using the following syntax:

```googlesql
  SELECT CAST(NEW_UUID() AS BYTES) AS UUID_BYTES;
```

You can also explicitly cast bytes to a UUID. Unlike strings, bytes can't be
implicitly coerced to a UUID.

##### Comparison operator examples

The comparison operator compares UUIDs using their internal representation.
However, the result is presented as if the comparison were performed on the
36-character lowercase ASCII string representation of the UUIDs,
using lexicographical order.

<table>
<thead>
<tr>
<th>Left term</th>
<th>Operator</th>
<th>Right term</th>
<th>Returns</th>
</tr>
</thead>
<tbody>
<tr>
<td>Any value</td>
<td><code>=</code></td>
<td><code>NULL</code></td>
<td><code>NULL</code></td>
</tr>
<tr>
<td><code>NULL</code></td>
<td><code>&lt;</code></td>
<td>Any value</td>
<td><code>NULL</code></td>
</tr>
<tr>
<td>00000000-0000-0000-0000-000000000000</td>
<td><code>&lt;</code></td>
<td>ffffffff-ffff-ffff-ffff-ffffffffffff</td>
<td><code>TRUE</code></td>
</tr>
<tr>
<td>00000000-0000-0000-0000-000000000000</td>
<td><code>=</code></td>
<td>00000000-0000-0000-0000-000000000000</td>
<td><code>TRUE</code></td>
</tr>
<tr>
<td>00000000-0000-0000-0000-000000000000</td>
<td><code>&gt;</code></td>
<td>ffffffff-ffff-ffff-ffff-ffffffffffff</td>
<td><code>FALSE</code></td>
</tr>
</tbody>
</table>

**Example**

```googlesql
  SELECT NEW_UUID() >= "00000000-0000-0000-0000-000000000000" AS Is_GE;

/*-------+
 | Is_GE |
 +-------+
 | true  |
 +-------*/
```

[array-nulls]: #array_nulls

[floating-point-types]: #floating_point_types

[lexical-literals]: https://github.com/google/googlesql/blob/master/docs/lexical.md#literals

[join-types]: https://github.com/google/googlesql/blob/master/docs/query-syntax.md#join_types

[order-by-clause]: https://github.com/google/googlesql/blob/master/docs/query-syntax.md#order_by_clause

[st-equals]: https://github.com/google/googlesql/blob/master/docs/geography_functions.md#st_equals

