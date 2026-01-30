

<!-- mdlint off(WHITESPACE_LINE_LENGTH) -->

# Protocol buffer functions

GoogleSQL supports the following protocol buffer functions.

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
  <td><a href="https://github.com/google/googlesql/blob/master/docs/protocol_buffer_functions.md#enum_value_descriptor_proto"><code>ENUM_VALUE_DESCRIPTOR_PROTO</code></a>
</td>
  <td>
    Gets the enum value descriptor proto
    (<code>proto2.EnumValueDescriptorProto</code>) for an enum.
  </td>
</tr>

<tr>
  <td><a href="https://github.com/google/googlesql/blob/master/docs/protocol_buffer_functions.md#proto_extract"><code>EXTRACT</code></a>
</td>
  <td>
    Extracts a value or metadata from a protocol buffer.
  </td>
</tr>

<tr>
  <td><a href="https://github.com/google/googlesql/blob/master/docs/protocol_buffer_functions.md#filter_fields"><code>FILTER_FIELDS</code></a>
</td>
  <td>
    Removed unwanted fields from a protocol buffer.
  </td>
</tr>

<tr>
  <td><a href="https://github.com/google/googlesql/blob/master/docs/protocol_buffer_functions.md#from_proto"><code>FROM_PROTO</code></a>
</td>
  <td>
    Converts a protocol buffer value into GoogleSQL value.
    
  </td>
</tr>

<tr>
  <td><a href="https://github.com/google/googlesql/blob/master/docs/protocol_buffer_functions.md#proto_default_if_null"><code>PROTO_DEFAULT_IF_NULL</code></a>
</td>
  <td>
    Produces the default protocol buffer field value if the
    protocol buffer field is <code>NULL</code>. Otherwise, returns the
    protocol buffer field value.
  </td>
</tr>

<tr>
  <td><a href="https://github.com/google/googlesql/blob/master/docs/protocol_buffer_functions.md#proto_map_contains_key"><code>PROTO_MAP_CONTAINS_KEY</code></a>
</td>
  <td>
    Checks if a protocol buffer map field contains a given key.
  </td>
</tr>

<tr>
  <td><a href="https://github.com/google/googlesql/blob/master/docs/protocol_buffer_functions.md#proto_modify_map"><code>PROTO_MODIFY_MAP</code></a>
</td>
  <td>
    Modifies a protocol buffer map field.
  </td>
</tr>

<tr>
  <td><a href="https://github.com/google/googlesql/blob/master/docs/protocol_buffer_functions.md#replace_fields"><code>REPLACE_FIELDS</code></a>
</td>
  <td>
    Replaces the values in one or more protocol buffer fields.
  </td>
</tr>

<tr>
  <td><a href="https://github.com/google/googlesql/blob/master/docs/protocol_buffer_functions.md#to_proto"><code>TO_PROTO</code></a>
</td>
  <td>
    Converts a GoogleSQL value into a protocol buffer value.
    
  </td>
</tr>

  </tbody>
</table>

## `ENUM_VALUE_DESCRIPTOR_PROTO`

```googlesql
ENUM_VALUE_DESCRIPTOR_PROTO(proto_enum)
```

**Description**

Gets the enum value descriptor proto
(`proto2.EnumValueDescriptorProto`) for an enum.

**Definitions**

+   `proto_enum`: An `ENUM` value that contains the descriptor to retrieve.

**Return type**

`proto2.EnumValueDescriptorProto PROTO`

**Example**

The following query gets the `ideally_enabled` and `in_development` options from
the value descriptors in the `LanguageFeature` enum, and then produces query
results that are based on these value descriptors.

