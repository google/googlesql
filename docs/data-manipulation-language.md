

<!-- mdlint off(WHITESPACE_LINE_LENGTH) -->

# Data manipulation language

GoogleSQL supports the following statements for manipulating data:

+ `INSERT`
+ `UPDATE`
+ `DELETE`
+ `MERGE`

## Example data 
<a id="example_data"></a>

The sections in this topic use the following table schemas.

### Singers table 
<a id="singers_table"></a>

<table>
<thead>
<tr>
<th>Column Name</th>
<th>Data Type</th>
<th>Default Value</th>
</tr>
</thead>
<tbody>
<tr>
<td>SingerId</td>
<td><code>INT64 NOT NULL</code></td>
<td><code>&lt;auto-increment&gt;</code></td>
</tr>
<tr>
<td>FirstName</td>
<td><code>STRING</code></td>
<td>&nbsp;</td>
</tr>
<tr>
<td>LastName</td>
<td><code>STRING</code></td>
<td>&nbsp;</td>
</tr>
<tr>
<td>BirthDate</td>
<td><code>DATE</code></td>
<td>&nbsp;</td>
</tr>
<tr>
<td>Status</td>
<td><code>STRING</code></td>
<td><code>"active"</code></td>
</tr>
<tr>
<td>SingerInfo</td>
<td><code>PROTO&lt;SingerMetadata&gt;</code></td>
<td>&nbsp;</td>
</tr>
<tr>
<td>Albums</td>
<td><code>PROTO&lt;Album&gt;</code></td>
<td>&nbsp;</td>
</tr>
<tr>

</tbody>
</table>

The proto, `SingerMetadata`, has the following definition:

<pre>
message SingerMetadata {
  optional string    nationality = 1;
  repeated Residence residence   = 2;<br/>
  message Residence {
    required int64  start_year   = 1;
    optional int64  end_year     = 2;
    optional string city         = 3;
    optional string country      = 4 [default = "USA"];
  }
}
</pre>

The proto, `Album`, has the following definition:

<pre>
message Album {
  optional string title = 1;
  optional int32 tracks = 2;
  repeated string comments = 3;
  repeated Song song = 4;<br/>
  message Song {
    optional string songtitle = 1;
    optional int32 length = 2;
    repeated Chart chart = 3;<br/>
    message Chart {
      optional string chartname = 1;
      optional int32 rank = 2;
    }
  }
}
</pre>

### Concerts table 
<a id="concerts_table"></a>

<table>
<thead>
<tr>
<th>Column Name</th>
<th>Data Type</th>
<th>Default Value</th>
</tr>
</thead>
<tbody>
<tr>
<td>VenueId</td>
<td><code>INT64 NOT NULL</code></td>
<td><code>&lt;auto-increment&gt;</code></td>
</tr>
<tr>
<td>SingerId</td>
<td><code>INT64 NOT NULL</code></td>
<td>&nbsp;</td>
</tr>
<tr>
<td>ConcertDate</td>
<td><code>DATE NOT NULL</code></td>
<td>&nbsp;</td>
</tr>
<tr>
<td>BeginTime</td>
<td><code>TIMESTAMP</code></td>
<td>&nbsp;</td>
</tr>
<tr>
<td>EndTime</td>
<td><code>TIMESTAMP</code></td>
<td>&nbsp;</td>
</tr>
<tr>
<td>TicketPrices</td>
<td><code>ARRAY&lt;INT64&gt;</code></td>
<td>&nbsp;</td>
</tr>
</tbody>
</table>

### Inventory table

<table>
<thead>
<tr>
<th>Column Name</th>
<th>Data Type</th>
<th>Default Value</th>
</tr>
</thead>
<tbody>
<tr>
<td>product</td>
<td><code>STRING NOT NULL</code></td>
<td>&nbsp;</td>
</tr>
<tr>
<td>quantity</td>
<td><code>INT64 NOT NULL</code></td>
<td>0</td>
</tr>
<tr>
<td>supply_constrained</td>
<td><code>BOOL</code></td>
<td>false</td>
</tr>
</tbody>
</table>

### NewArrivals table

<table>
<thead>
<tr>
<th>Column Name</th>
<th>Data Type</th>
<th>Default Value</th>
</tr>
</thead>
<tbody>
<tr>
<td>product</td>
<td><code>STRING NOT NULL</code></td>
<td>&nbsp;</td>
</tr>
<tr>
<td>quantity</td>
<td><code>INT64 NOT NULL</code></td>
<td>0</td>
</tr>
<tr>
<td>warehouse</td>
<td><code>STRING NOT NULL</code></td>
<td>&nbsp;</td>
</tr>
</tbody>
</table>

## `INSERT` statement 
<a id="insert_statement"></a>

Use the `INSERT` statement when you want to add new rows to a table.

<pre>
INSERT [[OR] IGNORE | REPLACE | UPDATE] [INTO] target_name
 (column[, ...])
 input [ASSERT_ROWS_MODIFIED m] [ then_return_clause ]

input:
  {
    VALUES (column_values) [, ...]
    | SELECT_QUERY
  }

column_values:
  { value_expression | DEFAULT } [, ...]

then_return_clause:
  THEN RETURN
  [ WITH ACTION [ AS alias ]  ]
  { value_expression | expression [  [ AS ] alias ] } [, ...]

value_expression:
  [expression.]* [ except_clause ] [ replace_clause ]

except_clause:
  EXCEPT ( column_name [, ...] )

replace_clause:
  REPLACE ( expression [ AS ] column_name [, ...] )

</pre>

<a id="statement-rules"></a>
`INSERT` statements must comply with the following rules:

