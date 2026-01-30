

<!-- mdlint off(WHITESPACE_LINE_LENGTH) -->

# Window function calls

A window function, also known as an analytic function, computes values
over a group of rows and returns a single result for _each_ row. This is
different from an aggregate function, which returns a single result for
_a group_ of rows.

A window function includes an `OVER` clause, which defines a window of rows
around the row being evaluated. For each row, the window function result
is computed using the selected window of rows as input, possibly
doing aggregation.

With window functions you can compute moving averages, rank items, calculate
cumulative sums, and perform other analyses.

## Window function syntax 
<a id="syntax"></a>

<pre>
function_name ( [ argument_list ] ) OVER over_clause

<a href="#def_over_clause">over_clause</a>:
  { named_window | ( [ window_specification ] ) }

<a href="#def_window_spec">window_specification</a>:
  [ named_window ]
  [ PARTITION BY partition_expression [, ...] ]
  [ ORDER BY expression [ { ASC | DESC }  ] [, ...] ]
  [ window_frame_clause ]

<a href="#def_window_frame">window_frame_clause</a>:
  { rows_range } { <a href="#def_window_frame">frame_start</a> | <a href="#def_window_frame">frame_between</a> }

<a href="#def_window_frame">rows_range</a>:
  { ROWS | RANGE }
</pre>

**Description**

A window function computes results over a group of rows. You can use the
following syntax to build a window function:

+  `function_name`: The function that performs a window operation.

   For example, the numbering function `RANK()` could be used here.
+  `argument_list`: Arguments that are specific to the function.
   Some functions have them, some don't.
+  `OVER`: Keyword required in the window function syntax preceding
   the [`OVER` clause][over-clause-def].
+  [`over_clause`][over-clause-def]: References a window that defines a group
   of rows in a table upon which to use a window function.
+  [`window_specification`][window-specs-def]: Defines the specifications for
   the window.
+  [`window_frame_clause`][window-frame-clause-def]: Defines the window frame
   for the window.
+  [`rows_range`][window-frame-clause-def]: Defines the physical rows or a
   logical range for a window frame.

**Notes**

A window function can appear as a scalar expression operand in
the following places in the query:

+  The `SELECT` list. If the window function appears in the `SELECT` list,
  its argument list and `OVER` clause can't refer to aliases introduced
  in the same `SELECT` list.
+  The `ORDER BY` clause. If the window function appears in the `ORDER BY`
  clause of the query, its argument list can refer to `SELECT`
  list aliases.
+  The `QUALIFY` clause.

A window function can't refer to another window function in its
argument list or its `OVER` clause, even indirectly through an alias.

A window function is evaluated after aggregation. For example, the
`GROUP BY` clause and non-window aggregate functions are evaluated first.
Because aggregate functions are evaluated before window functions,
aggregate functions can be used as input operands to window functions.

**Returns**

A single result for each row in the input.

### Defining the `OVER` clause 
<a id="def_over_clause"></a>

```googlesql
function_name ( [ argument_list ] ) OVER over_clause

over_clause:
  { named_window | ( [ window_specification ] ) }
```

**Description**

The `OVER` clause references a window that defines a group of rows in a table
upon which to use a window function. You can provide a
[`named_window`][named-windows] that is
[defined in your query][window-functions-link-to-window], or you can
define the [specifications for a new window][window-specs-def].

**Notes**

If neither a named window nor window specification is provided, all
input rows are included in the window for every row.

**Examples using the `OVER` clause**

These queries use window specifications:

+  [Compute a grand total][window-functions-compute-grand-total]
+  [Compute a subtotal][window-functions-compute-subtotal]
+  [Compute a cumulative sum][window-functions-compute-cumulative-sum]
+  [Compute a moving average][window-functions-compute-moving-avg]
+  [Compute the number of items within a range][window-functions-compute-item-range]
+  [Get the most popular item in each category][window-functions-get-popular-item]
+  [Get the last value in a range][window-functions-get-last-value-range]
+  [Compute rank][window-functions-compute-rank]

These queries use a named window:

+  [Get the last value in a range][window-functions-get-last-value-range]
+  [Use a named window in a window frame clause][window-functions-use-named-window]

### Defining the window specification 
<a id="def_window_spec"></a>

```googlesql
window_specification:
  [ named_window ]
  [ PARTITION BY partition_expression [, ...] ]
  [ ORDER BY expression [ { ASC | DESC } ] [, ...] ]
  [ window_frame_clause ]
```

**Description**

Defines the specifications for the window.

+  [`named_window`][named-windows]: The name of an existing window that was
   defined with a [`WINDOW` clause][window-functions-link-to-window].

