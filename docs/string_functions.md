

<!-- mdlint off(WHITESPACE_LINE_LENGTH) -->

# String functions

GoogleSQL supports string functions.
These string functions work on two different values:
`STRING` and `BYTES` data types. `STRING` values must be well-formed UTF-8.

Functions that return position values, such as [STRPOS][string-link-to-strpos],
encode those positions as `INT64`. The value `1`
refers to the first character (or byte), `2` refers to the second, and so on.
The value `0` indicates an invalid position. When working on `STRING` types, the
returned positions refer to character positions.

All string comparisons are done byte-by-byte, without regard to Unicode
canonical equivalence.

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
  <td><a href="https://github.com/google/googlesql/blob/master/docs/array_functions.md#array_to_string"><code>ARRAY_TO_STRING</code></a>
</td>
  <td>
    Produces a concatenation of the elements in an array as a
    <code>STRING</code> value.
    <br>For more information, see <a href="https://github.com/google/googlesql/blob/master/docs/array_functions.md">Array functions</a>.

  </td>
</tr>

<tr>
  <td><a href="https://github.com/google/googlesql/blob/master/docs/string_functions.md#ascii"><code>ASCII</code></a>
</td>
  <td>
    Gets the ASCII code for the first character or byte in a <code>STRING</code>
    or <code>BYTES</code> value.
  </td>
</tr>

<tr>
  <td><a href="https://github.com/google/googlesql/blob/master/docs/string_functions.md#byte_length"><code>BYTE_LENGTH</code></a>
</td>
  <td>
    Gets the number of <code>BYTES</code> in a <code>STRING</code> or
    <code>BYTES</code> value.
  </td>
</tr>

<tr>
  <td><a href="https://github.com/google/googlesql/blob/master/docs/string_functions.md#char_length"><code>CHAR_LENGTH</code></a>
</td>
  <td>
    Gets the number of characters in a <code>STRING</code> value.
  </td>
</tr>

<tr>
  <td><a href="https://github.com/google/googlesql/blob/master/docs/string_functions.md#character_length"><code>CHARACTER_LENGTH</code></a>
</td>
  <td>
    Synonym for <code>CHAR_LENGTH</code>.
  </td>
</tr>

<tr>
  <td><a href="https://github.com/google/googlesql/blob/master/docs/string_functions.md#chr"><code>CHR</code></a>
</td>
  <td>
    Converts a Unicode code point to a character.
    
  </td>
</tr>

<tr>
  <td><a href="https://github.com/google/googlesql/blob/master/docs/string_functions.md#code_points_to_bytes"><code>CODE_POINTS_TO_BYTES</code></a>
</td>
  <td>
    Converts an array of extended ASCII code points to a
    <code>BYTES</code> value.
    
  </td>
</tr>

<tr>
  <td><a href="https://github.com/google/googlesql/blob/master/docs/string_functions.md#code_points_to_string"><code>CODE_POINTS_TO_STRING</code></a>
</td>
  <td>
    Converts an array of extended ASCII code points to a
    <code>STRING</code> value.
    
  </td>
</tr>

<tr>
  <td><a href="https://github.com/google/googlesql/blob/master/docs/string_functions.md#collate"><code>COLLATE</code></a>
</td>
  <td>
    Combines a <code>STRING</code> value and a collation specification into a
    collation specification-supported <code>STRING</code> value.
  </td>
</tr>

<tr>
  <td><a href="https://github.com/google/googlesql/blob/master/docs/string_functions.md#concat"><code>CONCAT</code></a>
</td>
  <td>
    Concatenates one or more <code>STRING</code> or <code>BYTES</code>
    values into a single result.
  </td>
</tr>

<tr>
  <td><a href="https://github.com/google/googlesql/blob/master/docs/string_functions.md#edit_distance"><code>EDIT_DISTANCE</code></a>
</td>
  <td>
    Computes the Levenshtein distance between two <code>STRING</code>
    or <code>BYTES</code> values.
  </td>
</tr>

<tr>
  <td><a href="https://github.com/google/googlesql/blob/master/docs/string_functions.md#ends_with"><code>ENDS_WITH</code></a>
</td>
  <td>
    Checks if a <code>STRING</code> or <code>BYTES</code> value is the suffix
    of another value.
  </td>
</tr>

<tr>
  <td><a href="https://github.com/google/googlesql/blob/master/docs/string_functions.md#format_string"><code>FORMAT</code></a>
</td>
  <td>
    Formats data and produces the results as a <code>STRING</code> value.
  </td>
</tr>

<tr>
  <td><a href="https://github.com/google/googlesql/blob/master/docs/string_functions.md#from_base32"><code>FROM_BASE32</code></a>
</td>
  <td>
    Converts a base32-encoded <code>STRING</code> value into a
    <code>BYTES</code> value.
    
  </td>
</tr>

<tr>
  <td><a href="https://github.com/google/googlesql/blob/master/docs/string_functions.md#from_base64"><code>FROM_BASE64</code></a>
</td>
  <td>
    Converts a base64-encoded <code>STRING</code> value into a
    <code>BYTES</code> value.
    

  </td>
</tr>

<tr>
  <td><a href="https://github.com/google/googlesql/blob/master/docs/string_functions.md#from_hex"><code>FROM_HEX</code></a>
</td>
  <td>
    Converts a hexadecimal-encoded <code>STRING</code> value into a
    <code>BYTES</code> value.
    
  </td>
</tr>

<tr>
  <td><a href="https://github.com/google/googlesql/blob/master/docs/string_functions.md#initcap"><code>INITCAP</code></a>
</td>
  <td>
    Formats a <code>STRING</code> as proper case, which means that the first
    character in each word is uppercase and all other characters are lowercase.
  </td>
</tr>

<tr>
  <td><a href="https://github.com/google/googlesql/blob/master/docs/string_functions.md#instr"><code>INSTR</code></a>
</td>
  <td>
    Finds the position of a subvalue inside another value, optionally starting
    the search at a given offset or occurrence.
  </td>
</tr>

<tr>
  <td><a href="https://github.com/google/googlesql/blob/master/docs/json_functions.md#lax_string"><code>LAX_STRING</code></a>
</td>
  <td>
    Attempts to convert a JSON value to a SQL <code>STRING</code> value.
    <br>For more information, see <a href="https://github.com/google/googlesql/blob/master/docs/json_functions.md">JSON functions</a>.

  </td>
</tr>

<tr>
  <td><a href="https://github.com/google/googlesql/blob/master/docs/json_functions.md#lax_string_array"><code>LAX_STRING_ARRAY</code></a>
</td>
  <td>
    Attempts to convert a JSON value to a
    SQL <code>ARRAY&lt;STRING&gt;</code>value.
    <br>For more information, see <a href="https://github.com/google/googlesql/blob/master/docs/json_functions.md">JSON functions</a>.

  </td>
</tr>

<tr>
  <td><a href="https://github.com/google/googlesql/blob/master/docs/string_functions.md#left"><code>LEFT</code></a>
</td>
  <td>
    Gets the specified leftmost portion from a <code>STRING</code> or
    <code>BYTES</code> value.
  </td>
</tr>

<tr>
  <td><a href="https://github.com/google/googlesql/blob/master/docs/string_functions.md#length"><code>LENGTH</code></a>
</td>
  <td>
    Gets the length of a <code>STRING</code> or <code>BYTES</code> value.
  </td>
</tr>

<tr>
  <td><a href="https://github.com/google/googlesql/blob/master/docs/string_functions.md#lower"><code>LOWER</code></a>
</td>
  <td>
    Formats alphabetic characters in a <code>STRING</code> value as
    lowercase.
    <br /><br />
    Formats ASCII characters in a <code>BYTES</code> value as
    lowercase.
  </td>
</tr>

<tr>
  <td><a href="https://github.com/google/googlesql/blob/master/docs/string_functions.md#lpad"><code>LPAD</code></a>
</td>
  <td>
    Prepends a <code>STRING</code> or <code>BYTES</code> value with a pattern.
  </td>
</tr>

<tr>
  <td><a href="https://github.com/google/googlesql/blob/master/docs/string_functions.md#ltrim"><code>LTRIM</code></a>
</td>
  <td>
    Identical to the <code>TRIM</code> function, but only removes leading
    characters.
  </td>
</tr>

<tr>
  <td><a href="https://github.com/google/googlesql/blob/master/docs/string_functions.md#normalize"><code>NORMALIZE</code></a>
</td>
  <td>
    Case-sensitively normalizes the characters in a <code>STRING</code> value.
  </td>
</tr>

<tr>
  <td><a href="https://github.com/google/googlesql/blob/master/docs/string_functions.md#normalize_and_casefold"><code>NORMALIZE_AND_CASEFOLD</code></a>
</td>
  <td>
    Case-insensitively normalizes the characters in a <code>STRING</code> value.
  </td>
</tr>

<tr>
  <td><a href="https://github.com/google/googlesql/blob/master/docs/string_functions.md#octet_length"><code>OCTET_LENGTH</code></a>
</td>
  <td>
    Alias for <code>BYTE_LENGTH</code>.
  </td>
</tr>

<tr>
  <td><a href="https://github.com/google/googlesql/blob/master/docs/string_functions.md#regexp_contains"><code>REGEXP_CONTAINS</code></a>
</td>
  <td>
    Checks if a value is a partial match for a regular expression.
  </td>
</tr>

<tr>
  <td><a href="https://github.com/google/googlesql/blob/master/docs/string_functions.md#regexp_extract"><code>REGEXP_EXTRACT</code></a>
</td>
  <td>
    Produces a substring that matches a regular expression.
  </td>
</tr>

<tr>
  <td><a href="https://github.com/google/googlesql/blob/master/docs/string_functions.md#regexp_extract_groups"><code>REGEXP_EXTRACT_GROUPS</code></a>
</td>
  <td>
    Produces substrings that match multiple capturing groups in a regular expression.
  </td>
</tr>

<tr>
  <td><a href="https://github.com/google/googlesql/blob/master/docs/string_functions.md#regexp_extract_all"><code>REGEXP_EXTRACT_ALL</code></a>
</td>
  <td>
    Produces an array of all substrings that match a
    regular expression.
  </td>
</tr>

<tr>
  <td><a href="https://github.com/google/googlesql/blob/master/docs/string_functions.md#regexp_instr"><code>REGEXP_INSTR</code></a>
</td>
  <td>
    Finds the position of a regular expression match in a value, optionally
    starting the search at a given offset or occurrence.
  </td>
</tr>

<tr>
  <td><a href="https://github.com/google/googlesql/blob/master/docs/string_functions.md#regexp_match"><code>REGEXP_MATCH</code></a>
</td>
  <td>
    (Deprecated) Checks if a value is a full match for a regular expression.
  </td>
</tr>

<tr>
  <td><a href="https://github.com/google/googlesql/blob/master/docs/string_functions.md#regexp_replace"><code>REGEXP_REPLACE</code></a>
</td>
  <td>
    Produces a <code>STRING</code> value where all substrings that match a
    regular expression are replaced with a specified value.
  </td>
</tr>

<tr>
  <td><a href="https://github.com/google/googlesql/blob/master/docs/string_functions.md#regexp_substr"><code>REGEXP_SUBSTR</code></a>
</td>
  <td>
    Synonym for <code>REGEXP_EXTRACT</code>.
  </td>
</tr>

<tr>
  <td><a href="https://github.com/google/googlesql/blob/master/docs/string_functions.md#repeat"><code>REPEAT</code></a>
</td>
  <td>
    Produces a <code>STRING</code> or <code>BYTES</code> value that consists of
    an original value, repeated.
  </td>
</tr>

<tr>
  <td><a href="https://github.com/google/googlesql/blob/master/docs/string_functions.md#replace"><code>REPLACE</code></a>
</td>
  <td>
    Replaces all occurrences of a pattern with another pattern in a
    <code>STRING</code> or <code>BYTES</code> value.
  </td>
</tr>

<tr>
  <td><a href="https://github.com/google/googlesql/blob/master/docs/string_functions.md#reverse"><code>REVERSE</code></a>
</td>
  <td>
    Reverses a <code>STRING</code> or <code>BYTES</code> value.
  </td>
</tr>

<tr>
  <td><a href="https://github.com/google/googlesql/blob/master/docs/string_functions.md#right"><code>RIGHT</code></a>
</td>
  <td>
    Gets the specified rightmost portion from a <code>STRING</code> or
    <code>BYTES</code> value.
  </td>
</tr>

<tr>
  <td><a href="https://github.com/google/googlesql/blob/master/docs/string_functions.md#rpad"><code>RPAD</code></a>
</td>
  <td>
    Appends a <code>STRING</code> or <code>BYTES</code> value with a pattern.
  </td>
</tr>

<tr>
  <td><a href="https://github.com/google/googlesql/blob/master/docs/string_functions.md#rtrim"><code>RTRIM</code></a>
</td>
  <td>
    Identical to the <code>TRIM</code> function, but only removes trailing
    characters.
  </td>
</tr>

<tr>
  <td><a href="https://github.com/google/googlesql/blob/master/docs/string_functions.md#safe_convert_bytes_to_string"><code>SAFE_CONVERT_BYTES_TO_STRING</code></a>
</td>
  <td>
    Converts a <code>BYTES</code> value to a <code>STRING</code> value and
    replace any invalid UTF-8 characters with the Unicode replacement character,
    <code>U+FFFD</code>.
    
  </td>
</tr>

<tr>
  <td><a href="https://github.com/google/googlesql/blob/master/docs/string_functions.md#soundex"><code>SOUNDEX</code></a>
</td>
  <td>
    Gets the Soundex codes for words in a <code>STRING</code> value.
  </td>
</tr>

<tr>
  <td><a href="https://github.com/google/googlesql/blob/master/docs/string_functions.md#split"><code>SPLIT</code></a>
</td>
  <td>
    Splits a <code>STRING</code> or <code>BYTES</code> value, using a delimiter.
  </td>
</tr>

<tr>
  <td><a href="https://github.com/google/googlesql/blob/master/docs/string_functions.md#split_substr"><code>SPLIT_SUBSTR</code></a>
</td>
  <td>
    Returns the substring from an input string that's determined by a delimiter, a
    location that indicates the first split of the substring to return, and the
    number of splits to include.
  </td>
</tr>

<tr>
  <td><a href="https://github.com/google/googlesql/blob/master/docs/string_functions.md#starts_with"><code>STARTS_WITH</code></a>
</td>
  <td>
    Checks if a <code>STRING</code> or <code>BYTES</code> value is a
    prefix of another value.
  </td>
</tr>

<tr>
  <td><a href="https://github.com/google/googlesql/blob/master/docs/json_functions.md#string_for_json"><code>STRING</code> (JSON)</a>
</td>
  <td>
    Converts a JSON string to a SQL <code>STRING</code> value.
    <br>For more information, see <a href="https://github.com/google/googlesql/blob/master/docs/json_functions.md">JSON functions</a>.

  </td>
</tr>

<tr>
  <td><a href="https://github.com/google/googlesql/blob/master/docs/json_functions.md#string_array_for_json"><code>STRING_ARRAY</code></a>
</td>
  <td>
    Converts a JSON array of strings to a SQL <code>ARRAY&lt;STRING&gt;</code>
    value.
    <br>For more information, see <a href="https://github.com/google/googlesql/blob/master/docs/json_functions.md">JSON functions</a>.

  </td>
</tr>

<tr>
  <td><a href="https://github.com/google/googlesql/blob/master/docs/timestamp_functions.md#string"><code>STRING</code> (Timestamp)</a>
</td>
  <td>
    Converts a <code>TIMESTAMP</code> value to a <code>STRING</code> value.
    <br>For more information, see <a href="https://github.com/google/googlesql/blob/master/docs/timestamp_functions.md">Timestamp functions</a>.

  </td>
</tr>

<tr>
  <td><a href="https://github.com/google/googlesql/blob/master/docs/aggregate_functions.md#string_agg"><code>STRING_AGG</code></a>
</td>
  <td>
    Concatenates non-<code>NULL</code> <code>STRING</code> or
    <code>BYTES</code> values.
    <br>For more information, see <a href="https://github.com/google/googlesql/blob/master/docs/aggregate_functions.md">Aggregate functions</a>.

  </td>
</tr>

<tr>
  <td><a href="https://github.com/google/googlesql/blob/master/docs/string_functions.md#strpos"><code>STRPOS</code></a>
</td>
  <td>
    Finds the position of the first occurrence of a subvalue inside another
    value.
  </td>
</tr>

<tr>
  <td><a href="https://github.com/google/googlesql/blob/master/docs/string_functions.md#substr"><code>SUBSTR</code></a>
