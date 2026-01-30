

<!-- mdlint off(WHITESPACE_LINE_LENGTH) -->

# Conversion functions

GoogleSQL supports conversion functions. These data type
conversions are explicit, but some conversions can happen implicitly. You can
learn more about implicit and explicit conversion [here][conversion-rules].

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
  <td><a href="https://github.com/google/googlesql/blob/master/docs/bit_functions.md#bit_cast_to_int32"><code>BIT_CAST_TO_INT32</code></a>
</td>
  <td>
    Cast bits to an <code>INT32</code> value.
    <br>For more information, see <a href="https://github.com/google/googlesql/blob/master/docs/bit_functions.md">Bit functions</a>.

  </td>
</tr>

<tr>
  <td><a href="https://github.com/google/googlesql/blob/master/docs/bit_functions.md#bit_cast_to_int64"><code>BIT_CAST_TO_INT64</code></a>
</td>
  <td>
    Cast bits to an <code>INT64</code> value.
    <br>For more information, see <a href="https://github.com/google/googlesql/blob/master/docs/bit_functions.md">Bit functions</a>.

  </td>
</tr>

<tr>
  <td><a href="https://github.com/google/googlesql/blob/master/docs/bit_functions.md#bit_cast_to_uint32"><code>BIT_CAST_TO_UINT32</code></a>
</td>
  <td>
    Cast bits to an <code>UINT32</code> value.
    <br>For more information, see <a href="https://github.com/google/googlesql/blob/master/docs/bit_functions.md">Bit functions</a>.

  </td>
</tr>

<tr>
  <td><a href="https://github.com/google/googlesql/blob/master/docs/bit_functions.md#bit_cast_to_uint64"><code>BIT_CAST_TO_UINT64</code></a>
</td>
  <td>
    Cast bits to an <code>UINT64</code> value.
    <br>For more information, see <a href="https://github.com/google/googlesql/blob/master/docs/bit_functions.md">Bit functions</a>.

  </td>
</tr>

<tr>
  <td><a href="https://github.com/google/googlesql/blob/master/docs/json_functions.md#bool_for_json"><code>BOOL</code></a>
</td>
  <td>
    Converts a JSON boolean to a SQL <code>BOOL</code> value.
    <br>For more information, see <a href="https://github.com/google/googlesql/blob/master/docs/json_functions.md">JSON functions</a>.

  </td>
</tr>

<tr>
  <td><a href="https://github.com/google/googlesql/blob/master/docs/json_functions.md#bool_array_for_json"><code>BOOL_ARRAY</code></a>
</td>
  <td>
    Converts a JSON array of booleans to a
    SQL <code>ARRAY&lt;BOOL&gt;</code> value.
    <br>For more information, see <a href="https://github.com/google/googlesql/blob/master/docs/json_functions.md">JSON functions</a>.

  </td>
</tr>

<tr>
  <td><a href="https://github.com/google/googlesql/blob/master/docs/conversion_functions.md#cast"><code>CAST</code></a>
</td>
  <td>
    Convert the results of an expression to the given type.
  </td>
</tr>

<tr>
  <td><a href="https://github.com/google/googlesql/blob/master/docs/string_functions.md#chr"><code>CHR</code></a>
</td>
  <td>
    Converts a Unicode code point to a character.
    <br>For more information, see <a href="https://github.com/google/googlesql/blob/master/docs/string_functions.md">String functions</a>.

  </td>
</tr>

<tr>
  <td><a href="https://github.com/google/googlesql/blob/master/docs/string_functions.md#code_points_to_bytes"><code>CODE_POINTS_TO_BYTES</code></a>
</td>
  <td>
    Converts an array of extended ASCII code points to a
    <code>BYTES</code> value.
    <br>For more information, see <a href="https://github.com/google/googlesql/blob/master/docs/string_functions.md">String aggregate functions</a>.

  </td>
</tr>

<tr>
  <td><a href="https://github.com/google/googlesql/blob/master/docs/string_functions.md#code_points_to_string"><code>CODE_POINTS_TO_STRING</code></a>
</td>
  <td>
    Converts an array of extended ASCII code points to a
    <code>STRING</code> value.
    <br>For more information, see <a href="https://github.com/google/googlesql/blob/master/docs/string_functions.md">String aggregate functions</a>.

  </td>
</tr>

<tr>
  <td><a href="https://github.com/google/googlesql/blob/master/docs/date_functions.md#date_from_unix_date"><code>DATE_FROM_UNIX_DATE</code></a>
</td>
  <td>
    Interprets an <code>INT64</code> expression as the number of days
    since 1970-01-01.
    <br>For more information, see <a href="https://github.com/google/googlesql/blob/master/docs/date_functions.md">Date functions</a>.

  </td>
</tr>

<tr>
  <td><a href="https://github.com/google/googlesql/blob/master/docs/string_functions.md#from_base32"><code>FROM_BASE32</code></a>
</td>
  <td>
    Converts a base32-encoded <code>STRING</code> value into a
    <code>BYTES</code> value.
    <br>For more information, see <a href="https://github.com/google/googlesql/blob/master/docs/string_functions.md">String functions</a>.

  </td>
</tr>

<tr>
  <td><a href="https://github.com/google/googlesql/blob/master/docs/string_functions.md#from_base64"><code>FROM_BASE64</code></a>
</td>
  <td>
    Converts a base64-encoded <code>STRING</code> value into a
    <code>BYTES</code> value.
    <br>For more information, see <a href="https://github.com/google/googlesql/blob/master/docs/string_functions.md">String functions</a>.

  </td>
</tr>

<tr>
  <td><a href="https://github.com/google/googlesql/blob/master/docs/string_functions.md#from_hex"><code>FROM_HEX</code></a>
</td>
  <td>
    Converts a hexadecimal-encoded <code>STRING</code> value into a
    <code>BYTES</code> value.
    <br>For more information, see <a href="https://github.com/google/googlesql/blob/master/docs/string_functions.md">String functions</a>.

  </td>
</tr>

<tr>
  <td><a href="https://github.com/google/googlesql/blob/master/docs/protocol_buffer_functions.md#from_proto"><code>FROM_PROTO</code></a>
</td>
  <td>
    Converts a protocol buffer value into GoogleSQL value.
    <br>For more information, see <a href="https://github.com/google/googlesql/blob/master/docs/protocol_buffer_functions.md">Protocol buffer functions</a>.

  </td>
</tr>

<tr>
  <td><a href="https://github.com/google/googlesql/blob/master/docs/json_functions.md#int32_for_json"><code>INT32</code></a>
</td>
  <td>
    Converts a JSON number to a SQL <code>INT32</code> value.
    <br>For more information, see <a href="https://github.com/google/googlesql/blob/master/docs/json_functions.md">JSON functions</a>.

  </td>
</tr>

<tr>
  <td><a href="https://github.com/google/googlesql/blob/master/docs/json_functions.md#int32_array_for_json"><code>INT32_ARRAY</code></a>
</td>
  <td>
    Converts a JSON number to a SQL <code>ARRAY&lt;INT32&gt;</code> value.
    <br>For more information, see <a href="https://github.com/google/googlesql/blob/master/docs/json_functions.md">JSON functions</a>.

  </td>
</tr>

<tr>
  <td><a href="https://github.com/google/googlesql/blob/master/docs/json_functions.md#int64_for_json"><code>INT64</code></a>
</td>
  <td>
    Converts a JSON number to a SQL <code>INT64</code> value.
    <br>For more information, see <a href="https://github.com/google/googlesql/blob/master/docs/json_functions.md">JSON functions</a>.

  </td>
</tr>

<tr>
  <td><a href="https://github.com/google/googlesql/blob/master/docs/json_functions.md#int64_array_for_json"><code>INT64_ARRAY</code></a>
</td>
  <td>
    Converts a JSON array of numbers to a
    SQL <code>ARRAY&lt;INT64&gt;</code> value.
    <br>For more information, see <a href="https://github.com/google/googlesql/blob/master/docs/json_functions.md">JSON functions</a>.

  </td>
</tr>

<tr>
  <td><a href="https://github.com/google/googlesql/blob/master/docs/json_functions.md#lax_bool"><code>LAX_BOOL</code></a>
</td>
  <td>
    Attempts to convert a JSON value to a SQL <code>BOOL</code> value.
    <br>For more information, see <a href="https://github.com/google/googlesql/blob/master/docs/json_functions.md">JSON functions</a>.

  </td>
</tr>

<tr>
  <td><a href="https://github.com/google/googlesql/blob/master/docs/json_functions.md#lax_bool_array"><code>LAX_BOOL_ARRAY</code></a>
</td>
  <td>
    Attempts to convert a JSON value to a
    SQL <code>ARRAY&lt;BOOL&gt;</code> value.
    <br>For more information, see <a href="https://github.com/google/googlesql/blob/master/docs/json_functions.md">JSON functions</a>.

  </td>
</tr>

<tr>
  <td>
  
  <a href="https://github.com/google/googlesql/blob/master/docs/json_functions.md#lax_double"><code>LAX_DOUBLE</code></a>

  
  </td>
  <td>
    Attempts to convert a JSON value to a
    SQL <code>DOUBLE</code> value.
    <br>For more information, see <a href="https://github.com/google/googlesql/blob/master/docs/json_functions.md">JSON functions</a>.

  </td>
</tr>