```googlesql
WITH
  EnabledFeatures AS (
    SELECT CAST(999991 AS googlesql.LanguageFeature) AS feature UNION ALL
    SELECT CAST(999992 AS googlesql.LanguageFeature) AS feature
  )
SELECT
  CAST(feature AS STRING) AS feature_enum_name,
  CAST(feature AS INT64) AS feature_enum_id,
  IFNULL(
    ENUM_VALUE_DESCRIPTOR_PROTO(feature).options.(googlesql.language_feature_options).ideally_enabled,
    TRUE) AS feature_is_ideally_enabled,
  IFNULL(
    ENUM_VALUE_DESCRIPTOR_PROTO(feature).options.(googlesql.language_feature_options).in_development,
    FALSE) AS feature_is_in_development
FROM
  EnabledFeatures;

/*-------------------------------------------------+-----------------+----------------------------+---------------------------+
 | feature_enum_name                               | feature_enum_id | feature_is_ideally_enabled | feature_is_in_development |
 +-------------------------------------------------+-----------------+----------------------------+---------------------------+
 | FEATURE_TEST_IDEALLY_ENABLED_BUT_IN_DEVELOPMENT | 999991          | TRUE                       | TRUE                      |
 | FEATURE_TEST_IDEALLY_DISABLED                   | 999992          | FALSE                      | FALSE                     |
 +-------------------------------------------------+-----------------+----------------------------+---------------------------*/
```

## `EXTRACT` 
<a id="proto_extract"></a>

```googlesql
EXTRACT( extraction_type (proto_field) FROM proto_expression )

extraction_type:
  { FIELD | RAW | HAS | ONEOF_CASE }
```

**Description**

Extracts a value from a protocol buffer. `proto_expression` represents the
expression that returns a protocol buffer, `proto_field` represents the field of
the protocol buffer to extract from, and `extraction_type` determines the type
of data to return.

You can access most simple proto message fields idiomatically using the
[dot operator][dot-operator]. `EXTRACT` is a more general way to access fields
that can handle most cases. For instance, `EXTRACT` can access the values of
fields made ambiguous by tag reuse.

**Extraction Types**

You can choose the type of information to get with `EXTRACT`. Your choices are:

+  `FIELD`: Extract a value from a protocol buffer field.
+  `RAW`: Extract an uninterpreted value from a
    protocol buffer field. Raw values
    ignore any GoogleSQL type annotations.
+  `HAS`: Returns `TRUE` if a protocol buffer field is set in a proto message;
   otherwise, `FALSE`. Alternatively, use [`has_x`][has-value] to perform this
   task.
+  `ONEOF_CASE`: Returns the name of the set protocol buffer field in a Oneof.
   If no field is set, returns an empty string.

**Return Type**

The return type depends upon the extraction type in the query.

+  `FIELD`: Protocol buffer field type.
+  `RAW`: Protocol buffer field
    type. Format annotations are
    ignored.
+  `HAS`: `BOOL`
+  `ONEOF_CASE`: `STRING`

**Examples**

The examples in this section reference two protocol buffers called `Album` and
`Chart`, and one table called `AlbumList`.

```proto
message Album {
  optional string album_name = 1;
  repeated string song = 2;
  oneof group_name {
    string solo = 3;
    string duet = 4;
    string band = 5;
  }
}
```

```proto
message Chart {
  optional int64 date = 1 [(googlesql.format) = DATE];
  optional string chart_name = 2;
  optional int64 rank = 3;
}
```

```googlesql
WITH AlbumList AS (
  SELECT
    NEW Album(
      'Alana Yah' AS solo,
      'New Moon' AS album_name,
      ['Sandstorm','Wait'] AS song) AS album_col,
    NEW Chart(
      'Billboard' AS chart_name,
      '2016-04-23' AS date,
      1 AS rank) AS chart_col
    UNION ALL
  SELECT
    NEW Album(
      'The Roadlands' AS band,
      'Grit' AS album_name,
      ['The Way', 'Awake', 'Lost Things'] AS song) AS album_col,
    NEW Chart(
      'Billboard' AS chart_name,
      1 as rank) AS chart_col
)
SELECT * FROM AlbumList
```

The following example extracts the album names from a table called `AlbumList`
that contains a proto-typed column called `Album`.

