

<!-- mdlint off(WHITESPACE_LINE_LENGTH) -->

# Work with protocol buffers 
<a id="protocol-buffers"></a>

Protocol buffers are a flexible mechanism for serializing structured
data. They are small in size and efficient to send over RPCs.
Protocol buffers are represented in GoogleSQL using the
`PROTO` data type. A column can contain `PROTO` values the same way it can
contain `INT32` or `STRING` values.

A protocol buffer contains zero or more fields inside it. Each field inside a
protocol buffer has its own type. All SQL data types except `STRUCT` can be
contained inside a `PROTO`. Repeated fields in a protocol buffer are represented
as `ARRAY`s. To learn more about the `PROTO` data type, see [Protocol buffer
type][proto-data-type]. For related
functions, see [Protocol buffer functions][proto-functions].

As efficient and as popular as protocol buffers are, however, when it comes to
data storage they do have one drawback: they don't map well to SQL syntax. For
example, SQL syntax expects that a given field can support a `NULL` or default
value. Protocol buffers, on the other hand, don't support `NULL`s very well,
and there isn't a standard way to determine whether a missing field should get a
`NULL` or a default value.

To query protocol buffers, you need to understand how they are
represented, what features they support, and what data they can contain. To
learn more about protocol buffers, see the [Protocol Buffers Developer Guide][protocol-buffers-dev-guide].

## Construct a protocol buffer

You can construct a protocol buffer with the [`NEW`][new-operator] operator or
the [`SELECT AS typename`][select-as-typename] statement. To learn more about
constructing protocol buffers, see [Protocol buffer
type][proto-data-type-construct].

### Cast to or from a protocol buffer

You can use the [`CAST AS PROTO`][cast-as-proto] function to cast `PROTO` to or
from `BYTES`, `STRING`, or `PROTO`.

```googlesql
SELECT CAST('first_name: "Alana", last_name: "Yah", customer_no: 1234'
  AS example.CustomerInfo);
```

Casting to or from `BYTES` produces or parses [proto2 wire format bytes][proto2-wire-format].
If there is a failure during the serialization or deserialization process,
GoogleSQL throws an error. This can happen, for example, if no value is
specified for a required field.

Casting to or from `STRING` produces or parses the [proto2 text format][proto2-text-format].
When casting from `STRING`, unknown field names aren't parseable. This means you
need to be cautious, because round-tripping from `PROTO` to `STRING` back to
`PROTO` might result in unexpected results.

`STRING` literals used where a `PROTO` value is expected will be implicitly cast
to `PROTO`. If the literal value can't be parsed using the expected `PROTO`
type, an error is raised. To return `NULL`
instead of raising an error, use [`SAFE_CAST`][link_to_safe_cast].

## Map fields {: #map_fields}

You can add, remove, and retrieve values from protocol buffer map fields.

### Add values to map fields 
<a id="add_to_proto_maps"></a>

You can add a value to a protocol buffer map field in the following ways.

#### Use the `PROTO_MODIFY_MAP` function 
<a id="proto_maps_add_modify_map"></a>

You can use the [`PROTO_MODIFY_MAP` function][proto-modify-map] to add values to a
protocol buffer map field.

The following query adds a new key-value pair, `A, 11`, to a
map field called `purchased` in a protocol buffer called `Item`.

```proto
message Item {
  map<string, int64> purchased = 1;
}
```

```googlesql
SELECT
  PROTO_MODIFY_MAP(m.purchased, 'B', 11) AS result_map
FROM
  (SELECT AS VALUE CAST("purchased { key: 'A' value: 22 }" AS Item)) AS m;

/*-----------------------------------------------+
 | result_map                                    |
 +-----------------------------------------------+
 | { key: 'A' value: 22 } { key: 'B' value: 11 } |
 +-----------------------------------------------*/
```

#### Use a protocol buffer map array 
<a id="proto_maps_add_array"></a>

