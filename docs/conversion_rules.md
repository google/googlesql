

<!-- mdlint off(WHITESPACE_LINE_LENGTH) -->

# Conversion rules

GoogleSQL supports conversion.
Conversion includes, but isn't limited to, casting, coercion, and
supertyping.

+ Casting is explicit conversion and uses the
  [`CAST()`][con-rules-link-to-cast] function.
+ Coercion is implicit conversion, which GoogleSQL performs
  automatically under the conditions described below.
+ A supertype is a common type to which two or more expressions can be coerced.

There are also conversions that have their own function names, such as
`PARSE_DATE()`. To learn more about these functions, see
[Conversion functions][con-rules-link-to-conversion-functions].

### Comparison of casting and coercion 
<a id="comparison_chart"></a>

The following table summarizes all possible cast and coercion possibilities for
GoogleSQL data types. The _Coerce to_ column applies to all
expressions of a given data type, (for example, a
column), but
literals and parameters can also be coerced. See
[literal coercion][con-rules-link-to-literal-coercion] and
[query parameter coercion][con-rules-link-to-parameter-coercion] for details.

<table>
  <thead>
    <tr>
      <th>From type</th>
      <th>Cast to</th>
      <th>Coerce to</th>
    </tr>
  </thead>
  <tbody>

    <tr>
      <td><code>INT32</code></td>
      <td>

<span><code>BOOL</code></span><br /><span><code>INT32</code></span><br /><span><code>INT64</code></span><br /><span><code>UINT32</code></span><br /><span><code>UINT64</code></span><br /><span><code>NUMERIC</code></span><br /><span><code>BIGNUMERIC</code></span><br /><span><code>FLOAT</code></span><br /><span><code>DOUBLE</code></span><br /><span><code>STRING</code></span><br /><span><code>ENUM</code></span><br />
</td>
      <td>

<span><code>INT64</code></span><br /><span><code>NUMERIC</code></span><br /><span><code>BIGNUMERIC</code></span><br /><span><code>DOUBLE</code></span><br />
</td>
    </tr>

    <tr>
      <td><code>INT64</code></td>
      <td>

<span><code>BOOL</code></span><br /><span><code>INT32</code></span><br /><span><code>INT64</code></span><br /><span><code>UINT32</code></span><br /><span><code>UINT64</code></span><br /><span><code>NUMERIC</code></span><br /><span><code>BIGNUMERIC</code></span><br /><span><code>FLOAT</code></span><br /><span><code>DOUBLE</code></span><br /><span><code>STRING</code></span><br /><span><code>ENUM</code></span><br />
</td>
      <td>

<span><code>NUMERIC</code></span><br /><span><code>BIGNUMERIC</code></span><br /><span><code>DOUBLE</code></span><br />
</td>
    </tr>

    <tr>
      <td><code>UINT32</code></td>
      <td>

<span><code>BOOL</code></span><br /><span><code>INT32</code></span><br /><span><code>INT64</code></span><br /><span><code>UINT32</code></span><br /><span><code>UINT64</code></span><br /><span><code>NUMERIC</code></span><br /><span><code>BIGNUMERIC</code></span><br /><span><code>FLOAT</code></span><br /><span><code>DOUBLE</code></span><br /><span><code>STRING</code></span><br /><span><code>ENUM</code></span><br />
</td>
      <td>

<span><code>INT64</code></span><br /><span><code>UINT64</code></span><br /><span><code>NUMERIC</code></span><br /><span><code>BIGNUMERIC</code></span><br /><span><code>DOUBLE</code></span><br />
</td>
    </tr>

    <tr>
      <td><code>UINT64</code></td>
      <td>

<span><code>BOOL</code></span><br /><span><code>INT32</code></span><br /><span><code>INT64</code></span><br /><span><code>UINT32</code></span><br /><span><code>UINT64</code></span><br /><span><code>NUMERIC</code></span><br /><span><code>BIGNUMERIC</code></span><br /><span><code>FLOAT</code></span><br /><span><code>DOUBLE</code></span><br /><span><code>STRING</code></span><br /><span><code>ENUM</code></span><br />
</td>
      <td>

