This directory contains examples using the TPC-H schema and dataset.

The `catalog/` directory implements a `googlesql::Catalog` containing TPC-H
tables, including a generated 1MB dataset.

These tables can be queried using the `execute_query` tool (in
`tools/execute_query`) by using `--catalog=tpch` or selecting the `tpch`
catalog in the web UI (running with `--web`).

There are examples in these files:

*   [`describe.sql`](describe.sql): `DESCRIBE` statements to show the TPC-H
    schemas.

*   [`describe.txt`](describe.txt): Output of those `DESCRIBE`s, showing the
    table schemas.

*   `queries/*.sql`: The standard TPC-H benchmark queries. A couple are slightly
    modified to produce more output and to run faster on the reference
    implementation.

*   [`all_queries.sql`](all_queries.sql): `queries/*.sql` in one file.

*   `pipe_queries/*.sql`: The TPCH-H queries rewritten using pipe syntax.

*   [`all_pipe_queries.sql`](all_pipe_queries.sql): `pipe_queries/*.sql` in one
    file.

The pipe syntax queries were converted by hand.

* They still do joins mostly using comma joins with conditions in the `WHERE`
  clause like the original queries.
* Query 13 is the only that used `JOIN` syntax (because it has an outer join).
* TPC-H tables can't use `JOIN USING` because the corresponding column names are
  different in every table - not a recommended style.

Some good examples of queries simplified with pipe syntax:

*   [`1.sql`](pipe_queries/1.sql): A simple aggregate query with grouping and
    ordering collapses to just an `AGGREGATE` call using `GROUP AND ORDER BY`
    shorthand.

*   [`4.sql`](pipe_queries/4.sql): The simple outer aggregate involved repeating
    columns across `SELECT`, `GROUP BY` and `ORDERY`. In pipe syntax, all of
    that is done just once with `AGGREGATE`, using `GROUP AND ORDER BY`. Also,
    the `EXISTS` subquery is written without a placeholder `SELECT` list.

*   [`5.sql`](pipe_queries/5.sql): Uses the `DESC` ordering shorthand in
    `AGGREGATE` instead of repeating columns in `ORDER BY`.

*   [`7.sql`](pipe_queries/7.sql): Removes a subquery in favor of linear flow,
    with `SELECT` to compute expressions before the `AGGREGATE`. The final
    `SELECT` is unnecessary since it just selects the grouping and aggregate
    columns with `AGGREGATE` already produces.

*   [`8.sql`](pipe_queries/8.sql): The subquery is unnecessary. Linear pipe flow
    works without it.  Selecting `n2.n_name` and aliasing it is unnecessary
    since the original columns (and their table aliases) are still present after
    using `EXTEND` to compute expressions.

*   [`9.sql`](pipe_queries/9.sql): Removes unnecessary subquery and internal
    `SELECT` and aliasing of `n_name` column by using `EXTEND` to compute
    expressions.

*   [`11.sql`](pipe_queries/11.sql): `HAVING` is replaced by just a `WHERE`
    after the aggregate. It can use the `value` column rather than repeating the
    aggregate expression.

*   [`13.sql`](pipe_queries/13.sql): This is much shorter because it can apply
    `AGGREGATE` twice for two-level aggregation without needing a subquery. This
    uses pipe `JOIN`.  Using `JOIN` inside `FROM` would also work.

*   [`15.sql`](pipe_queries/15.sql): Has a `WITH` clause, combined with pipe
    syntax queries.

*   [`21.sql`](pipe_queries/21.sql): `EXISTS` subqueries omit the placeholder
    `SELECT`s.

*   [`22.sql`](pipe_queries/22.sql): A subquery isn't needed to compute an
    expression column so it can be referenced multiple times. It can just be
    computed directly in `GROUP BY` and given an alias.