Important: If you use a named window, special rules apply to
`PARTITION BY`, `ORDER BY`, and `window_frame_clause`. See
[Rules for using a named window in the window specification]
[named-window-rules].

+  `PARTITION BY`: Breaks up the input rows into separate partitions, over
   which the window function is independently evaluated.
   +  A `partition_expression` computes a value that determines which partition
      each row falls into.
   +  Multiple partition expressions are allowed in the `PARTITION BY` clause.
   +  An expression can't contain floating point types, non-groupable types,
      constants, or window functions.
   +  If this optional clause isn't used, all rows in the input table
      comprise a single partition.
+  `ORDER BY`: Defines how rows are ordered within a partition.

   This clause is optional in most situations, but is required in some
   cases for [navigation functions][navigation-functions-reference].
+  [`window_frame_clause`][window-frame-clause-def]: For aggregate analytic
   functions, defines the window frame within the current partition.
   The window frame determines what to include in the window.
   If this clause is used, `ORDER BY` is required except for fully
   unbounded windows.

**Notes**

If neither the `ORDER BY` clause nor window frame clause are present,
the window frame includes all rows in that partition.

For aggregate analytic functions, if the `ORDER BY` clause is present but
the window frame clause isn't, the following window frame clause is
used by default:

```googlesql
RANGE BETWEEN UNBOUNDED PRECEDING AND CURRENT ROW
```

For example, the following queries are equivalent:

```googlesql
SELECT book, LAST_VALUE(book)
  OVER (ORDER BY year)
FROM Library
```

```googlesql
SELECT book, LAST_VALUE(book)
  OVER (
    ORDER BY year
    RANGE BETWEEN UNBOUNDED PRECEDING AND CURRENT ROW)
FROM Library
```

[Hints][window-functions-link-to-hints] are supported on the `PARTITION BY`
clause and the `ORDER BY` clause.

<a id="named_window_rules"></a>
**Rules for using a named window in the window specification**

If you use a named window in your window specifications, these rules apply:

+  The specifications in the named window can be extended
   with new specifications that you define in the window specification clause.
+  You can't have redundant definitions. If you have an `ORDER BY` clause
   in the named window and the window specification clause, an
   error is thrown.
+  The order of clauses matters. `PARTITION BY` must come first,
   followed by `ORDER BY` and `window_frame_clause`. If you add a named window,
   its window specifications are processed first.

   ```googlesql
   --this works:
   SELECT item, purchases, LAST_VALUE(item)
     OVER (ItemWindow ROWS BETWEEN 2 PRECEDING AND 2 FOLLOWING) AS most_popular
   FROM Produce
   WINDOW ItemWindow AS (ORDER BY purchases)

   --this doesn't work:
   SELECT item, purchases, LAST_VALUE(item)
     OVER (ItemWindow ORDER BY purchases) AS most_popular
   FROM Produce
   WINDOW ItemWindow AS (ROWS BETWEEN 2 PRECEDING AND 2 FOLLOWING)
   ```
+  A named window and `PARTITION BY` can't appear together in the
   window specification. If you need `PARTITION BY`, add it to the named window.
+  You can't refer to a named window in an `ORDER BY` clause, an outer query,
   or any subquery.

**Examples using the window specification**

These queries define partitions in a window function:

+  [Compute a subtotal][window-functions-compute-subtotal]
+  [Compute a cumulative sum][window-functions-compute-cumulative-sum]
+  [Get the most popular item in each category][window-functions-get-popular-item]
+  [Get the last value in a range][window-functions-get-last-value-range]
+  [Compute rank][window-functions-compute-rank]
+  [Use a named window in a window frame clause][window-functions-use-named-window]

These queries include a named window in a window specification:

+  [Get the last value in a range][window-functions-get-last-value-range]
+  [Use a named window in a window frame clause][window-functions-use-named-window]

These queries define how rows are ordered in a partition:

+  [Compute a subtotal][window-functions-compute-subtotal]
+  [Compute a cumulative sum][window-functions-compute-cumulative-sum]
+  [Compute a moving average][window-functions-compute-moving-avg]
+  [Compute the number of items within a range][window-functions-compute-item-range]
+  [Get the most popular item in each category][window-functions-get-popular-item]
+  [Get the last value in a range][window-functions-get-last-value-range]
+  [Compute rank][window-functions-compute-rank]
+  [Use a named window in a window frame clause][window-functions-use-named-window]

### Defining the window frame clause 
<a id="def_window_frame"></a>