<tr>
  <td>
    
      <a href="https://github.com/google/googlesql/blob/master/docs/json_functions.md#lax_double_array"><code>LAX_DOUBLE_ARRAY</code></a>

    
  </td>
  <td>
    Attempts to convert a JSON value to a
    SQL <code>ARRAY&lt;DOUBLE&gt;</code> value.
    <br>For more information, see <a href="https://github.com/google/googlesql/blob/master/docs/json_functions.md">JSON functions</a>.

  </td>
</tr>

<tr>
  <td>
    
      <a href="https://github.com/google/googlesql/blob/master/docs/json_functions.md#lax_float"><code>LAX_FLOAT</code></a>

    
  </td>
  <td>
    Attempts to convert a JSON value to a
    SQL <code>FLOAT</code> value.
    <br>For more information, see <a href="https://github.com/google/googlesql/blob/master/docs/json_functions.md">JSON functions</a>.

  </td>
</tr>

<tr>
  <td>
    
      <a href="https://github.com/google/googlesql/blob/master/docs/json_functions.md#lax_float_array"><code>LAX_FLOAT_ARRAY</code></a>

    
  </td>
  <td>
    Attempts to convert a JSON value to a
    SQL <code>ARRAY&gt;FLOAT&lt;</code> value.
    <br>For more information, see <a href="https://github.com/google/googlesql/blob/master/docs/json_functions.md">JSON functions</a>.

  </td>
</tr>

<tr>
  <td><a href="https://github.com/google/googlesql/blob/master/docs/json_functions.md#lax_int32"><code>LAX_INT32</code></a>
</td>
  <td>
    Attempts to convert a JSON value to a SQL <code>INT32</code> value.
    <br>For more information, see <a href="https://github.com/google/googlesql/blob/master/docs/json_functions.md">JSON functions</a>.

  </td>
</tr>

<tr>
  <td><a href="https://github.com/google/googlesql/blob/master/docs/json_functions.md#lax_int32_array"><code>LAX_INT32_ARRAY</code></a>
</td>
  <td>
    Attempts to convert a JSON value to a
    SQL <code>ARRAY&lt;INT32&gt;</code> value.
    <br>For more information, see <a href="https://github.com/google/googlesql/blob/master/docs/json_functions.md">JSON functions</a>.

  </td>
</tr>

<tr>
  <td><a href="https://github.com/google/googlesql/blob/master/docs/json_functions.md#lax_int64"><code>LAX_INT64</code></a>
</td>
  <td>
    Attempts to convert a JSON value to a SQL <code>INT64</code> value.
    <br>For more information, see <a href="https://github.com/google/googlesql/blob/master/docs/json_functions.md">JSON functions</a>.

  </td>
</tr>

<tr>
  <td><a href="https://github.com/google/googlesql/blob/master/docs/json_functions.md#lax_int64_array"><code>LAX_INT64_ARRAY</code></a>
</td>
  <td>
    Attempts to convert a JSON value to a
    SQL <code>ARRAY&lt;INT64&gt;</code> value.
    <br>For more information, see <a href="https://github.com/google/googlesql/blob/master/docs/json_functions.md">JSON functions</a>.

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
  <td><a href="https://github.com/google/googlesql/blob/master/docs/json_functions.md#lax_uint32"><code>LAX_UINT32</code></a>
</td>
  <td>
    Attempts to convert a JSON value to a SQL <code>UINT32</code> value.
    <br>For more information, see <a href="https://github.com/google/googlesql/blob/master/docs/json_functions.md">JSON functions</a>.

  </td>
</tr>

<tr>
  <td><a href="https://github.com/google/googlesql/blob/master/docs/json_functions.md#lax_uint64"><code>LAX_UINT64</code></a>
</td>
  <td>
    Attempts to convert a JSON value to a SQL <code>UINT64</code> value.
    <br>For more information, see <a href="https://github.com/google/googlesql/blob/master/docs/json_functions.md">JSON functions</a>.

  </td>
</tr>

<tr>
  <td><a href="https://github.com/google/googlesql/blob/master/docs/json_functions.md#lax_uint64_array"><code>LAX_UINT64_ARRAY</code></a>
</td>
  <td>
    Attempts to convert a JSON value to a
    SQL <code>ARRAY&lt;UINT64&gt;</code> value.
    <br>For more information, see <a href="https://github.com/google/googlesql/blob/master/docs/json_functions.md">JSON functions</a>.

  </td>
</tr>

<tr>
  <td><a href="https://github.com/google/googlesql/blob/master/docs/conversion_functions.md#parse_bignumeric"><code>PARSE_BIGNUMERIC</code></a>
</td>
  <td>
    Converts a <code>STRING</code> value to a <code>BIGNUMERIC</code> value.
  </td>
</tr>

<tr>
  <td><a href="https://github.com/google/googlesql/blob/master/docs/date_functions.md#parse_date"><code>PARSE_DATE</code></a>
</td>
  <td>
    Converts a <code>STRING</code> value to a <code>DATE</code> value.
    <br>For more information, see <a href="https://github.com/google/googlesql/blob/master/docs/date_functions.md">Date functions</a>.

  </td>
</tr>

<tr>
  <td><a href="https://github.com/google/googlesql/blob/master/docs/datetime_functions.md#parse_datetime"><code>PARSE_DATETIME</code></a>
</td>
  <td>
    Converts a <code>STRING</code> value to a <code>DATETIME</code> value.
    <br>For more information, see <a href="https://github.com/google/googlesql/blob/master/docs/datetime_functions.md">Datetime functions</a>.

  </td>
</tr>

<tr>
  <td><a href="https://github.com/google/googlesql/blob/master/docs/json_functions.md#parse_json"><code>PARSE_JSON</code></a>
</td>
  <td>
    Converts a JSON-formatted <code>STRING</code> value to a
    <code>JSON</code> value.
    <br>For more information, see <a href="https://github.com/google/googlesql/blob/master/docs/json_functions.md">JSON functions</a>.

  </td>
</tr>

<tr>
  <td><a href="https://github.com/google/googlesql/blob/master/docs/conversion_functions.md#parse_numeric"><code>PARSE_NUMERIC</code></a>
</td>
  <td>
    Converts a <code>STRING</code> value to a <code>NUMERIC</code> value.
  </td>
</tr>

<tr>
  <td><a href="https://github.com/google/googlesql/blob/master/docs/time_functions.md#parse_time"><code>PARSE_TIME</code></a>
</td>
  <td>
    Converts a <code>STRING</code> value to a <code>TIME</code> value.
    <br>For more information, see <a href="https://github.com/google/googlesql/blob/master/docs/time_functions.md">Time functions</a>.

  </td>
</tr>

<tr>
  <td><a href="https://github.com/google/googlesql/blob/master/docs/timestamp_functions.md#parse_timestamp"><code>PARSE_TIMESTAMP</code></a>
</td>
  <td>
    Converts a <code>STRING</code> value to a <code>TIMESTAMP</code> value.
    <br>For more information, see <a href="https://github.com/google/googlesql/blob/master/docs/timestamp_functions.md">Timestamp functions</a>.

  </td>
</tr>

<tr>
  <td><a href="https://github.com/google/googlesql/blob/master/docs/conversion_functions.md#safe_casting"><code>SAFE_CAST</code></a>
</td>
  <td>
    Similar to the <code>CAST</code> function, but returns <code>NULL</code>
    when a runtime error is produced.
  </td>
</tr>

<tr>
  <td><a href="https://github.com/google/googlesql/blob/master/docs/string_functions.md#safe_convert_bytes_to_string"><code>SAFE_CONVERT_BYTES_TO_STRING</code></a>
</td>
  <td>
    Converts a <code>BYTES</code> value to a <code>STRING</code> value and
    replace any invalid UTF-8 characters with the Unicode replacement character,
    <code>U+FFFD</code>.
    <br>For more information, see <a href="https://github.com/google/googlesql/blob/master/docs/string_functions.md">String functions</a>.

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
  <td><a href="https://github.com/google/googlesql/blob/master/docs/timestamp_functions.md#timestamp_micros"><code>TIMESTAMP_MICROS</code></a>
</td>
  <td>
    Converts the number of microseconds since
    1970-01-01 00:00:00 UTC to a <code>TIMESTAMP</code>.
    <br>For more information, see <a href="https://github.com/google/googlesql/blob/master/docs/timestamp_functions.md">Timestamp functions</a>.

  </td>
</tr>

<tr>
  <td><a href="https://github.com/google/googlesql/blob/master/docs/timestamp_functions.md#timestamp_millis"><code>TIMESTAMP_MILLIS</code></a>
</td>
  <td>
    Converts the number of milliseconds since
    1970-01-01 00:00:00 UTC to a <code>TIMESTAMP</code>.
    <br>For more information, see <a href="https://github.com/google/googlesql/blob/master/docs/timestamp_functions.md">Timestamp functions</a>.

  </td>
</tr>

<tr>
  <td><a href="https://github.com/google/googlesql/blob/master/docs/timestamp_functions.md#timestamp_seconds"><code>TIMESTAMP_SECONDS</code></a>
</td>
  <td>
    Converts the number of seconds since
    1970-01-01 00:00:00 UTC to a <code>TIMESTAMP</code>.
    <br>For more information, see <a href="https://github.com/google/googlesql/blob/master/docs/timestamp_functions.md">Timestamp functions</a>.

  </td>
</tr>

<tr>
  <td><a href="https://github.com/google/googlesql/blob/master/docs/string_functions.md#to_base32"><code>TO_BASE32</code></a>
</td>
  <td>
    Converts a <code>BYTES</code> value to a
    base32-encoded <code>STRING</code> value.
    <br>For more information, see <a href="https://github.com/google/googlesql/blob/master/docs/string_functions.md">String functions</a>.

  </td>