+ Column names must be specified.
+ Duplicate names aren't allowed in the list of target columns.
+ Values must be added in the same order as the specified columns.
+ The number of values added must match the number of specified columns.
+ Values must have a type that's [compatible][compatible-types] with the
  target column.
+ All non-null columns must appear in the column list, and have a non-null value
  specified.

Statements that don't follow these rules will raise an error.

The following statement inserts a new row to the
[Singers example table][singers-table]:

```
INSERT INTO Singers
    (SingerId, FirstName, LastName, BirthDate, Status, SingerInfo)
VALUES (5, "Catalina", "Smith", "1990-08-17", "active", "nationality:'U.S.A.'");
```

This next statement inserts multiple rows into the table:

```
INSERT INTO Singers
    (SingerId, FirstName, LastName, BirthDate, Status, SingerInfo)
VALUES (6, "Zak", "Sterling", "1996-03-12", "active", "nationality:'U.S.A.'"),
       (7, "Edie", "Silver", "1998-01-23", "active", "nationality:'U.S.A.'");
```

You can construct `INSERT` statements to insert the results of a query. For
example, the following statement inserts a new concert for a singer into
the [Concerts table][concerts-table].

```
INSERT INTO Concerts
  (VenueId, SingerId, ConcertDate, BeginTime, EndTime, TicketPrices)
SELECT c.VenueId, c.SingerId, DATE "2015-06-01", c.BeginTime, c.EndTime, c.TicketPrices
  FROM Concerts c WHERE c.SingerId = 1;
```

### Value type compatibility 
<a id="compatible_types"></a>

Values added with an `INSERT` statement must be compatible with the target
column's type. A value's type is considered compatible with the target column's
type if one of the following criteria are met:

+ The value type matches the column type exactly. (For example, inserting a
value of type `INT32` in a column that also has a type of `INT32`.)
+ The value type is one that can be implicitly coerced into another type. See
the [Coercion][coercion] of the
[Expressions, Functions, and Operators][functions-and-operators]
topic for more information.
+ The value can be assigned after applying a narrowing integer or floating point
cast. For example, if you attempt to add a value of `INT64` into a column that
supports `INT32`, GoogleSQL automatically adds a narrowing cast to
`INT32`.

### Default values

If the target table supports a default value for a
column, you can omit the column from the `INSERT` statement. Alternatively, you
can insert the default value explicitly using the `DEFAULT` keyword.

**Note:** If a column doesn't require a value, the default value is typically
`NULL`.

The following example inserts default values for all unspecified columns.
For example, the [Singers table][singers-table] has one column that has a
defined default value&mdash;the `Status` column, which has a default value of
`active`. The `SingerId` column is defined `NOT NULL` and so has no implicit
default value. For all other columns, the default value is `NULL`.

```
INSERT INTO Singers
  (FirstName)
VALUES ("Neil");
```

If this was the first row added to the table, a `SELECT` statement querying
this row would return the following:

<table>
<thead>
<tr>
<th>SingerId</th>
<th>FirstName</th>
<th>LastName</th>
<th>BirthDate</th>
<th>Status</th>
<th>SingerInfo</th>
<th>Albums</th>
</tr>
</thead>
<tbody>
<tr>
<td>1</td>
<td>Neil</td>
<td>NULL</td>
<td>NULL</td>
<td>active</td>
<td>NULL</td>
<td>NULL</td>
</tr>
</tbody>
</table>

If you construct an `INSERT` statement to use a default value for a
column and that column doesn't support a default value, the statement fails.
For example, the `Singers` table contains a column, `SingerId`, which can't be
`NULL`. Consequently, the following statement fails, because the `SingerId`
doesn't support a default value:

```
INSERT INTO Singers
  (SingerId, FirstName, LastName, BirthDate, Status, SingerInfo)
VALUES (DEFAULT, "Andrew", "Duneskipper", "2000-01-15", "active", NULL);
```

### Duplicate rows

By default, an `INSERT` statement fails when it tries to add a duplicate row.
A duplicate row is defined as one for which the primary key or a unique key
already exists in the table.

You can change this behavior by choosing one of the following options for
handling duplicate rows:

+ `INSERT OR IGNORE`: The existing or first row is inserted, and the rest are
  ignored.
+ `INSERT OR REPLACE`: All but the last inserted row are replaced.
+ `INSERT OR UPDATE`: Updates are applied sequentially, ending with the last
  update.

Your statement would resemble the following:

<pre>
INSERT [OR] { IGNORE | REPLACE | UPDATE } <em>&lt;rest of statement&gt;</em>
</pre>

These keywords require that your table includes a primary key. If the table
doesn't have a primary key, the statement fails.

If a table is unordered, the first row, last row, and sequence of rows
are arbitrary.

#### `INSERT OR IGNORE` 
<a id="insert_ignore"></a>

You can instruct your `INSERT` statement to skip any rows that have duplicate
primary keys by using the `IGNORE` keyword.

For example, the following statement instructs GoogleSQL to skip the
row if it already exists in the table:

```
INSERT OR IGNORE INTO Singers
    (FirstName, LastName, BirthDate, Status, SingerInfo)
VALUES ("Catalina", "Smith", "1990-08-17", DEFAULT, "nationality:'U.S.A.'");
```

`INSERT OR IGNORE` ignores only duplicate rows that have the same
primary key. GoogleSQL raises an error if any other constraint
violation occurs, including duplicate keys in unique indexes.

#### `INSERT OR REPLACE` 
<a id="insert_replace"></a>

You can instruct your `INSERT` statement to replace any rows that have duplicate
primary keys by using the `REPLACE` statement. Replaced rows have the same
primary key as the original row.

In the following example, the `REPLACE` keyword is used instead of `IGNORE`.
This time, the duplicate row replaces the existing row in the table.

