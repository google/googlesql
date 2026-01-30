

<!-- mdlint off(WHITESPACE_LINE_LENGTH) -->

# Array functions

GoogleSQL supports the following array functions.

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
  <td><a href="https://github.com/google/googlesql/blob/master/docs/array_functions.md#array"><code>ARRAY</code></a>
</td>
  <td>
    Produces an array with one element for each row in a subquery.
  </td>
</tr>

<tr>
  <td><a href="https://github.com/google/googlesql/blob/master/docs/aggregate_functions.md#array_agg"><code>ARRAY_AGG</code></a>
</td>
  <td>
    Gets an array of values.
    <br>For more information, see <a href="https://github.com/google/googlesql/blob/master/docs/aggregate_functions.md">Aggregate functions</a>.

  </td>
</tr>

<tr>
  <td><a href="https://github.com/google/googlesql/blob/master/docs/array_functions.md#array_avg"><code>ARRAY_AVG</code></a>
</td>
  <td>
    Gets the average of non-<code>NULL</code> values in an array.
  </td>
</tr>

<tr>
  <td><a href="https://github.com/google/googlesql/blob/master/docs/array_functions.md#array_concat"><code>ARRAY_CONCAT</code></a>
</td>
  <td>
    Concatenates one or more arrays with the same element type into a
    single array.
  </td>
</tr>

<tr>
  <td><a href="https://github.com/google/googlesql/blob/master/docs/aggregate_functions.md#array_concat_agg"><code>ARRAY_CONCAT_AGG</code></a>
</td>
  <td>
    Concatenates arrays and returns a single array as a result.
    <br>For more information, see <a href="https://github.com/google/googlesql/blob/master/docs/aggregate_functions.md">Aggregate functions</a>.

  </td>
</tr>

<tr>
  <td><a href="https://github.com/google/googlesql/blob/master/docs/array_functions.md#array_filter"><code>ARRAY_FILTER</code></a>
</td>
  <td>
    Takes an array, filters out unwanted elements, and returns the results
    in a new array.
  </td>
</tr>

<tr>
  <td><a href="https://github.com/google/googlesql/blob/master/docs/array_functions.md#array_first"><code>ARRAY_FIRST</code></a>
</td>
  <td>
    Gets the first element in an array.
  </td>
</tr>

<tr>
  <td><a href="https://github.com/google/googlesql/blob/master/docs/array_functions.md#array_includes"><code>ARRAY_INCLUDES</code></a>
</td>
  <td>
    Checks if there is an element in the array that is
    equal to a search value.
  </td>
</tr>

<tr>
  <td><a href="https://github.com/google/googlesql/blob/master/docs/array_functions.md#array_includes_all"><code>ARRAY_INCLUDES_ALL</code></a>
</td>
  <td>
    Checks if all search values are in an array.
  </td>
</tr>

<tr>
  <td><a href="https://github.com/google/googlesql/blob/master/docs/array_functions.md#array_includes_any"><code>ARRAY_INCLUDES_ANY</code></a>
</td>
  <td>
    Checks if any search values are in an array.
  </td>
</tr>

<tr>
  <td><a href="https://github.com/google/googlesql/blob/master/docs/array_functions.md#array_is_distinct"><code>ARRAY_IS_DISTINCT</code></a>
</td>
  <td>
    Checks if an array contains no repeated elements.
  </td>
</tr>

<tr>
  <td><a href="https://github.com/google/googlesql/blob/master/docs/array_functions.md#array_last"><code>ARRAY_LAST</code></a>
</td>
  <td>
    Gets the last element in an array.
  </td>
</tr>

<tr>
  <td><a href="https://github.com/google/googlesql/blob/master/docs/array_functions.md#array_length"><code>ARRAY_LENGTH</code></a>
</td>
  <td>
    Gets the number of elements in an array.
  </td>
</tr>

<tr>
  <td><a href="https://github.com/google/googlesql/blob/master/docs/array_functions.md#array_max"><code>ARRAY_MAX</code></a>
</td>
  <td>
    Gets the maximum non-<code>NULL</code> value in an array.
  </td>
</tr>

<tr>
  <td><a href="https://github.com/google/googlesql/blob/master/docs/array_functions.md#array_min"><code>ARRAY_MIN</code></a>
</td>
  <td>
    Gets the minimum non-<code>NULL</code> value in an array.
  </td>
</tr>

<tr>
  <td><a href="https://github.com/google/googlesql/blob/master/docs/array_functions.md#array_reverse"><code>ARRAY_REVERSE</code></a>
</td>
  <td>
    Reverses the order of elements in an array.
  </td>
</tr>

<tr>
  <td><a href="https://github.com/google/googlesql/blob/master/docs/array_functions.md#array_slice"><code>ARRAY_SLICE</code></a>
</td>
  <td>
    Produces an array containing zero or more consecutive elements from an
    input array.
  </td>
</tr>

<tr>
  <td><a href="https://github.com/google/googlesql/blob/master/docs/array_functions.md#array_sum"><code>ARRAY_SUM</code></a>
</td>
  <td>
    Gets the sum of non-<code>NULL</code> values in an array.
  </td>
</tr>

<tr>
  <td><a href="https://github.com/google/googlesql/blob/master/docs/array_functions.md#array_to_string"><code>ARRAY_TO_STRING</code></a>
</td>
  <td>
    Produces a concatenation of the elements in an array as a
    <code>STRING</code> value.
    
  </td>
</tr>

<tr>
  <td><a href="https://github.com/google/googlesql/blob/master/docs/array_functions.md#array_transform"><code>ARRAY_TRANSFORM</code></a>
</td>
  <td>
    Transforms the elements of an array, and returns the results in a new
    array.
  </td>
</tr>

<tr>
  <td><a href="https://github.com/google/googlesql/blob/master/docs/array_functions.md#array_zip"><code>ARRAY_ZIP</code></a>
</td>
  <td>
    Combines elements from two to four arrays into one array.
  </td>
</tr>

<tr>
  <td><a href="https://github.com/google/googlesql/blob/master/docs/array_functions.md#flatten"><code>FLATTEN</code></a>
</td>
  <td>
    Flattens arrays of nested data to create a single flat array.
  </td>
</tr>

<tr>
  <td><a href="https://github.com/google/googlesql/blob/master/docs/array_functions.md#generate_array"><code>GENERATE_ARRAY</code></a>
</td>
  <td>
    Generates an array of values in a range.
  </td>
</tr>

<tr>
  <td><a href="https://github.com/google/googlesql/blob/master/docs/array_functions.md#generate_date_array"><code>GENERATE_DATE_ARRAY</code></a>
</td>
  <td>
    Generates an array of dates in a range.
    
  </td>
</tr>

<tr>
  <td><a href="https://github.com/google/googlesql/blob/master/docs/range-functions.md#generate_range_array"><code>GENERATE_RANGE_ARRAY</code></a>
</td>
  <td>
    Splits a range into an array of subranges.
    <br>For more information, see <a href="https://github.com/google/googlesql/blob/master/docs/range-functions.md">Range functions</a>.

  </td>
</tr>

<tr>
  <td><a href="https://github.com/google/googlesql/blob/master/docs/array_functions.md#generate_timestamp_array"><code>GENERATE_TIMESTAMP_ARRAY</code></a>
</td>
  <td>
    Generates an array of timestamps in a range.
    
  </td>
</tr>

<tr>
  <td><a href="https://github.com/google/googlesql/blob/master/docs/json_functions.md#json_array"><code>JSON_ARRAY</code></a>