```googlesql
SELECT EXTRACT(FIELD(album_name) FROM album_col) AS name_of_album
FROM AlbumList

/*------------------+
 | name_of_album    |
 +------------------+
 | New Moon         |
 | Grit             |
 +------------------*/
```

A table called `AlbumList` contains a proto-typed column called `Chart`.
`Chart` contains a field called `date`, which can store an integer. The
`date` field has an annotated format called `DATE` assigned to it, which means
that when you extract the value in this field, it returns a `DATE`, not an
`INT64`.

If you would like to return the value for `date` as an `INT64`, not
as a `DATE`, use the `RAW` extraction type in your query. For example:

```googlesql
SELECT
  EXTRACT(RAW(date) FROM chart_col) AS raw_date,
  EXTRACT(FIELD(date) FROM chart_col) AS formatted_date
FROM AlbumList

/*----------+----------------+
 | raw_date | formatted_date |
 +----------+----------------+
 | 16914    | 2016-04-23     |
 | 0        | 1970-01-01     |
 +----------+----------------*/
```

The following example checks to see if release dates exist in a table called
`AlbumList` that contains a protocol buffer called `Chart`.

```googlesql
SELECT EXTRACT(HAS(date) FROM chart_col) AS has_release_date
FROM AlbumList

/*------------------+
 | has_release_date |
 +------------------+
 | TRUE             |
 | FALSE            |
 +------------------*/
```

The following example extracts the group name that's assigned to an artist in
a table called `AlbumList`. The group name is set for exactly one
protocol buffer field inside of the `group_name` Oneof. The `group_name` Oneof
exists inside the `Album` protocol buffer.

```googlesql
SELECT EXTRACT(ONEOF_CASE(group_name) FROM album_col) AS artist_type
FROM AlbumList;

/*-------------+
 | artist_type |
 +-------------+
 | solo        |
 | band        |
 +-------------*/
```

[dot-operator]: https://github.com/google/googlesql/blob/master/docs/operators.md#field_access_operator

[has-value]: https://github.com/google/googlesql/blob/master/docs/protocol-buffers.md#checking_if_a_field_has_a_value

## `FILTER_FIELDS`

```googlesql
FILTER_FIELDS(
  proto_expression,
  proto_field_list
  [, reset_cleared_required_fields => { TRUE | FALSE } ]
)

proto_field_list:
  {+|-}proto_field_path[, ...]
```

**Description**

Takes a protocol buffer and a list of its fields to include or exclude.
Returns a version of that protocol buffer with unwanted fields removed.
Returns `NULL` if the protocol buffer is `NULL`.

Input values:

+ `proto_expression`: The protocol buffer to filter.
+ `proto_field_list`: The fields to exclude or include in the resulting
  protocol buffer.
+ `+`: Include a protocol buffer field and its children in the results.
+ `-`: Exclude a protocol buffer field and its children in the results.
+ `proto_field_path`: The protocol buffer field to include or exclude.
  If the field represents an [extension][querying-proto-extensions], you can use
  syntax for that extension in the path.
+ `reset_cleared_required_fields`: Named argument with a `BOOL` value.
  If not explicitly set, `FALSE` is used implicitly.
  If `FALSE`, you must include all protocol buffer `required` fields in the
  `FILTER_FIELDS` function. If `TRUE`, you don't need to include all required
  protocol buffer fields and the value of required fields
  defaults to these values:

  Type                    | Default value
  ----------------------- | --------
  Floating point          | `0.0`
  Integer                 | `0`
  Boolean                 | `FALSE`
  String, byte            | `""`
  Protocol buffer message | Empty message

Protocol buffer field expression behavior:

+ The first field in `proto_field_list` determines the default
  inclusion/exclusion. By default, when you include the first field, all other
  fields are excluded. Or by default, when you exclude the first field, all
  other fields are included.
+ A required field in the protocol buffer can't be excluded explicitly or
  implicitly, unless you have the
  `RESET_CLEARED_REQUIRED_FIELDS` named argument set as `TRUE`.
