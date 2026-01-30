

<!-- mdlint off(WHITESPACE_LINE_LENGTH) -->

# HyperLogLog++ functions 
<a id="hll_functions"></a>

The [HyperLogLog++ algorithm (HLL++)][hll-sketches] estimates
[cardinality][cardinality] from sketches.

HLL++ functions are approximate aggregate functions.
Approximate aggregation typically requires less
memory than exact aggregation functions,
like [`COUNT(DISTINCT)`][count-distinct], but also introduces statistical error.
This makes HLL++ functions appropriate for large data streams for
which linear memory usage is impractical, as well as for data that is
already approximate.

A data sketch is a compact summary of a data aggregation. If you don't need
materialized sketches, you can alternatively use an
[approximate aggregate function with system-defined precision][approx-functions-reference],
such as [`APPROX_COUNT_DISTINCT`][approx-count-distinct]. However,
`APPROX_COUNT_DISTINCT` doesn't allow partial aggregations, re-aggregations,
and custom precision.

GoogleSQL supports the following HLL++ functions.

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
  <td><a href="https://github.com/google/googlesql/blob/master/docs/hll_functions.md#hll_countextract"><code>HLL_COUNT.EXTRACT</code></a>
</td>
  <td>
    Extracts a cardinality estimate of an HLL++ sketch.
  </td>
</tr>

<tr>
  <td><a href="https://github.com/google/googlesql/blob/master/docs/hll_functions.md#hll_countinit"><code>HLL_COUNT.INIT</code></a>
</td>
  <td>
    Aggregates values of the same underlying type into a new HLL++ sketch.
  </td>
</tr>

<tr>
  <td><a href="https://github.com/google/googlesql/blob/master/docs/hll_functions.md#hll_countmerge"><code>HLL_COUNT.MERGE</code></a>
</td>
  <td>
    Merges HLL++ sketches of the same underlying type into a new sketch, and
    then gets the cardinality of the new sketch.
  </td>
</tr>

<tr>
  <td><a href="https://github.com/google/googlesql/blob/master/docs/hll_functions.md#hll_countmerge_partial"><code>HLL_COUNT.MERGE_PARTIAL</code></a>
</td>
  <td>
    Merges HLL++ sketches of the same underlying type into a new sketch.
  </td>
</tr>

  </tbody>
</table>

## `HLL_COUNT.EXTRACT`

```
HLL_COUNT.EXTRACT(sketch)
```

**Description**

A scalar function that extracts a cardinality estimate of a single
[HLL++][hll-link-to-research-whitepaper] sketch.

If `sketch` is `NULL`, this function returns a cardinality estimate of `0`.

**Supported input types**

`BYTES`

**Return type**

`INT64`

**Example**

The following query returns the number of distinct users for each country who
have at least one invoice.

```googlesql
SELECT
  country,
  HLL_COUNT.EXTRACT(HLL_sketch) AS distinct_customers_with_open_invoice
FROM
  (
    SELECT
      country,
      HLL_COUNT.INIT(customer_id) AS hll_sketch
    FROM
      UNNEST(
        ARRAY<STRUCT<country STRING, customer_id STRING, invoice_id STRING>>[
          ('UA', 'customer_id_1', 'invoice_id_11'),
          ('BR', 'customer_id_3', 'invoice_id_31'),
          ('CZ', 'customer_id_2', 'invoice_id_22'),
          ('CZ', 'customer_id_2', 'invoice_id_23'),
          ('BR', 'customer_id_3', 'invoice_id_31'),
          ('UA', 'customer_id_2', 'invoice_id_24')])
    GROUP BY country
  );

/*---------+--------------------------------------+
 | country | distinct_customers_with_open_invoice |
 +---------+--------------------------------------+
 | UA      |                                    2 |
 | BR      |                                    1 |
 | CZ      |                                    1 |
 +---------+--------------------------------------*/
```

[hll-link-to-research-whitepaper]: https://static.googleusercontent.com/media/research.google.com/en//pubs/archive/40671.pdf

## `HLL_COUNT.INIT`

```
HLL_COUNT.INIT(input [, precision])
```

**Description**

An aggregate function that takes one or more `input` values and aggregates them
into a [HLL++][hll-link-to-research-whitepaper] sketch. Each sketch
is represented using the `BYTES` data type. You can then merge sketches using
`HLL_COUNT.MERGE` or `HLL_COUNT.MERGE_PARTIAL`. If no merging is needed,
you can extract the final count of distinct values from the sketch using
`HLL_COUNT.EXTRACT`.

This function supports an optional parameter, `precision`. This parameter
defines the accuracy of the estimate at the cost of additional memory required
to process the sketches or store them on disk. The range for this value is
`10` to `24`. The default value is `15`. For more information about precision,
see [Precision for sketches][precision_hll].

If the input is `NULL`, this function returns `NULL`.

For more information, see [HyperLogLog in Practice: Algorithmic Engineering of
a State of The Art Cardinality Estimation Algorithm][hll-link-to-research-whitepaper].

**Supported input types**

+ `INT64`
+ `UINT64`
+ `NUMERIC`
+ `BIGNUMERIC`
+ `STRING`
+ `BYTES`

**Return type**

`BYTES`

**Example**

The following query creates HLL++ sketches that count the number of distinct
users with at least one invoice per country.