GoogleSQL supports adding key-value pairs to a
protocol buffer map field, using an array of
[typeless structs][typeless-structs] with this format:

```
STRUCT<key_type, value_type>(key_expression, value_expression)
```

+ `key_type` and `value_type` represent GoogleSQL data types.
+ `key_expression` and `value_expression` must be coercible or literal-coercible
  to `key_type` and `value_type`.

[Casting][proto-map-cast] inside structs is supported.

The following query adds two key-value pairs, `A, 32` and `B, 9` to a
map field called `purchased` in a protocol buffer called `Item`.

```proto
message Item {
  map<string, int64> purchased = 1;
}
```

```googlesql
SELECT NEW Item([('A', 32), ('B', 9)] AS purchased)
```

### Change values in map fields 
<a id="change_proto_maps"></a>

You can use the [`PROTO_MODIFY_MAP` function][proto-modify-map] to change values in a
protocol buffer map field.

The following query changes a value from `22` to `6` in a map field called
`purchased` in a protocol buffer called `Item`.

```proto
message Item {
  map<string, int64> purchased = 1;
}
```

```googlesql
SELECT
  PROTO_MODIFY_MAP(m.purchased, 'A', 6) AS result_map
FROM
  (SELECT AS VALUE CAST("purchased { key: 'A' value: 22 }" AS Item)) AS m;

/*-----------------------+
 | result_map            |
 +-----------------------+
 | { key: "A" value: 6 } |
 +-----------------------*/
```

### Remove values from map fields 
<a id="remove_from_proto_maps"></a>

You can use the [`PROTO_MODIFY_MAP` function][proto-modify-map] to remove values from
a protocol buffer map field.

The following query removes a key-value pair, `A, 11`, from a
map field called `purchased` in a protocol buffer called `Item`.

```proto
message Item {
  map<string, int64> purchased = 1;
}
```

```googlesql
SELECT
  PROTO_MODIFY_MAP(m.purchased, 'A', NULL) AS result_map
FROM
  (SELECT AS VALUE CAST("purchased { key: 'A' value: 22 } purchased { key: 'B' value: 11 }" AS Item)) AS m;

/*------------------------+
 | result_map             |
 +------------------------+
 | { key: "B" value: 11 } |
 +------------------------*/
```

### Check if a map field contains a key 
<a id="proto_maps_check_key"></a>

You can check to see if a protocol buffer map field contains a key with the
[`PROTO_MAP_CONTAINS_KEY` function][proto-map-contains-key].

In the following example, the key `B` isn't present in a
map field called `purchased` in a protocol buffer called `Item`.

```proto
message Item {
  map<string, int64> purchased = 1;
}
```

```googlesql
SELECT
  PROTO_MAP_CONTAINS_KEY(m.map, 'B') AS key_is_present
FROM
  (SELECT AS VALUE CAST("purchased { key: 'A' value: 2 }" AS Item)) AS m;

/*----------------+
 | key_is_present |
 +----------------+
 | FALSE          |
 +----------------*/
```

## Query a protocol buffer 
<a id="querying_protocol_buffers"></a>

Use the dot operator to access the fields contained within a protocol buffer.
You can't use this operator to get values of fields that use the
[`Oneof`][oneof-type], [`Any`][any-type], or
[`Unknown Fields`][unknown-fields-type] type. If you must
reference fields in ambiguous messages (caused by reused field numbers), use
the [`EXTRACT`][proto-extract] function.

### Example protocol buffer message 
<a id="example_protocol_buffer_message"></a>

The following example queries for a table called `Customers`. This table
contains a column `Orders` of type `PROTO`.

```googlesql
CREATE TABLE Customers (
  Id INT64,
  Orders examples.shipping.Order,
) PRIMARY KEY(Id);
```
The proto stored in `Orders` contains fields such as the items ordered and the shipping address. The `.proto`
file that defines this protocol buffer might look like this:

```proto

syntax = "proto2";
package examples.shipping;

message Order {
  optional string order_number = 1;
  optional int64 date = 2 [( googlesql.format ) = DATE];

  message Address {
    optional string street = 1;
    optional string city = 2;
    optional string state = 3;
    optional string country = 4;
  }

  optional Address shipping_address = 3;

  message Item {
    optional string product_name = 1;
    optional int32 quantity = 2;
  }

  repeated Item line_item = 4;

  map<string, string> labels = 5;

}
```

An instance of this message might be:

```
{
  order_number: 1234567
  date: 16242
  shipping_address: {
      street: "1234 Main St"
      city: "AnyCity"
      state: "AnyState"
      country: "United States"
  }
  line_item: [
    {
      product_name: "Foo"
      quantity: 10
    },
    {
      product_name: "Bar"
      quantity: 5
    }
  ]
}
```

### Query top-level fields 
<a id="querying_top-level_fields"></a>

You can write a query to return an entire protocol buffer message, or to return
a top-level or nested field of the message.

Using our example protocol buffer message, the following query returns all
protocol buffer values from the `Orders` column:

```googlesql
SELECT
  c.Orders
FROM
  Customers AS c;
```

This query returns the top-level field `order_number` from all protocol buffer
messages in the `Orders` column using the [dot operator][dot-operator]:

```googlesql
SELECT
  c.Orders.order_number
FROM
  Customers AS c;
```

### Query nested paths 
<a id="querying_nested_paths"></a>

You can write a query to return a nested field of a protocol buffer message.

In the previous example, the `Order` protocol buffer contains another protocol
buffer message, `Address`, in the `shipping_address` field. You can create a
query that returns all orders that have a shipping address in the United States:

```googlesql
SELECT
  c.Orders.order_number,
  c.Orders.shipping_address
FROM
  Customers AS c
WHERE
  c.Orders.shipping_address.country = "United States";
```

### Return repeated fields 
<a id="return_repeated_fields"></a>

A protocol buffer message can contain repeated fields. When referenced in a
SQL statement, the repeated fields return as `ARRAY` values. For example, our
protocol buffer message contains a repeated field, `line_item`.

The following query returns a set of `ARRAY`s containing the line items,
each holding all the line items for one order:

```googlesql
SELECT
  c.Orders.line_item
FROM
  Customers AS c;
```

For more information about arrays, see
[Work with arrays][working-with-arrays].

For more information about default field values, see [Default values and
`NULL`s][default-values].

### Return the number of elements in an array

You can return the number of values in a repeated fields in a protocol buffer
using the `ARRAY_LENGTH` function.

```googlesql
SELECT
  c.Orders.order_number,
  ARRAY_LENGTH(c.Orders.line_item)
FROM
  Customers AS c;
```

### Query map fields 
<a id="querying_map_fields"></a>

You can query protocol buffer map fields with the
[protocol buffer subscript operator][proto-subscript-operator].

In the following example, the subscript operator returns the value when the key
is present.

```proto
message Item {
  map<string, int64> purchased = 1;
}
```

```googlesql
SELECT
  m.purchased[KEY('A')] AS map_value
FROM
  (SELECT AS VALUE CAST("purchased { key: 'A' value: 2 }" AS Item)) AS m;

/*-----------+
 | map_value |
 +-----------+
 | 2         |
 +-----------*/
```

[Protocol buffer maps][proto-maps] are
[implemented as repeated fields][protocol-buffer-fields]. You can
query protocol buffer map fields by querying the underlying repeated field.
In the following example, the underlying repeated field has `key` and `value`
fields that can be queried.

```googlesql
SELECT
  C.Orders.order_number
FROM
  Customers AS C
WHERE
  EXISTS(
    SELECT
      *
    FROM
      C.Orders.Labels AS label
    WHERE
      label.key = 'color' AND label.value = 'red'
  );
```

