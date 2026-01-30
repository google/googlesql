

<!-- mdlint off(WHITESPACE_LINE_LENGTH) -->

# Data definition language

Data definition language (DDL) statements let you create, alter, and delete
GoogleSQL resources.

## `CREATE DATABASE`

<pre>
CREATE
  DATABASE
  database_name
  [OPTIONS (key=value, ...)]
</pre>

**Description**

The `CREATE DATABASE` statement creates a database. If you have schema
options, you can add them when you create the database. These options are
system-specific and follow the GoogleSQL
[`HINT` syntax][hints].

**Example**

```googlesql
CREATE DATABASE library OPTIONS(
  base_dir=`/city/orgs`,
  owner='libadmin'
);

/*--------------------+
 | Database           |
 +--------------------+
 | library            |
 +--------------------*/
```

[hints]: https://github.com/google/googlesql/blob/master/docs/lexical.md#hints

## `CREATE TABLE`

<a id="create_table_statement"></a>

Note: Some documentation is pending for this feature.

<pre>
CREATE
   [ OR REPLACE ]
   [ TEMP[ORARY] ]
   TABLE
   [ IF NOT EXISTS ]
   table_name [ ( <span class="var">table_element</span>, ... ) ]
   [ PARTITION [ hints ] BY partition_expression, ... ]
   [ CLUSTER [ hints ] BY cluster_expression, ... ]
   [ OPTIONS (key=value, ...) ]
   [ AS query ];

<span class="var">table_element:</span>
   <span class="var"><a href="#defining-columns">column_definition</a></span> | <span class="var"><a href="#defining-table-constraints">constraint_definition</a></span>
</pre>

**Description**

The `CREATE TABLE` statement creates a table and adds any table elements
defined in the table element list `(table_element, ...)`. If the `AS query`
clause is absent, the table element list must be present and contain at
least one column definition. A table element can be a column definition
or constraint definition. To learn more about column definitions, see
[Defining Columns][defining-columns]. To learn more about constraint
definitions, see [Defining Constraints][defining-constraints].

In a table, if both the table element list and the `AS query` clause
are present, then the number of columns in the table element list must
match the number of columns selected in `query`, and the type of each
column selected in `query` must be assignable to the column type in the
corresponding position of the table element list.

<!--TODO: Move assignable types to conversion_rules.md -->

<a id="assignable_types"></a>

A column is assignable if:

+   It's data type can be coerced into the corresponding table element
    column type.
+   If the column is an `INT64` type and the corresponding table element is an
    `INT32` type.
+   If the column is an `UINT64` type and the corresponding table element is an
    `UINT32` type.

**Optional Clauses**

+  `OR REPLACE`: Replaces any table with the same name if it exists. Can't
   appear with `IF NOT EXISTS`.
+  `TEMP | TEMPORARY`: Creates a temporary table. The lifetime of the table is
   system-specific.
+  `IF NOT EXISTS`: If any table exists with the same name, the `CREATE`
   statement will have no effect. Can't appear with `OR REPLACE`.
+  `PARTITION BY`: Creates partitions of a table. The expression can't
   contain floating point types, non-groupable types, constants,
   aggregate functions, or window functions.
+  `CLUSTER BY`: Co-locates rows if the rows aren't distinct for the values
   produced by the provided list of expressions.
   If the table was partitioned, co-location occurs within each partition.
   If the table wasn't partitioned, co-location occurs within the table.
+  `OPTIONS`: If you have schema options, you can add them when you create
   the table. These options are system-specific and follow the
   GoogleSQL[`HINT` syntax][hints]
+  `AS query`: Materializes the result of `query` into the new table.

**Examples**

Create a table.

```googlesql
CREATE TABLE books (title STRING, author STRING);
```

Create a table in a schema called `library`.

```googlesql
CREATE TABLE library.books (title STRING, author STRING);
```

Create a table that contains schema options.

```googlesql
CREATE TABLE books (title STRING, author STRING) OPTIONS (storage_kind=FLASH_MEMORY);
```

Partition a table.

```googlesql
CREATE TABLE books (title STRING, author STRING, publisher STRING, release_date DATE)
PARTITION BY publisher, author;
```

Partition a table with a pseudocolumn. In the example below, SYSDATE represents
the date when the book was added to the database. Replace SYSDATE with a
psuedocolumn supported by your SQL service.

```googlesql
CREATE TABLE books (title STRING, author STRING)
PARTITION BY sysdate;
```

Cluster a table.

```googlesql
CREATE TABLE books (
  title STRING,
  first_name STRING,
  last_name STRING
)
CLUSTER BY last_name, first_name;
```