<span><code>NUMERIC</code></span><br /><span><code>BIGNUMERIC</code></span><br /><span><code>DOUBLE</code></span><br />
</td>
    </tr>

    <tr>
      <td><code>NUMERIC</code></td>
      <td>

<span><code>INT32</code></span><br /><span><code>INT64</code></span><br /><span><code>UINT32</code></span><br /><span><code>UINT64</code></span><br /><span><code>NUMERIC</code></span><br /><span><code>BIGNUMERIC</code></span><br /><span><code>FLOAT</code></span><br /><span><code>DOUBLE</code></span><br /><span><code>STRING</code></span><br />
</td>
      <td>

<span><code>BIGNUMERIC</code></span><br /><span><code>DOUBLE</code></span><br />
</td>
    </tr>

    <tr>
      <td><code>BIGNUMERIC</code></td>
      <td>

<span><code>INT32</code></span><br /><span><code>INT64</code></span><br /><span><code>UINT32</code></span><br /><span><code>UINT64</code></span><br /><span><code>NUMERIC</code></span><br /><span><code>BIGNUMERIC</code></span><br /><span><code>FLOAT</code></span><br /><span><code>DOUBLE</code></span><br /><span><code>STRING</code></span><br />
</td>
      <td>

<span><code>DOUBLE</code></span><br />
</td>
    </tr>

    <tr>
      <td><code>FLOAT</code></td>
      <td>

<span><code>INT32</code></span><br /><span><code>INT64</code></span><br /><span><code>UINT32</code></span><br /><span><code>UINT64</code></span><br /><span><code>NUMERIC</code></span><br /><span><code>BIGNUMERIC</code></span><br /><span><code>FLOAT</code></span><br /><span><code>DOUBLE</code></span><br /><span><code>STRING</code></span><br />
</td>
      <td>

<span><code>DOUBLE</code></span><br />
</td>
    </tr>

    <tr>
      <td><code>DOUBLE</code></td>
      <td>

<span><code>INT32</code></span><br /><span><code>INT64</code></span><br /><span><code>UINT32</code></span><br /><span><code>UINT64</code></span><br /><span><code>NUMERIC</code></span><br /><span><code>BIGNUMERIC</code></span><br /><span><code>FLOAT</code></span><br /><span><code>DOUBLE</code></span><br /><span><code>STRING</code></span><br />
</td>
      <td>&nbsp;</td>
    </tr>

    <tr>
      <td><code>BOOL</code></td>
      <td>

<span><code>BOOL</code></span><br /><span><code>INT32</code></span><br /><span><code>INT64</code></span><br /><span><code>UINT32</code></span><br /><span><code>UINT64</code></span><br /><span><code>STRING</code></span><br />
</td>
      <td>&nbsp;</td>
    </tr>

    <tr>
      <td><code>STRING</code></td>
      <td>

<span><code>BOOL</code></span><br /><span><code>INT32</code></span><br /><span><code>INT64</code></span><br /><span><code>UINT32</code></span><br /><span><code>UINT64</code></span><br /><span><code>NUMERIC</code></span><br /><span><code>BIGNUMERIC</code></span><br /><span><code>FLOAT</code></span><br /><span><code>DOUBLE</code></span><br /><span><code>STRING</code></span><br /><span><code>BYTES</code></span><br /><span><code>DATE</code></span><br /><span><code>DATETIME</code></span><br /><span><code>TIME</code></span><br /><span><code>TIMESTAMP</code></span><br /><span><code>ENUM</code></span><br /><span><code>PROTO</code></span><br /><span><code>RANGE</code></span><br />

      </td>
      <td>&nbsp;</td>
    </tr>

    <tr>
      <td><code>BYTES</code></td>
      <td>

<span><code>STRING</code></span><br /><span><code>BYTES</code></span><br /><span><code>PROTO</code></span><br />
</td>
      <td>&nbsp;</td>
    </tr>

    <tr>
      <td><code>DATE</code></td>
      <td>

<span><code>STRING</code></span><br /><span><code>DATE</code></span><br /><span><code>DATETIME</code></span><br /><span><code>TIMESTAMP</code></span><br />
</td>
      <td>

<span><code>DATETIME</code></span><br />
</td>
    </tr>

    <tr>
      <td><code>DATETIME</code></td>
      <td>

