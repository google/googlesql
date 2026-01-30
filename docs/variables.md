

<!-- mdlint off(WHITESPACE_LINE_LENGTH) -->

# Variables

GoogleSQL supports three types of variables:

+   System variables: Defined by a client or an engine to expose configuration.
+   Query parameters: Defined by a user to bind typed values into a query.
+   Runtime variables: Defined by a user to track state in a script.

The implementation that you use determines which variable types are supported
and the way you set the variables.

## System variables

System variables are defined by a client or an engine to expose some state or
configuration. System variables are prefixed with a double `@@` symbol, and must
be one or more SQL [identifiers][sql-identifiers] separated by periods.

Because system variables are defined by each implementation and not by the
GoogleSQL language, see the documentation for your implementation to
determine the names, types, and behavior of available system variables.

**Syntax**

```googlesql
SET @@system_variable = expression;
```

**Examples**

```googlesql
-- Set the system variable `@@system_var_a` to have the literal STRING value
-- `"TEST"`.
SET @@system_var_a = "TEST";

-- Set the system variable `@@Request.system_var_b` to have the value of the
-- expression `1+2+3`.
SET @@Request.system_var_b = 1+2+3;

-- Reference the system variable `@@system_var_c` from a query. Whether system
-- variables can be read in this way depends on the implementation you use.
SELECT @@system_var_c;
```

## Query parameters

Query parameters are defined by a user as part of a query or request, and are
used to bind typed values into a query. Query parameters are prefixed with a
single `@` symbol.

**Syntax**

```googlesql
SET @query_parameter = expression;
```

**Examples**

```googlesql
-- Set the query parameter `@query_parameter_a` to have the value of the
-- expression `1`.
SET @query_parameter_a = 1;

-- Set the query parameter `@query_parameter_b` to have the value of an array
-- result from a scalar subquery.
SET @query_parameter_b = (SELECT ARRAY_AGG(country) FROM countries_t_able);

-- Reference the query parameters in a subsequent query.
SELECT *
FROM my_table
WHERE
  total_count > @query_parameter_a
  AND country IN UNNEST(@query_parameter_b)
```

## Runtime variables

Runtime variables are defined and set by a user to track state in a
GoogleSQL [procedural language][procedural-language] script. You must
first declare a runtime variable using a [`DECLARE`][declare] statement before
you can set the variable.

**Syntax**

```googlesql
DECLARE runtime_variable [variable_type] [DEFAULT expression];
SET runtime_variable = expression;
SET (variable1, variable2, ...) = struct_expression;
```

**Examples**

```googlesql
-- Declare two runtime variables: `target_word` and `corpus_count`.
DECLARE target_word STRING DEFAULT 'methinks';
DECLARE corpus_count, word_count INT64;

-- Set the variables by assigning the results of a `SELECT AS STRUCT` query to
-- the two variables.
SET (corpus_count, word_count) = (
  SELECT AS STRUCT COUNT(DISTINCT corpus), SUM(word_count)
  FROM shakespeare
  WHERE LOWER(word) = target_word
);

-- Reference the runtime variables in a subsequent query.
SELECT
  FORMAT('Found %d occurrences of "%s" across %d Shakespeare works',
         word_count, target_word, corpus_count) AS result;
```

<!-- mdlint off(WHITESPACE_LINE_LENGTH) -->

[sql-identifiers]: https://github.com/google/googlesql/blob/master/docs/lexical.md#identifiers

[procedural-language]: https://github.com/google/googlesql/blob/master/docs/procedural-language.md

[declare]: https://github.com/google/googlesql/blob/master/docs/procedural-language.md#declare

<!-- mdlint on -->