```googlesql
window_frame_clause:
  { rows_range } { frame_start | frame_between }

rows_range:
  { ROWS | RANGE }

frame_between:
  {
    BETWEEN  unbounded_preceding AND frame_end_a
    | BETWEEN numeric_preceding AND frame_end_a
    | BETWEEN current_row AND frame_end_b
    | BETWEEN numeric_following AND frame_end_c
  }

frame_start:
  { unbounded_preceding | numeric_preceding | [ current_row ] }

frame_end_a:
  { numeric_preceding | current_row | numeric_following | unbounded_following }

frame_end_b:
  { current_row | numeric_following | unbounded_following }

frame_end_c:
  { numeric_following | unbounded_following }

unbounded_preceding:
  UNBOUNDED PRECEDING

numeric_preceding:
  numeric_expression PRECEDING

unbounded_following:
  UNBOUNDED FOLLOWING

numeric_following:
  numeric_expression FOLLOWING

current_row:
  CURRENT ROW
```

The window frame clause defines the window frame around the current row within
a partition, over which the window function is evaluated.
Only aggregate analytic functions can use a window frame clause.

+  `rows_range`: A clause that defines a window frame with physical rows
   or a logical range.
   +  `ROWS`: Computes the window frame based on physical offsets from the
      current row. For example, you could include two rows before and after
      the current row.
   +  `RANGE`: Computes the window frame based on a logical range of rows
      around the current row, based on the current row’s `ORDER BY` key value.
      The provided range value is added or subtracted to the current row's
      key value to define a starting or ending range boundary for the
      window frame. In a range-based window frame, there must be exactly one
      expression in the `ORDER BY` clause, and the expression must have a
      numeric type.

  Tip: If you want to use a range with a date, use `ORDER BY` with the
  `UNIX_DATE()` function. If you want to use a range with a timestamp,
  use the `UNIX_SECONDS()`, `UNIX_MILLIS()`, or `UNIX_MICROS()` function.
+  `frame_between`: Creates a window frame with a lower and upper boundary.
    The first boundary represents the lower boundary. The second boundary
    represents the upper boundary. Only certain boundary combinations can be
    used, as show in the preceding syntax.
    +  Define the beginning of the window frame with `unbounded_preceding`,
       `numeric_preceding`, `numeric_following`, or `current_row`.
       +  `unbounded_preceding`: The window frame starts at the beginning of the
          partition.
       +  `numeric_preceding` or `numeric_following`: The start of the window
          frame is relative to the
          current row.
       +  `current_row`: The window frame starts at the current row.
    +  Define the end of the window frame with `numeric_preceding`,
       `numeric_following`, `current_row`, or `unbounded_following`.
        + `numeric_preceding` or `numeric_following`: The end of the window
          frame is relative to the current row.
        + `current_row`: The window frame ends at the current row.
        + `unbounded_following`: The window frame ends at the end of the
          partition.
+  `frame_start`: Creates a window frame with a lower boundary.
    The window frame ends at the current row.
    +  `unbounded_preceding`: The window frame starts at the beginning of the
        partition.
    +  `numeric_preceding`: The start of the window frame is relative to the
       current row.
    +  `current_row`: The window frame starts at the current row.
+  `numeric_expression`: An expression that represents a numeric type.
   The numeric expression must be a constant, non-negative integer
   or parameter.

**Notes**

If a boundary extends beyond the beginning or end of a partition,
the window frame will only include rows from within that partition.

You can't use a window frame clause with some
[navigation functions][navigation-functions-reference] and
[numbering functions][numbering-functions-reference],
such as `RANK()`.

**Examples using the window frame clause**

These queries compute values with `ROWS`:

+  [Compute a cumulative sum][window-functions-compute-cumulative-sum]
+  [Compute a moving average][window-functions-compute-moving-avg]
+  [Get the most popular item in each category][window-functions-get-popular-item]
+  [Get the last value in a range][window-functions-get-last-value-range]
+  [Use a named window in a window frame clause][window-functions-use-named-window]

These queries compute values with `RANGE`:

+  [Compute the number of items within a range][window-functions-compute-item-range]

These queries compute values with a partially or fully unbound window:

+  [Compute a grand total][window-functions-compute-grand-total]
+  [Compute a subtotal][window-functions-compute-subtotal]
+  [Compute a cumulative sum][window-functions-compute-cumulative-sum]
+  [Get the most popular item in each category][window-functions-get-popular-item]
+  [Compute rank][window-functions-compute-rank]

These queries compute values with numeric boundaries:

+  [Compute a cumulative sum][window-functions-compute-cumulative-sum]
+  [Compute a moving average][window-functions-compute-moving-avg]
+  [Compute the number of items within a range][window-functions-compute-item-range]
+  [Get the last value in a range][window-functions-get-last-value-range]
+  [Use a named window in a window frame clause][window-functions-use-named-window]

These queries compute values with the current row as a boundary:

+  [Compute a grand total][window-functions-compute-grand-total]
+  [Compute a subtotal][window-functions-compute-subtotal]
+  [Compute a cumulative sum][window-functions-compute-cumulative-sum]

### Referencing a named window 
<a id="ref_named_window"></a>

```googlesql
SELECT query_expr,
  function_name ( [ argument_list ] ) OVER over_clause
FROM from_item
WINDOW named_window_expression [, ...]

over_clause:
  { named_window | ( [ window_specification ] ) }

window_specification:
  [ named_window ]
  [ PARTITION BY partition_expression [, ...] ]
  [ ORDER BY expression [ { ASC | DESC } ] [, ...] ]
  [ window_frame_clause ]

named_window_expression:
  named_window AS { named_window | ( [ window_specification ] ) }
```

A named window represents a group of rows in a table upon which to use an
window function. A named window is defined in the
[`WINDOW` clause][window-functions-link-to-window], and referenced in
a window function's [`OVER` clause][over-clause-def].
In an `OVER` clause, a named window can appear either by itself or embedded
within a [window specification][window-specs-def].

**Examples**

+  [Get the last value in a range][window-functions-get-last-value-range]
+  [Use a named window in a window frame clause][window-functions-use-named-window]

## Filtering results with the QUALIFY clause 
<a id="filter_window_results"></a>

The `QUALIFY` clause can be used to filter the results of a window function.
For more information and examples, see the
[`QUALIFY` clause][window-functions-link-to-qualify].

## Window function examples

In these examples, the ==highlighted item== is the current row. The **bolded
items** are the rows that are included in the analysis.

### Common tables used in examples

The following tables are used in the subsequent aggregate analytic
query examples: [`Produce`][produce-table], [`Employees`][employees-table],
and [`Farm`][farm-table].

#### Produce table

Some examples reference a table called `Produce`:

```googlesql
WITH Produce AS
 (SELECT 'kale' as item, 23 as purchases, 'vegetable' as category
  UNION ALL SELECT 'banana', 2, 'fruit'
  UNION ALL SELECT 'cabbage', 9, 'vegetable'
  UNION ALL SELECT 'apple', 8, 'fruit'
  UNION ALL SELECT 'leek', 2, 'vegetable'
  UNION ALL SELECT 'lettuce', 10, 'vegetable')
SELECT * FROM Produce

/*-------------------------------------+
 | item      | category   | purchases  |
 +-------------------------------------+
 | kale      | vegetable  | 23         |
 | banana    | fruit      | 2          |
 | cabbage   | vegetable  | 9          |
 | apple     | fruit      | 8          |
 | leek      | vegetable  | 2          |
 | lettuce   | vegetable  | 10         |
 +-------------------------------------*/
```

#### Employees table

Some examples reference a table called `Employees`:

```googlesql
WITH Employees AS
 (SELECT 'Isabella' as name, 2 as department, DATE(1997, 09, 28) as start_date
  UNION ALL SELECT 'Anthony', 1, DATE(1995, 11, 29)
  UNION ALL SELECT 'Daniel', 2, DATE(2004, 06, 24)
  UNION ALL SELECT 'Andrew', 1, DATE(1999, 01, 23)
  UNION ALL SELECT 'Jacob', 1, DATE(1990, 07, 11)
  UNION ALL SELECT 'Jose', 2, DATE(2013, 03, 17))
SELECT * FROM Employees

/*-------------------------------------+
 | name      | department | start_date |
 +-------------------------------------+
 | Isabella  | 2          | 1997-09-28 |
 | Anthony   | 1          | 1995-11-29 |
 | Daniel    | 2          | 2004-06-24 |
 | Andrew    | 1          | 1999-01-23 |
 | Jacob     | 1          | 1990-07-11 |
 | Jose      | 2          | 2013-03-17 |
 +-------------------------------------*/
```

#### Farm table

Some examples reference a table called `Farm`:

```googlesql
WITH Farm AS
 (SELECT 'cat' as animal, 23 as population, 'mammal' as category
  UNION ALL SELECT 'duck', 3, 'bird'
  UNION ALL SELECT 'dog', 2, 'mammal'
  UNION ALL SELECT 'goose', 1, 'bird'
  UNION ALL SELECT 'ox', 2, 'mammal'
  UNION ALL SELECT 'goat', 2, 'mammal')
SELECT * FROM Farm

/*-------------------------------------+
 | animal    | category   | population |
 +-------------------------------------+
 | cat       | mammal     | 23         |
 | duck      | bird       | 3          |
 | dog       | mammal     | 2          |
 | goose     | bird       | 1          |
 | ox        | mammal     | 2          |
 | goat      | mammal     | 2          |
 +-------------------------------------*/
```

