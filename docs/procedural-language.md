

<!-- mdlint off(WHITESPACE_LINE_LENGTH) -->

# Procedural language

The GoogleSQL procedural language lets you execute multiple statements
in one query as a multi-statement query. You can use
a multi-statement query to:

+ Run multiple statements in a sequence, with shared state.
+ Automate management tasks such as creating or dropping tables.
+ Implement complex logic using programming constructs such as `IF` and `WHILE`.

## `DECLARE`

<pre class="lang-sql prettyprint">
DECLARE variable_name[, ...] [variable_type] [DEFAULT expression];
</pre>

`variable_name` must be a valid identifier, and `variable_type` is any
GoogleSQL [type](data-types).

**Description**

Declares a variable of the specified type. If the `DEFAULT` clause is specified,
the variable is initialized with the value of the expression; if no
`DEFAULT` clause is present, the variable is initialized with the value
`NULL`.

If `[variable_type]` is omitted then a `DEFAULT` clause must be specified.  The
variable’s type will be inferred by the type of the expression in the `DEFAULT`
clause.

Variable declarations must appear before other procedural statements, or at the
start of a `BEGIN` block. Variable names are case-insensitive.

Multiple variable names can appear in a single `DECLARE` statement, but only
one `variable_type` and `expression`.

It's an error to declare a variable with the same name as a variable
declared earlier in the current block or in a containing block.

If the `DEFAULT` clause is present, the value of the expression must be
coercible to the specified type. The expression may reference other variables
declared previously within the same block or a containing block.

**Examples**

The following example initializes the variable `x` as an
`INT64` with the value `NULL`.

<pre class="lang-sql prettyprint">
DECLARE x INT64;
</pre>

The following example initializes the variable `d` as a
`DATE` object with the value of the current date.

<pre class="lang-sql prettyprint">
DECLARE d DATE DEFAULT CURRENT_DATE();
</pre>

The following example initializes the variables `x`, `y`, and `z` as
`INT64` with the value 0.

<pre class="lang-sql prettyprint">
DECLARE x, y, z INT64 DEFAULT 0;
</pre>

The following example declares a variable named `item` corresponding to an
arbitrary item in the `schema1.products` table.  The type of `item` is inferred
from the table schema.

<pre class="lang-sql prettyprint">
DECLARE item DEFAULT (SELECT item FROM schema1.products LIMIT 1);
</pre>

## `SET`

**Syntax**

<pre class="lang-sql prettyprint">
SET variable_name = expression;
</pre>

<pre class="lang-sql prettyprint">
SET (variable_name[, ...]) = (expression[, ...]);
</pre>

**Description**

Sets a variable to have the value of the provided expression, or sets multiple
variables at the same time based on the result of multiple expressions.

The `SET` statement may appear anywhere within a multi-statement query.

**Examples**

The following example sets the variable `x` to have the value 5.

<pre class="lang-sql prettyprint">
SET x = 5;
</pre>

The following example sets the variable `a` to have the value 4, `b` to have the
value 'foo', and the variable `c` to have the value `false`.

<pre class="lang-sql prettyprint">
SET (a, b, c) = (1 + 3, 'foo', false);
</pre>

