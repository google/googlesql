

<!-- mdlint off(WHITESPACE_LINE_LENGTH) -->

# Hash functions

GoogleSQL supports the following hash functions.

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
  <td><a href="https://github.com/google/googlesql/blob/master/docs/hash_functions.md#farm_fingerprint"><code>FARM_FINGERPRINT</code></a>
</td>
  <td>
    Computes the fingerprint of a <code>STRING</code> or
    <code>BYTES</code> value, using the FarmHash Fingerprint64 algorithm.
  </td>
</tr>

<tr>
  <td><a href="https://github.com/google/googlesql/blob/master/docs/hash_functions.md#highway_fingerprint128"><code>HIGHWAY_FINGERPRINT128</code></a>
</td>
  <td>
    Computes the 128-bit fingerprint of a <code>STRING</code> or
    <code>BYTES</code> value, using the <a href="/third_party/highwayhash/README.md?cl=head">HighwayHash algorithm</a>
.
  </td>
</tr>

<tr>
  <td><a href="https://github.com/google/googlesql/blob/master/docs/hash_functions.md#md5"><code>MD5</code></a>
</td>
  <td>
    Computes the hash of a <code>STRING</code> or
    <code>BYTES</code> value, using the MD5 algorithm.
  </td>
</tr>

<tr>
  <td><a href="https://github.com/google/googlesql/blob/master/docs/hash_functions.md#sha1"><code>SHA1</code></a>
</td>
  <td>
    Computes the hash of a <code>STRING</code> or
    <code>BYTES</code> value, using the SHA-1 algorithm.
  </td>
</tr>

<tr>
  <td><a href="https://github.com/google/googlesql/blob/master/docs/hash_functions.md#sha256"><code>SHA256</code></a>
</td>
  <td>
    Computes the hash of a <code>STRING</code> or
    <code>BYTES</code> value, using the SHA-256 algorithm.
  </td>
</tr>

<tr>
  <td><a href="https://github.com/google/googlesql/blob/master/docs/hash_functions.md#sha512"><code>SHA512</code></a>
</td>
  <td>
    Computes the hash of a <code>STRING</code> or
    <code>BYTES</code> value, using the SHA-512 algorithm.
  </td>
</tr>

  </tbody>
</table>

## `FARM_FINGERPRINT`

```
FARM_FINGERPRINT(value)
```

**Description**

Computes the fingerprint of the `STRING` or `BYTES` input using the
`Fingerprint64` function from the
[open-source FarmHash library][hash-link-to-farmhash-github]. The output
of this function for a particular input will never change.

**Return type**

INT64

**Examples**

```googlesql
WITH example AS (
  SELECT 1 AS x, "foo" AS y, true AS z UNION ALL
  SELECT 2 AS x, "apple" AS y, false AS z UNION ALL
  SELECT 3 AS x, "" AS y, true AS z
)
SELECT
  *,
  FARM_FINGERPRINT(CONCAT(CAST(x AS STRING), y, CAST(z AS STRING)))
    AS row_fingerprint
FROM example;
/*---+-------+-------+----------------------+
 | x | y     | z     | row_fingerprint      |
 +---+-------+-------+----------------------+
 | 1 | foo   | true  | -1541654101129638711 |
 | 2 | apple | false | 2794438866806483259  |
 | 3 |       | true  | -4880158226897771312 |
 +---+-------+-------+----------------------*/
```

[hash-link-to-farmhash-github]: https://github.com/google/farmhash

## `HIGHWAY_FINGERPRINT128`

```
HIGHWAY_FINGERPRINT128(input STRING[, key BYTES]) -> BYTES
```

```
HIGHWAY_FINGERPRINT128(input BYTES[, key BYTES]) -> BYTES
```

**Description**

Computes the 128-bit fingerprint of a `STRING` or `BYTES` input value using the
[HighwayHash algorithm][highwayhash]. The string version treats the input as an
array of bytes. The optional `key` argument adds unpredictability to the hash
output for security.