### Compute a grand total

This computes a grand total for all items in the
[`Produce`][produce-table] table.

+  (**==banana==**, **apple**, **leek**, **cabbage**, **lettuce**, **kale**) = 54 total purchases
+  (**banana**, **==apple==**, **leek**, **cabbage**, **lettuce**, **kale**) = 54 total purchases
+  (**banana**, **apple**, **==leek==**, **cabbage**, **lettuce**, **kale**) = 54 total purchases
+  (**banana**, **apple**, **leek**, **==cabbage==**, **lettuce**, **kale**) = 54 total purchases
+  (**banana**, **apple**, **leek**, **cabbage**, **==lettuce==**, **kale**) = 54 total purchases
+  (**banana**, **apple**, **leek**, **cabbage**, **lettuce**, **==kale==**) = 54 total purchases

```googlesql
SELECT item, purchases, category, SUM(purchases)
  OVER () AS total_purchases
FROM Produce

/*-------------------------------------------------------+
 | item      | purchases  | category   | total_purchases |
 +-------------------------------------------------------+
 | banana    | 2          | fruit      | 54              |
 | leek      | 2          | vegetable  | 54              |
 | apple     | 8          | fruit      | 54              |
 | cabbage   | 9          | vegetable  | 54              |
 | lettuce   | 10         | vegetable  | 54              |
 | kale      | 23         | vegetable  | 54              |
 +-------------------------------------------------------*/
```

### Compute a subtotal

This computes a subtotal for each category in the
[`Produce`][produce-table] table.

+  fruit
   +  (**==banana==**, **apple**) = 10 total purchases
   +  (**banana**, **==apple==**) = 10 total purchases
+  vegetable
   +  (**==leek==**, **cabbage**, **lettuce**, **kale**) = 44 total purchases
   +  (**leek**, **==cabbage==**, **lettuce**, **kale**) = 44 total purchases
   +  (**leek**, **cabbage**, **==lettuce==**, **kale**) = 44 total purchases
   +  (**leek**, **cabbage**, **lettuce**, **==kale==**) = 44 total purchases

```googlesql
SELECT item, purchases, category, SUM(purchases)
  OVER (
    PARTITION BY category
    ORDER BY purchases
    ROWS BETWEEN UNBOUNDED PRECEDING AND UNBOUNDED FOLLOWING
  ) AS total_purchases
FROM Produce

/*-------------------------------------------------------+
 | item      | purchases  | category   | total_purchases |
 +-------------------------------------------------------+
 | banana    | 2          | fruit      | 10              |
 | apple     | 8          | fruit      | 10              |
 | leek      | 2          | vegetable  | 44              |
 | cabbage   | 9          | vegetable  | 44              |
 | lettuce   | 10         | vegetable  | 44              |
 | kale      | 23         | vegetable  | 44              |
 +-------------------------------------------------------*/
```

### Compute a cumulative sum

This computes a cumulative sum for each category in the
[`Produce`][produce-table] table. The sum is computed with respect to the
order defined using the `ORDER BY` clause.

+  fruit
   +  (**==banana==**, apple) = 2 total purchases
   +  (**banana**, **==apple==**) = 10 total purchases
+  vegetable
   +  (**==leek==**, cabbage, lettuce, kale) = 2 total purchases
   +  (**leek**, **==cabbage==**, lettuce, kale) = 11 total purchases
   +  (**leek**, **cabbage**, **==lettuce==**, kale) = 21 total purchases
   +  (**leek**, **cabbage**, **lettuce**, **==kale==**) = 44 total purchases

```googlesql
SELECT item, purchases, category, SUM(purchases)
  OVER (
    PARTITION BY category
    ORDER BY purchases
    ROWS BETWEEN UNBOUNDED PRECEDING AND CURRENT ROW
  ) AS total_purchases
FROM Produce

/*-------------------------------------------------------+
 | item      | purchases  | category   | total_purchases |
 +-------------------------------------------------------+
 | banana    | 2          | fruit      | 2               |
 | apple     | 8          | fruit      | 10              |
 | leek      | 2          | vegetable  | 2               |
 | cabbage   | 9          | vegetable  | 11              |
 | lettuce   | 10         | vegetable  | 21              |
 | kale      | 23         | vegetable  | 44              |
 +-------------------------------------------------------*/
```