</tr>

<tr>
  <td><a href="https://github.com/google/googlesql/blob/master/docs/string_functions.md#to_base64"><code>TO_BASE64</code></a>
</td>
  <td>
    Converts a <code>BYTES</code> value to a
    base64-encoded <code>STRING</code> value.
    <br>For more information, see <a href="https://github.com/google/googlesql/blob/master/docs/string_functions.md">String functions</a>.

  </td>
</tr>

<tr>
  <td><a href="https://github.com/google/googlesql/blob/master/docs/string_functions.md#to_code_points"><code>TO_CODE_POINTS</code></a>
</td>
  <td>
    Converts a <code>STRING</code> or <code>BYTES</code> value into an array of
    extended ASCII code points.
    <br>For more information, see <a href="https://github.com/google/googlesql/blob/master/docs/string_functions.md">String functions</a>.

  </td>
</tr>

<tr>
  <td><a href="https://github.com/google/googlesql/blob/master/docs/string_functions.md#to_hex"><code>TO_HEX</code></a>
</td>
  <td>
    Converts a <code>BYTES</code> value to a
    hexadecimal <code>STRING</code> value.
    <br>For more information, see <a href="https://github.com/google/googlesql/blob/master/docs/string_functions.md">String functions</a>.

  </td>
</tr>

<tr>
  <td><a href="https://github.com/google/googlesql/blob/master/docs/json_functions.md#to_json"><code>TO_JSON</code></a>
</td>
  <td>
    Converts a SQL value to a JSON value.
    <br>For more information, see <a href="https://github.com/google/googlesql/blob/master/docs/json_functions.md">JSON functions</a>.

  </td>
</tr>

<tr>
  <td><a href="https://github.com/google/googlesql/blob/master/docs/json_functions.md#to_json_string"><code>TO_JSON_STRING</code></a>
</td>
  <td>
    Converts a SQL value to a JSON-formatted <code>STRING</code> value.
    <br>For more information, see <a href="https://github.com/google/googlesql/blob/master/docs/json_functions.md">JSON functions</a>.

  </td>
</tr>

<tr>
  <td><a href="https://github.com/google/googlesql/blob/master/docs/protocol_buffer_functions.md#to_proto"><code>TO_PROTO</code></a>
</td>
  <td>
    Converts a GoogleSQL value into a protocol buffer value.
    <br>For more information, see <a href="https://github.com/google/googlesql/blob/master/docs/protocol_buffer_functions.md">Protocol buffer functions</a>.

  </td>
</tr>

<tr>
  <td><a href="https://github.com/google/googlesql/blob/master/docs/json_functions.md#uint32_for_json"><code>UINT32</code></a>
</td>
  <td>
    Converts a JSON number to a SQL <code>UINT32</code> value.
    <br>For more information, see <a href="https://github.com/google/googlesql/blob/master/docs/json_functions.md">JSON functions</a>.

  </td>
</tr>

<tr>
  <td><a href="https://github.com/google/googlesql/blob/master/docs/json_functions.md#uint32_array_for_json"><code>UINT32_ARRAY</code></a>
</td>
  <td>
    Converts a JSON number to a
    SQL <code>ARRAY&lt;UINT32&gt;</code> value.
    <br>For more information, see <a href="https://github.com/google/googlesql/blob/master/docs/json_functions.md">JSON functions</a>.

  </td>
</tr>

<tr>
  <td><a href="https://github.com/google/googlesql/blob/master/docs/json_functions.md#uint64_for_json"><code>UINT64</code></a>
</td>
  <td>
    Converts a JSON number to a SQL <code>UINT64</code> value.
    <br>For more information, see <a href="https://github.com/google/googlesql/blob/master/docs/json_functions.md">JSON functions</a>.

  </td>
</tr>

<tr>
  <td><a href="https://github.com/google/googlesql/blob/master/docs/json_functions.md#uint64_array_for_json"><code>UINT64_ARRAY</code></a>
</td>
  <td>
    Converts a JSON number to a SQL <code>ARRAY&lt;UINT64&gt;</code> value.
    <br>For more information, see <a href="https://github.com/google/googlesql/blob/master/docs/json_functions.md">JSON functions</a>.

  </td>
</tr>

<tr>
  <td><a href="https://github.com/google/googlesql/blob/master/docs/date_functions.md#unix_date"><code>UNIX_DATE</code></a>
</td>
  <td>
    Converts a <code>DATE</code> value to the number of days since 1970-01-01.
    <br>For more information, see <a href="https://github.com/google/googlesql/blob/master/docs/date_functions.md">Date functions</a>.

  </td>
</tr>

<tr>
  <td><a href="https://github.com/google/googlesql/blob/master/docs/timestamp_functions.md#unix_micros"><code>UNIX_MICROS</code></a>
</td>
  <td>
    Converts a <code>TIMESTAMP</code> value to the number of microseconds since
    1970-01-01 00:00:00 UTC.
    <br>For more information, see <a href="https://github.com/google/googlesql/blob/master/docs/timestamp_functions.md">Timestamp functions</a>.

  </td>
</tr>

<tr>
  <td><a href="https://github.com/google/googlesql/blob/master/docs/timestamp_functions.md#unix_millis"><code>UNIX_MILLIS</code></a>
</td>
  <td>
    Converts a <code>TIMESTAMP</code> value to the number of milliseconds
    since 1970-01-01 00:00:00 UTC.
    <br>For more information, see <a href="https://github.com/google/googlesql/blob/master/docs/timestamp_functions.md">Timestamp functions</a>.

  </td>
</tr>

<tr>
  <td><a href="https://github.com/google/googlesql/blob/master/docs/timestamp_functions.md#unix_seconds"><code>UNIX_SECONDS</code></a>
</td>
  <td>
    Converts a <code>TIMESTAMP</code> value to the number of seconds since
    1970-01-01 00:00:00 UTC.
    <br>For more information, see <a href="https://github.com/google/googlesql/blob/master/docs/timestamp_functions.md">Timestamp functions</a>.

  </td>
</tr>

  </tbody>
</table>

## `BIT_CAST_TO_INT32`

```googlesql
BIT_CAST_TO_INT32(value)
```

**Description**

GoogleSQL supports bit casting to `INT32`. A bit
cast is a cast in which the order of bits is preserved instead of the value
those bytes represent.

The `value` parameter can represent:

+ `INT32`
+ `UINT32`

**Return Data Type**

`INT32`

**Examples**

```googlesql
SELECT BIT_CAST_TO_UINT32(-1) as UINT32_value, BIT_CAST_TO_INT32(BIT_CAST_TO_UINT32(-1)) as bit_cast_value;

/*---------------+----------------------+
 | UINT32_value  | bit_cast_value       |
 +---------------+----------------------+
 | 4294967295    | -1                   |
 +---------------+----------------------*/
```

## `BIT_CAST_TO_INT64`

```googlesql
BIT_CAST_TO_INT64(value)
```

**Description**

GoogleSQL supports bit casting to `INT64`. A bit
cast is a cast in which the order of bits is preserved instead of the value
those bytes represent.

The `value` parameter can represent:

+ `INT64`
+ `UINT64`

**Return Data Type**

`INT64`

**Example**

```googlesql
SELECT BIT_CAST_TO_UINT64(-1) as UINT64_value, BIT_CAST_TO_INT64(BIT_CAST_TO_UINT64(-1)) as bit_cast_value;

/*-----------------------+----------------------+
 | UINT64_value          | bit_cast_value       |
 +-----------------------+----------------------+
 | 18446744073709551615  | -1                   |
 +-----------------------+----------------------*/
```

## `BIT_CAST_TO_UINT32`

```googlesql
BIT_CAST_TO_UINT32(value)
```

**Description**

GoogleSQL supports bit casting to `UINT32`. A bit
cast is a cast in which the order of bits is preserved instead of the value
those bytes represent.

The `value` parameter can represent:

+ `INT32`
+ `UINT32`

**Return Data Type**

`UINT32`

**Examples**

```googlesql
SELECT -1 as UINT32_value, BIT_CAST_TO_UINT32(-1) as bit_cast_value;

/*--------------+----------------------+
 | UINT32_value | bit_cast_value       |
 +--------------+----------------------+
 | -1           | 4294967295           |
 +--------------+----------------------*/
```

## `BIT_CAST_TO_UINT64`

```googlesql
BIT_CAST_TO_UINT64(value)
```

**Description**

GoogleSQL supports bit casting to `UINT64`. A bit
cast is a cast in which the order of bits is preserved instead of the value
those bytes represent.

The `value` parameter can represent:

+ `INT64`
+ `UINT64`

**Return Data Type**

`UINT64`

**Example**

```googlesql
SELECT -1 as INT64_value, BIT_CAST_TO_UINT64(-1) as bit_cast_value;

/*--------------+----------------------+
 | INT64_value  | bit_cast_value       |
 +--------------+----------------------+
 | -1           | 18446744073709551615 |
 +--------------+----------------------*/
```

## `CAST` 
<a id="cast"></a>

```googlesql
CAST(expression AS typename [format_clause])
```

**Description**

Cast syntax is used in a query to indicate that the result type of an
expression should be converted to some other type.

When using `CAST`, a query can fail if GoogleSQL is unable to perform
the cast. If you want to protect your queries from these types of errors, you
can use [SAFE_CAST][con-func-safecast].