#### Defining columns

<pre>
<span class="var">column_definition:</span>
  column_name
  {
    column_type
    | [ column_type ] <span class="var"><a href="#generated_column_clause">generated_column_clause</a></span>
  }
  [ <span class="var">column_attribute</span>[ ... ] ]
  [ OPTIONS (...) ]

<span class="var">column_attribute:</span>
  PRIMARY KEY
  | NOT NULL
  | HIDDEN
  | [ CONSTRAINT constraint_name ] <span class="var"><a href="#defining-foreign-references">foreign_reference</a></span>
</pre>

**Description**

A column exists within a table. Each column describes one attribute of the
rows that belong to the table.

Columns in a table are ordered. The order will have consequences on
some sorts of SQL statements such as `SELECT * FROM Table` and
`INSERT INTO Table VALUES (...)`.

**Definitions**

+   `column_name`: The name of the column. The name of a column must be unique
    within a table.
+   `column_type`: The GoogleSQL data type of the column.
+   `generated_column_clause`:  A generated column. To learn more, see
    [Defining the generated column clause][generated-column-clause].
+   `column_attribute`: A characteristic of the column. This can be:

    +  `PRIMARY KEY`: Defines this column as the [primary key][primary-key]
       of the table.

    +  `NOT NULL`: A value on a column can't be null. More specifically, this is
       shorthand for a constraint of the shape `CHECK [column_name] IS NOT NULL`.

    +  `HIDDEN`: Hides a column if it shouldn't appear in `SELECT * expansions`
       or in structifications of a row variable, such as
       `SELECT t FROM Table t`.

    +  `[ CONSTRAINT constraint_name ] foreign_reference`: The column in a
       [foreign table to reference][defining-foreign-reference].
       You can optionally name the constraint.
       A constraint name must be unique within its schema; it can't share the
       name of other constraints in the table.
       If a constraint name isn't provided, one is generated by the
       implementing engine. Users can use INFORMATION_SCHEMA to look up
       the generated names of table constraints.
+  `OPTIONS`: If you have schema options, you can add them when you create
   the column. These options are system-specific and follow the
   GoogleSQL[`HINT` syntax][hints]

**Examples**

Create a table with a primary key that can't be `NULL`.

```googlesql
CREATE TABLE books (title STRING, author STRING, isbn INT64 PRIMARY KEY NOT NULL);
```

Create a table with a generated column. In this example,
the generated column holds the first and last name of an author.

```googlesql
CREATE TABLE Authors (
  first_name STRING HIDDEN,
  last_name STRING HIDDEN,
  full_name STRING GENERATED ALWAYS AS (CONCAT(first_name, ' ', last_name)));
```

Create a table that contains schema options on column definitions.

```googlesql
CREATE TABLE books (
  title STRING NOT NULL PRIMARY KEY,
  author STRING
      OPTIONS (is_deprecated=true, comment="Replaced by authorId column"),
  authorId INT64 REFERENCES authors (id),
  category STRING OPTIONS (description="LCC Subclass")
)
```

#### Defining table constraints

<pre>
<span class="var">constraint_definition:</span>
   <span class="var">primary_key</span>
   | <span class="var">foreign_key</span>
   | <span class="var">check_constraint</span>

<span class="var">primary_key:</span>
  PRIMARY KEY (column_name [ ASC | DESC ], ... )
  [ OPTIONS (...) ]

<span class="var">foreign_key:</span>
  [ CONSTRAINT constraint_name ]
  FOREIGN KEY (column_name, ... )
  <span class="var"><a href="#defining-foreign-references">foreign_reference</a></span>
  [ OPTIONS (...) ]

<span class="var">check_constraint:</span>
  [ CONSTRAINT constraint_name ]
  CHECK ( boolean_expression )
  [ ENFORCED | NOT ENFORCED ]
  [ OPTIONS (...) ]
</pre>

**Description**

A `constraint_definition` is a rule enforced on the columns of a table.

**Constraint Kinds**

+  `primary_key`: Defines the [primary key][primary-key] for a table.
+  `foreign_key`:  Defines a foreign key for a table. A foreign key links
   two tables together.
+  `check_constraint`: Restricts the data that can be added to certain
   columns used by the expressions of the constraints.

**Optional Clauses**

+  `CONSTRAINT`: Names the constraint.
   A constraint name must be unique within its schema; it can't share the name
   of other constraints.
   If a constraint name isn't provided, one is generated by the
   implementing engine. Users can use INFORMATION_SCHEMA to look up
   the generated names of table constraints.
