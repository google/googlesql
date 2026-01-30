

<!-- mdlint off(WHITESPACE_LINE_LENGTH) -->

# Format elements

GoogleSQL supports the following format elements.

## Format elements for date and time parts 
<a id="format_elements_date_time"></a>

Many GoogleSQL parsing and formatting functions rely on a format string
to describe the format of parsed or formatted values. A format string represents
the textual form of date and time and contains separate format elements that are
applied left-to-right.

These functions use format strings:

+ [`FORMAT_DATE`][format-date]
+ [`FORMAT_DATETIME`][format-datetime]
+ [`FORMAT_TIME`][format-time]
+ [`FORMAT_TIMESTAMP`][format-timestamp]
+ [`PARSE_DATE`][parse-date]
+ [`PARSE_DATETIME`][parse-datetime]
+ [`PARSE_TIME`][parse-time]
+ [`PARSE_TIMESTAMP`][parse-timestamp]

Format strings generally support the following elements:

<table>
  <thead>
    <tr>
      <th>Format element</th>
      <th>Type</th>
      <th>Description</th>
      <th>Example</th>
    </tr>
  </thead>
  <tbody>
    <tr>
      <td><code>%A</code></td>
      <td>

<span><code>DATE</code></span><br /><span><code>DATETIME</code></span><br /><span><code>TIMESTAMP</code></span><br />
</td>
      <td>The full weekday name (English).</td>
      <td><code>Wednesday</code></td>
    </tr>
    <tr>
      <td><code>%a</code></td>
      <td>

<span><code>DATE</code></span><br /><span><code>DATETIME</code></span><br /><span><code>TIMESTAMP</code></span><br />
</td>
      <td>The abbreviated weekday name (English).</td>
      <td><code>Wed</code></td>
    </tr>
    <tr>
      <td><code>%B</code></td>
      <td>

<span><code>DATE</code></span><br /><span><code>DATETIME</code></span><br /><span><code>TIMESTAMP</code></span><br />
</td>
      <td>The full month name (English).</td>
      <td><code>January</code></td>
    </tr>
    <tr>
      <td><code>%b</code></td>
      <td>

<span><code>DATE</code></span><br /><span><code>DATETIME</code></span><br /><span><code>TIMESTAMP</code></span><br />
</td>
      <td>The abbreviated month name (English).</td>
      <td><code>Jan</code></td>
    </tr>
    <tr>
      <td><code>%C</code></td>
      <td>

<span><code>DATE</code></span><br /><span><code>DATETIME</code></span><br /><span><code>TIMESTAMP</code></span><br />
</td>
      <td>
        The century (a year divided by 100 and truncated to an integer) as a
        decimal number (00-99).
      </td>
      <td><code>20</code></td>
    </tr>
    <tr>
      <td><code>%c</code></td>
      <td>

<span><code>DATETIME</code></span><br /><span><code>TIMESTAMP</code></span><br />
</td>
      <td>The date and time representation (English).</td>
      <td><code>Wed Jan 20 21:47:00 2021</code></td>
    </tr>
    <tr>
      <td><code>%D</code></td>
      <td>

<span><code>DATE</code></span><br /><span><code>DATETIME</code></span><br /><span><code>TIMESTAMP</code></span><br />
</td>
      <td>The date in the format %m/%d/%y.</td>
      <td><code>01/20/21</code></td>
    </tr>
    <tr>
      <td><code>%d</code></td>
      <td>

<span><code>DATE</code></span><br /><span><code>DATETIME</code></span><br /><span><code>TIMESTAMP</code></span><br />
</td>
      <td>The day of the month as a decimal number (01-31).</td>
      <td><code>20</code></td>
    </tr>
    <tr>
      <td><code>%e</code></td>
      <td>

<span><code>DATE</code></span><br /><span><code>DATETIME</code></span><br /><span><code>TIMESTAMP</code></span><br />
</td>
      <td>
        The day of month as a decimal number (1-31); single digits are preceded
        by a space.
      </td>
      <td><code>20</code></td>
    </tr>
    <tr>
      <td><code>%F</code></td>
      <td>

<span><code>DATE</code></span><br /><span><code>DATETIME</code></span><br /><span><code>TIMESTAMP</code></span><br />
</td>
      <td>The date in the format %Y-%m-%d.</td>
      <td><code>2021-01-20</code></td>
    </tr>
    <tr>
      <td><code>%G</code></td>
      <td>

<span><code>DATE</code></span><br /><span><code>DATETIME</code></span><br /><span><code>TIMESTAMP</code></span><br />
</td>
      <td>
        The <a href="https://en.wikipedia.org/wiki/ISO_8601">ISO 8601</a>
        year with century as a decimal number. Each ISO year begins
        on the Monday before the first Thursday of the Gregorian calendar year.
        Note that %G and %Y may produce different results near Gregorian year
        boundaries, where the Gregorian year and ISO year can diverge.</td>
      <td><code>2021</code></td>
    </tr>
    <tr>
      <td><code>%g</code></td>
      <td>

<span><code>DATE</code></span><br /><span><code>DATETIME</code></span><br /><span><code>TIMESTAMP</code></span><br />
</td>
      <td>
        The <a href="https://en.wikipedia.org/wiki/ISO_8601">ISO 8601</a>
        year without century as a decimal number (00-99). Each ISO
        year begins on the Monday before the first Thursday of the Gregorian
        calendar year. Note that %g and %y may produce different results near
        Gregorian year boundaries, where the Gregorian year and ISO year can
        diverge.
      </td>
      <td><code>21</code></td>
    </tr>
    <tr>
      <td><code>%H</code></td>
      <td>

<span><code>TIME</code></span><br /><span><code>DATETIME</code></span><br /><span><code>TIMESTAMP</code></span><br />
</td>
      <td>The hour (24-hour clock) as a decimal number (00-23).</td>
      <td><code>21</code></td>
    </tr>
    <tr>
      <td><code>%h</code></td>
      <td>

<span><code>DATE</code></span><br /><span><code>DATETIME</code></span><br /><span><code>TIMESTAMP</code></span><br />
</td>
      <td>The abbreviated month name (English).</td>
      <td><code>Jan</code></td>
    </tr>
    <tr>
      <td><code>%I</code></td>
      <td>

<span><code>TIME</code></span><br /><span><code>DATETIME</code></span><br /><span><code>TIMESTAMP</code></span><br />
</td>
      <td>The hour (12-hour clock) as a decimal number (01-12).</td>
      <td><code>09</code></td>
    </tr>

    <tr>
      <td><code>%J</code></td>
      <td>

<span><code>DATE</code></span><br /><span><code>DATETIME</code></span><br /><span><code>TIMESTAMP</code></span><br />
</td>
      <td>
        The <a href="https://en.wikipedia.org/wiki/ISO_8601">ISO 8601</a>
        1-based day of the year (001-364 or 001-371 days). If the ISO year isn't
        set, this format element is ignored.
      </td>
      <td><code>364</code></td>
    </tr>

    <tr>
      <td><code>%j</code></td>
      <td>

<span><code>DATE</code></span><br /><span><code>DATETIME</code></span><br /><span><code>TIMESTAMP</code></span><br />
</td>
      <td>The day of the year as a decimal number (001-366).</td>
      <td><code>020</code></td>
    </tr>
    <tr>
      <td><code>%k</code></td>
      <td>

<span><code>TIME</code></span><br /><span><code>DATETIME</code></span><br /><span><code>TIMESTAMP</code></span><br />
</td>
      <td>
        The hour (24-hour clock) as a decimal number (0-23); single digits are
        preceded by a space.</td>
      <td><code>21</code></td>
    </tr>
    <tr>
      <td><code>%l</code></td>
      <td>

<span><code>TIME</code></span><br /><span><code>DATETIME</code></span><br /><span><code>TIMESTAMP</code></span><br />
</td>
      <td>
        The hour (12-hour clock) as a decimal number (1-12); single digits are
        preceded by a space.</td>
      <td><code>&nbsp;9</code></td>
    </tr>
    <tr>
      <td><code>%M</code></td>
      <td>

<span><code>TIME</code></span><br /><span><code>DATETIME</code></span><br /><span><code>TIMESTAMP</code></span><br />
</td>
      <td>The minute as a decimal number (00-59).</td>
      <td><code>47</code></td>
    </tr>
    <tr>
      <td><code>%m</code></td>
      <td>

<span><code>DATE</code></span><br /><span><code>DATETIME</code></span><br /><span><code>TIMESTAMP</code></span><br />
</td>
      <td>The month as a decimal number (01-12).</td>
      <td><code>01</code></td>
    </tr>
    <tr>
      <td><code>%n</code></td>
      <td>All</td>
      <td>A newline character.</td>
      <td></td>
    </tr>
    <tr>
      <td><code>%P</code></td>
      <td>

<span><code>TIME</code></span><br /><span><code>DATETIME</code></span><br /><span><code>TIMESTAMP</code></span><br />
</td>
      <td>
        When formatting, this is either am or pm.
        <br>
        

        
        When parsing, this can be used with am, pm, AM, or PM.
        

        
        <br>
      </td>
      <td><code>pm</code></td>
    </tr>
    <tr>
      <td><code>%p</code></td>
      <td>

<span><code>TIME</code></span><br /><span><code>DATETIME</code></span><br /><span><code>TIMESTAMP</code></span><br />
</td>
      <td>When formatting, this is either AM or PM.
      <br>When parsing, this can be used with am, pm, AM, or PM.<br></td>
      <td><code>PM</code></td>
    </tr>
    <tr>
      <td><code>%Q</code></td>
      <td>

<span><code>DATE</code></span><br /><span><code>DATETIME</code></span><br /><span><code>TIMESTAMP</code></span><br />
</td>
      <td>The quarter as a decimal number (1-4).</td>
      <td><code>1</code></td>
    </tr>
    <tr>
      <td><code>%R</code></td>
      <td>

<span><code>TIME</code></span><br /><span><code>DATETIME</code></span><br /><span><code>TIMESTAMP</code></span><br />
</td>
      <td>The time in the format %H:%M.</td>
      <td><code>21:47</code></td>
    </tr>
    <tr>
      <td><code>%S</code></td>
      <td>

<span><code>TIME</code></span><br /><span><code>DATETIME</code></span><br /><span><code>TIMESTAMP</code></span><br />
</td>
      <td>The second as a decimal number (00-60).</td>
      <td><code>00</code></td>
    </tr>
    <tr>
      <td><code>%s</code></td>
      <td>

<span><code>TIME</code></span><br /><span><code>DATETIME</code></span><br /><span><code>TIMESTAMP</code></span><br />
</td>
      <td>
        The number of seconds since 1970-01-01 00:00:00. Always overrides all
        other format elements, independent of where %s appears in the string.
        If multiple %s elements appear, then the last one takes precedence.
      </td>
      <td><code>1611179220</code></td>
    </tr>
    <tr>
      <td><code>%T</code></td>
      <td>

<span><code>TIME</code></span><br /><span><code>DATETIME</code></span><br /><span><code>TIMESTAMP</code></span><br />
</td>
      <td>The time in the format %H:%M:%S.</td>
      <td><code>21:47:00</code></td>
    </tr>
    <tr>
      <td><code>%t</code></td>
      <td>All</td>
      <td>A tab character.</td>
      <td></td>
    </tr>
    <tr>
      <td><code>%U</code></td>
      <td>