This does the same thing as the preceding example. You don't have to add
`CURRENT ROW` as a boundary unless you would like to for readability.

```googlesql
SELECT item, purchases, category, SUM(purchases)
  OVER (
    PARTITION BY category
    ORDER BY purchases
    ROWS UNBOUNDED PRECEDING
  ) AS total_purchases
FROM Produce
```

In this example, all items in the [`Produce`][produce-table] table are included
in the partition. Only preceding rows are analyzed. The analysis starts two
rows prior to the current row in the partition.

+  (==banana==, leek, apple, cabbage, lettuce, kale) = NULL
+  (banana, ==leek==, apple, cabbage, lettuce, kale) = NULL
+  (**banana**, leek, ==apple==, cabbage, lettuce, kale) = 2
+  (**banana**, **leek**, apple, ==cabbage==, lettuce, kale) = 4
+  (**banana**, **leek**, **apple**, cabbage, ==lettuce==, kale) = 12
+  (**banana**, **leek**, **apple**, **cabbage**, lettuce, ==kale==) = 21

```googlesql
SELECT item, purchases, category, SUM(purchases)
  OVER (
    ORDER BY purchases
    ROWS BETWEEN UNBOUNDED PRECEDING AND 2 PRECEDING
  ) AS total_purchases
FROM Produce;

/*-------------------------------------------------------+
 | item      | purchases  | category   | total_purchases |
 +-------------------------------------------------------+
 | banana    | 2          | fruit      | NULL            |
 | leek      | 2          | vegetable  | NULL            |
 | apple     | 8          | fruit      | 2               |
 | cabbage   | 9          | vegetable  | 4               |
 | lettuce   | 10         | vegetable  | 12              |
 | kale      | 23         | vegetable  | 21              |
 +-------------------------------------------------------*/
```

### Compute a moving average

This computes a moving average in the [`Produce`][produce-table] table.
The lower boundary is 1 row before the
current row. The upper boundary is 1 row after the current row.

+  (**==banana==**, **leek**, apple, cabbage, lettuce, kale) = 2 average purchases
+  (**banana**, **==leek==**, **apple**, cabbage, lettuce, kale) = 4 average purchases
+  (banana, **leek**, **==apple==**, **cabbage**, lettuce, kale) = 6.3333 average purchases
+  (banana, leek, **apple**, **==cabbage==**, **lettuce**, kale) = 9 average purchases
+  (banana, leek, apple, **cabbage**, **==lettuce==**, **kale**) = 14 average purchases
+  (banana, leek, apple, cabbage, **lettuce**, **==kale==**) = 16.5 average purchases

```googlesql
SELECT item, purchases, category, AVG(purchases)
  OVER (
    ORDER BY purchases
    ROWS BETWEEN 1 PRECEDING AND 1 FOLLOWING
  ) AS avg_purchases
FROM Produce

/*-------------------------------------------------------+
 | item      | purchases  | category   | avg_purchases   |
 +-------------------------------------------------------+
 | banana    | 2          | fruit      | 2               |
 | leek      | 2          | vegetable  | 4               |
 | apple     | 8          | fruit      | 6.33333         |
 | cabbage   | 9          | vegetable  | 9               |
 | lettuce   | 10         | vegetable  | 14              |
 | kale      | 23         | vegetable  | 16.5            |
 +-------------------------------------------------------*/
```

### Compute the number of items within a range

This example gets the number of animals that have a similar population
count in the [`Farm`][farm-table] table.

+  (**==goose==**, **dog**, **ox**, **goat**, duck, cat) = 4 animals between population range 0-2.
+  (**goose**, **==dog==**, **ox**, **goat**, **duck**, cat) = 5 animals between population range 1-3.
+  (**goose**, **dog**, **==ox==**, **goat**, **duck**, cat) = 5 animals between population range 1-3.
+  (**goose**, **dog**, **ox**, **==goat==**, **duck**, cat) = 5 animals between population range 1-3.
+  (goose, **dog**, **ox**, **goat**, **==duck==**, cat) = 4 animals between population range 2-4.
+  (goose, dog, ox, goat, duck, **==cat==**) = 1 animal between population range 22-24.

```googlesql
SELECT animal, population, category, COUNT(*)
  OVER (
    ORDER BY population
    RANGE BETWEEN 1 PRECEDING AND 1 FOLLOWING
  ) AS similar_population
FROM Farm;

/*----------------------------------------------------------+
 | animal    | population | category   | similar_population |
 +----------------------------------------------------------+
 | goose     | 1          | bird       | 4                  |
 | dog       | 2          | mammal     | 5                  |
 | ox        | 2          | mammal     | 5                  |
 | goat      | 2          | mammal     | 5                  |
 | duck      | 3          | bird       | 4                  |
 | cat       | 23         | mammal     | 1                  |
 +----------------------------------------------------------*/
```