```
INSERT OR REPLACE INTO Singers
    (FirstName, LastName, BirthDate, Status, SingerInfo)
VALUES ("Zak", "Sterling", "1996-03-12", "active", "nationality:'U.S.A.'");
```

Using `REPLACE` is the same as deleting the existing row and inserting a
replacement one. As a result, any columns that you don't mention in the
replacement row are cleared and replaced with their default values. If you want
your statement to preserve the values of unspecified columns, use
`INSERT OR UPDATE`.

#### `INSERT OR UPDATE` 
<a id="insert_update"></a>

`INSERT OR UPDATE` updates the columns specified
for one or more rows. Columns that aren't listed in the `INSERT` statement
remain unchanged.

The following example illustrates how `INSERT OR UPDATE` changes an
existing row. In this case, the status is changed from `active` to `inactive`.

```
INSERT OR UPDATE INTO Singers
    (SingerId, Status)
VALUES (5, "inactive");
```

Notice that if the row doesn't exist, the previous statement inserts a new row
with values in the specified fields, and all other values set to their
corresponding defaults.

When you use the `UPDATE` keyword, any column that you don't specify remains
unchanged.

### `INSERT` and `ASSERT_ROWS_MODIFIED`

With GoogleSQL, you can confirm how many rows are added each time you
use an `INSERT` statement by using the `ASSERT_ROWS_MODIFIED` keyword.

With the `ASSERT_ROWS_MODIFIED` keyword, you specify the number of rows you
expect the command to insert, and GoogleSQL compares that number
against the number of rows actually modified. This check occurs before
GoogleSQL commits the change to the database. If the row count matches,
GoogleSQL commits the changes. Otherwise, GoogleSQL returns
an error and rolls back the statement.

When calculating the number of rows modified, GoogleSQL includes rows
that were inserted, updated, or replaced. It doesn't count rows that were
skipped through the `IGNORE` keyword. To illustrate this, consider the following
example of a **Singers** table:

<table>
<thead>
<tr>
<th>SingerId</th>
<th>FirstName</th>
<th>LastName</th>
<th>BirthDate</th>
<th>SingerInfo</th>
<th>Albums</th>
</tr>
</thead>
<tbody>
<tr>
<td>5</td>
<td>Alice</td>
<td>Trentor</td>
<td>1991-10-2</td>
<td>"nationality: 'U.S.A.'"</td>
<td>NULL</td>
</tr>
</tbody>
</table>

```
INSERT OR UPDATE INTO Singers
    (SingerId, FirstName, LastName, Birthdate, Status, SingerInfo)
VALUES (5, "Zak", "Sterling", "1996-03-12", "active", "nationality:'U.S.A.'"),
       (6, "Edie", "Silver", "1998-01-23", "active", "nationality:'U.S.A.'")
ASSERT_ROWS_MODIFIED 2;
```

One of the value expressions in the `INSERT` statement adds a new row, while the
second updates an existing one. Both result in a total of 2 modified rows, which
matches the `ASSERT_ROWS_MODIFIED 2` clause of the statement.

The following statement uses `INSERT OR IGNORE` to add rows to the table.

```
INSERT OR IGNORE INTO Singers
    (SingerId, FirstName, LastName, Birthdate, Status, SingerInfo)
VALUES (5, "Zak", "Sterling", "1996-03-12", "active", "nationality:'U.S.A.'"),
       (7, "Edie", "Silver", "1998-01-23", "active", "nationality:'U.S.A.'")
ASSERT_ROWS_MODIFIED 1;
```

In this case, there is a collision when inserting a new row with a `SingerId` of
`5`, because a row with that `SingerId` value already exists. This statement
inserts one row, which matches the `ASSERT_ROWS_MODIFIED 1` statement.

### `INSERT` and `THEN RETURN` 
<a id="dml_then_return_clause"></a>

Use the `THEN RETURN` clause to return the results of the `INSERT` operation and
selected data from the newly inserted rows. This is especially useful for
retrieving values of columns with default values, generated columns, and
auto-generated keys, without issuing additional `SELECT` statements.

`THEN RETURN` supports a returning clause to capture expressions based on newly
inserted rows that include these parts:

+   `WITH ACTION`: An optional clause that adds a `string` column called
    `ACTION` to the result row set. Each value in this column represents the
    type of action that was applied to the column during statement execution.
    Values include `"INSERT"`, `"REPLACE"`,
    and `"UPDATE"`. The `ACTION` column is appended as the last output column.
+   `*`: Returns all columns.
+   `expression.*`: Returns all columns from range variables, like a table or
    proto field. The dot star expression can't be applied on other expressions,
    including field access.
+   `except_clause`: Specifies the columns to exclude from the result. All
    matching column names are omitted from the output.
+   `replace_clause`: Specifies the columns to replace in the result. The column
    that matches the identifier in a `replace_clause` is replaced by the
    expression in that `replace_clause`. A `replace_clause` doesn't change the
    names or order of columns. However, it can change the value and the value
    type of a column.
+   `expression`: Represents a column name of the table specified by
    `target_name` or an expression that uses any combination of such column
    names. Column names are valid if they belong to columns of the table
    being modified. Excluded expressions include aggregate and window
    functions.
+   `alias`: A temporary name for an expression in the query.

The following query inserts three rows into a table, and uses `THEN RETURN` to
fetch these rows and compute a new column called `new_quantity`.

```googlesql
INSERT Inventory (product, quantity)
VALUES ('washer', 20),
       ('dryer', 30),
       ('oven', 5)
THEN RETURN *, quantity * 10 AS new_quantity;

/*---------+----------+--------------------+--------------+
 | product | quantity | supply_constrained | new_quantity |
 +---------+----------+--------------------+--------------+
 | washer  | 20       | false              | 200          |
 | dryer   | 30       | false              | 300          |
 | oven    | 10       | false              | 50           |
 +---------+----------+--------------------+--------------*/
```