<span><code>DATE</code></span><br /><span><code>DATETIME</code></span><br /><span><code>TIMESTAMP</code></span><br />
</td>
      <td>
        The week number of the year (Sunday as the first day of the week) as a
        decimal number (00-53).
      </td>
      <td><code>03</code></td>
    </tr>
    <tr>
      <td><code>%u</code></td>
      <td>

<span><code>DATE</code></span><br /><span><code>DATETIME</code></span><br /><span><code>TIMESTAMP</code></span><br />
</td>
      <td>
        The weekday (Monday as the first day of the week) as a decimal number
        (1-7).
      </td>
      <td><code>3</code></td>
    </tr>
    <tr>
      <td><code>%V</code></td>
      <td>

<span><code>DATE</code></span><br /><span><code>DATETIME</code></span><br /><span><code>TIMESTAMP</code></span><br />
</td>
      <td>
        The <a href="https://en.wikipedia.org/wiki/ISO_week_date">ISO 8601</a>
        week number of the year (Monday as the first
        day of the week) as a decimal number (01-53).  If the week containing
        January 1 has four or more days in the new year, then it's week 1;
        otherwise it's week 53 of the previous year, and the next week is
        week 1.
      </td>
      <td><code>03</code></td>
    </tr>
    <tr>
      <td><code>%W</code></td>
      <td>

<span><code>DATE</code></span><br /><span><code>DATETIME</code></span><br /><span><code>TIMESTAMP</code></span><br />
</td>
      <td>
        The week number of the year (Monday as the first day of the week) as a
        decimal number (00-53).
      </td>
      <td><code>03</code></td>
    </tr>
    <tr>
      <td><code>%w</code></td>
      <td>

<span><code>DATE</code></span><br /><span><code>DATETIME</code></span><br /><span><code>TIMESTAMP</code></span><br />
</td>
      <td>
        The weekday (Sunday as the first day of the week) as a decimal number
        (0-6).
      </td>
      <td><code>3</code></td>
    </tr>
    <tr>
      <td><code>%X</code></td>
      <td>

<span><code>TIME</code></span><br /><span><code>DATETIME</code></span><br /><span><code>TIMESTAMP</code></span><br />
</td>
      <td>The time representation in HH:MM:SS format.</td>
      <td><code>21:47:00</code></td>
    </tr>
    <tr>
      <td><code>%x</code></td>
      <td>

<span><code>DATE</code></span><br /><span><code>DATETIME</code></span><br /><span><code>TIMESTAMP</code></span><br />
</td>
      <td>The date representation in MM/DD/YY format.</td>
      <td><code>01/20/21</code></td>
    </tr>
    <tr>
      <td><code>%Y</code></td>
      <td>

<span><code>DATE</code></span><br /><span><code>DATETIME</code></span><br /><span><code>TIMESTAMP</code></span><br />
</td>
      <td>The year with century as a decimal number.</td>
      <td><code>2021</code></td>
    </tr>
    <tr>
      <td><code>%y</code></td>
      <td>

<span><code>DATE</code></span><br /><span><code>DATETIME</code></span><br /><span><code>TIMESTAMP</code></span><br />
</td>
      <td>
        The year without century as a decimal number (00-99), with an optional
        leading zero. Can be mixed with %C. If %C isn't specified, years 00-68 are
        2000s, while years 69-99 are 1900s.
      </td>
      <td><code>21</code></td>
    </tr>
    <tr>
      <td><code>%Z</code></td>
      <td>

<span><code>TIMESTAMP</code></span><br />
</td>
      <td>The time zone name.</td>
      <td><code>UTC-5</code></td>
    </tr>
    <tr>
      <td><code>%z</code></td>
      <td>

<span><code>TIMESTAMP</code></span><br />
</td>
      <td>
        The offset from the Prime Meridian in the format +HHMM or -HHMM as
        appropriate, with positive values representing locations east of
        Greenwich.
      </td>
      <td><code>-0500</code></td>
    </tr>
    <tr>
      <td><code>%%</code></td>
      <td>All</td>
      <td>A single % character.</td>
      <td><code>%</code></td>
    </tr>
    <tr>
      <td><code>%Ez</code></td>
      <td>

<span><code>TIMESTAMP</code></span><br />
</td>
      <td>RFC 3339-compatible numeric time zone (+HH:MM or -HH:MM).</td>
      <td><code>-05:00</code></td>
    </tr>
    <tr>
      <td><code>%E&lt;number&gt;S</code></td>
      <td>

<span><code>TIME</code></span><br /><span><code>DATETIME</code></span><br /><span><code>TIMESTAMP</code></span><br />
</td>
      <td>Seconds with &lt;number&gt; digits of fractional precision.</td>
      <td><code>00.000 for %E3S</code></td>
    </tr>
    <tr>
      <td><code>%E*S</code></td>
      <td>

<span><code>TIME</code></span><br /><span><code>DATETIME</code></span><br /><span><code>TIMESTAMP</code></span><br />
</td>
      <td>Seconds with full fractional precision (a literal '*').</td>
      <td><code>00.123456</code></td>
    </tr>
    <tr>
      <td><code>%E4Y</code></td>
      <td>

<span><code>DATE</code></span><br /><span><code>DATETIME</code></span><br /><span><code>TIMESTAMP</code></span><br />
</td>
      <td>
        Four-character years (0001 ... 9999). Note that %Y produces as many
        characters as it takes to fully render the year.
      </td>
      <td><code>2021</code></td>
    </tr>
  </tbody>
</table>

Examples:

```googlesql
SELECT FORMAT_DATE("%b-%d-%Y", DATE "2008-12-25") AS formatted;

/*-------------+
 | formatted   |
 +-------------+
 | Dec-25-2008 |
 +-------------*/
```

```googlesql
SELECT
  FORMAT_DATETIME("%c", DATETIME "2008-12-25 15:30:00")
  AS formatted;

/*--------------------------+
 | formatted                |
 +--------------------------+
 | Thu Dec 25 15:30:00 2008 |
 +--------------------------*/
```

```googlesql
SELECT FORMAT_TIME("%R", TIME "15:30:00") as formatted_time;

/*----------------+
 | formatted_time |
 +----------------+
 | 15:30          |
 +----------------*/
```

```googlesql
SELECT FORMAT_TIMESTAMP("%b %Y %Ez", TIMESTAMP "2008-12-25 15:30:00+00")
  AS formatted;

/*-----------------+
 | formatted       |
 +-----------------+
 | Dec 2008 +00:00 |
 +-----------------*/
```

```googlesql
SELECT PARSE_DATE("%Y%m%d", "20081225") AS parsed;

/*------------+
 | parsed     |
 +------------+
 | 2008-12-25 |
 +------------*/
```

```googlesql
SELECT PARSE_DATETIME('%Y-%m-%d %H:%M:%S', '1998-10-18 13:45:55') AS datetime;

/*---------------------+
 | datetime            |
 +---------------------+
 | 1998-10-18 13:45:55 |
 +---------------------*/
```

```googlesql
SELECT PARSE_TIME('%I:%M:%S %p', '2:23:38 pm') AS parsed_time

/*-------------+
 | parsed_time |
 +-------------+
 | 14:23:38    |
 +-------------*/
```

```googlesql
SELECT PARSE_TIMESTAMP("%c", "Thu Dec 25 07:30:00 2008") AS parsed;

-- Display of results may differ, depending upon the environment and
-- time zone where this query was executed.
/*---------------------------------------------+
 | parsed                                      |
 +---------------------------------------------+
 | 2008-12-25 07:30:00.000 America/Los_Angeles |
 +---------------------------------------------*/
```

## Format clause for CAST 
<a id="formatting_syntax"></a>

```googlesql
format_clause:
  FORMAT format_model

format_model:
  format_string_expression
```

The format clause can be used in some [`CAST` functions][cast-functions]. You
use a format clause to provide instructions for how to conduct a
cast. For example, you could
instruct a cast to convert a sequence of bytes to a base64-encoded string
instead of a UTF-8-encoded string.

The format clause includes a format model. The format model can contain
format elements combined together as a format string.

### Format bytes as string 
<a id="format_bytes_as_string"></a>

```googlesql
CAST(bytes_expression AS STRING FORMAT format_string_expression)
```

You can cast a sequence of bytes to a string with a format element in the
format string. If the bytes can't be formatted with a
format element, an error is returned. If the sequence of bytes is `NULL`, the
result is `NULL`. Format elements are case-insensitive.

<table>
  <thead>
    <tr>
      <th width='100px'>Format element</th>
      <th width='400px'>Returns</th>
      <th>Example</th>
    </tr>
  </thead>
  <tbody>
    <tr>
      <td>HEX</td>
      <td>
        Converts a sequence of bytes into a hexadecimal string.
      </td>
      <td>
        Input: b'\x00\x01\xEF\xFF'<br />
        Output: 0001efff
      </td>
    </tr>
    <tr>
      <td>
        BASEX
      </td>
      <td>
        Converts a sequence of bytes into a
        <a href="#about_basex_encoding">BASEX</a> encoded string.
        X represents one of these numbers: 2, 8, 16, 32, 64.
      </td>
      <td>
        Input as BASE8: b'\x02\x11\x3B'<br />
        Output: 00410473
      </td>
    </tr>
    <tr>
      <td>BASE64M</td>
      <td>
        Converts a sequence of bytes into a
        <a href="#about_basex_encoding">base64</a>-encoded string based on
        <a href="https://tools.ietf.org/html/rfc2045#section-6.8">rfc 2045</a>
        for MIME. Generates a newline character ("\n") every 76 characters.
      </td>
      <td>
        Input: b'\xde\xad\xbe\xef'<br />
        Output: 3q2+7w==
      </td>
    </tr>
    <tr>
      <td>ASCII</td>
      <td>
        Converts a sequence of bytes that are ASCII values to a string. If the
        input contains bytes that aren't a valid ASCII encoding, an error
        is returned.
      </td>
      <td>
        Input: b'\x48\x65\x6c\x6c\x6f'<br />
        Output: Hello
      </td>
    </tr>
    <tr>
      <td>UTF-8</td>
      <td>
        Converts a sequence of bytes that are UTF-8 values to a string.
        If the input contains bytes that aren't a valid UTF-8 encoding,
        an error is returned.
      </td>
      <td>
        Input: b'\x24'<br />
        Output: $
      </td>
    </tr>
    <tr>
      <td>UTF8</td>
      <td>
        Same behavior as UTF-8.
      </td>
      <td>
      </td>
    </tr>
  </tbody>
</table>

**Return type**

`STRING`

**Example**

```googlesql
SELECT CAST(b'\x48\x65\x6c\x6c\x6f' AS STRING FORMAT 'ASCII') AS bytes_to_string;

/*-----------------+
 | bytes_to_string |
 +-----------------+
 | Hello           |
 +-----------------*/
```

### Format string as bytes 
<a id="format_string_as_bytes"></a>

```googlesql
CAST(string_expression AS BYTES FORMAT format_string_expression)
```

You can cast a string to bytes with a format element in the
format string. If the string can't be formatted with the
format element, an error is returned. Format elements are case-insensitive.

