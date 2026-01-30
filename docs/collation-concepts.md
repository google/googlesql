

<!-- mdlint off(WHITESPACE_LINE_LENGTH) -->

# Collation

GoogleSQL supports collation. Collation
defines rules to sort and compare strings in certain
[operations][collate-operations], such as conditional expressions, joins, and
groupings.

By default, GoogleSQL sorts strings case-sensitively. This means that `a` and
`A` are treated as different letters, and `Z` would come before `a`.

**Example default sorting:** Apple, Zebra, apple

By contrast, collation lets you sort and compare strings case-insensitively or
according to specific language rules.

**Example case-insensitive collation:** Apple, apple, Zebra

To customize collation for a
collation-supported operation, you typically [assign a collation
specification][collate-define] to at least one string in the operation inputs.
Some operations can't use collation, but can [propagate collation through
them][collate-propagate].

Collation is useful when you need fine-tuned control over how values are sorted,
joined, or grouped in tables.

## Operations affected by collation 
<a id="collate_operations"></a>

The following example query operations are affected by collation when
sorting and comparing strings:

| Operations                                                 |
| ---------------------------------------------------------- |
| Collation-supported [comparison operations][collate-funcs] |
| [Join operations][join-types]                              |
| [`ORDER BY`][order-by-clause]                              |
| [`GROUP BY`][group-by-clause]                              |
| [`WINDOW`][window-clause] for window functions             |
| Collation-supported [scalar functions][collate-funcs]      |
| Collation-supported [aggregate functions][collate-funcs]   |
| [Set operations][set-operators]                            |
| [`NULLIF` conditional expression][nullif]                  |

## Operations that propagate collation 
<a id="collate_propagate"></a>

Collation can pass through some query operations to other parts
of a query. When collation passes through an operation in a
query, this is known as _propagation_. During propagation:

+ If an input contains no collation specification or an empty
  collation specification and another input contains an explicitly defined
  collation, the explicitly defined collation is used for all of the inputs.
+ All inputs with a non-empty explicitly defined collation specification must
  have the same type of collation specification, otherwise an error is thrown.

GoogleSQL has several [functions][functions-propagation],
[operators][operators-propagation], and [expressions][expressions-propagation]
that can propagate collation.

In the following example, the `'und:ci'` collation specification is propagated
from the `character` column to the `ORDER BY` operation.

```googlesql
-- With collation
SELECT *
FROM UNNEST([
  COLLATE('B', 'und:ci'),
  'b',
  'a'
]) AS character
ORDER BY character

/*-----------+
 | character |
 +-----------+
 | a         |
 | B         |
 | b         |
 +-----------*/
```

```googlesql
-- Without collation
SELECT *
FROM UNNEST([
  'B',
  'b',
  'a'
]) AS character
ORDER BY character

/*-----------+
 | character |
 +-----------+
 | B         |
 | a         |
 | b         |
 +-----------*/
```

### Functions 
<a id="functions_propagation"></a>

The following example functions propagate collation.