+  `ASC | DESC`: Specifies that the engine should optimize reading the
   index records in ascending or descending order by this key part.
   `ASC` is the default. Each key part will be sorted with respect to
   the sort defined by any key parts to the left.
+  `ENFORCED | NOT ENFORCED`: Specifies whether or not the constraint
   is enforced.
+  `OPTIONS`: If you have schema options, you can add them when you create
   the constraint. These options are system-specific and follow the
   GoogleSQL[`HINT` syntax][hints]

**Examples**

Create a primary key constraint, using the `title` and `author` columns
in a table called `books`.

```googlesql
CREATE TABLE books (title STRING, author STRING, PRIMARY KEY (title ASC, author ASC));
```

Create a foreign key constraint. When data in the `top_authors` table
is updated or deleted, make the same change in the `authors` table.

```googlesql
CREATE TABLE top_authors (
  author_first_name STRING,
  author_last_name STRING,
  CONSTRAINT fk_top_authors_name
    FOREIGN KEY (author_first_name, author_last_name)
    REFERENCES authors (first_name, last_name)
);
```

Create a check constraint. A row that contains values for `words_per_chapter`
and `words_per_book` can only only be inserted into the `page_count_average`
table if the `words_per_chapter` value is less than the `words_per_book` value.

```googlesql
CREATE TABLE page_count_average (
  words_per_chapter INT64,
  words_per_book INT64,
  CHECK (words_per_chapter < words_per_book)
);
```

#### Defining foreign references

<pre>
<span class="var">foreign_reference:</span>
  REFERENCES table_name (column_name, ... )
  [ MATCH { SIMPLE | FULL | NOT DISTINCT } ]
  [ ON UPDATE <span class="var">referential_action</span> ]
  [ ON DELETE <span class="var">referential_action</span> ]
  [ ENFORCED | NOT ENFORCED ]

<span class="var">referential_action:</span>
  NO ACTION | RESTRICT | CASCADE | SET NULL
</pre>

**Description**

A foreign key is used to define a relationship between the rows in two tables.
The foreign key in one table can reference one or more columns in
another table. A foreign reference can be used to assign constraints
to a foreign key and to give a foreign key a unique name.

**Optional Clauses**

+  `MATCH`: Specifies when a constraint validation for a referencing row
   passes or fails. Your choices are:
   +  `SIMPLE`:
      +  Passes if *any column* of the local key is `NULL`.
      +  Passes if the columns of the local key are pairwise-equal to the
         columns of the referenced key for some row of the referenced table.
   +  `FULL`:
      +  Passes if *all columns* of the local key are `NULL`.
      +  Passes if the columns of the local key are pairwise-equal to the
         columns of the referenced key for some row of the referenced table.
   +  `NOT DISTINCT`:
      +  Passes if the columns of the local key are pairwise-not-distinct from
         the columns of the referenced key for some row of the referenced table.
+  `ON UPDATE`: If data in the referenced table updates, your choices are:
   +  `RESTRICT`: Fail the transaction.
   +  `NO ACTION`: Don't update the referencing table. If the data in the
      referencing table doesn't satisfy the constraint before it's
      checked, the transaction will fail.
   +  `CASCADE`: For each updated row in the referenced table,
      update all matching rows in the referencing table so that
      they continue to match the same row after the transaction.
   +  `SET NULL`: Any change to a referenced column in the referenced table
      causes the corresponding referencing column in matching rows of the
      referencing table to be set to null.
+  `ON DELETE`: If data in the referenced table is deleted, your choices are:
   +  `RESTRICT`: Fail the transaction.
   +  `NO ACTION`: Don't delete the data in the referencing table. If the
       data in the referencing table doesn't satisfy the constraint before it's
       checked, the transaction will fail.
   +  `CASCADE`: For each deleted row in the referenced table,
      delete all matching rows in the referencing table so that
      they continue to match the same row after the transaction.
   +  `SET NULL`: If a row of the referenced table is deleted, then all
       referencing columns in all matching rows of the referencing table
       are set to null.
+  `ENFORCED | NOT ENFORCED`: Specifies whether or not the constraint
   is enforced.

**Examples**

When data in the `top_books` table is updated or deleted, make the
same change in the `books` table.

```googlesql
CREATE TABLE top_books (
  book_name STRING,
  CONSTRAINT fk_top_books_name
    FOREIGN KEY (book_name)
    REFERENCES books (title)
    ON UPDATE CASCADE
    ON DELETE CASCADE
);
```

#### Defining the generated column clause 
<a id="generated_column_clause"></a>

<pre>
<span class="var">generated_column_clause:</span>
  [ GENERATED [ ALWAYS ] ] AS
  ( <span class="var">generated_column_expression</span> )
  [ STORED ]