+ If a field is included, its child fields and descendants are implicitly
  included in the results.
+ If a field is excluded, its child fields and descendants are
  implicitly excluded in the results.
+ A child field must be listed after its parent field in the argument list,
  but doesn't need to come right after the parent field.

Caveats:

+ If you attempt to exclude/include a field that already has been
  implicitly excluded/included, an error is produced.
+ If you attempt to explicitly include/exclude a field that has already
  implicitly been included/excluded, an error is produced.

**Return type**

Type of `proto_expression`

**Examples**

The examples in this section reference a protocol buffer called `Award` and
a table called `MusicAwards`.

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
WITH
  MusicAwards AS (
    SELECT
      CAST(
        '''
        year: 2001
        month: 9
        type { award_name: 'Best Artist' category: 'Artist' }
        type { award_name: 'Best Album' category: 'Album' }
        '''
        AS googlesql.examples.music.Award) AS award_col
    UNION ALL
    SELECT
      CAST(
        '''
        year: 2001
        month: 12
        type { award_name: 'Best Song' category: 'Song' }
        '''
        AS googlesql.examples.music.Award) AS award_col
  )
SELECT *
FROM MusicAwards

/*---------------------------------------------------------+
 | award_col                                               |
 +---------------------------------------------------------+
 | {                                                       |
 |   year: 2001                                            |
 |   month: 9                                              |
 |   type { award_name: "Best Artist" category: "Artist" } |
 |   type { award_name: "Best Album" category: "Album" }   |
 | }                                                       |
 | {                                                       |
 |   year: 2001                                            |
 |   month: 12                                             |
 |   type { award_name: "Best Song" category: "Song" }     |
 | }                                                       |
 +---------------------------------------------------------*/
```

The following example returns protocol buffers that only include the `year`
field.

```googlesql
SELECT FILTER_FIELDS(award_col, +year) AS filtered_fields
FROM MusicAwards

/*-----------------+
 | filtered_fields |
 +-----------------+
 | {year: 2001}    |
 | {year: 2001}    |
 +-----------------*/
```

The following example returns protocol buffers that include all but the `type`
field.

```googlesql
SELECT FILTER_FIELDS(award_col, -type) AS filtered_fields
FROM MusicAwards

/*------------------------+
 | filtered_fields        |
 +------------------------+
 | {year: 2001 month: 9}  |
 | {year: 2001 month: 12} |
 +------------------------*/
```

The following example returns protocol buffers that only include the `year` and
`type.award_name` fields.

```googlesql
SELECT FILTER_FIELDS(award_col, +year, +type.award_name) AS filtered_fields
FROM MusicAwards

/*--------------------------------------+
 | filtered_fields                      |
 +--------------------------------------+
 | {                                    |
 |   year: 2001                         |
 |   type { award_name: "Best Artist" } |
 |   type { award_name: "Best Album" }  |
 | }                                    |
 | {                                    |
 |   year: 2001                         |
 |   type { award_name: "Best Song" }   |
 | }                                    |
 +--------------------------------------*/
```

The following example returns the `year` and `type` fields, but excludes the
`award_name` field in the `type` field.

```googlesql
SELECT FILTER_FIELDS(award_col, +year, +type, -type.award_name) AS filtered_fields
FROM MusicAwards

/*---------------------------------+
 | filtered_fields                 |
 +---------------------------------+
 | {                               |
 |   year: 2001                    |
 |   type { category: "Artist" }   |
 |   type { category: "Album" }    |
 | }                               |
 | {                               |
 |   year: 2001                    |
 |   type { category: "Song" }     |
 | }                               |
 +---------------------------------*/
```

The following example produces an error because `year` is a required field
and can't be excluded explicitly or implicitly from the results.

```googlesql
SELECT FILTER_FIELDS(award_col, -year) AS filtered_fields
FROM MusicAwards