## Extensions

[extensions][protocol-extensions]
can be queried from `PROTO` values.

### Top-level extensions 
<a id="extensions"></a>

If your `PROTO` value contains extensions, you can query those fields using the
following syntax:

```
<identifier_of_proto_value>.(<package_of_extension>.<path_expression_to_extension_field>)
```

For example, consider this proto definition:

```proto
package some.package;

message Foo {
  optional int32 x = 1;
  extensions 100 to 130;
}

message Point {
  optional int32 x = 1;
  optional int32 y = 2;
}

extend Foo {
  optional int32 bar = 126;
  optional Point point = 127;
}
```

The following sections use this proto definition in a Table, `Test`, which
contains a field, `foo_field` of type `Foo`.

A query that returns the value of the `bar` extension field would resemble the
following:

```googlesql
SELECT
  foo_field.(some.package.bar)
FROM
  Test;
```

These types of extensions are often referred to as *top-level extensions*.

If you want your statement to return a specific value from a top-level
extension, you would modify it as follows:

```googlesql
SELECT
  foo_field.(some.package.point).y
FROM
  Test;
```

You can refine your statement to look for a specific value of a top-level
extension as well.

```googlesql
SELECT
  foo_field.(some.package.bar)
FROM
  Test
WHERE
  foo_field.(some.package.bar) = 5;
```

You can also use backticks to escape the components in the extension path name
to avoid collisions with reserved keywords. However, you must use backticks
around individual components, not around multiple components or the entire path.

Correct example:

```googlesql
SELECT
  foo_field.(`some`.`package`.`bar`).value = 5
FROM
  Test;
```

Incorrect examples:

```googlesql {.bad}
SELECT
  foo_field.(`some.package`.`bar`).value = 5
FROM
  Test;
```

```googlesql {.bad}
SELECT
  foo_field.(`some.package.bar`).value = 5
FROM
  Test;
```

### Nested extensions

[Nested extensions][nested-extensions]
are also supported. These are protocol buffer extensions that are declared
within the scope of some other protocol message. For example:

```proto
package some.package;

message Baz {
  extend Foo {
    optional Baz foo_ext = 127;
  }
  optional int32 a = 1;
  optional int32 b = 2;
  ...
}
```

To construct queries for nested extensions, you use the same parenthetical
syntax as described in the previous section. To reference a nested extension,
in addition to specifying the package name, you must also specify the name of
the message where the extension is declared. For example:

```googlesql
SELECT
  foo_field.(some.package.Baz.foo_ext)
FROM
  Test;
```

You can reference a specific field in a nested extension using the same syntax
described in the previous section. For example:

```googlesql
SELECT
  foo_field.(some.package.Baz.foo_ext).a
FROM
  Test;
```

### Unnest repeated fields and extensions 
<a id="unnest_repeated_fields"></a>

You can use a [correlated join][correlated-join] to [unnest][unnest-operator]
standard repeated fields or repeated extension fields and return a table with
one row for each instance of the field. A standard repeated field doesn't
require an explicit `UNNEST`, but a repeated extension field does.

Consider the following protocol buffer:

```proto
syntax = "proto2";

package some.package;

message Example {
  optional int64 record_key = 1;
  repeated int64 repeated_value = 2;
  extensions 3 to 3;
}

message Extension {
  extend Example {
    repeated int64 repeated_extension_value = 3;
  }
}
```

The following query uses a standard repeated field, `repeated_value`, in a
correlated `INNER JOIN` and runs without requiring an explicit `UNNEST`.

```googlesql
WITH
  ExampleData AS (
    SELECT
      CAST("""
        record_key: 1
        repeated_value: 1
        repeated_value: 2
        repeated_value: 3
        [some.package.Extension.repeated_extension_value]: 4
        [some.package.Extension.repeated_extension_value]: 5
        [some.package.Extension.repeated_extension_value]: 6"""
        AS some.package.Example) AS proto_field
  )
SELECT
  T.proto_field.record_key,
  value
FROM
  ExampleData AS T
INNER JOIN
  T.proto_field.repeated_value AS value;
```