</td>
  <td>
    Gets a portion of a <code>STRING</code> or <code>BYTES</code> value.
  </td>
</tr>

<tr>
  <td><a href="https://github.com/google/googlesql/blob/master/docs/string_functions.md#substring"><code>SUBSTRING</code></a>
</td>
  <td>Alias for <code>SUBSTR</code></td>
</tr>

<tr>
  <td><a href="https://github.com/google/googlesql/blob/master/docs/string_functions.md#to_base32"><code>TO_BASE32</code></a>
</td>
  <td>
    Converts a <code>BYTES</code> value to a
    base32-encoded <code>STRING</code> value.
    
  </td>
</tr>

<tr>
  <td><a href="https://github.com/google/googlesql/blob/master/docs/string_functions.md#to_base64"><code>TO_BASE64</code></a>
</td>
  <td>
    Converts a <code>BYTES</code> value to a
    base64-encoded <code>STRING</code> value.
    
  </td>
</tr>

<tr>
  <td><a href="https://github.com/google/googlesql/blob/master/docs/string_functions.md#to_code_points"><code>TO_CODE_POINTS</code></a>
</td>
  <td>
    Converts a <code>STRING</code> or <code>BYTES</code> value into an array of
    extended ASCII code points.
    
  </td>
</tr>

<tr>
  <td><a href="https://github.com/google/googlesql/blob/master/docs/string_functions.md#to_hex"><code>TO_HEX</code></a>
</td>
  <td>
    Converts a <code>BYTES</code> value to a
    hexadecimal <code>STRING</code> value.
    
  </td>
</tr>

<tr>
  <td><a href="https://github.com/google/googlesql/blob/master/docs/string_functions.md#translate"><code>TRANSLATE</code></a>
</td>
  <td>
    Within a value, replaces each source character with the corresponding
    target character.
  </td>
</tr>

<tr>
  <td><a href="https://github.com/google/googlesql/blob/master/docs/string_functions.md#trim"><code>TRIM</code></a>
</td>
  <td>
    Removes the specified leading and trailing Unicode code points or bytes
    from a <code>STRING</code> or <code>BYTES</code> value.
  </td>
</tr>

<tr>
  <td><a href="https://github.com/google/googlesql/blob/master/docs/string_functions.md#unicode"><code>UNICODE</code></a>
</td>
  <td>
    Gets the Unicode code point for the first character in a value.
  </td>
</tr>

<tr>
  <td><a href="https://github.com/google/googlesql/blob/master/docs/string_functions.md#upper"><code>UPPER</code></a>
</td>
  <td>
    Formats alphabetic characters in a <code>STRING</code> value as
    uppercase.
    <br /><br />
    Formats ASCII characters in a <code>BYTES</code> value as
    uppercase.
  </td>
</tr>

  </tbody>
</table>

## `ASCII`

```googlesql
ASCII(value)
```

**Description**

Returns the ASCII code for the first character or byte in `value`. Returns
`0` if `value` is empty or the ASCII code is `0` for the first character
or byte.

**Return type**

`INT64`

**Examples**

```googlesql
SELECT ASCII('abcd') as A, ASCII('a') as B, ASCII('') as C, ASCII(NULL) as D;

/*-------+-------+-------+-------+
 | A     | B     | C     | D     |
 +-------+-------+-------+-------+
 | 97    | 97    | 0     | NULL  |
 +-------+-------+-------+-------*/
```

## `BYTE_LENGTH`

```googlesql
BYTE_LENGTH(value)
```

**Description**

Gets the number of `BYTES` in a `STRING` or `BYTES` value,
regardless of whether the value is a `STRING` or `BYTES` type.

**Return type**

`INT64`

**Examples**

```googlesql
SELECT BYTE_LENGTH('абвгд') AS string_example;

/*----------------+
 | string_example |
 +----------------+
 | 10             |
 +----------------*/
```

```googlesql
SELECT BYTE_LENGTH(b'абвгд') AS bytes_example;

/*----------------+
 | bytes_example  |
 +----------------+
 | 10             |
 +----------------*/
```

## `CHAR_LENGTH`

```googlesql
CHAR_LENGTH(value)
```

**Description**

Gets the number of characters in a `STRING` value.

**Return type**

`INT64`

**Examples**

```googlesql
SELECT CHAR_LENGTH('абвгд') AS char_length;

/*-------------+
 | char_length |
 +-------------+
 | 5           |
 +------------ */
```

## `CHARACTER_LENGTH`

```googlesql
CHARACTER_LENGTH(value)
```

**Description**

Synonym for [CHAR_LENGTH][string-link-to-char-length].

**Return type**

`INT64`

**Examples**

```googlesql
SELECT
  'абвгд' AS characters,
  CHARACTER_LENGTH('абвгд') AS char_length_example

/*------------+---------------------+
 | characters | char_length_example |
 +------------+---------------------+
 | абвгд      |                   5 |
 +------------+---------------------*/
```

[string-link-to-char-length]: #char_length

## `CHR`

```googlesql
CHR(value)
```

**Description**

Takes a Unicode [code point][string-link-to-code-points-wikipedia] and returns
the character that matches the code point. Each valid code point should fall
within the range of [0, 0xD7FF] and [0xE000, 0x10FFFF]. Returns an empty string
if the code point is `0`. If an invalid Unicode code point is specified, an
error is returned.

To work with an array of Unicode code points, see
[`CODE_POINTS_TO_STRING`][string-link-to-codepoints-to-string]

**Return type**

`STRING`

**Examples**

```googlesql
SELECT CHR(65) AS A, CHR(255) AS B, CHR(513) AS C, CHR(1024)  AS D;

/*-------+-------+-------+-------+
 | A     | B     | C     | D     |
 +-------+-------+-------+-------+
 | A     | ÿ     | ȁ     | Ѐ     |
 +-------+-------+-------+-------*/
```

```googlesql
SELECT CHR(97) AS A, CHR(0xF9B5) AS B, CHR(0) AS C, CHR(NULL) AS D;

/*-------+-------+-------+-------+
 | A     | B     | C     | D     |
 +-------+-------+-------+-------+
 | a     | 例    |       | NULL  |
 +-------+-------+-------+-------*/
```

[string-link-to-code-points-wikipedia]: https://en.wikipedia.org/wiki/Code_point

[string-link-to-codepoints-to-string]: #code_points_to_string

## `CODE_POINTS_TO_BYTES`

```googlesql
CODE_POINTS_TO_BYTES(ascii_code_points)
```

**Description**

Takes an array of extended ASCII
[code points][string-link-to-code-points-wikipedia]
as `ARRAY<INT64>` and returns `BYTES`.

To convert from `BYTES` to an array of code points, see
[TO_CODE_POINTS][string-link-to-code-points].

**Return type**

`BYTES`

**Examples**

The following is a basic example using `CODE_POINTS_TO_BYTES`.

```googlesql
SELECT CODE_POINTS_TO_BYTES([65, 98, 67, 100]) AS bytes;

/*----------+
 | bytes    |
 +----------+
 | AbCd     |
 +----------*/
```

The following example uses a rotate-by-13 places (ROT13) algorithm to encode a
string.

```googlesql
SELECT CODE_POINTS_TO_BYTES(ARRAY_AGG(
  (SELECT
      CASE
        WHEN chr BETWEEN b'a' and b'z'
          THEN TO_CODE_POINTS(b'a')[offset(0)] +
            MOD(code+13-TO_CODE_POINTS(b'a')[offset(0)],26)
        WHEN chr BETWEEN b'A' and b'Z'
          THEN TO_CODE_POINTS(b'A')[offset(0)] +
            MOD(code+13-TO_CODE_POINTS(b'A')[offset(0)],26)
        ELSE code
      END
   FROM
     (SELECT code, CODE_POINTS_TO_BYTES([code]) chr)
  ) ORDER BY OFFSET)) AS encoded_string
FROM UNNEST(TO_CODE_POINTS(b'Test String!')) code WITH OFFSET;

/*------------------+
 | encoded_string   |
 +------------------+
 | Grfg Fgevat!     |
 +------------------*/
```

[string-link-to-code-points-wikipedia]: https://en.wikipedia.org/wiki/Code_point

[string-link-to-code-points]: #to_code_points

## `CODE_POINTS_TO_STRING`

```googlesql
CODE_POINTS_TO_STRING(unicode_code_points)
```

**Description**

Takes an array of Unicode [code points][string-link-to-code-points-wikipedia]
as `ARRAY<INT64>` and returns a `STRING`.

To convert from a string to an array of code points, see
[TO_CODE_POINTS][string-link-to-code-points].

**Return type**

`STRING`

**Examples**

The following are basic examples using `CODE_POINTS_TO_STRING`.

```googlesql
SELECT CODE_POINTS_TO_STRING([65, 255, 513, 1024]) AS string;

/*--------+
 | string |
 +--------+
 | AÿȁЀ   |
 +--------*/
```

```googlesql
SELECT CODE_POINTS_TO_STRING([97, 0, 0xF9B5]) AS string;

/*--------+
 | string |
 +--------+
 | a例    |
 +--------*/
```

```googlesql
SELECT CODE_POINTS_TO_STRING([65, 255, NULL, 1024]) AS string;

/*--------+
 | string |
 +--------+
 | NULL   |
 +--------*/
```

The following example computes the frequency of letters in a set of words.

```googlesql
WITH Words AS (
  SELECT word
  FROM UNNEST(['foo', 'bar', 'baz', 'giraffe', 'llama']) AS word
)
SELECT
  CODE_POINTS_TO_STRING([code_point]) AS letter,
  COUNT(*) AS letter_count
FROM Words,
  UNNEST(TO_CODE_POINTS(word)) AS code_point
GROUP BY 1
ORDER BY 2 DESC;

/*--------+--------------+
 | letter | letter_count |
 +--------+--------------+
 | a      | 5            |
 | f      | 3            |
 | r      | 2            |
 | b      | 2            |
 | l      | 2            |
 | o      | 2            |
 | g      | 1            |
 | z      | 1            |
 | e      | 1            |
 | m      | 1            |
 | i      | 1            |
 +--------+--------------*/
```

[string-link-to-code-points-wikipedia]: https://en.wikipedia.org/wiki/Code_point

[string-link-to-code-points]: #to_code_points

## `COLLATE`

```googlesql
COLLATE(value, collate_specification)
```

Takes a `STRING` and a [collation specification][link-collation-spec]. Returns
a `STRING` with a collation specification. If `collate_specification` is empty,
returns a value with collation removed from the `STRING`.

The collation specification defines how the resulting `STRING` can be compared
and sorted. To learn more, see
[Collation][link-collation-concepts].

+ `collation_specification` must be a string literal, otherwise an error is
  thrown.
+ Returns `NULL` if `value` is `NULL`.

**Return type**

`STRING`

**Examples**

In this example, the weight of `a` is less than the weight of `Z`. This
is because the collate specification, `und:ci` assigns more weight to `Z`.

```googlesql
WITH Words AS (
  SELECT
    COLLATE('a', 'und:ci') AS char1,
    COLLATE('Z', 'und:ci') AS char2
)
SELECT ( Words.char1 < Words.char2 ) AS a_less_than_Z
FROM Words;

/*----------------+
 | a_less_than_Z  |
 +----------------+
 | TRUE           |
 +----------------*/
```

In this example, the weight of `a` is greater than the weight of `Z`. This
is because the default collate specification assigns more weight to `a`.

```googlesql
WITH Words AS (
  SELECT
    'a' AS char1,
    'Z' AS char2
)
SELECT ( Words.char1 < Words.char2 ) AS a_less_than_Z
FROM Words;

/*----------------+
 | a_less_than_Z  |
 +----------------+
 | FALSE          |
 +----------------*/
```

[link-collation-spec]: https://github.com/google/googlesql/blob/master/docs/collation-concepts.md#collate_spec_details

[link-collation-concepts]: https://github.com/google/googlesql/blob/master/docs/collation-concepts.md

## `CONCAT`

```googlesql
CONCAT(value1[, ...])
```

**Description**

Concatenates one or more values into a single result. All values must be
`BYTES` or data types that can be cast to `STRING`.

The function returns `NULL` if any input argument is `NULL`.

Note: You can also use the
[|| concatenation operator][string-link-to-operators] to concatenate
values into a string.

**Return type**

`STRING` or `BYTES`

**Examples**

```googlesql
SELECT CONCAT('T.P.', ' ', 'Bar') as author;

/*---------------------+
 | author              |
 +---------------------+
 | T.P. Bar            |
 +---------------------*/
```

```googlesql
SELECT CONCAT('Summer', ' ', 1923) as release_date;

/*---------------------+
 | release_date        |
 +---------------------+
 | Summer 1923         |
 +---------------------*/
```

```googlesql

With Employees AS
  (SELECT
    'John' AS first_name,
    'Doe' AS last_name
  UNION ALL
  SELECT
    'Jane' AS first_name,
    'Smith' AS last_name
  UNION ALL
  SELECT
    'Joe' AS first_name,
    'Jackson' AS last_name)

SELECT
  CONCAT(first_name, ' ', last_name)
  AS full_name
FROM Employees;

/*---------------------+
 | full_name           |
 +---------------------+
 | John Doe            |
 | Jane Smith          |
 | Joe Jackson         |
 +---------------------*/
```

[string-link-to-operators]: https://github.com/google/googlesql/blob/master/docs/operators.md

## `EDIT_DISTANCE`

```googlesql
EDIT_DISTANCE(
  value1,
  value2,
  [ max_distance => max_distance_value ]
)
```

**Description**

Computes the [Levenshtein distance][l-distance] between two `STRING` or
`BYTES` values.

**Definitions**

+   `value1`: The first `STRING` or `BYTES` value to compare.
+   `value2`: The second `STRING` or `BYTES` value to compare.
+   `max_distance`: A named argument with a `INT64` value that's greater than
    or equal to zero. Represents the maximum distance between the two values
    to compute.

    If this distance is exceeded, the function returns this value.
    The default value for this argument is the maximum size of
    `value1` and `value2`.

**Details**

If `value1` or `value2` is `NULL`, `NULL` is returned.

You can only compare values of the same type. Otherwise, an error is produced.

**Return type**

`INT64`

**Examples**

In the following example, the first character in both strings is different:

```googlesql
SELECT EDIT_DISTANCE('a', 'b') AS results;

/*---------+
 | results |
 +---------+
 | 1       |
 +---------*/
```

In the following example, the first and second characters in both strings are
different:

```googlesql
SELECT EDIT_DISTANCE('aa', 'b') AS results;

/*---------+
 | results |
 +---------+
 | 2       |
 +---------*/
```

In the following example, only the first character in both strings is
different:

```googlesql
SELECT EDIT_DISTANCE('aa', 'ba') AS results;

/*---------+
 | results |
 +---------+
 | 1       |
 +---------*/
```

In the following example, the last six characters are different, but because
the maximum distance is `2`, this function exits early and returns `2`, the
maximum distance:

```googlesql
SELECT EDIT_DISTANCE('abcdefg', 'a', max_distance => 2) AS results;

/*---------+
 | results |
 +---------+
 | 2       |
 +---------*/
```

[l-distance]: https://en.wikipedia.org/wiki/Levenshtein_distance

## `ENDS_WITH`

```googlesql
ENDS_WITH(value, suffix)
```

**Description**

Takes two `STRING` or `BYTES` values. Returns `TRUE` if `suffix`
is a suffix of `value`.

This function supports specifying [collation][collation].

[collation]: https://github.com/google/googlesql/blob/master/docs/collation-concepts.md

**Return type**

`BOOL`

**Examples**

```googlesql
SELECT ENDS_WITH('apple', 'e') as example

/*---------+
 | example |
 +---------+
 |    True |
 +---------*/
```

## `FORMAT` 
<a id="format_string"></a>

```googlesql
FORMAT(format_string_expression, data_type_expression[, ...])
```

**Description**

`FORMAT` formats a data type expression as a string.

+ `format_string_expression`: Can contain zero or more
  [format specifiers][format-specifiers]. Each format specifier is introduced
  by the `%` symbol, and must map to one or more of the remaining arguments.
  In general, this is a one-to-one mapping, except when the `*` specifier is
  present. For example, `%.*i` maps to two arguments&mdash;a length argument
  and a signed integer argument. If the number of arguments related to the
  format specifiers isn't the same as the number of arguments, an error occurs.
+ `data_type_expression`: The value to format as a string. This can be any
  GoogleSQL data type.

**Return type**

`STRING`

**Examples**

