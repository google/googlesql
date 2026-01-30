

<!-- mdlint off(WHITESPACE_LINE_LENGTH) -->

# Debugging statements

GoogleSQL supports the following debugging statements.

## `ASSERT`

<pre>
ASSERT expression [AS description]
</pre>

**Description**

Evaluates `expression`.

If `expression` evaluates to `TRUE`, the statement returns successfully
without any result.

If `expression` evaluates to `FALSE` or `NULL`, the statement generates an
error. If `AS description` is present, `description` will appear in the error
message.

`expression` must evaluate to a `BOOL`.

`description` must be a `STRING` literal.

**Examples**

The following examples assert that the data source contains more than a specific
number of rows.

```googlesql
-- This query succeeds and no error is produced.
ASSERT (
  (SELECT COUNT(*) > 5 FROM UNNEST([1, 2, 3, 4, 5, 6]))
) AS 'Table must contain more than 5 rows.';
```

```googlesql
-- Error: Table must contain more than 10 rows.
ASSERT (
  (SELECT COUNT(*) > 10 FROM UNNEST([1, 2, 3, 4, 5, 6]))
) AS 'Table must contain more than 10 rows.';
```

The following examples assert that the data source contains a particular value.

```googlesql
-- This query succeeds and no error is produced.
ASSERT
  EXISTS(
    (SELECT X FROM UNNEST([7877, 7879, 7883, 7901, 7907]) AS X WHERE X = 7907))
AS 'Column X must contain the value 7907.';
```

```googlesql
-- Error: Column X must contain the value 7919.
ASSERT
  EXISTS(
    (SELECT X FROM UNNEST([7877, 7879, 7883, 7901, 7907]) AS X WHERE X = 7919))
AS 'Column X must contain the value 7919';
```

<!-- mdlint off(WHITESPACE_LINE_LENGTH) -->

<!-- mdlint on -->