-- Error
```

The following example produces an error because when `year` was included,
`month` was implicitly excluded. You can't explicitly exclude a field that
has already been implicitly excluded.

```googlesql
SELECT FILTER_FIELDS(award_col, +year, -month) AS filtered_fields
FROM MusicAwards

-- Error
```

When `RESET_CLEARED_REQUIRED_FIELDS` is set as `TRUE`, `FILTER_FIELDS` doesn't
need to include required fields. In the example below, `MusicAwards` has a
required field called `year`, but this isn't added as an argument for
`FILTER_FIELDS`. `year` is added to the results with its default value, `0`.

```googlesql
SELECT FILTER_FIELDS(
  award_col,
  +month,
  RESET_CLEARED_REQUIRED_FIELDS => TRUE) AS filtered_fields
FROM MusicAwards;

/*---------------------------------+
 | filtered_fields                 |
 +---------------------------------+
 | {                               |
 |   year: 0,                      |
 |   month: 9                      |
 | }                               |
 | {                               |
 |   year: 0,                      |
 |   month: 12                     |
 | }                               |
 +---------------------------------*/
```

[querying-proto-extensions]: https://github.com/google/googlesql/blob/master/docs/protocol-buffers.md#extensions

## `FROM_PROTO`

```googlesql
FROM_PROTO(expression)
```

**Description**

Returns a GoogleSQL value. The valid `expression` types are defined
in the table below, along with the return types that they produce.
Other input `expression` types are invalid. If `expression` can't be converted
to a valid value, an error is returned.

<table width="100%">
  <thead>
    <tr>
      <th width="50%"><code>expression</code> type</th>
      <th width="50%">Return type</th>
    </tr>
  </thead>
  <tbody>
    <tr>
      <td>
        <ul>
        <li>INT32</li>
        <li>google.protobuf.Int32Value</li>
        </ul>
      </td>
      <td>INT32</td>
    </tr>
    <tr>
      <td>
        <ul>
        <li>UINT32</li>
        <li>google.protobuf.UInt32Value</li>
        </ul>
      </td>
      <td>UINT32</td>
    </tr>
    <tr>
      <td>
        <ul>
        <li>INT64</li>
        <li>google.protobuf.Int64Value</li>
        </ul>
      </td>
      <td>INT64</td>
    </tr>
    <tr>
      <td>
        <ul>
        <li>UINT64</li>
        <li>google.protobuf.UInt64Value</li>
        </ul>
      </td>
      <td>UINT64</td>
    </tr>
    <tr>
      <td>
        <ul>
        <li>FLOAT</li>
        <li>google.protobuf.FloatValue</li>
        </ul>
      </td>
      <td>FLOAT</td>
    </tr>
    <tr>
      <td>
        <ul>
        <li>DOUBLE</li>
        <li>google.protobuf.DoubleValue</li>
        </ul>
      </td>
      <td>DOUBLE</td>
    </tr>
    <tr>
      <td>
        <ul>
        <li>BOOL</li>
        <li>google.protobuf.BoolValue</li>
        </ul>
      </td>
      <td>BOOL</td>
    </tr>
    <tr>
      <td>
        <ul>
          <li>STRING</li>
          <li>
            google.protobuf.StringValue
            <p>
            Note: The <code>StringValue</code>
            value field must be
            UTF-8 encoded.
            </p>
          </li>
        </ul>
      </td>
      <td>STRING</td>
    </tr>
    <tr>
      <td>
        <ul>
        <li>BYTES</li>
        <li>google.protobuf.BytesValue</li>
        </ul>
      </td>
      <td>BYTES</td>
    </tr>
    <tr>
      <td>
        <ul>
        <li>DATE</li>
        <li>google.type.Date</li>
        </ul>
      </td>
      <td>DATE</td>
    </tr>
    <tr>
      <td>
        <ul>
        <li>TIME</li>
        <li>
          google.type.TimeOfDay

          

          

        </li>
        </ul>
      </td>
      <td>TIME</td>
    </tr>
    <tr>
      <td>
        <ul>
        <li>TIMESTAMP</li>
        <li>
          google.protobuf.Timestamp

          

          

        </li>
        </ul>
      </td>
      <td>TIMESTAMP</td>
    </tr>
  </tbody>
</table>

**Return Type**

The return type depends upon the `expression` type. See the return types
in the table above.

**Examples**

Convert a `google.type.Date` type into a `DATE` type.

```googlesql
SELECT FROM_PROTO(
  new google.type.Date(
    2019 as year,
    10 as month,
    30 as day
  )
)