</td>
  <td>
    Creates a JSON array.
    <br>For more information, see <a href="https://github.com/google/googlesql/blob/master/docs/json_functions.md">JSON functions</a>.

  </td>
</tr>

<tr>
  <td><a href="https://github.com/google/googlesql/blob/master/docs/json_functions.md#json_array_append"><code>JSON_ARRAY_APPEND</code></a>
</td>
  <td>
    Appends JSON data to the end of a JSON array.
    <br>For more information, see <a href="https://github.com/google/googlesql/blob/master/docs/json_functions.md">JSON functions</a>.

  </td>
</tr>

<tr>
  <td><a href="https://github.com/google/googlesql/blob/master/docs/json_functions.md#json_array_insert"><code>JSON_ARRAY_INSERT</code></a>
</td>
  <td>
    Inserts JSON data into a JSON array.
    <br>For more information, see <a href="https://github.com/google/googlesql/blob/master/docs/json_functions.md">JSON functions</a>.

  </td>
</tr>

<tr>
  <td><a href="https://github.com/google/googlesql/blob/master/docs/json_functions.md#json_extract_array"><code>JSON_EXTRACT_ARRAY</code></a>
</td>
  <td>
    (Deprecated)
    Extracts a JSON array and converts it to
    a SQL <code>ARRAY&lt;JSON-formatted STRING&gt;</code>
     or
    <code>ARRAY&lt;JSON&gt;</code>
    
    value.
    <br>For more information, see <a href="https://github.com/google/googlesql/blob/master/docs/json_functions.md">JSON functions</a>.

  </td>
</tr>

<tr>
  <td><a href="https://github.com/google/googlesql/blob/master/docs/json_functions.md#json_extract_string_array"><code>JSON_EXTRACT_STRING_ARRAY</code></a>
</td>
  <td>
    (Deprecated)
    Extracts a JSON array of scalar values and converts it to a SQL
    <code>ARRAY&lt;STRING&gt;</code> value.
    <br>For more information, see <a href="https://github.com/google/googlesql/blob/master/docs/json_functions.md">JSON functions</a>.

  </td>
</tr>

<tr>
  <td><a href="https://github.com/google/googlesql/blob/master/docs/json_functions.md#json_query_array"><code>JSON_QUERY_ARRAY</code></a>
</td>
  <td>
    Extracts a JSON array and converts it to
    a SQL <code>ARRAY&lt;JSON-formatted STRING&gt;</code>
     or
    <code>ARRAY&lt;JSON&gt;</code>
    
    value.
    <br>For more information, see <a href="https://github.com/google/googlesql/blob/master/docs/json_functions.md">JSON functions</a>.

  </td>
</tr>

<tr>
  <td><a href="https://github.com/google/googlesql/blob/master/docs/json_functions.md#json_value_array"><code>JSON_VALUE_ARRAY</code></a>
</td>
  <td>
    Extracts a JSON array of scalar values and converts it to a SQL
    <code>ARRAY&lt;STRING&gt;</code> value.
    <br>For more information, see <a href="https://github.com/google/googlesql/blob/master/docs/json_functions.md">JSON functions</a>.

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

  </tbody>
</table>

## `ARRAY`

```googlesql
ARRAY(subquery)
```

**Description**

The `ARRAY` function returns an `ARRAY` with one element for each row in a
[subquery][subqueries].

If `subquery` produces a
[SQL table][datamodel-sql-tables],
the table must have exactly one column. Each element in the output `ARRAY` is
the value of the single column of a row in the table.

If `subquery` produces a
[value table][datamodel-value-tables],
then each element in the output `ARRAY` is the entire corresponding row of the
value table.

**Constraints**

+ Subqueries are unordered, so the elements of the output `ARRAY` aren't
guaranteed to preserve any order in the source table for the subquery. However,
if the subquery includes an `ORDER BY` clause, the `ARRAY` function will return
an `ARRAY` that honors that clause.
+ If the subquery returns more than one column, the `ARRAY` function returns an
error.
+ If the subquery returns an `ARRAY` typed column or `ARRAY` typed rows, the
  `ARRAY` function returns an error that GoogleSQL doesn't support
  `ARRAY`s with elements of type
  [`ARRAY`][array-data-type].
+ If the subquery returns zero rows, the `ARRAY` function returns an empty
`ARRAY`. It never returns a `NULL` `ARRAY`.

**Return type**

`ARRAY`

**Examples**

```googlesql
SELECT ARRAY
  (SELECT 1 UNION ALL
   SELECT 2 UNION ALL
   SELECT 3) AS new_array;

/*-----------+
 | new_array |
 +-----------+
 | [1, 2, 3] |
 +-----------*/
```

To construct an `ARRAY` from a subquery that contains multiple
columns, change the subquery to use `SELECT AS STRUCT`. Now
the `ARRAY` function will return an `ARRAY` of `STRUCT`s. The `ARRAY` will
contain one `STRUCT` for each row in the subquery, and each of these `STRUCT`s
will contain a field for each column in that row.

```googlesql
SELECT
  ARRAY
    (SELECT AS STRUCT 1, 2, 3
     UNION ALL SELECT AS STRUCT 4, 5, 6) AS new_array;

/*------------------------+
 | new_array              |
 +------------------------+
 | [{1, 2, 3}, {4, 5, 6}] |
 +------------------------*/
```

Similarly, to construct an `ARRAY` from a subquery that contains
one or more `ARRAY`s, change the subquery to use `SELECT AS STRUCT`.

```googlesql
SELECT ARRAY
  (SELECT AS STRUCT [1, 2, 3] UNION ALL
   SELECT AS STRUCT [4, 5, 6]) AS new_array;

/*----------------------------+
 | new_array                  |
 +----------------------------+
 | [{[1, 2, 3]}, {[4, 5, 6]}] |
 +----------------------------*/
```

[subqueries]: https://github.com/google/googlesql/blob/master/docs/subqueries.md

[datamodel-sql-tables]: https://github.com/google/googlesql/blob/master/docs/data-model.md#standard_sql_tables

[datamodel-value-tables]: https://github.com/google/googlesql/blob/master/docs/data-model.md#value_tables

[array-data-type]: https://github.com/google/googlesql/blob/master/docs/data-types.md#array_type

## `ARRAY_AVG`

```googlesql
ARRAY_AVG(input_array)
```

**Description**

Returns the average of non-`NULL` values in an array.

Caveats:

+ If the array is `NULL`, empty, or contains only `NULL`s, returns
  `NULL`.
+ If the array contains `NaN`, returns `NaN`.
+ If the array contains `[+|-]Infinity`, returns either `[+|-]Infinity`
  or `NaN`.
+ If there is numeric overflow, produces an error.
+ If a [floating-point type][floating-point-types] is returned, the result is
  [non-deterministic][non-deterministic], which means you might receive a
  different result each time you use this function.

[floating-point-types]: https://github.com/google/googlesql/blob/master/docs/data-types.md#floating_point_types

[non-deterministic]: https://github.com/google/googlesql/blob/master/docs/data-types.md#floating_point_semantics

**Supported Argument Types**

In the input array, `ARRAY<T>`, `T` can represent one of the following
data types:

+ Any numeric input type
+ `INTERVAL`

**Return type**

The return type depends upon `T` in the input array:

<table>