</pre>

**Description**

A generated column is a column in a base table whose value is defined as a
function of other columns in the same row.

**Definitions**

+   `column_type`: The GoogleSQL data type of the column.
+   `generated_column_expression`: A scalar expression that produces the
    content for the generated column. Subqueries aren't allowed. This expression
    can:

    + Reference any non-generated column of the containing table,
      regardless of column order.

    + Reference other generated columns, but recursion
      (or transitive recursion) isn't permitted.

    + Call built-in functions.

    + Call UDFs that don't access SQL data.
+   `STORED`: Controls whether the column is pre-computed and persisted in
    storage when the row is inserted or updated.

    When this keyword isn't present, the column is recomputed in
    every query that reads it. That means that the expression can be volatile
    and no storage is consumed. So if you use the `RAND()` function, then a
    different random number is generated each time the column is read.

    When this keyword is present, the column value is precomputed on update so
    that the value doesn't have to be recomputed each time it's read. That
    consumes storage space. This also means that volatile expressions won't work as
    intended. For example, the function `RAND()` will generate a random number
    once, and that first number will be used each time the column is read.

**Details**

By default, a generated column isn't stored and the column is computed when
the row is read.

A generated column can appear as an index key, primary key, or foreign key
if it satisfies the criteria for storage space.

If `column_type` and `generated_column_clause` are both defined, then the
type of the expression in `generated_column_clause` must be assignable to
`column_type`. For more information about assignable types, see the
[assignable types][assignable-types] note for columns in the `CREATE TABLE`
statement.

When a generated column is specified in a
`CREATE TABLE AS SELECT` statement, the query must not have an
output column for the generated column.

If computation of a non-stored generated column expression experiences a
runtime error when reading a query, the query fails. If a stored
generated column expression experiences a runtime error when being
written, the write operation fails.

**Example**

This query creates a table with a generated column. The generated column holds
the first and last name of an author.

```googlesql
CREATE TABLE Authors (
  first_name STRING HIDDEN,
  last_name STRING HIDDEN,
  full_name STRING GENERATED ALWAYS AS (CONCAT(first_name, ' ', last_name));
);
```

#### Using `CREATE TABLE AS`

```
CREATE TABLE table_definition AS query
```

**Description**

The `CREATE TABLE AS` statement creates a table and materializes the result
of a query into this new table. To learn more about the syntax for this
statement, see [`CREATE TABLE`][create-table].

**Examples**

Copy all rows from a table called `old_books` to a new table called
`books`.

```googlesql
CREATE TABLE books AS (SELECT * FROM old_books);
```

Copy rows from a table called `old_books` to a new table called `books`.
If a book was published before 1900, don't add it to `books`.

```googlesql
CREATE TABLE books
AS (SELECT * FROM old_books where year >= 1900)
```

Copy rows from a table called `old_books` to a new table called `books`.
Only copy the `title` column into `books`.

```googlesql
CREATE TABLE books AS (SELECT title FROM old_books);
```

Copy rows from `old_books` and `ancient_books` to a new table called
`books`.

```googlesql
CREATE TABLE books AS (
  SELECT * FROM old_books UNION ALL
  SELECT * FROM ancient_books);
```

[create-table]: #create_table

[hints]: https://github.com/google/googlesql/blob/master/docs/lexical.md#hints

[defining-columns]: #defining_columns

[defining-constraints]: #defining_table_constraints

[defining-foreign-reference]: #defining_foreign_references

[generated-column-clause]: #generated_column_clause

[primary-key]: #primary_key

[assignable-types]: #assignable_types

## `CREATE VIEW`

Note: The `RECURSIVE` keyword has been added, but documentation is pending.

<pre>
CREATE
  [OR REPLACE]
  [TEMP[ORARY]]
  VIEW
  [IF NOT EXISTS]
  view_name
  [SQL SECURITY { INVOKER | DEFINER }]
  [OPTIONS (key=value, ...)]
AS query;
</pre>

**Description**

The `CREATE VIEW` statement creates a view based on a specific query.

**Optional Clauses**

+   `OR REPLACE`: Replaces any view with the same name if it exists. Can't
    appear with `IF NOT EXISTS`.
+   `TEMP | TEMPORARY`: Creates a temporary view. The lifetime of the table is
    system-specific.
+   `IF NOT EXISTS`: If any view exists with the same name, the `CREATE`
    statement will have no effect. Can't appear with `OR REPLACE`.