The following query tries to insert two rows into a table, but ignores a
duplicated row. It uses `THEN RETURN` to fetch the inserted row and
`WITH ACTION` to show the modified row action type.

```googlesql
INSERT IGNORE Inventory (product, quantity)
VALUES ('desk', 40),
       ('desk', 45)
THEN RETURN WITH ACTION * EXCEPT(supply_constrained);

/*----------+----------+--------+
 | product  | quantity | ACTION |
 +----------+----------+--------+
 | desk     | 40       | INSERT |
 +----------+----------+--------*/
```

The following query tries to insert or update a row into a table. It uses
`THEN RETURN` to fetch the modified row and `WITH ACTION` to show the modified
row action type.

```googlesql
INSERT OR UPDATE Inventory (product, quantity)
VALUES ('oven', 100)
THEN RETURN WITH ACTION product, quantity * 10 AS new_quantity;

/*---------+--------------+--------+
 | product | new_quantity | ACTION |
 +---------+--------------+--------+
 | oven    | 1000         | UPDATE |
 +---------+--------------+--------*/
```

### `INSERT` examples

Add a new row to the `Singers` table.

```
INSERT INTO Singers
    (SingerId, FirstName, LastName, BirthDate, Status, SingerInfo)
VALUES (5, "Catalina", "Smith", "1990-08-17", DEFAULT, "nationality:'U.S.A.'");
```

**RESULT:** New singer, Catalina Smith, added to table.

Try to add a singer, but only if the `SingerId` isn't already in the table.

```
INSERT OR IGNORE INTO Singers
    (SingerId, FirstName, LastName, BirthDate, Status, SingerInfo)
VALUES (5, "Zak", "Sterling", "1996-03-12", "active", "nationality:'U.S.A.'");
```

**RESULT:** Table unaffected. Catalina Smith remains in the table.

Try to add another singer and replace an existing row if it has the same
`SingerId`.

```
INSERT OR REPLACE INTO Singers
    (SingerId, FirstName, LastName, BirthDate, Status, SingerInfo)
VALUES (5, "Zak", "Sterling", "1996-03-12", "active", "nationality:'U.S.A.'");
```

**RESULT:** Singer, Catalina Smith, replaced with Zak Sterling.

Add a singer to the table, or update a singer if they already exist.

```
INSERT OR UPDATE INTO Singers
    (SingerId, FirstName, LastName, Status)
VALUES (5, "Zak", "Sterling", "inactive");
```

**RESULT:** The database updated the existing row `Zak Sterling`. His status is
now `inactive`. All other values, such as `BirthDate`, remain unchanged.

Add a new row to the `Singers` table and return the `DEFAULT` column value.

```googlesql
INSERT INTO Singers (SingerId, FirstName,
LastName, BirthDate, Status, SingerInfo) VALUES (6, "Michael", "Leon",
"1958-08-29", DEFAULT, "nationality:'U.S.A.'") THEN RETURN Status;
```

**RESULT:** The database adds a new row to the `Singer` table and returns the
value `active` of its `Status` column.

## `DELETE` statement

Use the `DELETE` statement when you want to delete rows from a table.

<pre>
DELETE [FROM] target_name
WHERE condition [ASSERT_ROWS_MODIFIED n] [ then_return_clause ]

then_return_clause:
  THEN RETURN
  [ WITH ACTION [ AS alias ]  ]
  { value_expression | expression [  [ AS ] alias ] } [, ...]

value_expression:
  [expression.]* [ except_clause ] [ replace_clause ]

except_clause:
  EXCEPT ( column_name [, ...] )

replace_clause:
  REPLACE ( expression [ AS ] column_name [, ...] )

</pre>

**Note**: `DELETE` statements must comply with all
[statement rules][statement-rules].

### `WHERE` keyword

Each time you construct a `DELETE` statement, you must use the `WHERE` keyword,
followed by a condition. For example:

```
DELETE FROM Singers
WHERE LastName = "Sterling";
```

You can use the identifier for the target of the update as a range variable
within the `WHERE` clause. For example:

```
DELETE FROM Singers s
WHERE s.LastName = "Sterling";
```

Using an identifier can help make your `DELETE` statement more explicit with
regards to what data GoogleSQL should update.

The `WHERE` keyword is mandatory for any `DELETE` statement. However, you can
set the condition for the `WHERE` keyword to be true, which results in the
statement deleting all rows in the table.

```
DELETE FROM Singers s
WHERE true;
```

### `DELETE` and `ASSERT_ROWS_MODIFIED`

With GoogleSQL, you can confirm how many rows are deleted each time you
use a `DELETE` statement. You implement this confirmation through the
`ASSERT_ROWS_MODIFIED` keyword.

```
DELETE FROM Singers
WHERE LastName = "Sterling"
ASSERT_ROWS_MODIFIED 1;
```

With the `ASSERT_ROWS_MODIFIED` keyword, you specify the number of rows you
expect the command to delete, and compare that number against the number of rows
actually deleted. This check occurs before GoogleSQL commits the change
to the database. If the row count matches, GoogleSQL commits the
changes. Otherwise, GoogleSQL returns an error and rolls back the
statement.

### `DELETE` and `THEN RETURN`

With `THEN RETURN`, you can obtain data from rows that are being removed from a
table. To learn more about the values you can use in this clause, see [INSERT
and THEN RETURN][dml-then-return-clause]

Example:

The following query deletes all rows in a table that contains a product called
`washer` and then returns the deleted rows.