<span><code>STRING</code></span><br /><span><code>DATE</code></span><br /><span><code>DATETIME</code></span><br /><span><code>TIME</code></span><br /><span><code>TIMESTAMP</code></span><br />
</td>
      <td>&nbsp;</td>
    </tr>

    <tr>
      <td><code>TIME</code></td>
      <td>

<span><code>STRING</code></span><br /><span><code>TIME</code></span><br />
</td>
      <td>&nbsp;</td>
    </tr>

    <tr>
      <td><code>TIMESTAMP</code></td>
      <td>

<span><code>STRING</code></span><br /><span><code>DATE</code></span><br /><span><code>DATETIME</code></span><br /><span><code>TIME</code></span><br /><span><code>TIMESTAMP</code></span><br />
</td>
      <td>&nbsp;</td>
    </tr>

    <tr>
      <td><code>ARRAY</code></td>
      <td>

<span><code>ARRAY</code></span><br />
</td>
      <td>&nbsp;</td>
    </tr>

    <tr>
      <td><code>ENUM</code></td>
      <td>
        <span>
          <code>ENUM</code>
          (with the same <code>ENUM</code> name)
        </span><br />
        

<span><code>INT32</code></span><br /><span><code>INT64</code></span><br /><span><code>UINT32</code></span><br /><span><code>UINT64</code></span><br /><span><code>STRING</code></span><br />

      </td>
      <td><code>ENUM</code> (with the same <code>ENUM</code> name)</td>
    </tr>

    <tr>
      <td><code>STRUCT</code></td>
      <td>

<span><code>STRUCT</code></span><br />
</td>
      <td>&nbsp;</td>
    </tr>

    <tr>
      <td><code>PROTO</code></td>
      <td>
        <span>
          <code>PROTO</code>
          (with the same <code>PROTO</code> name)
        </span><br />
        

<span><code>STRING</code></span><br /><span><code>BYTES</code></span><br />

      </td>
      <td><code>PROTO</code> (with the same <code>PROTO</code> name)</td>
    </tr>

    <tr>
      <td><code>RANGE</code></td>
      <td>

<span><code>RANGE</code></span><br /><span><code>STRING</code></span><br />
</td>
      <td>&nbsp;</td>
    </tr>

  </tbody>
</table>

### Casting

Most data types can be cast from one type to another with the `CAST` function.
When using `CAST`, a query can fail if GoogleSQL is unable to perform
the cast. If you want to protect your queries from these types of errors, you
can use `SAFE_CAST`. To learn more about the rules for `CAST`, `SAFE_CAST` and
other casting functions, see
[Conversion functions][con-rules-link-to-conversion-functions].

### Coercion

GoogleSQL coerces the result type of an argument expression to another
type if needed to match function signatures. For example, if function `func()`
is defined to take a single argument of type `DOUBLE`
and an expression is used as an argument that has a result type of
`INT64`, then the result of the expression will be
coerced to `DOUBLE` type before `func()` is computed.

#### Literal coercion

GoogleSQL supports the following literal coercions:

<table>
  <thead>
    <tr>
      <th>Input data type</th>
      <th>Result data type</th>
      <th>Notes</th>
    </tr>
  </thead>
  <tbody>

  <tr>
    <td>Integer literal</td>
    <td>

<span><code>INT32</code></span><br /><span><code>UINT32</code></span><br /><span><code>UINT64</code></span><br /><span><code>ENUM</code></span><br />
</td>
    <td>
      
      Integer literals will implicitly coerce to <code>ENUM</code> type when
      necessary, or can be explicitly cast to a specific
      <code>ENUM</code> type name.
      
    </td>
  </tr>

  <tr>
    <td><code>DOUBLE</code> literal</td>
    <td>

<span><code>NUMERIC</code></span><br /><span><code>FLOAT</code></span><br />
</td>
    <td>Coercion may not be exact, and returns a close value.</td>
  </tr>

  <tr>
    <td><code>STRING</code> literal</td>
    <td>