### Get the most popular item in each category

This example gets the most popular item in each category. It defines how rows
in a window are partitioned and ordered in each partition. The
[`Produce`][produce-table] table is referenced.

+  fruit
   +  (**==banana==**, **apple**) = apple is most popular
   +  (**banana**, **==apple==**) = apple is most popular
+  vegetable
   +  (**==leek==**, **cabbage**, **lettuce**, **kale**) = kale is most popular
   +  (**leek**, **==cabbage==**, **lettuce**, **kale**) = kale is most popular
   +  (**leek**, **cabbage**, **==lettuce==**, **kale**) = kale is most popular
   +  (**leek**, **cabbage**, **lettuce**, **==kale==**) = kale is most popular

```googlesql
SELECT item, purchases, category, LAST_VALUE(item)
  OVER (
    PARTITION BY category
    ORDER BY purchases
    ROWS BETWEEN UNBOUNDED PRECEDING AND UNBOUNDED FOLLOWING
  ) AS most_popular
FROM Produce

/*----------------------------------------------------+
 | item      | purchases  | category   | most_popular |
 +----------------------------------------------------+
 | banana    | 2          | fruit      | apple        |
 | apple     | 8          | fruit      | apple        |
 | leek      | 2          | vegetable  | kale         |
 | cabbage   | 9          | vegetable  | kale         |
 | lettuce   | 10         | vegetable  | kale         |
 | kale      | 23         | vegetable  | kale         |
 +----------------------------------------------------*/
```

### Get the last value in a range

This example gets the most popular item in a specific window frame, using
the [`Produce`][produce-table] table. The window frame analyzes up to three
rows at a time. Take a close look at the `most_popular` column for vegetables.
Instead of getting the most popular item in a specific category, it gets the
most popular item in a specific range in that category.

+  fruit
   +  (**==banana==**, **apple**) = apple is most popular
   +  (**banana**, **==apple==**) = apple is most popular
+  vegetable
   +  (**==leek==**, **cabbage**, lettuce, kale) = cabbage is most popular
   +  (**leek**, **==cabbage==**, **lettuce**, kale) = lettuce is most popular
   +  (leek, **cabbage**, **==lettuce==**, **kale**) = kale is most popular
   +  (leek, cabbage, **lettuce**, **==kale==**) = kale is most popular

```googlesql
SELECT item, purchases, category, LAST_VALUE(item)
  OVER (
    PARTITION BY category
    ORDER BY purchases
    ROWS BETWEEN 1 PRECEDING AND 1 FOLLOWING
  ) AS most_popular
FROM Produce

/*----------------------------------------------------+
 | item      | purchases  | category   | most_popular |
 +----------------------------------------------------+
 | banana    | 2          | fruit      | apple        |
 | apple     | 8          | fruit      | apple        |
 | leek      | 2          | vegetable  | cabbage      |
 | cabbage   | 9          | vegetable  | lettuce      |
 | lettuce   | 10         | vegetable  | kale         |
 | kale      | 23         | vegetable  | kale         |
 +----------------------------------------------------*/
```

This example returns the same results as the preceding example, but it includes
a named window called `ItemWindow`. Some of the window specifications are
defined directly in the `OVER` clause and some are defined in the named window.

```googlesql
SELECT item, purchases, category, LAST_VALUE(item)
  OVER (
    ItemWindow
    ROWS BETWEEN 1 PRECEDING AND 1 FOLLOWING
  ) AS most_popular
FROM Produce
WINDOW ItemWindow AS (
  PARTITION BY category
  ORDER BY purchases)
```

### Compute rank

This example calculates the rank of each employee within their department,
based on their start date. The window specification is defined directly
in the `OVER` clause. The [`Employees`][employees-table] table is referenced.

+  department 1
   +  (**==Jacob==**, **Anthony**, **Andrew**) = Assign rank 1 to Jacob
   +  (**Jacob**, **==Anthony==**, **Andrew**) = Assign rank 2 to Anthony
   +  (**Jacob**, **Anthony**, **==Andrew==**) = Assign rank 3 to Andrew
+  department 2
   +  (**==Isabella==**, **Daniel**, **Jose**) = Assign rank 1 to Isabella
   +  (**Isabella**, **==Daniel==**, **Jose**) = Assign rank 2 to Daniel
   +  (**Isabella**, **Daniel**, **==Jose==**) = Assign rank 3 to Jose