```googlesql
DELETE FROM Inventory
WHERE product = 'washer'
THEN RETURN *;

/*---------+----------+--------------------+
 | product | quantity | supply_constrained |
 +---------+----------+--------------------+
 | washer  | 20       | false              |
 +---------+----------+--------------------*/
```

## `UPDATE` statement

Use the `UPDATE` statement when you want to update existing rows within a table.

<pre>
UPDATE target_name
SET set_clause
[FROM from_clause]
WHERE condition [ASSERT_ROWS_MODIFIED n] [ then_return_clause ]

set_clause:
  update_item[, ...]

update_item:
  {
    path_expression = expression
    | path_expression = DEFAULT
    | (dml_stmt)
  }

then_return_clause:
  THEN RETURN
  [ WITH ACTION [ AS alias ]  ]
  { value_expression | expression [  [ AS ] alias ] } [, ...]

value_expression:
  [expression.]* [ except_clause ] [ replace_clause ]

except_clause:
  EXCEPT ( column_name [, ...] )

replace_clause:
  REPLACE ( expression [ AS ] column_name [, ...] )

dml_stmt:
  { insert_statement | update_statement | delete_statement }
</pre>

**Note**: `UPDATE` statements must comply with all
[statement rules][statement-rules] and use
[compatible types][compatible-types].

### `FROM` keyword

An `UPDATE` statement can optionally include a `FROM` clause.

You can use the `FROM` clause to specify the rows to update in the target table.
You can also use columns from joined tables in a `SET` clause or `WHERE`
condition.

The `FROM` clause join can be a cross join if no condition is specified in the
`WHERE` clause, otherwise it's an inner join. In either case, rows from the
target table can join with at most one row from the `FROM` clause.

To specify the join predicate between the table to be updated and tables in
the `FROM` clause, use the `WHERE` clause.

Caveats:

+ The `SET` clause can reference columns from a target table and columns from
  any `FROM` item in the `FROM` clause. If there is a name collision,
  unqualified references are treated as ambiguous.
+ If the target table is present in the `FROM` clause as a table name, it
  must have an alias if you would like to perform a self-join.
+ If a row in the table to be updated joins with zero rows from the `FROM`
  clause, then the row isn't updated.
+ If a row in the table to be updated joins with exactly one row from the `FROM`
  clause, then the row is updated.
+ If a row in the table to be updated joins with more than one row from the
  `FROM` clause, then the query generates a runtime error.

The following example generates a table with inventory totals that include
existing inventory and inventory from the `NewArrivals` table, and
marks `supply_constrained` as false.

```googlesql
UPDATE dataset.Inventory
SET quantity = quantity +
  (SELECT quantity FROM dataset.NewArrivals
   WHERE Inventory.product = NewArrivals.product),
    supply_constrained = false
WHERE product IN (SELECT product FROM dataset.NewArrivals)
```

Alternately, you can join the tables.

```googlesql
UPDATE dataset.Inventory i
SET quantity = i.quantity + n.quantity,
    supply_constrained = false
FROM dataset.NewArrivals n
WHERE i.product = n.product
```

Note: The join predicate between `Inventory` and `NewArrivals` is specified
using the `WHERE` clause.

Before:

```googlesql
-- Inventory
/*-------------------+----------+--------------------+
 |      product      | quantity | supply_constrained |
 +-------------------+----------+--------------------+
 | dishwasher        |       30 |               NULL |
 | dryer             |       30 |               NULL |
 | front load washer |       20 |               NULL |
 | microwave         |       20 |               NULL |
 | oven              |        5 |               NULL |
 | refrigerator      |       10 |               NULL |
 | top load washer   |       10 |               NULL |
 +-------------------+----------+--------------------*/

-- NewArrivals
/*-----------------+----------+--------------+
 |     product     | quantity |  warehouse   |
 +-----------------+----------+--------------+
 | dryer           |      200 | warehouse #2 |
 | oven            |      300 | warehouse #3 |
 | top load washer |      100 | warehouse #1 |
 +-----------------+----------+--------------*/
```

After:

```googlesql
/*-------------------+----------+--------------------+
 |      product      | quantity | supply_constrained |
 +-------------------+----------+--------------------+
 | dishwasher        |       30 |               NULL |
 | dryer             |      230 |              false |
 | front load washer |       20 |               NULL |
 | microwave         |       20 |               NULL |
 | oven              |      305 |              false |
 | refrigerator      |       10 |               NULL |
 | top load washer   |      110 |              false |
 +-------------------+----------+--------------------*/
```

### `WHERE` keyword

Each `UPDATE` statement must include the `WHERE` keyword, followed by a
condition. For example, the following statement illustrates an `UPDATE`
statement that modifies the row with the primary key, `5`.

```
UPDATE Singers s
SET s.Status = "inactive"
WHERE s.SingerId = 5;
```

To update all rows in a table, use `WHERE true`. The following statement sets
the SingerInfo value in all rows to `NULL` (the default value for the field).

```
UPDATE Singers s
SET s.SingerInfo = DEFAULT
WHERE true;
```

### `UPDATE` and `ASSERT_ROWS_MODIFIED`

With GoogleSQL, you can confirm how many rows are updated each time you
use an `UPDATE` statement. You implement this confirmation through the
`ASSERT_ROWS_MODIFIED` keyword.

**Note**: A row still counts as modified even if the updated values are
identical to the previous values.

With the `ASSERT_ROWS_MODIFIED` keyword, you specify the number of rows you
expect the command to update. If the row count matches, GoogleSQL
commits the changes. Otherwise, GoogleSQL returns an error and rolls
back the statement.

The `ASSERT_ROWS_MODIFIED` keyword is helpful when you want to be sure your
statement changes only a specific number of rows. For example, the following
statement verifies that exactly one row was found and updated.