The following query uses a repeated extension field, `repeated_extension_value`,
in a correlated `INNER JOIN` and requires an explicit `UNNEST`.

```googlesql
WITH
  ExampleData AS (
    SELECT
      CAST("""
        record_key: 1
        repeated_value: 1
        repeated_value: 2
        repeated_value: 3
        [some.package.Extension.repeated_extension_value]: 4
        [some.package.Extension.repeated_extension_value]: 5
        [some.package.Extension.repeated_extension_value]: 6"""
        AS some.package.Example) AS proto_field
  )
SELECT
  T.proto_field.record_key,
  value
FROM
  ExampleData AS T
INNER JOIN
  UNNEST(T.proto_field.(some.package.Extension.repeated_extension_value)) AS value;
```

## Type mapping 
<a id="type_mapping"></a>

The following table gives examples of the mapping between various
protocol buffer field types and the resulting GoogleSQL types.

<table>
  <thead>
    <tr>
      <th>Protocol buffer field type</th>
      <th style="white-space:nowrap">GoogleSQL type</th>
    </tr>
  </thead>
  <tbody>
    <tr>
      <td><code>optional MessageType msg = 1;</code></td>
      <td><code>PROTO&lt;MessageType&gt;</code></td>
    </tr>
    <tr>
      <td><code>required int64 int = 1;</code></td>
      <td><code>INT64</code></td>
    </tr>
    <tr>
      <td>
      <code>optional int64 int = 1;</code><br>
      When reading, if this field isn't set, the default value (0) is returned. By
      default, protocol buffer fields don't return <code>NULL</code>.
      </td>
      <td><code>INT64</code></td>
    </tr>
    <tr>
      <td>
      <code>optional int64 int = 1 [( googlesql.use_defaults ) = false];</code><br>
      When reading, if this field isn't set, a <code>NULL</code> value is returned.
      This example uses an annotation, which is described in
      <a href="#default_values_and_nulls">Defaults and <code>NULL</code>s</a>.
      </td>
      <td><code>INT64</code></td>
    </tr>
    
    <tr>
      <td>
      <code>optional int32 date = 1 [( googlesql.format ) = DATE];</code>
      
      </td>
      <td><code>DATE</code></td>
    </tr>
    <tr>
      <td>
      <code>optional int64 time = 1 [( googlesql.format ) = TIMESTAMP_MICROS];</code>
      
      </td>
      <td><code>TIMESTAMP</code></td>
    </tr>
    
    <tr>
      <td><code>repeated int64 repeat = 1;</code></td>
      <td><code>ARRAY&lt;INT64&gt;</code></td>
    </tr>
  </tbody>
</table>

### Default values and `NULL`s 
<a id="default_values_and_nulls"></a>

