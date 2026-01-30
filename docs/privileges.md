

<!-- mdlint off(WHITESPACE_LINE_LENGTH) -->

# Privileges

GoogleSQL supports statements for privileges.

## `GRANT` and `REVOKE`

<pre>
GRANT privileges ON [object_type] object TO grantees;

REVOKE privileges ON [object_type] object FROM grantees;
</pre>

The `GRANT` and `REVOKE` statements have the same syntax. `GRANT` uses the `TO`
keyword to assign privileges to a grantee, while `REVOKE` uses the `FROM`
keyword to remove privileges from a grantee.

Both of these statements use the following variables:

+ `privileges` is either a comma-separated list of identifiers, or
  `ALL PRIVILEGES`.
+ `object_type` is an optional identifier for the object (for example,
  `TABLE`).
+ `object` identifies the object and supports an optional comma-separated
  list of identifiers in parentheses. These identifiers are usually column
  names.
+ `grantees` is a comma-separated list of strings.

<!-- mdlint off(WHITESPACE_LINE_LENGTH) -->

<!-- mdlint on -->