In the string expression, whitespace characters, such as `\n`, are ignored
if the `BASE64` or `BASE64M` format element is used.

<table>
  <thead>
    <tr>
      <th width='100px'>Format element</th>
      <th width='400px'>Returns</th>
      <th>Example</th>
    </tr>
  </thead>
  <tbody>
    <tr>
      <td>HEX</td>
      <td>
        Converts a hexadecimal-encoded string to bytes. If the input
        contains characters that aren't part of the HEX encoding alphabet
        (0~9, case-insensitive a~f), an error is returned.
      </td>
      <td>
        Input: '0001efff'<br />
        Output: b'\x00\x01\xEF\xFF'
      </td>
    </tr>
    <tr>
      <td>
        BASEX
      </td>
      <td>
        Converts a <a href="#about_basex_encoding">BASEX</a>-encoded string to
        bytes.  X represents one of these numbers: 2, 8, 16, 32, 64. An error
        is returned if the input contains characters that aren't part of the
        BASEX encoding alphabet, except whitespace characters if the
        format element is <code>BASE64</code>.
      </td>
      <td>
        Input as BASE8: '00410473'<br />
        Output: b'\x02\x11\x3B'
      </td>
    </tr>
    <tr>
      <td>BASE64M</td>
      <td>
        Converts a <a href="#about_basex_encoding">base64</a>-encoded string to
        bytes. If the input contains characters that aren't whitespace and not
        part of the base64 encoding alphabet defined at
        <a href="https://tools.ietf.org/html/rfc2045#section-6.8">rfc 2045</a>,
        an error is returned. <code>BASE64M</code> and <code>BASE64</code>
        decoding have the same behavior.
      </td>
      <td>
        Input: '3q2+7w=='<br />
        Output: b'\xde\xad\xbe\xef'
      </td>
    </tr>
    <tr>
      <td>ASCII</td>
      <td>
        Converts a string with only ASCII characters to bytes. If the input
        contains characters that aren't ASCII characters, an error is
        returned.
      </td>
      <td>
        Input: 'Hello'<br />
        Output: b'\x48\x65\x6c\x6c\x6f'
      </td>
    </tr>
    <tr>
      <td>UTF-8</td>
      <td>
        Converts a string to a sequence of UTF-8 bytes.
      </td>
      <td>
        Input: '$'<br />
        Output: b'\x24'
      </td>
    </tr>
    <tr>
      <td>UTF8</td>
      <td>
        Same behavior as UTF-8.
      </td>
      <td>
      </td>
    </tr>
  </tbody>
</table>

**Return type**

`BYTES`

**Example**

```googlesql
SELECT CAST('Hello' AS BYTES FORMAT 'ASCII') AS string_to_bytes

-- Displays the bytes output value (b'\x48\x65\x6c\x6c\x6f').

/*-------------------------+
 | string_to_bytes         |
 +-------------------------+
 | b'\x48\x65\x6c\x6c\x6f' |
 +-------------------------*/
```

### Format date and time as string 
<a id="format_date_time_as_string"></a>

You can format these date and time parts as a string:

+ [Format year part as string][format-year-as-string]
+ [Format month part as string][format-month-as-string]
+ [Format day part as string][format-day-as-string]
+ [Format hour part as string][format-hour-as-string]
+ [Format minute part as string][format-minute-as-string]
+ [Format second part as string][format-second-as-string]
+ [Format meridian indicator as string][format-meridian-as-string]
+ [Format time zone as string][format-tz-as-string]
+ [Format literal as string][format-literal-as-string]

Case matching is supported when you format some date or time parts as a string
and the output contains letters. To learn more,
see [Case matching][case-matching-date-time].

#### Case matching 
<a id="case_matching_date_time"></a>

When the output of some format element contains letters, the letter cases of
the output is matched with the letter cases of the format element,
meaning the words in the output are capitalized according to how the
format element is capitalized. This is called case matching. The rules are:

+ If the first two letters of the element are both upper case, the words in
  the output are capitalized. For example `DAY` = `THURSDAY`.
+ If the first letter of the element is upper case, and the second letter is
  lowercase, the first letter of each word in the output is capitalized and
  other letters are lowercase. For example `Day` = `Thursday`.
+ If the first letter of the element is lowercase, then all letters in the
  output are lowercase. For example, `day` = `thursday`.

#### Format year part as string 
<a id="format_year_as_string"></a>

```googlesql
CAST(expression AS STRING FORMAT format_string_expression)
```

Casts a data type that contains the year part to a string. Includes
format elements, which provide instructions for how to conduct the cast.

+ `expression`: This expression contains the data type with the year
  that you need to format.
+ `format_string_expression`: A string which contains format elements, including
  the year format element.

These data types include a year part:

+ `DATE`
+ `DATETIME`
+ `TIMESTAMP`

If `expression` or `format_string_expression` is `NULL` the return value is
`NULL`. If `format_string_expression` is an empty string, the output is an
empty string. An error is generated if a value that isn't a supported
format element appears in `format_string_expression` or `expression` doesn't
contain a value specified by a format element.

<table>
  <thead>
    <tr>
      <th width='100px'>Format element</th>
      <th width='400px'>Returns</th>
      <th>Example</th>
    </tr>
  </thead>
  <tbody>
    <tr>
      <td>YYYY</td>
      <td>Year, 4 or more digits.</td>
      <td>
        Input: DATE '2018-01-30'<br />
        Output: 2018
        <hr />
        Input: DATE '76-01-30'<br />
        Output: 0076
        <hr />
        Input: DATE '10000-01-30'<br />
        Output: 10000
      </td>
    </tr>
    <tr>
      <td>YYY</td>
      <td>Year, last 3 digits only.</td>
      <td>
        Input: DATE '2018-01-30'<br />
        Output: 018
        <hr />
        Input: DATE '98-01-30'<br />
        Output: 098
      </td>
    </tr>
    <tr>
      <td>YY</td>
      <td>Year, last 2 digits only.</td>
      <td>
        Input: DATE '2018-01-30'<br />
        Output: 18
        <hr />
        Input: DATE '8-01-30'<br />
        Output: 08
      </td>
    </tr>
    <tr>
      <td>Y</td>
      <td>Year, last digit only.</td>
      <td>
        Input: DATE '2018-01-30'<br />
        Output: 8
      </td>
    </tr>
    <tr>
      <td>RRRR</td>
      <td>Same behavior as YYYY.</td>
      <td></td>
    </tr>
    <tr>
      <td>RR</td>
      <td>Same behavior as YY.</td>
      <td></td>
    </tr>
  </tbody>
</table>

**Return type**

`STRING`

**Example**

```googlesql
SELECT CAST(DATE '2018-01-30' AS STRING FORMAT 'YYYY') AS date_time_to_string;

/*---------------------+
 | date_time_to_string |
 +---------------------+
 | 2018                |
 +---------------------*/
```

#### Format month part as string 
<a id="format_month_as_string"></a>

```googlesql
CAST(expression AS STRING FORMAT format_string_expression)
```

Casts a data type that contains the month part to a string. Includes
format elements, which provide instructions for how to conduct the cast.

+ `expression`: This expression contains the data type with the month
  that you need to format.
+ `format_string_expression`: A string which contains format elements, including
  the month format element.

These data types include a month part:

+ `DATE`
+ `DATETIME`
+ `TIMESTAMP`

If `expression` or `format_string_expression` is `NULL` the return value is
`NULL`. If `format_string_expression` is an empty string, the output is an
empty string. An error is generated if a value that isn't a supported
format element appears in `format_string_expression` or `expression` doesn't
contain a value specified by a format element.

<table>
  <thead>
    <tr>
      <th width='100px'>Format element</th>
      <th width='400px'>Returns</th>
      <th>Example</th>
    </tr>
  </thead>
  <tbody>
    <tr>
      <td>MM</td>
      <td>Month, 2 digits.</td>
      <td>
        Input: DATE '2018-01-30'<br />
        Output: 01
      </td>
    </tr>
    <tr>
      <td>MON</td>
      <td>
        Abbreviated, 3-character name of the month. The abbreviated month names
        for locale en-US are: JAN, FEB, MAR, APR, MAY, JUN, JUL, AUG, SEP, OCT,
        NOV, DEC. <a href="#case_matching_date_time">Case matching</a> is
        supported.
      </td>
      <td>
        Input: DATE '2018-01-30'<br />
        Output: JAN
      </td>
    </tr>
    <tr>
      <td>MONTH</td>
      <td>
        Name of the month.
        <a href="#case_matching_date_time">Case matching</a> is supported.
      </td>
      <td>
        Input: DATE '2018-01-30'<br />
        Output: JANUARY
      </td>
    </tr>
  </tbody>
</table>

**Return type**

`STRING`

**Example**

```googlesql
SELECT CAST(DATE '2018-01-30' AS STRING FORMAT 'MONTH') AS date_time_to_string;

/*---------------------+
 | date_time_to_string |
 +---------------------+
 | JANUARY             |
 +---------------------*/
```

#### Format day part as string 
<a id="format_day_as_string"></a>

```googlesql
CAST(expression AS STRING FORMAT format_string_expression)
```

Casts a data type that contains the day part to a string. Includes
format elements, which provide instructions for how to conduct the cast.

+ `expression`: This expression contains the data type with the day
  that you need to format.
+ `format_string_expression`: A string which contains format elements, including
  the day format element.

These data types include a day part:

+ `DATE`
+ `DATETIME`
+ `TIMESTAMP`

If `expression` or `format_string_expression` is `NULL` the return value is
`NULL`. If `format_string_expression` is an empty string, the output is an
empty string. An error is generated if a value that isn't a supported
format element appears in `format_string_expression` or `expression` doesn't
contain a value specified by a format element.

<table>
  <thead>
    <tr>
      <th width='100px'>Format element</th>
      <th width='400px'>Returns</th>
      <th>Example</th>
    </tr>
  </thead>
  <tbody>
    <tr>
      <td>DAY</td>
      <td>
        Name of the day of the week, localized. Spaces are padded on the right
        side to make the output size exactly 9.
        <a href="#case_matching_date_time">Case matching</a> is supported.
      </td>
      <td>
        Input: DATE '2020-12-31'<br />
        Output: THURSDAY
      </td>
    </tr>
    <tr>
      <td>DY</td>
      <td>
        Abbreviated, 3-character name of the weekday, localized.
        The abbreviated weekday names for locale en-US are: MON, TUE, WED, THU,
        FRI, SAT, SUN.
        <a href="#case_matching_date_time">Case matching</a> is supported.
      </td>
      <td>
        Input: DATE '2020-12-31'<br />
        Output: THU
      </td>
    </tr>
    <tr>
      <td>D</td>
      <td>Day of the week (1 to 7), starting with Sunday as 1.</td>
      <td>
        Input: DATE '2020-12-31'<br />
        Output: 4
      </td>
    </tr>
    <tr>
      <td>DD</td>
      <td>2-digit day of the month.</td>
      <td>
        Input: DATE '2018-12-02'<br />
        Output: 02
      </td>
    </tr>
    <tr>
      <td>DDD</td>
      <td>3-digit day of the year.</td>
      <td>
        Input: DATE '2018-02-03'<br />
        Output: 034
      </td>
    </tr>
  </tbody>