<thead>
<tr>
<th>INPUT</th><th><code>INT32</code></th><th><code>INT64</code></th><th><code>UINT32</code></th><th><code>UINT64</code></th><th><code>NUMERIC</code></th><th><code>BIGNUMERIC</code></th><th><code>FLOAT</code></th><th><code>DOUBLE</code></th><th><code>INTERVAL</code></th>
</tr>
</thead>
<tbody>
<tr><th>OUTPUT</th><td style="vertical-align:middle"><code>DOUBLE</code></td><td style="vertical-align:middle"><code>DOUBLE</code></td><td style="vertical-align:middle"><code>DOUBLE</code></td><td style="vertical-align:middle"><code>DOUBLE</code></td><td style="vertical-align:middle"><code>NUMERIC</code></td><td style="vertical-align:middle"><code>BIGNUMERIC</code></td><td style="vertical-align:middle"><code>DOUBLE</code></td><td style="vertical-align:middle"><code>DOUBLE</code></td><td style="vertical-align:middle"><code>INTERVAL</code></td></tr>
</tbody>

</table>

**Examples**

```googlesql
SELECT ARRAY_AVG([0, 2, NULL, 4, 4, 5]) as avg

/*-----+
 | avg |
 +-----+
 | 3   |
 +-----*/
```

## `ARRAY_CONCAT`

```googlesql
ARRAY_CONCAT(array_expression[, ...])
```

**Description**

Concatenates one or more arrays with the same element type into a single array.

The function returns `NULL` if any input argument is `NULL`.

Note: You can also use the [|| concatenation operator][array-link-to-operators]
to concatenate arrays.

**Return type**

`ARRAY`

**Examples**

```googlesql
SELECT ARRAY_CONCAT([1, 2], [3, 4], [5, 6]) as count_to_six;

/*--------------------------------------------------+
 | count_to_six                                     |
 +--------------------------------------------------+
 | [1, 2, 3, 4, 5, 6]                               |
 +--------------------------------------------------*/
```

[array-link-to-operators]: https://github.com/google/googlesql/blob/master/docs/operators.md

## `ARRAY_FILTER`

```googlesql
ARRAY_FILTER(array_expression, lambda_expression)

lambda_expression:
  {
    element_alias -> boolean_expression
    | (element_alias, index_alias) -> boolean_expression
  }
```

**Description**

Takes an array, filters out unwanted elements, and returns the results in a new
array.

+   `array_expression`: The array to filter.
+   `lambda_expression`: Each element in `array_expression` is evaluated against
    the [lambda expression][lambda-definition]. If the expression evaluates to
    `FALSE` or `NULL`, the element is removed from the resulting array.
+   `element_alias`: An alias that represents an array element.
+   `index_alias`: An alias that represents the zero-based offset of the array
    element.
+   `boolean_expression`: The predicate used to filter the array elements.

Returns `NULL` if the `array_expression` is `NULL`.

**Return type**

ARRAY

**Example**

```googlesql
SELECT
  ARRAY_FILTER([1 ,2, 3], e -> e > 1) AS a1,
  ARRAY_FILTER([0, 2, 3], (e, i) -> e > i) AS a2;

/*-------+-------+
 | a1    | a2    |
 +-------+-------+
 | [2,3] | [2,3] |
 +-------+-------*/
```

[lambda-definition]: https://github.com/google/googlesql/blob/master/docs/functions-reference.md#lambdas

## `ARRAY_FIRST`

```googlesql
ARRAY_FIRST(array_expression)
```

**Description**

Takes an array and returns the first element in the array.

Produces an error if the array is empty.

Returns `NULL` if `array_expression` is `NULL`.

Note: To get the last element in an array, see [`ARRAY_LAST`][array-last].

**Return type**

Matches the data type of elements in `array_expression`.

**Example**

```googlesql
SELECT ARRAY_FIRST(['a','b','c','d']) as first_element

/*---------------+
 | first_element |
 +---------------+
 | a             |
 +---------------*/
```

[array-last]: #array_last

## `ARRAY_INCLUDES`