| Function  | Notes
| --------- | --------
| [`AEAD.DECRYPT_STRING`][aead_decrypt_string] |
| [`ANY_VALUE`][any-value] |
| [`ARRAY_AGG`][array-agg] | Collation on input arguments are propagated as collation on the array element.
| [`ARRAY_FIRST`][array-first] |
| [`ARRAY_LAST`][array-last] |
| [`ARRAY_SLICE`][array-slice] |
| [`ARRAY_TO_STRING`][array-to-string] | Collation on array elements are propagated to output.
| [`COLLATE`][collate] |
| [`CONCAT`][concat] |
| [`FORMAT`][format-func] | Collation from `format_string` to the returned string is propagated.
| [`FORMAT_DATE`][format-date] | Collation from `format_string` to the returned string is propagated.
| [`FORMAT_DATETIME`][format-datetime] | Collation from `format_string` to the returned string is propagated.
| [`FORMAT_TIME`][format-time] | Collation from `format_string` to the returned string is propagated.
| [`FORMAT_TIMESTAMP`][format-timestamp] | Collation from `format_string` to the returned string is propagated.
| [`FROM_PROTO`][from-proto] |
| [`GREATEST`][greatest] |
| [`LAG`][lag] |
| [`LEAD`][lead] |
| [`LEAST`][least]|
| [`LEFT`][left] |
| [`LOWER`][lower] |
| [`LPAD`][lpad] |
| [`MAX`][max] |
| [`MIN`][min] |
| [`NET.HOST`][nethost] |
| [`NET.MAKE_NET`][netmake-net] |
| [`NET.PUBLIC_SUFFIX`][netpublic-suffix] |
| [`NET.REG_DOMAIN`][netreg-domain] |
| [`NTH_VALUE`][nth-value] |
| [`NORMALIZE`][normalize] |
| [`NORMALIZE_AND_CASEFOLD`][normalize-and-casefold] |
| [`NULLIFERROR`][nulliferror] |
| [`PROTO_DEFAULT_IF_NULL`][proto-default-if-null] |
| [`REPEAT`][repeat] |
| [`REPLACE`][replace] |
| [`REVERSE`][reverse] |
| [`RIGHT`][right] |
| [`RPAD`][rpad] |
| [`SOUNDEX`][soundex] |
| [`SPLIT`][split] | Collation on input arguments are propagated as collation on the array element.
| [`STRING_AGG`][string-agg] |
| [`SUBSTR`][substr] |
| [`UPPER`][upper] |

### Operators 
<a id="operators_propagation"></a>

The following example operators propagate collation.

| Operator  | Notes
| --------- | --------
| [`\|\|` concatenation operator][concat-op] |
| [Array subscript operator][array-subscript-operator] | Propagated to output.
| [Set operators][set-operators] | Collation of an output column is decided by the collations of input columns at the same position.
| [`STRUCT` field access operator][field-access-operator] | When getting a `STRUCT`, collation on the `STRUCT` field is propagated as the output collation.
| [`UNNEST`][unnest-operator] | Collation on the input array element is propagated to output.

### Expressions 
<a id="expressions_propagation"></a>

The following example expressions propagate collation.

| Expression  | Notes
| --------- | --------
| [`ARRAY`][array-dt] | When you construct an `ARRAY`, collation on input arguments is propagated on the elements in the `ARRAY`.
| [`CASE`][case] |
| [`CASE` expr][case-expr] |
| [`COALESCE`][coalesce] |
| [`IF`][if] |
| [`IFNULL`][ifnull] |
| [`NULLIF`][nullif] |
| [`STRUCT`][struct-dt] | When you construct a `STRUCT`, collation on input arguments is propagated on the fields in the `STRUCT`.

## Where you can assign a collation specification 
<a id="collate_define"></a>

You can assign a [collation specification][collate-spec-details] to these
collation-supported types:

+ A `STRING`
+ A `STRING` field in a `STRUCT`
+ A `STRING` element in an `ARRAY`

In addition:

+ You can assign a default collation specification to a schema when you
  create or alter it. This assigns a default collation specification to all
  future tables that are added to the schema if the tables don't have their
  own default collation specifications.
+ You can assign a default collation specification to a table when you create
  or alter it. This assigns a collation specification to all future
  collation-supported columns that are added to the table if the columns don't
  have collation specifications. This overrides a
  default collation specification on a schema.
+ You can assign a collation specification to a collation-supported type
  in a column. A column that contains a collation-supported type in its
  column schema is a collation-supported column. This overrides a
  default collation specification on a table.
+ You can assign a collation specification to a collation-supported
  query operation.
+ You can assign a collation specification to a collation-supported expression
  with the `COLLATE` function. This overrides any collation specifications set
  previously.

In summary:

You can define a default collation specification for a schema. For example:

```googlesql
CREATE SCHEMA (...)
DEFAULT COLLATE 'und:ci'
```

You can define a default collation specification for a table. For example:

```googlesql
CREATE TABLE (...)
DEFAULT COLLATE 'und:ci'
```

You can define a collation specification for a collation-supported column.
For example:

```googlesql
CREATE TABLE (
  case_insensitive_column STRING COLLATE 'und:ci'
)
```

You can specify a collation specification for a collation-supported expression
with the `COLLATE` function. For example:

```googlesql
SELECT COLLATE('a', 'und:ci') AS character
```