```
UPDATE Singers s
SET s.SingerInfo = DEFAULT
WHERE s.SingerId = 5
ASSERT_ROWS_MODIFIED 1;
```

### `UPDATE` and `THEN RETURN`

With `THEN RETURN`, you can obtain data from rows that are being updated in a
table. To learn more about the values you can use in this clause, see [INSERT
and THEN RETURN][dml-then-return-clause]

Example:

The following query updates the `supply_constrained` column in this `Inventory`
table when the quantity of product is zero, and returns all updated product with
an out of stock message.

```googlesql
UPDATE Inventory
SET supply_constrained = true
WHERE quantity = 0
THEN RETURN product, product || " is out of stock" AS message;
```

### Setting new values

Each `UPDATE` statement requires a `SET` keyword. This keyword identifies the
columns that you want to update.

You specify values for specific columns using the syntax <em>column_name =
expression</em>. Expressions can see all the columns within a table. For
example:

```
UPDATE Singers s
SET s.SingerId = s.SingerId + 1
WHERE true
```

Any assignments within the expression happen simultaneously. For example, the
following statement swaps the values in the `FirstName` and `LastName` column.

```
UPDATE Singers s
SET s.FirstName = s.LastName,
    s.LastName = s.FirstName
WHERE true;
```

Within the `SET` clause, you can use the identifier for the target of the update
as a table alias. For example:

```
UPDATE Singers s
SET s.LastName = "Solo"
WHERE s.SingerId = 5;
```

Using an identifier can help make your `UPDATE` statement more explicit with
regards to what data GoogleSQL should update.

### Specifying columns

An `UPDATE` statement must explicitly state which columns you want to update.
Any column not included in the `UPDATE` statement remains unmodified.

In the following example, notice that only the `Status` column is
specified &mdash; consequently, the other columns
(such as `SingerId`, `FirstName`, `LastName`, and `Birthdate`) remain unchanged.

```
UPDATE Singers s
SET s.Status = "inactive"
WHERE s.SingerId = 5;
```

You can specify the columns to update in any order; however, you can list each
column only once. For example, the following statement is invalid and would be
rejected by GoogleSQL:

```
UPDATE Singers s
SET s.LastName = "Solo", s.LastName = "Organa"
WHERE s.SingerId = 5;
```

`UPDATE` statements must use values that are compatible with the corresponding
column's type. For example, a value of `1991-10-02` works for a column of type
`Date`, but a value of `October 2, 1991` wouldn't. You can, however, use values
that can be cast to the type of the corresponding column. Casting happens
automatically for numerical values (this is also referred to as
[coercion][coercion]).

If you attempt to set a column with an invalid value, the statement fails.
For example, the following statement doesn't work:

```
UPDATE Singers s
SET s.Birthdate = "October 2, 1991"
WHERE s.SingerId = 5;
```

### Returning columns to default values

You can use an `UPDATE` statement to return any value to its corresponding
default by using the `DEFAULT` keyword. In cases where the column contains a
protocol buffer field, that field is cleared.

For example, consider the following **Singers** table:

<table>
<thead>
<tr>
<th>SingerId</th>
<th>FirstName</th>
<th>LastName</th>
<th>BirthDate</th>
<th>SingerInfo</th>
<th>Albums</th>
</tr>
</thead>
<tbody>
<tr>
<td>5</td>
<td>Alice</td>
<td>Trentor</td>
<td>1991-10-2</td>
<td>"nationality: 'USA'"</td>
<td>NULL</td>
</tr>
<tr>
<td>6</td>
<td>Zak</td>
<td>Sterling</td>
<td>1989-1-13</td>
<td>"nationality: 'Australia'"</td>
<td>NULL</td>
</tr>
</tbody>
</table>

If you run the following statement:

```
UPDATE Singers s
SET s.SingerInfo = DEFAULT
WHERE SingerId = 6;
```

The table is updated as follows:

<table>
<thead>
<tr>
<th>SingerId</th>
<th>FirstName</th>
<th>LastName</th>
<th>BirthDate</th>
<th>SingerInfo</th>
<th>Albums</th>
</tr>
</thead>
<tbody>
<tr>
<td>5</td>
<td>Alice</td>
<td>Trentor</td>
<td>1991-10-2</td>
<td>"nationality:'USA'"</td>
<td>NULL</td>
</tr>
<tr>
<td>6</td>
<td>Zak</td>
<td>Sterling</td>
<td>1989-1-13</td>
<td>NULL</td>
<td>NULL</td>
</tr>
</tbody>
</table>

Notice that the row for `Zak Sterling` now has `NULL` in the `SingerInfo`
column, because `NULL` is the default value for that column. The following
example has the same effect:

```
UPDATE Singers s
SET s.SingerInfo = NULL
WHERE SingerId = 6;
```

### Updating fields

GoogleSQL allows you to update non-repeating or repeating fields within
protocol buffers. To illustrate how to update a non-repeating field, consider
the [Singers example table][singers-table].  It contains a column, `Albums`, of
type `PROTO<Albums>`, and `Albums` contains a non-repeating field `tracks`. The
following statement updates the value of `tracks`:

```
UPDATE Singers s
SET s.Albums.tracks = 15
WHERE s.SingerId = 5 AND s.Albums.title = "Fire is Hot";
```

An update can replace a repeated field using an array of values, as shown below.

```
UPDATE Singers s
SET s.Albums.comments = ["A good album!", "Hurt my ears!", "Totally inedible."]
WHERE s.SingerId = 5 AND s.Albums.title = "Fire is Hot";
```

### Nested updates

Inside a parent update statement you can construct DML statements that modify
a repeated field of a protocol buffer or an array.  Such statements are called
**nested updates**.

For example, the `Albums` column contains a repeated field `comments`. This
nested update statement adds a comment to an album:

```
UPDATE Singers s
SET (INSERT s.Albums.comments
     VALUES ("Groovy!"))
WHERE s.SingerId = 5 AND s.Albums.title = "Fire is Hot";
```

`Albums` also contains a repeated protocol buffer, `Song`, which provides
information about a song on an album. This nested update statement updates the
album with a new song:

```
UPDATE Singers s
SET (INSERT s.Albums.Song(Song)
     VALUES ("songtitle: 'Bonus Track', length: 180"))
WHERE s.SingerId = 5 AND s.Albums.title = "Fire is Hot";
```

If the repeated field is another protocol buffer, you can provide the
protocol buffer as a string literal. For example, the following statement adds a
new song to an album and updates the number of tracks.
Notice that this
statement uses `ASSERT_ROWS_MODIFIED` to ensure that only one `Singer` is
updated.

```
UPDATE Singers s
SET (INSERT s.Albums.Song
     VALUES ('''songtitle: 'Bonus Track', length:180''')),
     s.Albums.tracks = 16
WHERE s.SingerId = 5 and s.Albums.title = "Fire is Hot"
ASSERT_ROWS_MODIFIED 1;
```

You can also nest a nested update statement in another nested update statement.
For example, the `Song` protocol buffer itself has another repeated
protocol buffer, `Chart`, which provides information on what chart the song
appeared on, and what rank it had.

The following statement adds a new chart to a song.

```
UPDATE Singers s
SET (UPDATE s.Albums.Song so
    SET (INSERT INTO so.Chart
         VALUES ("chartname: 'Galaxy Top 100', rank: 5"))
    WHERE so.songtitle = "Bonus Track")
WHERE s.SingerId = 5
ASSERT_ROWS_MODIFIED 1;
```

This next statement updates the chart to reflect a new rank for the song.
Notice
that each inner `UPDATE` statement uses `ASSERT_ROWS_MODIFIED 1` to ensure that
only one update is made.

```
UPDATE Singers s
SET (UPDATE s.Albums.Song so
     SET (UPDATE so.Chart c
          SET c.rank = 2
          WHERE c.chartname = "Galaxy Top 100"
          ASSERT_ROWS_MODIFIED 1)
     WHERE so.songtitle = "Bonus Track"
     ASSERT_ROWS_MODIFIED 1)
WHERE s.SingerId = 5
ASSERT_ROWS_MODIFIED 1;
```

GoogleSQL treats an array or a repeated field inside a row that matches
an `UPDATE WHERE` clause as a table, with individual elements of the array or field treated like rows. These rows can then have nested DML statements run
against them, allowing you to delete, update, and insert data as needed.

#### Modifying multiple fields

The previous sections demonstrated how to update a single value within a
compound data type. You can also perform multiple updates to a compound data
type within a single statement. For example:

```
UPDATE Singers s
SET
    (DELETE FROM s.SingerInfo.Residence r WHERE r.City = 'Seattle'),
    (UPDATE s.Albums.Song song SET song.songtitle = 'No, This Is Rubbish' WHERE song.songtitle = 'This Is Pretty Good'),
    (INSERT s.Albums.Song VALUES ("songtitle: 'The Second Best Song'"))
WHERE SingerId = 3 AND s.Albums.title = 'Go! Go! Go!';
```

Nested queries are processed as follows:

1. Delete all rows that match a `WHERE` clause of a `DELETE` statement.
2. Update any remaining rows that match a `WHERE` clause of an `UPDATE`
statement. Each row must match at most one `UPDATE WHERE` clause, or the
statement fails due to overlapping updates.
3. Insert all rows in `INSERT` statements.

You must construct nested statements that affect the same field in the following
order:

+ `DELETE`
+ `UPDATE`
+ `INSERT`

For example:

```
UPDATE Singers s
SET
    (DELETE FROM s.SingerInfo.Residence r WHERE r.City = 'Seattle'),
    (UPDATE s.SingerInfo.Residence r SET r.end_year = 2015 WHERE r.City = 'Eugene'),
    (INSERT s.Albums.Song VALUES ("songtitle: 'The Second Best Song'"))
WHERE SingerId = 3 AND s.Albums.title = 'Go! Go! Go!';
```

The following statement is invalid, because the `UPDATE` statement
happens after the `INSERT` statement.

```
UPDATE Singers s
SET
    (DELETE FROM s.SingerInfo.Residence r WHERE r.City = 'Seattle'),
    (INSERT s.Albums.Song VALUES ("songtitle: 'The Second Best Song'")),
    (UPDATE s.SingerInfo.Residence r SET r.end_year = 2015 WHERE r.City = 'Eugene')
WHERE SingerId = 3 AND s.Albums.title = 'Go! Go! Go!';
```

**Note:** In nested queries, you can't use `INSERT OR` statements. These types
of statements don't work because arrays and other compound data types don't
always have a primary key, so there is no applicable definition of duplicate
rows.

You can also add `ASSERT_ROWS_MODIFIED` to nested statements, as shown here:

```
UPDATE Singers
SET
    (DELETE FROM AlbumTitles a WHERE a.AlbumTitles = "Too Many Vegetables"
        ASSERT_ROWS_MODIFIED 1),
    (UPDATE AlbumTitles a SET a.AlbumTitles = "Album IV"
        WHERE a.AlbumTitles = "Album Four"),
    (INSERT AlbumTitles VALUES ("The Sloth and the Tiger"));
```

[from-clause]: https://github.com/google/googlesql/blob/master/docs/query-syntax.md#from-clause

## `MERGE` statement

Use the `MERGE` statement when you want to merge rows from a source table or
subquery into a target table.

<pre>
MERGE [INTO] target_name [[AS] alias]
USING source_name
ON merge_condition
when_clause [...]