<span><code>DATE</code></span><br /><span><code>DATETIME</code></span><br /><span><code>TIME</code></span><br /><span><code>TIMESTAMP</code></span><br /><span><code>ENUM</code></span><br /><span><code>PROTO</code></span><br />
</td>
    <td>
      
      String literals will implicitly coerce to <code>PROTO</code> or
      <code>ENUM</code> type when necessary, or can be explicitly cast to a specific
      <code>PROTO</code> or <code>ENUM</code> type name.
      
    </td>
  </tr>

  <tr>
    <td><code>BYTES</code> literal</td>
    <td>

<span><code>PROTO</code></span><br />
</td>
    <td>&nbsp;</td>
  </tr>

  </tbody>
</table>

Literal coercion is needed when the actual literal type is different from the
type expected by the function in question. For
example, if function `func()` takes a DATE argument,
then the expression `func("2014-09-27")` is valid because the
string literal `"2014-09-27"` is coerced to
`DATE`.

Literal conversion is evaluated at analysis time, and gives an error if the
input literal can't be converted successfully to the target type.

Note: String literals don't coerce to numeric types.

#### Query parameter coercion 
<a id="parameter_coercion"></a>

GoogleSQL supports the following query parameter coercions:

<table>
  <thead>
    <tr>
      <th>Input data type</th>
      <th>Result data type</th>
    </tr>
  </thead>
  <tbody>

    <tr>
      <td><code>INT32</code> parameter</td>
      <td>

<span><code>ENUM</code></span><br />
</td>
    </tr>

    <tr>
      <td><code>INT64</code> parameter</td>
      <td>

<span><code>ENUM</code></span><br />
</td>
    </tr>

    <tr>
      <td><code>STRING parameter</code></td>
      <td>

<span><code>DATE</code></span><br /><span><code>DATETIME</code></span><br /><span><code>TIME</code></span><br /><span><code>TIMESTAMP</code></span><br /><span><code>ENUM</code></span><br /><span><code>PROTO</code></span><br />
</td>
    </tr>

    <tr>
      <td><code>BYTES</code> parameter</td>
      <td><code>PROTO</code></td>
    </tr>

    <tr>
      <td><code>GRAPH_ELEMENT</code> parameter</td>
      <td>
        

<span><code>GRAPH_ELEMENT</code></span><br />

        <p>
          Coercion is only allowed from one graph element type to another
          graph element type if the second graph element type is a supertype of
          the first. After the conversion, the graph element type can access the
          properties that are described in its supertype.
        </p>
        <p>
          With <code>GRAPH_ELEMENT</code> coercion, the property reference
          returns <code>NULL</code>.
        </p>
      </td>
    </tr>

  </tbody>
</table>

If the parameter value can't be coerced successfully to the target type, an
error is provided.

### Supertypes

A supertype is a common type to which two or more expressions can be coerced.
Supertypes are used with set operations such as `UNION ALL` and expressions such
as `CASE` that expect multiple arguments with matching types. Each type has one
or more supertypes, including itself, which defines its set of supertypes.

<table>
  <thead>
    <tr>
      <th>Input type</th>
      <th>Supertypes</th>
    </tr>
  </thead>
  <tbody>

    <tr>
      <td><code>BOOL</code></td>
      <td>

<span><code>BOOL</code></span><br />
</td>
    </tr>

    <tr>
      <td><code>INT32</code></td>
      <td>

<span><code>INT32</code></span><br /><span><code>INT64</code></span><br /><span><code>FLOAT</code></span><br /><span><code>DOUBLE</code></span><br /><span><code>NUMERIC</code></span><br /><span><code>BIGNUMERIC</code></span><br />
</td>
    </tr>

    <tr>
      <td><code>INT64</code></td>
      <td>

<span><code>INT64</code></span><br /><span><code>FLOAT</code></span><br /><span><code>DOUBLE</code></span><br /><span><code>NUMERIC</code></span><br /><span><code>BIGNUMERIC</code></span><br />
</td>
    </tr>

    <tr>
      <td><code>UINT32</code></td>
      <td>

<span><code>UINT32</code></span><br /><span><code>INT64</code></span><br /><span><code>UINT64</code></span><br /><span><code>FLOAT</code></span><br /><span><code>DOUBLE</code></span><br /><span><code>NUMERIC</code></span><br /><span><code>BIGNUMERIC</code></span><br />
</td>
    </tr>

    <tr>
      <td><code>UINT64</code></td>
      <td>

