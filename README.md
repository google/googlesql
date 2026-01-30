## GoogleSQL - Analyzer Framework for SQL

> GoogleSQL was previously named ZetaSQL. Please refer to [this
> doc](zetasql_to_googlesql_migration.md) for a guide that helps you to
> migrate.

GoogleSQL defines a SQL language (grammar, types, data model, semantics, and
function library) and implements parsing and analysis for that language as a
reusable component. GoogleSQL is not itself a database or query engine. Instead,
it's intended to be used by multiple engines, to provide consistent language and
behavior (name resolution, type checking, implicit casting, etc.). Specific
query engines may implement a subset of features, giving errors for unsupported
features. GoogleSQL's compliance test suite can be used to validate query engine
implementations are correct and consistent.

GoogleSQL implements the GoogleSQL language, which is used across several of
Google's SQL products, both publicly and internally, including BigQuery,
Spanner, F1, BigTable, Dremel, Procella, and others.

GoogleSQL and GoogleSQL have been described in these publications:

*   (CDMS 2022) [GoogleSQL: A SQL Language as a Component](https://cdmsworkshop.github.io/2022/Slides/Fri_C2.5_DavidWilhite.pptx)
    (Slides)
*   (SIGMOD 2017) [Spanner: Becoming a SQL System](https://static.googleusercontent.com/media/research.google.com/en//pubs/archive/46103.pdf)
    -- See section 6.
*   (VLDB 2024) [SQL Has Problems. We Can Fix Them: Pipe Syntax in
    SQL](https://research.google/pubs/pub1005959/) -- Describes GoogleSQL's new
    pipe query syntax.

Some other documentation:

*   [GoogleSQL Language Reference](docs/README.md)
*   [GoogleSQL Resolved AST](docs/resolved_ast.md), documenting the intermediate
    representation produced by the GoogleSQL analyzer.
*   [GoogleSQL
    Toolkit](https://github.com/GoogleCloudPlatform/googlesql-toolkit), a
    project using GoogleSQL to analyze and understand queries against BigQuery,
    and other GoogleSQL engines.
*   Pipe query syntax
    *   See the [reference documentation](https://github.com/google/googlesql/blob/master/docs/pipe-syntax.md)
        and [research paper](https://research.google/pubs/pub1005959/).
    *   See some [example scripts](googlesql/examples/pipe_queries) and [TPC-H
        queries](googlesql/examples/tpch).

## Project Overview

The main components and APIs are in these directories under `googlesql/`:

*   `googlesql/public`: Most public APIs are here.
*   `googlesql/resolved_ast`: Defines the [Resolved AST](docs/resolved_ast.md),
    which the analyzer produces.
*   `googlesql/parser`: The grammar and parser implementation. (Semi-public,
    since the parse trees are not a stable API.)
*   `googlesql/analyzer`: The internal implementation of query analysis.
*   `googlesql/reference_impl`: The reference implementation for executing
    queries.
*   `googlesql/compliance`: Compliance test framework and compliance tests.
*   `googlesql/public/functions`: Function implementations for engines to use.
*   `googlesql/tools/execute_query`: Interactive query execution for debugging.
*   `googlesql/java/com/google/googlesql`: Java APIs, implemented by calling a
    local RPC server.

Multiplatform support is planned for the following platforms:

-   Linux (Ubuntu 22.04 is our reference platform, but others may work).
-   MacOS (Experimental)

We do not provide any guarantees of API stability and *cannot accept
contributions*.

## Running Queries with `execute_query`

The `execute_query` tool can parse, analyze and run SQL queries using the
reference implementation.

See [Execute Query](execute_query.md) for more details on using the tool.

You can run it using binaries from
[Releases](https://github.com/google/googlesql/releases), or build it using the
instructions below.

There are some runnable example queries in
[`googlesql/examples/tpch`](googlesql/examples/tpch) and
[`googlesql/examples/pipe_queries`](googlesql/examples/pipe_queries).

### Getting and Running `execute_query`

#### Pre-built Binaries

GoogleSQL provides pre-built binaries for `execute_query` for Linux and MacOS on
the [Releases](https://github.com/google/googlesql/releases) page. You can run
the downloaded binary like:

```bash
chmod +x execute_query_linux
./execute_query_linux --web
```

MacOS users may see the error `execute_query_macos cannot be opened because the
developer cannot be verified.`. You can right click the `execute_query_macos`
file, click "open", and then you should be able to run the binary.

Note the prebuilt binaries require GCC-9+ and tzdata. If you run into dependency
issues or if the binary is incompatible with your platform, you can try running
`execute_query` with Docker. See the [Run with Docker](#run-with-docker)
section.

#### Running from a Bazel Build

You can build `execute_query` with Bazel from source and run it by:

```bash
bazel run googlesql/tools/execute_query:execute_query -- --web
```

#### Run with Docker

You can run `execute_query` using Docker. Download the pre-built Docker image
file `googlesql_docker.tar.gz` from the
[Releases](https://github.com/google/googlesql/releases) page, and load the
image using:

```bash
sudo docker load -i /path/to/the/downloaded/googlesql_docker.tar.gz
```

The Docker image name is `googlesql`. (You can also build a Docker image locally
using the instructions in the [Build with Docker](#build-with-docker) section.)

You can then run `execute_query` using:

```bash
sudo docker run --init -it -h=$(hostname) -p 8080:8080 googlesql execute_query --web
```

If you are using MacOS with Apple's M series chip, add the additional argument
`--platform=linux/amd64`:

```bash
sudo docker run --init -it -h=$(hostname) -p 8080:8080 --platform linux/amd64 googlesql execute_query --web
```

Argument descriptions:

*   `--init`: Allows `execute_query` to handle signals properly.
*   `-it`: Runs the container in interactive mode.
*   `-h=$(hostname)`: Makes the hostname of the container the same as that of
    the host.
*   `-p 8080:8080`: Sets up port forwarding.
*   `googlesql`: The docker image name.

`-h=$(hostname)` and `-p 8080:8080` together make the URL address of the web
server accessible from the host machine.

Alternatively, you can run this to start a bash shell, and then run
`execute_query` inside:

```bash
sudo docker run --init -it -h=$(hostname) -p 8080:8080 my-googlesql-image

# Inside the container bash shell
execute_query --web
```

## How to Build

### Build with Bazel

GoogleSQL uses [Bazel](https://bazel.build) for building and dependency
resolution. Instructions for installing Bazel can be found in
https://bazel.build/install. The Bazel version that GoogleSQL uses is specified
in the `.bazelversion` file.

Besides Bazel, the following dependencies are also needed:

*   tzdata

`tzdata` provides the support for time zone information. It is generally
available on MacOS. If you run Linux and it is not pre-installed, you can
install it with `apt-get install tzdata`.

Once the dependencies are installed, you can build or run GoogleSQL targets as
needed, for example:

```bash
# Build everything.
bazel build ...

# Build and run the execute_query tool.
bazel run //googlesql/tools/execute_query:execute_query -- --web

# The built binary can be found under bazel-bin and run directly.
bazel-bin/googlesql/tools/execute_query/execute_query --web

# Build and run a test.
bazel test //googlesql/parser:parser_set_test
```

Some Mac users may experience build issues due to the Python error
`ModuleNotFoundError: no module named 'google.protobuf'`. To resolve it, run
`pip install protobuf==<version>` to install python protobuf. The protobuf
version can be found in the `googlesql_deps_step_2.bzl` file.

### Build with Docker

GoogleSQL also provides a `Dockerfile` which configures all the dependencies so
that users can build GoogleSQL more easily across different platforms.

To build the Docker image locally (called `my-googlesql-image` here), run:

```bash
sudo docker build . -t my-googlesql-image -f Dockerfile
```

Alternatively, GoogleSQL provides pre-built Docker images named `googlesql`. See
the [Releases](https://github.com/google/googlesql/releases) page. You can load
the downloaded image by:

```bash
sudo docker load -i /path/to/the/downloaded/googlesql_docker.tar.gz
```

To run builds or other commands inside the Docker environment, run this command
to open a bash shell inside the container:

```bash
# Start a bash shell running inside the Docker container.
sudo docker run -it my-googlesql-image
```

Replace `my-googlesql-image` with `googlesql` if you use the pre-built Docker
image.

Then you can run the commands from the [Build with Bazel](#build-with-bazel)
section above.

## Differential Privacy

For questions, documentation, and examples of GoogleSQL's implementation of
Differential Privacy, please check out
(https://github.com/google/differential-privacy).

## Versions

GoogleSQL makes no guarantees regarding compatibility between releases. Breaking
changes may be made at any time. Our releases are numbered based on the date of
the commit the release is cut from. The number format is YYYY.MM.n, where YYYY
is the year, MM is the two digit month, and n is a sequence number within the
time period.

## License

[Apache License 2.0](LICENSE)

## Support Disclaimer

This is not an officially supported Google product.
