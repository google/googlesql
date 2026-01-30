

<!-- mdlint off(WHITESPACE_LINE_LENGTH) -->

# Conditional expressions

GoogleSQL supports conditional expressions.
Conditional expressions impose constraints on the evaluation order of their
inputs. In essence, they are evaluated left to right, with short-circuiting, and
only evaluate the output value that was chosen. In contrast, all inputs to
regular functions are evaluated before calling the function. Short-circuiting in
conditional expressions can be exploited for error handling or performance
tuning.

### Expression list

<table>
  <thead>
    <tr>
      <th>Name</th>
      <th>Summary</th>
    </tr>
  </thead>
  <tbody>

<tr>
  <td><a href="#case_expr"><code>CASE expr</code></a>
</td>
  <td>
    Compares the given expression to each successive <code>WHEN</code> clause
    and produces the first result where the values are equal.
  </td>
</tr>

<tr>
  <td><a href="#case"><code>CASE</code></a>
</td>
  <td>
    Evaluates the condition of each successive <code>WHEN</code> clause and
    produces the first result where the condition evaluates to
    <code>TRUE</code>.
  </td>
</tr>

<tr>
  <td><a href="#coalesce"><code>COALESCE</code></a>
</td>
  <td>
    Produces the value of the first non-<code>NULL</code> expression, if any,
    otherwise <code>NULL</code>.
  </td>
</tr>

<tr>
  <td><a href="#if"><code>IF</code></a>
</td>
  <td>
    If an expression evaluates to <code>TRUE</code>, produces a specified
    result, otherwise produces the evaluation for an <i>else result</i>.
  </td>
</tr>

<tr>
  <td><a href="#ifnull"><code>IFNULL</code></a>
</td>
  <td>
    If an expression evaluates to <code>NULL</code>, produces a specified
    result, otherwise produces the expression.
  </td>
</tr>

<tr>
  <td><a href="#nullif"><code>NULLIF</code></a>
</td>
  <td>
    Produces <code>NULL</code> if the first expression that matches another
    evaluates to <code>TRUE</code>, otherwise returns the first expression.
  </td>
</tr>

<tr>
  <td><a href="#nullifzero"><code>NULLIFZERO</code></a>
</td>
  <td>
    Produces <code>NULL</code> if an expression is <code>0</code>,
    otherwise produces the expression.
  </td>
</tr>

<tr>
  <td><a href="#zeroifnull"><code>ZEROIFNULL</code></a>
</td>
  <td>
    Produces <code>0</code> if an expression is <code>NULL</code>, otherwise
    produces the expression.
  </td>
</tr>

  </tbody>
</table>

### `CASE expr` 
<a id="case_expr"></a>

```googlesql
CASE expr
  WHEN expr_to_match THEN result
  [ ... ]
  [ ELSE else_result ]
  END
```

**Description**

Compares `expr` to `expr_to_match` of each successive `WHEN` clause and returns
the first result where this comparison evaluates to `TRUE`. The remaining `WHEN`
clauses and `else_result` aren't evaluated.

If the `expr = expr_to_match` comparison evaluates to `FALSE` or `NULL` for all
`WHEN` clauses, returns the evaluation of `else_result` if present; if
`else_result` isn't present, then returns `NULL`.

Consistent with [equality comparisons][logical-operators] elsewhere, if both
`expr` and `expr_to_match` are `NULL`, then `expr = expr_to_match` evaluates to
`NULL`, which returns `else_result`. If a CASE statement needs to distinguish a
`NULL` value, then the alternate [CASE][case] syntax should be used.

`expr` and `expr_to_match` can be any type. They must be implicitly
coercible to a common [supertype][cond-exp-supertype]; equality comparisons are
done on coerced values. There may be multiple `result` types. `result` and
`else_result` expressions must be coercible to a common supertype.

This expression supports specifying [collation][collation].

[collation]: https://github.com/google/googlesql/blob/master/docs/collation-concepts.md

**Return Data Type**