<span><code>UINT64</code></span><br /><span><code>FLOAT</code></span><br /><span><code>DOUBLE</code></span><br /><span><code>NUMERIC</code></span><br /><span><code>BIGNUMERIC</code></span><br />
</td>
    </tr>

    <tr>
      <td><code>FLOAT</code></td>
      <td>

<span><code>FLOAT</code></span><br /><span><code>DOUBLE</code></span><br />
</td>
    </tr>

    <tr>
      <td><code>DOUBLE</code></td>
      <td>

<span><code>DOUBLE</code></span><br />
</td>
    </tr>

    <tr>
      <td><code>NUMERIC</code></td>
      <td>

<span><code>NUMERIC</code></span><br /><span><code>BIGNUMERIC</code></span><br /><span><code>DOUBLE</code></span><br />
</td>
    </tr>

    <tr>
      <td><code>DECIMAL</code></td>
      <td>

<span><code>DECIMAL</code></span><br /><span><code>BIGDECIMAL</code></span><br /><span><code>DOUBLE</code></span><br />
</td>
    </tr>

    <tr>
      <td><code>BIGNUMERIC</code></td>
      <td>

<span><code>BIGNUMERIC</code></span><br /><span><code>DOUBLE</code></span><br />
</td>
    </tr>

    <tr>
      <td><code>BIGDECIMAL</code></td>
      <td>

<span><code>BIGDECIMAL</code></span><br /><span><code>DOUBLE</code></span><br />
</td>
    </tr>

    <tr>
      <td><code>STRING</code></td>
      <td>

<span><code>STRING</code></span><br />
</td>
    </tr>

    <tr>
      <td><code>DATE</code></td>
      <td>

<span><code>DATE</code></span><br />
</td>
    </tr>

    <tr>
      <td><code>TIME</code></td>
      <td>

<span><code>TIME</code></span><br />
</td>
    </tr>

    <tr>
      <td><code>DATETIME</code></td>
      <td>

<span><code>DATETIME</code></span><br />
</td>
    </tr>

    <tr>
      <td><code>TIMESTAMP</code></td>
      <td>

<span><code>TIMESTAMP</code></span><br />
</td>
    </tr>

    <tr>
      <td><code>ENUM</code></td>
      <td>
        <code>ENUM</code> with the same name. The resulting enum supertype is
        the one that occurred first.
      </td>
    </tr>

    <tr>
      <td><code>BYTES</code></td>
      <td>

<span><code>BYTES</code></span><br />
</td>
    </tr>

    <tr>
      <td><code>STRUCT</code></td>
      <td><code>STRUCT</code> with the same field position types.</td>
    </tr>

    <tr>
      <td><code>ARRAY</code></td>
      <td><code>ARRAY</code> with the same element types.</td>
    </tr>

    <tr>
      <td><code>PROTO</code></td>
      <td>
        <code>PROTO</code> with the same name. The resulting <code>PROTO</code>
        supertype is the one that occurred first. For example, the first
        occurrence could be in the first branch of a set operation or the first
        result expression in a <code>CASE</code> statement.
      </td>
    </tr>

    <tr>
      <td><code>GEOGRAPHY</code></td>
      <td>

<span><code>GEOGRAPHY</code></span><br />
</td>
    </tr>

    <tr>
      <td><code>GRAPH_ELEMENT</code></td>
      <td>
        <code>GRAPH_ELEMENT</code>. A graph element can be a supertype of
        another graph element if the following is true:
        <ul>
          <li>
            Graph element <code>a</code> is a supertype of graph element
            <code>b</code> and they're the same element kind.
          </li>
          <li>
            Graph element <code>a</code>'s property type list is a
            compatible superset of graph element <code>b</code>'s
            property type list. This means that properties with the same name
            must also have the same type.
          </li>
        </ul>
      </td>
    </tr>

    <tr>
      <td><code>RANGE</code></td>
      <td><code>RANGE</code> with the same subtype.</td>
    </tr>

  </tbody>
</table>

If you want to find the supertype for a set of input types, first determine the
intersection of the set of supertypes for each input type. If that set is empty
then the input types have no common supertype. If that set is non-empty, then
the common supertype is generally the
[most specific][con-supertype-specificity] type in that set. Generally,
the most specific type is the type with the most restrictive domain.