<table>
<tr>
<th>Description</th>
<th>Statement</th>
<th>Result</th>
</tr>
<tr>
<td>Simple integer</td>
<td>FORMAT('%d', 10)</td>
<td>10</td>
</tr>
<tr>
<td>Integer with left blank padding</td>
<td>FORMAT('|%10d|', 11)</td>
<td>|&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;11|</td>
</tr>
<tr>
<td>Integer with left zero padding</td>
<td>FORMAT('+%010d+', 12)</td>
<td>+0000000012+</td>
</tr>
<tr>
<td>Integer with commas</td>
<td>FORMAT("%'d", 123456789)</td>
<td>123,456,789</td>
</tr>
<tr>
<td>STRING</td>
<td>FORMAT('-%s-', 'abcd efg')</td>
<td>-abcd efg-</td>
</tr>
<tr>
<td>DOUBLE</td>
<td>FORMAT('%f %E', 1.1, 2.2)</td>
<td>1.100000&nbsp;2.200000E+00</td>
</tr>

<tr>
<td>DATE</td>
<td>FORMAT('%t', date '2015-09-01')</td>
<td>2015-09-01</td>
</tr>

<tr>
<td>TIMESTAMP</td>
<td>FORMAT('%t', timestamp '2015-09-01 12:34:56
America/Los_Angeles')</td>
<td>2015&#8209;09&#8209;01&nbsp;19:34:56+00</td>
</tr>
</table>

The `FORMAT()` function doesn't provide fully customizable formatting for all
types and values, nor formatting that's sensitive to locale.

If custom formatting is necessary for a type, you must first format it using
type-specific format functions, such as `FORMAT_DATE()` or `FORMAT_TIMESTAMP()`.
For example:

```googlesql
SELECT FORMAT('date: %s!', FORMAT_DATE('%B %d, %Y', date '2015-01-02'));
```

Returns

```
date: January 02, 2015!
```

#### Supported format specifiers 
<a id="format_specifiers"></a>

```
%[flags][width][.precision]specifier
```

A [format specifier][format-specifier-list] adds formatting when casting a
value to a string. It can optionally contain these sub-specifiers:

+ [Flags][flags]
+ [Width][width]
+ [Precision][precision]

Additional information about format specifiers:

+ [%g and %G behavior][g-and-g-behavior]
+ [%p and %P behavior][p-and-p-behavior]
+ [%t and %T behavior][t-and-t-behavior]
+ [Error conditions][error-format-specifiers]
+ [NULL argument handling][null-format-specifiers]
+ [Additional semantic rules][rules-format-specifiers]

##### Format specifiers 
<a id="format_specifier_list"></a>

<table>
 <tr>
    <td>Specifier</td>
    <td>Description</td>
    <td width="200px">Examples</td>
    <td>Types</td>
 </tr>
 <tr>
    <td><code>d</code> or <code>i</code></td>
    <td>Decimal integer</td>
    <td>392</td>
    <td>

<span><code>INT32</code></span><br /><span><code>INT64</code></span><br /><span><code>UINT32</code></span><br /><span><code>UINT64</code></span><br />
</td>
 </tr>

 <tr>
    <td><code>u</code></td>
    <td>Unsigned integer</td>
    <td>7235</td>
    <td>

<span><code>UINT32</code></span><br /><span><code>UINT64</code></span><br />
</td>
 </tr>

 <tr>
    <td><code>o</code></td>
    <td>
    Octal
    <br /><br />
    Note: If an <code>INT64</code> value is negative, an error is produced.
    </td>
    <td>610</td>
    <td>

<span><code>INT32</code></span><br /><span><code>INT64</code></span><br /><span><code>UINT32</code></span><br /><span><code>UINT64</code></span><br />
</td>
 </tr>
 <tr>
    <td><code>x</code></td>
    <td>
      Hexadecimal integer
      <br /><br />
      Note: If an <code>INT64</code> value is negative, an error is produced.
    </td>
    <td>7fa</td>
    <td>

<span><code>INT32</code></span><br /><span><code>INT64</code></span><br /><span><code>UINT32</code></span><br /><span><code>UINT64</code></span><br />
</td>
 </tr>
 <tr>
    <td><code>X</code></td>
    <td>
      Hexadecimal integer (uppercase)
      <br /><br />
      Note: If an <code>INT64</code> value is negative, an error is produced.
    </td>
    <td>7FA</td>
    <td>

<span><code>INT32</code></span><br /><span><code>INT64</code></span><br /><span><code>UINT32</code></span><br /><span><code>UINT64</code></span><br />

    </td>
 </tr>
 <tr>
    <td><code>f</code></td>
    <td>Decimal notation, in [-](integer part).(fractional part) for finite
        values, and in lowercase for non-finite values</td>
    <td>392.650000<br/>
    inf<br/>
    nan</td>
    <td>

<span><code>NUMERIC</code></span><br /><span><code>BIGNUMERIC</code></span><br /><span><code>FLOAT</code></span><br /><span><code>DOUBLE</code></span><br />
</td>
 </tr>
 <tr>
    <td><code>F</code></td>
    <td>Decimal notation, in [-](integer part).(fractional part) for finite
        values, and in uppercase for non-finite values</td>
    <td>392.650000<br/>
    INF<br/>
    NAN</td>
    <td>

<span><code>NUMERIC</code></span><br /><span><code>BIGNUMERIC</code></span><br /><span><code>FLOAT</code></span><br /><span><code>DOUBLE</code></span><br />
</td>
 </tr>
 <tr>
    <td><code>e</code></td>
    <td>Scientific notation (mantissa/exponent), lowercase</td>
    <td>3.926500e+02<br/>
    inf<br/>
    nan</td>
    <td>

<span><code>NUMERIC</code></span><br /><span><code>BIGNUMERIC</code></span><br /><span><code>FLOAT</code></span><br /><span><code>DOUBLE</code></span><br />
</td>
 </tr>
 <tr>
    <td><code>E</code></td>
    <td>Scientific notation (mantissa/exponent), uppercase</td>
    <td>3.926500E+02<br/>
    INF<br/>
    NAN</td>
    <td>

<span><code>NUMERIC</code></span><br /><span><code>BIGNUMERIC</code></span><br /><span><code>FLOAT</code></span><br /><span><code>DOUBLE</code></span><br />
</td>
 </tr>
 <tr>
    <td><code>g</code></td>
    <td>Either decimal notation or scientific notation, depending on the input
        value's exponent and the specified precision. Lowercase.
        See <a href="#g_and_g_behavior">%g and %G behavior</a> for details.</td>
    <td>392.65<br/>
      3.9265e+07<br/>
    inf<br/>
    nan</td>
    <td>

<span><code>NUMERIC</code></span><br /><span><code>BIGNUMERIC</code></span><br /><span><code>FLOAT</code></span><br /><span><code>DOUBLE</code></span><br />
</td>
 </tr>
 <tr>
    <td><code>G</code></td>
    <td>
      Either decimal notation or scientific notation, depending on the input
      value's exponent and the specified precision. Uppercase.
      See <a href="#g_and_g_behavior">%g and %G behavior</a> for details.
    </td>
    <td>
      392.65<br/>
      3.9265E+07<br/>
      INF<br/>
      NAN
    </td>
    <td>

<span><code>NUMERIC</code></span><br /><span><code>BIGNUMERIC</code></span><br /><span><code>FLOAT</code></span><br /><span><code>DOUBLE</code></span><br />
</td>
 </tr>

 <tr>
    <td><code>p</code></td>
    <td>
      
      Produces a one-line printable string representing a protocol buffer
      or JSON.
      
      See <a href="#p_and_p_behavior">%p and %P behavior</a>.
    </td>
    <td>
      
<pre>year: 2019 month: 10</pre>
      
      
<pre>{"month":10,"year":2019}</pre>
      
    </td>
    <td>

<span><code>JSON</code></span><br /><span><code>PROTO</code></span><br />
</td>
 </tr>

  <tr>
    <td><code>P</code></td>
    <td>
      
      Produces a multi-line printable string representing a protocol buffer
      or JSON.
      
      See <a href="#p_and_p_behavior">%p and %P behavior</a>.
    </td>
    <td>
      
<pre>
year: 2019
month: 10
</pre>
      
      
<pre>
{
  "month": 10,
  "year": 2019
}
</pre>
      
    </td>
    <td>

<span><code>JSON</code></span><br /><span><code>PROTO</code></span><br />
</td>
  </tr>

 <tr>
    <td><code>s</code></td>
    <td>String of characters</td>
    <td>sample</td>
    <td>

<span><code>STRING</code></span><br />
</td>
 </tr>
 <tr>
    <td><code>t</code></td>
    <td>
      Returns a printable string representing the value. Often looks
      similar to casting the argument to <code>STRING</code>.
      See <a href="#t_and_t_behavior">%t and %T behavior</a>.
    </td>
    <td>
      sample<br/>
      2014&#8209;01&#8209;01
    </td>
    <td>Any type</td>
 </tr>
 <tr>
    <td><code>T</code></td>
    <td>
      Produces a string that's a valid GoogleSQL constant with a
      similar type to the value's type (maybe wider, or maybe string).
      See <a href="#t_and_t_behavior">%t and %T behavior</a>.
    </td>
    <td>
      'sample'<br/>
      b'bytes&nbsp;sample'<br/>
      1234<br/>
      2.3<br/>
      date&nbsp;'2014&#8209;01&#8209;01'
    </td>
    <td>Any type</td>
 </tr>
 <tr>
    <td><code>%</code></td>
    <td>'%%' produces a single '%'</td>
    <td>%</td>
    <td>n/a</td>
 </tr>
</table>

The format specifier can optionally contain the sub-specifiers identified above
in the specifier prototype.

These sub-specifiers must comply with the following specifications.

##### Flags 
<a id="flags"></a>

<table>
 <tr>
    <td>Flags</td>
    <td>Description</td>
 </tr>
 <tr>
    <td><code>-</code></td>
    <td>Left-justify within the given field width; Right justification is the
default (see width sub-specifier)</td>
</tr>
 <tr>
    <td><code>+</code></td>
    <td>Forces to precede the result with a plus or minus sign (<code>+</code>
or <code>-</code>) even for positive numbers. By default, only negative numbers
are preceded with a <code>-</code> sign</td>
</tr>
 <tr>
    <td>&lt;space&gt;</td>
    <td>If no sign is going to be written, a blank space is inserted before the
value</td>
</tr>
 <tr>
    <td><code>#</code></td>
    <td><ul>
      <li>For `%o`, `%x`, and `%X`, this flag means to precede the
          value with 0, 0x or 0X respectively for values different than zero.</li>
      <li>For `%f`, `%F`, `%e`, and `%E`, this flag means to add the decimal
          point even when there is no fractional part, unless the value
          is non-finite.</li>
      <li>For `%g` and `%G`, this flag means to add the decimal point even
          when there is no fractional part unless the value is non-finite, and
          never remove the trailing zeros after the decimal point.</li>
      </ul>
   </td>
 </tr>
 <tr>
    <td><code>0</code></td>
    <td>
      Left-pads the number with zeroes (0) instead of spaces when padding is
      specified (see width sub-specifier)</td>
 </tr>
 <tr>
  <td><code>'</code></td>
  <td>
    <p>Formats integers using the appropriating grouping character.
       For example:</p>
    <ul>
      <li><code>FORMAT("%'d", 12345678)</code> returns <code>12,345,678</code></li>
      <li><code>FORMAT("%'x", 12345678)</code> returns <code>bc:614e</code></li>
      <li><code>FORMAT("%'o", 55555)</code> returns <code>15,4403</code></li>
      <p>This flag is only relevant for decimal, hex, and octal values.</p>
    </ul>
  </td>
 </tr>
</table>

Flags may be specified in any order. Duplicate flags aren't an error. When
flags aren't relevant for some element type, they are ignored.

##### Width 
<a id="width"></a>

<table>
  <tr>
    <td>Width</td>
    <td>Description</td>
  </tr>
  <tr>
    <td>&lt;number&gt;</td>
    <td>
      Minimum number of characters to be printed. If the value to be printed
      is shorter than this number, the result is padded with blank spaces.
      The value isn't truncated even if the result is larger
    </td>
  </tr>
  <tr>
    <td><code>*</code></td>
    <td>
      The width isn't specified in the format string, but as an additional
      integer value argument preceding the argument that has to be formatted
    </td>
  </tr>
</table>

##### Precision 
<a id="precision"></a>

<table>
 <tr>
    <td>Precision</td>
    <td>Description</td>
 </tr>
 <tr>
    <td><code>.</code>&lt;number&gt;</td>
    <td>
      <ul>
      <li>For integer specifiers `%d`, `%i`, `%o`, `%u`, `%x`, and `%X`:
          precision specifies the
          minimum number of digits to be written. If the value to be written is
          shorter than this number, the result is padded with trailing zeros.
          The value isn't truncated even if the result is longer. A precision
          of 0 means that no character is written for the value 0.</li>
      <li>For specifiers `%a`, `%A`, `%e`, `%E`, `%f`, and `%F`: this is the
          number of digits to be printed after the decimal point. The default
          value is 6.</li>
      <li>For specifiers `%g` and `%G`: this is the number of significant digits
          to be printed, before the removal of the trailing zeros after the
          decimal point. The default value is 6.</li>
      </ul>
   </td>
</tr>
 <tr>
    <td><code>.*</code></td>
    <td>
      The precision isn't specified in the format string, but as an
      additional integer value argument preceding the argument that has to be
      formatted
   </td>
  </tr>
</table>

##### %g and %G behavior 
<a id="g_and_g_behavior"></a>
The `%g` and `%G` format specifiers choose either the decimal notation (like
the `%f` and `%F` specifiers) or the scientific notation (like the `%e` and `%E`
specifiers), depending on the input value's exponent and the specified
[precision](#precision).

Let p stand for the specified [precision](#precision) (defaults to 6; 1 if the
specified precision is less than 1). The input value is first converted to
scientific notation with precision = (p - 1). If the resulting exponent part x
is less than -4 or no less than p, the scientific notation with precision =
(p - 1) is used; otherwise the decimal notation with precision = (p - 1 - x) is
used.

Unless [`#` flag](#flags) is present, the trailing zeros after the decimal point
are removed, and the decimal point is also removed if there is no digit after
it.

##### %p and %P behavior 
<a id="p_and_p_behavior"></a>

The `%p` format specifier produces a one-line printable string. The `%P`
format specifier produces a multi-line printable string. You can use these
format specifiers with the following data types:

<table>
  <tr>
    <td><strong>Type</strong></td>
    <td><strong>%p</strong></td>
    <td><strong>%P</strong></td>
  </tr>
  <tr>
    <td>PROTO</td>
    <td valign="top">
      <p>PROTO input:</p>
<pre>
message ReleaseDate {
 required int32 year = 1 [default=2019];
 required int32 month = 2 [default=10];
}</pre>
      <p>Produces a one-line printable string representing a protocol buffer:</p>
      <pre>year: 2019 month: 10</pre>
    </td>
    <td valign="top">
      <p>PROTO input:</p>
<pre>
message ReleaseDate {
 required int32 year = 1 [default=2019];
 required int32 month = 2 [default=10];
}</pre>
      <p>Produces a multi-line printable string representing a protocol buffer:</p>
<pre>
year: 2019
month: 10
</pre>
    </td>
  </tr>
  <tr>
    <td>JSON</td>
    <td valign="top">
      <p>JSON input:</p>
<pre>
JSON '
{
  "month": 10,
  "year": 2019
}
'</pre>
      <p>Produces a one-line printable string representing JSON:</p>
      <pre>{"month":10,"year":2019}</pre>
    </td>
    <td valign="top">
      <p>JSON input:</p>
<pre>
JSON '
{
  "month": 10,
  "year": 2019
}
'</pre>
      <p>Produces a multi-line printable string representing JSON:</p>
<pre>
{
  "month": 10,
  "year": 2019
}
</pre>
    </td>
  </tr>
</table>

##### %t and %T behavior 
<a id="t_and_t_behavior"></a>

The `%t` and `%T` format specifiers are defined for all types. The
[width](#width), [precision](#precision), and [flags](#flags) act as they do
for `%s`: the [width](#width) is the minimum width and the `STRING` will be
padded to that size, and [precision](#precision) is the maximum width
of content to show and the `STRING` will be truncated to that size, prior to
padding to width.

The `%t` specifier is always meant to be a readable form of the value.

The `%T` specifier is always a valid SQL literal of a similar type, such as a
wider numeric type. The literal will not include casts or a type name,
except for the special case of non-finite floating point values.

The `STRING` is formatted as follows:

<table>
  <tr>
    <td><strong>Type</strong></td>
    <td><strong>%t</strong></td>
    <td><strong>%T</strong></td>
  </tr>
  <tr>
    <td><code>NULL</code> of any type</td>
    <td>NULL</td>
    <td>NULL</td>
  </tr>
  <tr>
    <td>

<span><code>INT32</code></span><br /><span><code>INT64</code></span><br /><span><code>UINT32</code></span><br /><span><code>UINT64</code></span><br />
</td>
    <td>123</td>
    <td>123</td>
  </tr>

  <tr>
    <td>NUMERIC</td>
    <td>123.0  <em>(always with .0)</em></td>
    <td>NUMERIC "123.0"</td>
  </tr>
  <tr>

    <td>FLOAT, DOUBLE</td>

    <td>
      123.0  <em>(always with .0)</em><br/>
      123e+10<br/>
      <code>inf</code><br/>
      <code>-inf</code><br/>
      <code>NaN</code>
    </td>
    <td>
      123.0  <em>(always with .0)</em><br/>
      123e+10<br/>
      CAST("inf" AS &lt;type&gt;)<br/>
      CAST("-inf" AS &lt;type&gt;)<br/>
      CAST("nan" AS &lt;type&gt;)
    </td>
  </tr>
  <tr>
    <td>STRING</td>
    <td>unquoted string value</td>
    <td>quoted string literal</td>
  </tr>
  <tr>
    <td>BYTES</td>
    <td>
      unquoted escaped bytes<br/>
      e.g., abc\x01\x02
    </td>
    <td>
      quoted bytes literal<br/>
      e.g., b"abc\x01\x02"
    </td>
  </tr>
  <tr>
    <td>BOOL</td>
    <td>boolean value</td>
    <td>boolean value</td>
  </tr>

  <tr>
    <td>ENUM</td>
    <td>EnumName</td>
    <td>"EnumName"</td>
  </tr>
  <tr>
    <td>DATE</td>
    <td>2011-02-03</td>
    <td>DATE "2011-02-03"</td>
  </tr>
  <tr>
    <td>TIMESTAMP</td>
    <td>2011-02-03 04:05:06+00</td>
    <td>TIMESTAMP "2011-02-03 04:05:06+00"</td>
  </tr>
  <tr>
    <td>INTERVAL</td>
    <td>1-2 3 4:5:6.789</td>
    <td>INTERVAL "1-2 3 4:5:6.789" YEAR TO SECOND</td>
  </tr>
  <tr>
    <td>PROTO</td>
    <td>
      one-line printable string representing a protocol buffer.
    </td>
    <td>
      quoted string literal with one-line printable string representing a
      protocol buffer.
    </td>
  </tr>
  <tr>
    <td>ARRAY</td>
    <td>[value, value, ...]<br/>
    where values are formatted with %t</td>
    <td>[value, value, ...]<br/>
    where values are formatted with %T</td>
  </tr>
  <tr>
    <td>STRUCT</td>
    <td>(value, value, ...)<br/>
    where fields are formatted with %t</td>
    <td>(value, value, ...)<br/>
    where fields are formatted with %T<br/>
    <br/>
    Special cases:<br/>
    Zero fields: STRUCT()<br/>
    One field: STRUCT(value)</td>
  </tr>
  <tr>
    <td>JSON</td>
    <td>
      one-line printable string representing JSON.<br />
      <pre class="lang-json prettyprint">{"name":"apple","stock":3}</pre>
    </td>
    <td>
      one-line printable string representing a JSON literal.<br />
      <pre class="lang-sql prettyprint">JSON '{"name":"apple","stock":3}'</pre>
    </td>
  </tr>
</table>

##### Error conditions 
<a id="error_format_specifiers"></a>

If a format specifier is invalid, or isn't compatible with the related
argument type, or the wrong number or arguments are provided, then an error is
produced. For example, the following `<format_string>` expressions are invalid:

```googlesql
FORMAT('%s', 1)
```

```googlesql
FORMAT('%')
```

##### NULL argument handling 
<a id="null_format_specifiers"></a>

A `NULL` format string results in a `NULL` output `STRING`. Any other arguments
are ignored in this case.

The function generally produces a `NULL` value if a `NULL` argument is present.
For example, `FORMAT('%i', NULL_expression)` produces a `NULL STRING` as
output.

However, there are some exceptions: if the format specifier is %t or %T
(both of which produce `STRING`s that effectively match CAST and literal value
semantics), a `NULL` value produces 'NULL' (without the quotes) in the result
`STRING`. For example, the function:

```googlesql
FORMAT('00-%t-00', NULL_expression);
```

Returns

```googlesql
00-NULL-00
```

##### Additional semantic rules 
<a id="rules_format_specifiers"></a>

`DOUBLE` and
`FLOAT` values can be `+/-inf` or `NaN`.
When an argument has one of those values, the result of the format specifiers
`%f`, `%F`, `%e`, `%E`, `%g`, `%G`, and `%t` are `inf`, `-inf`, or `nan`
(or the same in uppercase) as appropriate. This is consistent with how
GoogleSQL casts these values to `STRING`. For `%T`,
GoogleSQL returns quoted strings for
`DOUBLE` values that don't have non-string literal
representations.

[format-specifiers]: #format_specifiers

[format-specifier-list]: #format_specifier_list

[flags]: #flags

[width]: #width

[precision]: #precision

[g-and-g-behavior]: #g_and_g_behavior

[p-and-p-behavior]: #p_and_p_behavior

[t-and-t-behavior]: #t_and_t_behavior

[error-format-specifiers]: #error_format_specifiers

[null-format-specifiers]: #null_format_specifiers

[rules-format-specifiers]: #rules_format_specifiers

## `FROM_BASE32`

```googlesql
FROM_BASE32(string_expr)
```

**Description**

Converts the base32-encoded input `string_expr` into `BYTES` format. To convert
`BYTES` to a base32-encoded `STRING`, use [TO_BASE32][string-link-to-base32].

**Return type**

`BYTES`

**Example**

```googlesql
SELECT FROM_BASE32('MFRGGZDF74======') AS byte_data;

/*-----------+
 | byte_data |
 +-----------+
 | abcde\xff |
 +-----------*/
```

[string-link-to-base32]: #to_base32

## `FROM_BASE64`

```googlesql
FROM_BASE64(string_expr)
```

**Description**

Converts the base64-encoded input `string_expr` into
`BYTES` format. To convert
`BYTES` to a base64-encoded `STRING`,
use [TO_BASE64][string-link-to-to-base64].

There are several base64 encodings in common use that vary in exactly which
alphabet of 65 ASCII characters are used to encode the 64 digits and padding.
See [RFC 4648][RFC-4648] for details. This
function expects the alphabet `[A-Za-z0-9+/=]`.

**Return type**

`BYTES`

**Example**

```googlesql
SELECT FROM_BASE64('/+A=') AS byte_data;

/*-----------+
 | byte_data |
 +-----------+
 | \377\340  |
 +-----------*/
```

To work with an encoding using a different base64 alphabet, you might need to
compose `FROM_BASE64` with the `REPLACE` function. For instance, the
`base64url` url-safe and filename-safe encoding commonly used in web programming
uses `-_=` as the last characters rather than `+/=`. To decode a
`base64url`-encoded string, replace `-` and `_` with `+` and `/` respectively.

```googlesql
SELECT FROM_BASE64(REPLACE(REPLACE('_-A=', '-', '+'), '_', '/')) AS binary;

/*-----------+
 | binary    |
 +-----------+
 | \377\340  |
 +-----------*/
```

[RFC-4648]: https://tools.ietf.org/html/rfc4648#section-4

[string-link-to-to-base64]: #to_base64

## `FROM_HEX`

```googlesql
FROM_HEX(string)
```

**Description**

Converts a hexadecimal-encoded `STRING` into `BYTES` format. Returns an error
if the input `STRING` contains characters outside the range
`(0..9, A..F, a..f)`. The lettercase of the characters doesn't matter. If the
input `STRING` has an odd number of characters, the function acts as if the
input has an additional leading `0`. To convert `BYTES` to a hexadecimal-encoded
`STRING`, use [TO_HEX][string-link-to-to-hex].

**Return type**

`BYTES`

**Example**

```googlesql
WITH Input AS (
  SELECT '00010203aaeeefff' AS hex_str UNION ALL
  SELECT '0AF' UNION ALL
  SELECT '666f6f626172'
)
SELECT hex_str, FROM_HEX(hex_str) AS bytes_str
FROM Input;

/*------------------+----------------------------------+
 | hex_str          | bytes_str                        |
 +------------------+----------------------------------+
 | 0AF              | \x00\xaf                         |
 | 00010203aaeeefff | \x00\x01\x02\x03\xaa\xee\xef\xff |
 | 666f6f626172     | foobar                           |
 +------------------+----------------------------------*/
```

[string-link-to-to-hex]: #to_hex

## `INITCAP`

```googlesql
INITCAP(value[, delimiters])
```

**Description**

Takes a `STRING` and returns it with the first character in each word in
uppercase and all other characters in lowercase. Non-alphabetic characters
remain the same.

`delimiters` is an optional string argument that's used to override the default
set of characters used to separate words. If `delimiters` isn't specified, it
defaults to the following characters: \
`<whitespace> [ ] ( ) { } / | \ < > ! ? @ " ^ # $ & ~ _ , . : ; * % + -`

If `value` or `delimiters` is `NULL`, the function returns `NULL`.

**Return type**

`STRING`

**Examples**

```googlesql
SELECT
  'Hello World-everyone!' AS value,
  INITCAP('Hello World-everyone!') AS initcap_value

/*-------------------------------+-------------------------------+
 | value                         | initcap_value                 |
 +-------------------------------+-------------------------------+
 | Hello World-everyone!         | Hello World-Everyone!         |
 +-------------------------------+-------------------------------*/
```

```googlesql
SELECT
  'Apples1oranges2pears' as value,
  '12' AS delimiters,
  INITCAP('Apples1oranges2pears' , '12') AS initcap_value

/*----------------------+------------+----------------------+
 | value                | delimiters | initcap_value        |
 +----------------------+------------+----------------------+
 | Apples1oranges2pears | 12         | Apples1Oranges2Pears |
 +----------------------+------------+----------------------*/
```

## `INSTR`

```googlesql
INSTR(value, subvalue[, position[, occurrence]])
```

**Description**

Returns the lowest 1-based position of `subvalue` in `value`.
`value` and `subvalue` must be the same type, either
`STRING` or `BYTES`.

If `position` is specified, the search starts at this position in
`value`, otherwise it starts at `1`, which is the beginning of
`value`. If `position` is negative, the function searches backwards
from the end of `value`, with `-1` indicating the last character.
`position` is of type `INT64` and can't be `0`.

If `occurrence` is specified, the search returns the position of a specific
instance of `subvalue` in `value`. If not specified, `occurrence`
defaults to `1` and returns the position of the first occurrence.
For `occurrence` > `1`, the function includes overlapping occurrences.
`occurrence` is of type `INT64` and must be positive.

This function supports specifying [collation][collation].

[collation]: https://github.com/google/googlesql/blob/master/docs/collation-concepts.md

Returns `0` if:

+ No match is found.
+ If `occurrence` is greater than the number of matches found.
+ If `position` is greater than the length of `value`.

Returns `NULL` if:

+ Any input argument is `NULL`.

Returns an error if:

+ `position` is `0`.
+ `occurrence` is `0` or negative.

**Return type**

`INT64`

**Examples**

```googlesql
SELECT
  'banana' AS value, 'an' AS subvalue, 1 AS position, 1 AS occurrence,
  INSTR('banana', 'an', 1, 1) AS instr;

/*--------------+--------------+----------+------------+-------+
 | value        | subvalue     | position | occurrence | instr |
 +--------------+--------------+----------+------------+-------+
 | banana       | an           | 1        | 1          | 2     |
 +--------------+--------------+----------+------------+-------*/
```

```googlesql
SELECT
  'banana' AS value, 'an' AS subvalue, 1 AS position, 2 AS occurrence,
  INSTR('banana', 'an', 1, 2) AS instr;

/*--------------+--------------+----------+------------+-------+
 | value        | subvalue     | position | occurrence | instr |
 +--------------+--------------+----------+------------+-------+
 | banana       | an           | 1        | 2          | 4     |
 +--------------+--------------+----------+------------+-------*/
```

```googlesql
SELECT
  'banana' AS value, 'an' AS subvalue, 1 AS position, 3 AS occurrence,
  INSTR('banana', 'an', 1, 3) AS instr;

/*--------------+--------------+----------+------------+-------+
 | value        | subvalue     | position | occurrence | instr |
 +--------------+--------------+----------+------------+-------+
 | banana       | an           | 1        | 3          | 0     |
 +--------------+--------------+----------+------------+-------*/
```

```googlesql
SELECT
  'banana' AS value, 'an' AS subvalue, 3 AS position, 1 AS occurrence,
  INSTR('banana', 'an', 3, 1) AS instr;

/*--------------+--------------+----------+------------+-------+
 | value        | subvalue     | position | occurrence | instr |
 +--------------+--------------+----------+------------+-------+
 | banana       | an           | 3        | 1          | 4     |
 +--------------+--------------+----------+------------+-------*/
```

```googlesql
SELECT
  'banana' AS value, 'an' AS subvalue, -1 AS position, 1 AS occurrence,
  INSTR('banana', 'an', -1, 1) AS instr;

/*--------------+--------------+----------+------------+-------+
 | value        | subvalue     | position | occurrence | instr |
 +--------------+--------------+----------+------------+-------+
 | banana       | an           | -1       | 1          | 4     |
 +--------------+--------------+----------+------------+-------*/
```

```googlesql
SELECT
  'banana' AS value, 'an' AS subvalue, -3 AS position, 1 AS occurrence,
  INSTR('banana', 'an', -3, 1) AS instr;

/*--------------+--------------+----------+------------+-------+
 | value        | subvalue     | position | occurrence | instr |
 +--------------+--------------+----------+------------+-------+
 | banana       | an           | -3       | 1          | 4     |
 +--------------+--------------+----------+------------+-------*/
```

```googlesql
SELECT
  'banana' AS value, 'ann' AS subvalue, 1 AS position, 1 AS occurrence,
  INSTR('banana', 'ann', 1, 1) AS instr;

/*--------------+--------------+----------+------------+-------+
 | value        | subvalue     | position | occurrence | instr |
 +--------------+--------------+----------+------------+-------+
 | banana       | ann          | 1        | 1          | 0     |
 +--------------+--------------+----------+------------+-------*/
```

```googlesql
SELECT
  'helloooo' AS value, 'oo' AS subvalue, 1 AS position, 1 AS occurrence,
  INSTR('helloooo', 'oo', 1, 1) AS instr;

/*--------------+--------------+----------+------------+-------+
 | value        | subvalue     | position | occurrence | instr |
 +--------------+--------------+----------+------------+-------+
 | helloooo     | oo           | 1        | 1          | 5     |
 +--------------+--------------+----------+------------+-------*/
```

```googlesql
SELECT
  'helloooo' AS value, 'oo' AS subvalue, 1 AS position, 2 AS occurrence,
  INSTR('helloooo', 'oo', 1, 2) AS instr;

/*--------------+--------------+----------+------------+-------+
 | value        | subvalue     | position | occurrence | instr |
 +--------------+--------------+----------+------------+-------+
 | helloooo     | oo           | 1        | 2          | 6     |
 +--------------+--------------+----------+------------+-------*/
```

## `LEFT`

```googlesql
LEFT(value, length)
```

**Description**

Returns a `STRING` or `BYTES` value that consists of the specified
number of leftmost characters or bytes from `value`. The `length` is an
`INT64` that specifies the length of the returned
value. If `value` is of type `BYTES`, `length` is the number of leftmost bytes
to return. If `value` is `STRING`, `length` is the number of leftmost characters
to return.

If `length` is 0, an empty `STRING` or `BYTES` value will be
returned. If `length` is negative, an error will be returned. If `length`
exceeds the number of characters or bytes from `value`, the original `value`
will be returned.

**Return type**

`STRING` or `BYTES`

**Examples**

```googlesql
SELECT LEFT('banana', 3) AS results

/*---------+
 | results |
  +--------+
 | ban     |
 +---------*/
```

```googlesql
SELECT LEFT(b'\xab\xcd\xef\xaa\xbb', 3) AS results

/*--------------+
 | results      |
 +--------------+
 | \xab\xcd\xef |
 +--------------*/
```

## `LENGTH`

```googlesql
LENGTH(value)
```

**Description**

Returns the length of the `STRING` or `BYTES` value. The returned
value is in characters for `STRING` arguments and in bytes for the `BYTES`
argument.

**Return type**

`INT64`

**Examples**

```googlesql
SELECT
  LENGTH('абвгд') AS string_example,
  LENGTH(CAST('абвгд' AS BYTES)) AS bytes_example;

/*----------------+---------------+
 | string_example | bytes_example |
 +----------------+---------------+
 | 5              | 10            |
 +----------------+---------------*/
```

## `LOWER`

```googlesql
LOWER(value)
```

**Description**

For `STRING` arguments, returns the original string with all alphabetic
characters in lowercase. Mapping between lowercase and uppercase is done
according to the
[Unicode Character Database][string-link-to-unicode-character-definitions]
without taking into account language-specific mappings.

For `BYTES` arguments, the argument is treated as ASCII text, with all bytes
greater than 127 left intact.

**Return type**

`STRING` or `BYTES`

**Examples**

```googlesql
SELECT
  LOWER('FOO BAR BAZ') AS example
FROM items;

/*-------------+
 | example     |
 +-------------+
 | foo bar baz |
 +-------------*/
```

[string-link-to-unicode-character-definitions]: http://unicode.org/ucd/

## `LPAD`

```googlesql
LPAD(original_value, return_length[, pattern])
```

**Description**

Returns a `STRING` or `BYTES` value that consists of `original_value` prepended
with `pattern`. The `return_length` is an `INT64` that
specifies the length of the returned value. If `original_value` is of type
`BYTES`, `return_length` is the number of bytes. If `original_value` is
of type `STRING`, `return_length` is the number of characters.

The default value of `pattern` is a blank space.

Both `original_value` and `pattern` must be the same data type.

If `return_length` is less than or equal to the `original_value` length, this
function returns the `original_value` value, truncated to the value of
`return_length`. For example, `LPAD('hello world', 7);` returns `'hello w'`.

If `original_value`, `return_length`, or `pattern` is `NULL`, this function
returns `NULL`.

This function returns an error if:

+ `return_length` is negative
+ `pattern` is empty

**Return type**

`STRING` or `BYTES`

**Examples**

```googlesql
SELECT FORMAT('%T', LPAD('c', 5)) AS results

/*---------+
 | results |
 +---------+
 | "    c" |
 +---------*/
```

```googlesql
SELECT LPAD('b', 5, 'a') AS results

/*---------+
 | results |
 +---------+
 | aaaab   |
 +---------*/
```

```googlesql
SELECT LPAD('abc', 10, 'ghd') AS results

/*------------+
 | results    |
 +------------+
 | ghdghdgabc |
 +------------*/
```

```googlesql
SELECT LPAD('abc', 2, 'd') AS results

/*---------+
 | results |
 +---------+
 | ab      |
 +---------*/
```

```googlesql
SELECT FORMAT('%T', LPAD(b'abc', 10, b'ghd')) AS results

/*---------------+
 | results       |
 +---------------+
 | b"ghdghdgabc" |
 +---------------*/
```

## `LTRIM`

```googlesql
LTRIM(value1[, value2])
```

**Description**

Identical to [TRIM][string-link-to-trim], but only removes leading characters.

**Return type**

`STRING` or `BYTES`

**Examples**

```googlesql
SELECT CONCAT('#', LTRIM('   apple   '), '#') AS example

/*-------------+
 | example     |
 +-------------+
 | #apple   #  |
 +-------------*/
```

```googlesql
SELECT LTRIM('***apple***', '*') AS example

/*-----------+
 | example   |
 +-----------+
 | apple***  |
 +-----------*/
```

```googlesql
SELECT LTRIM('xxxapplexxx', 'xyz') AS example

/*-----------+
 | example   |
 +-----------+
 | applexxx  |
 +-----------*/
```

[string-link-to-trim]: #trim

## `NORMALIZE`

```googlesql
NORMALIZE(value[, normalization_mode])
```

**Description**

Takes a string value and returns it as a normalized string. If you don't
provide a normalization mode, `NFC` is used.

[Normalization][string-link-to-normalization-wikipedia] is used to ensure that
two strings are equivalent. Normalization is often used in situations in which
two strings render the same on the screen but have different Unicode code
points.

`NORMALIZE` supports four optional normalization modes:

| Value   | Name                                           | Description|
|---------|------------------------------------------------|------------|
| `NFC`   | Normalization Form Canonical Composition       | Decomposes and recomposes characters by canonical equivalence.|
| `NFKC`  | Normalization Form Compatibility Composition   | Decomposes characters by compatibility, then recomposes them by canonical equivalence.|
| `NFD`   | Normalization Form Canonical Decomposition     | Decomposes characters by canonical equivalence, and multiple combining characters are arranged in a specific order.|
| `NFKD`  | Normalization Form Compatibility Decomposition | Decomposes characters by compatibility, and multiple combining characters are arranged in a specific order.|

**Return type**

`STRING`

**Examples**

The following example normalizes different language characters:

```googlesql
SELECT
  NORMALIZE('\u00ea') as a,
  NORMALIZE('\u0065\u0302') as b,
  NORMALIZE('\u00ea') = NORMALIZE('\u0065\u0302') as normalized;

/*---+---+------------+
 | a | b | normalized |
 +---+---+------------+
 | ê | ê | TRUE       |
 +---+---+------------*/
```
The following examples normalize different space characters:

```googlesql
SELECT NORMALIZE('Raha\u2004Mahan', NFKC) AS normalized_name

/*-----------------+
 | normalized_name |
 +-----------------+
 | Raha Mahan      |
 +-----------------*/
```

```googlesql
SELECT NORMALIZE('Raha\u2005Mahan', NFKC) AS normalized_name

/*-----------------+
 | normalized_name |
 +-----------------+
 | Raha Mahan      |
 +-----------------*/
```

```googlesql
SELECT NORMALIZE('Raha\u2006Mahan', NFKC) AS normalized_name

/*-----------------+
 | normalized_name |
 +-----------------+
 | Raha Mahan      |
 +-----------------*/
```

```googlesql
SELECT NORMALIZE('Raha Mahan', NFKC) AS normalized_name

/*-----------------+
 | normalized_name |
 +-----------------+
 | Raha Mahan      |
 +-----------------*/
```

[string-link-to-normalization-wikipedia]: https://en.wikipedia.org/wiki/Unicode_equivalence#Normalization

## `NORMALIZE_AND_CASEFOLD`

```googlesql
NORMALIZE_AND_CASEFOLD(value[, normalization_mode])
```

**Description**

Takes a string value and returns it as a normalized string. If you don't
provide a normalization mode, `NFC` is used.

[Normalization][string-link-to-normalization-wikipedia] is used to ensure that
two strings are equivalent. Normalization is often used in situations in which
two strings render the same on the screen but have different Unicode code
points.

[Case folding][string-link-to-case-folding-wikipedia] is used for the caseless
comparison of strings. If you need to compare strings and case shouldn't be
considered, use `NORMALIZE_AND_CASEFOLD`, otherwise use
[`NORMALIZE`][string-link-to-normalize].

`NORMALIZE_AND_CASEFOLD` supports four optional normalization modes:

| Value   | Name                                           | Description|
|---------|------------------------------------------------|------------|
| `NFC`   | Normalization Form Canonical Composition       | Decomposes and recomposes characters by canonical equivalence.|
| `NFKC`  | Normalization Form Compatibility Composition   | Decomposes characters by compatibility, then recomposes them by canonical equivalence.|
| `NFD`   | Normalization Form Canonical Decomposition     | Decomposes characters by canonical equivalence, and multiple combining characters are arranged in a specific order.|
| `NFKD`  | Normalization Form Compatibility Decomposition | Decomposes characters by compatibility, and multiple combining characters are arranged in a specific order.|

**Return type**

`STRING`

**Examples**

```googlesql
SELECT
  NORMALIZE('The red barn') = NORMALIZE('The Red Barn') AS normalized,
  NORMALIZE_AND_CASEFOLD('The red barn')
    = NORMALIZE_AND_CASEFOLD('The Red Barn') AS normalized_with_case_folding;

/*------------+------------------------------+
 | normalized | normalized_with_case_folding |
 +------------+------------------------------+
 | FALSE      | TRUE                         |
 +------------+------------------------------*/
```

```googlesql
SELECT
  '\u2168' AS a,
  'IX' AS b,
  NORMALIZE_AND_CASEFOLD('\u2168', NFD)=NORMALIZE_AND_CASEFOLD('IX', NFD) AS nfd,
  NORMALIZE_AND_CASEFOLD('\u2168', NFC)=NORMALIZE_AND_CASEFOLD('IX', NFC) AS nfc,
  NORMALIZE_AND_CASEFOLD('\u2168', NFKD)=NORMALIZE_AND_CASEFOLD('IX', NFKD) AS nfkd,
  NORMALIZE_AND_CASEFOLD('\u2168', NFKC)=NORMALIZE_AND_CASEFOLD('IX', NFKC) AS nfkc;

/*---+----+-------+-------+------+------+
 | a | b  | nfd   | nfc   | nfkd | nfkc |
 +---+----+-------+-------+------+------+
 | Ⅸ | IX | false | false | true | true |
 +---+----+-------+-------+------+------*/
```

```googlesql
SELECT
  '\u0041\u030A' AS a,
  '\u00C5' AS b,
  NORMALIZE_AND_CASEFOLD('\u0041\u030A', NFD)=NORMALIZE_AND_CASEFOLD('\u00C5', NFD) AS nfd,
  NORMALIZE_AND_CASEFOLD('\u0041\u030A', NFC)=NORMALIZE_AND_CASEFOLD('\u00C5', NFC) AS nfc,
  NORMALIZE_AND_CASEFOLD('\u0041\u030A', NFKD)=NORMALIZE_AND_CASEFOLD('\u00C5', NFKD) AS nkfd,
  NORMALIZE_AND_CASEFOLD('\u0041\u030A', NFKC)=NORMALIZE_AND_CASEFOLD('\u00C5', NFKC) AS nkfc;

/*---+----+-------+-------+------+------+
 | a | b  | nfd   | nfc   | nkfd | nkfc |
 +---+----+-------+-------+------+------+
 | Å | Å  | true  | true  | true | true |
 +---+----+-------+-------+------+------*/
```

[string-link-to-normalization-wikipedia]: https://en.wikipedia.org/wiki/Unicode_equivalence#Normalization

[string-link-to-case-folding-wikipedia]: https://en.wikipedia.org/wiki/Letter_case#Case_folding

[string-link-to-normalize]: #normalize

## `OCTET_LENGTH`

```googlesql
OCTET_LENGTH(value)
```

Alias for [`BYTE_LENGTH`][byte-length].

[byte-length]: #byte_length

## `REGEXP_CONTAINS`

```googlesql
REGEXP_CONTAINS(value, regexp)
```

**Description**

Returns `TRUE` if `value` is a partial match for the regular expression,
`regexp`.

If the `regexp` argument is invalid, the function returns an error.

You can search for a full match by using `^` (beginning of text) and `$` (end of
text). Due to regular expression operator precedence, it's good practice to use
parentheses around everything between `^` and `$`.

Note: GoogleSQL provides regular expression support using the
[re2][string-link-to-re2] library; see that documentation for its
regular expression syntax.

**Return type**

`BOOL`

**Examples**

The following queries check to see if an email is valid:

```googlesql
SELECT
  'foo@example.com' AS email,
  REGEXP_CONTAINS('foo@example.com', r'@[a-zA-Z0-9-]+\.[a-zA-Z0-9-.]+') AS is_valid

/*-----------------+----------+
 | email           | is_valid |
 +-----------------+----------+
 | foo@example.com | TRUE     |
 +-----------------+----------*/
 ```

 ```googlesql
SELECT
  'www.example.net' AS email,
  REGEXP_CONTAINS('www.example.net', r'@[a-zA-Z0-9-]+\.[a-zA-Z0-9-.]+') AS is_valid

/*-----------------+----------+
 | email           | is_valid |
 +-----------------+----------+
 | www.example.net | FALSE    |
 +-----------------+----------*/
 ```

The following queries check to see if an email is valid. They
perform a full match, using `^` and `$`. Due to regular expression operator
precedence, it's good practice to use parentheses around everything between `^`
and `$`.

```googlesql
SELECT
  'a@foo.com' AS email,
  REGEXP_CONTAINS('a@foo.com', r'^([\w.+-]+@foo\.com|[\w.+-]+@bar\.org)$') AS valid_email_address,
  REGEXP_CONTAINS('a@foo.com', r'^[\w.+-]+@foo\.com|[\w.+-]+@bar\.org$') AS without_parentheses;

/*----------------+---------------------+---------------------+
 | email          | valid_email_address | without_parentheses |
 +----------------+---------------------+---------------------+
 | a@foo.com      | true                | true                |
 +----------------+---------------------+---------------------*/
```

```googlesql
SELECT
  'a@foo.computer' AS email,
  REGEXP_CONTAINS('a@foo.computer', r'^([\w.+-]+@foo\.com|[\w.+-]+@bar\.org)$') AS valid_email_address,
  REGEXP_CONTAINS('a@foo.computer', r'^[\w.+-]+@foo\.com|[\w.+-]+@bar\.org$') AS without_parentheses;

/*----------------+---------------------+---------------------+
 | email          | valid_email_address | without_parentheses |
 +----------------+---------------------+---------------------+
 | a@foo.computer | false               | true                |
 +----------------+---------------------+---------------------*/
```

```googlesql
SELECT
  'b@bar.org' AS email,
  REGEXP_CONTAINS('b@bar.org', r'^([\w.+-]+@foo\.com|[\w.+-]+@bar\.org)$') AS valid_email_address,
  REGEXP_CONTAINS('b@bar.org', r'^[\w.+-]+@foo\.com|[\w.+-]+@bar\.org$') AS without_parentheses;

/*----------------+---------------------+---------------------+
 | email          | valid_email_address | without_parentheses |
 +----------------+---------------------+---------------------+
 | b@bar.org      | true                | true                |
 +----------------+---------------------+---------------------*/
```

```googlesql
SELECT
  '!b@bar.org' AS email,
  REGEXP_CONTAINS('!b@bar.org', r'^([\w.+-]+@foo\.com|[\w.+-]+@bar\.org)$') AS valid_email_address,
  REGEXP_CONTAINS('!b@bar.org', r'^[\w.+-]+@foo\.com|[\w.+-]+@bar\.org$') AS without_parentheses;

/*----------------+---------------------+---------------------+
 | email          | valid_email_address | without_parentheses |
 +----------------+---------------------+---------------------+
 | !b@bar.org     | false               | true                |
 +----------------+---------------------+---------------------*/
```

```googlesql
SELECT
  'c@buz.net' AS email,
  REGEXP_CONTAINS('c@buz.net', r'^([\w.+-]+@foo\.com|[\w.+-]+@bar\.org)$') AS valid_email_address,
  REGEXP_CONTAINS('c@buz.net', r'^[\w.+-]+@foo\.com|[\w.+-]+@bar\.org$') AS without_parentheses;

/*----------------+---------------------+---------------------+
 | email          | valid_email_address | without_parentheses |
 +----------------+---------------------+---------------------+
 | c@buz.net      | false               | false               |
 +----------------+---------------------+---------------------*/
```

[string-link-to-re2]: https://github.com/google/re2/wiki/Syntax

## `REGEXP_EXTRACT`

```googlesql
REGEXP_EXTRACT(value, regexp[, position[, occurrence]])
```

**Description**

Returns the substring in `value` that matches the
[re2 regular expression][string-link-to-re2], `regexp`.
Returns `NULL` if there is no match.

If the regular expression contains a capturing group (`(...)`), and there is a
match for that capturing group, that match is returned. If there
are multiple matches for a capturing group, the first match is returned.

To extract matches for multiple capturing groups in a single call, use
[`REGEXP_EXTRACT_GROUPS`][regexp-extract-groups].

If `position` is specified, the search starts at this
position in `value`, otherwise it starts at the beginning of `value`. The
`position` must be a positive integer and can't be 0. If `position` is greater
than the length of `value`, `NULL` is returned.

If `occurrence` is specified, the search returns a specific occurrence of the
`regexp` in `value`, otherwise returns the first match. If `occurrence` is
greater than the number of matches found, `NULL` is returned. For
`occurrence` > 1, the function searches for additional occurrences beginning
with the character following the previous occurrence.

Returns an error if:

+ The regular expression is invalid
+ The regular expression has more than one capturing group
+ The `position` isn't a positive integer
+ The `occurrence` isn't a positive integer

**Return type**

`STRING` or `BYTES`

**Examples**

```googlesql
SELECT REGEXP_EXTRACT('foo@example.com', r'^[a-zA-Z0-9_.+-]+') AS user_name

/*-----------+
 | user_name |
 +-----------+
 | foo       |
 +-----------*/
```

```googlesql
SELECT REGEXP_EXTRACT('foo@example.com', r'^[a-zA-Z0-9_.+-]+@[a-zA-Z0-9-]+\.([a-zA-Z0-9-.]+$)')

/*------------------+
 | top_level_domain |
 +------------------+
 | com              |
 +------------------*/
```

```googlesql
SELECT
  REGEXP_EXTRACT('ab', '.b') AS result_a,
  REGEXP_EXTRACT('ab', '(.)b') AS result_b,
  REGEXP_EXTRACT('xyztb', '(.)+b') AS result_c,
  REGEXP_EXTRACT('ab', '(z)?b') AS result_d

/*-------------------------------------------+
 | result_a | result_b | result_c | result_d |
 +-------------------------------------------+
 | ab       | a        | t        | NULL     |
 +-------------------------------------------*/
```

```googlesql
WITH example AS
(SELECT 'Hello Helloo and Hellooo' AS value, 'H?ello+' AS regex, 1 as position,
1 AS occurrence UNION ALL
SELECT 'Hello Helloo and Hellooo', 'H?ello+', 1, 2 UNION ALL
SELECT 'Hello Helloo and Hellooo', 'H?ello+', 1, 3 UNION ALL
SELECT 'Hello Helloo and Hellooo', 'H?ello+', 1, 4 UNION ALL
SELECT 'Hello Helloo and Hellooo', 'H?ello+', 2, 1 UNION ALL
SELECT 'Hello Helloo and Hellooo', 'H?ello+', 3, 1 UNION ALL
SELECT 'Hello Helloo and Hellooo', 'H?ello+', 3, 2 UNION ALL
SELECT 'Hello Helloo and Hellooo', 'H?ello+', 3, 3 UNION ALL
SELECT 'Hello Helloo and Hellooo', 'H?ello+', 20, 1 UNION ALL
SELECT 'cats&dogs&rabbits' ,'\\w+&', 1, 2 UNION ALL
SELECT 'cats&dogs&rabbits', '\\w+&', 2, 3
)
SELECT value, regex, position, occurrence, REGEXP_EXTRACT(value, regex,
position, occurrence) AS regexp_value FROM example;

/*--------------------------+---------+----------+------------+--------------+
 | value                    | regex   | position | occurrence | regexp_value |
 +--------------------------+---------+----------+------------+--------------+
 | Hello Helloo and Hellooo | H?ello+ | 1        | 1          | Hello        |
 | Hello Helloo and Hellooo | H?ello+ | 1        | 2          | Helloo       |
 | Hello Helloo and Hellooo | H?ello+ | 1        | 3          | Hellooo      |
 | Hello Helloo and Hellooo | H?ello+ | 1        | 4          | NULL         |
 | Hello Helloo and Hellooo | H?ello+ | 2        | 1          | ello         |
 | Hello Helloo and Hellooo | H?ello+ | 3        | 1          | Helloo       |
 | Hello Helloo and Hellooo | H?ello+ | 3        | 2          | Hellooo      |
 | Hello Helloo and Hellooo | H?ello+ | 3        | 3          | NULL         |
 | Hello Helloo and Hellooo | H?ello+ | 20       | 1          | NULL         |
 | cats&dogs&rabbits        | \w+&    | 1        | 2          | dogs&        |
 | cats&dogs&rabbits        | \w+&    | 2        | 3          | NULL         |
 +--------------------------+---------+----------+------------+--------------*/
```

[string-link-to-re2]: https://github.com/google/re2/wiki/Syntax

[regexp-extract-groups]: https://github.com/google/googlesql/blob/master/docs/string_functions.md#regexp_extract_groups

## `REGEXP_EXTRACT_GROUPS`

```googlesql
REGEXP_EXTRACT_GROUPS(value, regexp)
```

**Description**

Returns a `STRUCT` where each field contains a substring from `value` that
matches a capturing group in the [re2 regular expression][string-link-to-re2],
`regexp`. The function returns the substrings from the first place in `value`
where the *entire* `regexp` pattern matches.

**Details**

This function is similar to [`REGEXP_EXTRACT`][regexp-extract], but it returns a
`STRUCT` with a field for each capturing group in the `regexp`.

The `regexp` must contain at least one capturing group. The fields in the
returned `STRUCT` correspond to these capturing groups:

+   If a capturing group is named (for example, `(?<name>...)` or `(?P<name>...)`),
    the corresponding `STRUCT` field will have that name. Both syntaxes are
    equivalent.
+   If a capturing group is unnamed, the corresponding `STRUCT` field is
    anonymous. These fields can be accessed by their 0-based position in the
    `STRUCT`.
+   The order of fields in the `STRUCT` matches the order of the capturing
    groups in `regexp` from left to right.

Returns `NULL` if `value` is `NULL` or if the overall `regexp` pattern doesn't
match at all. If a specific capturing group doesn't match (for example, if it's
part of an alternation or is optional), the corresponding `STRUCT` field is
`NULL`.

Returns an error if:

+ The `regexp` is invalid.
+ The `regexp` is not a string literal.
+ The `regexp` has no capturing groups.
+ A capturing group name is not a valid `STRUCT` field name (for example, starts
  with a digit or contains spaces). Valid names consist of letters, numbers,
  and underscores, and must start with a letter or underscore.
+ The same capturing group name is used more than once (case-insensitive).

**Return type**

`STRUCT<...>`

The fields of the `STRUCT` are generally `STRING` (or `BYTES` if the inputs are
`BYTES`). However, fields can be [auto-casted](#auto_casting) to other types.

**Examples**

Extract unnamed groups:

```googlesql
SELECT REGEXP_EXTRACT_GROUPS('abc123xyz', r'([a-z]+)([0-9]+)([a-z]+)') AS result

/*---------------------------------+
 | result                          |
 +---------------------------------+
 | {abc, 123, xyz}                 |
 +---------------------------------*/
```

Extract named groups:

```googlesql
SELECT REGEXP_EXTRACT_GROUPS('2025-09-10', r'(?<year>\d{4})-(?<month>\d{2})-(?<day>\d{2})') AS result

/*----------------------------------------------+
 | result                                       |
 +----------------------------------------------+
 | {2025 year, 09 month, 10 day}                |
 +----------------------------------------------*/
```

**Expand STRUCT fields into columns**

Because `REGEXP_EXTRACT_GROUPS` returns a `STRUCT`, you can use the `.*` operator
in the `SELECT` list to expand the fields of the `STRUCT` into separate columns.
Expanding `STRUCT` fields into columns is particularly useful when all capturing
groups are named.

```googlesql
SELECT REGEXP_EXTRACT_GROUPS('PROD-WIDGET-1234', r'(?<env>\w+)-(?<product>\w+)-(?<id>\d+)').*

/*-------+-----------+------+
 | env   | product   | id   |
 +-------+-----------+------+
 | PROD  | WIDGET    | 1234 |
 +-------+-----------+------*/
```

Mix of named and unnamed groups:

```googlesql
SELECT REGEXP_EXTRACT_GROUPS('id:123', r'(?<key>[a-z]+):([0-9]+)') AS result

/*-----------------------+
 | result                |
 +-----------------------+
 | {id key, 123}         |
 +-----------------------*/
```

No match returns `NULL`:

```googlesql
SELECT REGEXP_EXTRACT_GROUPS('abc', r'(\d+)') AS result

/*--------+
 | result |
 +--------+
 | NULL   |
 +--------*/
```

Optional groups and empty matches:

```googlesql
WITH inputs AS (
  SELECT 'id:123:extra' AS t UNION ALL
  SELECT 'id:123:' AS t UNION ALL
  SELECT 'id:123' AS t
)
SELECT
  t,
  REGEXP_EXTRACT_GROUPS(t, r'(?<key>\w+):(?<val>\w+)(?::(?<opt>\w*))?') AS result
FROM inputs;

/*-----------------+--------------------------------------+
 | t               | result                               |
 +-----------------+--------------------------------------+
 | id:123:extra    | {id key, 123 val, extra opt}         |
 | id:123:         | {id key, 123 val,  opt}              |
 | id:123          | {id key, 123 val, NULL opt}          |
 +-----------------+--------------------------------------*/
```

Note that in the second row, the optional group `opt` matches an empty string,
which is different from the third row where the group doesn't match at all and
results in `NULL`.

Nested groups:

```googlesql
SELECT REGEXP_EXTRACT_GROUPS('a=b=c', r'(\w+)=((\w+)=\w+)') AS result

/*-----------------------+
 | result                |
 +-----------------------+
 | {a, b=c, b}           |
 +-----------------------*/
```

Alternation with different groups:

```googlesql
WITH inputs AS (
  SELECT 'config_id=123' AS t UNION ALL
  SELECT 'option_name=ABC' AS t
)
SELECT
  t,
  REGEXP_EXTRACT_GROUPS(t, r'config_id=(?<id>\d+)|option_name=(?<name>\w+)') AS result
FROM inputs;

/*-----------------+--------------------------+
 | t               | result                   |
 +-----------------+--------------------------+
 | config_id=123   | {123 id, NULL name}      |
 | option_name=ABC | {NULL id, ABC name}      |
 +-----------------+--------------------------*/
```

The `STRUCT` result contains fields for all named capturing groups across all
alternatives in the regular expression. In each row, only the fields
corresponding to the alternative that matched are populated. Other fields are
`NULL`.

##### Auto-casting 
<a id="auto_casting"></a>

You can automatically cast the captured substring to a specific type by
suffixing the capturing group name with a double underscore (`__`) followed by
the type name.

Any type that can be cast from `STRING` (or `BYTES` for the `BYTES` version
of the function) is supported. Type names are case-insensitive.

The field name in the resulting `STRUCT` will have the `__TYPE` suffix removed.

If the captured substring can't be cast to the specified type, an error is
returned. This includes casting an empty string to a numeric or boolean type.
If the captured substring is `NULL` (due to an optional group not matching), the
cast result is also `NULL`.

**Examples of auto-casting**

```googlesql
SELECT REGEXP_EXTRACT_GROUPS('val=0x1a', r'val=(?<val__INT64>0x[0-9a-fA-F]+)') AS result

/*-------------+
 | result      |
 +-------------+
 | {26 val}    |
 +-------------*/
```

Auto-casted values in expressions with Pipe syntax:

```googlesql
FROM UNNEST(['02:30:10', '01:02:03']) AS time_str
|> EXTEND REGEXP_EXTRACT_GROUPS(time_str, r'(?<h__INT64>\d{2}):(?<m__INT64>\d{2}):(?<s__INT64>\d{2})').*
|> SELECT time_str, h * 3600 + m * 60 + s AS total_seconds

/*----------+---------------+
 | time_str | total_seconds |
 +----------+---------------+
 | 02:30:10 | 9010          |
 | 01:02:03 | 3723          |
 +----------+---------------*/
```

Expand auto-casted fields into columns:

```googlesql
SELECT REGEXP_EXTRACT_GROUPS('2025-09-10', r'(?<year__INT64>\d{4})-(?<month__INT64>\d{2})-(?<day__INT64>\d{2})').*

/*--------+---------+-------+
 | year   | month   | day   |
 +--------+---------+-------+
 | 2025   | 9       | 10    |
 +--------+---------+-------*/
```

Cast failure:

```googlesql {.bad}
-- Error: Bad INT64 value
SELECT REGEXP_EXTRACT_GROUPS('ID: ABC', r'ID: (?<item_id__INT64>\w+)')
```

Cast failure with empty string:

```googlesql {.bad}
-- Error: Bad INT64 value
SELECT REGEXP_EXTRACT_GROUPS('ID: ', r'ID: (?<item_id__INT64>\d*)')
```

Workaround for empty string cast failure by making the group optional:

```googlesql
SELECT REGEXP_EXTRACT_GROUPS('ID: ', r'ID: (?<item_id__INT64>\d+)?') AS result

/*-----------------+
 | result          |
 +-----------------+
 | {NULL item_id}  |
 +-----------------*/
```

[string-link-to-re2]: https://github.com/google/re2/wiki/Syntax

[regexp-extract]: https://github.com/google/googlesql/blob/master/docs/string_functions.md#regexp_extract

## `REGEXP_EXTRACT_ALL`

```googlesql
REGEXP_EXTRACT_ALL(value, regexp)
```

**Description**

Returns an array of all substrings of `value` that match the
[re2 regular expression][string-link-to-re2], `regexp`. Returns an empty array
if there is no match.

If the regular expression contains a capturing group (`(...)`), and there is a
match for that capturing group, that match is added to the results.

The `REGEXP_EXTRACT_ALL` function only returns non-overlapping matches. For
example, using this function to extract `ana` from `banana` returns only one
substring, not two.

Returns an error if:

+ The regular expression is invalid
+ The regular expression has more than one capturing group

**Return type**

`ARRAY<STRING>` or `ARRAY<BYTES>`

**Examples**

```googlesql
SELECT REGEXP_EXTRACT_ALL('Try `func(x)` or `func(y)`', '`(.+?)`') AS example

/*--------------------+
 | example            |
 +--------------------+
 | [func(x), func(y)] |
 +--------------------*/
```

[string-link-to-re2]: https://github.com/google/re2/wiki/Syntax

## `REGEXP_INSTR`

```googlesql
REGEXP_INSTR(source_value, regexp [, position[, occurrence, [occurrence_position]]])
```

**Description**

Returns the lowest 1-based position of a regular expression, `regexp`, in
`source_value`. `source_value` and `regexp` must be the same type, either
`STRING` or `BYTES`.

If `position` is specified, the search starts at this position in
`source_value`, otherwise it starts at `1`, which is the beginning of
`source_value`. `position` is of type `INT64` and must be positive.

If `occurrence` is specified, the search returns the position of a specific
instance of `regexp` in `source_value`. If not specified, `occurrence` defaults
to `1` and returns the position of the first occurrence. For `occurrence` > 1,
the function searches for the next, non-overlapping occurrence.
`occurrence` is of type `INT64` and must be positive.

You can optionally use `occurrence_position` to specify where a position
in relation to an `occurrence` starts. Your choices are:

+  `0`: Returns the start position of `occurrence`.
+  `1`: Returns the end position of `occurrence` + `1`. If the
   end of the occurrence is at the end of `source_value `,
   `LENGTH(source_value) + 1` is returned.

Returns `0` if:

+ No match is found.
+ If `occurrence` is greater than the number of matches found.
+ If `position` is greater than the length of `source_value`.
+ The regular expression is empty.

Returns `NULL` if:

+ `position` is `NULL`.
+ `occurrence` is `NULL`.

Returns an error if:

+ `position` is `0` or negative.
+ `occurrence` is `0` or negative.
+ `occurrence_position` is neither `0` nor `1`.
+ The regular expression is invalid.
+ The regular expression has more than one capturing group.

**Return type**

`INT64`

**Examples**

```googlesql
SELECT
  REGEXP_INSTR('ab@cd-ef',  '@[^-]*') AS instr_a,
  REGEXP_INSTR('ab@d-ef',   '@[^-]*') AS instr_b,
  REGEXP_INSTR('abc@cd-ef', '@[^-]*') AS instr_c,
  REGEXP_INSTR('abc-ef',    '@[^-]*') AS instr_d,

/*---------------------------------------+
 | instr_a | instr_b | instr_c | instr_d |
 +---------------------------------------+
 | 3       | 3       | 4       | 0       |
 +---------------------------------------*/
```

```googlesql
SELECT
  REGEXP_INSTR('a@cd-ef b@cd-ef', '@[^-]*', 1) AS instr_a,
  REGEXP_INSTR('a@cd-ef b@cd-ef', '@[^-]*', 2) AS instr_b,
  REGEXP_INSTR('a@cd-ef b@cd-ef', '@[^-]*', 3) AS instr_c,
  REGEXP_INSTR('a@cd-ef b@cd-ef', '@[^-]*', 4) AS instr_d,

/*---------------------------------------+
 | instr_a | instr_b | instr_c | instr_d |
 +---------------------------------------+
 | 2       | 2       | 10      | 10      |
 +---------------------------------------*/
```

```googlesql
SELECT
  REGEXP_INSTR('a@cd-ef b@cd-ef c@cd-ef', '@[^-]*', 1, 1) AS instr_a,
  REGEXP_INSTR('a@cd-ef b@cd-ef c@cd-ef', '@[^-]*', 1, 2) AS instr_b,
  REGEXP_INSTR('a@cd-ef b@cd-ef c@cd-ef', '@[^-]*', 1, 3) AS instr_c

/*-----------------------------+
 | instr_a | instr_b | instr_c |
 +-----------------------------+
 | 2       | 10      | 18      |
 +-----------------------------*/
```

```googlesql
SELECT
  REGEXP_INSTR('a@cd-ef', '@[^-]*', 1, 1, 0) AS instr_a,
  REGEXP_INSTR('a@cd-ef', '@[^-]*', 1, 1, 1) AS instr_b

/*-------------------+
 | instr_a | instr_b |
 +-------------------+
 | 2       | 5       |
 +-------------------*/
```

## `REGEXP_MATCH` (Deprecated) 
<a id="regexp_match"></a>

```googlesql
REGEXP_MATCH(value, regexp)
```

**Description**

Returns `TRUE` if `value` is a full match for the regular expression, `regexp`.

If the `regexp` argument is invalid, the function returns an error.

This function is deprecated. When possible, use
[`REGEXP_CONTAINS`][regexp-contains] to find a partial match for a
regular expression.

Note: GoogleSQL provides regular expression support using the
[re2][string-link-to-re2] library; see that documentation for its
regular expression syntax.

**Return type**

`BOOL`

**Examples**

```googlesql
WITH email_addresses AS
  (SELECT 'foo@example.com' as email
  UNION ALL
  SELECT 'bar@example.org' as email
  UNION ALL
  SELECT 'notavalidemailaddress' as email)

SELECT
  email,
  REGEXP_MATCH(email,
               r'[a-zA-Z0-9_.+-]+@[a-zA-Z0-9-]+\.[a-zA-Z0-9-.]+')
               AS valid_email_address
FROM email_addresses;

/*-----------------------+---------------------+
 | email                 | valid_email_address |
 +-----------------------+---------------------+
 | foo@example.com       | true                |
 | bar@example.org       | true                |
 | notavalidemailaddress | false               |
 +-----------------------+---------------------*/
```

[string-link-to-re2]: https://github.com/google/re2/wiki/Syntax

[regexp-contains]: https://github.com/google/googlesql/blob/master/docs/string_functions.md#regexp_contains

## `REGEXP_REPLACE`

```googlesql
REGEXP_REPLACE(value, regexp, replacement)
```

**Description**

Returns a `STRING` where all substrings of `value` that
match regular expression `regexp` are replaced with `replacement`.

You can use backslashed-escaped digits (\1 to \9) within the `replacement`
argument to insert text matching the corresponding parenthesized group in the
`regexp` pattern. Use \0 to refer to the entire matching text.

To add a backslash in your regular expression, you must first escape it. For
example, `SELECT REGEXP_REPLACE('abc', 'b(.)', 'X\\1');` returns `aXc`. You can
also use [raw strings][string-link-to-lexical-literals] to remove one layer of
escaping, for example `SELECT REGEXP_REPLACE('abc', 'b(.)', r'X\1');`.

The `REGEXP_REPLACE` function only replaces non-overlapping matches. For
example, replacing `ana` within `banana` results in only one replacement, not
two.

If the `regexp` argument isn't a valid regular expression, this function
returns an error.

Note: GoogleSQL provides regular expression support using the
[re2][string-link-to-re2] library; see that documentation for its
regular expression syntax.

**Return type**

`STRING` or `BYTES`

**Examples**

```googlesql
SELECT REGEXP_REPLACE('# Heading', r'^# ([a-zA-Z0-9\s]+$)', '<h1>\\1</h1>') AS html

/*--------------------------+
 | html                     |
 +--------------------------+
 | <h1>Heading</h1>         |
 +--------------------------*/
```

[string-link-to-re2]: https://github.com/google/re2/wiki/Syntax

[string-link-to-lexical-literals]: https://github.com/google/googlesql/blob/master/docs/lexical.md#string_and_bytes_literals

## `REGEXP_SUBSTR`

```googlesql
REGEXP_SUBSTR(value, regexp[, position[, occurrence]])
```

**Description**

Synonym for [REGEXP_EXTRACT][string-link-to-regex].

**Return type**

`STRING` or `BYTES`

**Examples**

```googlesql
WITH example AS
(SELECT 'Hello World Helloo' AS value, 'H?ello+' AS regex, 1 AS position, 1 AS
occurrence
)
SELECT value, regex, position, occurrence, REGEXP_SUBSTR(value, regex,
position, occurrence) AS regexp_value FROM example;

/*--------------------+---------+----------+------------+--------------+
 | value              | regex   | position | occurrence | regexp_value |
 +--------------------+---------+----------+------------+--------------+
 | Hello World Helloo | H?ello+ | 1        | 1          | Hello        |
 +--------------------+---------+----------+------------+--------------*/
```

[string-link-to-regex]: #regexp_extract

## `REPEAT`

```googlesql
REPEAT(original_value, repetitions)
```

**Description**

Returns a `STRING` or `BYTES` value that consists of `original_value`, repeated.
The `repetitions` parameter specifies the number of times to repeat
`original_value`. Returns `NULL` if either `original_value` or `repetitions`
are `NULL`.

This function returns an error if the `repetitions` value is negative.

**Return type**

`STRING` or `BYTES`

**Examples**

```googlesql
SELECT REPEAT('abc', 3) AS results

/*-----------+
 | results   |
 |-----------|
 | abcabcabc |
 +-----------*/
```

```googlesql
SELECT REPEAT('abc', NULL) AS results

/*---------+
 | results |
 |---------|
 | NULL    |
 +---------*/
```

```googlesql
SELECT REPEAT(NULL, 3) AS results

/*---------+
 | results |
 |---------|
 | NULL    |
 +---------*/
```

## `REPLACE`

```googlesql
REPLACE(original_value, from_pattern, to_pattern)
```

**Description**

Replaces all occurrences of `from_pattern` with `to_pattern` in
`original_value`. If `from_pattern` is empty, no replacement is made.

This function supports specifying [collation][collation].

[collation]: https://github.com/google/googlesql/blob/master/docs/collation-concepts.md

**Return type**

`STRING` or `BYTES`

**Examples**

```googlesql
WITH desserts AS
  (SELECT 'apple pie' as dessert
  UNION ALL
  SELECT 'blackberry pie' as dessert
  UNION ALL
  SELECT 'cherry pie' as dessert)

SELECT
  REPLACE (dessert, 'pie', 'cobbler') as example
FROM desserts;

/*--------------------+
 | example            |
 +--------------------+
 | apple cobbler      |
 | blackberry cobbler |
 | cherry cobbler     |
 +--------------------*/
```

## `REVERSE`

```googlesql
REVERSE(value)
```

**Description**

Returns the reverse of the input `STRING` or `BYTES`.

**Return type**

`STRING` or `BYTES`

**Examples**

```googlesql
SELECT REVERSE('abc') AS results

/*---------+
 | results |
 +---------+
 | cba     |
 +---------*/
```

```googlesql
SELECT FORMAT('%T', REVERSE(b'1a3')) AS results

/*---------+
 | results |
 +---------+
 | b"3a1"  |
 +---------*/
```

## `RIGHT`

```googlesql
RIGHT(value, length)
```

**Description**

Returns a `STRING` or `BYTES` value that consists of the specified
number of rightmost characters or bytes from `value`. The `length` is an
`INT64` that specifies the length of the returned
value. If `value` is `BYTES`, `length` is the number of rightmost bytes to
return. If `value` is `STRING`, `length` is the number of rightmost characters
to return.

If `length` is 0, an empty `STRING` or `BYTES` value will be
returned. If `length` is negative, an error will be returned. If `length`
exceeds the number of characters or bytes from `value`, the original `value`
will be returned.

**Return type**

`STRING` or `BYTES`

**Examples**

```googlesql
SELECT 'apple' AS example, RIGHT('apple', 3) AS right_example

/*---------+---------------+
 | example | right_example |
 +---------+---------------+
 | apple   | ple           |
 +---------+---------------*/
```

```googlesql
SELECT b'apple' AS example, RIGHT(b'apple', 3) AS right_example

/*----------------------+---------------+
 | example              | right_example |
 +----------------------+---------------+
 | apple                | ple           |
 +----------------------+---------------*
```

## `RPAD`

```googlesql
RPAD(original_value, return_length[, pattern])
```

**Description**

Returns a `STRING` or `BYTES` value that consists of `original_value` appended
with `pattern`. The `return_length` parameter is an
`INT64` that specifies the length of the
returned value. If `original_value` is `BYTES`,
`return_length` is the number of bytes. If `original_value` is `STRING`,
`return_length` is the number of characters.

The default value of `pattern` is a blank space.

Both `original_value` and `pattern` must be the same data type.

If `return_length` is less than or equal to the `original_value` length, this
function returns the `original_value` value, truncated to the value of
`return_length`. For example, `RPAD('hello world', 7);` returns `'hello w'`.

If `original_value`, `return_length`, or `pattern` is `NULL`, this function
returns `NULL`.

This function returns an error if:

+ `return_length` is negative
+ `pattern` is empty

**Return type**

`STRING` or `BYTES`

**Examples**

```googlesql
SELECT FORMAT('%T', RPAD('c', 5)) AS results

/*---------+
 | results |
 +---------+
 | "c    " |
 +---------*/
```

```googlesql
SELECT RPAD('b', 5, 'a') AS results

/*---------+
 | results |
 +---------+
 | baaaa   |
 +---------*/
```

```googlesql
SELECT RPAD('abc', 10, 'ghd') AS results

/*------------+
 | results    |
 +------------+
 | abcghdghdg |
 +------------*/
```

```googlesql
SELECT RPAD('abc', 2, 'd') AS results

/*---------+
 | results |
 +---------+
 | ab      |
 +---------*/
```

```googlesql
SELECT FORMAT('%T', RPAD(b'abc', 10, b'ghd')) AS results

/*---------------+
 | results       |
 +---------------+
 | b"abcghdghdg" |
 +---------------*/
```

## `RTRIM`

```googlesql
RTRIM(value1[, value2])
```

**Description**

Identical to [TRIM][string-link-to-trim], but only removes trailing characters.

**Return type**

`STRING` or `BYTES`

**Examples**

```googlesql
SELECT RTRIM('***apple***', '*') AS example

/*-----------+
 | example   |
 +-----------+
 | ***apple  |
 +-----------*/
```

```googlesql
SELECT RTRIM('applexxz', 'xyz') AS example

/*---------+
 | example |
 +---------+
 | apple   |
 +---------*/
```

[string-link-to-trim]: #trim

## `SAFE_CONVERT_BYTES_TO_STRING`

```googlesql
SAFE_CONVERT_BYTES_TO_STRING(value)
```

**Description**

Converts a sequence of `BYTES` to a `STRING`. Any invalid UTF-8 characters are
replaced with the Unicode replacement character, `U+FFFD`.

**Return type**

`STRING`

**Examples**

The following statement returns the Unicode replacement character, &#65533;.

```googlesql
SELECT SAFE_CONVERT_BYTES_TO_STRING(b'\xc2') as safe_convert;
```

## `SOUNDEX`

```googlesql
SOUNDEX(value)
```

**Description**

Returns a `STRING` that represents the
[Soundex][string-link-to-soundex-wikipedia] code for `value`.

SOUNDEX produces a phonetic representation of a string. It indexes words by
sound, as pronounced in English. It's typically used to help determine whether
two strings, such as the family names _Levine_ and _Lavine_, or the words _to_
and _too_, have similar English-language pronunciation.

The result of the SOUNDEX consists of a letter followed by 3 digits. Non-latin
characters are ignored. If the remaining string is empty after removing
non-Latin characters, an empty `STRING` is returned.

**Return type**

`STRING`

**Examples**

```googlesql
SELECT 'Ashcraft' AS value, SOUNDEX('Ashcraft') AS soundex

/*----------------------+---------+
 | value                | soundex |
 +----------------------+---------+
 | Ashcraft             | A261    |
 +----------------------+---------*/
```

[string-link-to-soundex-wikipedia]: https://en.wikipedia.org/wiki/Soundex

## `SPLIT`

```googlesql
SPLIT(value[, delimiter])
```

**Description**

Splits a `STRING` or `BYTES` value, using a delimiter. The `delimiter` argument
must be a literal character or sequence of characters. You can't split with a
regular expression.

For `STRING`, the default delimiter is the comma `,`.

For `BYTES`, you must specify a delimiter.

Splitting on an empty delimiter produces an array of UTF-8 characters for
`STRING` values, and an array of `BYTES` for `BYTES` values.

Splitting an empty `STRING` returns an
`ARRAY` with a single empty
`STRING`.

This function supports specifying [collation][collation].

[collation]: https://github.com/google/googlesql/blob/master/docs/collation-concepts.md

**Return type**

`ARRAY<STRING>` or `ARRAY<BYTES>`

**Examples**

```googlesql
WITH letters AS
  (SELECT '' as letter_group
  UNION ALL
  SELECT 'a' as letter_group
  UNION ALL
  SELECT 'b c d' as letter_group)

SELECT SPLIT(letter_group, ' ') as example
FROM letters;

/*----------------------+
 | example              |
 +----------------------+
 | []                   |
 | [a]                  |
 | [b, c, d]            |
 +----------------------*/
```

## `SPLIT_SUBSTR`

```googlesql
SPLIT_SUBSTR(value, delimiter, start_split[, count])
```

**Description**

Returns a substring from an input `STRING` that's determined by a delimiter, a
location that indicates the first split of the substring to return, and the
number of splits to include in the returned substring.

The `value` argument is the supplied `STRING` value from which a substring is
returned.

The `delimiter` argument is the delimiter used to split the input `STRING`. It
must be a literal character or sequence of characters.

+ The `delimiter` argument can't be a regular expression.
+ Delimiter matching is from left to right.
+ If the delimiter is a sequence of characters, then two instances of the
  delimiter in the input string can't overlap. For example, if the delimiter is
  `**`, then the delimiters in the string `aa***bb***cc` are:
    + The first two asterisks after `aa`.
    + The first two asterisks after `bb`.

The `start_split` argument is an integer that specifies the first split of the
substring to return.

+ If `start_split` is `1`, then the returned substring starts from the first
  split.
+ If `start_split` is `0` or less than the negative of the number of splits,
  then `start_split` is treated as if it's `1` and returns a substring that
  starts with the first split.
+ If `start_split` is greater than the number of splits, then an empty string is
  returned.
+ If `start_split` is negative, then the splits are counted from the end of the
  input string. If `start_split` is `-1`, then the last split in the input
  string is returned.

The optional `count` argument is an integer that specifies the maximum number
of splits to include in the returned substring.

+ If `count` isn't specified, then the substring from the `start_split`
  position to the end of the input string is returned.
+ If `count` is `0`, an empty string is returned.
+ If `count` is negative, an error is returned.
+ If the sum of `count` plus `start_split` is greater than the number of splits,
  then a substring from `start_split` to the end of the input string is
  returned.

This function supports specifying [collation][collation].

[collation]: https://github.com/google/googlesql/blob/master/docs/collation-concepts.md

**Return type**

`STRING`

**Examples**

The following example returns an empty string because `count` is `0`:

```googlesql
SELECT SPLIT_SUBSTR("www.abc.xyz.com", ".", 1, 0) AS example

/*---------+
 | example |
 +---------+
 |         |
 +---------*/
```

The following example returns two splits starting with the first split:

```googlesql
SELECT SPLIT_SUBSTR("www.abc.xyz.com", ".", 1, 2) AS example

/*---------+
 | example |
 +---------+
 | www.abc |
 +---------*/
```

The following example returns one split starting with the first split:

```googlesql
SELECT SPLIT_SUBSTR("www.abc.xyz.com", ".", 1, 1) AS example

/*---------+
 | example |
 +---------+
 | www     |
 +---------*/
```

The following example returns splits from the right because `start_split` is a
negative value:

```googlesql
SELECT SPLIT_SUBSTR("www.abc.xyz.com", ".", -1, 1) AS example

/*---------+
 | example |
 +---------+
 | com     |
 +---------*/
```

The following example returns a substring with three splits, starting with the
first split:

```googlesql
SELECT SPLIT_SUBSTR("www.abc.xyz.com", ".", 1, 3) AS example

/*-------------+
 | example     |
 +-------------+
 | www.abc.xyz |
 +------------*/
```

If `start_split` is zero, then it's treated as if it's `1`. The following
example returns three substrings starting with the first split:

```googlesql
SELECT SPLIT_SUBSTR("www.abc.xyz.com", ".", 0, 3) AS example

/*-------------+
 | example     |
 +-------------+
 | www.abc.xyz |
 +------------*/
```

If `start_split` is greater than the number of splits, then an empty string is
returned:

```googlesql
SELECT SPLIT_SUBSTR("www.abc.xyz.com", ".", 5, 3) AS example

/*---------+
 | example |
 +---------+
 |         |
 +--------*/
```

In the following example, the `start_split` value (`-5`) is less than the
negative of the number of splits (`-4`), so `start_split` is treated as `1`:

```googlesql
SELECT SPLIT_SUBSTR("www.abc.xyz.com", ".", -5, 3) AS example

/*-------------+
 | example     |
 +-------------+
 | www.abc.xyz |
 +------------*/
```

In the following example, the substring from `start_split` to the end of the
string is returned because `count` isn't specified:

```googlesql
SELECT SPLIT_SUBSTR("www.abc.xyz.com", ".", 3) AS example

/*---------+
 | example |
 +---------+
 | xyz.com |
 +--------*/
```

The following two examples demonstrate how `SPLIT_SUBSTR` works with a
multi-character delimiter that has overlapping matches in the input string. In
each example, the input string contains instances of three asterisks in a row
(`***`) and the delimiter is two asterisks (`**`).

```googlesql
SELECT SPLIT_SUBSTR('aaa***bbb***ccc', '**', 1, 2) AS example

/*-----------+
 | example   |
 +-----------+
 | aaa***bbb |
 +----------*/
```

```googlesql
SELECT SPLIT_SUBSTR('aaa***bbb***ccc', '**', 2, 2) AS example

/*------------+
 | example    |
 +------------+
 | *bbb***ccc |
 +-----------*/
```

## `STARTS_WITH`

```googlesql
STARTS_WITH(value, prefix)
```

**Description**

Takes two `STRING` or `BYTES` values. Returns `TRUE` if `prefix` is a
prefix of `value`.

This function supports specifying [collation][collation].

[collation]: https://github.com/google/googlesql/blob/master/docs/collation-concepts.md

**Return type**

`BOOL`

**Examples**

```googlesql
SELECT STARTS_WITH('bar', 'b') AS example

/*---------+
 | example |
 +---------+
 |    True |
 +---------*/
```

## `STRPOS`

```googlesql
STRPOS(value, subvalue)
```

**Description**

Takes two `STRING` or `BYTES` values. Returns the 1-based position of the first
occurrence of `subvalue` inside `value`. Returns `0` if `subvalue` isn't found.

This function supports specifying [collation][collation].

[collation]: https://github.com/google/googlesql/blob/master/docs/collation-concepts.md

**Return type**

`INT64`

**Examples**

```googlesql
SELECT STRPOS('foo@example.com', '@') AS example

/*---------+
 | example |
 +---------+
 |       4 |
 +---------*/
```

## `SUBSTR`

```googlesql
SUBSTR(value, position[, length])
```

**Description**

Gets a portion (substring) of the supplied `STRING` or `BYTES` value.

The `position` argument is an integer specifying the starting position of the
substring.

+ If `position` is `1`, the substring starts from the first character or byte.
+ If `position` is `0` or less than `-LENGTH(value)`, `position` is set to `1`,
  and the substring starts from the first character or byte.
+ If `position` is greater than the length of `value`, the function produces
  an empty substring.
+ If `position` is negative, the function counts from the end of `value`,
  with `-1` indicating the last character or byte.

The `length` argument specifies the maximum number of characters or bytes to
return.

+ If `length` isn't specified, the function produces a substring that starts
  at the specified position and ends at the last character or byte of `value`.
+ If `length` is `0`, the function produces an empty substring.
+ If `length` is negative, the function produces an error.
+ The returned substring may be shorter than `length`, for example, when
  `length` exceeds the length of `value`, or when the starting position of the
  substring plus `length` is greater than the length of `value`.

**Return type**

`STRING` or `BYTES`

**Examples**

```googlesql
SELECT SUBSTR('apple', 2) AS example

/*---------+
 | example |
 +---------+
 | pple    |
 +---------*/
```

```googlesql
SELECT SUBSTR('apple', 2, 2) AS example

/*---------+
 | example |
 +---------+
 | pp      |
 +---------*/
```

```googlesql
SELECT SUBSTR('apple', -2) AS example

/*---------+
 | example |
 +---------+
 | le      |
 +---------*/
```

```googlesql
SELECT SUBSTR('apple', 1, 123) AS example

/*---------+
 | example |
 +---------+
 | apple   |
 +---------*/
```

```googlesql
SELECT SUBSTR('apple', 123) AS example

/*---------+
 | example |
 +---------+
 |         |
 +---------*/
```

```googlesql
SELECT SUBSTR('apple', 123, 5) AS example

/*---------+
 | example |
 +---------+
 |         |
 +---------*/
```

## `SUBSTRING`

```googlesql
SUBSTRING(value, position[, length])
```

Alias for [`SUBSTR`][substr].

[substr]: #substr

## `TO_BASE32`

```googlesql
TO_BASE32(bytes_expr)
```

**Description**

Converts a sequence of `BYTES` into a base32-encoded `STRING`. To convert a
base32-encoded `STRING` into `BYTES`, use [FROM_BASE32][string-link-to-from-base32].

**Return type**

`STRING`

**Example**

```googlesql
SELECT TO_BASE32(b'abcde\xFF') AS base32_string;

/*------------------+
 | base32_string    |
 +------------------+
 | MFRGGZDF74====== |
 +------------------*/
```

[string-link-to-from-base32]: #from_base32

## `TO_BASE64`

```googlesql
TO_BASE64(bytes_expr)
```

**Description**

Converts a sequence of `BYTES` into a base64-encoded `STRING`. To convert a
base64-encoded `STRING` into `BYTES`, use [FROM_BASE64][string-link-to-from-base64].

There are several base64 encodings in common use that vary in exactly which
alphabet of 65 ASCII characters are used to encode the 64 digits and padding.
See [RFC 4648][RFC-4648] for details. This
function adds padding and uses the alphabet `[A-Za-z0-9+/=]`.

**Return type**

`STRING`

**Example**

```googlesql
SELECT TO_BASE64(b'\377\340') AS base64_string;

/*---------------+
 | base64_string |
 +---------------+
 | /+A=          |
 +---------------*/
```

To work with an encoding using a different base64 alphabet, you might need to
compose `TO_BASE64` with the `REPLACE` function. For instance, the
`base64url` url-safe and filename-safe encoding commonly used in web programming
uses `-_=` as the last characters rather than `+/=`. To encode a
`base64url`-encoded string, replace `+` and `/` with `-` and `_` respectively.

```googlesql
SELECT REPLACE(REPLACE(TO_BASE64(b'\377\340'), '+', '-'), '/', '_') as websafe_base64;

/*----------------+
 | websafe_base64 |
 +----------------+
 | _-A=           |
 +----------------*/
```

[string-link-to-from-base64]: #from_base64

[RFC-4648]: https://tools.ietf.org/html/rfc4648#section-4

## `TO_CODE_POINTS`

```googlesql
TO_CODE_POINTS(value)
```

**Description**

Takes a `STRING` or `BYTES` value and returns an array of `INT64` values that
represent code points or extended ASCII character values.

+ If `value` is a `STRING`, each element in the returned array represents a
  [code point][string-link-to-code-points-wikipedia]. Each code point falls
  within the range of [0, 0xD7FF] and [0xE000, 0x10FFFF].
+ If `value` is `BYTES`, each element in the array is an extended ASCII
  character value in the range of [0, 255].

To convert from an array of code points to a `STRING` or `BYTES`, see
[CODE_POINTS_TO_STRING][string-link-to-codepoints-to-string] or
[CODE_POINTS_TO_BYTES][string-link-to-codepoints-to-bytes].

**Return type**

`ARRAY<INT64>`

**Examples**

The following examples get the code points for each element in an array of
words.

```googlesql
SELECT
  'foo' AS word,
  TO_CODE_POINTS('foo') AS code_points

/*---------+------------------------------------+
 | word    | code_points                        |
 +---------+------------------------------------+
 | foo     | [102, 111, 111]                    |
 +---------+------------------------------------*/
```

```googlesql
SELECT
  'bar' AS word,
  TO_CODE_POINTS('bar') AS code_points

/*---------+------------------------------------+
 | word    | code_points                        |
 +---------+------------------------------------+
 | bar     | [98, 97, 114]                      |
 +---------+------------------------------------*/
```

```googlesql
SELECT
  'baz' AS word,
  TO_CODE_POINTS('baz') AS code_points

/*---------+------------------------------------+
 | word    | code_points                        |
 +---------+------------------------------------+
 | baz     | [98, 97, 122]                      |
 +---------+------------------------------------*/
```

```googlesql
SELECT
  'giraffe' AS word,
  TO_CODE_POINTS('giraffe') AS code_points

/*---------+------------------------------------+
 | word    | code_points                        |
 +---------+------------------------------------+
 | giraffe | [103, 105, 114, 97, 102, 102, 101] |
 +---------+------------------------------------*/
```

```googlesql
SELECT
  'llama' AS word,
  TO_CODE_POINTS('llama') AS code_points

/*---------+------------------------------------+
 | word    | code_points                        |
 +---------+------------------------------------+
 | llama   | [108, 108, 97, 109, 97]            |
 +---------+------------------------------------*/
```

The following examples convert integer representations of `BYTES` to their
corresponding ASCII character values.

```googlesql
SELECT
  b'\x66\x6f\x6f' AS bytes_value,
  TO_CODE_POINTS(b'\x66\x6f\x6f') AS bytes_value_as_integer

/*------------------+------------------------+
 | bytes_value      | bytes_value_as_integer |
 +------------------+------------------------+
 | foo              | [102, 111, 111]        |
 +------------------+------------------------*/
```

```googlesql
SELECT
  b'\x00\x01\x10\xff' AS bytes_value,
  TO_CODE_POINTS(b'\x00\x01\x10\xff') AS bytes_value_as_integer

/*------------------+------------------------+
 | bytes_value      | bytes_value_as_integer |
 +------------------+------------------------+
 | \x00\x01\x10\xff | [0, 1, 16, 255]        |
 +------------------+------------------------*/
```

The following example demonstrates the difference between a `BYTES` result and a
`STRING` result. Notice that the character `Ā` is represented as a two-byte
Unicode sequence. As a result, the `BYTES` version of `TO_CODE_POINTS` returns
an array with two elements, while the `STRING` version returns an array with a
single element.

```googlesql
SELECT TO_CODE_POINTS(b'Ā') AS b_result, TO_CODE_POINTS('Ā') AS s_result;

/*------------+----------+
 | b_result   | s_result |
 +------------+----------+
 | [196, 128] | [256]    |
 +------------+----------*/
```

[string-link-to-code-points-wikipedia]: https://en.wikipedia.org/wiki/Code_point

[string-link-to-codepoints-to-string]: #code_points_to_string

[string-link-to-codepoints-to-bytes]: #code_points_to_bytes

## `TO_HEX`

```googlesql
TO_HEX(bytes)
```

**Description**

Converts a sequence of `BYTES` into a hexadecimal `STRING`. Converts each byte
in the `STRING` as two hexadecimal characters in the range
`(0..9, a..f)`. To convert a hexadecimal-encoded
`STRING` to `BYTES`, use [FROM_HEX][string-link-to-from-hex].

**Return type**

`STRING`

**Example**

```googlesql
SELECT
  b'\x00\x01\x02\x03\xAA\xEE\xEF\xFF' AS byte_string,
  TO_HEX(b'\x00\x01\x02\x03\xAA\xEE\xEF\xFF') AS hex_string

/*----------------------------------+------------------+
 | byte_string                      | hex_string       |
 +----------------------------------+------------------+
 | \x00\x01\x02\x03\xaa\xee\xef\xff | 00010203aaeeefff |
 +----------------------------------+------------------*/
```

[string-link-to-from-hex]: #from_hex

## `TRANSLATE`

```googlesql
TRANSLATE(expression, source_characters, target_characters)
```

**Description**

In `expression`, replaces each character in `source_characters` with the
corresponding character in `target_characters`. All inputs must be the same
type, either `STRING` or `BYTES`.

+ Each character in `expression` is translated at most once.
+ A character in `expression` that isn't present in `source_characters` is left
  unchanged in `expression`.
+ A character in `source_characters` without a corresponding character in
  `target_characters` is omitted from the result.
+ A duplicate character in `source_characters` results in an error.

**Return type**

`STRING` or `BYTES`

**Examples**

```googlesql
SELECT TRANSLATE('This is a cookie', 'sco', 'zku') AS translate

/*------------------+
 | translate        |
 +------------------+
 | Thiz iz a kuukie |
 +------------------*/
```

## `TRIM`

```googlesql
TRIM(value_to_trim[, set_of_characters_to_remove])
```

**Description**

Takes a `STRING` or `BYTES` value to trim.

If the value to trim is a `STRING`, removes from this value all leading and
trailing Unicode code points in `set_of_characters_to_remove`.
The set of code points is optional. If it isn't specified, all
whitespace characters are removed from the beginning and end of the
value to trim.

If the value to trim is `BYTES`, removes from this value all leading and
trailing bytes in `set_of_characters_to_remove`. The set of bytes is required.

**Return type**

+ `STRING` if `value_to_trim` is a `STRING` value.
+ `BYTES` if `value_to_trim` is a `BYTES` value.

**Examples**

In the following example, all leading and trailing whitespace characters are
removed from `item` because `set_of_characters_to_remove` isn't specified.

```googlesql
SELECT CONCAT('#', TRIM( '   apple   '), '#') AS example

/*----------+
 | example  |
 +----------+
 | #apple#  |
 +----------*/
```

In the following example, all leading and trailing `*` characters are removed
from '***apple***'.

```googlesql
SELECT TRIM('***apple***', '*') AS example

/*---------+
 | example |
 +---------+
 | apple   |
 +---------*/
```

In the following example, all leading and trailing `x`, `y`, and `z` characters
are removed from 'xzxapplexxy'.

```googlesql
SELECT TRIM('xzxapplexxy', 'xyz') as example

/*---------+
 | example |
 +---------+
 | apple   |
 +---------*/
```

In the following example, examine how `TRIM` interprets characters as
Unicode code-points. If your trailing character set contains a combining
diacritic mark over a particular letter, `TRIM` might strip the
same diacritic mark from a different letter.

```googlesql
SELECT
  TRIM('abaW̊', 'Y̊') AS a,
  TRIM('W̊aba', 'Y̊') AS b,
  TRIM('abaŪ̊', 'Y̊') AS c,
  TRIM('Ū̊aba', 'Y̊') AS d

/*------+------+------+------+
 | a    | b    | c    | d    |
 +------+------+------+------+
 | abaW | W̊aba | abaŪ | Ūaba |
 +------+------+------+------*/
```

In the following example, all leading and trailing `b'n'`, `b'a'`, `b'\xab'`
bytes are removed from `item`.

```googlesql
SELECT b'apple', TRIM(b'apple', b'na\xab') AS example

/*----------------------+------------------+
 | item                 | example          |
 +----------------------+------------------+
 | apple                | pple             |
 +----------------------+------------------*/
```

## `UNICODE`

```googlesql
UNICODE(value)
```

**Description**

Returns the Unicode [code point][string-code-point] for the first character in
`value`. Returns `0` if `value` is empty, or if the resulting Unicode code
point is `0`.

**Return type**

`INT64`

**Examples**

```googlesql
SELECT UNICODE('âbcd') as A, UNICODE('â') as B, UNICODE('') as C, UNICODE(NULL) as D;

/*-------+-------+-------+-------+
 | A     | B     | C     | D     |
 +-------+-------+-------+-------+
 | 226   | 226   | 0     | NULL  |
 +-------+-------+-------+-------*/
```

[string-code-point]: https://en.wikipedia.org/wiki/Code_point

## `UPPER`

```googlesql
UPPER(value)
```

**Description**

For `STRING` arguments, returns the original string with all alphabetic
characters in uppercase. Mapping between uppercase and lowercase is done
according to the
[Unicode Character Database][string-link-to-unicode-character-definitions]
without taking into account language-specific mappings.

For `BYTES` arguments, the argument is treated as ASCII text, with all bytes
greater than 127 left intact.

**Return type**

`STRING` or `BYTES`

**Examples**

```googlesql
SELECT UPPER('foo') AS example

/*---------+
 | example |
 +---------+
 | FOO     |
 +---------*/
```

[string-link-to-unicode-character-definitions]: http://unicode.org/ucd/

[string-link-to-strpos]: #strpos