The following example assigns the result of a query to multiple variables.
First, it declares two variables, `target_word` and `corpus_count`; next, it
assigns the results of a
[`SELECT AS STRUCT` query](query-syntax#select_as_struct)
to the two variables. The result of the query is a single row containing a
`STRUCT` with two fields; the first element is
assigned to the first variable, and the second element is assigned to the second
variable.

<pre class="lang-sql prettyprint">
DECLARE target_word STRING DEFAULT 'methinks';
DECLARE corpus_count, word_count INT64;

SET (corpus_count, word_count) = (
  SELECT AS STRUCT COUNT(DISTINCT corpus), SUM(word_count)
  FROM shakespeare
  WHERE LOWER(word) = target_word
);

SELECT
  FORMAT('Found %d occurrences of "%s" across %d Shakespeare works',
         word_count, target_word, corpus_count) AS result;
</pre>

This statement list outputs the following string:

```
Found 151 occurrences of "methinks" across 38 Shakespeare works
```

## `EXECUTE IMMEDIATE`

**Syntax**

```googlesql
EXECUTE IMMEDIATE sql_expression [ INTO variable[, ...] ] [ USING identifier[, ...] ];

sql_expression:
  { "query_statement" | expression("query_statement") }

identifier:
  { variable | value } [ AS alias ]
```

**Description**

Executes a dynamic SQL statement on the fly.

+  `sql_expression`: An expression that can represent one of the following:

    + A [query statement][query-syntax]
    + An expression that you can use on a query statement
    + A single [DDL statement][ddl]
    + A single [DML statement][dml]

    This expression can't be a control statement like `IF`.
+  `expression`: Can be a
   [function][functions], [conditional expression][conditional-expressions], or
   [expression subquery][expression-subqueries].
+  `query_statement`: Represents a valid standalone SQL statement to execute.
   If this returns a value, the `INTO` clause must contain values of the same
   type. You may access both system variables and values present in the `USING`
   clause; all other local variables and query parameters aren't exposed to
   the query statement.
+  `INTO` clause: After the SQL expression is executed, you can store the
   results in one or more [variables][declare], using the `INTO` clause.
+  `USING` clause: Before you execute your SQL expression, you can pass in one
   or more identifiers from the `USING` clause into the SQL expression.
   These identifiers function similarly to query parameters, exposing values to
   the query statement. An identifier can be a variable or a value.

You can include these placeholders in the `query_statement` for identifiers
referenced in the `USING` clause:

+  `?`: The value for this placeholder is bound to an identifier in the `USING`
   clause by index.

   ```googlesql
   DECLARE y INT64;
   -- y = 1 * (3 + 2) = 5
   EXECUTE IMMEDIATE "SELECT ? * (? + 2)" INTO y USING 1, 3;
   ```
+  `@identifier`: The value for this placeholder is bound to an identifier in
   the `USING` clause by name. This syntax is identical to
   the query parameter syntax.

   ```googlesql
   DECLARE y INT64;
   -- y = 1 * (3 + 2) = 5
   EXECUTE IMMEDIATE "SELECT @a * (@b + 2)" INTO y USING 1 as a, 3 as b;
   ```

Here are some additional notes about the behavior of the `EXECUTE IMMEDIATE`
statement:

+ `EXECUTE IMMEDIATE` is restricted from being executed dynamically as a
  nested element. This means `EXECUTE IMMEDIATE` can't be nested in another
  `EXECUTE IMMEDIATE` statement.
+  If an `EXECUTE IMMEDIATE` statement returns results, then those results
   become the result of the entire statement and any appropriate
   system variables are updated.
+  The same variable can appear in both the `INTO` and `USING` clauses.
+  `query_statement` can contain a single parsed statement that contains other
   statements (for example, BEGIN...END)
+  If zero rows are returned from `query_statement`, including from zero-row
   value tables, all variables in the `INTO` clause are set to NULL.
+  If one row is returned from `query_statement`, including from zero-row
   value tables, values are assigned by position, not variable name.
+  If an `INTO` clause is present, an error is thrown if you attempt to return
   more than one row from `query_statement`.

**Examples**

In this example, we create a table of books and populate it with data. Note
the different ways that you can reference variables, save values to
variables, and use expressions.

```googlesql
-- Create some variables.
DECLARE book_name STRING DEFAULT 'Ulysses';
DECLARE book_year INT64 DEFAULT 1922;
DECLARE first_date INT64;

-- Create a temporary table called Books.
EXECUTE IMMEDIATE
  "CREATE TEMP TABLE Books (title STRING, publish_date INT64)";

-- Add a row for Hamlet (less secure).
EXECUTE IMMEDIATE
  "INSERT INTO Books (title, publish_date) VALUES('Hamlet', 1599)";

-- Add a row for Ulysses, using the variables declared and the ? placeholder.
EXECUTE IMMEDIATE
  "INSERT INTO Books (title, publish_date) VALUES(?, ?)"
  USING book_name, book_year;

-- Add a row for Emma, using the identifier placeholder.
EXECUTE IMMEDIATE
  "INSERT INTO Books (title, publish_date) VALUES(@name, @year)"
  USING 1815 as year, "Emma" as name;

-- Add a row for Middlemarch, using an expression.
EXECUTE IMMEDIATE
  CONCAT(
    "INSERT INTO Books (title, publish_date)", "VALUES('Middlemarch', 1871)"
  );

-- The table looks similar to the following:
/*------------------+------------------+
 | title            | publish_date     |
 +------------------+------------------+
 | Hamlet           | 1599             |
 | Ulysses          | 1922             |
 | Emma             | 1815             |
 | Middlemarch      | 1871             |
 +------------------+------------------*/

-- Save the publish date of the earliest book, Hamlet, to a variable called
-- first_date.
EXECUTE IMMEDIATE "SELECT MIN(publish_date) FROM Books LIMIT 1" INTO first_date;
```

## `BEGIN...END` 
<a id="begin"></a>

**Syntax**

<pre class="lang-sql prettyprint">
BEGIN
  sql_statement_list
END;
</pre>

**Description**

`BEGIN` initiates a block of statements where declared variables exist only
until the corresponding `END`. `sql_statement_list` is a list of zero or more
SQL statements ending with semicolons.

Variable declarations must appear at the start of the block, prior to other
types of statements. Variables declared inside a block may only be referenced
within that block and in any nested blocks. It's an error to declare a variable
with the same name as a variable declared in the same block or an outer block.

There is a maximum nesting level of 50 for blocks and conditional statements
such as `BEGIN`/`END`, `IF`/`ELSE`/`END IF`, and `WHILE`/`END WHILE`.

`BEGIN`/`END` is restricted from being executed dynamically as a nested element.

You can use a label with this statement. To learn more, see [Labels][labels].

**Examples**

The following example declares a variable `x` with the default value 10; then,
it initiates a block, in which a variable `y` is assigned the value of `x`,
which is 10, and returns this value; next, the `END` statement ends the
block, ending the scope of variable `y`; finally, it returns the value of `x`.

<pre class="lang-sql prettyprint">
DECLARE x INT64 DEFAULT 10;
BEGIN
  DECLARE y INT64;
  SET y = x;
  SELECT y;
END;
SELECT x;
</pre>

## `BEGIN...EXCEPTION...END`

**Syntax**

<pre class="lang-sql prettyprint">
BEGIN
  sql_statement_list
EXCEPTION WHEN ERROR THEN
  sql_statement_list
END;
</pre>

**Description**

`BEGIN...EXCEPTION` executes a block of statements. If any of the statements
encounter an error, the remainder of the block is skipped and the statements in
the `EXCEPTION` clause are executed.

To handle exceptions that are thrown (and not handled) by an exception handler
itself, you must wrap the block in an outer block with a separate exception
handler.

The following shows how to use an outer block with a separate exception handler:

<pre class="lang-sql prettyprint">
BEGIN
  BEGIN
    ...
  EXCEPTION WHEN ERROR THEN
    SELECT 1/0;
  END;
EXCEPTION WHEN ERROR THEN
  -- The exception thrown from the inner exception handler lands here.
END;
</pre>

`BEGIN...EXCEPTION` blocks also support `DECLARE` statements, just like any
other `BEGIN` block.  Variables declared in a `BEGIN` block are valid only in
the `BEGIN` section, and may not be used in the block’s exception handler.

You can use a label with this statement. To learn more, see [Labels][labels].

**Examples**

In this example, when the division by zero error occurs, instead of
stopping the entire multi-statement query, GoogleSQL stops
`schema1.proc1()` and `schema1.proc2()` and execute the `SELECT` statement in
the exception handler.

<pre class="lang-sql prettyprint">
CREATE OR REPLACE PROCEDURE schema1.proc1() BEGIN
  SELECT 1/0;
END;

CREATE OR REPLACE PROCEDURE schema1.proc2() BEGIN
  CALL schema1.proc1();
END;

BEGIN
  CALL schema1.proc2();
EXCEPTION WHEN ERROR THEN
  SELECT "An error occurred when calling schema1.proc2";
END;
</pre>

## `CASE`

**Syntax**

<pre class="lang-sql prettyprint">
CASE
  WHEN boolean_expression THEN sql_statement_list
  [...]
  [ELSE sql_statement_list]
END CASE;
</pre>

**Description**

Executes the `THEN sql_statement_list` where the boolean expression is true,
or the optional `ELSE sql_statement_list` if no conditions match.

`CASE` can have a maximum of 50 nesting levels.

`CASE` is restricted from being executed dynamically as a nested element. This
means `CASE` can't be nested in an `EXECUTE IMMEDIATE` statement.

**Examples**

In this example, a search if conducted for the `target_product_ID` in the
`products_a` table. If the ID isn't found there, a search is conducted for
the ID in the `products_b` table. If the ID isn't found there, the statement in
the `ELSE` block is executed.

<pre class="lang-sql prettyprint">
DECLARE target_product_id INT64 DEFAULT 103;
CASE
  WHEN
    EXISTS(SELECT 1 FROM schema.products_a WHERE product_id = target_product_id)
    THEN SELECT 'found product in products_a table';
  WHEN
    EXISTS(SELECT 1 FROM schema.products_b WHERE product_id = target_product_id)
    THEN SELECT 'found product in products_b table';
  ELSE
    SELECT 'did not find product';
END CASE;
</pre>

## `CASE search_expression`

**Syntax**

<pre class="lang-sql prettyprint">
CASE search_expression
  WHEN expression THEN sql_statement_list
  [...]
  [ELSE sql_statement_list]
END CASE;
</pre>

**Description**

Executes the first `sql_statement_list` where the search expression is matches
a `WHEN` expression. The `search_expression` is evaluated once and then
tested against each `WHEN` expression for equality until a match is found.
If no match is found, then the optional <code>ELSE</code> `sql_statement_list`
is executed.

`CASE` can have a maximum of 50 nesting levels.

`CASE` is restricted from being executed dynamically as a nested element. This
means `CASE` can't be nested in an `EXECUTE IMMEDIATE` statement.

**Examples**

The following example uses the product ID as the search expression. If the
ID is `1`, `'Product one'` is returned. If the ID is `2`, `'Product two'`
is returned. If the ID is anything else, `Invalid product` is returned.

<pre class="lang-sql prettyprint">
DECLARE product_id INT64 DEFAULT 1;
CASE product_id
  WHEN 1 THEN
    SELECT CONCAT('Product one');
  WHEN 2 THEN
    SELECT CONCAT('Product two');
  ELSE
    SELECT CONCAT('Invalid product');
END CASE;
</pre>

## `IF`

**Syntax**

<pre class="lang-sql prettyprint">
IF condition THEN [sql_statement_list]
  [<span class="kwd">ELSEIF</span> condition THEN sql_statement_list]
  [...]
  [ELSE sql_statement_list]
END IF;
</pre>

**Description**

Executes the first `sql_statement_list` where the condition is true, or the
optional <code>ELSE</code> `sql_statement_list` if no conditions match.

There is a maximum nesting level of 50 for blocks and conditional statements
such as `BEGIN`/`END`, `IF`/`ELSE`/`END IF`, and `WHILE`/`END WHILE`.

`IF` is restricted from being executed dynamically as a nested element. This
means `IF` can't be nested in an `EXECUTE IMMEDIATE` statement.

**Examples**

The following example declares a INT64 variable
`target_product_id` with a default value of 103; then, it checks whether the
table `schema.products` contains a row with the `product_id` column matches
the value of `target_product_id`; if so, it outputs a string stating that the
product has been found, along with the value of `default_product_id`; if not,
it outputs a string stating that the product hasn't been found, also with the
value of `default_product_id`.

<pre class="lang-sql prettyprint">
DECLARE target_product_id INT64 DEFAULT 103;
IF EXISTS(SELECT 1 FROM schema.products
           WHERE product_id = target_product_id) THEN
  SELECT CONCAT('found product ', CAST(target_product_id AS STRING));
  <span class="kwd">ELSEIF</span> EXISTS(SELECT 1 FROM schema.more_products
           WHERE product_id = target_product_id) THEN
  SELECT CONCAT('found product from more_products table',
  CAST(target_product_id AS STRING));
ELSE
  SELECT CONCAT('did not find product ', CAST(target_product_id AS STRING));
END IF;
</pre>

## Labels

**Syntax**

<pre class="lang-sql prettyprint">
label_name: BEGIN
  block_statement_list
END [label_name];
</pre>

<pre class="lang-sql prettyprint">
label_name: <span class="kwd">LOOP</span>
  loop_statement_list
END <span class="kwd">LOOP</span> [label_name];
</pre>

<pre class="lang-sql prettyprint">
label_name: WHILE condition <span class="kwd">DO</span>
  loop_statement_list
END WHILE [label_name];
</pre>

<pre class="lang-sql prettyprint">
label_name: FOR variable IN query <span class="kwd">DO</span>
  loop_statement_list
END FOR [label_name];
</pre>

<pre class="lang-sql prettyprint">
label_name: <span class="kwd">REPEAT</span>
  loop_statement_list
  <span class="kwd">UNTIL</span> boolean_condition
END <span class="kwd">REPEAT</span> [label_name];
</pre>

<pre class="lang-sql prettyprint">
block_statement_list:
  { statement | break_statement_with_label }[, ...]

loop_statement_list:
  { statement | break_continue_statement_with_label }[, ...]

break_statement_with_label:
  { BREAK | <span class="kwd">LEAVE</span> } label_name;

break_continue_statement_with_label:
  { BREAK | <span class="kwd">LEAVE</span> | CONTINUE | <span class="kwd">ITERATE</span> } label_name;
</pre>

**Description**

A BREAK or CONTINUE statement with a label provides an unconditional jump to
the end of the block or loop associated with that label. To use a label with a
block or loop, the label must appear at the beginning of the block or loop, and
optionally at the end.

+ A label name may consist of any GoogleSQL identifier, including the
  use of backticks to include reserved characters or keywords.
+ Multipart path names can be used, but only as quoted identifiers.

  ```none
  `foo.bar`: BEGIN ... END -- Works
  foo.bar: BEGIN ... END -- Doesn't work
  ```
+ Label names are case-insensitive.
+ Each stored procedure has an independent store of label names. For example,
  a procedure may redefine a label already used in a calling procedure.
+ A loop or block may not repeat a label name used in an enclosing loop or
  block.
+ Repeated label names are allowed in non-overlapping parts in
  procedural statements.
+ A label and variable with the same name is allowed.
+ When the `BREAK`, `LEAVE`, `CONTINUE`, or `ITERATE` statement specifies a
  label, it exits or continues the loop matching the label name, rather than
  always picking the innermost loop.

**Examples**

You can only reference a block or loop while inside of it.

<pre class="lang-sql prettyprint">
label_1: BEGIN
  SELECT 1;
  BREAK label_1;
  SELECT 2; -- Unreached
END;
</pre>

<pre class="lang-sql prettyprint">
label_1: <span class="kwd">LOOP</span>
  BREAK label_1;
END <span class="kwd">LOOP</span> label_1;

WHILE x &lt; 1 <span class="kwd">DO</span>
  CONTINUE label_1; -- Error
END WHILE;
</pre>

Repeated label names are allowed in non-overlapping parts of
the multi-statement query. This works:

<pre class="lang-sql prettyprint">
label_1: BEGIN
  BREAK label_1;
END;

label_2: BEGIN
  BREAK label_2;
END;

label_1: BEGIN
  BREAK label_1;
END;
</pre>

A loop or block may not repeat a label name used in an enclosing loop or block.
This throws an error:

<pre class="lang-sql prettyprint">
label_1: BEGIN
   label_1: BEGIN -- Error
     BREAK label_1;
   END;
END;
</pre>

A label and variable can have same name. This works:

<pre class="lang-sql prettyprint">
label_1: BEGIN
   DECLARE label_1 INT64;
   BREAK label_1;
END;
</pre>

The `END` keyword terminating a block or loop may specify a label name, but
this is optional. These both work:

<pre class="lang-sql prettyprint">
label_1: BEGIN
  BREAK label_1;
END label_1;
</pre>

<pre class="lang-sql prettyprint">
label_1: BEGIN
  BREAK label_1;
END;
</pre>

You can't have a label at the end of a block or loop if there isn't a label
at the beginning of the block or loop. This throws an error:

<pre class="lang-sql prettyprint">
BEGIN
  BREAK label_1;
END label_1;
</pre>

In this example, the `BREAK` and `CONTINUE` statements target the outer
`label_1: LOOP`, rather than the inner `WHILE x < 1 DO` loop:

<pre class="lang-sql prettyprint">
label_1: <span class="kwd">LOOP</span>
  WHILE x &lt; 1 <span class="kwd">DO</span>
    IF y &lt; 1 THEN
      CONTINUE label_1;
    ELSE
      BREAK label_1;
  END WHILE;
END <span class="kwd">LOOP</span> label_1
</pre>

A `BREAK`, `LEAVE`, or `CONTINUE`, or `ITERATE` statement that specifies a label
that doesn't exist throws an error:

<pre class="lang-sql prettyprint">
WHILE x &lt; 1 <span class="kwd">DO</span>
  BREAK label_1; -- Error
END WHILE;
</pre>

Exiting a block from within the exception handler section is allowed:

<pre class="lang-sql prettyprint">
label_1: BEGIN
  SELECT 1;
  <span class="kwd">EXCEPTION WHEN ERROR THEN</span>
    BREAK label_1;
    SELECT 2; -- Unreached
END;
</pre>

`CONTINUE` can't be used with a block label. This throws an error:

<pre class="lang-sql prettyprint">
label_1: BEGIN
  SELECT 1;
  CONTINUE label_1; -- Error
  SELECT 2;
END;
</pre>

## Loops

### `LOOP`

**Syntax**

<pre class="lang-sql prettyprint">
<span class="kwd">LOOP</span>
  sql_statement_list
END <span class="kwd">LOOP</span>;
</pre>

**Description**

Executes `sql_statement_list` until a `BREAK` or `LEAVE` statement exits the
loop. `sql_statement_list` is a list of zero or more SQL statements ending with
semicolons.

`LOOP` is restricted from being executed dynamically as a nested element. This
means `LOOP` can't be nested in an `EXECUTE IMMEDIATE` statement.

You can use a label with this statement. To learn more, see [Labels][labels].

**Examples**

The following example declares a variable `x` with the default value 0; then,
it uses the `LOOP` statement to create a loop that executes until the variable
`x` is greater than or equal to 10; after the loop exits, the example
outputs the value of `x`.

<pre class="lang-sql prettyprint">
DECLARE x INT64 DEFAULT 0;
<span class="kwd">LOOP</span>
  SET x = x + 1;
  IF x >= 10 THEN
    LEAVE;
  END IF;
END <span class="kwd">LOOP</span>;
SELECT x;
</pre>

This example outputs the following:

```googlesql
/*----+
 | x  |
 +----+
 | 10 |
 +----*/
```

### `REPEAT`

**Syntax**

<pre class="lang-sql prettyprint">
<span class="kwd">REPEAT</span>
  sql_statement_list
  <span class="kwd">UNTIL</span> boolean_condition
END <span class="kwd">REPEAT</span>;
</pre>

**Description**

Repeatedly executes a list of zero or more SQL statements until the
boolean condition at the end of the list is `TRUE`. The boolean condition
must be an expression. You can exit this loop early with the `BREAK` or `LEAVE`
statement.

`REPEAT` is restricted from being executed dynamically as a nested element. This
means `REPEAT` can't be nested in an `EXECUTE IMMEDIATE` statement.

You can use a label with this statement. To learn more, see [Labels][labels].

**Examples**

The following example declares a variable `x` with the default value `0`; then,
it uses the `REPEAT` statement to create a loop that executes until the variable
`x` is greater than or equal to `3`.

<pre class="lang-sql prettyprint">
DECLARE x INT64 DEFAULT 0;

<span class="kwd">REPEAT</span>
  SET x = x + 1;
  SELECT x;
  <span class="kwd">UNTIL</span> x >= 3
END <span class="kwd">REPEAT</span>;
</pre>

This example outputs the following:

```googlesql
/*---+
 | x |
 +---+
 | 1 |
 +---*/

/*---+
 | x |
 +---+
 | 2 |
 +---*/

/*---+
 | x |
 +---+
 | 3 |
 +---*/
```

### `WHILE`

**Syntax**

<pre class="lang-sql prettyprint">
WHILE boolean_expression <span class="kwd">DO</span>
  sql_statement_list
END WHILE;
</pre>

There is a maximum nesting level of 50 for blocks and conditional statements
such as `BEGIN`/`END`, `IF`/`ELSE`/`END IF`, and `WHILE`/`END WHILE`.

**Description**

While `boolean_expression` is true, executes `sql_statement_list`.
`boolean_expression` is evaluated for each iteration of the loop.

`WHILE` is restricted from being executed dynamically as a nested element. This
means `WHILE` can't be nested in an `EXECUTE IMMEDIATE` statement.

You can use a label with this statement. To learn more, see [Labels][labels].

### `BREAK`

**Syntax**

<pre class="lang-sql prettyprint">
BREAK;
</pre>

**Description**

Exit the current loop.

It's an error to use `BREAK` outside of a loop.

You can use a label with this statement. To learn more, see [Labels][labels].

**Examples**

The following example declares two variables, `heads` and `heads_count`; next,
it initiates a loop, which assigns a random boolean value to `heads`, then
checks to see whether `heads` is true; if so, it outputs "Heads!" and increments
`heads_count`; if not, it outputs "Tails!" and exits the loop; finally, it
outputs a string stating how many times the "coin flip" resulted in "heads."

<pre class="lang-sql prettyprint">
DECLARE heads BOOL;
DECLARE heads_count INT64 DEFAULT 0;
<span class="kwd">LOOP</span>
  SET heads = RAND() &lt; 0.5;
  IF heads THEN
    SELECT 'Heads!';
    SET heads_count = heads_count + 1;
  ELSE
    SELECT 'Tails!';
    BREAK;
  END IF;
END <span class="kwd">LOOP</span>;
SELECT CONCAT(CAST(heads_count AS STRING), ' heads in a row');
</pre>

### `LEAVE`

Synonym for [`BREAK`][break].

### `CONTINUE`

**Syntax**

<pre class="lang-sql prettyprint">
CONTINUE;
</pre>

**Description**

Skip any following statements in the current loop and return to the start of
the loop.

It's an error to use `CONTINUE` outside of a loop.

You can use a label with this statement. To learn more, see [Labels][labels].

**Examples**

The following example declares two variables, `heads` and `heads_count`; next,
it initiates a loop, which assigns a random boolean value to `heads`, then
checks to see whether `heads` is true; if so, it outputs "Heads!", increments
`heads_count`, and restarts the loop, skipping any remaining statements; if not,
it outputs "Tails!" and exits the loop; finally, it outputs a string stating how
many times the "coin flip" resulted in "heads."

<pre class="lang-sql prettyprint">
DECLARE heads BOOL;
DECLARE heads_count INT64 DEFAULT 0;
<span class="kwd">LOOP</span>
  SET heads = RAND() &lt; 0.5;
  IF heads THEN
    SELECT 'Heads!';
    SET heads_count = heads_count + 1;
    CONTINUE;
  END IF;
  SELECT 'Tails!';
  BREAK;
END <span class="kwd">LOOP</span>;
SELECT CONCAT(CAST(heads_count AS STRING), ' heads in a row');
</pre>

### `ITERATE`

Synonym for [`CONTINUE`][continue].

### `FOR...IN` 
<a id="for-in"></a>

**Syntax**

<pre class="lang-sql prettyprint">
FOR loop_variable_name IN (table_expression)
<span class="kwd">DO</span>
  sql_expression_list
END FOR;
</pre>

**Description**

Loops over every row in `table_expression` and assigns the row to
`loop_variable_name`. Inside each loop, the SQL statements in
`sql_expression_list` are executed using the current value of
`loop_variable_name`.

The value of `table_expression` is evaluated once at the start of the loop. On
each iteration, the value of `loop_variable_name` is a `STRUCT` that contains
the top-level columns of the table expression as fields. The order in which
values are assigned to `loop_variable_name` isn't defined, unless the table
expression has a top-level `ORDER BY` clause or `UNNEST` array operator.

The scope of `loop_variable_name` is the body of the loop. The name of
`loop_variable_name` can't conflict with other variables within the same
scope.

You can use a label with this statement. To learn more, see [Labels][labels].

**Example**

<pre class="lang-sql prettyprint">
FOR record IN
  (SELECT word, word_count
   FROM shakespeare
   <span class="kwd">LIMIT</span> 5)
<span class="kwd">DO</span>
  SELECT record.word, record.word_count;
END FOR;
</pre>

## Transactions

### `BEGIN TRANSACTION`

**Syntax**

<pre class="lang-sql prettyprint">
BEGIN [TRANSACTION];
</pre>

**Description**

Begins a transaction.

The transaction ends when a [`COMMIT TRANSACTION`][commit-transaction] or
[`ROLLBACK TRANSACTION`][rollback-transaction] statement is reached. If
execution ends before reaching either of these statements,
an automatic rollback occurs.

**Example**

The following example performs a transaction that selects rows from an
existing table into a temporary table, deletes those rows from the original
table, and merges the temporary table into another table.

<pre class="lang-sql prettyprint">
BEGIN TRANSACTION;

-- Create a temporary table of new arrivals from warehouse #1
CREATE TEMP TABLE tmp AS
SELECT * FROM myschema.NewArrivals WHERE warehouse = 'warehouse #1';

-- Delete the matching records from the original table.
DELETE myschema.NewArrivals WHERE warehouse = 'warehouse #1';

-- Merge the matching records into the Inventory table.
MERGE myschema.Inventory AS I
USING tmp AS T
ON I.product = T.product
WHEN NOT MATCHED THEN
 INSERT(product, quantity, supply_constrained)
 VALUES(product, quantity, false)
WHEN MATCHED THEN
 UPDATE SET quantity = I.quantity + T.quantity;

DROP TABLE tmp;

COMMIT TRANSACTION;
</pre>

### `COMMIT TRANSACTION`

**Syntax**

<pre class="lang-sql prettyprint">
COMMIT [TRANSACTION];
</pre>

**Description**

Commits an open transaction. If no open transaction is in progress, then the
statement fails.

**Example**

<pre class="lang-sql prettyprint">
BEGIN TRANSACTION;

-- SQL statements for the transaction go here.

COMMIT TRANSACTION;
</pre>

### `ROLLBACK TRANSACTION`

**Syntax**

<pre class="lang-sql prettyprint">
ROLLBACK [TRANSACTION];
</pre>

**Description**

Rolls back an open transaction. If there is no open transaction in progress,
then the statement fails.

**Example**

The following example rolls back a transaction if an error occurs during the
transaction. To illustrate the logic, the example triggers a divide-by-zero
error after inserting a row into a table. After these statements run, the table
is unaffected.

<pre class="lang-sql prettyprint">
BEGIN

  BEGIN TRANSACTION;
  INSERT INTO myschema.NewArrivals
    VALUES ('top load washer', 100, 'warehouse #1');
  -- Trigger an error.
  SELECT 1/0;
  COMMIT TRANSACTION;

EXCEPTION WHEN ERROR THEN
  -- Roll back the transaction inside the exception handler.
  SELECT @@error.message;
  ROLLBACK TRANSACTION;
END;
</pre>

## `RAISE`

**Syntax**

<pre class="lang-sql prettyprint">
RAISE [USING MESSAGE = message];
</pre>

**Description**

Raises an error, optionally using the specified error message when `USING
MESSAGE = message` is supplied.

#### When `USING MESSAGE` isn't supplied

The `RAISE` statement must only be used within an `EXCEPTION` clause. The
`RAISE` statement will re-raise the exception that was caught, and preserve the
original stack trace.

#### When `USING MESSAGE` is supplied

If the `RAISE` statement is contained within the `BEGIN` section of a
`BEGIN...EXCEPTION` block:

+ The handler will be invoked.
+ The stack trace will be set to the `RAISE` statement.

If the `RAISE` statement isn't contained within the `BEGIN` section of a
`BEGIN...EXCEPTION` block, the `RAISE` statement stops execution of the
multi-statement query with the error message supplied.

## `RETURN`

`RETURN` stops execution of the multi-statements query.

## `CALL`

**Syntax**

<pre class="lang-sql prettyprint">
<span class="kwd">CALL</span> procedure_name (procedure_argument[, …])
</pre>

**Description**

Calls a [procedure][procedures] with an argument list.
`procedure_argument` may be a variable or an expression.

The maximum depth of procedure calls is 50 frames.

`CALL` is restricted from being executed dynamically as a nested element. This
means `CALL` can't be nested in an `EXECUTE IMMEDIATE` statement.

**Examples**

The following example declares a variable `retCode`. Then, it calls the
procedure `updateSomeTables` in the schema `mySchema`, passing the arguments
`'someAccountId'` and `retCode`. Finally, it returns the value of `retCode`.

<pre class="lang-sql prettyprint">
DECLARE retCode INT64;
-- Procedure signature: (IN account_id STRING, OUT retCode INT64)
<span class="kwd">CALL</span> mySchema.UpdateSomeTables('someAccountId', retCode);
SELECT retCode;
</pre>

<!-- mdlint off(WHITESPACE_LINE_LENGTH) -->

[procedures]: https://github.com/google/googlesql/blob/master/docs/data-definition-language.md#create-procedure

[begin-transaction]: #begin_transaction

[rollback-transaction]: #rollback_transaction

[commit-transaction]: #commit_transaction

[continue]: #continue

[break]: #break

[labels]: #labels

[declare]: #declare

[query-syntax]: https://github.com/google/googlesql/blob/master/docs/query-syntax.md

[functions]: https://github.com/google/googlesql/blob/master/docs/functions-and-operators.md

[conditional-expressions]: https://github.com/google/googlesql/blob/master/docs/conditional_expressions.md

[expression-subqueries]: https://github.com/google/googlesql/blob/master/docs/subqueries.md#expression_subquery_concepts

[ddl]: https://github.com/google/googlesql/blob/master/docs/data-definition-language.md

[dml]: https://github.com/google/googlesql/blob/master/docs/data-manipulation-language.md

<!-- mdlint on -->