[Supertype][cond-exp-supertype] of `result`[, ...] and `else_result`.

**Example**

```googlesql
WITH Numbers AS (
  SELECT 90 as A, 2 as B UNION ALL
  SELECT 50, 8 UNION ALL
  SELECT 60, 6 UNION ALL
  SELECT 50, 10
)
SELECT
  A,
  B,
  CASE A
    WHEN 90 THEN 'red'
    WHEN 50 THEN 'blue'
    ELSE 'green'
    END
    AS result
FROM Numbers

/*------------------+
 | A  | B  | result |
 +------------------+
 | 90 | 2  | red    |
 | 50 | 8  | blue   |
 | 60 | 6  | green  |
 | 50 | 10 | blue   |
 +------------------*/
```

[logical-operators]: https://github.com/google/googlesql/blob/master/docs/operators.md#logical_operators

[case]: #case

[cond-exp-supertype]: https://github.com/google/googlesql/blob/master/docs/conversion_rules.md#supertypes

### `CASE` 
<a id="case"></a>

```googlesql
CASE
  WHEN condition THEN result
  [ ... ]
  [ ELSE else_result ]
  END
```

**Description**

Evaluates the condition of each successive `WHEN` clause and returns the
first result where the condition evaluates to `TRUE`; any remaining `WHEN`
clauses and `else_result` aren't evaluated.

If all conditions evaluate to `FALSE` or `NULL`, returns evaluation of
`else_result` if present; if `else_result` isn't present, then returns `NULL`.

For additional rules on how values are evaluated, see the
three-valued logic table in [Logical operators][logical-operators].

`condition` must be a boolean expression. There may be multiple `result` types.
`result` and `else_result` expressions must be implicitly coercible to a common
[supertype][cond-exp-supertype].

This expression supports specifying [collation][collation].

[collation]: https://github.com/google/googlesql/blob/master/docs/collation-concepts.md

**Return Data Type**

[Supertype][cond-exp-supertype] of `result`[, ...] and `else_result`.

**Example**

```googlesql
WITH Numbers AS (
  SELECT 90 as A, 2 as B UNION ALL
  SELECT 50, 6 UNION ALL
  SELECT 20, 10
)
SELECT
  A,
  B,
  CASE
    WHEN A > 60 THEN 'red'
    WHEN B = 6 THEN 'blue'
    ELSE 'green'
    END
    AS result
FROM Numbers

/*------------------+
 | A  | B  | result |
 +------------------+
 | 90 | 2  | red    |
 | 50 | 6  | blue   |
 | 20 | 10 | green  |
 +------------------*/
```

[cond-exp-supertype]: https://github.com/google/googlesql/blob/master/docs/conversion_rules.md#supertypes

[logical-operators]: https://github.com/google/googlesql/blob/master/docs/operators.md#logical_operators

### `COALESCE` 
<a id="coalesce"></a>

```googlesql
COALESCE(expr[, ...])
```

**Description**

Returns the value of the first non-`NULL` expression, if any, otherwise
`NULL`. The remaining expressions aren't evaluated. An input expression can be
any type. There may be multiple input expression types.
All input expressions must be implicitly coercible to a common
[supertype][cond-exp-supertype].

**Return Data Type**

[Supertype][cond-exp-supertype] of `expr`[, ...].

**Examples**

```googlesql
SELECT COALESCE('A', 'B', 'C') as result

/*--------+
 | result |
 +--------+
 | A      |
 +--------*/
```

```googlesql
SELECT COALESCE(NULL, 'B', 'C') as result

/*--------+
 | result |
 +--------+
 | B      |
 +--------*/
```

[cond-exp-supertype]: https://github.com/google/googlesql/blob/master/docs/conversion_rules.md#supertypes

### `IF` 
<a id="if"></a>

```googlesql
IF(expr, true_result, else_result)
```

**Description**