Casts between supported types that don't successfully map from the original
value to the target domain produce runtime errors. For example, casting
`BYTES` to `STRING` where the byte sequence isn't valid UTF-8 results in a
runtime error.

Other examples include:

+ Casting `INT64` to `INT32` where the value overflows `INT32`.
+ Casting `STRING` to `INT32` where the `STRING` contains non-digit characters.

Some casts can include a [format clause][formatting-syntax], which provides
instructions for how to conduct the
cast. For example, you could
instruct a cast to convert a sequence of bytes to a BASE64-encoded string
instead of a UTF-8-encoded string.

The structure of the format clause is unique to each type of cast and more
information is available in the section for that cast.

**Examples**

The following query results in `"true"` if `x` is `1`, `"false"` for any other
non-`NULL` value, and `NULL` if `x` is `NULL`.

```googlesql
CAST(x=1 AS STRING)
```

### CAST AS ARRAY

```googlesql
CAST(expression AS ARRAY<element_type>)
```

**Description**

GoogleSQL supports [casting][con-func-cast] to `ARRAY`. The
`expression` parameter can represent an expression for these data types:

+ `ARRAY`

**Conversion rules**

<table>
  <tr>
    <th>From</th>
    <th>To</th>
    <th>Rule(s) when casting <code>x</code></th>
  </tr>
  <tr>
    <td><code>ARRAY</code></td>
    <td><code>ARRAY</code></td>
    <td>
      
      The element types of the input
      array must be castable to the
      element types of the target array.
      For example, casting from type
      <code>ARRAY&lt;INT64&gt;</code> to
      <code>ARRAY&lt;DOUBLE&gt;</code> or
      <code>ARRAY&lt;STRING&gt;</code> is valid;
      casting from type <code>ARRAY&lt;INT64&gt;</code>
      to <code>ARRAY&lt;BYTES&gt;</code> isn't valid.
      
    </td>
  </tr>
</table>

### CAST AS BIGNUMERIC 
<a id="cast_bignumeric"></a>

```googlesql
CAST(expression AS BIGNUMERIC)
```

**Description**

GoogleSQL supports [casting][con-func-cast] to `BIGNUMERIC`. The
`expression` parameter can represent an expression for these data types:

+ `INT32`
+ `UINT32`
+ `INT64`
+ `UINT64`
+ `FLOAT`
+ `DOUBLE`
+ `NUMERIC`
+ `BIGNUMERIC`
+ `STRING`

**Conversion rules**

<table>
  <tr>
    <th>From</th>
    <th>To</th>
    <th>Rule(s) when casting <code>x</code></th>
  </tr>
  <tr>
    <td>Floating Point</td>
    <td><code>BIGNUMERIC</code></td>
    <td>
      The floating point number will round
      <a href="https://en.wikipedia.org/wiki/Rounding#Round_half_away_from_zero">half away from zero</a>.

      Casting a <code>NaN</code>, <code>+inf</code> or
      <code>-inf</code> will return an error. Casting a value outside the range
      of <code>BIGNUMERIC</code> returns an overflow error.
    </td>
  </tr>
  <tr>
    <td><code>STRING</code></td>
    <td><code>BIGNUMERIC</code></td>
    <td>
      The numeric literal contained in the string must not exceed
      the maximum precision or range of the
      <code>BIGNUMERIC</code> type, or an error will occur. If the number of
      digits after the decimal point exceeds 38, then the resulting
      <code>BIGNUMERIC</code> value will round
      <a href="https://en.wikipedia.org/wiki/Rounding#Round_half_away_from_zero">half away from zero</a>

      to have 38 digits after the decimal point.
    </td>
  </tr>
</table>

### CAST AS BOOL

```googlesql
CAST(expression AS BOOL)
```

**Description**

GoogleSQL supports [casting][con-func-cast] to `BOOL`. The
`expression` parameter can represent an expression for these data types:

+ `INT32`
+ `UINT32`
+ `INT64`
+ `UINT64`
+ `BOOL`
+ `STRING`

**Conversion rules**

<table>
  <tr>
    <th>From</th>
    <th>To</th>
    <th>Rule(s) when casting <code>x</code></th>
  </tr>
  <tr>
    <td>Integer</td>
    <td><code>BOOL</code></td>
    <td>
      Returns <code>FALSE</code> if <code>x</code> is <code>0</code>,
      <code>TRUE</code> otherwise.
    </td>
  </tr>
  <tr>
    <td><code>STRING</code></td>
    <td><code>BOOL</code></td>
    <td>
      Returns <code>TRUE</code> if <code>x</code> is <code>"true"</code> and
      <code>FALSE</code> if <code>x</code> is <code>"false"</code><br />
      All other values of <code>x</code> are invalid and throw an error instead
      of casting to a boolean.<br />
      A string is case-insensitive when converting
      to a boolean.
    </td>
  </tr>
</table>

### CAST AS BYTES

```googlesql
CAST(expression AS BYTES [format_clause])
```

**Description**

GoogleSQL supports [casting][con-func-cast] to `BYTES`. The
`expression` parameter can represent an expression for these data types:

+ `BYTES`
+ `STRING`
+ `PROTO`

**Format clause**

When an expression of one type is cast to another type, you can use the
[format clause][formatting-syntax] to provide instructions for how to conduct
the cast. You can use the format clause in this section if `expression` is a
`STRING`.

+ [Format string as bytes][format-string-as-bytes]

**Conversion rules**

<table>
  <tr>
    <th>From</th>
    <th>To</th>
    <th>Rule(s) when casting <code>x</code></th>
  </tr>
  <tr>
    <td><code>STRING</code></td>
    <td><code>BYTES</code></td>
    <td>
      Strings are cast to bytes using UTF-8 encoding. For example,
      the string "&copy;", when cast to
      bytes, would become a 2-byte sequence with the
      hex values C2 and A9.
    </td>
  </tr>
  
  <tr>
    <td><code>PROTO</code></td>
    <td><code>BYTES</code></td>
    <td>
      Returns the proto2 wire format bytes
      of <code>x</code>.
    </td>
  </tr>
  
</table>

### CAST AS DATE

```googlesql
CAST(expression AS DATE [format_clause])
```

**Description**

GoogleSQL supports [casting][con-func-cast] to `DATE`. The `expression`
parameter can represent an expression for these data types:

+ `STRING`
+ `DATETIME`
+ `TIMESTAMP`

**Format clause**

When an expression of one type is cast to another type, you can use the
[format clause][formatting-syntax] to provide instructions for how to conduct
the cast. You can use the format clause in this section if `expression` is a
`STRING`.

+ [Format string as date and time][format-string-as-date-time]

**Conversion rules**

<table>
  <tr>
    <th>From</th>
    <th>To</th>
    <th>Rule(s) when casting <code>x</code></th>
  </tr>
  <tr>
    <td><code>STRING</code></td>
    <td><code>DATE</code></td>
    <td>
      When casting from string to date, the string must conform to
      the supported date literal format, and is independent of time zone. If the
      string expression is invalid or represents a date that's outside of the
      supported min/max range, then an error is produced.
    </td>
  </tr>
  
  <tr>
    <td><code>TIMESTAMP</code></td>
    <td><code>DATE</code></td>
    <td>
      Casting from a timestamp to date effectively truncates the timestamp as
      of the default time zone.
    </td>
  </tr>
  
</table>

### CAST AS DATETIME

```googlesql
CAST(expression AS DATETIME [format_clause])
```

**Description**

GoogleSQL supports [casting][con-func-cast] to `DATETIME`. The
`expression` parameter can represent an expression for these data types:

+ `STRING`
+ `DATETIME`
+ `TIMESTAMP`

**Format clause**

When an expression of one type is cast to another type, you can use the
[format clause][formatting-syntax] to provide instructions for how to conduct
the cast. You can use the format clause in this section if `expression` is a
`STRING`.

+ [Format string as date and time][format-string-as-date-time]

**Conversion rules**

<table>
  <tr>
    <th>From</th>
    <th>To</th>
    <th>Rule(s) when casting <code>x</code></th>
  </tr>
  <tr>
    <td><code>STRING</code></td>
    <td><code>DATETIME</code></td>
    <td>
      When casting from string to datetime, the string must conform to the
      supported datetime literal format, and is independent of time zone. If
      the string expression is invalid or represents a datetime that's outside
      of the supported min/max range, then an error is produced.
    </td>
  </tr>
  
  <tr>
    <td><code>TIMESTAMP</code></td>
    <td><code>DATETIME</code></td>
    <td>
      Casting from a timestamp to datetime effectively truncates the timestamp
      as of the default time zone.
    </td>
  </tr>
  
</table>

### CAST AS ENUM

```googlesql
CAST(expression AS ENUM)
```

**Description**

GoogleSQL supports [casting][con-func-cast] to `ENUM`. The `expression`
parameter can represent an expression for these data types:

+ `INT32`
+ `UINT32`
+ `INT64`
+ `UINT64`
+ `STRING`
+ `ENUM`

**Conversion rules**

<table>
  <tr>
    <th>From</th>
    <th>To</th>
    <th>Rule(s) when casting <code>x</code></th>
  </tr>
  <tr>
    <td><code>ENUM</code></td>
    <td><code>ENUM</code></td>
    <td>Must have the same enum name.</td>
  </tr>
</table>

### CAST AS Floating Point 
<a id="cast_as_floating_point"></a>

```googlesql
CAST(expression AS DOUBLE)
```

```googlesql
CAST(expression AS FLOAT)
```

**Description**