```googlesql
SELECT
  country,
  HLL_COUNT.INIT(customer_id, 10)
    AS hll_sketch
FROM
  UNNEST(
    ARRAY<STRUCT<country STRING, customer_id STRING, invoice_id STRING>>[
      ('UA', 'customer_id_1', 'invoice_id_11'),
      ('CZ', 'customer_id_2', 'invoice_id_22'),
      ('CZ', 'customer_id_2', 'invoice_id_23'),
      ('BR', 'customer_id_3', 'invoice_id_31'),
      ('UA', 'customer_id_2', 'invoice_id_24')])
GROUP BY country;

/*---------+------------------------------------------------------------------------------------+
 | country | hll_sketch                                                                         |
 +---------+------------------------------------------------------------------------------------+
 | UA      | "\010p\020\002\030\002 \013\202\007\r\020\002\030\n \0172\005\371\344\001\315\010" |
 | CZ      | "\010p\020\002\030\002 \013\202\007\013\020\001\030\n \0172\003\371\344\001"       |
 | BR      | "\010p\020\001\030\002 \013\202\007\013\020\001\030\n \0172\003\202\341\001"       |
 +---------+------------------------------------------------------------------------------------*/
```

[hll-link-to-research-whitepaper]: https://static.googleusercontent.com/media/research.google.com/en//pubs/archive/40671.pdf

[precision_hll]: https://github.com/google/googlesql/blob/master/docs/sketches.md#precision_hll

## `HLL_COUNT.MERGE`

```
HLL_COUNT.MERGE(sketch)
```

**Description**

An aggregate function that returns the cardinality of several
[HLL++][hll-link-to-research-whitepaper] sketches by computing their union.

Each `sketch` must be initialized on the same type. Attempts to merge sketches
for different types results in an error. For example, you can't merge a sketch
initialized from `INT64` data with one initialized from `STRING` data.

If the merged sketches were initialized with different precisions, the precision
will be downgraded to the lowest precision involved in the merge.

This function ignores `NULL` values when merging sketches. If the merge happens
over zero rows or only over `NULL` values, the function returns `0`.

**Supported input types**

`BYTES`

**Return type**

`INT64`

**Example**

 The following query counts the number of distinct users across all countries
 who have at least one invoice.

```googlesql
SELECT HLL_COUNT.MERGE(hll_sketch) AS distinct_customers_with_open_invoice
FROM
  (
    SELECT
      country,
      HLL_COUNT.INIT(customer_id) AS hll_sketch
    FROM
      UNNEST(
        ARRAY<STRUCT<country STRING, customer_id STRING, invoice_id STRING>>[
          ('UA', 'customer_id_1', 'invoice_id_11'),
          ('BR', 'customer_id_3', 'invoice_id_31'),
          ('CZ', 'customer_id_2', 'invoice_id_22'),
          ('CZ', 'customer_id_2', 'invoice_id_23'),
          ('BR', 'customer_id_3', 'invoice_id_31'),
          ('UA', 'customer_id_2', 'invoice_id_24')])
    GROUP BY country
  );

/*--------------------------------------+
 | distinct_customers_with_open_invoice |
 +--------------------------------------+
 |                                    3 |
 +--------------------------------------*/
```

[hll-link-to-research-whitepaper]: https://static.googleusercontent.com/media/research.google.com/en//pubs/archive/40671.pdf

## `HLL_COUNT.MERGE_PARTIAL`

```
HLL_COUNT.MERGE_PARTIAL(sketch)
```

**Description**

An aggregate function that takes one or more
[HLL++][hll-link-to-research-whitepaper] `sketch`
inputs and merges them into a new sketch.

Each `sketch` must be initialized on the same type. Attempts to merge sketches
for different types results in an error. For example, you can't merge a sketch
initialized from `INT64` data with one initialized from `STRING` data.

If the merged sketches were initialized with different precisions, the precision
will be downgraded to the lowest precision involved in the merge. For example,
if `MERGE_PARTIAL` encounters sketches of precision 14 and 15, the returned new
sketch will have precision 14.

This function returns `NULL` if there is no input or all inputs are `NULL`.

**Supported input types**

`BYTES`

**Return type**

`BYTES`

**Example**

The following query returns an HLL++ sketch that counts the number of distinct
users who have at least one invoice across all countries.

```googlesql
SELECT HLL_COUNT.MERGE_PARTIAL(HLL_sketch) AS distinct_customers_with_open_invoice
FROM
  (
    SELECT
      country,
      HLL_COUNT.INIT(customer_id) AS hll_sketch
    FROM
      UNNEST(
        ARRAY<STRUCT<country STRING, customer_id STRING, invoice_id STRING>>[
          ('UA', 'customer_id_1', 'invoice_id_11'),
          ('BR', 'customer_id_3', 'invoice_id_31'),
          ('CZ', 'customer_id_2', 'invoice_id_22'),
          ('CZ', 'customer_id_2', 'invoice_id_23'),
          ('BR', 'customer_id_3', 'invoice_id_31'),
          ('UA', 'customer_id_2', 'invoice_id_24')])
    GROUP BY country
  );

/*----------------------------------------------------------------------------------------------+
 | distinct_customers_with_open_invoice                                                         |
 +----------------------------------------------------------------------------------------------+
 | "\010p\020\006\030\002 \013\202\007\020\020\003\030\017 \0242\010\320\2408\352}\244\223\002" |
 +----------------------------------------------------------------------------------------------*/
```

[hll-link-to-research-whitepaper]: https://static.googleusercontent.com/media/research.google.com/en//pubs/archive/40671.pdf

[hll-sketches]: https://en.wikipedia.org/wiki/HyperLogLog

[cardinality]: https://en.wikipedia.org/wiki/Cardinality

[count-distinct]: https://github.com/google/googlesql/blob/master/docs/aggregate_functions.md#count

[approx-count-distinct]: https://github.com/google/googlesql/blob/master/docs/approximate_aggregate_functions.md#approx_count_distinct

[approx-functions-reference]: https://github.com/google/googlesql/blob/master/docs/approximate_aggregate_functions.md