**Examples**

<table>
  <thead>
    <tr>
      <th>Input types</th>
      <th>Common supertype</th>
      <th>Returns</th>
      <th>Notes</th>
    </tr>
  </thead>
  <tbody>
    
    <tr>
      <td>

<span><code>INT64</code></span><br /><span><code>FLOAT</code></span><br />
</td>
      <td><code>DOUBLE</code></td>
      <td><code>DOUBLE</code></td>
      <td>
        If you apply supertyping to <code>INT64</code> and <code>FLOAT</code>,
        supertyping succeeds because they they share a supertype,
        <code>DOUBLE</code>.
      </td>
    </tr>
    
    <tr>
      <td>

<span><code>INT64</code></span><br /><span><code>DOUBLE</code></span><br />
</td>
      <td><code>DOUBLE</code></td>
      <td><code>DOUBLE</code></td>
      <td>
        If you apply supertyping to <code>INT64</code> and
        <code>DOUBLE</code>,
        supertyping succeeds because they they share a supertype,
        <code>DOUBLE</code>.
      </td>
    </tr>
    <tr>
      <td>

<span><code>INT64</code></span><br /><span><code>BOOL</code></span><br />
</td>
      <td>None</td>
      <td>Error</td>
      <td>
        If you apply supertyping to <code>INT64</code> and <code>BOOL</code>,
        supertyping fails because they don't share a common supertype.
      </td>
    </tr>
  </tbody>
</table>

#### Exact and inexact types

Numeric types can be exact or inexact. For supertyping, if all of the
input types are exact types, then the resulting supertype can only be an
exact type.

The following table contains a list of exact and inexact numeric data types.

<table>
  <thead>
    <tr>
      <th>Exact types</th>
      <th>Inexact types</th>
    </tr>
  </thead>
  <tbody>
    <tr>
      <td>

<span><code>INT32</code></span><br /><span><code>UINT32</code></span><br /><span><code>INT64</code></span><br /><span><code>UINT64</code></span><br /><span><code>NUMERIC</code></span><br /><span><code>BIGNUMERIC</code></span><br />
</td>
      <td>

<span><code>FLOAT</code></span><br /><span><code>DOUBLE</code></span><br />
</td>
    </tr>
  </tbody>
</table>

**Examples**

<table>
  <thead>
    <tr>
      <th>Input types</th>
      <th>Common supertype</th>
      <th>Returns</th>
      <th>Notes</th>
    </tr>
  </thead>
  <tbody>
    
    <tr>
      <td>

<span><code>UINT64</code></span><br /><span><code>INT64</code></span><br />
</td>
      <td><code>DOUBLE</code></td>
      <td>Error</td>
      <td>
        If you apply supertyping to <code>INT64</code> and <code>UINT64</code>,
        supertyping fails because they are both exact numeric types and the only
        shared supertype is <code>DOUBLE</code>, which
        is an inexact numeric type.
      </td>
    </tr>
    
    
    <tr>
      <td>

<span><code>UINT32</code></span><br /><span><code>INT32</code></span><br />
</td>
      <td><code>INT64</code></td>
      <td><code>INT64</code></td>
      <td>
        If you apply supertyping to <code>INT32</code> and <code>UINT32</code>,
        supertyping succeeds because they are both exact numeric types and they
        share an exact supertype, <code>INT64</code>.
      </td>
    </tr>
    
    <tr>
      <td>

<span><code>INT64</code></span><br /><span><code>DOUBLE</code></span><br />
</td>
      <td><code>DOUBLE</code></td>
      <td><code>DOUBLE</code></td>
      <td>
        If supertyping is applied to <code>INT64</code> and <code>DOUBLE</code>,
        supertyping succeeds because there are exact and inexact numeric types
        being supertyped.
      </td>
    </tr>
    
    <tr>
      <td>

<span><code>UINT64</code></span><br /><span><code>INT64</code></span><br /><span><code>DOUBLE</code></span><br />
</td>
      <td><code>DOUBLE</code></td>
      <td><code>DOUBLE</code></td>
      <td>
        If supertyping is applied to <code>INT64</code>, <code>UINT64</code>,
        and <code>DOUBLE</code>, supertyping succeeds
        because there are exact and inexact numeric types being supertyped.
      </td>
    </tr>
    
  </tbody>