+   `SQL SECURITY`: Specifies how data access control lists (ACLs) are applied
    with respect to schema objects referenced within the view's query.
    +  `DEFINER`: The privileges of the role (user) that created the view are
       used to run the view, and are applied to each schema object the view
       accesses.
    +  `INVOKER`: The privileges of the role (user) that's running the query
       that invoked the view are used to run the view, and are applied to each
       schema object the view accesses.

## `CREATE EXTERNAL TABLE`

<pre>
CREATE
  [OR REPLACE]
  [TEMP[ORARY]]
  EXTERNAL TABLE
  [IF NOT EXISTS]
  table_name
  [OPTIONS (key=value, ...)];
</pre>

**Description**

The `CREATE EXTERNAL TABLE` creates a table from external data. `CREATE EXTERNAL
TABLE` also supports creating persistent definitions.

The `CREATE EXTERNAL TABLE` doesn't build a new table; instead, it creates a
pointer to data that exists outside of the database.

**Optional Clauses**

+   `OR REPLACE`: Replaces any table with the same name if it exists. Can't
    appear with `IF NOT EXISTS`.
+   `TEMP | TEMPORARY`: Creates a temporary table. The lifetime of the table is
    system-specific.
+   `IF NOT EXISTS`: If any table exists with the same name, the `CREATE`
    statement will have no effect. Can't appear with `OR REPLACE`.

## `CREATE SNAPSHOT TABLE`

Documentation is pending for this feature.

## `CREATE INDEX`

<pre>
CREATE
  [OR REPLACE]
  [UNIQUE]
  INDEX
  [IF NOT EXISTS]
  index_name
  ON
  table_name [[AS] alias]
  [UNNEST(array_expression) [[AS] alias] [WITH OFFSET [[AS] alias]] ...]
  (key_expression [ASC|DESC], ...)
  [STORING (stored_expression, ...)]
  [OPTIONS (key=value, ...)];
</pre>

**Description**

The `CREATE INDEX` statement creates a secondary index for one or more
values computed from expressions in a table.

**Expressions**

+  `array_expression`: An immutable expression that's used to produce an array
   value from each row of an indexed table.
+  `key_expression`: An immutable expression that's used to produce an index
    key value. The expression must have a type that satisfies the requirement of
    an index key column.
+  `stored_expression`: An immutable expression that's used to produce a value
    stored in the index.

**Optional Clauses**

+  `OR REPLACE`: If the index already exists, replace it. This can't
    appear with `IF NOT EXISTS`.
+  `UNIQUE`: Don't index the same key multiple times. Systems can choose how to
    resolve conflicts in case the index is over a non-unique key.
+  `IF NOT EXISTS`: Don't create an index if it already exists. This can't
    appear with `OR REPLACE`.
+  `UNNEST(array_name)`: Create an index for the elements in an array.
+  `WITH OFFSET`: Return a separate column containing the offset value
    for each row produced by the `UNNEST` operation.
+  `ASC | DESC`: Sort the indexed values in ascending or descending
    order. `ASC` is the default value with respect to the sort defined by any
    key parts to the left.
+  `STORING`: Specify additional values computed from the indexed base table row
    to materialize with the index entry.
+  `OPTIONS`: If you have schema options, you can add them when you create the
    index. These options are system-specific and follow the
    GoogleSQL[`HINT` syntax][hints].

**Examples**

Create an index on a column in a table.

```googlesql
CREATE INDEX i1 ON KeyValue (Key);
```

Create an index on multiple columns in a table.

```googlesql
CREATE INDEX i1 ON KeyValue (Key, Value);
```

If the index already exists, replace it.

```googlesql
CREATE OR REPLACE INDEX i1 ON KeyValue (Key, Value);
```

If the index already exists, don't replace it.

```googlesql
CREATE INDEX IF NOT EXISTS i1 ON KeyValue (Key, Value);
```

Create an index that contains unique values.

```googlesql
CREATE UNIQUE INDEX i1 ON Books (Title);
```

Create an index that contains a schema option.

```googlesql
CREATE INDEX i1 ON KeyValue (Value) OPTIONS (page_count=1);
```

Reference the table name for a column.

```googlesql
CREATE INDEX i1 ON KeyValue (KeyValue.Key, KeyValue.Value);
```

```googlesql
CREATE INDEX i1 on KeyValue AS foo (foo.Key, foo.Value);
```

Use the path expression for a key.

```googlesql
CREATE INDEX i1 ON KeyValue (Key.sub_field1.sub_field2);
```

Choose the sort order for the columns assigned to an index.

```googlesql
CREATE INDEX i1 ON KeyValue (Key DESC, Value ASC);
```

Create an index on an array, but not the elements in an array.