</table>

**Return type**

`STRING`

**Example**

```googlesql
SELECT CAST(DATE '2018-02-15' AS STRING FORMAT 'DD') AS date_time_to_string;

/*---------------------+
 | date_time_to_string |
 +---------------------+
 | 15                  |
 +---------------------*/
```

#### Format hour part as string 
<a id="format_hour_as_string"></a>

```googlesql
CAST(expression AS STRING FORMAT format_string_expression)
```

Casts a data type that contains the hour part to a string. Includes
format elements, which provide instructions for how to conduct the cast.

+ `expression`: This expression contains the data type with the hour
  that you need to format.
+ `format_string_expression`: A string which contains format elements, including
  the hour format element.

These data types include a hour part:

+ `TIME`
+ `DATETIME`
+ `TIMESTAMP`

If `expression` or `format_string_expression` is `NULL` the return value is
`NULL`. If `format_string_expression` is an empty string, the output is an
empty string. An error is generated if a value that isn't a supported
format element appears in `format_string_expression` or `expression` doesn't
contain a value specified by a format element.

<table>
  <thead>
    <tr>
      <th width='100px'>Format element</th>
      <th width='400px'>Returns</th>
      <th>Example</th>
    </tr>
  </thead>
  <tbody>
    <tr>
      <td>HH</td>
      <td>Hour of the day, 12-hour clock, 2 digits.</td>
      <td>
        Input: TIME '21:30:00'<br />
        Output: 09
      </td>
    </tr>
    <tr>
      <td>HH12</td>
      <td>
        Hour of the day, 12-hour clock.
      </td>
      <td>
        Input: TIME '21:30:00'<br />
        Output: 09
      </td>
    </tr>
    <tr>
      <td>HH24</td>
      <td>
        Hour of the day, 24-hour clock, 2 digits.
      </td>
      <td>
        Input: TIME '21:30:00'<br />
        Output: 21
      </td>
    </tr>
  </tbody>
</table>

**Return type**

`STRING`

**Examples**

```googlesql
SELECT CAST(TIME '21:30:00' AS STRING FORMAT 'HH24') AS date_time_to_string;

/*---------------------+
 | date_time_to_string |
 +---------------------+
 | 21                  |
 +---------------------*/
```

```googlesql
SELECT CAST(TIME '21:30:00' AS STRING FORMAT 'HH12') AS date_time_to_string;

/*---------------------+
 | date_time_to_string |
 +---------------------+
 | 09                  |
 +---------------------*/
```

#### Format minute part as string 
<a id="format_minute_as_string"></a>

```googlesql
CAST(expression AS STRING FORMAT format_string_expression)
```

Casts a data type that contains the minute part to a string. Includes
format elements, which provide instructions for how to conduct the cast.

+ `expression`: This expression contains the data type with the minute
  that you need to format.
+ `format_string_expression`: A string which contains format elements, including
  the minute format element.

These data types include a minute part:

+ `TIME`
+ `DATETIME`
+ `TIMESTAMP`

If `expression` or `format_string_expression` is `NULL` the return value is
`NULL`. If `format_string_expression` is an empty string, the output is an
empty string. An error is generated if a value that isn't a supported
format element appears in `format_string_expression` or `expression` doesn't
contain a value specified by a format element.

<table>
  <thead>
    <tr>
      <th width='100px'>Format element</th>
      <th width='400px'>Returns</th>
      <th>Example</th>
    </tr>
  </thead>
  <tbody>
    <tr>
      <td>MI</td>
      <td>Minute, 2 digits.</td>
      <td>
        Input: TIME '01:02:03'<br />
        Output: 02
      </td>
    </tr>
  </tbody>
</table>

**Return type**

`STRING`

**Example**

```googlesql
SELECT CAST(TIME '21:30:00' AS STRING FORMAT 'MI') AS date_time_to_string;

/*---------------------+
 | date_time_to_string |
 +---------------------+
 | 30                  |
 +---------------------*/
```

#### Format second part as string 
<a id="format_second_as_string"></a>

```googlesql
CAST(expression AS STRING FORMAT format_string_expression)
```

Casts a data type that contains the second part to a string. Includes
format elements, which provide instructions for how to conduct the cast.

+ `expression`: This expression contains the data type with the second
  that you need to format.
+ `format_string_expression`: A string which contains format elements, including
  the second format element.

These data types include a second part:

+ `TIME`
+ `DATETIME`
+ `TIMESTAMP`

If `expression` or `format_string_expression` is `NULL` the return value is
`NULL`. If `format_string_expression` is an empty string, the output is an
empty string. An error is generated if a value that isn't a supported
format element appears in `format_string_expression` or `expression` doesn't
contain a value specified by a format element.

<table>
  <thead>
    <tr>
      <th width='100px'>Format element</th>
      <th width='400px'>Returns</th>
      <th>Example</th>
    </tr>
  </thead>
  <tbody>
    <tr>
      <td>SS</td>
      <td>Seconds of the minute, 2 digits.</td>
      <td>
        Input: TIME '01:02:03'<br />
        Output: 03
      </td>
    </tr>
    <tr>
      <td>SSSSS</td>
      <td>Seconds of the day, 5 digits.</td>
      <td>
        Input: TIME '01:02:03'<br />
        Output: 03723
      </td>
    </tr>
    <tr>
      <td>FFn</td>
      <td>
        Fractional part of the second, <code>n</code> digits long.
        Replace <code>n</code> with a value from 1 to 9. For example, FF5.
        The fractional part of the second is rounded
        to fit the size of the output.
      </td>
      <td>
        Input for FF1: TIME '01:05:07.16'<br />
        Output: 1
        <hr />
        Input for FF2: TIME '01:05:07.16'<br />
        Output: 16
        <hr />
        Input for FF3: TIME '01:05:07.16'<br />
        Output: 016
      </td>
    </tr>
  </tbody>
</table>

**Return type**

`STRING`

**Examples**

```googlesql
SELECT CAST(TIME '21:30:25.16' AS STRING FORMAT 'SS') AS date_time_to_string;

/*---------------------+
 | date_time_to_string |
 +---------------------+
 | 25                  |
 +---------------------*/
```

```googlesql
SELECT CAST(TIME '21:30:25.16' AS STRING FORMAT 'FF2') AS date_time_to_string;

/*---------------------+
 | date_time_to_string |
 +---------------------+
 | 16                  |
 +---------------------*/
```

#### Format meridian indicator part as string 
<a id="format_meridian_as_string"></a>

```googlesql
CAST(expression AS STRING FORMAT format_string_expression)
```

Casts a data type that contains the meridian indicator part to a string. Includes
format elements, which provide instructions for how to conduct the cast.

+ `expression`: This expression contains the data type with the meridian indicator
  that you need to format.
+ `format_string_expression`: A string which contains format elements, including
  the meridian indicator format element.

These data types include a meridian indicator part:

+ `TIME`
+ `DATETIME`
+ `TIMESTAMP`

If `expression` or `format_string_expression` is `NULL` the return value is
`NULL`. If `format_string_expression` is an empty string, the output is an
empty string. An error is generated if a value that isn't a supported
format element appears in `format_string_expression` or `expression` doesn't
contain a value specified by a format element.

<table>
  <thead>
    <tr>
      <th width='100px'>Format element</th>
      <th width='400px'>Returns</th>
      <th>Example</th>
    </tr>
  </thead>
  <tbody>
    <tr>
      <td>A.M.</td>
      <td>
        A.M. if the time is less than 12, otherwise P.M.
        The letter case of the output is determined by the first letter case
        of the format element.
      </td>
      <td>
        Input for A.M.: TIME '01:02:03'<br />
        Output: A.M.
        <hr />
        Input for A.M.: TIME '16:02:03'<br />
        Output: P.M.
        <hr />
        Input for a.m.: TIME '01:02:03'<br />
        Output: a.m.
        <hr />
        Input for a.M.: TIME '01:02:03'<br />
        Output: a.m.
      </td>
    </tr>
    <tr>
      <td>AM</td>
      <td>
        AM if the time is less than 12, otherwise PM.
        The letter case of the output is determined by the first letter case
        of the format element.
      </td>
      <td>
        Input for AM: TIME '01:02:03'<br />
        Output: AM
        <hr />
        Input for AM: TIME '16:02:03'<br />
        Output: PM
        <hr />
        Input for am: TIME '01:02:03'<br />
        Output: am
        <hr />
        Input for aM: TIME '01:02:03'<br />
        Output: am
      </td>
    </tr>
    <tr>
      <td>P.M.</td>
      <td>Output is the same as A.M. format element.</td>
      <td></td>
    </tr>
    <tr>
      <td>PM</td>
      <td>Output is the same as AM format element.</td>
      <td></td>
    </tr>
  </tbody>
</table>

**Return type**

`STRING`

**Examples**

```googlesql
SELECT CAST(TIME '21:30:00' AS STRING FORMAT 'AM') AS date_time_to_string;
SELECT CAST(TIME '21:30:00' AS STRING FORMAT 'PM') AS date_time_to_string;

/*---------------------+
 | date_time_to_string |
 +---------------------+
 | PM                  |
 +---------------------*/
```

```googlesql
SELECT CAST(TIME '01:30:00' AS STRING FORMAT 'AM') AS date_time_to_string;
SELECT CAST(TIME '01:30:00' AS STRING FORMAT 'PM') AS date_time_to_string;

/*---------------------+
 | date_time_to_string |
 +---------------------+
 | AM                  |
 +---------------------*/
```

#### Format time zone part as string 
<a id="format_tz_as_string"></a>

```googlesql
CAST(expression AS STRING FORMAT format_string_expression)
```

Casts a data type that contains the time zone part to a string. Includes
format elements, which provide instructions for how to conduct the cast.

+ `expression`: This expression contains the data type with the time zone
  that you need to format.
+ `format_string_expression`: A string which contains format elements, including
  the time zone format element.

These data types include a time zone part:

+ `DATE`
+ `TIME`
+ `DATETIME`
+ `TIMESTAMP`

If `expression` or `format_string_expression` is `NULL` the return value is
`NULL`. If `format_string_expression` is an empty string, the output is an
empty string. An error is generated if a value that isn't a supported
format element appears in `format_string_expression` or `expression` doesn't
contain a value specified by a format element.

<table>
  <thead>
    <tr>
      <th width='100px'>Format element</th>
      <th width='400px'>Returns</th>
      <th>Example</th>
    </tr>
  </thead>
  <tbody>
    <tr>
      <td>TZH</td>
      <td>
        Hour offset for a time zone. This includes the <code>+/-</code> sign and
        2-digit hour.
      </td>
      <td>
        Inputstamp: TIMESTAMP '2008-12-25 05:30:00+00'
        Output: −08
      </td>
    </tr>
    <tr>
      <td>TZM</td>
      <td>
        Minute offset for a time zone. This includes only the 2-digit minute.
      </td>
      <td>
        Inputstamp: TIMESTAMP '2008-12-25 05:30:00+00'
        Output: 00
      </td>
    </tr>
  </tbody>
</table>

**Return type**

`STRING`

**Examples**