</table>

#### Types specificity 
<a id="supertype_specificity"></a>

Each type has a domain of values that it supports. A type with a
narrow domain is more specific than a type with a wider domain. Exact types
are more specific than inexact types because inexact types have a wider range
of domain values that are supported than exact types. For example,
`INT64` is more specific than `DOUBLE`.

#### Supertypes and literals

Supertype rules for literals are more permissive than for normal expressions,
and are consistent with implicit coercion rules. The following algorithm is used
when the input set of types includes types related to literals:

+ If there exists non-literals in the set, find the set of common supertypes
  of the non-literals.
+ If there is at least one possible supertype, find the
  [most specific][con-supertype-specificity] type to
  which the remaining literal types can be implicitly coerced and return that
  supertype. Otherwise, there is no supertype.
+ If the set only contains types related to literals, compute the supertype of
  the literal types.
+ If all input types are related to `NULL` literals, then the resulting
  supertype is `INT64`.
+ If no common supertype is found, an error is produced.

**Examples**

<table>
  <thead>
    <tr>
      <th>Input types</th>
      <th>Common supertype</th>
      <th>Returns</th>
    </tr>
  </thead>
  <tbody>
    
    <tr>
      <td>
        <code>INT64</code> literal<br />
        <code>INT32</code> expression<br />
      </td>
      <td><code>INT32</code></td>
      <td><code>INT32</code></td>
    </tr>
    
    
    <tr>
      <td>
        <code>INT64</code> literal<br />
        <code>UINT32</code> expression<br />
      </td>
      <td><code>UINT32</code></td>
      <td><code>UINT32</code></td>
    </tr>
    
    <tr>
      <td>
        <code>INT64</code> literal<br />
        <code>UINT64</code> expression<br />
      </td>
      <td><code>UINT64</code></td>
      <td><code>UINT64</code></td>
    </tr>
    
    <tr>
      <td>
        <code>DOUBLE</code> literal<br />
        <code>FLOAT</code> expression<br />
      </td>
      <td><code>FLOAT</code></td>
      <td><code>FLOAT</code></td>
    </tr>
    
    
    <tr>
      <td>
        <code>INT64</code> literal<br />
        <code>DOUBLE</code> literal<br />
      </td>
      <td><code>DOUBLE</code></td>
      <td><code>DOUBLE</code></td>
    </tr>
    
    
    <tr>
      <td>
        <code>INT64</code> expression<br />
        <code>UINT64</code> expression<br />
        DOUBLE literal<br />
      </td>
      <td><code>DOUBLE</code></td>
      <td><code>DOUBLE</code></td>
    </tr>
    
    
    <tr>
      <td>
        <code>TIMESTAMP</code> expression<br />
        <code>STRING</code> literal<br />
      </td>
      <td><code>TIMESTAMP</code></td>
      <td><code>TIMESTAMP</code></td>
    </tr>
    
    <tr>
      <td>
        <code>NULL</code> literal<br />
        <code>NULL</code> literal<br />
      </td>
      <td><code>INT64</code></td>
      <td><code>INT64</code></td>
    </tr>
    <tr>
      <td>
        <code>BOOL</code> literal<br />
        <code>TIMESTAMP</code> literal<br />
      </td>
      <td>None</td>
      <td>Error</td>
    </tr>
  </tbody>
</table>

<!-- mdlint off(WHITESPACE_LINE_LENGTH) -->

[conversion-rules-table]: #conversion_rules

[con-rules-link-to-literal-coercion]: #literal_coercion

[con-rules-link-to-parameter-coercion]: #parameter_coercion

[con-rules-link-to-casting]: #casting

[con-supertype-specificity]: #supertype_specificity

[con-rules-link-to-time-zones]: https://github.com/google/googlesql/blob/master/docs/data-types.md#time_zones

[con-rules-link-to-conversion-functions]: https://github.com/google/googlesql/blob/master/docs/conversion_functions.md

[con-rules-link-to-cast]: https://github.com/google/googlesql/blob/master/docs/conversion_functions.md#cast

<!-- mdlint on -->

