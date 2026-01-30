

<!-- mdlint off(WHITESPACE_LINE_LENGTH) -->

# Data model

The following sections provide an overview of the GoogleSQL data
model.

## Standard SQL tables

GoogleSQL data is stored in tables. Each table consists of an ordered
list of columns and a number of rows. Each column has a name used to identify it
through SQL statements, and is assigned a specific data type.

For example, the following table, **Singers**, is a standard SQL table.

<table>
<thead>
<tr>
<th>Column Name</th>
<th>Data Type</th>
<th>Default Value</th>
</tr>
</thead>
<tbody>
<tr>
<td>SingerId</td>
<td><code>INT64</code></td>
<td><code>&lt;auto-increment&gt;</code></td>
</tr>
<tr>
<td>FirstName</td>
<td><code>STRING</code></td>
<td>&nbsp;</td>
</tr>
<tr>
<td>LastName</td>
<td><code>STRING</code></td>
<td>&nbsp;</td>
</tr>
<tr>
<td>BirthDate</td>
<td><code>DATE</code></td>
<td>&nbsp;</td>
</tr>
<tr>
<td>Status</td>
<td><code>STRING</code></td>
<td><code>"active"</code></td>
</tr>
<tr>
<td>SingerInfo</td>
<td><code>PROTO&lt;SingerMetadata&gt;</code></td>
<td>&nbsp;</td>
</tr>
<tr>
<td>Albums</td>
<td><code>PROTO&lt;Album&gt;</code></td>
<td>&nbsp;</td>
</tr>
</tbody>
</table>

The proto, `SingerMetadata`, has the following definition:

```
message SingerMetadata {
  optional string    nationality = 1;
  repeated Residence residence   = 2;

  message Residence {
    required int64  start_year   = 1;
    optional int64  end_year     = 2;
    optional string city         = 3;
    optional string country      = 4 [default = "USA"];
  }
}
```

A `SELECT *` statement on this table would return rows similar to the following:

<table>
<thead>
<tr>
<th>SingerId</th>
<th>FirstName</th>
<th>LastName</th>
<th>BirthDate</th>
<th>Status</th>
<th>SingerInfo</th>
</tr>
</thead>
<tbody>
<tr>
<td>1</td>
<td>Marc</td>
<td>Richards</td>
<td>1970-09-03</td>
<td>active</td>
<td>{nationality: "England"}</td>
</tr>
<tr>
<td>2</td>
<td>Catalina</td>
<td>Smith</td>
<td>1990-08-17</td>
<td>inactive</td>
<td>{nationality: "U.S.A."}</td>
</tr>
<tr>
<td>3</td>
<td>Lea</td>
<td>Martin</td>
<td>1991-11-09</td>
<td>active</td>
<td>{nationality: "Australia"}</td>
</tr>
<tr>
<td>4</td>
<td>Xanathe</td>
<td>Riou</td>
<td>1995-05-23</td>
<td>inactive</td>
<td>{nationality: U.S.A."}</td>
</tr>
</tbody>
</table>

While tables don't have a type, some operations will construct an implicit
`STRUCT` type out of a SQL row, using the column names and types for field
definitions.

For more information on the data types GoogleSQL supports, see
[Data Types][data-types].

## Constraints

Constraints require that any writes to one or more columns, such as inserts or
updates, conform to certain rules.
[Data manipulation language (DML)][data-manipulation-language]
statements enforce constraints. GoogleSQL supports the
following constraints:

* **Primary key constraint.** A primary key consists of one or more columns, and
  specifies that the value of each row of these combined columns must be unique
  within that table. A table can contain at most one primary key constraint.

  Some [data manipulation language (DML)][data-manipulation-language]
  keywords may require the existence of a primary key.
  GoogleSQL also implicitly builds an index on the primary key. The
  default order of this index is ascending. The primary key can contain `NULL`
  values.
* **Unique constraint.** Specifies that one or more columns must contain only
  unique values. Unlike a primary key, more than one unique constraint can exist
  on a table.

## Indexes

An index allows the database engine to query a column or set of columns more
quickly. You can specify that sort order is ascending or descending. A unique
or primary key index defines an indexed column that's subject to the uniqueness
constraint.

## Pseudocolumns 
<a id="pseudo_columns"></a>

GoogleSQL tables support pseudocolumns. Pseudocolumns contain data elements
that you can query like regular columns, but aren't considered real columns in
the table. Pseudocolumn values may not be physically stored with each row, but
the query engine will materialize a value for the column using some appropriate
mechanism.

For example, an engine might support a pseudocolumn called `ROWNUM`, which
returns a number indicating the order in which a row was returned. You can then
construct a query like this:

```
SELECT ROWNUM, SingerId, FirstName, LastName FROM Singers
WHERE Status = "active"
```

Here's an example of rows returned by this query:

<table>
<thead>
<tr>
<th>ROWNUM</th>
<th>SingerId</th>
<th>FirstName</th>
<th>LastName</th>
</tr>
</thead>
<tbody>
<tr>
<td>1</td>
<td>1</td>
<td>Marc</td>
<td>Richards</td>
</tr>
<tr>
<td>2</td>
<td>3</td>
<td>Lea</td>
<td>Martin</td>
</tr>
</tbody>
</table>

In this case,the schema for the **Singers** table doesn't define a column,
`ROWNUM`. Instead, the engine materializes the data only when requested.

To return a value of a pseudocolumn, you must specify it in your query.
Pseudocolumns don't show up in `SELECT *` statements. For example:

```
SELECT * FROM singers
```

This query will return all named columns in the table, but won't include
pseudocolumns such as `ROWNUM`.

## Value tables

In addition to standard SQL _tables_, GoogleSQL supports _value tables_.
In a value table, rather than having rows made up of a list of columns, each row
is a single value of type `STRUCT`, and there are no column names.

In the following example, a value table for a `STRUCT` is produced with the
`SELECT AS VALUE` statement:

```googlesql
SELECT * FROM (SELECT AS VALUE STRUCT(123 AS a, FALSE AS b))

/*-----+-------+
 | a   | b     |
 +-----+-------+
 | 123 | FALSE |
 +-----+-------*/
```

Value tables are common when working with protocol buffers that may be stored in
files instead of in the database.

<a id="value_table_example"></a>

For example, the following protocol buffer definition, `AlbumReview`, contains
data about the reviews for an album.

```googlesql
message AlbumReview {
  optional string albumtitle = 1;
  optional string reviewer = 2;
  optional string review = 3;
}
```

A list of `AlbumReview` protocol buffers is stored in a file, `AlbumReviewData`.

```json
{albumtitle: "Songs on a Broken Banjo", reviewer: "Dan Starling", review: "Off key"}
{albumtitle: "Six and Seven", reviewer: "Alice Wayfarer", review: "Hurt my ears!"}
{albumtitle: "Go! Go! Go!", reviewer: "Eustace Millson", review: "My kids loved it!"}
```

The following query returns a stream of rows, with each row a value of type
`AlbumReview`.

```googlesql
SELECT a FROM AlbumReviewsData AS a
```

To get specific data, such as all album titles in
the table, you have two options. You can specify `albumtitle` as a protocol
buffer field:

```googlesql
SELECT a.albumtitle FROM AlbumReviewsData AS a
```

You can also access the top-level fields inside the value like columns in a
table:

```googlesql
SELECT albumtitle FROM AlbumReviewsData
```

### Return query results as a value table

You can use GoogleSQL to return query results as a value table. This
is useful when you want to store a query result with a
`PROTO` or  type as a
table. To return a query result as a value table, use one of the following
statements:

+ [`SELECT AS typename`][query-syntax-select]
+ [`SELECT AS STRUCT`][query-syntax-select]
+ [`SELECT AS VALUE`][query-syntax-select]

Value tables can also occur as the output of the [`UNNEST`][unnest-operator]
operator or a [subquery][subquery-concepts]. The [`WITH` clause][with-clause]
introduces a value table if the subquery used produces a value table.

In contexts where a query with exactly one column is expected, a value table
query can be used instead. For example, scalar and
array [subqueries][subquery-concepts] normally require a single-column query,
but in GoogleSQL, they also allow using a value table query.

Most commonly, value tables are used for protocol buffer value tables, where the
table contains a stream of protocol buffer values. In this case, the top-level
protocol buffer fields can be used in the same way that column names are used
when querying a regular table.

### Copy a protocol buffer using a value table

In some cases you might not want to work with the data within a protocol buffer,
but with the protocol buffer itself.

Using `SELECT AS VALUE` can help you keep your GoogleSQL statements as
simple as possible. To illustrate this, consider the
[AlbumReview][value-table-example] example specified earlier. To create a new
table from this data, you could write:

```googlesql
CREATE TABLE Reviews AS
SELECT albumreviews FROM AlbumReviewData AS albumreviews;
```

This statement creates a table that has a single column,
`albumreviews`, which has a protocol buffer value of type
`AlbumReviewData`. To retrieve all album titles from this table, you'd need to
write a query similar to:

```googlesql
SELECT r.albumreviews.albumtitle
FROM Reviews AS r;
```

Now, consider the same initial `CREATE TABLE` statement, this time modified to
use `SELECT AS VALUE`:

```googlesql
CREATE TABLE Reviews AS
SELECT AS VALUE albumreviews FROM AlbumReview AS albumreviews;
```

This statement creates a value table, instead of table. As a result, you can
query any protocol buffer field as if it was a column. Now, if
you want to retrieve all album titles from this table, you can write a much
simpler query:

```googlesql
SELECT albumtitle
FROM Reviews;
```

### Use a set operation on a value table

In `SET` operations like `UNION ALL` you can combine tables with value tables,
provided that the table consists of a single column with a type that matches the
value table's type. The result of these operations is always a value table.

For example, consider the following definition for a table,
`SingersAndAlbums`.

<table>
<thead>
<tr>
<th>Column Name</th>
<th>Data Type</th>
</tr>
</thead>
<tbody>
<tr>
<td>SingerId</td>
<td><code>INT64</code></td>
</tr>
<tr>
<td>AlbumId</td>
<td><code>INT64</code></td>
</tr>
<tr>
<td>AlbumReview</td>
<td><code>PROTO&lt;AlbumReview&gt;</code></td>
</tr>
</tbody>
</table>

Next, we have a file, `AlbumReviewData` that contains a list of `AlbumReview`
protocol buffers.

```googlesql
{albumtitle: "Songs on a Broken Banjo", reviewer: "Dan Starling", review: "Off key"}
{albumtitle: "Six and Seven", reviewer: "Alice Wayfarer", review: "Hurt my ears!"}
{albumtitle: "Go! Go! Go!", reviewer: "Eustace Millson", review: "My kids loved it!"}
```

The following query combines the `AlbumReview` data from the
`SingersAndAlbums` table with the data stored in the `AlbumReviewData` file and
stores it in a new value table, `AllAlbumReviews`.

```googlesql
SELECT AS VALUE sa.AlbumReview FROM SingersAndAlbums AS sa
UNION ALL
SELECT a FROM AlbumReviewData AS a
```

### Pseudocolumns and value tables

In most cases, [pseudocolumns][pseudocolumns] work the same with
value tables as they do with tables.

For example, consider the following query and its results. This example works
because `a` is an alias of the table `AlbumReviewData`, and this table has a
`ROWNUM` pseudocolumn. As a result, `AlbumReviewData AS a` represents the
scanned rows, not the value.

```googlesql
-- This works
SELECT a.ROWNUM, a.albumtitle AS title FROM AlbumReviewData AS a

/*--------+---------------------------+
 | ROWNUM | title                     |
 +--------+---------------------------+
 | 1      | "Songs on a Broken Banjo" |
 | 2      | "Six and Seven"           |
 | 3      | "Go! Go! Go!"             |
 +--------+---------------------------*/
```

However, if you try to construct the query as follows, the query doesn't work.
The reason it fails is because the subquery,
`SELECT a FROM AlbumReviewData AS a`, returns the `AlbumReviewData` value only,
and this value doesn't have a field called `ROWNUM`.

```googlesql
-- This fails
SELECT a.ROWNUM, a.albumtitle AS title FROM (SELECT a FROM AlbumReviewData AS a)
```

<!-- mdlint off(WHITESPACE_LINE_LENGTH) -->

[subquery-concepts]: https://github.com/google/googlesql/blob/master/docs/subqueries.md

[value-table-example]: #value_table_example

[pseudocolumns]: #pseudo_columns

[unnest-operator]: https://github.com/google/googlesql/blob/master/docs/query-syntax.md#unnest_operator

[with-clause]: https://github.com/google/googlesql/blob/master/docs/query-syntax.md#with_clause

[query-syntax-select]: https://github.com/google/googlesql/blob/master/docs/query-syntax.md#select_list

<!-- mdlint on -->

## Property graphs 
<a id="property_graphs"></a>

A property graph is a directed graph that includes the following parts:

+ Nodes: Each node has a set of labels and properties. Each label has a
  name identifier and determines a set of properties. Each property has a
  name identifier and a value type.
+ Edges: Similar to nodes, each edge has
  a set of labels and properties. Additionally, directed edges
  include source nodes and destination nodes.
+ Labels: A label is identified by a unique name in the property graph and
  determines a set of properties. Nodes and edges can expose the same set of
  properties, using the same label.
+ Properties: A property is identified by a unique name in a property graph.
  Properties declared on any label on a node or edge table are declared across
  the whole enclosing property graph, so they must always be consistent.

To create a property graph on top of a relational dataset, see the
[CREATE PROPERTY GRAPH statement][create-property-graph] in the DDL.

<!-- mdlint off(WHITESPACE_LINE_LENGTH) -->

[data-types]: https://github.com/google/googlesql/blob/master/docs/data-types.md

[data-manipulation-language]: https://github.com/google/googlesql/blob/master/docs/data-manipulation-language.md

[create-property-graph]: https://github.com/google/googlesql/blob/master/docs/data-definition-language.md#create_property_graph

<!-- mdlint on -->