/*------------+
 | $col1      |
 +------------+
 | 2019-10-30 |
 +------------*/
```

Pass in and return a `DATE` type.

```googlesql
SELECT FROM_PROTO(DATE '2019-10-30')

/*------------+
 | $col1      |
 +------------+
 | 2019-10-30 |
 +------------*/
```

## `PROTO_DEFAULT_IF_NULL`

```googlesql
PROTO_DEFAULT_IF_NULL(proto_field_expression)
```

**Description**

Evaluates any expression that results in a proto field access.
If the `proto_field_expression` evaluates to `NULL`, returns the default
value for the field. Otherwise, returns the field value.

Stipulations:

+ The expression can't resolve to a required field.
+ The expression can't resolve to a message field.
+ The expression must resolve to a regular proto field access, not
  a virtual field.
+ The expression can't access a field with
  `googlesql.use_defaults=false`.

**Return Type**

Type of `proto_field_expression`.

**Example**

In the following example, each book in a library has a country of origin. If
the country isn't set, the country defaults to unknown.

In this statement, table `library_books` contains a column named `book`,
whose type is `Book`.

```googlesql
SELECT PROTO_DEFAULT_IF_NULL(book.country) AS origin FROM library_books;
```

`Book` is a type that contains a field called `country`.

```proto
message Book {
  optional string country = 4 [default = 'Unknown'];
}
```

This is the result if `book.country` evaluates to `Canada`.

```googlesql
/*-----------------+
 | origin          |
 +-----------------+
 | Canada          |
 +-----------------*/
```

This is the result if `book` is `NULL`. Since `book` is `NULL`,
`book.country` evaluates to `NULL` and therefore the function result is the
default value for `country`.

```googlesql
/*-----------------+
 | origin          |
 +-----------------+
 | Unknown         |
 +-----------------*/
```

## `PROTO_MAP_CONTAINS_KEY`

```googlesql
PROTO_MAP_CONTAINS_KEY(proto_map_field_expression, key)
```

**Description**

Returns whether a [protocol buffer map field][proto-map] contains a given key.

Input values:

+ `proto_map_field_expression`: A protocol buffer map field.
+ `key`: A key in the protocol buffer map field.

`NULL` handling:

+ If `map_field` is `NULL`, returns `NULL`.
+ If `key` is `NULL`, returns `FALSE`.

**Return type**

`BOOL`

**Examples**

To illustrate the use of this function, consider the protocol buffer message
`Item`:

```proto
message Item {
  optional map<string, int64> purchased = 1;
};
```

In the following example, the function returns `TRUE` when the key is
present, `FALSE` otherwise.

```googlesql
SELECT
  PROTO_MAP_CONTAINS_KEY(m.purchased, 'A') AS contains_a,
  PROTO_MAP_CONTAINS_KEY(m.purchased, 'B') AS contains_b
FROM
  (SELECT AS VALUE CAST("purchased { key: 'A' value: 2 }" AS Item)) AS m;

/*------------+------------+
 | contains_a | contains_b |
 +------------+------------+
 | TRUE       | FALSE      |
 +------------+------------*/
```

[proto-map]: https://developers.google.com/protocol-buffers/docs/proto3#maps

## `PROTO_MODIFY_MAP`

```googlesql
PROTO_MODIFY_MAP(proto_map_field_expression, key_value_pair[, ...])

key_value_pair:
  key, value
