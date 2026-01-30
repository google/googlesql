

<!-- mdlint off(WHITESPACE_LINE_LENGTH) -->

# Transactions

GoogleSQL supports the following transaction statements.

## `BEGIN`

<pre>
BEGIN [TRANSACTION][ISOLATION LEVEL isolation_level];
</pre>

Begins a transaction.

This statement supports an optional `ISOLATION LEVEL` clause. Following are the
standard values for `isolation_level`:

+ `READ UNCOMMITTED`
+ `READ COMMITTED`
+ `REPEATABLE READ`
+ `SERIALIZABLE`

**Example**

The following example begins a transaction using the `READ COMMITTED` isolation
level.

```
BEGIN TRANSACTION ISOLATION LEVEL READ COMMITTED;
```

## `START TRANSACTION`

<pre>
START TRANSACTION [ISOLATION LEVEL isolation_level]
</pre>

Synonymous with [`BEGIN`][begin-transaction].

## `COMMIT`

<pre>
COMMIT [TRANSACTION];
</pre>

Commits a transaction.

## `ROLLBACK`

<pre>
ROLLBACK [TRANSACTION];
</pre>

Rolls back a transaction.

[begin-transaction]: #begin

<!-- mdlint off(WHITESPACE_LINE_LENGTH) -->

<!-- mdlint on -->