In the `ORDER BY` clause, you can specify a collation specification for a
collation-supported column. This overrides any
collation specifications set previously.

For example:

```googlesql
SELECT Place
FROM Locations
ORDER BY Place COLLATE "und:ci"
```

### DDL statements 
<a id="collate_ddl"></a>

You can assign a collation specification to the following DDL statements.

<!-- mdlint off(WHITESPACE_LINE_LENGTH) -->

| Location  | Support                          | Notes                                          |
| ----------| -------------------------------  | ---------------------------------------------- |
| Schema | [`CREATE SCHEMA`][create-schema] | Create a schema and optionally add a default   |
:           :                                  : collation specification to the schema.         :
| Schema | [`ALTER SCHEMA`][alter-schema] | Updates the default collation specification    |
:           :                                  : for a schema.                                  :
| Table     | [`CREATE TABLE`][create-table]   | Create a table and optionally add a default    |
:           :                                  : collation specification to a table             :
:           :                                  : or a collation specification to a              :
:           :                                  : collation-supported type in a column.          :
:           :                                  : <br /><br />                                   :
:           :                                  : You can't have collation on a column used      :
:           :                                  : with `CLUSTERING`.                             :
| Table     | [`ALTER TABLE`][alter-table]     | Update the default collation specification     |
:           :                                  : for collation-supported type in a table.       :
| Column    | [`ADD COLUMN`][add-column]       | Add a collation specification to a             |
:           :                                  : collation-supported type in a new column       :
:           :                                  : in an existing table.                          :

<!-- mdlint on -->

### Data types 
<a id="collate_data_types"></a>

You can assign a collation specification to the following data types.

<!-- mdlint off(WHITESPACE_LINE_LENGTH) -->

| Type                  | Notes                                               |
| --------------------- | --------------------------------------------------- |
| [`STRING`][string-dt] | You can apply a collation specification directly to |
:                       : this data type.                                     :
| [`STRUCT`][struct-dt] | You can apply a collation specification to a        |
:                       : `STRING` field in a `STRUCT`. A `STRUCT` can        :
:                       : have `STRING` fields with different                 :
:                       : collation specifications.                           :
:                       : A `STRUCT` can only be used in comparisons with the :
:                       : following operators and conditional expressions&#58;:
:                       : `=`, `!=`, `IN`, `NULLIF`, and `CASE`.              :
| [`ARRAY`][array-dt]   | You can apply a collation specification to a        |
:                       : `STRING` element in an `ARRAY`. An `ARRAY` can      :
:                       : have `STRING` elements with different               :
:                       : collation specifications.                           :

<!-- mdlint on -->

Note: Use the [`COLLATE`][collate] function to apply a collation specification
to collation-supported expressions.

### Query statements 
<a id="collate_query"></a>

You can assign a collation specification to the following query statements.

| Type             | Support                              |
| ---------------- | ------------------------------------ |
| Sorting          | [`ORDER BY` clause][order-by-clause] |

### Functions, operators, and conditional expressions 
<a id="collate_funcs"></a>

You can assign a collation specification to the following functions, operators,
and conditional expressions.

<!-- mdlint off(WHITESPACE_LINE_LENGTH) -->

#### Functions

| Type      | Support                      | Notes                                       |
| ----------| ---------------------------- | ------------------------------------------- |
| Scalar    | [`COLLATE`][collate]         |                                             |
| Scalar    | [`ENDS_WITH`][ends-with]     |                                             |
| Scalar    | [`GREATEST`][greatest]       |                                             |
:           :                              :                                             :
| Scalar    | [`INSTR`][instr]             |                                             |
| Scalar    | [`LEAST`][least]             |                                             |
| Scalar    | [`REPLACE`][replace]         |                                             |
:           :                              :                                             :
| Scalar    | [`SPLIT`][split]             |                                             |
| Scalar    | [`STARTS_WITH`][starts-with] |                                             |
| Scalar    | [`STRPOS`][strpos]           |                                             |
| Aggregate | [`COUNT`][count]             | This operator is only affected by           |
:           :                              : collation when the input includes           :
:           :                              : the `DISTINCT` argument.                    :
| Aggregate | [`MAX`][max]                 |                                             |
| Aggregate | [`MIN`][min]                 |                                             |
:           :                              :                                             :