```

**Description**

Modifies a [protocol buffer map field][proto-map] and returns the modified map
field.

Input values:

+ `proto_map_field_expression`: A protocol buffer map field.
+ `key_value_pair`: A key-value pair in the protocol buffer map field.

Modification behavior:

+ If the key isn't already in the map field, adds the key and its value to the
  map field.
+ If the key is already in the map field, replaces its value.
+ If the key is in the map field and the value is `NULL`, removes the key and
  its value from the map field.

`NULL` handling:

+ If `key` is `NULL`, produces an error.
+ If the same `key` appears more than once, produces an error.
+ If `map` is `NULL`, `map` is treated as empty.

**Return type**

In the input protocol buffer map field, `V` as represented in `map<K,V>`.

**Examples**

To illustrate the use of this function, consider the protocol buffer message
`Item`:

```proto
message Item {
  optional map<string, int64> purchased = 1;
};
```

In the following example, the query deletes key `A`, replaces `B`, and adds
`C` in a map field called `purchased`.

```googlesql
SELECT
  PROTO_MODIFY_MAP(m.purchased, 'A', NULL, 'B', 4, 'C', 6) AS result_map
FROM
  (SELECT AS VALUE CAST("purchased { key: 'A' value: 2 } purchased { key: 'B' value: 3}" AS Item)) AS m;

/*---------------------------------------------+
 | result_map                                  |
 +---------------------------------------------+
 | { key: 'B' value: 4 } { key: 'C' value: 6 } |
 +---------------------------------------------*/
```

[proto-map]: https://developers.google.com/protocol-buffers/docs/proto3#maps

## `REPLACE_FIELDS`

```googlesql
REPLACE_FIELDS(proto_expression, value AS field_path [, ... ])
```

**Description**

Returns a copy of a protocol buffer, replacing the values in one or more fields.
`field_path` is a delimited path to the protocol buffer field that's replaced.
When using `replace_fields`, the following limitations apply:

+   If `value` is `NULL`, it un-sets `field_path` or returns an error if the
    last component of `field_path` is a required field.
+   Replacing subfields will succeed only if the message containing the field is
    set.
+   Replacing subfields of repeated field isn't allowed.
+   A repeated field can be replaced with an `ARRAY` value.

**Return type**

Type of `proto_expression`

**Examples**

The following example uses protocol buffer messages `Book` and `BookDetails`.

```
message Book {
  required string title = 1;
  repeated string reviews = 2;
  optional BookDetails details = 3;
};

message BookDetails {
  optional string author = 1;
  optional int32 chapters = 2;
};
```

This statement replaces the values of the field `title` and subfield `chapters`
of proto type `Book`. Note that field `details` must be set for the statement
to succeed.

```googlesql
SELECT REPLACE_FIELDS(
  NEW Book(
    "The Hummingbird" AS title,
    NEW BookDetails(10 AS chapters) AS details),
  "The Hummingbird II" AS title,
  11 AS details.chapters)
AS proto;

/*-----------------------------------------------------------------------------+
 | proto                                                                       |
 +-----------------------------------------------------------------------------+
 |{title: "The Hummingbird II" details: {chapters: 11 }}                       |
 +-----------------------------------------------------------------------------*/
```

The function can replace value of repeated fields.

```googlesql
SELECT REPLACE_FIELDS(
  NEW Book("The Hummingbird" AS title,
    NEW BookDetails(10 AS chapters) AS details),
  ["A good read!", "Highly recommended."] AS reviews)
AS proto;

/*-----------------------------------------------------------------------------+
 | proto                                                                       |
 +-----------------------------------------------------------------------------+
 |{title: "The Hummingbird" review: "A good read" review: "Highly recommended."|
 | details: {chapters: 10 }}                                                   |
 +-----------------------------------------------------------------------------*/
```

The function can also set a field to `NULL`.

```googlesql
SELECT REPLACE_FIELDS(
  NEW Book("The Hummingbird" AS title,
    NEW BookDetails(10 AS chapters) AS details),
  NULL AS details)
AS proto;