Protocol buffer messages themselves don't have a default value; only the
non-repeated fields contained inside a protocol buffer have defaults. When
the result of a query returns a full protocol buffer value, it's returned
as a `blob` (a data structure that's used to store large binary data).
The result preserves all fields in the protocol buffer as they were stored,
including unset fields. This means that you can run a query that returns a
protocol buffer, and then extract fields or check field presence in your client
code with normal protocol buffer default behavior.

`NULL` values aren't typically returned when accessing non-repeated leaf fields
contained in a `PROTO` from within a SQL statement. However, `NULL` values can
result in some cases, for example if a containing parent message is `NULL`.

This behavior is consistent with the standards for handling protocol buffers
messages. If the field value isn't explicitly set on a non-repeated leaf field,
the `PROTO` default value for the field is returned. A change to the default
value for a `PROTO` field affects all future reads of that field using the new
`PROTO` definition for records where the value is unset.

For example, consider the following protocol buffer:

```proto

message SimpleMessage {
  optional SubMessage message_field_x = 1;
  optional SubMessage message_field_y = 2;
}

message SubMessage {
  optional string field_a = 1;
  optional string field_b = 2;
  optional string field_c = 3;
  repeated string field_d = 4;
  repeated string field_e = 5;
}
```

Assume the following field values:

+ `message_field_x` isn't set.
+ `message_field_y.field_a` is set to `"a"`.
+ `message_field_y.field_b` isn't set.
+ `message_field_y.field_c` isn't set.
+ `message_field_y.field_d` is set to `["d"]`.
+ `message_field_y.field_e` isn't set.

For this example, the following table summarizes the values produced from
various accessed fields:

<table>
<thead>
<tr>
<th style="width:30%">Accessed field</th>
<th style="width:30%">Value produced</th>
<th>Reason</th>
</tr>
</thead>
<tbody>
<tr>
<td><code>message_field_x</code></td>
<td><code>NULL</code></td>
<td>Message isn't set.</td>
</tr>
<tr>
<td><code>message_field_x.field_a</code> through <code>message_field_x.field_e</code></td>
<td><code>NULL</code></td>
<td>Parent message isn't set.</td>
</tr>
<tr>
<td><code>message_field_y</code></td>
<td><code>PROTO&lt;SubMessage&gt;{ field_a: "a" field_d: ["d"]}</code></td>
<td>Parent message and child fields are set.</td>
</tr>
<tr>
<td><code>message_field_y.field_a</code></td>
<td><code>"a"</code></td>
<td>Field is set.</td>
</tr>
<tr>
<td><code>message_field_y.field_b</code></td>
<td><code>""</code> (empty string)</td>
<td>Field isn't set but parent message is set, so default value (empty string) is produced.</td>
</tr>
<tr>
<td><code>message_field_y.field_c</code></td>
<td><code>NULL</code></td>
<td>Field isn't set and annotation indicates to not use defaults.</td>
</tr>
<tr>
<td><code>message_field_y.field_d</code></td>
<td><code>["d"]</code></td>
<td>Field is set.</td>
</tr>
<tr>
<td><code>message_field_y.field_e</code></td>
<td><code>[ ]</code> (empty array)</td>
<td>Repeated field isn't set.</td>
</tr>
</tbody>
</table>

### `googlesql.use_defaults` {: #googlesql_use_defaults}

You can change this default behavior using a special annotation on your protocol
message definition, `googlesql.use_defaults`, which you set on an
individual field to cause `NULL` values to be returned whenever a field value is
not explicitly set.

This annotation takes a boolean value.  The default is `true`, which means to
use the protocol buffer field defaults.  The annotation normally is written with
the value `false`, meaning that defaults should be ignored and `NULL`s should be
returned.

The following example shows how you can use the `use_defaults` annotation for an
optional protocol buffer field.

```proto
import "googlesql/public/proto/type_annotation.proto";

message SimpleMessage {
  // String field, where GoogleSQL interprets missing values as NULLs.
  optional string str = 2 [( googlesql.use_defaults ) = false];
}
```

In the case where protocol buffers have empty repeated fields, an empty `ARRAY`
is returned rather than a `NULL`-valued `ARRAY`. This behavior can't be
changed.

After a value has been read out of a protocol buffer field, that value is
treated like any other value of that type. For non-`PROTO` values, such as
`INT64`, this means that after you get the value, you will not be able to tell
if the value for that field was set explicitly, or if it was read as a default
value.

### `googlesql.use_field_defaults`

The `googlesql.use_field_defaults` annotation is just like
`googlesql.use_defaults`, but you set it on a message and it applies to
all unset fields within a given protocol buffer message. If both are present,
the field-level annotation takes precedence.

```proto
import "googlesql/public/proto/type_annotation.proto";

message AnotherSimpleMessage {
  // Interpret missing value as NULLs for all fields in this message.
  option ( googlesql.use_field_defaults ) = false;

  optional int64 nullable_int = 1;
  optional string nullable_string = 2;
}
```

### Check if a non-repeated field has a value 
<a id="checking_if_a_field_has_a_value"></a>

You can detect whether `optional` fields are set using a virtual field, `has_X`,
where `X` is the name of the field being checked. The type of the `has_X` field
is `BOOL`. The `has_` field is available for any non-repeated field of a `PROTO`
value. This field equals true if the value `X` is explicitly set in the message.

This field is useful for determining if a protocol buffer field has an explicit
value, or if reads will return a default value.

Consider the following protocol buffer example, which has a field `country`. You
can construct a query to determine if a `Customer` protocol buffer message has a
value for the country field by using the virtual field `has_country`:

```proto
message ShippingAddress {
  optional string name = 1;
  optional string address = 2;
  optional string country = 3;
}
```

```googlesql
SELECT
  c.Orders.shipping_address.has_country
FROM
  Customer AS c;
```

If `has_country` is `TRUE`, that means the value for the `country` field has
been explicitly set. If `has_country` is `FALSE`, that means the parent message
`c.Orders.shipping_address` isn't `NULL`, but the `country` field hasn't been
explicitly set. If `has_country` is `NULL`, that means the parent message
`c.Orders.shipping_address` is `NULL`, and therefore `country` can't be
considered either set or unset.

For more information about default field values, see [Default values and
`NULL`s][default-values].

### Check for a repeated value 
<a id="checking_for_a_repeated_value"></a>

You can use an `EXISTS` subquery to scan inside a repeated field and check if
any value exists with some desired property. For example, the following query
returns the name of every customer who has placed an order for the product
"Foo".

```googlesql
SELECT
  C.Id
FROM
  Customers AS C
WHERE
  EXISTS(
    SELECT
      *
    FROM
      C.Orders.line_item AS item
    WHERE
      item.product_name = 'Foo'
  );
```

### Nullness and nested fields

A `PROTO` value may contain fields which are themselves `PROTO`s. When this
happens, it's possible for the nested `PROTO` to be `NULL`. In such a
case, the fields contained within that nested field are also `NULL`
regardless of their `use_default_value` settings.

Consider this example proto:

```proto
syntax = "proto2";

package examples.package;

message NestedMessage {
  optional int64 value = 1;
}

message OuterMessage {
  optional NestedMessage nested = 1;
}
```

Running the following query returns a `5` for `value` because it is
explicitly defined.

```googlesql
SELECT
  proto_field.nested.value
FROM
  (SELECT
     CAST("nested { value: 5 }" AS examples.package.OuterMessage) AS proto_field);
```

If `value` isn't explicitly defined but `nested` is, you get a `0` because
the annotation on the protocol buffer definition says to use default values.

```googlesql
SELECT
  proto_field.nested.value
FROM
  (SELECT
     CAST("nested { }" AS examples.package.OuterMessage) AS proto_field);
```

However, if `nested` isn't explicitly defined, you get a `NULL` even
though the annotation says to use default values for the `value` field. This is
because the containing message is `NULL`. This behavior applies to both
repeated and non-repeated fields within a nested message.

```googlesql
SELECT
  proto_field.nested.value
FROM
  (SELECT
     CAST("" AS examples.package.OuterMessage) AS proto_field);
```

### Annotations to extend the type system 
<a id="proto_annotations"></a>

The GoogleSQL type system contains more types than the protocol buffer
type system.
<a href="https://developers.google.com/protocol-buffers/docs/proto?csw=1#options">
Proto annotations</a> are used to store non-protocol-buffer types inside
serialized protos and read them back as the correct type.

While protocol buffers themselves don't support `DATE` or `TIMESTAMP` types,
you can use annotations on your protocol message definition to indicate that
certain fields should be interpreted as `DATE` or `TIMESTAMP` values when read
using SQL. For instance, a protocol message definition could contain the
following line:

```proto
optional int32 date = 2 [( googlesql.format ) = DATE];
```

The `googlesql.format` annotation indicates that this field, which stores
an `int32` in the protocol buffer, should be interpreted as a `DATE`. Queries
over the `date` field return a `DATE` type instead of an `INT32` because of the
annotation.

This result is the equivalent of having an `INT32` column and querying it as
follows:

```googlesql
SELECT
  DATE_FROM_UNIX_DATE(date)...
```

## Coercion

Protocol buffers can be coerced into other data types. For more information, see
[Conversion rules][conversion-rules].

<!-- mdlint off(WHITESPACE_LINE_LENGTH) -->

[proto-data-type]: https://github.com/google/googlesql/blob/master/docs/data-types.md#protocol_buffer_type

[proto-data-type-construct]: https://github.com/google/googlesql/blob/master/docs/data-types.md#constructing_a_proto

[protocol-buffer-fields]: https://developers.google.com/protocol-buffers/docs/proto3#specifying_field_rules

[protocol-buffers-dev-guide]: https://developers.google.com/protocol-buffers

[nested-extensions]: https://developers.google.com/protocol-buffers/docs/proto#nested-extensions

[default-values]: #default_values_and_nulls

[googlesql-use-defaults]: #googlesql_use_defaults

[conversion-rules]: https://github.com/google/googlesql/blob/master/docs/conversion_rules.md

[proto2-wire-format]: https://protobuf.dev/programming-guides/encoding

[proto2-text-format]: https://protobuf.dev/reference/protobuf/textformat-spec

[oneof-type]: https://protobuf.dev/programming-guides/proto3/#oneof

[any-type]: https://protobuf.dev/programming-guides/proto3/#any

[unknown-fields-type]: https://protobuf.dev/programming-guides/proto3/#unknowns

[dot-operator]: https://github.com/google/googlesql/blob/master/docs/operators.md#field_access_operator

[correlated-join]: https://github.com/google/googlesql/blob/master/docs/query-syntax.md#correlated_join

[unnest-operator]: https://github.com/google/googlesql/blob/master/docs/query-syntax.md#unnest_operator

[new-operator]: https://github.com/google/googlesql/blob/master/docs/operators.md#new_operator

[select-as-typename]: https://github.com/google/googlesql/blob/master/docs/query-syntax.md#select_as_typename

[working-with-arrays]: https://github.com/google/googlesql/blob/master/docs/arrays.md#working_with_arrays

[cast-as-proto]: https://github.com/google/googlesql/blob/master/docs/conversion_functions.md#cast-as-proto

[link_to_safe_cast]: https://github.com/google/googlesql/blob/master/docs/conversion_functions.md#safe_casting

[link_to_expression_subquery]: https://github.com/google/googlesql/blob/master/docs/subqueries.md#expression_subquery_concepts

[proto-extract]: https://github.com/google/googlesql/blob/master/docs/protocol_buffer_functions.md#proto_extract

[proto-maps]: https://developers.google.com/protocol-buffers/docs/proto3#maps

[proto-map-cast]: https://github.com/google/googlesql/blob/master/docs/conversion_functions.md#cast

[proto-subscript-operator]: https://github.com/google/googlesql/blob/master/docs/operators.md#proto_subscript_operator

[proto-map-contains-key]: https://github.com/google/googlesql/blob/master/docs/protocol_buffer_functions.md#proto_map_contains_key

[proto-modify-map]: https://github.com/google/googlesql/blob/master/docs/protocol_buffer_functions.md#proto_modify_map

[typeless-structs]: https://github.com/google/googlesql/blob/master/docs/data-types.md#typeless_struct_syntax

[proto-functions]: https://github.com/google/googlesql/blob/master/docs/protocol_buffer_functions.md

<!-- mdlint on -->