#### Operators

| Support                              | Notes                                 |
| ------------------------------------ | ------------------------------------- |
| [`<`][comparison-op]                 |                                       |
| [`<=`][comparison-op]                |                                       |
| [`>`][comparison-op]                 |                                       |
| [`>=`][comparison-op]                |                                       |
| [`=`][comparison-op]                 |                                       |
| [`!=`][comparison-op]                |                                       |
| [`[NOT] BETWEEN`][comparison-op]     |                                       |
| [`[NOT] IN`][in-op]                  | [Limitations apply][in-op].           |
| [`[NOT] LIKE`][like-op]              | [Limitations apply][like-op].         |
| [Quantified `[NOT] LIKE`][q-like-op] | [Limitations apply][q-like-op].       |

#### Conditional expressions

| Support                  |                                             |
| ------------------------ | ------------------------------------------- |
| [`CASE`][case]           |                                             |
| [`CASE` expr][case-expr] |                                             |
| [`NULLIF`][nullif]       |                                             |

<!-- mdlint on -->

The preceding collation-supported operations
(functions, operators, and conditional expressions)
can include input with explicitly defined collation specifications for
collation-supported types. In a collation-supported operation:

+ All inputs with a non-empty, explicitly defined collation specification must
  be the same, otherwise an error is thrown.
+ If an input doesn't contain an explicitly defined collation
  and another input contains an explicitly defined collation, the
  explicitly defined collation is used for both.

For example:

```googlesql
-- Assume there's a table with this column declaration:
CREATE TABLE table_a
(
    col_a STRING COLLATE 'und:ci',
    col_b STRING COLLATE '',
    col_c STRING,
    col_d STRING COLLATE 'und:ci'
);

-- This runs. Column 'b' has a collation specification and the
-- column 'c' doesn't.
SELECT STARTS_WITH(col_b_expression, col_c_expression)
FROM table_a;

-- This runs. Column 'a' and 'd' have the same collation specification.
SELECT STARTS_WITH(col_a_expression, col_d_expression)
FROM table_a;

-- This runs. Even though column 'a' and 'b' have different
-- collation specifications, column 'b' is considered the default collation
-- because it's assigned to an empty collation specification.
SELECT STARTS_WITH(col_a_expression, col_b_expression)
FROM table_a;

-- This works. Even though column 'a' and 'b' have different
-- collation specifications, column 'b' is updated to use the same
-- collation specification as column 'a'.
SELECT STARTS_WITH(col_a_expression, COLLATE(col_b_expression, 'und:ci'))
FROM table_a;

-- This runs. Column 'c' doesn't have a collation specification, so it uses the
-- collation specification of column 'd'.
SELECT STARTS_WITH(col_c_expression, col_d_expression)
FROM table_a;
```

## Collation specification details 
<a id="collate_spec_details"></a>

A collation specification determines how strings are sorted and compared in
[collation-supported operations][collate-operations]. You can define a
collation specification for [collation-supported types][collate-define].
These types of collation specifications are available:

+ [Binary collation specification][binary-collation]
+ [Unicode collation specification][unicode-collation]

If a collation specification isn't defined, the default collation specification
is used. To learn more, see the next section.

### Default collation specification 
<a id="default_collation"></a>

When a collation specification isn't assigned or is empty,
`'binary'` collation is used. Binary collation indicates that the
operation should return data in [Unicode code point order][unicode-code-point].
You can't set binary collation explicitly.

In general, the following behavior occurs when an empty string is included in
collation:

+   If a string has `und:ci` collation, the string comparison is
    case-insensitive.
+   If a string has empty collation, the string comparison is case-sensitive.
+   If string not assigned collation, the string comparison is case-sensitive.
+   A column with unassigned collation inherit the table's default
    collation.
+   A column with empty collation doesn't inherit the table's default collation.

### Binary collation specification 
<a id="binary_collation"></a>

```googlesql
collation_specification:
  'language_tag'
```

A binary collation specification indicates that the operation should
return data in [Unicode code point order][unicode-code-point]. The
collation specification can be a `STRING` literal or a query parameter.