```googlesql
CREATE INDEX i1 ON Books (BookList);
```

Create an index for the elements in an array.

```googlesql
CREATE INDEX i1 ON Books UNNEST (BookList) (BookList);
```

```googlesql
CREATE INDEX i1 ON Books UNNEST (BookListA) AS a UNNEST (BookListB) AS b (a, b);
```

Create an index for the elements in an array using an offset.

```googlesql
CREATE index i1 on Books UNNEST(BookList) WITH OFFSET (BookList, offset);
```

```googlesql
CREATE index i1 on Books UNNEST(BookList) WITH OFFSET AS foo (BookList, foo);
```

Store an additional column but don't sort it.

```googlesql
CREATE INDEX i1 ON KeyValue (Value) STORING (Key);
```

Store multiple additional columns and don't sort them.

```googlesql
CREATE INDEX i1 ON Books (Title) STORING (First_Name, Last_Name);
```

Store a column but don't sort it. Reference a table name.

```googlesql
CREATE INDEX i1 ON Books (InStock) STORING (Books.Title);
```

Use an expression in the `STORING` clause.

```googlesql
CREATE INDEX i1 ON KeyValue (Key) STORING (Key+1);
```

Use an implicit alias in the `STORING` clause.

```googlesql
CREATE INDEX i1 ON KeyValue (Key) STORING (KeyValue);
```

[hints]: https://github.com/google/googlesql/blob/master/docs/lexical.md#hints

## `CREATE_SCHEMA`

<a id="create_schema_statement"></a>

Documentation is pending for this feature.

## `CREATE CONSTANT`

<pre>
CREATE
  [OR REPLACE]
  [{ TEMP[ORARY] | PUBLIC | PRIVATE }]
  CONSTANT
  [IF NOT EXISTS]
  <span class="var">constant_name</span> = <span class="var">constant_value</span>;
</pre>

**Description**

The `CREATE CONSTANT` statement creates a constant and assigns a value to it.
The value can't be altered later with the `ALTER` statement, but you can use
`CREATE OR REPLACE CONSTANT` if you need to replace the value.

+ `OR REPLACE`: Replace a constant if it already exists. This can't appear
  with `IF NOT EXISTS`.
+ `TEMP | TEMPORARY`: Creates a temporary constant. The lifetime of the
  constant is system-specific.
+ `PUBLIC`: If the constant is declared in a module, `PUBLIC` specifies that
  it's available outside of the module.
+ `PRIVATE`: If the constant is declared in a module, `PRIVATE` specifies that
  it's only available inside of the module (default).
+ `IF NOT EXISTS`: Don't create a constant if it already exists. If the
  constant already exists, don't produce an error and keep the existing value.
  This can't appear with `OR REPLACE`.
+ `constant_name`: The name of the constant. This can include a path.
+ `constant_value`: An expression that represents the value for the constant.

The constant declaration doesn't specify a type. The constant type is the
`constant_value` type. You can't `ALTER`, `RENAME`, or `DROP` a constant.

Constants can't be used as arguments to aggregate
user-defined functions (UDFs).

**Example**

Create a constant, `DEFAULT_HEIGHT`:

```googlesql
CREATE TEMPORARY CONSTANT DEFAULT_HEIGHT = 25;
```

Use it in a statement:

```googlesql
SELECT (DEFAULT_HEIGHT + 5) AS result;

/*--------+
 | result |
 +--------+
 | 30     |
 +--------*/
```

## `CREATE AGGREGATE FUNCTION`

A user-defined aggregate function (UDA), lets you create an aggregate function
using another SQL expression or another programming language. These functions
accept arguments and perform actions, returning the result of those actions as a
value. To create a UDA, see [UDAs][udas].

[udas]: https://github.com/google/googlesql/blob/master/docs/user-defined-aggregates.md#udas

## `CREATE FUNCTION`

A user-defined function (UDF), lets you create a scalar function using another
SQL expression or another programming language. These functions accept arguments
and perform actions, returning the result of those actions as a value. To create
a UDF, see [UDFs][udfs].

[udfs]: https://github.com/google/googlesql/blob/master/docs/user-defined-functions.md

## `CREATE MODEL`

Documentation is pending for this feature.

## `CREATE PLACEMENT`

Documentation is pending for this feature.

## `CREATE PROCEDURE`

<pre>
CREATE
  [OR REPLACE]
  [{ TEMP[ORARY] | PUBLIC | PRIVATE }]
  PROCEDURE
  [IF NOT EXISTS]
  procedure_name
  ( [ <span class="var">parameter_definition</span> [, ...] ] )
  [OPTIONS (key=value, ...)]
  BEGIN
    <span class="var">sql_statement_list</span>
  END;