```googlesql
SELECT CAST(TIMESTAMP '2008-12-25 00:00:00+00:00' AS STRING FORMAT 'TZH') AS date_time_to_string;

-- Results depend upon where this query was executed.
/*---------------------+
 | date_time_to_string |
 +---------------------+
 | -08                 |
 +---------------------*/
```

```googlesql
SELECT CAST(TIMESTAMP '2008-12-25 00:00:00+00:00' AS STRING FORMAT 'TZH' AT TIME ZONE 'Asia/Kolkata')
AS date_time_to_string;

-- Because the time zone is specified, the result is always the same.
/*---------------------+
 | date_time_to_string |
 +---------------------+
 | +05                 |
 +---------------------*/
```

```googlesql
SELECT CAST(TIMESTAMP '2008-12-25 00:00:00+00:00' AS STRING FORMAT 'TZM') AS date_time_to_string;

-- Results depend upon where this query was executed.
/*---------------------+
 | date_time_to_string |
 +---------------------+
 | 00                  |
 +---------------------*/
```

```googlesql
SELECT CAST(TIMESTAMP '2008-12-25 00:00:00+00:00' AS STRING FORMAT 'TZM' AT TIME ZONE 'Asia/Kolkata')
AS date_time_to_string;

-- Because the time zone is specified, the result is always the same.
/*---------------------+
 | date_time_to_string |
 +---------------------+
 | 30                  |
 +---------------------*/
```

#### Format literal as string 
<a id="format_literal_as_string"></a>

```googlesql
CAST(expression AS STRING FORMAT format_string_expression)
```

<table>
  <thead>
    <tr>
      <th width='100px'>Format element</th>
      <th width='400px'>Returns</th>
      <th>Example</th>
    </tr>
  </thead>
  <tbody>
    <tr>
      <td>-</td>
      <td>Output is the same as the input.</td>
      <td>-</td>
    </tr>
    <tr>
      <td>.</td>
      <td>Output is the same as the input.</td>
      <td>.</td>
    </tr>
    <tr>
      <td>/</td>
      <td>Output is the same as the input.</td>
      <td>/</td>
    </tr>
    <tr>
      <td>,</td>
      <td>Output is the same as the input.</td>
      <td>,</td>
    </tr>
    <tr>
      <td>'</td>
      <td>Output is the same as the input.</td>
      <td>'</td>
    </tr>
    <tr>
      <td>;</td>
      <td>Output is the same as the input.</td>
      <td>;</td>
    </tr>
    <tr>
      <td>:</td>
      <td>Output is the same as the input.</td>
      <td>:</td>
    </tr>
    <tr>
      <td>Whitespace</td>
      <td>
        Output is the same as the input.
        Whitespace means the space character, ASCII 32. It doesn't mean
        other types of space like tab or new line. Any whitespace character
        that isn't the ASCII 32 character in the format model generates
        an error.
      </td>
      <td></td>
    </tr>
    <tr>
      <td>"text"</td>
      <td>
        Output is the value within the double quotes. To preserve a double
        quote or backslash character, use the <code>\"</code> or <code>\\</code>
        escape sequence. Other escape sequences aren't supported.
      </td>
      <td>
        Input: "abc"<br />
        Output: abc
        <hr />
        Input: "a\"b\\c"<br />
        Output: a"b\c
      </td>
    </tr>
  </tbody>
</table>

### Format string as date and time 
<a id="format_string_as_datetime"></a>

You can format a string with these date and time parts:

+ [Format string as year part][format-string-as-year]
+ [Format string as month part][format-string-as-month]
+ [Format string as day part][format-string-as-day]
+ [Format string as hour part][format-string-as-hour]
+ [Format string as minute part][format-string-as-minute]
+ [Format string as second part][format-string-as-second]
+ [Format string as meridian indicator part][format-string-as-meridian]
+ [Format string as time zone part][format-string-as-tz]
+ [Format string as literal part][format-string-as-literal]

When formatting a string with date and time parts, you must follow the
[format model rules][format-model-rules-date-time].

#### Format model rules 
<a id="format_model_rules_date_time"></a>

When casting a string to date and time parts, you must ensure the _format model_
is valid. The format model represents the elements passed into
`CAST(string_expression AS type FORMAT format_string_expression)` as the
`format_string_expression` and is validated according to the following
rules:

+ It contains at most one of each of the following parts:
  meridian indicator, year, month, day, hour.
+ A non-literal, non-whitespace format element can't appear more than once.
+ If it contains the day of year format element, `DDD`,  then it can't contain
  the month.
+ If it contains the 24-hour format element, `HH24`,  then it can't contain the
  12-hour format element or a meridian indicator.
+ If it contains the 12-hour format element, `HH12` or `HH`,  then it must also
  contain a meridian indicator.
+ If it contains a meridian indicator, then it must also contain a 12-hour
  format element.
+ If it contains the second of the day format element, `SSSSS`,  then it can't
  contain any of the following: hour, minute, second, or meridian indicator.
+ It can't contain a format element such that the value it sets doesn't exist
  in the target type. For example, an hour format element such as `HH24` can't
  appear in a string you are casting as a `DATE`.

#### Format string as year part 
<a id="format_string_as_year"></a>

```googlesql
CAST(string_expression AS type FORMAT format_string_expression)
```

Casts a string-formatted year to a data type that contains
the year part. Includes format elements, which provide instructions for how
to conduct the cast.

+ `string_expression`: This expression contains the string with the year
  that you need to format.
+ `type`: The data type to which you are casting. Must include the year
  part.
+ `format_string_expression`: A string which contains format elements, including
  the year format element. The formats elements in this string are
  defined collectively as the format model, which must follow
  [these rules][format-model-rules-date-time].

These data types include a year part:

+ `DATE`
+ `DATETIME`
+ `TIMESTAMP`

If the `YEAR` part is missing from `string_expression` and the return type
includes this part, `YEAR` is set to the current year.

An error is generated if a value that isn't a supported format element appears
in `format_string_expression` or `string_expression` doesn't contain a value
specified by a format element.

<table>
  <thead>
    <tr>
      <th width='100px'>Format element</th>
      <th width='400px'>Returns</th>
      <th>Example</th>
    </tr>
  </thead>
  <tbody>
    <tr>
      <td>YYYY</td>
      <td>
        If it's delimited, matches 1 to 5 digits. If it isn't delimited,
        matches 4 digits. Sets the year part to the matched number.
      </td>
      <td>
        Input for MM-DD-YYYY: '03-12-2018'<br />
        Output as DATE: 2018-12-03
        <hr />
        Input for YYYY-MMDD: '10000-1203'<br />
        Output as DATE: 10000-12-03
        <hr />
        Input for YYYY: '18'<br />
        Output as DATE: 2018-03-01 (Assume current date is March 23, 2021)
      </td>
    </tr>
    <tr>
      <td>YYY</td>
      <td>
        Matches 3 digits. Sets the last 3 digits of the year part to the
        matched number.
      </td>
      <td>
        Input for YYY-MM-DD: '018-12-03'<br />
        Output as DATE: 2018-12-03
        <hr />
        Input for YYY-MM-DD: '038-12-03'<br />
        Output as DATE: 2038-12-03
      </td>
    </tr>
    <tr>
      <td>YY</td>
      <td>
        Matches 2 digits. Sets the last 2 digits of the year part to the
        matched number.
      </td>
      <td>
        Input for YY-MM-DD: '18-12-03'<br />
        Output as DATE: 2018-12-03
        <hr />
        Input for YY-MM-DD: '38-12-03'<br />
        Output as DATE: 2038-12-03
      </td>
    </tr>
    <tr>
      <td>Y</td>
      <td>
        Matches 1 digit. Sets the last digit of the year part to the matched
        number.
      </td>
      <td>
        Input for Y-MM-DD: '8-12-03'<br />
        Output as DATE: 2008-12-03
      </td>
    </tr>
    <tr>
      <td>Y,YYY</td>
      <td>
        Matches the pattern of 1 to 2 digits, comma, then exactly 3 digits.
        Sets the year part to the matched number.
      </td>
      <td>
        Input for Y,YYY-MM-DD: '2,018-12-03'<br />
        Output as DATE: 2008-12-03
      </td>
    </tr>
    <tr>
    <tr>
      <td>RRRR</td>
      <td>Same behavior as YYYY.</td>
      <td></td>
    </tr>
    <tr>
      <td>RR</td>
      <td>
        <p>
          Matches 2 digits.
        </p>
        <p>
          If the 2 digits entered are between 00 and 49 and the
          last 2 digits of the current year are between 00 and 49, the
          returned year has the same first 2 digits as the current year.
          If the last 2 digits of the current year are between 50 and 99,
          the first 2 digits of the returned year is 1 greater than the first 2
          digits of the current year.
        </p>
        <p>
          If the 2 digits entered are between 50 and 99 and the
          last 2 digits of the current year are between 00 and 49, the first
          2 digits of the returned year are 1 less than the first 2 digits of
          the current year. If the last 2 digits of the current year are
          between 50 and 99, the returned year has the same first 2 digits
          as the current year.
        </p>
      </td>
      <td>
        Input for RR-MM-DD: '18-12-03'<br />
        Output as DATE: 2018-12-03 (executed in the year 2021)
        Output as DATE: 2118-12-03 (executed in the year 2050)
        <hr />
        Input for RR-MM-DD: '50-12-03'<br />
        Output as DATE: 2050-12-03 (executed in the year 2021)
        Output as DATE: 2050-12-03 (executed in the year 2050)
      </td>
    </tr>
  </tbody>
</table>

**Return type**

The data type to which the string was cast. This can be:

+ `DATE`
+ `DATETIME`
+ `TIMESTAMP`

**Examples**

```googlesql
SELECT CAST('18-12-03' AS DATE FORMAT 'YY-MM-DD') AS string_to_date

/*----------------+
 | string_to_date |
 +----------------+
 | 2018-12-03     |
 +----------------*/
```

#### Format string as month part 
<a id="format_string_as_month"></a>

```googlesql
CAST(string_expression AS type FORMAT format_string_expression)
```

Casts a string-formatted month to a data type that contains
the month part. Includes format elements, which provide instructions for how
to conduct the cast.

+ `string_expression`: This expression contains the string with the month
  that you need to format.
+ `type`: The data type to which you are casting. Must include the month
  part.
+ `format_string_expression`: A string which contains format elements, including
  the month format element. The formats elements in this string are
  defined collectively as the format model, which must follow
  [these rules][format-model-rules-date-time].

These data types include a month part:

+ `DATE`
+ `DATETIME`
+ `TIMESTAMP`

If the `MONTH` part is missing from `string_expression` and the return type
includes this part, `MONTH` is set to the current month.

An error is generated if a value that isn't a supported format element appears
in `format_string_expression` or `string_expression` doesn't contain a value
specified by a format element.

<table>
  <thead>
    <tr>
      <th width='100px'>Format element</th>
      <th width='400px'>Returns</th>
      <th>Example</th>
    </tr>
  </thead>
  <tbody>
    <tr>
      <td>MM</td>
      <td>
        Matches 2 digits. Sets the month part to the matched number.
      </td>
      <td>
        Input for MM-DD-YYYY: '03-12-2018'<br />
        Output as DATE: 2018-12-03
      </td>
    </tr>
    <tr>
      <td>MON</td>
      <td>
        Matches 3 letters. Sets the month part to the matched string interpreted
        as the abbreviated name of the month.
      </td>
      <td>
        Input for MON DD, YYYY: 'DEC 03, 2018'<br />
        Output as DATE: 2018-12-03
      </td>
    </tr>
    <tr>
      <td>MONTH</td>
      <td>
        Matches 9 letters. Sets the month part to the matched string interpreted
        as the name of the month.
      </td>
      <td>
        Input for MONTH DD, YYYY: 'DECEMBER 03, 2018'<br />
        Output as DATE: 2018-12-03
      </td>
    </tr>
  </tbody>
