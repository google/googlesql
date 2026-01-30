# ZetaSQL to GoogleSQL Migration Guide

ZetaSQL is being renamed to **GoogleSQL** to unify our branding and provide a
consistent experience across our ecosystems. This document provides instructions
for users to migrate their projects from ZetaSQL to GoogleSQL.

## Quick Reference Table

| Category | Old (ZetaSQL) | New (GoogleSQL) |
| :--- | :--- | :--- |
| **Project Name** | ZetaSQL | GoogleSQL |
| **GitHub Repository** | `google/zetasql` | `google/googlesql` |
| **C++ Namespace** | `zetasql` / `zetasql_base` | `googlesql` / `googlesql_base` |
| **C++ Macros** | `ZETASQL_...` | `GOOGLESQL_...` |
| **C++ Functions and Variables** | `ZetaSql...` | `GoogleSql...` |
| **Java Package** | `com.google.zetasql` | `com.google.googlesql` |
| **Java Artifacts** | `zetasql-...` | `googlesql-...` |
| **Bazel Workspace** | `@zetasql` | `@googlesql` |
| **Include Paths** | `#include "zetasql/..."` | `#include "googlesql/..."` |
| **File Names** | `ZetaSql...` / `zetasql...` | `GoogleSql...` / `googlesql...` |

## Migration Steps for Users

### 1. Update GitHub Remotes

The GitHub repository has been renamed. While GitHub provides redirects, we
recommend updating your local git remotes to point directly to the new URL.

```bash
# Update your 'origin' remote
git remote set-url origin https://github.com/google/googlesql.git
```

If you have forks or other references, ensure they are updated to point to
`google/googlesql`.

### 2. File and Directory Renaming

We strongly recommend renaming any files or directories that contain `zetasql`
in their name.

*   Rename directories/files matching `*zetasql*` to replace `zetasql` with
    `googlesql`.
*   Rename directories/files matching `*ZetaSQL*` to replace `ZetaSQL` with
    `GoogleSQL`.

### 3. Search and Replace

Perform the following search and replace operations in your codebase. Completing
these replacements should address most migration requirements.

*   `zetasql` -> `googlesql`
*   `ZetaSQL` -> `GoogleSQL`
*   `ZetaSql` -> `GoogleSql`
*   `ZETASQL` -> `GOOGLESQL`

> **Warning**: Ensure you exclude `.git` directories and build artifacts (e.g.,
`bazel-*`) to avoid corrupting your repository or build cache.

You may try to compile and test your project. If the compilation or tests fail,
follow the detailed steps below and verify your build after each step.

#### Step 3.1: Update Java References

Update your dependency version to `2026.01.1`. Replace packages and artifact
names:

*   `com.google.zetasql` -> `com.google.googlesql`
*   `zetasql-jni` -> `googlesql-jni`
*   `zetasql-types` -> `googlesql-types`
*   `zetasql-client` -> `googlesql-client`

The Java artifacts were previously under the
[com.google.zetasql](https://central.sonatype.com/namespace/com.google.zetasql)
namespace and have been moved to
[com.google.googlesql](https://central.sonatype.com/namespace/com.google.googlesql).
Artifacts are renamed from `zetasql-*` to `googlesql-*`. All current artifacts
are:

*   `googlesql-client`
*   `googlesql-types`
*   `googlesql-jni-channel`
*   `googlesql-jni-channel-darwin`
*   `googlesql-jni-channel-linux`

Replace path:

*   `java/com/google/zetasql` -> `java/com/google/googlesql`

#### Step 3.2: Update Build Dependencies

GoogleSQL requires Bzlmod as of version `2025.12.1`. If you are not using Bzlmod
(i.e., you are using the `WORKSPACE` system), you must migrate to Bzlmod first
(see [Bazel Migration Guide](https://bazel.build/external/migration)). Check out
examples to use GoogleSQL as a module [here](https://github.com/google/googlesql/tree/master/examples/bazel).

If you are using Bzlmod, update your `MODULE.bazel` file to depend on
`googlesql` instead of `zetasql`.

```python
# Old
bazel_dep(name = "zetasql")
archive_override(
    module_name = "zetasql",
    ...
)

# New
bazel_dep(name = "googlesql")
archive_override(
    module_name = "googlesql",
    ...
)
```

Update Bazel references:

*   `@zetasql` -> `@googlesql`

#### Step 3.3: Update C++ Code

Update namespaces, macros, includes, variable and function names:

*   `namespace zetasql` -> `namespace googlesql`
*   `zetasql::` -> `googlesql::`
*   `zetasql_base` -> `googlesql_base`
*   `#include "zetasql/` -> `#include "googlesql/`
*   `ZETASQL_` -> `GOOGLESQL_`
*   `kZetaSqlTypeUrlPrefix` -> `kGoogleSqlTypeUrlPrefix`
*   `ZetaSqlMakeCheckOpValueString` -> `GoogleSqlMakeCheckOpValueString`
*   `ZetaSqlMakeCheckOpString` -> `GoogleSqlMakeCheckOpString`

Finally, check for any remaining "zetasql" references and update them as needed.