when_clause:
  {
    matched_clause
    | not_matched_by_target_clause
    | not_matched_by_source_clause
  }

matched_clause:
  WHEN MATCHED [ AND search_condition ]
  THEN { merge_update_clause | merge_delete_clause }

not_matched_by_target_clause:
  WHEN NOT MATCHED [BY TARGET] [ AND search_condition ]
  THEN merge_insert_clause

not_matched_by_source_clause:
  WHEN NOT MATCHED BY SOURCE [ AND search_condition ]
  THEN { merge_update_clause | merge_delete_clause }

merge_condition:
  bool_expression

search_condition:
  bool_expression

merge_update_clause:
  UPDATE SET update_item [, ...]

update_item:
  path_expression = expression

merge_delete_clause:
  DELETE

merge_insert_clause:
  INSERT [(column [, ... ])] input

input:
  {
    VALUES (expr [, ... ])
    | ROW
  }

expr:
  { expression | DEFAULT }
</pre>

A `MERGE` statement is a DML statement that can combine `INSERT`, `UPDATE`,
and `DELETE` operations into a single statement and perform the operations
atomically.

+  `target_name`: The name of the table you're changing.
+  `source_name`: A table name or subquery.
+  `merge_condition`: A `MERGE` statement performs a `JOIN` between the
   target and the source. Then, depending on the match status (row matched,
   only in source table, only in destination table), the corresponding `WHEN`
   clause is executed. The merge condition is used by the `JOIN` to match rows
   between source and target tables. Depending on the combination of
   `WHEN` clauses, different `INNER` and `OUTER JOIN` types are applied.

   If the merge condition is `FALSE`, the query optimizer avoids using a `JOIN`.
   This optimization is referred to as a constant false predicate. A
   constant false predicate is useful when you perform an atomic `DELETE` on
   the target plus an `INSERT` from a source (`DELETE` with `INSERT` is also
   known as a `REPLACE` operation).
+  `when_clause`: The `WHEN` clause has three options: `MATCHED`,
   `NOT MATCHED BY TARGET` and `NOT MATCHED BY SOURCE`. There must be at least
   one `WHEN` clause in each `MERGE` statement.

   Each `WHEN` clause can have an optional search condition. The `WHEN` clause
   is executed for a row if both the merge condition and search condition are
   satisfied. When there are multiple qualified clauses, only the first
   `WHEN` clause is executed for a row.
+  `matched_clause`: The `WHEN MATCHED` clause defines how to update or delete
   a row in the target table if that row matches a row in the source table.

   If there is at least one matched clause performing an `UPDATE` operation,
   a runtime error is returned when multiple rows from the source table match
   one row from the target table, and you are trying to update or delete that
   row in the target table.
+  `not_matched_by_target_clause`: The `WHEN NOT MATCHED` clause defines how to
   insert into the target table if a row from the source table doesn't match
   any row in the target table.

   When the column names of the target table are omitted, all columns in the
   target table are included in ascending order based on their ordinal
   positions.

   `ROW` can be used to include all the columns of the source in the ascending
   sequence of their ordinal positions. Note that none of the pseudo column of
   the source table is included.
+  `not_matched_by_source_clause`: The `WHEN NOT MATCHED BY SOURCE` clause
   defines how to update or delete a row in the target table if that row doesn't
   match any row in the source table.

In the following example, the query merges items from the `NewArrivals` table
into the `Inventory` table. If an item is already present in `Inventory`, the
query increments the quantity field. Otherwise, the query inserts a new row.

```googlesql
MERGE dataset.Inventory T
USING dataset.NewArrivals S
ON T.product = S.product
WHEN MATCHED THEN
  UPDATE SET quantity = T.quantity + S.quantity
WHEN NOT MATCHED THEN
  INSERT (product, quantity) VALUES(product, quantity)
```

These are the tables before you run the query:

```googlesql
-- NewArrivals
/*-----------------+----------+--------------+
 |     product     | quantity |  warehouse   |
 +-----------------+----------+--------------+
 | dryer           |       20 | warehouse #2 |
 | oven            |       30 | warehouse #3 |
 | refrigerator    |       25 | warehouse #2 |
 | top load washer |       10 | warehouse #1 |
 +-----------------+----------+--------------*/

--Inventory
/*-------------------+----------+
 |      product      | quantity |
 +-------------------+----------+
 | dishwasher        |       30 |
 | dryer             |       30 |
 | front load washer |       20 |
 | microwave         |       20 |
 | oven              |        5 |
 | top load washer   |       10 |
 +-------------------+----------*/
```

This is the `Inventory` table after you run the query:

```googlesql
--Inventory
/*-------------------+----------+
 |      product      | quantity |
 +-------------------+----------+
 | dishwasher        |       30 |
 | dryer             |       50 |
 | front load washer |       20 |
 | microwave         |       20 |
 | oven              |       35 |
 | refrigerator      |       25 |
 | top load washer   |       20 |
 +-------------------+----------*/
```

<!-- mdlint off(WHITESPACE_LINE_LENGTH) -->

[from-clause]: https://github.com/google/googlesql/blob/master/docs/query-syntax.md#from_clause

[join-operator]: https://github.com/google/googlesql/blob/master/docs/query-syntax.md#join_types

[dml-then-return-clause]: #dml_then_return_clause

[coercion]: https://github.com/google/googlesql/blob/master/docs/conversion_rules.md#coercion

[functions-and-operators]: https://github.com/google/googlesql/blob/master/docs/functions-and-operators.md

[statement-rules]: #statement_rules

[example-data]: #example_data

[singers-table]: #singers_table

[concerts-table]: #concerts_table

[compatible-types]: #compatible_types

<!-- mdlint on -->