GoogleSQL supports [casting][con-func-cast] to floating point types.
The `expression` parameter can represent an expression for these data types:

+ `INT32`
+ `UINT32`
+ `INT64`
+ `UINT64`
+ `FLOAT`
+ `DOUBLE`
+ `NUMERIC`
+ `BIGNUMERIC`
+ `STRING`

**Conversion rules**

<table>
  <tr>
    <th>From</th>
    <th>To</th>
    <th>Rule(s) when casting <code>x</code></th>
  </tr>
  <tr>
    <td>Integer</td>
    <td>Floating Point</td>
    <td>
      Returns a close but potentially not exact floating point value.
    </td>
  </tr>
  
  <tr>
    <td><code>NUMERIC</code></td>
    <td>Floating Point</td>
    <td>
      <code>NUMERIC</code> will convert to the closest floating point number
      with a possible loss of precision.
    </td>
  </tr>
  
  
  <tr>
    <td><code>BIGNUMERIC</code></td>
    <td>Floating Point</td>
    <td>
      <code>BIGNUMERIC</code> will convert to the closest floating point number
      with a possible loss of precision.
    </td>
  </tr>
  
  <tr>
    <td><code>STRING</code></td>
    <td>Floating Point</td>
    <td>
      Returns <code>x</code> as a floating point value, interpreting it as
      having the same form as a valid floating point literal.
      Also supports casts from <code>"[+,-]inf"</code> to
      <code>[,-]Infinity</code>,
      <code>"[+,-]infinity"</code> to <code>[,-]Infinity</code>, and
      <code>"[+,-]nan"</code> to <code>NaN</code>.
      Conversions are case-insensitive.
    </td>
  </tr>
</table>

### CAST AS Integer 
<a id="cast_as_integer"></a>

```googlesql
CAST(expression AS INT32)
```

```googlesql
CAST(expression AS UINT32)
```

```googlesql
CAST(expression AS INT64)
```

```googlesql
CAST(expression AS UINT64)
```

**Description**

GoogleSQL supports [casting][con-func-cast] to integer types.
The `expression` parameter can represent an expression for these data types:

+ `INT32`
+ `UINT32`
+ `INT64`
+ `UINT64`
+ `FLOAT`
+ `DOUBLE`
+ `NUMERIC`
+ `BIGNUMERIC`
+ `ENUM`
+ `BOOL`
+ `STRING`

**Conversion rules**

<table>
  <tr>
    <th>From</th>
    <th>To</th>
    <th>Rule(s) when casting <code>x</code></th>
  </tr>
  
  <tr>
    <td>
      Floating Point
    </td>
    <td>
      Integer
    </td>
    <td>
      Returns the closest integer value.<br />
      Halfway cases such as 1.5 or -0.5 round away from zero.
    </td>
  </tr>
  <tr>
    <td><code>BOOL</code></td>
    <td>Integer</td>
    <td>
      Returns <code>1</code> if <code>x</code> is <code>TRUE</code>,
      <code>0</code> otherwise.
    </td>
  </tr>
  
  <tr>
    <td><code>STRING</code></td>
    <td>Integer</td>
    <td>
      A hex string can be cast to an integer. For example,
      <code>0x123</code> to <code>291</code> or <code>-0x123</code> to
      <code>-291</code>.
    </td>
  </tr>
  
</table>

**Examples**

If you are working with hex strings (`0x123`), you can cast those strings as
integers:

```googlesql
SELECT '0x123' as hex_value, CAST('0x123' as INT64) as hex_to_int;

/*-----------+------------+
 | hex_value | hex_to_int |
 +-----------+------------+
 | 0x123     | 291        |
 +-----------+------------*/
```

```googlesql
SELECT '-0x123' as hex_value, CAST('-0x123' as INT64) as hex_to_int;

/*-----------+------------+
 | hex_value | hex_to_int |
 +-----------+------------+
 | -0x123    | -291       |
 +-----------+------------*/
```

### CAST AS INTERVAL

```googlesql
CAST(expression AS INTERVAL)
```

**Description**

GoogleSQL supports [casting][con-func-cast] to `INTERVAL`. The
`expression` parameter can represent an expression for these data types:

+ `STRING`

**Conversion rules**

<table>
  <tr>
    <th>From</th>
    <th>To</th>
    <th>Rule(s) when casting <code>x</code></th>
  </tr>
  <tr>
    <td><code>STRING</code></td>
    <td><code>INTERVAL</code></td>
    <td>
      When casting from string to interval, the string must conform to either
      <a href="https://en.wikipedia.org/wiki/ISO_8601#Durations">ISO 8601 Duration</a>

      standard or to interval literal
      format 'Y-M D H:M:S.F'. Partial interval literal formats are also accepted
      when they aren't ambiguous, for example 'H:M:S'.
      If the string expression is invalid or represents an interval that is
      outside of the supported min/max range, then an error is produced.
    </td>
  </tr>
</table>

**Examples**

```googlesql
SELECT input, CAST(input AS INTERVAL) AS output
FROM UNNEST([
  '1-2 3 10:20:30.456',
  '1-2',
  '10:20:30',
  'P1Y2M3D',
  'PT10H20M30,456S'
]) input

/*--------------------+--------------------+
 | input              | output             |
 +--------------------+--------------------+
 | 1-2 3 10:20:30.456 | 1-2 3 10:20:30.456 |
 | 1-2                | 1-2 0 0:0:0        |
 | 10:20:30           | 0-0 0 10:20:30     |
 | P1Y2M3D            | 1-2 3 0:0:0        |
 | PT10H20M30,456S    | 0-0 0 10:20:30.456 |
 +--------------------+--------------------*/
```

### CAST AS NUMERIC 
<a id="cast_numeric"></a>

```googlesql
CAST(expression AS NUMERIC)
```

**Description**

GoogleSQL supports [casting][con-func-cast] to `NUMERIC`. The
`expression` parameter can represent an expression for these data types:

+ `INT32`
+ `UINT32`
+ `INT64`
+ `UINT64`
+ `FLOAT`
+ `DOUBLE`
+ `NUMERIC`
+ `BIGNUMERIC`
+ `STRING`

**Conversion rules**

<table>
  <tr>
    <th>From</th>
    <th>To</th>
    <th>Rule(s) when casting <code>x</code></th>
  </tr>
  <tr>
    <td><code>Floating Point</code></td>
    <td><code>NUMERIC</code></td>
    <td>
      The floating point number will round
      <a href="https://en.wikipedia.org/wiki/Rounding#Round_half_away_from_zero">half away from zero</a>.

      Casting a <code>NaN</code>, <code>+inf</code> or
      <code>-inf</code> will return an error. Casting a value outside the range
      of <code>NUMERIC</code> returns an overflow error.
    </td>
  </tr>
  <tr>
    <td><code>STRING</code></td>
    <td><code>NUMERIC</code></td>
    <td>
      The numeric literal contained in the string must not exceed
      the maximum precision or range of the <code>NUMERIC</code>
      type, or an error will occur. If the number of digits
      after the decimal point exceeds nine, then the resulting
      <code>NUMERIC</code> value will round
      <a href="https://en.wikipedia.org/wiki/Rounding#Round_half_away_from_zero">half away from zero</a>.

      to have nine digits after the decimal point.
    </td>
  </tr>
</table>

### CAST AS PROTO

```googlesql
CAST(expression AS PROTO)
```

**Description**

GoogleSQL supports [casting][con-func-cast] to `PROTO`. The
`expression` parameter can represent an expression for these data types:

+ `STRING`
+ `BYTES`
+ `PROTO`

**Conversion rules**

<table>
  <tr>
    <th>From</th>
    <th>To</th>
    <th>Rule(s) when casting <code>x</code></th>
  </tr>
  <tr>
    <td><code>STRING</code></td>
    <td><code>PROTO</code></td>
    <td>
      Returns the protocol buffer that results from parsing
      from proto2 text format.<br />
      Throws an error if parsing fails, e.g., if not all required fields are
      set.
    </td>
  </tr>
  <tr>
    <td><code>BYTES</code></td>
    <td><code>PROTO</code></td>
    <td>
      Returns the protocol buffer that results from parsing
      <code>x</code> from the proto2 wire format.<br />
      Throws an error if parsing fails, e.g., if not all required fields are
      set.
    </td>
  </tr>
  <tr>
    <td><code>PROTO</code></td>
    <td><code>PROTO</code></td>
    <td>Must have the same protocol buffer name.</td>
  </tr>
</table>

**Example**

This example references a protocol buffer called `Award`.

```proto
message Award {
  required int32 year = 1;
  optional int32 month = 2;
  repeated Type type = 3;

  message Type {
    optional string award_name = 1;
    optional string category = 2;
  }
}
```

```googlesql
SELECT
  CAST(
    '''
    year: 2001
    month: 9
    type { award_name: 'Best Artist' category: 'Artist' }
    type { award_name: 'Best Album' category: 'Album' }
    '''
    AS googlesql.examples.music.Award)
  AS award_col

/*---------------------------------------------------------+
 | award_col                                               |
 +---------------------------------------------------------+
 | {                                                       |
 |   year: 2001                                            |
 |   month: 9                                              |
 |   type { award_name: "Best Artist" category: "Artist" } |
 |   type { award_name: "Best Album" category: "Album" }   |
 | }                                                       |
 +---------------------------------------------------------*/
```

### CAST AS RANGE

```googlesql
CAST(expression AS RANGE)
```

**Description**

GoogleSQL supports [casting][con-func-cast] to `RANGE`. The
`expression` parameter can represent an expression for these data types:

+ `STRING`

**Conversion rules**

<table>
  <tr>
    <th>From</th>
    <th>To</th>
    <th>Rule(s) when casting <code>x</code></th>
  </tr>
  <tr>
    <td><code>STRING</code></td>
    <td><code>RANGE</code></td>
    <td>
      When casting from string to range, the string must conform to the
      supported range literal format. If the string expression is invalid or
      represents a range that's outside of the supported subtype min/max range,
      then an error is produced.
    </td>
  </tr>
</table>

**Examples**

```googlesql
SELECT CAST(
  '[2020-01-01, 2020-01-02)'
  AS RANGE<DATE>) AS string_to_range

/*----------------------------------------+
 | string_to_range                        |
 +----------------------------------------+
 | [DATE '2020-01-01', DATE '2020-01-02') |
 +----------------------------------------*/
```

```googlesql
SELECT CAST(
  '[2014-09-27 12:30:00.45, 2016-10-17 11:15:00.33)'
  AS RANGE<DATETIME>) AS string_to_range

/*------------------------------------------------------------------------+
 | string_to_range                                                        |
 +------------------------------------------------------------------------+
 | [DATETIME '2014-09-27 12:30:00.45', DATETIME '2016-10-17 11:15:00.33') |
 +------------------------------------------------------------------------*/
```

```googlesql
SELECT CAST(
  '[2014-09-27 12:30:00+08, 2016-10-17 11:15:00+08)'
  AS RANGE<TIMESTAMP>) AS string_to_range

-- Results depend upon where this query was executed.
/*--------------------------------------------------------------------------+
 | string_to_range                                                          |
 +--------------------------------------------------------------------------+
 | [TIMESTAMP '2014-09-27 12:30:00+08', TIMESTAMP '2016-10-17 11:15:00+08') |
 +--------------------------------------------------------------------------*/
```

```googlesql
SELECT CAST(
  '[UNBOUNDED, 2020-01-02)'
  AS RANGE<DATE>) AS string_to_range

/*--------------------------------+
 | string_to_range                |
 +--------------------------------+
 | [UNBOUNDED, DATE '2020-01-02') |
 +--------------------------------*/
```

```googlesql
SELECT CAST(
  '[2020-01-01, NULL)'
  AS RANGE<DATE>) AS string_to_range

/*--------------------------------+
 | string_to_range                |
 +--------------------------------+
 | [DATE '2020-01-01', UNBOUNDED) |
 +--------------------------------*/
```

### CAST AS STRING 
<a id="cast_as_string"></a>

```googlesql
CAST(expression AS STRING [format_clause [AT TIME ZONE timezone_expr]])
```

**Description**

GoogleSQL supports [casting][con-func-cast] to `STRING`. The
`expression` parameter can represent an expression for these data types:

+ `INT32`
+ `UINT32`
+ `INT64`
+ `UINT64`
+ `FLOAT`
+ `DOUBLE`
+ `NUMERIC`
+ `BIGNUMERIC`
+ `ENUM`
+ `BOOL`
+ `BYTES`
+ `PROTO`
+ `TIME`
+ `DATE`
+ `DATETIME`
+ `TIMESTAMP`
+ `RANGE`
+ `INTERVAL`
+ `STRING`

**Format clause**

When an expression of one type is cast to another type, you can use the
[format clause][formatting-syntax] to provide instructions for how to conduct
the cast. You can use the format clause in this section if `expression` is one
of these data types:

+ `INT32`
+ `UINT32`
+ `INT64`
+ `UINT64`
+ `FLOAT`
+ `DOUBLE`
+ `NUMERIC`
+ `BIGNUMERIC`
+ `BYTES`
+ `TIME`
+ `DATE`
+ `DATETIME`
+ `TIMESTAMP`

The format clause for `STRING` has an additional optional clause called
`AT TIME ZONE timezone_expr`, which you can use to specify a specific time zone
to use during formatting of a `TIMESTAMP`. If this optional clause isn't
included when formatting a `TIMESTAMP`, the default time zone,
which is implementation defined, is used.

For more information, see the following topics:

+ [Format bytes as string][format-bytes-as-string]
+ [Format date and time as string][format-date-time-as-string]
+ [Format numeric type as string][format-numeric-type-as-string]

**Conversion rules**

<table>
  <tr>
    <th>From</th>
    <th>To</th>
    <th>Rule(s) when casting <code>x</code></th>
  </tr>
  <tr>
    <td>Floating Point</td>
    <td><code>STRING</code></td>
    <td>Returns an approximate string representation. A returned
    <code>NaN</code> or <code>0</code> will not be signed.<br />
    </td>
  </tr>
  <tr>
    <td><code>BOOL</code></td>
    <td><code>STRING</code></td>
    <td>
      Returns <code>"true"</code> if <code>x</code> is <code>TRUE</code>,
      <code>"false"</code> otherwise.</td>
  </tr>
  <tr>
    <td><code>BYTES</code></td>
    <td><code>STRING</code></td>
    <td>
      Returns <code>x</code> interpreted as a UTF-8 string.<br />
      For example, the bytes literal
      <code>b'\xc2\xa9'</code>, when cast to a string,
      is interpreted as UTF-8 and becomes the unicode character "&copy;".<br />
      An error occurs if <code>x</code> isn't valid UTF-8.</td>
  </tr>
  
  <tr>
    <td><code>ENUM</code></td>
    <td><code>STRING</code></td>
    <td>
      Returns the canonical enum value name of
      <code>x</code>.<br />
      If an enum value has multiple names (aliases),
      the canonical name/alias for that value is used.</td>
  </tr>
  
  
  <tr>
    <td><code>PROTO</code></td>
    <td><code>STRING</code></td>
    <td>Returns the proto2 text format representation of <code>x</code>.</td>
  </tr>
  
  
  <tr>
    <td><code>TIME</code></td>
    <td><code>STRING</code></td>
    <td>
      Casting from a time type to a string is independent of time zone and
      is of the form <code>HH:MM:SS</code>.
    </td>
  </tr>
  
  
  <tr>
    <td><code>DATE</code></td>
    <td><code>STRING</code></td>
    <td>
      Casting from a date type to a string is independent of time zone and is
      of the form <code>YYYY-MM-DD</code>.
    </td>
  </tr>
  
  
  <tr>
    <td><code>DATETIME</code></td>
    <td><code>STRING</code></td>
    <td>
      Casting from a datetime type to a string is independent of time zone and
      is of the form <code>YYYY-MM-DD HH:MM:SS</code>.
    </td>
  </tr>
  
  
  <tr>
    <td><code>TIMESTAMP</code></td>
    <td><code>STRING</code></td>
    <td>
      When casting from timestamp types to string, the timestamp is interpreted
      using the default time zone, which is implementation defined. The number of
      subsecond digits produced depends on the number of trailing zeroes in the
      subsecond part: the CAST function will truncate zero, three, or six
      digits.
    </td>
  </tr>
  
  <tr>
    <td><code>INTERVAL</code></td>
    <td><code>STRING</code></td>
    <td>
      Casting from an interval to a string is of the form
      <code>Y-M D H:M:S</code>.
    </td>
  </tr>
  
</table>

**Examples**

```googlesql
SELECT CAST(CURRENT_DATE() AS STRING) AS current_date

/*---------------+
 | current_date  |
 +---------------+
 | 2021-03-09    |
 +---------------*/
```

```googlesql
SELECT CAST(CURRENT_DATE() AS STRING FORMAT 'DAY') AS current_day

/*-------------+
 | current_day |
 +-------------+
 | MONDAY      |
 +-------------*/
```

```googlesql
SELECT CAST(
  TIMESTAMP '2008-12-25 00:00:00+00:00'
  AS STRING FORMAT 'YYYY-MM-DD HH24:MI:SS TZH:TZM') AS date_time_to_string

-- Results depend upon where this query was executed.
/*------------------------------+
 | date_time_to_string          |
 +------------------------------+
 | 2008-12-24 16:00:00 -08:00   |
 +------------------------------*/
```

```googlesql
SELECT CAST(
  TIMESTAMP '2008-12-25 00:00:00+00:00'
  AS STRING FORMAT 'YYYY-MM-DD HH24:MI:SS TZH:TZM'
  AT TIME ZONE 'Asia/Kolkata') AS date_time_to_string

-- Because the time zone is specified, the result is always the same.
/*------------------------------+
 | date_time_to_string          |
 +------------------------------+
 | 2008-12-25 05:30:00 +05:30   |
 +------------------------------*/
```

```googlesql
SELECT CAST(INTERVAL 3 DAY AS STRING) AS interval_to_string

/*--------------------+
 | interval_to_string |
 +--------------------+
 | 0-0 3 0:0:0        |
 +--------------------*/
```

```googlesql
SELECT CAST(
  INTERVAL "1-2 3 4:5:6.789" YEAR TO SECOND
  AS STRING) AS interval_to_string

/*--------------------+
 | interval_to_string |
 +--------------------+
 | 1-2 3 4:5:6.789    |
 +--------------------*/
```

### CAST AS STRUCT

```googlesql
CAST(expression AS STRUCT)
```

**Description**

GoogleSQL supports [casting][con-func-cast] to `STRUCT`. The
`expression` parameter can represent an expression for these data types:

+ `STRUCT`

**Conversion rules**

