

<!-- mdlint off(WHITESPACE_LINE_LENGTH) -->

# Subqueries

GoogleSQL supports subqueries.

## About subqueries

A subquery is a [query][subqueries-query-syntax] that appears inside another
query statement. Subqueries are also referred to as sub-`SELECT`s or
nested `SELECT`s. The full `SELECT` syntax is valid in subqueries.

## Expression subqueries 
<a id="expression_subquery_concepts"></a>

Expression subqueries are used in
a query wherever expressions are valid. They return a single value, as opposed
to a column or table. Expression subqueries can be
[correlated][correlated_subquery_concepts].

### Scalar subqueries 
<a id="scalar_subquery_concepts"></a>

<pre>
( subquery )
</pre>

**Description**

A subquery inside an expression is interpreted as a scalar subquery.
Scalar subqueries are often used in the `SELECT` list or `WHERE` clause.

A scalar subquery must select a single column. Trying to select multiple
columns will result in an analysis error. A `SELECT` list with a single
expression is the simplest way to select a single column. The result type
of the scalar subquery is the type of that expression.

Another possibility is to use `SELECT AS STRUCT` to define a subquery that
selects a single `STRUCT` type value whose fields are defined by one or more
expressions. `SELECT AS ProtocolBufferName`
can also be used to define a subquery that selects a single `PROTO` value
of the specified type where one or more expressions define its
fields.

If the subquery returns exactly one row, that single value is the
scalar subquery result. If the subquery returns zero rows, the result is `NULL`.
If the subquery returns more than one row, the query fails with a runtime error.

**Examples**

In this example, a correlated scalar subquery returns the mascots for a list of
players, using the [`Players`][example-tables] and [`Mascots`][example-tables]
tables:

```googlesql
SELECT
  username,
  (SELECT mascot FROM Mascots WHERE Players.team = Mascots.team) AS player_mascot
FROM
  Players;

/*---------------------------+
 | username  | player_mascot |
 +---------------------------+
 | gorbie    | cardinal      |
 | junelyn   | finch         |
 | corba     | parrot        |
 +---------------------------*/
```

In this example, an aggregate scalar
subquery calculates `avg_level`, the average level of a user in the
[`Players`][example-tables] table.

```googlesql {highlight="lines:1:24-1:55"}
SELECT
  username,
  level,
  (SELECT AVG(level) FROM Players) AS avg_level
FROM
  Players;

/*---------------------------------------+
 | username  | level      | avg_level    |
 +---------------------------------------+
 | gorbie    | 29         | 24.66        |
 | junelyn   | 2          | 24.66        |
 | corba     | 43         | 24.66        |
 +---------------------------------------*/
```

### Array subqueries 
<a id="array_subquery_concepts"></a>

<pre>
ARRAY ( subquery )
</pre>

**Description**

An ARRAY subquery is a special case of expression subquery, in that it returns
an ARRAY. If the subquery returns zero
rows, returns an empty ARRAY.
Never returns a `NULL` ARRAY.

The `SELECT` list in an ARRAY subquery must have exactly one column of
any type, which defines the element type of the array returned by the
array subquery. If not, an error is returned. When the subquery is written with
`SELECT AS STRUCT` or `SELECT AS ProtocolBufferName`,
the `SELECT` list can include multiple columns, and the value returned by
the array subquery is an ARRAY of the constructed
STRUCTs or PROTOs.
Selecting multiple columns without using `SELECT AS` is an error.

ARRAY subqueries can use `SELECT AS STRUCT` to build
arrays of structs.
ARRAY subqueries can use `SELECT AS ProtocolBufferName` to build arrays
of PROTOs.

See [Array functions][array-function] for full semantics.

**Examples**

In this example, an ARRAY subquery returns an array of usernames assigned to the
red team in the [`NPCs`][example-tables] table:

```googlesql {highlight="range:ARRAY,)"}
SELECT
  ARRAY(SELECT username FROM NPCs WHERE team = 'red') AS red;

/*-----------------+
 | red             |
 +-----------------+
 | [niles,jujul]   |
 +-----------------*/
```

### `IN` subqueries 
<a id="in_subquery_concepts"></a>

<pre>
value [ NOT ] IN ( subquery )
</pre>

**Description**

Returns TRUE if `value` is in the set of rows returned by the subquery.
Returns FALSE if the subquery returns zero rows.

The subquery's SELECT list must have a single column of any type and
its type must be comparable to the type for `value`. If not, an error is
returned. For full semantics, including `NULL` handling, see the
[`IN` operator][in-operator].

