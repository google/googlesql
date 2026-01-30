

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
  <td><a href="https://github.com/google/googlesql/blob/master/docs/security_functions.md#session_user"><code>SESSION_USER</code></a>
</td>
  <td>
    Get the email address or principal identifier of the user that's running
    the query.
  </td>
</tr>

  </tbody>
</table>

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