+   [Signature 1](#array_includes_signature1):
    `ARRAY_INCLUDES(array_to_search, search_value)`
+   [Signature 2](#array_includes_signature2):
    `ARRAY_INCLUDES(array_to_search, lambda_expression)`

#### Signature 1 
<a id="array_includes_signature1"></a>

```googlesql
ARRAY_INCLUDES(array_to_search, search_value)
```

**Description**

Takes an array and returns `TRUE` if there is an element in the array that is
equal to the search_value.

+   `array_to_search`: The array to search.
+   `search_value`: The element to search for in the array.

Returns `NULL` if `array_to_search` or `search_value` is `NULL`.

**Return type**

`BOOL`

**Example**

In the following example, the query first checks to see if `0` exists in an
array. Then the query checks to see if `1` exists in an array.

```googlesql
SELECT
  ARRAY_INCLUDES([1, 2, 3], 0) AS a1,
  ARRAY_INCLUDES([1, 2, 3], 1) AS a2;

/*-------+------+
 | a1    | a2   |
 +-------+------+
 | false | true |
 +-------+------*/
```

#### Signature 2 
<a id="array_includes_signature2"></a>

```googlesql
ARRAY_INCLUDES(array_to_search, lambda_expression)

lambda_expression: element_alias -> boolean_expression
```

**Description**

Takes an array and returns `TRUE` if the lambda expression evaluates to `TRUE`
for any element in the array.

+   `array_to_search`: The array to search.
+   `lambda_expression`: Each element in `array_to_search` is evaluated against
    the [lambda expression][lambda-definition].
+   `element_alias`: An alias that represents an array element.
+   `boolean_expression`: The predicate used to evaluate the array elements.

Returns `NULL` if `array_to_search` is `NULL`.

**Return type**

`BOOL`

**Example**

In the following example, the query first checks to see if any elements that are
greater than 3 exist in an array (`e > 3`). Then the query checks to see if any
elements that are greater than 0 exist in an array (`e > 0`).

```googlesql
SELECT
  ARRAY_INCLUDES([1, 2, 3], e -> e > 3) AS a1,
  ARRAY_INCLUDES([1, 2, 3], e -> e > 0) AS a2;

/*-------+------+
 | a1    | a2   |
 +-------+------+
 | false | true |
 +-------+------*/
```

[lambda-definition]: https://github.com/google/googlesql/blob/master/docs/functions-reference.md#lambdas

## `ARRAY_INCLUDES_ALL`

```googlesql
ARRAY_INCLUDES_ALL(array_to_search, search_values)
```

**Description**

Takes an array to search and an array of search values. Returns `TRUE` if all
search values are in the array to search, otherwise returns `FALSE`.

+   `array_to_search`: The array to search.
+   `search_values`: The array that contains the elements to search for.

Returns `NULL` if `array_to_search` or `search_values` is
`NULL`.

**Return type**

`BOOL`

**Example**

In the following example, the query first checks to see if `3`, `4`, and `5`
exists in an array. Then the query checks to see if `4`, `5`, and `6` exists in
an array.

```googlesql
SELECT
  ARRAY_INCLUDES_ALL([1,2,3,4,5], [3,4,5]) AS a1,
  ARRAY_INCLUDES_ALL([1,2,3,4,5], [4,5,6]) AS a2;

/*------+-------+
 | a1   | a2    |
 +------+-------+
 | true | false |
 +------+-------*/
```

## `ARRAY_INCLUDES_ANY`

```googlesql
ARRAY_INCLUDES_ANY(array_to_search, search_values)
```

**Description**

Takes an array to search and an array of search values. Returns `TRUE` if any
search values are in the array to search, otherwise returns `FALSE`.

+   `array_to_search`: The array to search.
+   `search_values`: The array that contains the elements to search for.

Returns `NULL` if `array_to_search` or `search_values` is
`NULL`.

**Return type**

`BOOL`

**Example**

In the following example, the query first checks to see if `3`, `4`, or `5`
exists in an array. Then the query checks to see if `4`, `5`, or `6` exists in
an array.

```googlesql
SELECT
  ARRAY_INCLUDES_ANY([1,2,3], [3,4,5]) AS a1,
  ARRAY_INCLUDES_ANY([1,2,3], [4,5,6]) AS a2;

/*------+-------+
 | a1   | a2    |
 +------+-------+
 | true | false |
 +------+-------*/
```

## `ARRAY_IS_DISTINCT`

```googlesql
ARRAY_IS_DISTINCT(value)
```

**Description**

Returns `TRUE` if the array contains no repeated elements, using the same
equality comparison logic as `SELECT DISTINCT`.

**Return type**

`BOOL`

**Examples**

```googlesql
SELECT ARRAY_IS_DISTINCT([1, 2, 3]) AS is_distinct

/*-------------+
 | is_distinct |
 +-------------+
 | true        |
 +-------------*/
```

```googlesql
SELECT ARRAY_IS_DISTINCT([1, 1, 1]) AS is_distinct

/*-------------+
 | is_distinct |
 +-------------+
 | false       |
 +-------------*/
```

```googlesql
SELECT ARRAY_IS_DISTINCT([1, 2, NULL]) AS is_distinct

/*-------------+
 | is_distinct |
 +-------------+
 | true        |
 +-------------*/
```

```googlesql
SELECT ARRAY_IS_DISTINCT([1, 1, NULL]) AS is_distinct

/*-------------+
 | is_distinct |
 +-------------+
 | false       |
 +-------------*/
```

```googlesql
SELECT ARRAY_IS_DISTINCT([1, NULL, NULL]) AS is_distinct

/*-------------+
 | is_distinct |
 +-------------+
 | false       |
 +-------------*/
```
```googlesql
SELECT ARRAY_IS_DISTINCT([]) AS is_distinct

/*-------------+
 | is_distinct |
 +-------------+
 | true        |
 +-------------*/
```

```googlesql
SELECT ARRAY_IS_DISTINCT(NULL) AS is_distinct

/*-------------+
 | is_distinct |
 +-------------+
 | NULL        |
 +-------------*/
```

## `ARRAY_LAST`

```googlesql
ARRAY_LAST(array_expression)
```

**Description**

Takes an array and returns the last element in the array.

Produces an error if the array is empty.

Returns `NULL` if `array_expression` is `NULL`.

Note: To get the first element in an array, see [`ARRAY_FIRST`][array-first].

**Return type**

Matches the data type of elements in `array_expression`.

**Example**

```googlesql
SELECT ARRAY_LAST(['a','b','c','d']) as last_element

/*---------------+
 | last_element  |
 +---------------+
 | d             |
 +---------------*/
```

[array-first]: #array_first

## `ARRAY_LENGTH`

```googlesql
ARRAY_LENGTH(array_expression)
```

**Description**

Returns the size of the array. Returns 0 for an empty array. Returns `NULL` if
the `array_expression` is `NULL`.

**Return type**

`INT64`

**Examples**

```googlesql
SELECT
  ARRAY_LENGTH(["coffee", NULL, "milk" ]) AS size_a,
  ARRAY_LENGTH(["cake", "pie"]) AS size_b;

/*--------+--------+
 | size_a | size_b |
 +--------+--------+
 | 3      | 2      |
 +--------+--------*/
```

## `ARRAY_MAX`

```googlesql
ARRAY_MAX(input_array)
```

**Description**

Returns the maximum non-`NULL` value in an array.

Caveats:

+ If the array is `NULL`, empty, or contains only `NULL`s, returns
  `NULL`.
+ If the array contains `NaN`, returns `NaN`.

**Supported Argument Types**

In the input array, `ARRAY<T>`, `T` can be an
[orderable data type][data-type-properties].

**Return type**

The same data type as `T` in the input array.

**Examples**

```googlesql
SELECT ARRAY_MAX([8, 37, NULL, 55, 4]) as max

/*-----+
 | max |
 +-----+
 | 55  |
 +-----*/
```

[data-type-properties]: https://github.com/google/googlesql/blob/master/docs/data-types.md#data_type_properties

## `ARRAY_MIN`

```googlesql
ARRAY_MIN(input_array)
```

**Description**

Returns the minimum non-`NULL` value in an array.

Caveats:

+ If the array is `NULL`, empty, or contains only `NULL`s, returns
  `NULL`.
+ If the array contains `NaN`, returns `NaN`.

**Supported Argument Types**

In the input array, `ARRAY<T>`, `T` can be an
[orderable data type][data-type-properties].

**Return type**

The same data type as `T` in the input array.

**Examples**

```googlesql
SELECT ARRAY_MIN([8, 37, NULL, 4, 55]) as min

/*-----+
 | min |
 +-----+
 | 4   |
 +-----*/
```

[data-type-properties]: https://github.com/google/googlesql/blob/master/docs/data-types.md#data_type_properties

## `ARRAY_REVERSE`

```googlesql
ARRAY_REVERSE(value)
```

**Description**

Returns the input `ARRAY` with elements in reverse order.

**Return type**

`ARRAY`

**Examples**

```googlesql
SELECT ARRAY_REVERSE([1, 2, 3]) AS reverse_arr

/*-------------+
 | reverse_arr |
 +-------------+
 | [3, 2, 1]   |
 +-------------*/
```

## `ARRAY_SLICE`

```googlesql
ARRAY_SLICE(array_to_slice, start_offset, end_offset)
```

**Description**

Returns an array containing zero or more consecutive elements from the
input array.

+ `array_to_slice`: The array that contains the elements you want to slice.
+ `start_offset`: The inclusive starting offset.
+ `end_offset`: The inclusive ending offset.

An offset can be positive or negative. A positive offset starts from the
beginning of the input array and is 0-based. A negative offset starts from
the end of the input array. Out-of-bounds offsets are supported. Here are some
examples:

  <table>
  <thead>
    <tr>
      <th width="150px">Input offset</th>
      <th width="200px">Final offset in array</th>
      <th>Notes</th>
    </tr>
  </thead>
  <tbody>
    <tr>
      <td>0</td>
      <td>[<b>'a'</b>, 'b', 'c', 'd']</td>
      <td>The final offset is <code>0</code>.</td>
    </tr>
    <tr>
      <td>3</td>
      <td>['a', 'b', 'c', <b>'d'</b>]</td>
      <td>The final offset is <code>3</code>.</td>
    </tr>
    <tr>
      <td>5</td>
      <td>['a', 'b', 'c', <b>'d'</b>]</td>
      <td>
        Because the input offset is out of bounds,
        the final offset is <code>3</code> (<code>array length - 1</code>).
      </td>
    </tr>
    <tr>
      <td>-1</td>
      <td>['a', 'b', 'c', <b>'d'</b>]</td>
      <td>
        Because a negative offset is used, the offset starts at the end of the
        array. The final offset is <code>3</code>
        (<code>array length - 1</code>).
      </td>
    </tr>
    <tr>
      <td>-2</td>
      <td>['a', 'b', <b>'c'</b>, 'd']</td>
      <td>
        Because a negative offset is used, the offset starts at the end of the
        array. The final offset is <code>2</code>
        (<code>array length - 2</code>).
      </td>
    </tr>
    <tr>
      <td>-4</td>
      <td>[<b>'a'</b>, 'b', 'c', 'd']</td>
      <td>
        Because a negative offset is used, the offset starts at the end of the
        array. The final offset is <code>0</code>
        (<code>array length - 4</code>).
      </td>
    </tr>
    <tr>
      <td>-5</td>
      <td>[<b>'a'</b>, 'b', 'c', 'd']</td>
      <td>
        Because the offset is negative and out of bounds, the final offset is
        <code>0</code> (<code>array length - array length</code>).
      </td>
    </tr>
  </tbody>
</table>

Additional details:

+ The input array can contain `NULL` elements. `NULL` elements are included
  in the resulting array.
+ Returns `NULL` if `array_to_slice`, `start_offset`, or `end_offset` is
  `NULL`.
+ Returns an empty array if `array_to_slice` is empty.
+ Returns an empty array if the position of the `start_offset` in the array is
  after the position of the `end_offset`.

**Return type**

`ARRAY`

**Examples**

```googlesql
SELECT ARRAY_SLICE(['a', 'b', 'c', 'd', 'e'], 1, 3) AS result

/*-----------+
 | result    |
 +-----------+
 | [b, c, d] |
 +-----------*/
```

```googlesql
SELECT ARRAY_SLICE(['a', 'b', 'c', 'd', 'e'], -1, 3) AS result

/*-----------+
 | result    |
 +-----------+
 | []        |
 +-----------*/
```

```googlesql
SELECT ARRAY_SLICE(['a', 'b', 'c', 'd', 'e'], 1, -3) AS result

/*--------+
 | result |
 +--------+
 | [b, c] |
 +--------*/
```

```googlesql
SELECT ARRAY_SLICE(['a', 'b', 'c', 'd', 'e'], -1, -3) AS result

/*-----------+
 | result    |
 +-----------+
 | []        |
 +-----------*/
```

```googlesql
SELECT ARRAY_SLICE(['a', 'b', 'c', 'd', 'e'], -3, -1) AS result

/*-----------+
 | result    |
 +-----------+
 | [c, d, e] |
 +-----------*/
```

```googlesql
SELECT ARRAY_SLICE(['a', 'b', 'c', 'd', 'e'], 3, 3) AS result

/*--------+
 | result |
 +--------+
 | [d]    |
 +--------*/
```

```googlesql
SELECT ARRAY_SLICE(['a', 'b', 'c', 'd', 'e'], -3, -3) AS result

/*--------+
 | result |
 +--------+
 | [c]    |
 +--------*/
```

```googlesql
SELECT ARRAY_SLICE(['a', 'b', 'c', 'd', 'e'], 1, 30) AS result

/*--------------+
 | result       |
 +--------------+
 | [b, c, d, e] |
 +--------------*/
```

```googlesql
SELECT ARRAY_SLICE(['a', 'b', 'c', 'd', 'e'], 1, -30) AS result

/*-----------+
 | result    |
 +-----------+
 | []        |
 +-----------*/
```

```googlesql
SELECT ARRAY_SLICE(['a', 'b', 'c', 'd', 'e'], -30, 30) AS result

/*-----------------+
 | result          |
 +-----------------+
 | [a, b, c, d, e] |
 +-----------------*/
```

```googlesql
SELECT ARRAY_SLICE(['a', 'b', 'c', 'd', 'e'], -30, -5) AS result

/*--------+
 | result |
 +--------+
 | [a]    |
 +--------*/
```

```googlesql
SELECT ARRAY_SLICE(['a', 'b', 'c', 'd', 'e'], 5, 30) AS result

/*--------+
 | result |
 +--------+
 | []     |
 +--------*/
```

```googlesql
SELECT ARRAY_SLICE(['a', 'b', 'c', 'd', 'e'], 1, NULL) AS result

/*-----------+
 | result    |
 +-----------+
 | NULL      |
 +-----------*/
```

```googlesql
SELECT ARRAY_SLICE(['a', 'b', NULL, 'd', 'e'], 1, 3) AS result

/*--------------+
 | result       |
 +--------------+
 | [b, NULL, d] |
 +--------------*/
```

## `ARRAY_SUM`

```googlesql
ARRAY_SUM(input_array)
```

**Description**

Returns the sum of non-`NULL` values in an array.

Caveats:

+ If the array is `NULL`, empty, or contains only `NULL`s, returns
  `NULL`.
+ If the array contains `NaN`, returns `NaN`.
+ If the array contains `[+|-]Infinity`, returns either `[+|-]Infinity`
  or `NaN`.
+ If there is numeric overflow, produces an error.
+ If a [floating-point type][floating-point-types] is returned, the result is
  [non-deterministic][non-deterministic], which means you might receive a
  different result each time you use this function.

[floating-point-types]: https://github.com/google/googlesql/blob/master/docs/data-types.md#floating_point_types

[non-deterministic]: https://github.com/google/googlesql/blob/master/docs/data-types.md#floating_point_semantics

**Supported Argument Types**

In the input array, `ARRAY<T>`, `T` can represent:

+ Any supported numeric data type
+ `INTERVAL`

**Return type**

The return type depends upon `T` in the input array:

<table>

<thead>
<tr>
<th>INPUT</th><th><code>INT32</code></th><th><code>INT64</code></th><th><code>UINT32</code></th><th><code>UINT64</code></th><th><code>NUMERIC</code></th><th><code>BIGNUMERIC</code></th><th><code>FLOAT</code></th><th><code>DOUBLE</code></th><th><code>INTERVAL</code></th>
</tr>
</thead>
<tbody>
<tr><th>OUTPUT</th><td style="vertical-align:middle"><code>INT64</code></td><td style="vertical-align:middle"><code>INT64</code></td><td style="vertical-align:middle"><code>UINT64</code></td><td style="vertical-align:middle"><code>UINT64</code></td><td style="vertical-align:middle"><code>NUMERIC</code></td><td style="vertical-align:middle"><code>BIGNUMERIC</code></td><td style="vertical-align:middle"><code>DOUBLE</code></td><td style="vertical-align:middle"><code>DOUBLE</code></td><td style="vertical-align:middle"><code>INTERVAL</code></td></tr>
</tbody>

</table>

**Examples**

```googlesql
SELECT ARRAY_SUM([1, 2, 3, 4, 5, NULL, 4, 3, 2, 1]) as sum

/*-----+
 | sum |
 +-----+
 | 25  |
 +-----*/
```

## `ARRAY_TO_STRING`

```googlesql
ARRAY_TO_STRING(array_expression, delimiter[, null_text])
```

**Description**

Returns a concatenation of the elements in `array_expression` as a `STRING`
or `BYTES` value. The value for `array_expression` can
either be an array of `STRING` or `BYTES` data type.

If the `null_text` parameter is used, the function replaces any `NULL` values in
the array with the value of `null_text`.

If the `null_text` parameter isn't used, the function omits the `NULL` value
and its preceding delimiter.

**Return type**

* `STRING` for a function signature with `STRING` input.
* `BYTES` for a function signature with `BYTES` input.

**Examples**

```googlesql
SELECT ARRAY_TO_STRING(['coffee', 'tea', 'milk', NULL], '--', 'MISSING') AS text

/*--------------------------------+
 | text                           |
 +--------------------------------+
 | coffee--tea--milk--MISSING     |
 +--------------------------------*/
```

```googlesql

SELECT ARRAY_TO_STRING(['cake', 'pie', NULL], '--', 'MISSING') AS text

/*--------------------------------+
 | text                           |
 +--------------------------------+
 | cake--pie--MISSING             |
 +--------------------------------*/
```

```googlesql

SELECT ARRAY_TO_STRING([b'prefix', b'middle', b'suffix', b'\x00'], b'--') AS data

/*--------------------------------+
 | data                           |
 +--------------------------------+
 | prefix--middle--suffix--\x00   |
 +--------------------------------*/
```

## `ARRAY_TRANSFORM`

```googlesql
ARRAY_TRANSFORM(array_expression, lambda_expression)

lambda_expression:
  {
    element_alias -> transform_expression
    | (element_alias, index_alias) -> transform_expression
  }
```

**Description**

Takes an array, transforms the elements, and returns the results in a new array.
The output array always has the same length as the input array.

+   `array_expression`: The array to transform.
+   `lambda_expression`: Each element in `array_expression` is evaluated against
    the [lambda expression][lambda-definition]. The evaluation results are
    returned in a new array.
+   `element_alias`: An alias that represents an array element.
+   `index_alias`: An alias that represents the zero-based offset of the array
    element.
+   `transform_expression`: The expression used to transform the array elements.

Returns `NULL` if the `array_expression` is `NULL`.

**Return type**

`ARRAY`

**Example**

```googlesql
SELECT
  ARRAY_TRANSFORM([1, 4, 3], e -> e + 1) AS a1,
  ARRAY_TRANSFORM([1, 4, 3], (e, i) -> e + i) AS a2;

/*---------+---------+
 | a1      | a2      |
 +---------+---------+
 | [2,5,4] | [1,5,5] |
 +---------+---------*/
```

[lambda-definition]: https://github.com/google/googlesql/blob/master/docs/functions-reference.md#lambdas

## `ARRAY_ZIP`

```googlesql
ARRAY_ZIP(
  array_input [ AS alias ],
  array_input [ AS alias ][, ... ]
  [, [ transformation => ] value ]
  [, mode => { 'STRICT' | 'TRUNCATE' | 'PAD' } ]
)
```

**Description**

Combines the elements from two to four arrays into one array.

**Definitions**

+   `array_input`: An input `ARRAY` value to be zipped with the other array
    inputs. `ARRAY_ZIP` supports two to four input arrays.
+   `alias`: An alias optionally supplied for an `array_input`. In the results,
    the alias is the name of the associated `STRUCT` field.
+   `transformation`: A named argument with a lambda expression.
    The lambda expression specifies how elements are combined as they are
    zipped. This overrides the default `STRUCT` creation behavior.
+   `mode`: A named argument with a `STRING` value. Determines how arrays of
    differing lengths are zipped. If this argument isn't supplied, the
    function uses `STRICT` mode. This argument can be one of the
    following values:

    +   `STRICT` (default): If the length of any array is different from the
        others, produce an error.

    +   `TRUNCATE`: Truncate longer arrays to match the length of the shortest
        array.

    +   `PAD`: Pad shorter arrays with `NULL` values to match the length of the
        longest array.

**Details**

+   If an `array_input` or `mode` is `NULL`, this function returns `NULL`, even when
    `mode` is `STRICT`.
+   Argument aliases can't be used with the `transformation` argument.

**Return type**

+   If `transformation` is used and returns type `T`, the
    return type is `ARRAY<T>`.
+   Otherwise, the return type is `ARRAY<STRUCT>`, with the `STRUCT` having a
    number of fields equal to the number of input arrays. Each field's name is
    either the user-provided `alias` for the corresponding `array_input`, or a
    default alias assigned by the compiler, following the same logic used for
    [naming columns in a SELECT list][implicit-aliases].

**Examples**

The following query zips two arrays into one:

```googlesql
SELECT ARRAY_ZIP([1, 2], ['a', 'b']) AS results

/*----------------------+
 | results              |
 +----------------------+
 | [(1, 'a'), (2, 'b')] |
 +----------------------*/
```

You can give an array an alias. For example, in the following
query, the returned array is of type `ARRAY<STRUCT<A1, alias_inferred>>`,
where:

+   `A1` is the alias provided for array `[1, 2]`.
+   `alias_inferred` is the inferred alias provided for array `['a', 'b']`.

```googlesql
WITH T AS (
  SELECT ['a', 'b'] AS alias_inferred
)
SELECT ARRAY_ZIP([1, 2] AS A1, alias_inferred) AS results
FROM T

/*----------------------------------------------------------+
 | results                                                  |
 +----------------------------------------------------------+
 | [{1 A1, 'a' alias_inferred}, {2 A1, 'b' alias_inferred}] |
 +----------------------------------------------------------*/
```

To provide a custom transformation of the input arrays, use the `transformation`
argument:

```googlesql
SELECT ARRAY_ZIP([1, 2], [3, 4], transformation => (e1, e2) -> (e1 + e2))

/*---------+
 | results |
 +---------+
 | [4, 6]  |
 +---------*/
```

The argument name `transformation` isn't required. For example:

```googlesql
SELECT ARRAY_ZIP([1, 2], [3, 4], (e1, e2) -> (e1 + e2))

/*---------+
 | results |
 +---------+
 | [4, 6]  |
 +---------*/
```

When `transformation` is provided, the input arrays aren't allowed to have
aliases. For example, the following query is invalid:

```googlesql {.bad}
-- Error: ARRAY_ZIP function with lambda argument doesn't allow providing
-- argument aliases
SELECT ARRAY_ZIP([1, 2], [3, 4] AS alias_not_allowed, (e1, e2) -> (e1 + e2))
```

To produce an error when arrays with different lengths are zipped, don't
add `mode`, or if you do, set it as `STRICT`. For example:

```googlesql {.bad}
-- Error: Unequal array length
SELECT ARRAY_ZIP([1, 2], ['a', 'b', 'c', 'd']) AS results
```

```googlesql {.bad}
-- Error: Unequal array length
SELECT ARRAY_ZIP([1, 2], ['a', 'b', 'c', 'd'], mode => 'STRICT') AS results
```

Use the `PAD` mode to pad missing values with `NULL` when input arrays have
different lengths. For example:

```googlesql
SELECT ARRAY_ZIP([1, 2], ['a', 'b', 'c', 'd'], [], mode => 'PAD') AS results

/*------------------------------------------------------------------------+
 | results                                                                |
 +------------------------------------------------------------------------+
 | [{1, 'a', NULL}, {2, 'b', NULL}, {NULL, 'c', NULL}, {NULL, 'd', NULL}] |
 +------------------------------------------------------------------------*/
```

Use the `TRUNCATE` mode to truncate all arrays that are longer than the shortest
array. For example:

```googlesql
SELECT ARRAY_ZIP([1, 2], ['a', 'b', 'c', 'd'], mode => 'TRUNCATE') AS results

/*----------------------+
 | results              |
 +----------------------+
 | [(1, 'a'), (2, 'b')] |
 +----------------------*/
```

<!-- mdlint off(WHITESPACE_LINE_LENGTH) -->

[implicit-aliases]: https://github.com/google/googlesql/blob/master/docs/query-syntax.md#implicit_aliases

<!-- mdlint on -->

## `FLATTEN`

```googlesql
FLATTEN(array_elements_field_access_expression)
```

**Description**

Takes an array of nested data and flattens a specific part of it into a single,
flat array with the
[array elements field access operator][array-el-field-operator].
Returns `NULL` if the input value is `NULL`.
If `NULL` array elements are
encountered, they are added to the resulting array.

There are several ways to flatten nested data into arrays. To learn more, see
[Flattening nested data into an array][flatten-tree-to-array].

**Return type**

`ARRAY`

**Examples**

In the following example, all of the arrays for `v.sales.quantity` are
concatenated in a flattened array.

```googlesql
WITH t AS (
  SELECT
  [
    STRUCT([STRUCT([1,2,3] AS quantity), STRUCT([4,5,6] AS quantity)] AS sales),
    STRUCT([STRUCT([7,8] AS quantity), STRUCT([] AS quantity)] AS sales)
  ] AS v
)
SELECT FLATTEN(v.sales.quantity) AS all_values
FROM t;

/*--------------------------+
 | all_values               |
 +--------------------------+
 | [1, 2, 3, 4, 5, 6, 7, 8] |
 +--------------------------*/
```

In the following example, `OFFSET` gets the second value in each array and
concatenates them.

```googlesql
WITH t AS (
  SELECT
  [
    STRUCT([STRUCT([1,2,3] AS quantity), STRUCT([4,5,6] AS quantity)] AS sales),
    STRUCT([STRUCT([7,8,9] AS quantity), STRUCT([10,11,12] AS quantity)] AS sales)
  ] AS v
)
SELECT FLATTEN(v.sales.quantity[OFFSET(1)]) AS second_values
FROM t;

/*---------------+
 | second_values |
 +---------------+
 | [2, 5, 8, 11] |
 +---------------*/
```

In the following example, all values for `v.price` are returned in a
flattened array.

```googlesql
WITH t AS (
  SELECT
  [
    STRUCT(1 AS price, 2 AS quantity),
    STRUCT(10 AS price, 20 AS quantity)
  ] AS v
)
SELECT FLATTEN(v.price) AS all_prices
FROM t;

/*------------+
 | all_prices |
 +------------+
 | [1, 10]    |
 +------------*/
```

For more examples, including how to use protocol buffers with `FLATTEN`, see the
[array elements field access operator][array-el-field-operator].

[flatten-tree-to-array]: https://github.com/google/googlesql/blob/master/docs/arrays.md#flattening_nested_data_into_arrays

[array-el-field-operator]: https://github.com/google/googlesql/blob/master/docs/operators.md#array_el_field_operator

## `GENERATE_ARRAY`

```googlesql
GENERATE_ARRAY(start_expression, end_expression[, step_expression])
```

**Description**

Returns an array of values. The `start_expression` and `end_expression`
parameters determine the inclusive start and end of the array.

The `GENERATE_ARRAY` function accepts the following data types as inputs:

+ `INT64`
+ `UINT64`
+ `NUMERIC`
+ `BIGNUMERIC`
+ `DOUBLE`

The `step_expression` parameter determines the increment used to
generate array values. The default value for this parameter is `1`.

This function returns an error if `step_expression` is set to 0, or if any
input is `NaN`.

If any argument is `NULL`, the function will return a `NULL` array.

**Return Data Type**

`ARRAY`

**Examples**

The following returns an array of integers, with a default step of 1.

```googlesql
SELECT GENERATE_ARRAY(1, 5) AS example_array;

/*-----------------+
 | example_array   |
 +-----------------+
 | [1, 2, 3, 4, 5] |
 +-----------------*/
```

The following returns an array using a user-specified step size.

```googlesql
SELECT GENERATE_ARRAY(0, 10, 3) AS example_array;

/*---------------+
 | example_array |
 +---------------+
 | [0, 3, 6, 9]  |
 +---------------*/
```

The following returns an array using a negative value, `-3` for its step size.

```googlesql
SELECT GENERATE_ARRAY(10, 0, -3) AS example_array;

/*---------------+
 | example_array |
 +---------------+
 | [10, 7, 4, 1] |
 +---------------*/
```

The following returns an array using the same value for the `start_expression`
and `end_expression`.

```googlesql
SELECT GENERATE_ARRAY(4, 4, 10) AS example_array;

/*---------------+
 | example_array |
 +---------------+
 | [4]           |
 +---------------*/
```

The following returns an empty array, because the `start_expression` is greater
than the `end_expression`, and the `step_expression` value is positive.

```googlesql
SELECT GENERATE_ARRAY(10, 0, 3) AS example_array;

/*---------------+
 | example_array |
 +---------------+
 | []            |
 +---------------*/
```

The following returns a `NULL` array because `end_expression` is `NULL`.

```googlesql
SELECT GENERATE_ARRAY(5, NULL, 1) AS example_array;

/*---------------+
 | example_array |
 +---------------+
 | NULL          |
 +---------------*/
```

The following returns multiple arrays.

```googlesql
SELECT GENERATE_ARRAY(start, 5) AS example_array
FROM UNNEST([3, 4, 5]) AS start;

/*---------------+
 | example_array |
 +---------------+
 | [3, 4, 5]     |
 | [4, 5]        |
 | [5]           |
 +---------------*/
```

## `GENERATE_DATE_ARRAY`

```googlesql
GENERATE_DATE_ARRAY(start_date, end_date[, INTERVAL INT64_expr date_part])
```

**Description**

Returns an array of dates. The `start_date` and `end_date`
parameters determine the inclusive start and end of the array.

The `GENERATE_DATE_ARRAY` function accepts the following data types as inputs:

+ `start_date` must be a `DATE`.
+ `end_date` must be a `DATE`.
+ `INT64_expr` must be an `INT64`.
+ `date_part` must be either DAY, WEEK, MONTH, QUARTER, or YEAR.

The `INT64_expr` parameter determines the increment used to generate dates. The
default value for this parameter is 1 day.

This function returns an error if `INT64_expr` is set to 0.

**Return Data Type**

`ARRAY` containing 0 or more `DATE` values.

**Examples**

The following returns an array of dates, with a default step of 1.

```googlesql
SELECT GENERATE_DATE_ARRAY('2016-10-05', '2016-10-08') AS example;

/*--------------------------------------------------+
 | example                                          |
 +--------------------------------------------------+
 | [2016-10-05, 2016-10-06, 2016-10-07, 2016-10-08] |
 +--------------------------------------------------*/
```

The following returns an array using a user-specified step size.

```googlesql
SELECT GENERATE_DATE_ARRAY(
 '2016-10-05', '2016-10-09', INTERVAL 2 DAY) AS example;

/*--------------------------------------+
 | example                              |
 +--------------------------------------+
 | [2016-10-05, 2016-10-07, 2016-10-09] |
 +--------------------------------------*/
```

The following returns an array using a negative value, `-3` for its step size.

```googlesql
SELECT GENERATE_DATE_ARRAY('2016-10-05',
  '2016-10-01', INTERVAL -3 DAY) AS example;

/*--------------------------+
 | example                  |
 +--------------------------+
 | [2016-10-05, 2016-10-02] |
 +--------------------------*/
```

The following returns an array using the same value for the `start_date`and
`end_date`.

```googlesql
SELECT GENERATE_DATE_ARRAY('2016-10-05',
  '2016-10-05', INTERVAL 8 DAY) AS example;

/*--------------+
 | example      |
 +--------------+
 | [2016-10-05] |
 +--------------*/
```

The following returns an empty array, because the `start_date` is greater
than the `end_date`, and the `step` value is positive.

```googlesql
SELECT GENERATE_DATE_ARRAY('2016-10-05',
  '2016-10-01', INTERVAL 1 DAY) AS example;

/*---------+
 | example |
 +---------+
 | []      |
 +---------*/
```

The following returns a `NULL` array, because one of its inputs is
`NULL`.

```googlesql
SELECT GENERATE_DATE_ARRAY('2016-10-05', NULL) AS example;

/*---------+
 | example |
 +---------+
 | NULL    |
 +---------*/
```

The following returns an array of dates, using MONTH as the `date_part`
interval:

```googlesql
SELECT GENERATE_DATE_ARRAY('2016-01-01',
  '2016-12-31', INTERVAL 2 MONTH) AS example;

/*--------------------------------------------------------------------------+
 | example                                                                  |
 +--------------------------------------------------------------------------+
 | [2016-01-01, 2016-03-01, 2016-05-01, 2016-07-01, 2016-09-01, 2016-11-01] |
 +--------------------------------------------------------------------------*/
```

The following uses non-constant dates to generate an array.

```googlesql
SELECT GENERATE_DATE_ARRAY(date_start, date_end, INTERVAL 1 WEEK) AS date_range
FROM (
  SELECT DATE '2016-01-01' AS date_start, DATE '2016-01-31' AS date_end
  UNION ALL SELECT DATE "2016-04-01", DATE "2016-04-30"
  UNION ALL SELECT DATE "2016-07-01", DATE "2016-07-31"
  UNION ALL SELECT DATE "2016-10-01", DATE "2016-10-31"
) AS items;

/*--------------------------------------------------------------+
 | date_range                                                   |
 +--------------------------------------------------------------+
 | [2016-01-01, 2016-01-08, 2016-01-15, 2016-01-22, 2016-01-29] |
 | [2016-04-01, 2016-04-08, 2016-04-15, 2016-04-22, 2016-04-29] |
 | [2016-07-01, 2016-07-08, 2016-07-15, 2016-07-22, 2016-07-29] |
 | [2016-10-01, 2016-10-08, 2016-10-15, 2016-10-22, 2016-10-29] |
 +--------------------------------------------------------------*/
```

## `GENERATE_TIMESTAMP_ARRAY`

```googlesql
GENERATE_TIMESTAMP_ARRAY(start_timestamp, end_timestamp,
                         INTERVAL step_expression date_part)
```

**Description**

Returns an `ARRAY` of `TIMESTAMPS` separated by a given interval. The
`start_timestamp` and `end_timestamp` parameters determine the inclusive
lower and upper bounds of the `ARRAY`.

The `GENERATE_TIMESTAMP_ARRAY` function accepts the following data types as
inputs:

+ `start_timestamp`: `TIMESTAMP`
+ `end_timestamp`: `TIMESTAMP`
+ `step_expression`: `INT64`
+ Allowed `date_part` values are:
  `PICOSECOND`,
  `NANOSECOND`,
  `MICROSECOND`, `MILLISECOND`, `SECOND`, `MINUTE`, `HOUR`, or `DAY`.

The `step_expression` parameter determines the increment used to generate
timestamps.

**Return Data Type**

An `ARRAY` containing 0 or more `TIMESTAMP` values.

**Examples**

The following example returns an `ARRAY` of `TIMESTAMP`s at intervals of 1 day.

```googlesql
SELECT GENERATE_TIMESTAMP_ARRAY('2016-10-05 00:00:00', '2016-10-07 00:00:00',
                                INTERVAL 1 DAY) AS timestamp_array;

/*--------------------------------------------------------------------------+
 | timestamp_array                                                          |
 +--------------------------------------------------------------------------+
 | [2016-10-05 00:00:00+00, 2016-10-06 00:00:00+00, 2016-10-07 00:00:00+00] |
 +--------------------------------------------------------------------------*/
```

The following example returns an `ARRAY` of `TIMESTAMP`s at intervals of 1
second.

```googlesql
SELECT GENERATE_TIMESTAMP_ARRAY('2016-10-05 00:00:00', '2016-10-05 00:00:02',
                                INTERVAL 1 SECOND) AS timestamp_array;

/*--------------------------------------------------------------------------+
 | timestamp_array                                                          |
 +--------------------------------------------------------------------------+
 | [2016-10-05 00:00:00+00, 2016-10-05 00:00:01+00, 2016-10-05 00:00:02+00] |
 +--------------------------------------------------------------------------*/
```

The following example returns an `ARRAY` of `TIMESTAMPS` with a negative
interval.

```googlesql
SELECT GENERATE_TIMESTAMP_ARRAY('2016-10-06 00:00:00', '2016-10-01 00:00:00',
                                INTERVAL -2 DAY) AS timestamp_array;

/*--------------------------------------------------------------------------+
 | timestamp_array                                                          |
 +--------------------------------------------------------------------------+
 | [2016-10-06 00:00:00+00, 2016-10-04 00:00:00+00, 2016-10-02 00:00:00+00] |
 +--------------------------------------------------------------------------*/
```

The following example returns an `ARRAY` with a single element, because
`start_timestamp` and `end_timestamp` have the same value.

```googlesql
SELECT GENERATE_TIMESTAMP_ARRAY('2016-10-05 00:00:00', '2016-10-05 00:00:00',
                                INTERVAL 1 HOUR) AS timestamp_array;

/*--------------------------+
 | timestamp_array          |
 +--------------------------+
 | [2016-10-05 00:00:00+00] |
 +--------------------------*/
```

The following example returns an empty `ARRAY`, because `start_timestamp` is
later than `end_timestamp`.

```googlesql
SELECT GENERATE_TIMESTAMP_ARRAY('2016-10-06 00:00:00', '2016-10-05 00:00:00',
                                INTERVAL 1 HOUR) AS timestamp_array;

/*-----------------+
 | timestamp_array |
 +-----------------+
 | []              |
 +-----------------*/
```

The following example returns a null `ARRAY`, because one of the inputs is
`NULL`.

```googlesql
SELECT GENERATE_TIMESTAMP_ARRAY('2016-10-05 00:00:00', NULL, INTERVAL 1 HOUR)
  AS timestamp_array;

/*-----------------+
 | timestamp_array |
 +-----------------+
 | NULL            |
 +-----------------*/
```

The following example generates `ARRAY`s of `TIMESTAMP`s from columns containing
values for `start_timestamp` and `end_timestamp`.

```googlesql
SELECT GENERATE_TIMESTAMP_ARRAY(start_timestamp, end_timestamp, INTERVAL 1 HOUR)
  AS timestamp_array
FROM
  (SELECT
    TIMESTAMP '2016-10-05 00:00:00' AS start_timestamp,
    TIMESTAMP '2016-10-05 02:00:00' AS end_timestamp
   UNION ALL
   SELECT
    TIMESTAMP '2016-10-05 12:00:00' AS start_timestamp,
    TIMESTAMP '2016-10-05 14:00:00' AS end_timestamp
   UNION ALL
   SELECT
    TIMESTAMP '2016-10-05 23:59:00' AS start_timestamp,
    TIMESTAMP '2016-10-06 01:59:00' AS end_timestamp);

/*--------------------------------------------------------------------------+
 | timestamp_array                                                          |
 +--------------------------------------------------------------------------+
 | [2016-10-05 00:00:00+00, 2016-10-05 01:00:00+00, 2016-10-05 02:00:00+00] |
 | [2016-10-05 12:00:00+00, 2016-10-05 13:00:00+00, 2016-10-05 14:00:00+00] |
 | [2016-10-05 23:59:00+00, 2016-10-06 00:59:00+00, 2016-10-06 01:59:00+00] |
 +--------------------------------------------------------------------------*/
```

## Supplemental materials

### OFFSET and ORDINAL

For information about using `OFFSET` and `ORDINAL` with arrays, see
[Array subscript operator][array-subscript-operator] and [Accessing array
elements][accessing-array-elements].

<!-- mdlint off(WHITESPACE_LINE_LENGTH) -->

[array-subscript-operator]: https://github.com/google/googlesql/blob/master/docs/operators.md#array_subscript_operator

[accessing-array-elements]: https://github.com/google/googlesql/blob/master/docs/arrays.md#accessing_array_elements

<!-- mdlint on -->