If you need to use an `IN` subquery with an array, these are equivalent:

```googlesql
value [ NOT ] IN ( subquery )
value [ NOT ] IN UNNEST( ARRAY( subquery ) )
```

**Examples**

In this example, the `IN` operator that checks to see if a username called
`corba` exists within the [`Players`][example-tables] table:

```googlesql {highlight="lines:1:8-1:47"}
SELECT
  'corba' IN (SELECT username FROM Players) AS result;

/*--------+
 | result |
 +--------+
 | TRUE   |
 +--------*/
```

### Quantified `LIKE` subqueries 
<a id="quantified_like_subquery_concepts"></a>

```googlesql
value [ NOT ] LIKE { ANY | SOME | ALL } ( subquery )
```

**Description**

A subquery that can be used in a quantified `LIKE` operation. `value` is a
`STRING` or `BYTES` type. The `SELECT` list for `subquery`
must have a single column and its type must be comparable to the type
for `value`. If not, an error is returned. For full semantics, including
`NULL` handling, see the [Quantified LIKE operator][quantified-like].

**Examples**

In the example, the quantified `LIKE` operator checks to see if `corba` matches
at least one pattern in the set of rows returned by the `LIKE ANY` subquery:

```googlesql {highlight="lines:1:8-1:53"}
WITH Words AS (
  SELECT '%orb%' as chars UNION ALL
  SELECT 'xyg'
)
SELECT 'corba' LIKE ANY (SELECT chars FROM Words) as result;

+--------+
| result |
+--------+
| TRUE   |
+--------+
```

### `EXISTS` subqueries 
<a id="exists_subquery_concepts"></a>

<pre>
EXISTS( subquery )
</pre>

**Description**

Returns TRUE if the subquery produces one or more rows. Returns FALSE if the
subquery produces zero rows. Never returns `NULL`. Unlike all other
expression subqueries, there are no rules about the column list.
Any number of columns may be selected and it will not affect the query result.

**Examples**

In this example, the `EXISTS` operator that checks to see if any rows are
produced, using the [`Players`][example-tables] table:

```googlesql {highlight="range:EXISTS,)"}
SELECT
  EXISTS(SELECT username FROM Players WHERE team = 'yellow') AS result;

/*--------+
 | result |
 +--------+
 | FALSE  |
 +--------*/
```

## Table subqueries 
<a id="table_subquery_concepts"></a>

<pre>
FROM ( subquery ) [ [ AS ] alias ]
</pre>

**Description**

With table subqueries, the outer query treats the result of the subquery as a
table. You can only use these in the `FROM` clause.

**Examples**

In this example, a subquery returns a table of usernames from the
[`Players`][example-tables] table:

```googlesql {highlight="range:(,)"}
SELECT results.username
FROM (SELECT * FROM Players) AS results;

/*-----------+
 | username  |
 +-----------+
 | gorbie    |
 | junelyn   |
 | corba     |
 +-----------*/
```

In this example, a list of [`NPCs`][example-tables]
assigned to the red team are returned.

```googlesql
SELECT
  username
FROM (
  WITH red_team AS (SELECT * FROM NPCs WHERE team = 'red')
  SELECT * FROM red_team
);

/*-----------+
 | username  |
 +-----------+
 | niles     |
 | jujul     |
 +-----------*/
```

## Correlated subqueries 
<a id="correlated_subquery_concepts"></a>

A correlated subquery is a subquery that references a column from outside that
subquery. Correlation prevents reusing of the subquery result. You can learn
more about this [here][evaluation-rules-subqueries].

**Examples**

In this example, a list of mascots that don't have any players assigned to them
are returned. The [`Mascots`][example-tables] and [`Players`][example-tables]
tables are referenced.

```googlesql
SELECT mascot
FROM Mascots
WHERE
  NOT EXISTS(SELECT username FROM Players WHERE Mascots.team = Players.team);

/*----------+
 | mascot   |
 +----------+
 | sparrow  |
 +----------*/
```

In this example, a correlated scalar subquery returns the mascots for a list of
players, using the [`Players`][example-tables] and [`Mascots`][example-tables]
tables:

```googlesql
SELECT
  username,
  (SELECT mascot FROM Mascots WHERE Players.team = Mascots.team) AS player_mascot
FROM Players;

/*---------------------------+
 | username  | player_mascot |
 +---------------------------+
 | gorbie    | cardinal      |
 | junelyn   | finch         |
 | corba     | parrot        |
 +---------------------------*/
```