If `expr` evaluates to `TRUE`, returns `true_result`, else returns the
evaluation for `else_result`. `else_result` isn't evaluated if `expr` evaluates
to `TRUE`. `true_result` isn't evaluated if `expr` evaluates to `FALSE` or
`NULL`.

`expr` must be a boolean expression. `true_result` and `else_result`
must be coercible to a common [supertype][cond-exp-supertype].

**Return Data Type**

[Supertype][cond-exp-supertype] of `true_result` and `else_result`.

**Examples**

```googlesql
SELECT
  10 AS A,
  20 AS B,
  IF(10 < 20, 'true', 'false') AS result

/*------------------+
 | A  | B  | result |
 +------------------+
 | 10 | 20 | true   |
 +------------------*/
```

```googlesql
SELECT
  30 AS A,
  20 AS B,
  IF(30 < 20, 'true', 'false') AS result

/*------------------+
 | A  | B  | result |
 +------------------+
 | 30 | 20 | false  |
 +------------------*/
```

[cond-exp-supertype]: https://github.com/google/googlesql/blob/master/docs/conversion_rules.md#supertypes

### `IFNULL` 
<a id="ifnull"></a>

```googlesql
IFNULL(expr, null_result)
```

**Description**

If `expr` evaluates to `NULL`, returns `null_result`. Otherwise, returns
`expr`. If `expr` doesn't evaluate to `NULL`, `null_result` isn't evaluated.

`expr` and `null_result` can be any type and must be implicitly coercible to
a common [supertype][cond-exp-supertype]. Synonym for
`COALESCE(expr, null_result)`.

**Return Data Type**

[Supertype][cond-exp-supertype] of `expr` or `null_result`.

**Examples**

```googlesql
SELECT IFNULL(NULL, 0) as result

/*--------+
 | result |
 +--------+
 | 0      |
 +--------*/
```

```googlesql
SELECT IFNULL(10, 0) as result

/*--------+
 | result |
 +--------+
 | 10     |
 +--------*/
```

[cond-exp-supertype]: https://github.com/google/googlesql/blob/master/docs/conversion_rules.md#supertypes

### `NULLIF` 
<a id="nullif"></a>

```googlesql
NULLIF(expr, expr_to_match)
```

**Description**

Returns `NULL` if `expr = expr_to_match` evaluates to `TRUE`, otherwise
returns `expr`.

`expr` and `expr_to_match` must be implicitly coercible to a
common [supertype][cond-exp-supertype], and must be comparable.

This expression supports specifying [collation][collation].

[collation]: https://github.com/google/googlesql/blob/master/docs/collation-concepts.md

**Return Data Type**

[Supertype][cond-exp-supertype] of `expr` and `expr_to_match`.

**Example**

```googlesql
SELECT NULLIF(0, 0) as result

/*--------+
 | result |
 +--------+
 | NULL   |
 +--------*/
```

```googlesql
SELECT NULLIF(10, 0) as result

/*--------+
 | result |
 +--------+
 | 10     |
 +--------*/
```

[cond-exp-supertype]: https://github.com/google/googlesql/blob/master/docs/conversion_rules.md#supertypes

### `NULLIFZERO` 
<a id="nullifzero"></a>

```googlesql
NULLIFZERO(expr)
```

**Description**

Returns `NULL` if the value of `expr` is `0`. Otherwise, returns `expr`. `expr`
must be a zeroable type.

**Return Data Type**

Type of `expr`.

**Example**

```googlesql
SELECT NULLIFZERO(0) AS result

/*--------+
 | result |
 +--------+
 | NULL   |
 +--------*/
```

### `ZEROIFNULL` 
<a id="zeroifnull"></a>

```googlesql
ZEROIFNULL(expr)
```

**Description**

Returns `0` if the value of `expr` is `NULL`. Otherwise, returns `expr`. `expr`
must be a zeroable type.

**Return Data Type**

Type of `expr`.

**Example**

```googlesql
SELECT ZEROIFNULL(NULL) AS result

/*--------+
 | result |
 +--------+
 | 0      |
 +--------*/
```

