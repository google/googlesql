

<!-- mdlint off(WHITESPACE_LINE_LENGTH) -->

# Utility statements

GoogleSQL supports the following utility statements.

## `EXPLAIN`

<pre>
EXPLAIN query;
</pre>

The `EXPLAIN` statement provides information on how GoogleSQL would
execute a given query. This statement doesn't execute the query.

## `DESCRIBE/DESC`

<pre>
DESCRIBE [object_type] object [FROM source];
DESC [object_type] object [FROM source];
</pre>

The `DESCRIBE` statement provides a description for an object, such as a table.
This statement is analogous to [`EXPLAIN`][explain-statement].
While `EXPLAIN` provides
information on how the query would be executed, `DESCRIBE` provides information
about an object.

The `DESC` statement is the short form of `DESCRIBE`.

This statement includes an optional `FROM` syntax. This syntax can help clarify
ambiguous statements that could match multiple objects.

This statement uses the following variables:

+ `object_type`: An optional type for the object (for example,
  `TABLE`).
+ `object`: Identifies the object.
+ `source`: Identifies the source that contains the object.

**Example**

The following example returns a description of a table named `foo`.

```
DESCRIBE TABLE foo;
```

## `SHOW`

<pre>
SHOW object_type [FROM object] [LIKE pattern];
</pre>

The `SHOW` statement produces a list of objects, usually in a format that
resembles a query result.

This statement uses the following variables:

+ `object_type`: An optional type for the object (for example,
  `TABLE`).
+ `object`: Identifies the object.
+ `pattern`: Provides a pattern, such as a `STRING`, that object names should
  match.

<!-- mdlint off(WHITESPACE_LINE_LENGTH) -->

[explain-statement]: #explain

<!-- mdlint on -->