## Volatile subqueries

A volatile subquery is a subquery that doesn't always produce the same result
over the same inputs. For example, if a subquery includes a function
that returns a random number, the subquery is volatile because the result
isn't always the same.

**Examples**

In this example, a random number of usernames are returned from the
[`Players`][example-tables] table.

```googlesql
SELECT
  results.username
FROM
  (SELECT * FROM Players WHERE RAND() < 0.5) AS results;

-- The results aren't always the same when you execute
-- the preceding query, but will look similar to this:
/*----------+
 | username |
 +----------+
 | gorbie   |
 | junelyn  |
 +----------*/
```

## Evaluation rules for subqueries 
<a id="evaluation_rules_subqueries"></a>

Some subqueries are evaluated once, others more often.

*  A non-correlated, volatile subquery may be re-evaluated once per
   row, depending on your [query plan][query-plan].
*  A non-correlated, non-volatile subquery can be evaluated once and reused,
   but may be evaluated as many times as once per row depending on your
   query plan and execution engine.
*  A correlated subquery must be logically re-evaluated for every distinct set
   of parameter values. Depending on your query plan, a correlated
   subquery may be re-evaluated once per row, even if multiple
   rows have the same parameter values.
*  A subquery assigned to a temporary table by `WITH` is evaluated "as-if" once.
   A query plan may only re-evaluate the subquery if re-evaluating
   it's guaranteed to produce the same table each time.

## Common tables used in examples 
<a id="example_tables"></a>

Some examples reference a table called `Players`:

```googlesql
/*-----------------------------+
 | username  | level   | team  |
 +-----------------------------+
 | gorbie    | 29      | red   |
 | junelyn   | 2       | blue  |
 | corba     | 43      | green |
 +-----------------------------*/
```

Some examples reference a table called `NPCs`:

```googlesql
/*-------------------+
 | username  | team  |
 +-------------------+
 | niles     | red   |
 | jujul     | red   |
 | effren    | blue  |
 +-------------------*/
```

Some examples reference a table called `Mascots`:

```googlesql
/*-------------------+
 | mascot   | team   |
 +-------------------+
 | cardinal | red    |
 | parrot   | green  |
 | finch    | blue   |
 | sparrow  | yellow |
 +-------------------*/
```

You can use this `WITH` clause to emulate temporary table names for
`Players` and `NPCs`
in subqueries that support the `WITH` clause.:

```googlesql
WITH
  Players AS (
    SELECT 'gorbie' AS username, 29 AS level, 'red' AS team UNION ALL
    SELECT 'junelyn', 2 , 'blue' UNION ALL
    SELECT 'corba', 43, 'green'),
  NPCs AS (
    SELECT 'niles' AS username, 'red' AS team UNION ALL
    SELECT 'jujul', 'red' UNION ALL
    SELECT 'effren', 'blue'),
  Mascots AS (
    SELECT 'cardinal' AS mascot , 'red' AS team UNION ALL
    SELECT 'parrot', 'green' UNION ALL
    SELECT 'finch', 'blue' UNION ALL
    SELECT 'sparrow', 'yellow')
SELECT * FROM (
  SELECT username, team FROM Players UNION ALL
  SELECT username, team FROM NPCs);
```

<!-- mdlint off(WHITESPACE_LINE_LENGTH) -->

[query-plan]: https://en.wikipedia.org/wiki/Query_plan

[about-subqueries]: #about_subqueries

[evaluation-rules-subqueries]: #evaluation_rules_subqueries

[example-tables]: #example_tables

[correlated_subquery_concepts]: #correlated_subquery_concepts

[subqueries-query-syntax]: https://github.com/google/googlesql/blob/master/docs/query-syntax.md

[in-operator]: https://github.com/google/googlesql/blob/master/docs/operators.md#in_operators

[array-function]: https://github.com/google/googlesql/blob/master/docs/array_functions.md#array

[aggregate-functions]: https://github.com/google/googlesql/blob/master/docs/aggregate_functions.md

[semantic-rules-in]: https://github.com/google/googlesql/blob/master/docs/operators.md#semantic_rules_in

[quantified-like]: https://github.com/google/googlesql/blob/master/docs/operators.md#like_operator_quantified

[semantic-rules-like]: https://github.com/google/googlesql/blob/master/docs/operators.md#semantic_rules_like

[quantified-like]: https://github.com/google/googlesql/blob/master/docs/operators.md#like_operator_quantified

<!-- mdlint on -->

