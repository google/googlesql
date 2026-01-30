

<!-- mdlint off(WHITESPACE_LINE_LENGTH) -->

# Lexical structure and syntax

A GoogleSQL statement comprises a series of tokens. Tokens include
identifiers, quoted identifiers, literals, keywords, operators, and
special characters. You can separate tokens with comments or whitespace such
as spaces, backspaces, tabs, or newlines.

## Identifiers 
<a id="identifiers"></a>

Identifiers are names that are associated with columns, tables,
fields, path expressions, and more. They can be
[unquoted][unquoted-identifiers] or [quoted][quoted-identifiers] and some
are [case-sensitive][case-sensitivity].

### Unquoted identifiers 
<a id="unquoted_identifiers"></a>

+ Must begin with a letter or an underscore (_) character.
+ Subsequent characters can be letters, numbers, or underscores (_).

### Quoted identifiers 
<a id="quoted_identifiers"></a>

+ Must be enclosed by backtick (`) characters.
+ Can contain any characters, including spaces and symbols.
+ Can't be empty.
+ Have the same escape sequences as [string literals][string-literals].
+ If an identifier is the same as a [reserved keyword](#reserved_keywords), the
  identifier must be quoted. For example, the identifier `FROM` must be quoted.
  Additional rules apply for [path expressions][path-expressions]
  ,
  [table names][table-names], [column names][column-names], and
  [field names][field-names].

### Identifier examples

Path expression examples:

```googlesql
-- Valid. _5abc and dataField are valid identifiers.
_5abc.dataField

-- Valid. `5abc` and dataField are valid identifiers.
`5abc`.dataField

-- Invalid. 5abc is an invalid identifier because it's unquoted and starts
-- with a number rather than a letter or underscore.
5abc.dataField

-- Valid. abc5 and dataField are valid identifiers.
abc5.dataField

-- Invalid. abc5! is an invalid identifier because it's unquoted and contains
-- a character that isn't a letter, number, or underscore.
abc5!.dataField

-- Valid. `GROUP` and dataField are valid identifiers.
`GROUP`.dataField

-- Invalid. GROUP is an invalid identifier because it's unquoted and is a
-- stand-alone reserved keyword.
GROUP.dataField

-- Valid. abc5 and GROUP are valid identifiers.
abc5.GROUP
```

Function examples:

```googlesql
-- Valid. dataField is a valid identifier in a function called foo().
foo().dataField
```

Array access operation examples:

```googlesql
-- Valid. dataField is a valid identifier in an array called items.
items[OFFSET(3)].dataField
```

Named query parameter examples:

```googlesql
-- Valid. param and dataField are valid identifiers.
@param.dataField
```

Protocol buffer examples:

```googlesql
-- Valid. dataField is a valid identifier in a protocol buffer called foo.
(foo).dataField
```

Table name examples:

```googlesql
-- Valid table name.
mytable287
```

```googlesql
-- Invalid table name. The table name starts with a number and is
-- unquoted.
287mytable
```

```googlesql
-- Invalid table name. The table name is unquoted and isn't a valid
-- dashed identifier, as the part after the dash is neither a number nor
-- an identifier starting with a letter or an underscore.
mytable-287a
```

## Path expressions

A path expression describes how to navigate to an object in a graph of objects
and generally follows this structure:

```none
path:
  [path_expression][. ...]

path_expression:
  [first_part]/subsequent_part[ { / | : | - } subsequent_part ][...]

first_part:
  { unquoted_identifier | quoted_identifier }

subsequent_part:
  { unquoted_identifier | quoted_identifier | number }
```

+ `path`: A graph of one or more objects.
+ `path_expression`: An object in a graph of objects.
+ `first_part`: A path expression can start with a quoted or
  unquoted identifier. If the path expressions starts with a
  [reserved keyword](#reserved_keywords), it must be a quoted identifier.
+ `subsequent_part`: Subsequent parts of a path expression can include
  non-identifiers, such as reserved keywords. If a subsequent part of a
  path expressions starts with a [reserved keyword](#reserved_keywords), it
  may be quoted or unquoted.

Examples:

```none
foo.bar
foo.bar/25
foo/bar:25
foo/bar/25-31
/foo/bar
/25/foo/bar
```

## Table names

A table name represents the name of a table.

+ Table names can be quoted identifiers or unquoted identifiers.
+ Table names can be path expressions.
+ Table names have [case-sensitivity rules][case-sensitivity].

Examples:

```none
mytable
`287mytable`
```

## Column names

A column name represents the name of a column in a table.

+ Column names can be quoted identifiers or unquoted identifiers.
+ If unquoted, identifiers support dashed identifiers when referenced in a
  `FROM` or `TABLE` clause.

Examples:

```none
columnA
column-a
`287column`
```

## Field names

A field name represents the name of a field inside a complex data type such
as a struct,
protocol buffer message, or JSON object.

+ A field name can be a quoted identifier or an unquoted identifier.
+ Field names must adhere to all of the rules for column names.

## Literals 
<a id="literals"></a>

A literal represents a constant value of a built-in data type. Some, but not
all, data types can be expressed as literals.

### Tokens in literals

A literal can contain one or more tokens. For example:

  ```googlesql
  -- This date literal has one token: '2014-01-31'
  SELECT DATE '2014-01-31'
  ```

  ```googlesql
  -- This date literal has three tokens: '2014', '-01', and '-31'
  SELECT DATE '2014' '-01' '-31'
  ```

When a literal contains multiple tokens, the tokens must be separated by
whitespace, comments, or both. For example, the following date literals
produce the same results:

  ```googlesql
  SELECT DATE '2014-01-31'
  ```

  ```googlesql
  SELECT DATE '2014' '-01' '-31'
  ```

  ```googlesql
  SELECT DATE /* year */ '2014' /* month */ '-01' /* day */ '-31'
  ```

  ```googlesql
  SELECT DATE /* year and month */ '2014' '-01' /* day */ '-31'
  ```

A token can be a `STRING` type or a `BYTES` type. String tokens can only be
used with string tokens and bytes tokens can only be used with
bytes tokens. If you try to use them together in a literal, an error is
produced. For example:

  ```googlesql
  -- The following string literal contains string tokens.
  SELECT 'x' 'y' 'z'
  ```

  ```googlesql
  -- The following bytes literal contains bytes tokens.
  SELECT b'x' b'y' b'z'
  ```

  ```googlesql
  -- Error: string and bytes tokens can't be used together in the same literal.
  SELECT 'x' b'y'
  ```

String tokens can be one of the following
[format types][quoted-literals] and used together:

  +   Quoted string
  +   Triple-quoted string
  +   Raw string

  If a raw string is used, it's applied to the immediate token, but not
  to the results.

  Examples:

  ```googlesql
  -- Compatible format types can be used together in a string literal.
  SELECT 'abc' "d" '''ef'''

  /*--------+
   | abcdef |
   +--------*/
  ```

  ```googlesql
  -- \n is escaped in the raw string token but not in the quoted string token.
  SELECT '\na' r"\n"

  /*-----+
   |     |
   | a\n |
   +-----*/
  ```

Bytes tokens can be one of the following
[format types][quoted-literals] and used together:

  +   Bytes
  +   Raw bytes

  If raw bytes are used, they're applied to the immediate token, but not to
  the results.

  Examples:

  ```googlesql
  -- Compatible format types can be used together in a bytes literal.
  SELECT b'\x41' b'''\x42''' b"""\x41"""

  /*-----+
   | ABA |
   +-----*/
  ```

  ```googlesql
  -- Control characters are escaped in the raw bytes tokens but not in the
  -- bytes token.
  SELECT b'\x41' RB'\x42' br'\x41'

  /*-------------+
   | A\\x42\\x41 |
   +-------------*/
  ```

Additional examples:

```googlesql
-- The following JSON literal is equivalent to: JSON '{"name":"my_file.md","regex":"\\d+"}'
SELECT JSON '{"name": "my_file.md", "regex": ' /*start*/ r' "\\d+"' /*end*/ '}'

/*--------------------------------------+
 | {"name":"my_file.md","regex":"\\d+"} |
 +--------------------------------------*/
```

```googlesql
-- The following NUMERIC literal is equivalent to: NUMERIC '-1.2'
SELECT NUMERIC '-' "1" '''.''' r'2'

/*------+
 | -1.2 |
 +------*/
```

```googlesql
-- The following NUMERIC literal is equivalent to: NUMERIC '1.23e-6 '
SELECT NUMERIC "1" '''.'''' r'23' 'e-6'

/*------------+
 | 0.00000123 |
 +------------*/
```

```googlesql
-- The following DATE literal is equivalent to: DATE '2014-01-31'
SELECT DATE /* year */ '2014' /* month and day */ "-01-31"

/*------------+
 | 2014-01-31 |
 +------------*/
```

```googlesql
-- Error: Illegal escape sequence found in '\def'.
SELECT r'abc' '\def'
```

```googlesql
-- Error: backticks are reserved for quoted identifiers and not a valid
-- format type.
SELECT `abc` `def` AS results;
```

### String and bytes literals 
<a id="string_and_bytes_literals"></a>

A string literal represents a constant value of the
[string data type][string-data-type]. A bytes literal represents a
constant value of the [bytes data type][bytes-data-type].

Both string and bytes literals must be *quoted*, either with single (`'`) or
double (`"`) quotation marks, or *triple-quoted* with groups of three single
(`'''`) or three double (`"""`) quotation marks.

#### Formats for quoted literals 
<a id="quoted_literals"></a>

The following table lists all of the ways you can format a quoted literal.

<table>
<thead>
<tr>
<th>Literal</th>
<th>Examples</th>
<th>Description</th>
</tr>
</thead>
<tbody>
<tr>
<td>Quoted string</td>
<td><ul><li><code>"abc"</code></li><li><code>"it's"</code></li><li><code>'it\'s'</code></li><li><code>'Title: "Boy"'</code></li></ul></td>
<td>Quoted strings enclosed by single (<code>'</code>) quotes can contain unescaped double (<code>"</code>) quotes, as well as the inverse. <br>Backslashes (<code>\</code>) introduce escape sequences. See the Escape Sequences table below.<br>Quoted strings can't contain newlines, even when preceded by a backslash (<code>\</code>).</td>
</tr>
<tr>
<td>Triple-quoted string</td>
<td><ul><li><code>"""abc"""</code></li><li><code>'''it's'''</code></li><li><code>'''Title:"Boy"'''</code></li><li><code>'''two<br>lines'''</code></li><li><code>'''why\?'''</code></li></ul></td>
<td>Embedded newlines and quotes are allowed without escaping - see fourth example.<br>Backslashes (<code>\</code>) introduce escape sequences. See Escape Sequences table below.<br>A trailing unescaped backslash (<code>\</code>) at the end of a line isn't allowed.<br>End the string with three unescaped quotes in a row that match the starting quotes.</td>
</tr>
<tr>
<td>Raw string</td>
<td><ul><li><code>r"abc+"</code></li><li> <code>r'''abc+'''</code></li><li> <code>r"""abc+"""</code></li><li><code>r'f\(abc,(.*),def\)'</code></li></ul></td>
<td>Quoted or triple-quoted literals that have the raw string literal prefix (<code>r</code> or <code>R</code>) are interpreted as raw strings (sometimes described as regex strings).<br>Backslash characters (<code>\</code>) don't act as escape characters. If a backslash followed by another character occurs inside the string literal, both characters are preserved.<br>A raw string can't end with an odd number of backslashes.<br>Raw strings are useful for constructing regular expressions.
The prefix is case-insensitive.
</td>
</tr>
<tr>
<td>Bytes</td>
<td><ul><li><code>B"abc"</code></li><li><code>B'''abc'''</code></li><li><code>b"""abc"""</code></li></ul></td>
<td>Quoted or triple-quoted literals that have the bytes literal prefix (<code>b</code> or <code>B</code>) are interpreted as bytes.</td>
</tr>
<tr>
<td>Raw bytes</td>
<td><ul><li><code>br'abc+'</code></li><li><code>RB"abc+"</code></li><li><code>RB'''abc'''</code></li></ul></td>
<td>A bytes literal can be interpreted as raw bytes if both the
<code>r</code> and <code>b</code> prefixes are present. These prefixes can be
combined in any order and are case-insensitive. For example,
<code>rb'abc*'</code> and <code>rB'abc*'</code> and <code>br'abc*'</code> are
all equivalent. See the description for raw string to learn more about
what you can do with a raw literal.
</td>
</tr>
</tbody>
</table>

Like in many other languages, such as Python and C++, you can divide a
GoogleSQL string or bytes literal into chunks, each with its own quoting
or raw specification. The literal value is the concatenation of all these parts.

This is useful for a variety of purposes, including readability, organization,
formatting and maintainability, for example:

+   You can break a literal into multiple chunks fit into a width of 80
    characters.
+   You can break a literal into chunks of different quotings and raw
    specifications to avoid escaping. For example, a string value inside a
    `JSON` string.
+   You can change only one part of a literal through a macro, while the rest of
    the literal is unchanged.
+   You can use string literal concatenation in other literals that include
    strings such as `DATE`, `TIMESTAMP`, `JSON`, etc.

The following restrictions apply to these literal concatenations:

+   You can't mix string and byte literals.
+   You must ensure there is some separation between the concatenated parts,
    such as whitespace or comments.
+   `r` specifiers apply only to the immediate chunk, not the rest of the
    literal parts.
+   Quoted identifiers don't concatenate.

Examples:

<table>
<thead>
<tr>
  <th>Literals divided into chunks</th>
  <th>Equivalent literals</th>
</tr>
</thead>
<tbody>
<tr>
<td>
<pre><code class="lang-sql">SELECT
 r'\n' /*Only the prev is raw!*/ '\n' "b" """c"d"e""" '''f'g'h''' "1" "2",
 br'\n'/*Only the prev is raw!*/ b'\n' b"b" b"""c"d"e""" b'''f'g'h''' b"1" b"2",
  NUMERIC "1" r'2',
  DECIMAL /*whole:*/ '1' /*fractional:*/ ".23" /*exponent=*/ "e+6",
  BIGNUMERIC '1' r"2",
  BIGDECIMAL /*sign*/ '-' /*whole:*/ '1' /*fractional:*/ ".23" /*exponent=*/ "e+6",
  RANGE&lt;DATE&gt; '[2014-01-01,' /*comment*/ "2015-01-01)",
  DATE '2014' "-01-01",
  DATETIME '2016-01-01 ' r"12:00:00",
  TIMESTAMP '2018-10-01 ' "12:00:00+08"
</code>
</pre>
</td>
<td>
<pre><code class="lang-sql">SELECT
 "\\n\nbc\"d\"ef'g'h12",
 b"\\n\nbc\"d\"ef'g'h12",
  NUMERIC "12",
  DECIMAL '1.23e+6',
  BIGNUMERIC '12',
  BIGDECIMAL "-1.23e+6",
  RANGE&lt;DATE&gt; '[2014-01-01 2015-01-01)',
  DATE '2014-01-01',
  DATETIME '2016-01-01 12:00:00',
  TIMESTAMP "2018-10-01 12:00:00+08"
</code>
</pre>
</td>
</tr>
</tbody>
</table>

#### Escape sequences for string and bytes literals 
<a id="escape_sequences"></a>

The following table lists all valid escape sequences for representing
non-alphanumeric characters in string and bytes literals. Any sequence not in
this table produces an error.

<table>
<thead>
<tr>
<th>Escape Sequence</th>
<th>Description</th>
</tr>
</thead>
<tbody>
<tr>
<td><code>\a</code></td>
<td>Bell</td>
</tr>
<tr>
<td><code>\b</code></td>
<td>Backspace</td>
</tr>
<tr>
<td><code>\f</code></td>
<td>Formfeed</td>
</tr>
<tr>
<td><code>\n</code></td>
<td>Newline</td>
</tr>
<tr>
<td><code>\r</code></td>
<td>Carriage Return</td>
</tr>
<tr>
<td><code>\t</code></td>
<td>Tab</td>
</tr>
<tr>
<td><code>\v</code></td>
<td>Vertical Tab</td>
</tr>
<tr>
<td><code>\\</code></td>
<td>Backslash (<code>\</code>)</td>
</tr>
<tr>
<td><code>\?</code></td>
<td>Question Mark (<code>?</code>)</td>
</tr>
<tr>
<td><code>\"</code></td>
<td>Double Quote (<code>"</code>)</td>
</tr>
<tr>
<td><code>\'</code></td>
<td>Single Quote (<code>'</code>)</td>
</tr>
<tr>
<td><code>\`</code></td>
<td>Backtick (<code>`</code>)</td>
</tr>
<tr>
<td><code>\ooo</code></td>
<td>Octal escape, with exactly 3 digits (in the range 0–7). Decodes to a single Unicode character (in string literals) or byte (in bytes literals).</td>
</tr>
<tr>
<td><code>\xhh</code> or <code>\Xhh</code></td>
<td>Hex escape, with exactly 2 hex digits (0–9 or A–F or a–f). Decodes to a single Unicode character (in string literals) or byte (in bytes literals). Examples:<ul style="list-style-type:none"><li><code>'\x41'</code> == <code>'A'</code></li><li><code>'\x41B'</code> is <code>'AB'</code></li><li><code>'\x4'</code> is an error</li></ul></td>
</tr>
<tr>
<td><code>\uhhhh</code></td>
<td>Unicode escape, with lowercase 'u' and exactly 4 hex digits. Valid only in string literals or identifiers.<br/>Note that the range D800-DFFF isn't allowed, as these are surrogate unicode values.</td>
</tr>
<tr>
<td><code>\Uhhhhhhhh</code></td>
<td>Unicode escape, with uppercase 'U' and exactly 8 hex digits. Valid only in string literals or identifiers.<br/>The range D800-DFFF isn't allowed, as these values are surrogate unicode values. Also, values greater than 10FFFF aren't allowed.</td>
</tr>
</tbody>
</table>

### Integer literals 
<a id="integer_literals"></a>

Integer literals are either a sequence of decimal digits (0–9) or a hexadecimal
value that's prefixed with "`0x`" or "`0X`". Integers can be prefixed by "`+`"
or "`-`" to represent positive and negative values, respectively.
Examples:

```
123
0xABC
-123
```

An integer literal is interpreted as an `INT64`.

Coercion (implicit casting) of integer literals to other integer types can occur
if casting doesn't result in truncation. For example, if the integer 55 of type
`INT32` is compared to the integer literal 77, the
literal value 77 is coerced into type `INT32` because
`77` can be represented by the `INT32` type.

A integer literal represents a constant value of the
[integer data type][integer-data-type].

### `NUMERIC` literals

You can construct `NUMERIC` literals using the
`NUMERIC` keyword followed by a floating point value in quotes.

Examples:

```googlesql
SELECT NUMERIC '0';
SELECT NUMERIC '123456';
SELECT NUMERIC '-3.14';
SELECT NUMERIC '-0.54321';
SELECT NUMERIC '1.23456e05';
SELECT NUMERIC '-9.876e-3';
```

A `NUMERIC` literal represents a constant value of the
[`NUMERIC` data type][decimal-data-type].

### `BIGNUMERIC` literals

You can construct `BIGNUMERIC` literals using the `BIGNUMERIC` keyword followed
by a floating point value in quotes.

Examples:

```googlesql
SELECT BIGNUMERIC '0';
SELECT BIGNUMERIC '123456';
SELECT BIGNUMERIC '-3.14';
SELECT BIGNUMERIC '-0.54321';
SELECT BIGNUMERIC '1.23456e05';
SELECT BIGNUMERIC '-9.876e-3';
```

A `BIGNUMERIC` literal represents a constant value of the
[`BIGNUMERIC` data type][decimal-data-type].

### Floating point literals 
<a id="floating_point_literals"></a>

Syntax options:

```googlesql
[+-]DIGITS.[DIGITS][e[+-]DIGITS]
[+-][DIGITS].DIGITS[e[+-]DIGITS]
DIGITSe[+-]DIGITS
```

`DIGITS` represents one or more decimal numbers (0 through 9) and `e` represents
the exponent marker (e or E).

Examples:

```
123.456e-67
.1E4
58.
4e2
```

Numeric literals that contain
either a decimal point or an exponent marker are presumed to be type double.

Implicit coercion of floating point literals to float type is possible if the
value is within the valid float range.

There is no literal
representation of NaN or infinity, but the following case-insensitive strings
can be explicitly cast to float:

 + "NaN"
 + "inf" or "+inf"
 + "-inf"

A floating-point literal represents a constant value of the
[floating-point data type][floating-point-data-type].

### Array literals 
<a id="array_literals"></a>

Array literals are comma-separated lists of elements
enclosed in square brackets. The `ARRAY` keyword is optional, and an explicit
element type T is also optional.

You can write an empty array of a specific type using `ARRAY<type>[]`. You can
also write an untyped empty array using `[]`, in which case GoogleSQL
attempts to infer the array type from the surrounding context. If
GoogleSQL can't infer a type, the default type `ARRAY<INT64>` is used.

Examples:

```googlesql
[1, 2, 3]
['x', 'y', 'xy']
ARRAY[1, 2, 3]
ARRAY<string>['x', 'y', 'xy']
ARRAY<int64>[]
[]
```

An array literal represents a constant value of the
[array data type][array-data-type].

### Struct literals

A struct literal is a struct whose fields are all literals. Struct literals can
be written using any of the syntaxes for [constructing a
struct][constructing-a-struct] (tuple syntax, typeless struct syntax, or typed
struct syntax).

Note that tuple syntax requires at least two fields, in order to distinguish it
from an ordinary parenthesized expression. To write a struct literal with a
single field, use typeless struct syntax or typed struct syntax.

<table>
<thead>
<tr>
<th>Example</th>
<th>Output Type</th>
</tr>
</thead>
<tbody>
<tr>
<td><code>(1, 2, 3)</code></td>
<td><code>STRUCT&lt;INT64, INT64, INT64&gt;</code></td>
</tr>
<tr>
<td><code>(1, 'abc')</code></td>
<td><code>STRUCT&lt;INT64, STRING&gt;</code></td>
</tr>
<tr>
<td><code>STRUCT(1 AS foo, 'abc' AS bar)</code></td>
<td><code>STRUCT&lt;foo INT64, bar STRING&gt;</code></td>
</tr>
<tr>
<td><code>STRUCT&lt;INT64, STRING&gt;(1, 'abc')</code></td>
<td><code>STRUCT&lt;INT64, STRING&gt;</code></td>
</tr>
<tr>
<td><code>STRUCT(1)</code></td>
<td><code>STRUCT&lt;INT64&gt;</code></td>
</tr>
<tr>
<td><code>STRUCT&lt;INT64&gt;(1)</code></td>
<td><code>STRUCT&lt;INT64&gt;</code></td>
</tr>
</tbody>
</table>

A struct literal represents a constant value of the
[struct data type][struct-data-type].

### Date literals

Syntax:

```googlesql
DATE 'date_canonical_format'
```

Date literals contain the `DATE` keyword followed by
[`date_canonical_format`][date-format],
a string literal that conforms to the canonical date format, enclosed in single
quotation marks. Date literals support a range between the
years 1 and 9999, inclusive. Dates outside of this range are invalid.

For example, the following date literal represents September 27, 2014:

```googlesql
DATE '2014-09-27'
```

String literals in canonical date format also implicitly coerce to DATE type
when used where a DATE-type expression is expected. For example, in the query

```googlesql
SELECT * FROM foo WHERE date_col = "2014-09-27"
```

the string literal `"2014-09-27"` will be coerced to a date literal.

A date literal represents a constant value of the
[date data type][date-data-type].

 

### Time literals

Syntax:

```googlesql
TIME 'time_canonical_format'
```

Time literals contain the `TIME` keyword and
[`time_canonical_format`][time-format], a string literal that conforms to
the canonical time format, enclosed in single quotation marks.

For example, the following time represents 12:30 p.m.:

```googlesql
TIME '12:30:00.45'
```

A time literal represents a constant value of the
[time data type][time-data-type].

### Datetime literals

Syntax:

```googlesql
DATETIME 'datetime_canonical_format'
```

Datetime literals contain the `DATETIME` keyword and
[`datetime_canonical_format`][datetime-format], a string literal that
conforms to the canonical datetime format, enclosed in single quotation marks.

For example, the following datetime represents 12:30 p.m. on September 27,
2014:

```googlesql
DATETIME '2014-09-27 12:30:00.45'
```

Datetime literals support a range between the years 1 and 9999, inclusive.
Datetimes outside of this range are invalid.

String literals with the canonical datetime format implicitly coerce to a
datetime literal when used where a datetime expression is expected.

For example:

```googlesql
SELECT * FROM foo
WHERE datetime_col = "2014-09-27 12:30:00.45"
```

In the query above, the string literal `"2014-09-27 12:30:00.45"` is coerced to
a datetime literal.

A datetime literal can also include the optional character `T` or `t`. If
you use this character, a space can't be included before or after it.
These are valid:

```googlesql
DATETIME '2014-09-27T12:30:00.45'
DATETIME '2014-09-27t12:30:00.45'
```

A datetime literal represents a constant value of the
[datatime data type][datetime-data-type].

### Timestamp literals

Syntax:

```
TIMESTAMP 'timestamp_canonical_format'
```

Timestamp literals contain the `TIMESTAMP` keyword and
[`timestamp_canonical_format`][timestamp-format], a string literal that
conforms to the canonical timestamp format, enclosed in single quotation marks.

Timestamp literals support a range between the years 1 and 9999, inclusive.
Timestamps outside of this range are invalid.

A timestamp literal can include a numerical suffix to indicate the time zone:

```googlesql
TIMESTAMP '2014-09-27 12:30:00.45-08'
```

If this suffix is absent, the default time zone,
which is implementation defined, is used.

For example, the following timestamp represents 12:30 p.m. on September 27,
2014 in the default time zone, which is implementation defined:

```googlesql
TIMESTAMP '2014-09-27 12:30:00.45'
```

For more information about time zones, see [Time zone][time-zone].

String literals with the canonical timestamp format, including those with
time zone names, implicitly coerce to a timestamp literal when used where a
timestamp expression is expected.  For example, in the following query, the
string literal `"2014-09-27 12:30:00.45 America/Los_Angeles"` is coerced
to a timestamp literal.

```googlesql
SELECT * FROM foo
WHERE timestamp_col = "2014-09-27 12:30:00.45 America/Los_Angeles"
```

A timestamp literal can include these optional characters:

+  `T` or `t`
+  `Z` or `z`

If you use one of these characters, a space can't be included before or after
it. These are valid:

```googlesql
TIMESTAMP '2017-01-18T12:34:56.123456Z'
TIMESTAMP '2017-01-18t12:34:56.123456'
TIMESTAMP '2017-01-18 12:34:56.123456z'
TIMESTAMP '2017-01-18 12:34:56.123456Z'
```

A timestamp literal represents a constant value of the
[timestamp data type][timestamp-data-type].

#### Time zone 
<a id="timezone"></a>

Since timestamp literals must be mapped to a specific point in time, a time zone
is necessary to correctly interpret a literal. If a time zone isn't specified
as part of the literal itself, then GoogleSQL uses the default time zone
value, which the GoogleSQL implementation sets.

GoogleSQL can represent a time zones using a string, which represents
the [offset from Coordinated Universal Time (UTC)][utc-offset].

Examples:

```
'-08:00'
'-8:15'
'+3:00'
'+07:30'
'-7'
```

Time zones can also be expressed using string
[time zone names][time-zone-name].

Examples:

```googlesql
TIMESTAMP '2014-09-27 12:30:00 America/Los_Angeles'
TIMESTAMP '2014-09-27 12:30:00 America/Argentina/Buenos_Aires'
```

### Range literals

Syntax:

```googlesql
RANGE<T> '[lower_bound, upper_bound)'
```

A range literal contains a contiguous range between two
[dates][date-data-type], [datetimes][datetime-data-type], or
[timestamps][timestamp-data-type]. The lower or upper bound can be unbounded,
if desired.

Example of a date range literal with a lower and upper bound:

```googlesql
RANGE<DATE> '[2020-01-01, 2020-12-31)'
```

Example of a datetime range literal with a lower and upper bound:

```googlesql
RANGE<DATETIME> '[2020-01-01 12:00:00, 2020-12-31 12:00:00)'
```

Example of a timestamp range literal with a lower and upper bound:

```googlesql
RANGE<TIMESTAMP> '[2020-10-01 12:00:00+08, 2020-12-31 12:00:00+08)'
```

Examples of a range literal without a lower bound:

```googlesql
RANGE<DATE> '[UNBOUNDED, 2020-12-31)'
```
```googlesql
RANGE<DATE> '[NULL, 2020-12-31)'
```

Examples of a range literal without an upper bound:

```googlesql
RANGE<DATE> '[2020-01-01, UNBOUNDED)'
```
```googlesql
RANGE<DATE> '[2020-01-01, NULL)'
```

Examples of a range literal that includes all possible values:

```googlesql
RANGE<DATE> '[UNBOUNDED, UNBOUNDED)'
```

```googlesql
RANGE<DATE> '[NULL, NULL)'
```

There must be a single whitespace after the comma in a range literal, otherwise
an error is produced. For example:

```googlesql
-- This range literal is valid:
RANGE<DATE> '[2020-01-01, 2020-12-31)'
```

```googlesql
-- This range literal produces an error:
RANGE<DATE> '[2020-01-01,2020-12-31)'
```

A range literal represents a constant value of the
[range data type][range-data-type].

### Interval literals

An interval literal represents a constant value of the
[interval data type][interval-data-type]. There are two types of
interval literals:

+  [Interval literal with a single datetime part][interval-literal-single]
+  [Interval literal with a datetime part range][interval-literal-range]

An interval literal can be used directly inside of the `SELECT` statement
and as an argument in some functions that support the interval data type.

#### Interval literal with a single datetime part 
<a id="interval_literal_single"></a>

Syntax:

```googlesql
INTERVAL int64_expression datetime_part
```

The single datetime part syntax includes an `INT64` expression and a
single [interval-supported datetime part][interval-datetime-parts].
For example:

```googlesql
-- 0 years, 0 months, 5 days, 0 hours, 0 minutes, 0 seconds (0-0 5 0:0:0)
INTERVAL 5 DAY

-- 0 years, 0 months, -5 days, 0 hours, 0 minutes, 0 seconds (0-0 -5 0:0:0)
INTERVAL -5 DAY

-- 0 years, 0 months, 0 days, 0 hours, 0 minutes, 1 seconds (0-0 0 0:0:1)
INTERVAL 1 SECOND
```

When a negative sign precedes the year or month part in an interval literal, the
negative sign distributes over the years and months. Or, when a negative sign
precedes the time part in an interval literal, the negative sign distributes
over the hours, minutes, and seconds. For example:

```googlesql
-- -2 years, -1 months, 0 days, 0 hours, 0 minutes, and 0 seconds (-2-1 0 0:0:0)
INTERVAL -25 MONTH

-- 0 years, 0 months, 0 days, -1 hours, -30 minutes, and 0 seconds (0-0 0 -1:30:0)
INTERVAL -90 MINUTE
```

For more information on how to construct interval with a single datetime part,
see [Construct an interval with a single datetime part][construct-single-interval].

####  Interval literal with a datetime part range 
<a id="interval_literal_range"></a>

Syntax:

```googlesql
INTERVAL datetime_parts_string starting_datetime_part TO ending_datetime_part
```

The range datetime part syntax includes a
[datetime parts string][construct-range-interval],
a [starting datetime part][interval-datetime-parts], and an
[ending datetime part][interval-datetime-parts].

For example:

```googlesql
-- 0 years, 0 months, 0 days, 10 hours, 20 minutes, 30 seconds (0-0 0 10:20:30.520)
INTERVAL '10:20:30.52' HOUR TO SECOND

-- 1 year, 2 months, 0 days, 0 hours, 0 minutes, 0 seconds (1-2 0 0:0:0)
INTERVAL '1-2' YEAR TO MONTH

-- 0 years, 1 month, -15 days, 0 hours, 0 minutes, 0 seconds (0-1 -15 0:0:0)
INTERVAL '1 -15' MONTH TO DAY

-- 0 years, 0 months, 1 day, 5 hours, 30 minutes, 0 seconds (0-0 1 5:30:0)
INTERVAL '1 5:30' DAY TO MINUTE
```

When a negative sign precedes the year or month part in an interval literal, the
negative sign distributes over the years and months. Or, when a negative sign
precedes the time part in an interval literal, the negative sign distributes
over the hours, minutes, and seconds.  For example:

```googlesql
-- -23 years, -2 months, 10 days, -12 hours, -30 minutes, and 0 seconds (-23-2 10 -12:30:0)
INTERVAL '-23-2 10 -12:30' YEAR TO MINUTE

-- -23 years, -2 months, 10 days, 0 hours, -30 minutes, and 0 seconds (-23-2 10 -0:30:0)
SELECT INTERVAL '-23-2 10 -0:30' YEAR TO MINUTE

-- Produces an error because the negative sign for minutes must come before the hour.
SELECT INTERVAL '-23-2 10 0:-30' YEAR TO MINUTE

-- Produces an error because the negative sign for months must come before the year.
SELECT INTERVAL '23--2 10 0:30' YEAR TO MINUTE

-- 0 years, -2 months, 10 days, 0 hours, 30 minutes, and 0 seconds (-0-2 10 0:30:0)
SELECT INTERVAL '-2 10 0:30' MONTH TO MINUTE

-- 0 years, 0 months, 0 days, 0 hours, -30 minutes, and -10 seconds (0-0 0 -0:30:10)
SELECT INTERVAL '-30:10' MINUTE TO SECOND
```

For more information on how to construct interval with a datetime part range,
see
[Construct an interval with a datetime part range][construct-single-interval].

### Enum literals 
<a id="enum_literals"></a>

There is no syntax for enum literals. Integer or string literals are coerced to
the enum type when necessary, or explicitly cast to a specific enum type name.
 For more information, see [Literal coercion][coercion].

An enum literal represents a constant value of the
[enum data type][enum-data-type].

### JSON literals 
<a id="json_literals"></a>

Syntax:

```googlesql
JSON 'json_formatted_data'
```

A JSON literal represents [JSON][json-wiki]-formatted data.

Example:

```googlesql
JSON '
{
  "id": 10,
  "type": "fruit",
  "name": "apple",
  "on_menu": true,
  "recipes":
    {
      "salads":
      [
        { "id": 2001, "type": "Walnut Apple Salad" },
        { "id": 2002, "type": "Apple Spinach Salad" }
      ],
      "desserts":
      [
        { "id": 3001, "type": "Apple Pie" },
        { "id": 3002, "type": "Apple Scones" },
        { "id": 3003, "type": "Apple Crumble" }
      ]
    }
}
'
```

A JSON literal represents a constant value of the
[JSON data type][json-data-type].

## Case sensitivity 
<a id="case_sensitivity"></a>

GoogleSQL follows these rules for case sensitivity:

<table>
  <thead>
    <tr>
      <th>Category</th>
      <th>Case-sensitive?</th>
      <th>Notes</th>
    </tr>
  </thead>
  <tbody>
    <tr>
      <td>Keywords</td>
      <td>No</td>
      <td>&nbsp;</td>
    </tr>
    
    <tr>
      <td>Function names</td>
      <td>No</td>
      <td>&nbsp;</td>
    </tr>
    
    <tr>
      <td>Table names</td>
      <td>See Notes</td>
      <td>
        
        Table names are usually case-insensitive, but they might be
        case-sensitive when querying a database that uses case-sensitive
        table names.
        
      </td>
    </tr>
    <tr>
      <td>Column names</td>
      <td>No</td>
      <td>&nbsp;</td>
    </tr>
    <tr>
      <td>Field names</td>
      <td>No</td>
      <td>&nbsp;</td>
    </tr>
    
    <tr>
      <td>All type names except for protocol buffer type names</td>
      <td>No</td>
      <td>&nbsp;</td>
    </tr>
    <tr>
      <td>Protocol buffer type names</td>
      <td>Yes</td>
      <td>&nbsp;</td>
    </tr>
    
    <tr>
      <td>Enum type names</td>
      <td>Yes</td>
      <td>&nbsp;</td>
    </tr>
    <tr>
      <td>String values</td>
      <td>Yes</td>
      <td>
        Any value of type <code>STRING</code> preserves its case. For example, the result of an expression that produces a <code>STRING</code> value or a column value that's of type <code>STRING</code>.
      </td>
    </tr>
    <tr>
      <td>String comparisons</td>
      <td>Yes</td>
      <td>
        
        However, string comparisons are case-insensitive in <a href="https://github.com/google/googlesql/blob/master/docs/collation-concepts.md">collations</a>
 that are case-insensitive. This behavior also applies to operations affected by collation, such as <code>GROUP BY</code> and <code>DISTINCT</code> clauses.
        
      </td>
    </tr>
    <tr>
      <td>Aliases within a query</td>
      <td>No</td>
      <td>&nbsp;</td>
    </tr>
    <tr>
      <td>Regular expression matching</td>
      <td>See Notes</td>
      <td>
        Regular expression matching is case-sensitive by default, unless the
        expression itself specifies that it should be case-insensitive.
      </td>
    </tr>
    <tr>
      <td><code>LIKE</code> matching</td>
      <td>Yes</td>
      <td>&nbsp;</td>
    </tr>
    
    
    <tr>
      <td>Property graph names</td>
      <td>No</td>
      <td>&nbsp;</td>
    </tr>
    <tr>
      <td>Property graph label names</td>
      <td>No</td>
      <td>&nbsp;</td>
    </tr>
    <tr>
      <td>Property graph property names</td>
      <td>No</td>
      <td>&nbsp;</td>
    </tr>
    
  </tbody>
</table>

## Reserved keywords 
<a id="reserved_keywords"></a>

Keywords are a group of tokens that have special meaning in the GoogleSQL
language, and  have the following characteristics:

 + Keywords can't be used as identifiers unless enclosed by backtick (`) characters.
 + Keywords are case-insensitive.

GoogleSQL has the following reserved keywords.

<table style="table-layout: fixed; width: 110%">
<tbody>
<tr>
<td>
ALL<br />
AND<br />
ANY<br />
ARRAY<br />
AS<br />
ASC<br />
ASSERT_ROWS_MODIFIED<br />
AT<br />
BETWEEN<br />
BY<br />
CASE<br />
CAST<br />
COLLATE<br />
CONTAINS<br />
CREATE<br />
CROSS<br />
CUBE<br />
CURRENT<br />
DEFAULT<br />
DEFINE<br />
DESC<br />
DISTINCT<br />
ELSE<br />
END<br />
</td>
<td>
ENUM<br />
ESCAPE<br />
EXCEPT<br />
EXCLUDE<br />
EXISTS<br />
EXTRACT<br />
FALSE<br />
FETCH<br />
FOLLOWING<br />
FOR<br />
FROM<br />
FULL<br />
GRAPH_TABLE<br />
GROUP<br />
GROUPING<br />
GROUPS<br />
HASH<br />
HAVING<br />
IF<br />
IGNORE<br />
IN<br />
INNER<br />
INTERSECT<br />
INTERVAL<br />
INTO<br />
</td>
<td>
IS<br />
JOIN<br />
LATERAL<br />
LEFT<br />
LIKE<br />
LIMIT<br />
LOOKUP<br />
MERGE<br />
NATURAL<br />
NEW<br />
NO<br />
NOT<br />
NULL<br />
NULLS<br />
OF<br />
ON<br />
OR<br />
ORDER<br />
OUTER<br />
OVER<br />
PARTITION<br />
PRECEDING<br />
PROTO<br />
QUALIFY<br />
RANGE<br />
</td>
<td>
RECURSIVE<br />
RESPECT<br />
RIGHT<br />
ROLLUP<br />
ROWS<br />
SELECT<br />
SET<br />
SOME<br />
STRUCT<br />
TABLESAMPLE<br />
THEN<br />
TO<br />
TREAT<br />
TRUE<br />
UNBOUNDED<br />
UNION<br />
UNNEST<br />
USING<br />
WHEN<br />
WHERE<br />
WINDOW<br />
WITH<br />
WITHIN<br />
</td>
</tr>
</tbody>
</table>

## Terminating semicolons 
<a id="terminating_semicolons"></a>

You can optionally use a terminating semicolon (`;`) when you submit a query
string statement through an Application Programming Interface (API).

In a request containing multiple statements, you must separate statements with
semicolons, but the semicolon is generally optional after the final statement.
Some interactive tools require statements to have a terminating semicolon.

## Trailing commas 
<a id="trailing_commas"></a>

You can optionally use a trailing comma (`,`) at the end of a column list in a
`SELECT` statement. You might have a trailing comma as the result of
programmatically creating a column list.

**Example**

```
SELECT name, release_date, FROM Books
```

## Query parameters 
<a id="query_parameters"></a>

You can use query parameters to substitute arbitrary expressions.
However, query parameters can't be used to substitute identifiers,
column names, table names, or other parts of the query itself.
Query parameters are defined outside of the query statement.

Client APIs allow the binding of parameter names to values; the query engine
substitutes a bound value for a parameter at execution time.

Query parameters can't be used in the SQL body of these statements:
`CREATE FUNCTION`, `CREATE TABLE FUNCTION`, `CREATE VIEW`, `CREATE MATERIALIZED VIEW`, and `CREATE PROCEDURE`.

### Named query parameters

Syntax:

```googlesql
@parameter_name
```

A named query parameter is denoted using an [identifier][lexical-identifiers]
preceded by the `@` character. Named query
parameters can't be used alongside [positional query
parameters][positional-query-parameters].

A named query parameter can start with an identifier or a reserved keyword.
An identifier can be unquoted or quoted.

**Example:**

This example returns all rows where `LastName` is equal to the value of the
named query parameter `myparam`.

```googlesql
SELECT * FROM Roster WHERE LastName = @myparam
```

### Positional query parameters

Positional query parameters are denoted using the `?` character.
Positional parameters are evaluated by the order in which they are passed in.
Positional query parameters can't be used
alongside [named query parameters][named-query-parameters].

**Example:**

This query returns all rows where `LastName` and `FirstName` are equal to the
values passed into this query. The order in which these values are passed in
matters. If the last name is passed in first, followed by the first name, the
expected results will not be returned.

```googlesql
SELECT * FROM Roster WHERE FirstName = ? and LastName = ?
```

## Hints 
<a id="hints"></a>

```googlesql
@{ hint [, ...] }

hint:
  [engine_name.]hint_name = value
```

The purpose of a hint is to modify the execution strategy for a query
without changing the result of the query. Hints generally don't affect query
semantics, but may have performance implications.

Hint syntax requires the `@` character followed by curly braces.
You can create one hint or a group of hints. The optional `engine_name.`
prefix allows for multiple engines to define hints with the same `hint_name`.
This is important if you need to suggest different engine-specific
execution strategies or different engines support different hints.

You can assign [identifiers][lexical-identifiers] and
[literals][lexical-literals] to hints.

+  Identifiers are useful for hints that are meant to act like enums.
   You can use an identifier to avoid using a quoted string.
   In the resolved AST, identifier hints are represented as string literals,
   so `@{hint="abc"}` is the same as `@{hint=abc}`. Identifier hints can also
   be used for hints that take a table name or column
   name as a single identifier.
+  NULL literals are allowed and are inferred as integers.

Hints are meant to apply only to the node they are attached to,
and not to a larger scope.

**Examples**

In this example, a literal is assigned to a hint. This hint is only used
with two database engines called `database_engine_a` and `database_engine_b`.
The value for the hint is different for each database engine.

```googlesql
@{ database_engine_a.file_count=23, database_engine_b.file_count=10 }
```

## Comments

Comments are sequences of characters that the parser ignores.
GoogleSQL supports the following types of comments.

### Single-line comments 
<a id="single_line_comments"></a>

Use a single-line comment if you want the comment to appear on a line by itself.

**Examples**

```googlesql
# this is a single-line comment
SELECT book FROM library;
```

```googlesql
-- this is a single-line comment
SELECT book FROM library;
```

```googlesql
/* this is a single-line comment */
SELECT book FROM library;
```

```googlesql
SELECT book FROM library
/* this is a single-line comment */
WHERE book = "Ulysses";
```

### Inline comments

Use an inline comment if you want the comment to appear on the same line as
a statement. A comment that's prepended with `#` or `--` must appear to the
right of a statement.

**Examples**

```googlesql
SELECT book FROM library; # this is an inline comment
```

```googlesql
SELECT book FROM library; -- this is an inline comment
```

```googlesql
SELECT book FROM library; /* this is an inline comment */
```

```googlesql
SELECT book FROM library /* this is an inline comment */ WHERE book = "Ulysses";
```

### Multiline comments

Use a multiline comment if you need the comment to span multiple lines.
Nested multiline comments aren't supported.

**Examples**

```googlesql
SELECT book FROM library
/*
  This is a multiline comment
  on multiple lines
*/
WHERE book = "Ulysses";
```

```googlesql
SELECT book FROM library
/* this is a multiline comment
on two lines */
WHERE book = "Ulysses";
```

<!-- mdlint off(WHITESPACE_LINE_LENGTH) -->

[json-wiki]: https://en.wikipedia.org/wiki/JSON

[quoted-identifiers]: #quoted_identifiers

[unquoted-identifiers]: #unquoted_identifiers

[lexical-identifiers]: #identifiers

[lexical-literals]: #literals

[case-sensitivity]: #case_sensitivity

[time-zone]: #timezone

[string-literals]: #string_and_bytes_literals

[path-expressions]: #path_expressions

[field-names]: #field_names

[table-names]: #table_names

[column-names]: #column_names

[named-query-parameters]: #named_query_parameters

[positional-query-parameters]: #positional_query_parameters

[query-reference]: https://github.com/google/googlesql/blob/master/docs/query-syntax.md

[lexical-udfs-reference]: https://github.com/google/googlesql/blob/master/docs/user-defined-functions.md

[constructing-a-struct]: https://github.com/google/googlesql/blob/master/docs/data-types.md#constructing_a_struct

[coercion]: https://github.com/google/googlesql/blob/master/docs/conversion_rules.md#coercion

[string-data-type]: https://github.com/google/googlesql/blob/master/docs/data-types.md#string_type

[bytes-data-type]: https://github.com/google/googlesql/blob/master/docs/data-types.md#bytes_type

[array-data-type]: https://github.com/google/googlesql/blob/master/docs/data-types.md#array_type

[struct-data-type]: https://github.com/google/googlesql/blob/master/docs/data-types.md#struct_type

[integer-data-type]: https://github.com/google/googlesql/blob/master/docs/data-types.md#integer_types

[floating-point-data-type]: https://github.com/google/googlesql/blob/master/docs/data-types.md#floating_point_types

[quoted-literals]: #quoted_literals

[decimal-data-type]: https://github.com/google/googlesql/blob/master/docs/data-types.md#decimal_types

[date-data-type]: https://github.com/google/googlesql/blob/master/docs/data-types.md#date_type

[date-format]: https://github.com/google/googlesql/blob/master/docs/data-types.md#canonical_format_for_date_literals

[time-data-type]: https://github.com/google/googlesql/blob/master/docs/data-types.md#time_type

[time-format]: https://github.com/google/googlesql/blob/master/docs/data-types.md#canonical_format_for_time_literals

[datetime-data-type]: https://github.com/google/googlesql/blob/master/docs/data-types.md#datetime_type

[datetime-format]: https://github.com/google/googlesql/blob/master/docs/data-types.md#canonical_format_for_datetime_literals

[timestamp-data-type]: https://github.com/google/googlesql/blob/master/docs/data-types.md#timestamp_type

[timestamp-format]: https://github.com/google/googlesql/blob/master/docs/data-types.md#canonical_format_for_timestamp_literals

[utc-offset]: https://github.com/google/googlesql/blob/master/docs/data-types.md#utc_offset

[time-zone-name]: https://github.com/google/googlesql/blob/master/docs/data-types.md#time_zone_name

[interval-literal-single]: #interval_literal_single

[interval-literal-range]: #interval_literal_range

[interval-data-type]: https://github.com/google/googlesql/blob/master/docs/data-types.md#interval_type

[interval-datetime-parts]: https://github.com/google/googlesql/blob/master/docs/data-types.md#interval_datetime_parts

[construct-single-interval]: https://github.com/google/googlesql/blob/master/docs/data-types.md#single_datetime_part_interval

[construct-range-interval]: https://github.com/google/googlesql/blob/master/docs/data-types.md#range_datetime_part_interval

[range-data-type]: https://github.com/google/googlesql/blob/master/docs/data-types.md#range_type

[enum-data-type]: https://github.com/google/googlesql/blob/master/docs/data-types.md#enum_type

[json-data-type]: https://github.com/google/googlesql/blob/master/docs/data-types.md#json_type

<!-- mdlint on -->