<span class="var">parameter_definition:</span>
  [ <span class="var">mode</span> ] parameter_name type

<span class="var">mode:</span>
  { IN | OUT | INOUT }
</pre>

**Description**

The `CREATE PROCEDURE` statement creates a procedure. A procedure is a reusable
block of SQL statements that can be invoked by name from other queries, and
supports arguments.

**Optional Clauses**

+   `OR REPLACE`: Replaces any procedure with the same name if it exists. Can't
    appear with `IF NOT EXISTS`.
+   `TEMP | TEMPORARY`: Creates a temporary procedure. The lifetime of the
    procedure is system specific.
+   `PUBLIC`: If the procedure is declared in a module, `PUBLIC` specifies that
    it's available outside of the module.
+   `PRIVATE`: If the procedure is declared in a module, `PRIVATE` specifies
    that it's only available inside of the module (default).
+   `IF NOT EXISTS`: If any procedure exists with the same name, the `CREATE`
    statement has no effect. Can't appear with `OR REPLACE`.
+   `parameter_definition`: Defines a parameter for the procedure.
    +   `mode`: The mode of the parameter. Can be `IN`, `OUT`, or `INOUT`.
        +   `IN`: The parameter is an input parameter.
        +   `OUT`: The parameter is an output parameter.
        +   `INOUT`: The parameter is both an input and an output parameter.
    +   `parameter_name`: The name of the parameter.
    +   `type`: The GoogleSQL data type of the parameter.
    +   `DEFAULT default_value`: The default value for the parameter.
+   `OPTIONS`: If you have schema options, you can add them when you create the
    procedure. These options are system specific and follow the GoogleSQL[`HINT`
    syntax][hints].
+   `BEGIN ... END`: The block of SQL statements that make up the procedure.

**Example**

```sql
CREATE PROCEDURE my_procedure(IN x INT64, OUT y STRING)
BEGIN
  IF x > 0 THEN
    SET y = 'positive';
  ELSE
    SET y = 'non-positive';
  END IF;
END;
```

## `CREATE ROW POLICY`

Documentation is pending for this feature.

## `CREATE PRIVILEGE RESTRICTION`

<pre>
CREATE
  [ OR REPLACE ]
  PRIVILEGE RESTRICTION
  [ IF NOT EXISTS ]
  ON SELECT (<span class="var">column_list</span>)
  ON <span class="var">object_type</span> object_path
  [ RESTRICT TO (<span class="var">exemptee_list</span>) ];

<span class="var">column_list:</span>
  column_name[, ...]

<span class="var">object_type:</span>
  { TABLE | VIEW }

<span class="var">exemptee_list:</span>
  { mdb_user | gaia_user | mdb_group }[, ...]
</pre>

**Description**

The `CREATE PRIVILEGE RESTRICTION` statement restricts who can select a list of
columns from a table or view. Users and groups not in the exemptee list are
denied from exercising these privileges on the object.

> Warning: Creating a privilege restriction can break downstream dependencies.
> For tips to avoid this, see [Privileges and Restrictions]
> ((broken link)).

**Parameters**

+   `column_list`: The list of columns on which to add privilege restrictions.
+   `object_type`: The type of object on which to add privilege restrictions.
    Restrictions can be added for tables and views.
+   `object_path`: The path of the object.
+   `exemptee_list`: Comma-delimited list of quoted users and groups.
    Only users and groups with `READER` privileges on the object who are also in
    this list can exercise those privileges.

**Optional Clauses**

+   `OR REPLACE`: This creates restrictions if they don't exist or overwrites
    them if they do. Can't be used with `IF NOT EXISTS`.
+   `IF NOT EXISTS`: This creates restrictions if they don't exist or does
    nothing if they do. Can't be used with `OR REPLACE`.
+   If neither `OR REPLACE` nor `IF NOT EXISTS` is specified, this creates
    restrictions if they don't exist or returns an error if they do.
+   `RESTRICT TO`: If specified, sets the ACL of the privilege restriction to
    `exemptee_list`. If not specified, the privilege restriction is
    interpreted as having an empty ACL, in which case no users can exercise the
    privileges.

## `CREATE PROPERTY GRAPH`

Creates a property graph. For more information, see
[`CREATE PROPERTY GRAPH`][create-property-graph] in the GQL reference.

[create-property-graph]: https://github.com/google/googlesql/blob/master/docs/graph-schema-statements.md#create_property_graph

## `CREATE TABLE FUNCTION`