The language tag determines how strings are generally sorted and compared.
The allowed value for the `language_tag` is `binary`.

This is what the `binary` language tag looks like when used with the `ORDER BY`
clause:

```googlesql
SELECT Place
FROM Locations
ORDER BY Place COLLATE 'binary'
```

### Unicode collation specification 
<a id="unicode_collation"></a>

```googlesql
collation_specification:
  'language_tag[:collation_attribute]'
```

A unicode collation specification indicates that the operation should use the
[Unicode Collation Algorithm][tr10-collation-algorithm] to sort and compare
strings. The collation specification can be a `STRING` literal or a
query parameter.

#### The language tag

The language tag determines how strings are generally sorted and compared.
Allowed values for `language_tag` are:

+ A standard locale string: This name is usually two or three letters
  that represent the language, optionally followed by an underscore or dash and
  two letters that represent the region &mdash; for example, `en_US`. These
  names are defined by the
  [Common Locale Data Repository (CLDR)][unicode-locale-identifier].
+ `und`: A locale string representing the _undetermined_ locale. `und` is a
  special language tag defined in the
  [IANA language subtag registry][iana-language-subtag-registry] and used to
  indicate an undetermined locale. This is also known as the _root_ locale and
  can be considered the _default_ Unicode collation. It defines a reasonable,
  locale agnostic collation. It differs significantly from
  `binary`.
+ `unicode`: Identical to `binary`. It's recommended to migrate `unicode`
  to `binary`.

Additionally, you can append a language tag with an extension. To learn more,
see [extensions][collate-extensions] for the language tag.

#### The collation attribute

In addition to the language tag, the unicode collation specification can have
an optional `collation_attribute`, which enables additional rules for sorting
and comparing strings. Allowed values are:

+ `ci`: Collation is case-insensitive.
+ `cs`: Collation is case-sensitive. By default, `collation_attribute` is
  implicitly `cs`.

If you're using the `unicode` language tag with a collation attribute, these
caveats apply:

+ `unicode:cs` is identical to `unicode`.
+ `unicode:ci` is identical to `und:ci`. It's recommended to migrate
  `unicode:ci` to `binary`.

#### Collation specification example

This is what the `ci` collation attribute looks like when used with the
`und` language tag in the `COLLATE` function:

```googlesql
COLLATE('orange1', 'und:ci')
```

This is what the `ci` collation attribute looks like when used with the
`und` language tag in the `ORDER BY` clause:

```googlesql
SELECT Place
FROM Locations
ORDER BY Place COLLATE 'und:ci'
```

#### Extensions 
<a id="collation_extensions"></a>

The [Unicode Collation Algorithm][tr10-collation-algorithm] standard
includes some useful locale extensions. In GoogleSQL, a `language_tag`
may be extended by appending `-u-[extension]` to it and replacing `[extension]`
with your desired [Unicode local extension][tr35-collation-settings].

This is what the `kn-true` extension looks like when used with the
`en-us` language tag in the `ORDER BY` clause:

For example:

```googlesql
SELECT *
FROM UNNEST([
  'a12b',
  'a1b'
]) AS ids
ORDER BY ids COLLATE 'en-us-u-kn-true'

/*-------+
 | ids   |
 +-------+
 | a1b   |
 | a12b  |
 +-------*/
```

```googlesql
SELECT *
FROM UNNEST([
  'a12b',
  'a1b'
]) AS ids
ORDER BY ids COLLATE 'en-us-u-kn-false'

/*-------+
 | ids   |
 +-------+
 | a12b  |
 | a1b   |
 +-------*/
```

Here are some commonly used extensions:

<table>
  <thead>
    <tr>
      <th>Extension</th>
      <th>Name</th>
      <th>Example</th>
    </tr>
  </thead>
  <tbody>
    <tr>
      <td>ks-level2</td>
      <td>Case-Insensitive</td>
      <td>"a1" &lt; "A2"</td>
    </tr>
    <tr>
      <td>ks-level1</td>
      <td>Accent and Case-Insensitive</td>
      <td>"ä1" &lt; "a2" &lt; "A3"</td>
    </tr>
    <tr>
      <td>ks-level1-kc-true</td>
      <td>Accent Insensitive</td>
      <td>"ä1" &lt; "a2"</td>
    </tr>
    <tr>
      <td>kn-true</td>
      <td>Numeric Ordering</td>
      <td>"a1b" &lt; "a12b"</td>
    </tr>
  </tbody>