</table>

**Return type**

The data type to which the string was cast. This can be:

+ `DATE`
+ `DATETIME`
+ `TIMESTAMP`

**Examples**

```googlesql
SELECT CAST('DEC 03, 2018' AS DATE FORMAT 'MON DD, YYYY') AS string_to_date

/*----------------+
 | string_to_date |
 +----------------+
 | 2018-12-03     |
 +----------------*/
```

#### Format string as day part 
<a id="format_string_as_day"></a>

```googlesql
CAST(string_expression AS type FORMAT format_string_expression)
```

Casts a string-formatted day to a data type that contains
the day part. Includes format elements, which provide instructions for how
to conduct the cast.

+ `string_expression`: This expression contains the string with the day
  that you need to format.
+ `type`: The data type to which you are casting. Must include the day
  part.
+ `format_string_expression`: A string which contains format elements, including
  the day format element. The formats elements in this string are
  defined collectively as the format model, which must follow
  [these rules][format-model-rules-date-time].

These data types include a day part:

+ `DATE`
+ `DATETIME`
+ `TIMESTAMP`

If the `DAY` part is missing from `string_expression` and the return type
includes this part, `DAY` is set to `1`.

An error is generated if a value that isn't a supported format element appears
in `format_string_expression` or `string_expression` doesn't contain a value
specified by a format element.

<table>
  <thead>
    <tr>
      <th width='100px'>Format element</th>
      <th width='400px'>Returns</th>
      <th>Example</th>
    </tr>
  </thead>
  <tbody>
    <tr>
      <td>DD</td>
      <td>Matches 2 digits. Sets the day part to the matched number.</td>
      <td>
        Input for MONTH DD, YYYY: 'DECEMBER 03, 2018'<br />
        Output as DATE: 2018-12-03
      </td>
    </tr>
  </tbody>
</table>

**Return type**

The data type to which the string was cast. This can be:

+ `DATE`
+ `DATETIME`
+ `TIMESTAMP`

**Examples**

```googlesql
SELECT CAST('DECEMBER 03, 2018' AS DATE FORMAT 'MONTH DD, YYYY') AS string_to_date

/*----------------+
 | string_to_date |
 +----------------+
 | 2018-12-03     |
 +----------------*/
```

#### Format string as hour part 
<a id="format_string_as_hour"></a>

```googlesql
CAST(string_expression AS type FORMAT format_string_expression)
```

Casts a string-formatted hour to a data type that contains
the hour part. Includes format elements, which provide instructions for how
to conduct the cast.

+ `string_expression`: This expression contains the string with the hour
  that you need to format.
+ `type`: The data type to which you are casting. Must include the hour
  part.
+ `format_string_expression`: A string which contains format elements, including
  the hour format element. The formats elements in this string are
  defined collectively as the format model, which must follow
  [these rules][format-model-rules-date-time].

These data types include a hour part:

+ `TIME`
+ `DATETIME`
+ `TIMESTAMP`

If the `HOUR` part is missing from `string_expression` and the return type
includes this part, `HOUR` is set to `0`.

An error is generated if a value that isn't a supported format element appears
in `format_string_expression` or `string_expression` doesn't contain a value
specified by a format element.

<table>
  <thead>
    <tr>
      <th width='100px'>Format element</th>
      <th width='400px'>Returns</th>
      <th>Example</th>
    </tr>
  </thead>
  <tbody>
    <tr>
      <td>HH</td>
      <td>
        Matches 2 digits. If the matched number <code>n</code> is <code>12</code>,
        sets <code>temp = 0</code>; otherwise, sets <code>temp = n</code>. If
        the matched value of the A.M./P.M. format element is P.M., sets
        <code>temp = n + 12</code>. Sets the hour part to <code>temp</code>.
        A meridian indicator must be present in the format model, when
        HH is present.
      </td>
      <td>
        Input for HH:MI P.M.: '03:30 P.M.'<br />
        Output as TIME: 15:30:00
      </td>
    </tr>
    <tr>
      <td>HH12</td>
      <td>
        Same behavior as HH.
      </td>
      <td></td>
    </tr>
    <tr>
      <td>HH24</td>
      <td>
        Matches 2 digits. Sets the hour part to the matched number.
      </td>
      <td>
        Input for HH24:MI: '15:30'<br />
        Output as TIME: 15:30:00
      </td>
    </tr>
  </tbody>
</table>

**Return type**

The data type to which the string was cast. This can be:

+ `TIME`
+ `DATETIME`
+ `TIMESTAMP`

**Examples**

```googlesql
SELECT CAST('15:30' AS TIME FORMAT 'HH24:MI') AS string_to_date_time

/*---------------------+
 | string_to_date_time |
 +---------------------+
 | 15:30:00            |
 +---------------------*/
```

#### Format string as minute part 
<a id="format_string_as_minute"></a>

```googlesql
CAST(string_expression AS type FORMAT format_string_expression)
```

Casts a string-formatted minute to a data type that contains
the minute part. Includes format elements, which provide instructions for how
to conduct the cast.

+ `string_expression`: This expression contains the string with the minute
  that you need to format.
+ `type`: The data type to which you are casting. Must include the minute
  part.
+ `format_string_expression`: A string which contains format elements, including
  the minute format element. The formats elements in this string are
  defined collectively as the format model, which must follow
  [these rules][format-model-rules-date-time].

These data types include a minute part:

+ `TIME`
+ `DATETIME`
+ `TIMESTAMP`

If the `MINUTE` part is missing from `string_expression` and the return type
includes this part, `MINUTE` is set to `0`.

An error is generated if a value that isn't a supported format element appears
in `format_string_expression` or `string_expression` doesn't contain a value
specified by a format element.

<table>
  <thead>
    <tr>
      <th width='100px'>Format element</th>
      <th width='400px'>Returns</th>
      <th>Example</th>
    </tr>
  </thead>
  <tbody>
    <tr>
      <td>MI</td>
      <td>
        Matches 2 digits. Sets the minute part to the matched number.
      </td>
      <td>
        Input for HH:MI P.M.: '03:30 P.M.'<br />
        Output as TIME: 15:30:00
      </td>
    </tr>
  </tbody>
</table>

**Return type**

The data type to which the string was cast. This can be:

+ `TIME`
+ `DATETIME`
+ `TIMESTAMP`

**Examples**

```googlesql
SELECT CAST('03:30 P.M.' AS TIME FORMAT 'HH:MI P.M.') AS string_to_date_time

/*---------------------+
 | string_to_date_time |
 +---------------------+
 | 15:30:00            |
 +---------------------*/
```

#### Format string as second part 
<a id="format_string_as_second"></a>

```googlesql
CAST(string_expression AS type FORMAT format_string_expression)
```

Casts a string-formatted second to a data type that contains
the second part. Includes format elements, which provide instructions for how
to conduct the cast.

+ `string_expression`: This expression contains the string with the second
  that you need to format.
+ `type`: The data type to which you are casting. Must include the second
  part.
+ `format_string_expression`: A string which contains format elements, including
  the second format element. The formats elements in this string are
  defined collectively as the format model, which must follow
  [these rules][format-model-rules-date-time].

These data types include a second part:

+ `TIME`
+ `DATETIME`
+ `TIMESTAMP`

If the `SECOND` part is missing from `string_expression` and the return type
includes this part, `SECOND` is set to `0`.

An error is generated if a value that isn't a supported format element appears
in `format_string_expression` or `string_expression` doesn't contain a value
specified by a format element.

<table>
  <thead>
    <tr>
      <th width='100px'>Format element</th>
      <th width='400px'>Returns</th>
      <th>Example</th>
    </tr>
  </thead>
  <tbody>
    <tr>
      <td>SS</td>
      <td>
        Matches 2 digits. Sets the second part to the matched number.
      </td>
      <td>
        Input for HH:MI:SS P.M.: '03:30:02 P.M.'<br />
        Output as TIME: 15:30:02
      </td>
    </tr>
    <tr>
      <td>SSSSS</td>
      <td>
        Matches 5 digits. Sets the hour, minute and second parts by interpreting
        the matched number as the number of seconds past midnight.
      </td>
      <td>
        Input for SSSSS: '03723'<br />
        Output as TIME: 01:02:03
      </td>
    </tr>
    <tr>
      <td>FFn</td>
      <td>
        Matches <code>n</code> digits, where <code>n</code> is the number
        following FF in the format element. Sets the fractional part of the
        second part to the matched number.
      </td>
      <td>
        Input for HH24:MI:SS.FF1: '01:05:07.16'<br />
        Output as TIME: 01:05:07.2
        <hr />
        Input for HH24:MI:SS.FF2: '01:05:07.16'<br />
        Output as TIME: 01:05:07.16
        <hr />
        Input for HH24:MI:SS.FF3: 'FF3: 01:05:07.16'<br />
        Output as TIME: 01:05:07.160
      </td>
    </tr>
  </tbody>
</table>

**Return type**

The data type to which the string was cast. This can be:

+ `TIME`
+ `DATETIME`
+ `TIMESTAMP`

**Examples**

```googlesql
SELECT CAST('01:05:07.16' AS TIME FORMAT 'HH24:MI:SS.FF1') AS string_to_date_time

/*---------------------+
 | string_to_date_time |
 +---------------------+
 | 01:05:07.2          |
 +---------------------*/
```

#### Format string as meridian indicator part 
<a id="format_string_as_meridian"></a>

```googlesql
CAST(string_expression AS type FORMAT format_string_expression)
```

Casts a string-formatted meridian indicator to a data type that contains
the meridian indicator part. Includes format elements, which provide instructions for how
to conduct the cast.

+ `string_expression`: This expression contains the string with the meridian indicator
  that you need to format.
+ `type`: The data type to which you are casting. Must include the meridian indicator
  part.
+ `format_string_expression`: A string which contains format elements, including
  the meridian indicator format element. The formats elements in this string are
  defined collectively as the format model, which must follow
  [these rules][format-model-rules-date-time].

These data types include a meridian indicator part:

+ `TIME`
+ `DATETIME`
+ `TIMESTAMP`

An error is generated if a value that isn't a supported format element appears
in `format_string_expression` or `string_expression` doesn't contain a value
specified by a format element.

<table>
  <thead>
    <tr>
      <th width='100px'>Format element</th>
      <th width='400px'>Returns</th>
      <th>Example</th>
    </tr>
  </thead>
  <tbody>
    <tr>
      <td>A.M. or P.M.</td>
      <td>
        Matches using the regular expression <code>'(A|P)\.M\.'</code>.
      </td>
      <td>
        Input for HH:MI A.M.: '03:30 A.M.'<br />
        Output as TIME: 03:30:00
        <hr />
        Input for HH:MI P.M.: '03:30 P.M.'<br />
        Output as TIME: 15:30:00
        <hr />
        Input for HH:MI P.M.: '03:30 A.M.'<br />
        Output as TIME: 03:30:00
        <hr />
        Input for HH:MI A.M.: '03:30 P.M.'<br />
        Output as TIME: 15:30:00
        <hr />
        Input for HH:MI a.m.: '03:30 a.m.'<br />
        Output as TIME: 03:30:00
      </td>
    </tr>
  </tbody>