<table>
  <tr>
    <th>From</th>
    <th>To</th>
    <th>Rule(s) when casting <code>x</code></th>
  </tr>
  <tr>
    <td><code>STRUCT</code></td>
    <td><code>STRUCT</code></td>
    <td>
      Allowed if the following conditions are met:<br />
      <ol>
        <li>
          The two structs have the same number of
          fields.
        </li>
        <li>
          The original struct field types can be
          explicitly cast to the corresponding target
          struct field types (as defined by field
          order, not field name).
        </li>
      </ol>
    </td>
  </tr>
</table>

### CAST AS TIME

```googlesql
CAST(expression AS TIME [format_clause])
```

**Description**

GoogleSQL supports [casting][con-func-cast] to TIME. The `expression`
parameter can represent an expression for these data types:

+ `STRING`
+ `TIME`
+ `DATETIME`
+ `TIMESTAMP`

**Format clause**

When an expression of one type is cast to another type, you can use the
[format clause][formatting-syntax] to provide instructions for how to conduct
the cast. You can use the format clause in this section if `expression` is a
`STRING`.

+ [Format string as date and time][format-string-as-date-time]

**Conversion rules**

<table>
  <tr>
    <th>From</th>
    <th>To</th>
    <th>Rule(s) when casting <code>x</code></th>
  </tr>
  <tr>
    <td><code>STRING</code></td>
    <td><code>TIME</code></td>
    <td>
      When casting from string to time, the string must conform to
      the supported time literal format, and is independent of time zone. If the
      string expression is invalid or represents a time that's outside of the
      supported min/max range, then an error is produced.
    </td>
  </tr>
</table>

### CAST AS TIMESTAMP

```googlesql
CAST(expression AS TIMESTAMP [format_clause [AT TIME ZONE timezone_expr]])
```

**Description**

GoogleSQL supports [casting][con-func-cast] to `TIMESTAMP`. The
`expression` parameter can represent an expression for these data types:

+ `STRING`
+ `DATETIME`
+ `TIMESTAMP`

**Format clause**

When an expression of one type is cast to another type, you can use the
[format clause][formatting-syntax] to provide instructions for how to conduct
the cast. You can use the format clause in this section if `expression` is a
`STRING`.

+ [Format string as date and time][format-string-as-date-time]

The format clause for `TIMESTAMP` has an additional optional clause called
`AT TIME ZONE timezone_expr`, which you can use to specify a specific time zone
to use during formatting. If this optional clause isn't included, the default
time zone, which is implementation defined, is used.

**Conversion rules**

<table>
  <tr>
    <th>From</th>
    <th>To</th>
    <th>Rule(s) when casting <code>x</code></th>
  </tr>
  <tr>
    <td><code>STRING</code></td>
    <td><code>TIMESTAMP</code></td>
    <td>
      When casting from string to a timestamp, <code>string_expression</code>
      must conform to the supported timestamp literal formats, or else a runtime
      error occurs. The <code>string_expression</code> may itself contain a
      time zone.
      <br /><br />
      If there is a time zone in the <code>string_expression</code>, that
      time zone is used for conversion, otherwise the default time zone,
      which is implementation defined, is used. If the string has fewer than six digits,
      then it's implicitly widened.
      <br /><br />
      An error is produced if the <code>string_expression</code> is invalid,
      has more than six subsecond digits (i.e., precision greater than
      microseconds), or represents a time outside of the supported timestamp
      range.
    </td>
  </tr>
  
  <tr>
    <td><code>DATE</code></td>
    <td><code>TIMESTAMP</code></td>
    <td>
      Casting from a date to a timestamp interprets <code>date_expression</code>
      as of midnight (start of the day) in the default time zone,
      which is implementation defined.
    </td>
  </tr>
  
  
  <tr>
    <td><code>DATETIME</code></td>
    <td><code>TIMESTAMP</code></td>
    <td>
      Casting from a datetime to a timestamp interprets
      <code>datetime_expression</code> in the default time zone,
      which is implementation defined.
      <br /><br />
      Most valid datetime values have exactly one corresponding timestamp
      in each time zone. However, there are certain combinations of valid
      datetime values and time zones that have zero or two corresponding
      timestamp values. This happens in a time zone when clocks are set forward
      or set back, such as for Daylight Savings Time.
      When there are two valid timestamps, the earlier one is used.
      When there is no valid timestamp, the length of the gap in time
      (typically one hour) is added to the datetime.
    </td>
  </tr>
  
</table>

**Examples**

The following example casts a string-formatted timestamp as a timestamp:

```googlesql
SELECT CAST("2020-06-02 17:00:53.110+00:00" AS TIMESTAMP) AS as_timestamp

-- Results depend upon where this query was executed.
/*----------------------------+
 | as_timestamp               |
 +----------------------------+
 | 2020-06-03 00:00:53.110+00 |
 +----------------------------*/
```

The following examples cast a string-formatted date and time as a timestamp.
These examples return the same output as the previous example.

```googlesql
SELECT CAST('06/02/2020 17:00:53.110' AS TIMESTAMP FORMAT 'MM/DD/YYYY HH24:MI:SS.FF3' AT TIME ZONE 'UTC') AS as_timestamp
```

```googlesql
SELECT CAST('06/02/2020 17:00:53.110' AS TIMESTAMP FORMAT 'MM/DD/YYYY HH24:MI:SS.FF3' AT TIME ZONE '+00') AS as_timestamp
```

```googlesql
SELECT CAST('06/02/2020 17:00:53.110 +00' AS TIMESTAMP FORMAT 'MM/DD/YYYY HH24:MI:SS.FF3 TZH') AS as_timestamp
```

[formatting-syntax]: https://github.com/google/googlesql/blob/master/docs/format-elements.md#formatting_syntax

[format-string-as-bytes]: https://github.com/google/googlesql/blob/master/docs/format-elements.md#format_string_as_bytes

[format-bytes-as-string]: https://github.com/google/googlesql/blob/master/docs/format-elements.md#format_bytes_as_string

[format-date-time-as-string]: https://github.com/google/googlesql/blob/master/docs/format-elements.md#format_date_time_as_string

[format-string-as-date-time]: https://github.com/google/googlesql/blob/master/docs/format-elements.md#format_string_as_datetime

[format-numeric-type-as-string]: https://github.com/google/googlesql/blob/master/docs/format-elements.md#format_numeric_type_as_string

[con-func-cast]: #cast

[con-func-safecast]: #safe_casting

## `PARSE_BIGNUMERIC` 
<a id="parse_bignumeric"></a>

```googlesql
PARSE_BIGNUMERIC(string_expression)
```

**Description**

Converts a `STRING` to a `BIGNUMERIC` value.

The numeric literal contained in the string must not exceed the
[maximum precision or range][bignumeric-type] of the `BIGNUMERIC` type, or an
error occurs. If the number of digits after the decimal point exceeds 38, then
the resulting `BIGNUMERIC` value rounds
[half away from zero][half-from-zero-wikipedia] to have 38 digits after the
decimal point.

```googlesql

-- This example shows how a string with a decimal point is parsed.
SELECT PARSE_BIGNUMERIC("123.45") AS parsed;

/*--------+
 | parsed |
 +--------+
 | 123.45 |
 +--------*/

-- This example shows how a string with an exponent is parsed.
SELECT PARSE_BIGNUMERIC("123.456E37") AS parsed;

/*-----------------------------------------+
 | parsed                                  |
 +-----------------------------------------+
 | 123400000000000000000000000000000000000 |
 +-----------------------------------------*/

-- This example shows the rounding when digits after the decimal point exceeds 38.
SELECT PARSE_BIGNUMERIC("1.123456789012345678901234567890123456789") as parsed;

/*------------------------------------------+
 | parsed                                   |
 +------------------------------------------+
 | 1.12345678901234567890123456789012345679 |
 +------------------------------------------*/
```

This function is similar to using the [`CAST AS BIGNUMERIC`][cast-bignumeric]
function except that the `PARSE_BIGNUMERIC` function only accepts string inputs
and allows the following in the string:

+   Spaces between the sign (+/-) and the number
+   Signs (+/-) after the number

Rules for valid input strings:

<table>
  <thead>
    <tr>
      <th>Rule</th>
      <th>Example Input</th>
      <th>Output</th>
    </tr>
  </thead>
  <tbody>
    <tr>
      <td>
        The string can only contain digits, commas, decimal points and signs.
      </td>
      <td>
        "- 12,34567,89.0"
      </td>
      <td>-123456789</td>
    </tr>
    <tr>
      <td>Whitespaces are allowed anywhere except between digits.</td>
      <td>
        "  -  12.345  "
      </td>
      <td>-12.345</td>
    </tr>
    <tr>
      <td>Only digits and commas are allowed before the decimal point.</td>
      <td>
        " 12,345,678"
      </td>
      <td>12345678</td>
    </tr>
    <tr>
      <td>Only digits are allowed after the decimal point.</td>
      <td>
        "1.234 "
      </td>
      <td>1.234</td>
    </tr>
    <tr>
      <td>
        Use <code>E</code> or <code>e</code> for exponents. After the
        <code>e</code>, digits and a leading sign indicator are allowed.
      </td>
      <td>" 123.45e-1"</td>
      <td>12.345</td>
    </tr>
    <tr>
      <td>
        If the integer part isn't empty, then it must contain at least one
        digit.
      </td>
      <td>" 0,.12 -"</td>
      <td>-0.12</td>
    </tr>
    <tr>
      <td>
        If the string contains a decimal point, then it must contain at least
        one digit.
      </td>
      <td>" .1"</td>
      <td>0.1</td>
    </tr>
    <tr>
      <td>
        The string can't contain more than one sign.
      </td>
      <td>" 0.5 +"</td>
      <td>0.5</td>
    </tr>
  </tbody>