```googlesql
SELECT name, department, start_date,
  RANK() OVER (PARTITION BY department ORDER BY start_date) AS rank
FROM Employees;

/*--------------------------------------------+
 | name      | department | start_date | rank |
 +--------------------------------------------+
 | Jacob     | 1          | 1990-07-11 | 1    |
 | Anthony   | 1          | 1995-11-29 | 2    |
 | Andrew    | 1          | 1999-01-23 | 3    |
 | Isabella  | 2          | 1997-09-28 | 1    |
 | Daniel    | 2          | 2004-06-24 | 2    |
 | Jose      | 2          | 2013-03-17 | 3    |
 +--------------------------------------------*/
```

### Use a named window in a window frame clause 
<a id="def_use_named_window"></a>

You can define some of your logic in a named window and some of it in a
window frame clause. This logic is combined. Here is an example, using the
[`Produce`][produce-table] table.

```googlesql
SELECT item, purchases, category, LAST_VALUE(item)
  OVER (ItemWindow) AS most_popular
FROM Produce
WINDOW ItemWindow AS (
  PARTITION BY category
  ORDER BY purchases
  ROWS BETWEEN 2 PRECEDING AND 2 FOLLOWING)

/*-------------------------------------------------------+
 | item      | purchases  | category   | most_popular    |
 +-------------------------------------------------------+
 | banana    | 2          | fruit      | apple           |
 | apple     | 8          | fruit      | apple           |
 | leek      | 2          | vegetable  | lettuce         |
 | cabbage   | 9          | vegetable  | kale            |
 | lettuce   | 10         | vegetable  | kale            |
 | kale      | 23         | vegetable  | kale            |
 +-------------------------------------------------------*/
```

You can also get the previous results with these examples:

```googlesql
SELECT item, purchases, category, LAST_VALUE(item)
  OVER (ItemWindow) AS most_popular
FROM Produce
WINDOW
  a AS (PARTITION BY category),
  b AS (a ORDER BY purchases),
  c AS (b ROWS BETWEEN 2 PRECEDING AND 2 FOLLOWING),
  ItemWindow AS (c)
```

```googlesql
SELECT item, purchases, category, LAST_VALUE(item)
  OVER (ItemWindow ROWS BETWEEN 2 PRECEDING AND 2 FOLLOWING) AS most_popular
FROM Produce
WINDOW
  a AS (PARTITION BY category),
  b AS (a ORDER BY purchases),
  ItemWindow AS (b)
```

The following example produces an error because a window frame clause has been
defined twice:

```googlesql {.bad}
SELECT item, purchases, category, LAST_VALUE(item)
  OVER (
    ItemWindow
    ROWS BETWEEN 1 PRECEDING AND 1 FOLLOWING
    ) AS most_popular
FROM Produce
WINDOW ItemWindow AS (
  ORDER BY purchases
  ROWS BETWEEN 2 PRECEDING AND 2 FOLLOWING)
```

<!-- mdlint off(WHITESPACE_LINE_LENGTH) -->

[named-window-rules]: #named_window_rules

[over-clause-def]: #def_over_clause

[window-specs-def]: #def_window_spec

[window-frame-clause-def]: #def_window_frame

[named-windows]: #ref_named_window

[produce-table]: #produce_table

[farm-table]: #farm_table

[employees-table]: #employees_table

[window-functions-compute-grand-total]: #compute_a_grand_total

[window-functions-compute-subtotal]: #compute_a_subtotal

[window-functions-compute-cumulative-sum]: #compute_a_cumulative_sum

[window-functions-compute-moving-avg]: #compute_a_moving_average

[window-functions-compute-item-range]: #compute_the_number_of_items_within_a_range

[window-functions-get-popular-item]: #get_the_most_popular_item_in_each_category

[window-functions-get-last-value-range]: #get_the_last_value_in_a_range

[window-functions-compute-rank]: #compute_rank

[window-functions-use-named-window]: #def_use_named_window

[window-functions-link-to-window]: https://github.com/google/googlesql/blob/master/docs/query-syntax.md#window_clause

[window-functions-link-to-qualify]: https://github.com/google/googlesql/blob/master/docs/query-syntax.md#qualify_clause

[window-functions-link-to-hints]: https://github.com/google/googlesql/blob/master/docs/lexical.md#hints

[navigation-functions-reference]: https://github.com/google/googlesql/blob/master/docs/navigation_functions.md

[numbering-functions-reference]: https://github.com/google/googlesql/blob/master/docs/numbering_functions.md

<!-- mdlint on -->

