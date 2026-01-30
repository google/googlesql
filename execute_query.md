## Execute Query

`execute_query` is a tool to parse, analyze and run SQL queries using the
reference implementation.

You can run a query by supplying it directly on the command-line:

```sh
execute_query "select 1 + 1;"
```

The tool can be run as a local web server that presents a UI where you can enter
a query and execute it interactively. The following command will run a local web
server and show a URL that you can open in your browser. You can also provide an
optional `--port` flag (the default port is 8080).

```sh
execute_query --web
```

> [!NOTE]
> Note that this tool is intended primarily as a way to explore the language and
> debug its implementation. It executes queries using the internal reference
> implementation which is intended primarily for testing. It is not performant
> or scalable, and there is no query optimizer.

### Modes

The default mode executes the query and shows the result. Other modes
show the intermediate stages like parse trees and analzyer output.
For example, this shows the
parse tree, the resolved AST, and the results of executing the query.

```sh
execute_query --mode=parse,analyze,execute 'SELECT 1+2, CONCAT("hello", " world");'
```

The web UI (run with the `--web` flag) has checkboxes for selecting these modes.

The modes are:

* `execute`: Executes the query and shows results in a table. In the
  command-line mode, you can also see the results in JSON or textproto format
  using the `--output_mode` flag.
* `analyze`: Shows the resolved AST, as documented in [GoogleSQL Resolved
  AST](https://github.com/google/googlesql/blob/master/docs/resolved_ast.md)
* `parse`: Shows the parse tree as defined in
  [ast_node.h](https://github.com/google/googlesql/blob/master/googlesql/parser/ast_node.h)
* `explain`: Shows the evaluator query plan (for execution in the reference
  implementation)
* `unparse`: Shows the result of converting the parse tree back to SQL
* `unanalyze`: Shows the result of converting the resolved AST back to SQL

### Catalogs

The tool also includes some built-in catalogs that provide some pre-defined
tables with sample data that you can use for queries. You can specify the
catalog to use with the `--catalog` flag. In the web UI, the
catalog can be selected from a dropdown list.

The following catalogs are supported:

* `none`: An empty catalog with no tables.
* `sample`: The sample catalog defined in
  [sample_catalog.cc](https://github.com/google/googlesql/blob/master/googlesql/testdata/sample_catalog.cc),
  which is used for most [analyzer tests](https://github.com/google/googlesql/tree/master/googlesql/analyzer/testdata).
  These tables do not have any data.
* `tpch`: A catalog with the standard
  [tables](https://github.com/google/googlesql/tree/master/googlesql/examples/tpch/describe.txt)
  from the TPC-H benchmark, with a 1MB dataset.

For example, the `sample` catalog defines the tables `KeyValue` and
`MultipleColumns`, and the `tpch` catalog defines the tables `Orders`,
`LineItem` and `Customer`, among others. You can see the schema of these tables
by executing `DESCRIBE <table name>`.

In `parse` mode, the catalog isn't used, so any statement can be parsed
regardless of the catalog.

For `analyze` and `execute` modes, the catalog is used to resolve table schemas.

When executing queries, some catalogs provided have data attached to the
tables. Queries against those tables will be executable.

### Executable statements

Execution is mostly limited to queries. They can be written in standard syntax
using `SELECT` or in  pipe syntax using `FROM`.

Other executable statements:

* `DESCRIBE <object_name>;` shows table schemas or function signatures, looking
  up names from the selected catalog.
* `SELECT <expression>;` can be used to evaluate expressions. In the
  command-line mode, you can also specify `--sql_mode=expression` to interpret
  the input as expressions.

DDL statements that update the catalog:

* `CREATE FUNCTION` creates a SQL UDF, or defines signatures for non-SQL UDFs.
  These functions support analysis but not execution.
* `CREATE TABLE FUNCTION `creates ia SQL TVF, or defines a signature for non-SQL
  a TVF.
* `CREATE TABLE` defines a table.  It will have zero rows when queried.

SQL functions and TVFs will be executable.  Non-SQL functions and TVFs don't
have an implementation, so queries using them can be analyzed but can't be
executed.

### Example queries

There are some runnable example queries in
[`googlesql/examples/tpch`](googlesql/examples/tpch) and
[`googlesql/examples/pipe_queries`](googlesql/examples/pipe_queries).