</table>

**Return Data Type**

`BIGNUMERIC`

**Examples**

This example shows an input with spaces before, after, and between the
sign and the number:

```googlesql
SELECT PARSE_BIGNUMERIC("  -  12.34 ") as parsed;

/*--------+
 | parsed |
 +--------+
 | -12.34 |
 +--------*/
```

This example shows an input with an exponent as well as the sign after the
number:

```googlesql
SELECT PARSE_BIGNUMERIC("12.34e-1-") as parsed;

/*--------+
 | parsed |
 +--------+
 | -1.234 |
 +--------*/
```

This example shows an input with multiple commas in the integer part of the
number:

```googlesql
SELECT PARSE_BIGNUMERIC("  1,2,,3,.45 + ") as parsed;

/*--------+
 | parsed |
 +--------+
 | 123.45 |
 +--------*/
```

This example shows an input with a decimal point and no digits in the whole
number part:

```googlesql
SELECT PARSE_BIGNUMERIC(".1234  ") as parsed;

/*--------+
 | parsed |
 +--------+
 | 0.1234 |
 +--------*/
```

**Examples of invalid inputs**

This example is invalid because the whole number part contains no digits:

```googlesql
SELECT PARSE_BIGNUMERIC(",,,.1234  ") as parsed;
```

This example is invalid because there are whitespaces between digits:

```googlesql
SELECT PARSE_BIGNUMERIC("1  23.4 5  ") as parsed;
```

This example is invalid because the number is empty except for an exponent:

```googlesql
SELECT PARSE_BIGNUMERIC("  e1 ") as parsed;
```

This example is invalid because the string contains multiple signs:

```googlesql
SELECT PARSE_BIGNUMERIC("  - 12.3 - ") as parsed;
```

This example is invalid because the value of the number falls outside the range
of `BIGNUMERIC`:

```googlesql
SELECT PARSE_BIGNUMERIC("12.34E100 ") as parsed;
```

This example is invalid because the string contains invalid characters:

```googlesql
SELECT PARSE_BIGNUMERIC("$12.34") as parsed;
```

[half-from-zero-wikipedia]: https://en.wikipedia.org/wiki/Rounding#Round_half_away_from_zero

[cast-bignumeric]: #cast_bignumeric

[bignumeric-type]: https://github.com/google/googlesql/blob/master/docs/data-types.md#decimal_types

## `PARSE_NUMERIC`

```googlesql
PARSE_NUMERIC(string_expression)
```

**Description**

Converts a `STRING` to a `NUMERIC` value.

The numeric literal contained in the string must not exceed the
[maximum precision or range][numeric-type] of the `NUMERIC` type, or an error
occurs. If the number of digits after the decimal point exceeds nine, then the
resulting `NUMERIC` value rounds
[half away from zero][half-from-zero-wikipedia] to have nine digits after the
decimal point.

```googlesql

-- This example shows how a string with a decimal point is parsed.
SELECT PARSE_NUMERIC("123.45") AS parsed;

/*--------+
 | parsed |
 +--------+
 | 123.45 |
 +--------*/

-- This example shows how a string with an exponent is parsed.
SELECT PARSE_NUMERIC("12.34E27") as parsed;

/*-------------------------------+
 | parsed                        |
 +-------------------------------+
 | 12340000000000000000000000000 |
 +-------------------------------*/

-- This example shows the rounding when digits after the decimal point exceeds 9.
SELECT PARSE_NUMERIC("1.0123456789") as parsed;

/*-------------+
 | parsed      |
 +-------------+
 | 1.012345679 |
 +-------------*/
```

This function is similar to using the [`CAST AS NUMERIC`][cast-numeric] function
except that the `PARSE_NUMERIC` function only accepts string inputs and allows
the following in the string:

+   Spaces between the sign (+/-) and the number
+   Signs (+/-) after the number

Rules for valid input strings:

<table>
  <thead>
    <tr>
      <th>Rule</th>
      <th>Example Input</th>
      <th>Output</th>
    </tr>
  </thead>
  <tbody>
    <tr>
      <td>
        The string can only contain digits, commas, decimal points and signs.
      </td>
      <td>
        "- 12,34567,89.0"
      </td>
      <td>-123456789</td>
    </tr>
    <tr>
      <td>Whitespaces are allowed anywhere except between digits.</td>
      <td>
        "  -  12.345  "
      </td>
      <td>-12.345</td>
    </tr>
    <tr>
      <td>Only digits and commas are allowed before the decimal point.</td>
      <td>
        " 12,345,678"
      </td>
      <td>12345678</td>
    </tr>
    <tr>
      <td>Only digits are allowed after the decimal point.</td>
      <td>
        "1.234 "
      </td>
      <td>1.234</td>
    </tr>
    <tr>
      <td>
        Use <code>E</code> or <code>e</code> for exponents. After
        the <code>e</code>,
        digits and a leading sign indicator are allowed.
      </td>
      <td>" 123.45e-1"</td>
      <td>12.345</td>
    </tr>
    <tr>
      <td>
        If the integer part isn't empty, then it must contain at least one
        digit.
      </td>
      <td>" 0,.12 -"</td>
      <td>-0.12</td>
    </tr>
    <tr>
      <td>
        If the string contains a decimal point, then it must contain at least
        one digit.
      </td>
      <td>" .1"</td>
      <td>0.1</td>
    </tr>
    <tr>
      <td>
        The string can't contain more than one sign.
      </td>
      <td>" 0.5 +"</td>
      <td>0.5</td>
    </tr>
  </tbody>
</table>

**Return Data Type**

`NUMERIC`

**Examples**

This example shows an input with spaces before, after, and between the
sign and the number:

```googlesql
SELECT PARSE_NUMERIC("  -  12.34 ") as parsed;

/*--------+
 | parsed |
 +--------+
 | -12.34 |
 +--------*/
```

This example shows an input with an exponent as well as the sign after the
number:

```googlesql
SELECT PARSE_NUMERIC("12.34e-1-") as parsed;

/*--------+
 | parsed |
 +--------+
 | -1.234 |
 +--------*/
```

This example shows an input with multiple commas in the integer part of the
number:

```googlesql
SELECT PARSE_NUMERIC("  1,2,,3,.45 + ") as parsed;

/*--------+
 | parsed |
 +--------+
 | 123.45 |
 +--------*/
```

This example shows an input with a decimal point and no digits in the whole
number part:

```googlesql
SELECT PARSE_NUMERIC(".1234  ") as parsed;

/*--------+
 | parsed |
 +--------+
 | 0.1234 |
 +--------*/
```

**Examples of invalid inputs**

This example is invalid because the whole number part contains no digits:

```googlesql
SELECT PARSE_NUMERIC(",,,.1234  ") as parsed;
```

This example is invalid because there are whitespaces between digits:

```googlesql
SELECT PARSE_NUMERIC("1  23.4 5  ") as parsed;
```

This example is invalid because the number is empty except for an exponent:

```googlesql
SELECT PARSE_NUMERIC("  e1 ") as parsed;
```

This example is invalid because the string contains multiple signs:

```googlesql
SELECT PARSE_NUMERIC("  - 12.3 - ") as parsed;
```

This example is invalid because the value of the number falls outside the range
of `BIGNUMERIC`:

```googlesql
SELECT PARSE_NUMERIC("12.34E100 ") as parsed;
```

This example is invalid because the string contains invalid characters:

```googlesql
SELECT PARSE_NUMERIC("$12.34") as parsed;
```

[half-from-zero-wikipedia]: https://en.wikipedia.org/wiki/Rounding#Round_half_away_from_zero

[cast-numeric]: #cast_numeric

[numeric-type]: https://github.com/google/googlesql/blob/master/docs/data-types.md#decimal_types

## `SAFE_CAST` 
<a id="safe_casting"></a>

<pre class="lang-sql prettyprint">
<code>SAFE_CAST(expression AS typename [format_clause])</code>
</pre>

**Description**

When using `CAST`, a query can fail if GoogleSQL is unable to perform
the cast. For example, the following query generates an error:

```googlesql
SELECT CAST("apple" AS INT64) AS not_a_number;
```

If you want to protect your queries from these types of errors, you can use
`SAFE_CAST`. `SAFE_CAST` replaces runtime errors with `NULL`s. However, during
static analysis, impossible casts between two non-castable types still produce
an error because the query is invalid.

```googlesql
SELECT SAFE_CAST("apple" AS INT64) AS not_a_number;

/*--------------+
 | not_a_number |
 +--------------+
 | NULL         |
 +--------------*/
```

Some casts can include a [format clause][formatting-syntax], which provides
instructions for how to conduct the
cast. For example, you could
instruct a cast to convert a sequence of bytes to a BASE64-encoded string
instead of a UTF-8-encoded string.

The structure of the format clause is unique to each type of cast and more
information is available in the section for that cast.

If you are casting from bytes to strings, you can also use the
function, [`SAFE_CONVERT_BYTES_TO_STRING`][SC_BTS]. Any invalid UTF-8 characters
are replaced with the unicode replacement character, `U+FFFD`.

[SC_BTS]: https://github.com/google/googlesql/blob/master/docs/string_functions.md#safe_convert_bytes_to_string

[formatting-syntax]: https://github.com/google/googlesql/blob/master/docs/format-elements.md#formatting_syntax

[conversion-rules]: https://github.com/google/googlesql/blob/master/docs/conversion_rules.md