This function returns 16 bytes.

**Return type**

BYTES

**Arguments**

*   `input`: A `STRING` or `BYTES` value to be hashed.
*   `key`: (Optional) A `BYTES` value that makes the hash output unpredictable
    to an attacker, preventing hash-flooding denial-of-service attacks. This key
    must be exactly 32 bytes (256 bits) long.

**Examples**

```googlesql
-- Without `key`

SELECT HIGHWAY_FINGERPRINT128('Hello World')

/*------------------------------------------------------------+
 | \xa3\xc3\xb8\xb4\xfeo\x88W\xbf\x88\xae\x89\xe4\xab\xd3\x8e |
 +------------------------------------------------------------*/

```

```googlesql
-- With `key`

SELECT HIGHWAY_FINGERPRINT128(
    'Hello World',
    -- 32-byte key
    b'abcdefghijklmnopqrstuvwxyzabcdef'
)

/*------------------------------------------+
 | o\x9c\xbc:tQ{p\xa5}Y\x0c\xc3\xbe\x04\xcb |
 +------------------------------------------*/

```

[highwayhash]: /third_party/highwayhash/README.md?cl=head

## `MD5`

```
MD5(input)
```

**Description**

Computes the hash of the input using the
[MD5 algorithm][hash-link-to-md5-wikipedia]. The input can either be
`STRING` or `BYTES`. The string version treats the input as an array of bytes.

This function returns 16 bytes.

Warning: MD5 is no longer considered secure.
For increased security use another hashing function.

**Return type**

`BYTES`

**Example**

```googlesql
SELECT MD5("Hello World") as md5;

/*-------------------------------------------------+
 | md5                                             |
 +-------------------------------------------------+
 | \xb1\n\x8d\xb1d\xe0uA\x05\xb7\xa9\x9b\xe7.?\xe5 |
 +-------------------------------------------------*/
```

[hash-link-to-md5-wikipedia]: https://en.wikipedia.org/wiki/MD5

## `SHA1`

```
SHA1(input)
```

**Description**

Computes the hash of the input using the
[SHA-1 algorithm][hash-link-to-sha-1-wikipedia]. The input can either be
`STRING` or `BYTES`. The string version treats the input as an array of bytes.

This function returns 20 bytes.

Warning: SHA1 is no longer considered secure.
For increased security, use another hashing function.

**Return type**

`BYTES`

**Example**

```googlesql
SELECT SHA1("Hello World") as sha1;

/*-----------------------------------------------------------+
 | sha1                                                      |
 +-----------------------------------------------------------+
 | \nMU\xa8\xd7x\xe5\x02/\xabp\x19w\xc5\xd8@\xbb\xc4\x86\xd0 |
 +-----------------------------------------------------------*/
```

[hash-link-to-sha-1-wikipedia]: https://en.wikipedia.org/wiki/SHA-1

## `SHA256`

```
SHA256(input)
```

**Description**

Computes the hash of the input using the
[SHA-256 algorithm][hash-link-to-sha-2-wikipedia]. The input can either be
`STRING` or `BYTES`. The string version treats the input as an array of bytes.

This function returns 32 bytes.

**Return type**

`BYTES`

**Example**

```googlesql
SELECT SHA256("Hello World") as sha256;
```

[hash-link-to-sha-2-wikipedia]: https://en.wikipedia.org/wiki/SHA-2

## `SHA512`

```
SHA512(input)
```

**Description**

Computes the hash of the input using the
[SHA-512 algorithm][hash-link-to-sha-2-wikipedia]. The input can either be
`STRING` or `BYTES`. The string version treats the input as an array of bytes.

This function returns 64 bytes.

**Return type**

`BYTES`

**Example**

```googlesql
SELECT SHA512("Hello World") as sha512;
```

[hash-link-to-sha-2-wikipedia]: https://en.wikipedia.org/wiki/SHA-2