</table>

For a complete list and in depth technical details, consult
[Unicode Locale Data Markup Language Part 5: Collation]
[tr35-collation-settings].

#### Caveats

+ Differing strings can be considered equal.
  For instance, `ẞ` (LATIN CAPITAL LETTER SHARP S) is considered equal to `'SS'`
  in some contexts. The following expressions both evaluate to `TRUE`:

  + `COLLATE('ẞ', 'und:ci') > COLLATE('SS', 'und:ci')`
  + `COLLATE('ẞ1', 'und:ci') < COLLATE('SS2', 'und:ci')`

  This is similar to how case insensitivity works.
+ In search operations, strings with different lengths could be considered
  equal. To ensure consistency, collation should be used without
  search tailoring.
+ There are a wide range of unicode code points (punctuation, symbols, etc),
  that are treated as if they aren't there. So strings with
  and without them are sorted identically. For example, the format control
  code point `U+2060` is ignored when the following strings are sorted:

  ```googlesql
  SELECT *
  FROM UNNEST([
    COLLATE('oran\u2060ge1', 'und:ci'),
    COLLATE('\u2060orange2', 'und:ci'),
    COLLATE('orange3', 'und:ci')
  ]) AS fruit
  ORDER BY fruit

 /*---------+
  | fruit   |
  +---------+
  | orange1 |
  | orange2 |
  | orange3 |
  +---------*/
  ```
+ Ordering _may_ change. The Unicode specification of the `und` collation can
  change occasionally, which can affect sorting
  order. If you need a stable sort order that's
  guaranteed to never change, use `unicode` collation.

## Limitations

Limitations for supported features are captured in the previous
sections, but here are a few general limitations to keep in mind:

+ Table functions can't take table arguments with collated columns.

  ```googlesql
  CREATE TABLE FUNCTION my_dataset.my_tvf(x TABLE<col_str STRING>) AS (
    SELECT col_str FROM x
  );

  SELECT * FROM my_dataset.my_tvf(
    (SELECT COLLATE('abc', 'und:ci') AS col_str)
  );

  -- User error:
  -- "Collation 'und:ci' on column col_str of argument of TVF call isn't allowed"
  ```

<!-- mdlint off(WHITESPACE_LINE_LENGTH) -->

[unicode-code-point]: https://en.wikipedia.org/wiki/List_of_Unicode_characters

[iana-language-subtag-registry]: https://www.iana.org/assignments/language-subtag-registry/language-subtag-registry

[unicode-locale-identifier]: https://www.unicode.org/reports/tr35/#Unicode_locale_identifier

[tr35-collation-settings]: http://www.unicode.org/reports/tr35/tr35-collation.html#Setting_Options

[tr10-collation-algorithm]: http://www.unicode.org/reports/tr10/

[collate-operations]: #collate_operations

[collate-define]: #collate_define

[collate-propagate]: #collate_propagate

[collate-spec-details]: #collate_spec_details

[collate-funcs]: #collate_funcs

[collate-query]: #collate_query

[collate-dts]: #collate_data_types

[collate-ddl]: #collate_ddl

[unicode-collation]: #unicode_collation

[binary-collation]: #binary_collation

[functions-propagation]: #functions_propagation

[operators-propagation]: #operators_propagation

[expressions-propagation]: #expressions_propagation

[collate-extensions]: #collation_extensions

[limitations]: #limitations

[order-by-clause]: https://github.com/google/googlesql/blob/master/docs/query-syntax.md#order_by_clause

[collate-clause]: https://github.com/google/googlesql/blob/master/docs/query-syntax.md#collate_clause

[create-schema]: #create_schema_statement
[create-table]: #create_table_statement
[alter-schema]: #alter_schema_collate_statement
[alter-table]: #alter_table_collate_statement
[alter-column]: #alter_column_set_data_type_statement
[add-column]: #alter_table_add_column_statement

