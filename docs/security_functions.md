

<!-- mdlint off(WHITESPACE_LINE_LENGTH) -->

# Security functions

GoogleSQL supports the following security functions.

## Function list

<table>
  <thead>
    <tr>
      <th>Name</th>
      <th>Summary</th>
    </tr>
  </thead>
  <tbody>

<tr>
  <td><a href="https://github.com/google/googlesql/blob/master/docs/security_functions.md#secure_context"><code>SECURE_CONTEXT</code></a>
</td>
  <td>Returns the value of a request-scoped secure context parameter provided at
      query run time.</td>
</tr>

<tr>
  <td><a href="https://github.com/google/googlesql/blob/master/docs/security_functions.md#session_user"><code>SESSION_USER</code></a>
</td>
  <td>
    Get the email address or principal identifier of the user that's running
    the query.
  </td>
</tr>

  </tbody>
</table>

## `SECURE_CONTEXT`

```googlesql
SECURE_CONTEXT(key)
```

**Description**

Returns the value of a request-scoped secure context parameter provided at query
run time.

**Definitions**

+   `key`: A constant, non-`NULL` `STRING` expression representing the name of the
    secure context parameter to retrieve. `key` isn't coercible.

**Limitations**

+   **Supported types**: Values supplied in the secure context must be `STRING`
    values.
+   **Error mode**: `SECURE_CONTEXT` doesn't support the `SAFE.` prefix.
    Calling `SAFE.SECURE_CONTEXT(key)` returns an analysis error.
+   **Missing parameter handling**: If a query evaluates
    `SECURE_CONTEXT('my_param')` and `my_param` isn't provided in the request's
    secure context, the query fails and returns an error.

**Return type**

`STRING`

**Examples**

The following example filters a query using `SECURE_CONTEXT('user_dept')` when
the secure context parameter `'user_dept'` is set to `'Sales'` at run time:

```googlesql
WITH
  Orders AS (
    SELECT 1001 AS order_id, 'Sales' AS department, 250.00 AS amount
    UNION ALL
    SELECT 1002 AS order_id, 'Engineering' AS department, 500.00 AS amount
    UNION ALL
    SELECT 1004 AS order_id, 'Sales' AS department, 430.50 AS amount
  )
SELECT
  order_id,
  department,
  amount
FROM Orders
WHERE department = SECURE_CONTEXT('user_dept');

/*----------+------------+--------+
 | order_id | department | amount |
 +----------+------------+--------+
 | 1001     | Sales      | 250.00 |
 | 1004     | Sales      | 430.50 |
 +----------+------------+--------*/
```

## `SESSION_USER`

```
SESSION_USER()
```

**Description**

For first-party users, returns the email address of the user that's running the
query.
For third-party users, returns the
[principal identifier](https://cloud.google.com/iam/docs/principal-identifiers)
of the user that's running the query.
For more information about identities, see
[Principals](https://cloud.google.com/docs/authentication#principal).

**Return Data Type**

`STRING`

**Example**

```googlesql
SELECT SESSION_USER() as user;

/*----------------------+
 | user                 |
 +----------------------+
 | jdoe@example.com     |
 +----------------------*/
```