</table>

**Return type**

The data type to which the string was cast. This can be:

+ `TIME`
+ `DATETIME`
+ `TIMESTAMP`

**Examples**

```googlesql
SELECT CAST('03:30 P.M.' AS TIME FORMAT 'HH:MI A.M.') AS string_to_date_time

/*---------------------+
 | string_to_date_time |
 +---------------------+
 | 15:30:00            |
 +---------------------*/
```

#### Format string as time zone part 
<a id="format_string_as_tz"></a>

```googlesql
CAST(string_expression AS type FORMAT format_string_expression)
```

Casts a string-formatted time zone to a data type that contains
the time zone part. Includes format elements, which provide instructions for how
to conduct the cast.

+ `string_expression`: This expression contains the string with the time zone
  that you need to format.
+ `type`: The data type to which you are casting. Must include the time zone
  part.
+ `format_string_expression`: A string which contains format elements, including
  the time zone format element. The formats elements in this string are
  defined collectively as the format model, which must follow
  [these rules][format-model-rules-date-time].

These data types include a time zone part:

+ `DATE`
+ `TIME`
+ `DATETIME`
+ `TIMESTAMP`

An error is generated if a value that isn't a supported format element appears
in `format_string_expression` or `string_expression` doesn't contain a value
specified by a format element.

<table>
  <thead>
    <tr>
      <th width='100px'>Format element</th>
      <th width='400px'>Returns</th>
      <th>Example</th>
    </tr>
  </thead>
  <tbody>
    <tr>
      <td>TZH</td>
      <td>
        Matches using the regular expression <code>'(\+|\-| )[0-9]{2}'</code>.
        Sets the time zone and hour parts to the matched sign and number.
        Sets the time zone sign to be the first letter of the matched string.
        The number 2 means matching up to 2 digits for non-exact matching, and
        exactly 2 digits for exact matching.
      </td>
      <td>
        Input for YYYY-MM-DD HH:MI:SSTZH: '2008-12-25 05:30:00-08'<br />
        Output as TIMESTAMP: 2008-12-25 05:30:00-08
      </td>
    </tr>
    <tr>
      <td>TZM</td>
      <td>
        Matches 2 digits. Let <code>n</code> be the matched number. If the
        time zone sign is the minus sign, sets the time zone minute part to
        <code>-n</code>. Otherwise, sets the time zone minute part to
        <code>n</code>.
      </td>
      <td>
        Input for YYYY-MM-DD HH:MI:SSTZH: '2008-12-25 05:30:00+05.30'<br />
        Output as TIMESTAMP: 2008-12-25 05:30:00+05.30
      </td>
    </tr>
  </tbody>
</table>

**Return type**

The data type to which the string was cast. This can be:

+ `DATE`
+ `TIME`
+ `DATETIME`
+ `TIMESTAMP`

**Examples**

```googlesql
SELECT CAST('2020.06.03 00:00:53+00' AS TIMESTAMP FORMAT 'YYYY.MM.DD HH:MI:SSTZH') AS string_to_date_time

/*----------------------------+
 | as_timestamp               |
 +----------------------------+
 | 2020-06-03 00:00:53.110+00 |
 +----------------------------*/
```

#### Format string as literal 
<a id="format_string_as_literal"></a>

```googlesql
CAST(string_expression AS data_type FORMAT format_string_expression)
```

<table>
  <thead>
    <tr>
      <th width='100px'>Format element</th>
      <th width='400px'>Returns</th>
      <th>Example</th>
    </tr>
  </thead>
  <tbody>
    <tr>
      <td>-</td>
      <td>Output is the same as the input.</td>
      <td></td>
    </tr>
    <tr>
      <td>.</td>
      <td>Output is the same as the input.</td>
      <td>.</td>
    </tr>
    <tr>
      <td>/</td>
      <td>Output is the same as the input.</td>
      <td>/</td>
    </tr>
    <tr>
      <td>,</td>
      <td>Output is the same as the input.</td>
      <td>,</td>
    </tr>
    <tr>
      <td>'</td>
      <td>Output is the same as the input.</td>
      <td>'</td>
    </tr>
    <tr>
      <td>;</td>
      <td>Output is the same as the input.</td>
      <td>;</td>
    </tr>
    <tr>
      <td>:</td>
      <td>Output is the same as the input.</td>
      <td>:</td>
    </tr>
    <tr>
      <td>Whitespace</td>
      <td>
        A consecutive sequence of one or more spaces in the format model
        is matched with one or more consecutive Unicode whitespace characters
        in the input. Space means the ASCII 32 space character.
        It doesn't mean the general whitespace such as a tab or new line.
        Any whitespace character that isn't the ASCII 32 character in the
        format model generates an error.
      </td>
      <td></td>
    </tr>
    <tr>
      <td>"text"</td>
      <td>
        Output generated by the format element in formatting, using this
        regular expression, with <code>s</code> representing the string input:
        <code>regex.escape(s)</code>.
      </td>
      <td>
        Input: "abc"<br />
        Output: abc
        <hr />
        Input: "a\"b\\c"<br />
        Output: a"b\c
      </td>
    </tr>
  </tbody>
</table>

### Format numeric type as string 
<a id="format_numeric_type_as_string"></a>

```googlesql
CAST(numeric_expression AS STRING FORMAT format_string_expression)
```

You can cast a [numeric type][numeric-types] to a string by combining the
following format elements:

- [Digits][format-digits]
- [Decimal point][format-decimal-point]
- [Sign][format-sign]
- [Currency symbol][format-currency-symbol]
- [Group separator][format-group-separator]
- [Other format elements][format-other-elements]

Except for the exponent format element (`EEEE`), all of the format elements
generate a fixed number of characters in the output, and the output is aligned
by the decimal point. The first character outputs a `-` for negative numbers;
otherwise a space. To suppress blank characters and trailing zeroes, use the
`FM` flag.

**Return type**

`STRING`

**Example**

```googlesql
SELECT input, CAST(input AS STRING FORMAT '$999,999.999') AS output
FROM UNNEST([1.2, 12.3, 123.456, 1234.56, -12345.678, 1234567.89]) AS input

/*------------+---------------+
 |   input    |    output     |
 +------------+---------------+
 |        1.2 |        $1.200 |
 |       12.3 |       $12.300 |
 |    123.456 |      $123.456 |
 |    1234.56 |    $1,234.560 |
 | -12345.678 |  -$12,345.678 |
 | 1234567.89 |  $###,###.### |
 +------------+---------------*/
```

#### Format digits as string 
<a id="format_digits"></a>

The following format elements output digits. If there aren't enough
digit format elements to represent the input, all digit format elements are
replaced with `#` in the output.
If there are no sign format elements, one extra space is reserved for the sign.
For example, if the input is <code>12</code> and the format string is
<code>'99'</code>, then the output is <code>' 12'</code>, with a length of three
characters.

<table>
  <thead>
    <tr>
      <th width='100px'>Format element</th>
      <th width='400px'>Returns</th>
      <th>Example</th>
    </tr>
  </thead>
  <tbody>
    <tr>
      <td>0</td>
      <td>A decimal digit. Leading and trailing zeros are included.</td>
      <td>
        Input: <code>12</code> <br/>
        Format: <code>'000'</code> <br/>
        Output: <code>' 012'</code>
        <hr/>
        Input: <code>12</code> <br/>
        Format: <code>'000.000'</code> <br/>
        Output: <code>' 012.000'</code>
        <hr/>
        Input: <code>-12</code> <br/>
        Format: <code>'000.000'</code> <br/>
        Output: <code>'-012.000'</code>
      </td>
    </tr>
    <tr>
      <td>9</td>
      <td>A decimal digit. Leading zeros are replaced with spaces. Trailing
        zeros are included.</td>
      <td>
        Input: <code>12</code> <br/>
        Format: <code>'999'</code> <br/>
        Output: <code>'&nbsp;&nbsp;12'</code>
        <hr/>
        Input: <code>12</code> <br/>
        Format: <code>'999.999'</code> <br/>
        Output: <code>'&nbsp;&nbsp;12.000'</code>
      </td>
    </tr>
    <tr>
      <td>X or x</td>
      <td><p>A hexadecimal digit. Can't appear with other format elements
        except 0, FM, and the sign format elements. The maximum number of
        hexadecimal digits in the format string is 16.</p>
        <p>X generates uppercase letters and x generates lowercase letters.
        </p>
        <p>When 0 is combined with the hexadecimal format element, the letter
        generated by 0 matches the case of the next X or x element. If
        there is no subsequent X or x, then 0 generates an uppercase
        letter.</p>
      </td>
      <td>
        Input: <code>43981</code> <br/>
        Format: <code>'XXXX'</code> <br/>
        Output: <code>' ABCD'</code>
        <hr />
        Input: <code>43981</code> <br/>
        Format: <code>'xxxx'</code> <br/>
        Output: <code>' abcd'</code>
        <hr />
        Input: <code>43981</code> <br/>
        Format: <code>'0X0x'</code> <br/>
        Output: <code>' ABcd'</code>
        <hr />
        Input: <code>43981</code> <br/>
        Format: <code>'0000000X'</code> <br/>
        Output: <code>' 0000ABCD'</code>
      </td>
    </tr>
  </tbody>
</table>

**Return type**

`STRING`

**Example**

```googlesql
SELECT
  CAST(12 AS STRING FORMAT '999') as a,
  CAST(-12 AS STRING FORMAT '999') as b;

/*------+------+
 | a    | b    |
 +------+------+
 |   12 |  -12 |
 +------+------*/
```

#### Format decimal point as string 
<a id="format_decimal_point"></a>

The following format elements output a decimal point. These format elements are
mutually exclusive. At most one can appear in the format string.

<table>
  <thead>
    <tr>
      <th width='100px'>Format element</th>
      <th width='400px'>Returns</th>
      <th>Example</th>
    </tr>
  </thead>
  <tbody>
    <tr>
      <td>. (period)</td>
      <td>Decimal point.</td>
      <td>
        Input: <code>123.58</code> <br/>
        Format: <code>'999.999'</code> <br/>
        Output: <code>' 123.580'</code>
    </tr>
    <tr>
      <td>D</td>
      <td>The decimal point of the current locale.</td>
      <td>
        Input: <code>123.58</code> <br/>
        Format: <code>'999D999'</code> <br/>
        Output: <code>' 123.580'</code>
    </tr>
  </tbody>
</table>

**Return type**

`STRING`

**Example**

```googlesql
SELECT CAST(12.5 AS STRING FORMAT '99.99') as a;

/*--------+
 | a      |
 +--------+
 |  12.50 |
 +--------*/
```

#### Format sign as string 
<a id="format_sign"></a>

The following format elements output the sign (+/-). These format elements are
mutually exclusive. At most one can appear in the format string.