[string-dt]: https://github.com/google/googlesql/blob/master/docs/data-types.md#string_type

[struct-dt]: https://github.com/google/googlesql/blob/master/docs/data-types.md#struct_type

[array-dt]: https://github.com/google/googlesql/blob/master/docs/data-types.md#array_type

[join-types]: https://github.com/google/googlesql/blob/master/docs/query-syntax.md#join_types

[group-by-clause]: https://github.com/google/googlesql/blob/master/docs/query-syntax.md#group_by_clause

[window-clause]: https://github.com/google/googlesql/blob/master/docs/query-syntax.md#window_clause

[set-operators]: https://github.com/google/googlesql/blob/master/docs/query-syntax.md#set_operators

[unnest-operator]: https://github.com/google/googlesql/blob/master/docs/query-syntax.md#unnest_operator

[aead_decrypt_string]: https://github.com/google/googlesql/blob/master/docs/aead_encryption_functions.md.md#aeaddecrypt_string

[any-value]: https://github.com/google/googlesql/blob/master/docs/aggregate_functions.md#any_value

[approx-top-count]: https://github.com/google/googlesql/blob/master/docs/approximate_aggregate_functions.md#approx_top_count

[array-agg]: https://github.com/google/googlesql/blob/master/docs/aggregate_functions.md#array_agg

[array-to-string]: https://github.com/google/googlesql/blob/master/docs/array_functions.md#array_to_string

[array-slice]: https://github.com/google/googlesql/blob/master/docs/array_functions.md#array_slice

[array-first]: https://github.com/google/googlesql/blob/master/docs/array_functions.md#array_first

[array-last]: https://github.com/google/googlesql/blob/master/docs/array_functions.md#array_last

[cast]: https://github.com/google/googlesql/blob/master/docs/conversion_functions.md

[collate]: https://github.com/google/googlesql/blob/master/docs/string_functions.md#collate

[concat]: https://github.com/google/googlesql/blob/master/docs/string_functions.md#concat

[count]: https://github.com/google/googlesql/blob/master/docs/aggregate_functions.md#count

[ends-with]: https://github.com/google/googlesql/blob/master/docs/string_functions.md#ends_with

[format-func]: https://github.com/google/googlesql/blob/master/docs/string_functions.md#format_string

[format-date]: https://github.com/google/googlesql/blob/master/docs/date_functions.md#format_date

[format-datetime]: https://github.com/google/googlesql/blob/master/docs/datetime_functions.md#format_datetime

[format-time]: https://github.com/google/googlesql/blob/master/docs/time_functions.md#format_time

[format-timestamp]: https://github.com/google/googlesql/blob/master/docs/timestamp_functions.md#format_timestamp

[from-proto]: https://github.com/google/googlesql/blob/master/docs/protocol_buffer_functions.md#from_proto

[greatest]: https://github.com/google/googlesql/blob/master/docs/mathematical_functions.md#greatest

[initcap]: https://github.com/google/googlesql/blob/master/docs/string_functions.md#initcap

[instr]: https://github.com/google/googlesql/blob/master/docs/string_functions.md#instr

[json-extract]: https://github.com/google/googlesql/blob/master/docs/json_functions.md#json_extract

[json-extract-array]: https://github.com/google/googlesql/blob/master/docs/json_functions.md#json_extract_array

[json-extract-scalar]: https://github.com/google/googlesql/blob/master/docs/json_functions.md#json_extract_scalar

[json-extract-string-array]: https://github.com/google/googlesql/blob/master/docs/json_functions.md#json_extract_string_array

[json-query]: https://github.com/google/googlesql/blob/master/docs/json_functions.md#json_query

[json-query-array]: https://github.com/google/googlesql/blob/master/docs/json_functions.md#json_query_array

[json-value]: https://github.com/google/googlesql/blob/master/docs/json_functions.md#json_value

[json-value-array]: https://github.com/google/googlesql/blob/master/docs/json_functions.md#json_value_array

[lag]: https://github.com/google/googlesql/blob/master/docs/navigation_functions.md#lag

[lead]: https://github.com/google/googlesql/blob/master/docs/navigation_functions.md#lead