/*-----------------------------------------------------------------------------+
 | proto                                                                       |
 +-----------------------------------------------------------------------------+
 |{title: "The Hummingbird" }                                                  |
 +-----------------------------------------------------------------------------*/
```

## `TO_PROTO`

```
TO_PROTO(expression)
```

**Description**

Returns a PROTO value. The valid `expression` types are defined in the
table below, along with the return types that they produce. Other input
`expression` types are invalid.

<table width="100%">
  <thead>
    <tr>
      <th width="50%"><code>expression</code> type</th>
      <th width="50%">Return type</th>
    </tr>
  </thead>
  <tbody>
    <tr>
      <td>
        <ul>
        <li>INT32</li>
        <li>google.protobuf.Int32Value</li>
        </ul>
      </td>
      <td>google.protobuf.Int32Value</td>
    </tr>
    <tr>
      <td>
        <ul>
        <li>UINT32</li>
        <li>google.protobuf.UInt32Value</li>
        </ul>
      </td>
      <td>google.protobuf.UInt32Value</td>
    </tr>
    <tr>
      <td>
        <ul>
        <li>INT64</li>
        <li>google.protobuf.Int64Value</li>
        </ul>
      </td>
      <td>google.protobuf.Int64Value</td>
    </tr>
    <tr>
      <td>
        <ul>
        <li>UINT64</li>
        <li>google.protobuf.UInt64Value</li>
        </ul>
      </td>
      <td>google.protobuf.UInt64Value</td>
    </tr>
    <tr>
      <td>
        <ul>
        <li>FLOAT</li>
        <li>google.protobuf.FloatValue</li>
        </ul>
      </td>
      <td>google.protobuf.FloatValue</td>
    </tr>
    <tr>
      <td>
        <ul>
        <li>DOUBLE</li>
        <li>google.protobuf.DoubleValue</li>
        </ul>
      </td>
      <td>google.protobuf.DoubleValue</td>
    </tr>
    <tr>
      <td>
        <ul>
        <li>BOOL</li>
        <li>google.protobuf.BoolValue</li>
        </ul>
      </td>
      <td>google.protobuf.BoolValue</td>
    </tr>
    <tr>
      <td>
        <ul>
          <li>STRING</li>
          <li>google.protobuf.StringValue</li>
        </ul>
      </td>
      <td>google.protobuf.StringValue</td>
    </tr>
    <tr>
      <td>
        <ul>
        <li>BYTES</li>
        <li>google.protobuf.BytesValue</li>
        </ul>
      </td>
      <td>google.protobuf.BytesValue</td>
    </tr>
    <tr>
      <td>
        <ul>
        <li>DATE</li>
        <li>google.type.Date</li>
        </ul>
      </td>
      <td>google.type.Date</td>
    </tr>
    <tr>
      <td>
        <ul>
        <li>TIME</li>
        <li>google.type.TimeOfDay</li>
        </ul>
      </td>
      <td>google.type.TimeOfDay</td>
    </tr>
    <tr>
      <td>
        <ul>
        <li>TIMESTAMP</li>
        <li>google.protobuf.Timestamp</li>
        </ul>
      </td>
      <td>google.protobuf.Timestamp</td>
    </tr>
  </tbody>
</table>

**Return Type**

The return type depends upon the `expression` type. See the return types
in the table above.

**Examples**

Convert a `DATE` type into a `google.type.Date` type.

```googlesql
SELECT TO_PROTO(DATE '2019-10-30')

/*--------------------------------+
 | $col1                          |
 +--------------------------------+
 | {year: 2019 month: 10 day: 30} |
 +--------------------------------*/
```

Pass in and return a `google.type.Date` type.

```googlesql
SELECT TO_PROTO(
  new google.type.Date(
    2019 as year,
    10 as month,
    30 as day
  )
)

/*--------------------------------+
 | $col1                          |
 +--------------------------------+
 | {year: 2019 month: 10 day: 30} |
 +--------------------------------*/
```

