This directory contains some example scripts demonstrating
[SQL pipe syntax](https://github.com/google/googlesql/blob/master/docs/pipe-syntax.md).

These mostly use tables from the TPC-H schema. For more information, see
[examples/tpch](https://github.com/google/googlesql/tree/master/googlesql/examples/tpch)
which also includes standard TPC-H queries in standard syntax and pipe syntax.

The scripts here can be run in the `execute_query` tool by selecting the `tpch`
catalog in the web UI with `--web` or using `--catalog=tpch` flag on the command
line.
See [execute\_query](https://github.com/google/googlesql/blob/master/execute_query.md) for more details on this tool.

Examples with TVFs depend on the `REWRITE_INLINE_SQL_TVFS` rewriter, which is
in-development and may return incorrect results. This rewriter must be enabled
manually when invoking execute_query, for example:
`--enabled_ast_rewrites=ALL_MINUS_DEV,+INLINE_SQL_TVFS`
<!-- TODO: b/202167428 - Remove note once this rewriter is out of development.

For more information on pipe query syntax, see the
[reference documentation](https://github.com/google/googlesql/blob/master/docs/pipe-syntax.md)
and [research paper](https://research.google/pubs/pub1005959/).
-->