[least]: https://github.com/google/googlesql/blob/master/docs/mathematical_functions.md#least

[left]: https://github.com/google/googlesql/blob/master/docs/string_functions.md#left

[lower]: https://github.com/google/googlesql/blob/master/docs/string_functions.md#lower

[lpad]: https://github.com/google/googlesql/blob/master/docs/string_functions.md#lpad

[max]: https://github.com/google/googlesql/blob/master/docs/aggregate_functions.md#max

[min]: https://github.com/google/googlesql/blob/master/docs/aggregate_functions.md#min

[nethost]: https://github.com/google/googlesql/blob/master/docs/net_functions.md#nethost

[netmake-net]: https://github.com/google/googlesql/blob/master/docs/net_functions.md#netmake_net

[netpublic-suffix]: https://github.com/google/googlesql/blob/master/docs/net_functions.md#netpublic_suffix

[netreg-domain]: https://github.com/google/googlesql/blob/master/docs/net_functions.md#netreg_domain

[nth-value]: https://github.com/google/googlesql/blob/master/docs/navigation_functions.md#nth_value

[normalize]: https://github.com/google/googlesql/blob/master/docs/string_functions.md#normalize

[normalize-and-casefold]: https://github.com/google/googlesql/blob/master/docs/string_functions.md#normalize_and_casefold

[nulliferror]: https://github.com/google/googlesql/blob/master/docs/debugging_functions.md#nulliferror

[proto-default-if-null]: https://github.com/google/googlesql/blob/master/docs/protocol_buffer_functions.md#proto_default_if_null

[repeat]: https://github.com/google/googlesql/blob/master/docs/string_functions.md#repeat

[replace]: https://github.com/google/googlesql/blob/master/docs/string_functions.md#replace

[reverse]: https://github.com/google/googlesql/blob/master/docs/string_functions.md#reverse

[right]: https://github.com/google/googlesql/blob/master/docs/string_functions.md#right

[rpad]: https://github.com/google/googlesql/blob/master/docs/string_functions.md#rpad

[soundex]: https://github.com/google/googlesql/blob/master/docs/string_functions.md#soundex

[split]: https://github.com/google/googlesql/blob/master/docs/string_functions.md#split

[starts-with]: https://github.com/google/googlesql/blob/master/docs/string_functions.md#starts_with

[string-func]: https://github.com/google/googlesql/blob/master/docs/conversion_functions.md#other_conv_functions

[string-agg]: https://github.com/google/googlesql/blob/master/docs/aggregate_functions.md#string_agg

[strpos]: https://github.com/google/googlesql/blob/master/docs/string_functions.md#strpos

[substr]: https://github.com/google/googlesql/blob/master/docs/string_functions.md#substr

[upper]: https://github.com/google/googlesql/blob/master/docs/string_functions.md#upper

[comparison-op]: https://github.com/google/googlesql/blob/master/docs/operators.md#comparison_operators

[in-op]: https://github.com/google/googlesql/blob/master/docs/operators.md#in_operators

[like-op]: https://github.com/google/googlesql/blob/master/docs/operators.md#like_operator

[q-like-op]: https://github.com/google/googlesql/blob/master/docs/operators.md#like_operator_quantified

[concat-op]: https://github.com/google/googlesql/blob/master/docs/operators.md#concatenation_operator

[field-access-operator]: https://github.com/google/googlesql/blob/master/docs/operators.md#field_access_operator

[array-subscript-operator]: https://github.com/google/googlesql/blob/master/docs/operators.md#array_subscript_operator

[case]: https://github.com/google/googlesql/blob/master/docs/conditional_expressions.md#case

[case-expr]: https://github.com/google/googlesql/blob/master/docs/conditional_expressions.md#case_expr

[coalesce]: https://github.com/google/googlesql/blob/master/docs/conditional_expressions.md#coalesce

[if]: https://github.com/google/googlesql/blob/master/docs/conditional_expressions.md#if

[ifnull]: https://github.com/google/googlesql/blob/master/docs/conditional_expressions.md#ifnull

[nullif]: https://github.com/google/googlesql/blob/master/docs/conditional_expressions.md#nullif

<!-- mdlint on -->