If there are no sign format elements, one extra space is reserved for the sign.
For example, if the input is <code>12</code> and the format string is
<code>'99'</code>, then the output is <code>' 12'</code>, with a length of three
characters.

The sign appears before the number. If the format model includes a currency
symbol element, then the sign appears before the currency symbol.

<table>
  <thead>
    <tr>
      <th width='100px'>Format element</th>
      <th width='400px'>Returns</th>
      <th>Example</th>
    </tr>
  </thead>
  <tbody>
    <tr>
      <td>S</td>
      <td>Explicit sign. Outputs <code>+</code> for positive numbers and
      <code>-</code> for negative numbers. The position in the output is
      anchored to the number. <code>NaN</code> and <code>0</code>
      will not be signed.</td>
      <td>
        Input: <code>-12</code> <br/>
        Format: <code>'S9999'</code> <br />
        Output: <code>'&nbsp;&nbsp;-12'</code>
        <hr />
        Input: <code>-12</code> <br/>
        Format: <code>'9999S'</code> <br />
        Output: <code>'&nbsp;&nbsp;12-'</code>
      </td>
    </tr>
    <tr>
      <td>MI</td>
      <td>Explicit sign. Outputs a space for positive numbers and <code>-</code>
        for negative numbers. This element can only appear in the last position.
      </td>
      <td>
        Input: <code>12</code> <br/>
        Format: <code>'9999MI'</code> <br />
        Output: <code>'&nbsp;&nbsp;12 '</code>
        <hr />
        Input: <code>-12</code> <br/>
        Format: <code>'9999MI'</code> <br />
        Output: <code>'&nbsp;&nbsp;12-'</code>
      </td>
    </tr>
    <tr>
      <td>PR</td>
      <td>For negative numbers, the value is enclosed in angle brackets. For
        positive numbers, the value is returned with a leading and trailing
        space. This element can only appear in the last position.
      </td>
      <td>
        Input: <code>12</code> <br/>
        Format: <code>'9999PR'</code> <br />
        Output: <code>'&nbsp;&nbsp;&nbsp;12 '</code>
        <hr />
        Input: <code>-12</code> <br/>
        Format: <code>'9999PR'</code> <br />
        Output: <code>'&nbsp;&nbsp;&lt;12&gt;'</code>
      </td>
    </tr>
  </tbody>
</table>

**Return type**

`STRING`

**Example**

```googlesql
SELECT
  CAST(12 AS STRING FORMAT 'S99') as a,
  CAST(-12 AS STRING FORMAT 'S99') as b;

/*-----+-----+
 | a   | b   |
 +-----+-----+
 | +12 | -12 |
 +-----+-----*/
```

#### Format currency symbol as string 
<a id="format_currency_symbol"></a>

The following format elements output a currency symbol. These format elements
are mutually exclusive. At most one can appear in the format string. In the
output, the currency symbol appears before the first digit or decimal point.

<table>
  <thead>
    <tr>
      <th width='100px'>Format element</th>
      <th width='400px'>Returns</th>
      <th>Example</th>
    </tr>
  </thead>
  <tbody>
    <tr>
      <td>$</td>
      <td>Dollar sign ($).</td>
      <td>
        Input: <code>-12</code> <br/>
        Format: <code>'$999'</code> <br/>
        Output: <code>' -$12'</code>
      </td>
    </tr>
    <tr>
      <td>C or c</td>
      <td>The ISO-4217 currency code of the current locale.</td>
      <td>
        Input: <code>-12</code> <br/>
        Format: <code>'C999'</code> <br/>
        Output: <code>' -USD12'</code>
        <hr/>
        Input: <code>-12</code> <br/>
        Format: <code>'c999'</code> <br/>
        Output: <code>' -usd12'</code>
      </td>
    </tr>
    <tr>
      <td>L</td>
      <td>The currency symbol of the current locale.</td>
      <td>
        Input: <code>-12</code> <br/>
        Format: <code>'L999'</code> <br/>
        Output: <code>' -$12'</code>
      </td>
    </tr>
  </tbody>
</table>

**Return type**

`STRING`

**Example**

```googlesql
SELECT
  CAST(12 AS STRING FORMAT '$99') as a,
  CAST(-12 AS STRING FORMAT '$99') as b;

/*------+------+
 | a    | b    |
 +------+------+
 |  $12 | -$12 |
 +------+------*/
```

#### Format group separator as string 
<a id="format_group_separator"></a>

The following format elements output a group separator.

<table>
  <thead>
    <tr>
      <th width='100px'>Format element</th>
      <th width='400px'>Returns</th>
      <th>Example</th>
    </tr>
  </thead>
  <tbody>
    <tr>
      <td>, (comma)</td>
      <td>Group separator.</td>
      <td>
        Input: <code>12345</code> <br/>
        Format: <code>'999,999'</code> <br/>
        Output: <code>'&nbsp;&nbsp;12,345'</code>
    </tr>
    <tr>
      <td>G</td>
      <td>The group separator point of the current locale.</td>
      <td>
        Input: <code>12345</code> <br/>
        Format: <code>'999G999'</code> <br/>
        Output: <code>'&nbsp;&nbsp;12,345'</code>
    </tr>
  </tbody>
</table>

**Return type**

`STRING`

**Example**

```googlesql
SELECT CAST(1234 AS STRING FORMAT '999,999') as a;

/*----------+
 | a        |
 +----------+
 |    1,234 |
 +----------*/
```

#### Other numeric format elements 
<a id="format_other_elements"></a>

<table>
  <thead>
    <tr>
      <th width='100px'>Format element</th>
      <th width='400px'>Returns</th>
      <th>Example</th>
    </tr>
  </thead>
  <tbody>
    <tr>
      <td>B</td>
      <td>Outputs spaces when the integer part is zero. If the integer part of
        the number is 0, then the following format elements generate spaces in
        the output: digits (9, X, 0), decimal point, group separator, currency,
        sign, and exponent.</td>
      <td>
        Input: <code>0.23</code> <br/>
        Format: <code>'B99.999S'</code> <br/>
        Output: <code>'&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;'</code>
        <hr />
        Input: <code>1.23</code> <br/>
        Format: <code>'B99.999S'</code> <br/>
        Output: <code>' 1.230+'</code>
    </tr>
    <tr>
      <td>EEEE</td>
      <td>Outputs the exponent part of the value in scientific notation. If
        the exponent value is between -99 and 99, the output is four characters.
        Otherwise, the minimum number of digits is used in the output.
      </td>
      <td>
        Input: <code>20</code> <br/>
        Format: <code>'9.99EEEE'</code> <br/>
        Output: <code>' 2.0E+01'</code>
        <hr />
        Input: <code>299792458</code> <br/>
        Format: <code>'S9.999EEEE'</code> <br/>
        Output: <code>'+2.998E+08'</code>
    </tr>
    <tr>
      <td>FM</td>
      <td>Removes all spaces and trailing zeroes from the output. You can use
      this element to suppress spaces and trailing zeroes that are generated
      by other format elements.</td>
      <td>
        Input: <code>12.5</code> <br/>
        Format: <code>'999999.000FM'</code> <br/>
        Output: <code>'12.5'</code>
    </tr>
    <tr>
      <td>RN</td>
      <td>Returns the value as Roman numerals, rounded to the nearest integer.
        The input must be between 1 and 3999. The output is padded with spaces
        to the left to a length of 15. This element can't be used with other
        format elements except <code>FM</code>.
      </td>
      <td>
        Input: <code>2021</code> <br/>
        Format: <code>'RN'</code> <br/>
        Output: <code>'&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;MMXXI'</code>
      </td>
    </tr>
    <tr>
      <td>V</td>
      <td>The input value is multiplied by 10^n, where n is the number of 9s
        after the <code>V</code>. This element can't be used with a decimal
        point or exponent format element.
      </td>
      <td>
        Input: <code>23.5</code> <br/>
        Format: <code>'S000V00'</code> <br/>
        Output: <code>'+02350'</code>
      </td>
    </tr>
  </tbody>
</table>

**Return type**

`STRING`

**Example**

```googlesql
SELECT CAST(-123456 AS STRING FORMAT '9.999EEEE') as a;"

/*------------+
 | a          |
 +------------+
 | -1.235E+05 |
 +------------*/
```

### About BASE encoding 
<a id="about_basex_encoding"></a>

BASE encoding translates binary data in string format into a radix-X
representation.

If X is 2, 8, or 16, Arabic numerals 0–9 and the Latin letters
a–z are used in the encoded string. So for example, BASE16/Hexadecimal encoding
results contain 0~9 and a~f.

If X is 32 or 64, the default character tables are defined in
[rfc 4648][rfc-4648]. When you decode a BASE string where X is 2, 8, or 16,
the Latin letters in the input string are case-insensitive. For example, both
"3a" and "3A" are valid input strings for BASE16/Hexadecimal decoding, and
will output the same result.

[format-date]: https://github.com/google/googlesql/blob/master/docs/date_functions.md#format_date

[parse-date]: https://github.com/google/googlesql/blob/master/docs/date_functions.md#parse_date

[format-time]: https://github.com/google/googlesql/blob/master/docs/time_functions.md#format_time

[parse-time]: https://github.com/google/googlesql/blob/master/docs/time_functions.md#parse_time

[format-datetime]: https://github.com/google/googlesql/blob/master/docs/datetime_functions.md#format_datetime

[parse-datetime]: https://github.com/google/googlesql/blob/master/docs/datetime_functions.md#parse_datetime

[format-timestamp]: https://github.com/google/googlesql/blob/master/docs/timestamp_functions.md#format_timestamp

[parse-timestamp]: https://github.com/google/googlesql/blob/master/docs/timestamp_functions.md#parse_timestamp

[rfc-4648]: https://tools.ietf.org/html/rfc4648#section-3.3

[case-matching-date-time]: #case_matching_date_time

[format-year-as-string]: #format_year_as_string

[format-month-as-string]: #format_month_as_string

[format-day-as-string]: #format_day_as_string

[format-hour-as-string]: #format_hour_as_string

[format-minute-as-string]: #format_minute_as_string

[format-second-as-string]: #format_second_as_string

[format-meridian-as-string]: #format_meridian_as_string

[format-tz-as-string]: #format_tz_as_string

[format-literal-as-string]: #format_literal_as_string

[format-model-rules-date-time]: #format_model_rules_date_time

[format-string-as-year]: #format_string_as_year

[format-string-as-month]: #format_string_as_month

[format-string-as-day]: #format_string_as_day

[format-string-as-hour]: #format_string_as_hour

[format-string-as-minute]: #format_string_as_minute

[format-string-as-second]: #format_string_as_second

[format-string-as-meridian]: #format_string_as_meridian

[format-string-as-tz]: #format_string_as_tz

[format-string-as-literal]: #format_string_as_literal

[format-digits]: #format_digits

[format-decimal-point]: #format_decimal_point

[format-sign]: #format_sign

[format-currency-symbol]: #format_currency_symbol

[format-group-separator]: #format_group_separator

[format-other-elements]: #format_other_elements

[numeric-types]: https://github.com/google/googlesql/blob/master/docs/data-types.md#numeric_types

[cast-functions]: https://github.com/google/googlesql/blob/master/docs/conversion_functions.md#cast

