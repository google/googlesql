

<!-- mdlint off(WHITESPACE_LINE_LENGTH) -->

# User-defined aggregate functions 
<a id="udas"></a>

GoogleSQL supports user-defined aggregate functions (UDAs).

A SQL UDA is a user-defined function that
performs a calculation on a group of rows at a time and returns the result of
that calculation as a single value.

The arguments represent a column of the input group such that each row in the
group has a value for that column.

## Create a SQL UDA

You can create a UDA using the following syntax:

```googlesql
CREATE
  [ OR REPLACE ]
  [ { TEMPORARY | TEMP } ] AGGREGATE FUNCTION
  [ IF NOT EXISTS ]
  function_name ( [ function_parameter [, ...] ] )
  [ RETURNS data_type ]
  AS ( function_body )

function_parameter:
  parameter_name
  { data_type | ANY TYPE }
  [ DEFAULT default_value ]
  [ NOT AGGREGATE ]
```

This syntax consists of the following components:

+ `CREATE ... FUNCTION`: Creates a new function. A function
  can have zero or more function parameters.

    + `TEMPORARY` or `TEMP`: Indicates that the function is temporary, meaning
      that it exists for the lifetime of the session. A temporary function can
      have the same name as a built-in function. If this happens, the
      temporary function hides the built-in function for the duration of the
      temporary function's lifetime.
+ `OR REPLACE`: Replaces any function with the same name if it exists.
      Can't appear with `IF NOT EXISTS`.
+ `IF NOT EXISTS`: If any function exists with the same name, the `CREATE`
      statement has no effect. Can't appear with `OR REPLACE`.
+ `function_name`: The name of the function.
+ `function_parameter`: A parameter for the function.

    + `parameter_name`: The name of the function parameter.

    + `data_type`: A GoogleSQL [data type][data-types].

    
    + `ANY TYPE`: The function will accept an argument of any type for this
      function parameter. If more than one parameter includes `ANY TYPE`,
      a relationship isn't enforced between these parameters when the function
      is defined. However, if the type of argument passed into the function at
      call time is incompatible with the function definition, this will
      result in an error.

      `ANY TYPE` is a [_templated function parameter_][templated-parameters].
    

    
    + `DEFAULT default_value`: If an argument isn't provided for a function
      parameter, `default_value` is used. `default_value` must be a literal
      or `NULL` value. All function parameters following this one
      must also have default values.
    

    
    + `NOT AGGREGATE`: Specifies that a function parameter isn't an
      aggregate. A non-aggregate function parameter can appear anywhere in the
      function definition. You can learn more about UDAF parameters
      [here][aggregate-udf-parameters].
    
+ `RETURNS data_type`: Optional clause that specifies the data type
  that the function returns. GoogleSQL infers the result type
  of the function from the SQL function body when the `RETURN` clause is
  omitted.
+ `function_body`: The SQL expression that defines the function body.

[quoted-literals]: https://github.com/google/googlesql/blob/master/docs/lexical.md#quoted_literals

## Call a SQL UDA

You can call a SQL UDA in the same way that you call a built-in
aggregate function. For details, see [Function calls][function-calls].

## UDA function parameters 
<a id="aggregate_udf_parameters"></a>

A user-defined aggregate (UDA) function can include aggregate or non-aggregate
function parameters. Like other [aggregate functions][aggregate-fns-link],
UDA functions normally aggregate function parameters across all rows in a
[group][group-by-link]. However, you can specify a function parameter as
non-aggregate with the `NOT AGGREGATE` keyword.

A non-aggregate function parameter is a scalar function parameter with a
constant value for all rows in a group. Valid non-aggregate function parameters
include literals, constants, query parameters, and any references to the
function parameters of a user-defined function. Inside the UDA definition,
aggregate function parameters can only appear as function arguments to aggregate
function calls. References to non-aggregate function parameters can appear
anywhere in the UDA definition.

## SQL UDA examples

The following example shows a SQL UDA that includes a non-aggregate
function parameter. Inside the function definition, the aggregate `SUM` method
takes the aggregate function parameter `dividend`, while the non-aggregate
division operator ( `/` ) takes the non-aggregate function parameter `divisor`.

```googlesql
CREATE TEMP AGGREGATE FUNCTION ScaledSum(
  dividend DOUBLE,
  divisor DOUBLE NOT AGGREGATE)
RETURNS DOUBLE
AS (
  SUM(dividend) / divisor
);

SELECT ScaledSum(col1, 2) AS scaled_sum
FROM (
  SELECT 1 AS col1 UNION ALL
  SELECT 3 AS col1 UNION ALL
  SELECT 5 AS col1
);

/*------------+
 | scaled_sum |
 +------------+
 | 4.5        |
 +------------*/
```

## Templated function parameters

A templated function parameter can match more than one argument type at
function call time. If a function signature includes a
templated function parameter, GoogleSQL allows function calls
to pass to the function any argument type as long as the function body is
valid for that argument type.

<!-- mdlint off(WHITESPACE_LINE_LENGTH) -->

[templated-parameters]: #templated_function_parameters

[data-types]: https://github.com/google/googlesql/blob/master/docs/data-types.md

[function-calls]: https://github.com/google/googlesql/blob/master/docs/functions-reference.md

[aggregate-udf-parameters]: #aggregate_udf_parameters

[group-by-link]: https://github.com/google/googlesql/blob/master/docs/query-syntax.md#group_by_clause

[aggregate-fns-link]: https://github.com/google/googlesql/blob/master/docs/aggregate-function-calls.md

<!-- mdlint on -->