A table function, also known as a table-valued function (TVF), returns a table. A
TVF is called in the `FROM` clause like a table subquery. To create a TVF, see
[TVFs][tvfs].

[tvfs]: https://github.com/google/googlesql/blob/master/docs/table-functions.md#tvfs

## `DEFINE TABLE`

<pre>
DEFINE TABLE table_name (options);
</pre>

**Description**

The `DEFINE TABLE` statement allows queries to run against an exported data
source.

## `ALTER`

<a id="alter_schema_collate_statement"></a>
<a id="alter_table_collate_statement"></a>

Note: Some documentation is pending for this feature.

<pre>
ALTER <span class="var">object_type</span> <span class="var">alter_action</span>, ...

<span class="var">object_type</span>:
  TABLE
  | VIEW

<span class="var">alter_action</span>:
  SET OPTIONS (key=value, ...)
  | ADD COLUMN [ IF NOT EXISTS ]  column_definition
  | DROP COLUMN [ IF EXISTS ]  column_name
</pre>

**Description**

The `ALTER` statement modifies metadata for a table or a view.

`table_name` is any identifier or dotted path.

The option entries are system-specific. These follow the GoogleSQL
[`HINT` syntax][hints].

SET OPTIONS action raises an error under these conditions:

+   The table doesn't exist.
+   A key isn't recognized.

The following semantics apply:

+   The value is updated for each key in the `SET OPTIONS` clause.
+   Keys not in the `SET OPTIONS` clause remain unchanged.
+   Setting a key value to `NULL` clears it.

ADD and DROP COLUMN actions raise an error under these conditions:

+   ADD COLUMN or DROP COLUMN are applied to a view.
+   ADD COLUMN specified without IF NOT EXIST and the column already exists.
+   DROP COLUMN specified without IF EXIST and the column doesn't exist.

**Examples**

The following examples illustrate ways to use the `ALTER SET OPTIONS` statement:

Update table description.

```googlesql
ALTER TABLE my_dataset.my_table
SET OPTIONS (description='my table');
```

Remove table description.

```googlesql
ALTER TABLE my_dataset.my_table
SET OPTIONS (description=NULL);
```

The following example illustrates using the `ALTER ADD COLUMN` statement:

```googlesql
ALTER TABLE mydataset.mytable
    ADD COLUMN A STRING,
    ADD COLUMN IF NOT EXISTS B GEOGRAPHY,
    ADD COLUMN C ARRAY<NUMERIC>,
    ADD COLUMN D DATE OPTIONS(description="my description")
```

Add column A of type STRUCT.

```googlesql
ALTER TABLE mydataset.mytable
ADD COLUMN A STRUCT<
               B GEOGRAPHY,
               C ARRAY<INT64>,
               D INT64 NOT NULL,
               E TIMESTAMP OPTIONS(description="creation time")
             >
```

[hints]: https://github.com/google/googlesql/blob/master/docs/lexical.md#hints

## `RENAME`

<pre>
RENAME object_type old_name_path TO new_name_path;
</pre>

**Description**

The `RENAME` object renames an object. `object_type` indicates what type of
object to rename.

## `DROP`

Note: Some documentation is pending for this feature.

<pre>
DROP object_type [IF EXISTS] object_path;
</pre>

**Description**

The `DROP` statement drops an object. `object_type` indicates what type of
object to drop.

**Optional Clauses**

+   `IF EXISTS`: If no object exists at `object_path`, the `DROP` statement will
    have no effect.

## `DROP PROPERTY GRAPH`

Deletes a property graph. For more information, see
[`DROP PROPERTY GRAPH`][drop-property-graph] in the GQL reference.

[drop-property-graph]: https://github.com/google/googlesql/blob/master/docs/graph-schema-statements.md#drop_property_graph

## Terminology

### Primary key

A primary key constraint is an attribute of a table. A table can have at most
one primary key constraint that includes one or more columns of that table.
Each row in the table has a tuple that's the row's primary key. The primary key
tuple has values that correspond to the columns in the primary key constraint.
The primary key of all rows must be unique among the primary keys of all rows in
the table.

**Examples**

In this example, a column in a table called `books` is assigned to a primary
key. For each row in this table, the value in the `id` column must be distinct
from the value in the `id` column of all other rows in the table.

```googlesql
CREATE TABLE books (title STRING, id STRING, PRIMARY KEY (id));
```

In this example, multiple columns in a table called `books` are assigned to a
primary key. For each row in this table, the tuple of values in the `title` and
`name` columns must together be distinct from the values in the respective
`title` and `name` columns of all other rows in the table.

```googlesql
CREATE TABLE books (title STRING, name STRING, PRIMARY KEY (title, name));
```

