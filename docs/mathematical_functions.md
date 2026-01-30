

<!-- mdlint off(WHITESPACE_LINE_LENGTH) -->

# Mathematical functions

GoogleSQL supports mathematical functions.
All mathematical functions have the following behaviors:

+  They return `NULL` if any of the input parameters is `NULL`.
+  They return `NaN` if any of the arguments is `NaN`.

## Categories

<!-- disableFinding(LINK_ID) -->
<table>
  <thead>
    <tr>
      <th>Category</th>
      <th>Functions</th>
    </tr>
  </thead>
  <tbody>
    <tr>
      <td>Trigonometric</td>
      <td>
        <a href="#acos"><code>ACOS</code></a>&nbsp;&nbsp;
        <a href="#acosh"><code>ACOSH</code></a>&nbsp;&nbsp;
        <a href="#asin"><code>ASIN</code></a>&nbsp;&nbsp;
        <a href="#asinh"><code>ASINH</code></a>&nbsp;&nbsp;
        <a href="#atan"><code>ATAN</code></a>&nbsp;&nbsp;
        <a href="#atan2"><code>ATAN2</code></a>&nbsp;&nbsp;
        <a href="#atanh"><code>ATANH</code></a>&nbsp;&nbsp;
        <a href="#cos"><code>COS</code></a>&nbsp;&nbsp;
        <a href="#cosh"><code>COSH</code></a>&nbsp;&nbsp;
        <a href="#cot"><code>COT</code></a>&nbsp;&nbsp;
        <a href="#coth"><code>COTH</code></a>&nbsp;&nbsp;
        <a href="#csc"><code>CSC</code></a>&nbsp;&nbsp;
        <a href="#csch"><code>CSCH</code></a>&nbsp;&nbsp;
        <a href="#sec"><code>SEC</code></a>&nbsp;&nbsp;
        <a href="#sech"><code>SECH</code></a>&nbsp;&nbsp;
        <a href="#sin"><code>SIN</code></a>&nbsp;&nbsp;
        <a href="#sinh"><code>SINH</code></a>&nbsp;&nbsp;
        <a href="#tan"><code>TAN</code></a>&nbsp;&nbsp;
        <a href="#tanh"><code>TANH</code></a>&nbsp;&nbsp;
      </td>
    </tr>
    <tr>
      <td>
        Exponential and<br />
        logarithmic
      </td>
      <td>
        <a href="#exp"><code>EXP</code></a>&nbsp;&nbsp;
        <a href="#ln"><code>LN</code></a>&nbsp;&nbsp;
        <a href="#log"><code>LOG</code></a>&nbsp;&nbsp;
        <a href="#log10"><code>LOG10</code></a>&nbsp;&nbsp;
      </td>
    </tr>
    <tr>
      <td>
        Rounding and<br />
        truncation
      </td>
      <td>
        <a href="#ceil"><code>CEIL</code></a>&nbsp;&nbsp;
        <a href="#ceiling"><code>CEILING</code></a>&nbsp;&nbsp;
        <a href="#floor"><code>FLOOR</code></a>&nbsp;&nbsp;
        <a href="#round"><code>ROUND</code></a>&nbsp;&nbsp;
        <a href="#trunc"><code>TRUNC</code></a>&nbsp;&nbsp;
      </td>
    </tr>
    <tr>
      <td>
        Power and<br />
        root
      </td>
      <td>
        <a href="#cbrt"><code>CBRT</code></a>&nbsp;&nbsp;
        <a href="#pow"><code>POW</code></a>&nbsp;&nbsp;
        <a href="#power"><code>POWER</code></a>&nbsp;&nbsp;
        <a href="#sqrt"><code>SQRT</code></a>&nbsp;&nbsp;
      </td>
    </tr>
    <tr>
      <td>Sign</td>
      <td>
        <a href="#abs"><code>ABS</code></a>&nbsp;&nbsp;
        <a href="#sign"><code>SIGN</code></a>&nbsp;&nbsp;
      </td>
    </tr>
    
    <tr>
      <td>
        Distance
      </td>
      <td>
        
        
        
        
        <a href="#cosine_distance"><code>COSINE_DISTANCE</code></a>&nbsp;&nbsp;
        <a href="#euclidean_distance"><code>EUCLIDEAN_DISTANCE</code></a>&nbsp;&nbsp;
      </td>
    </tr>
    
    <tr>
      <td>
        Comparison
      </td>
      <td>
        <a href="#greatest"><code>GREATEST</code></a>&nbsp;&nbsp;
        <a href="#least"><code>LEAST</code></a>&nbsp;&nbsp;
      </td>
    </tr>
    
    <tr>
      <td>Random number generator</td>
      <td>
        <a href="#rand"><code>RAND</code></a>&nbsp;&nbsp;
      </td>
    </tr>
    
    <tr>
      <td>Arithmetic and error handling</td>
      <td>
        <a href="#div"><code>DIV</code></a>&nbsp;&nbsp;
        <a href="#ieee_divide"><code>IEEE_DIVIDE</code></a>&nbsp;&nbsp;
        <a href="#is_inf"><code>IS_INF</code></a>&nbsp;&nbsp;
        <a href="#is_nan"><code>IS_NAN</code></a>&nbsp;&nbsp;
        <a href="#mod"><code>MOD</code></a>&nbsp;&nbsp;
        <a href="#safe_add"><code>SAFE_ADD</code></a>&nbsp;&nbsp;
        <a href="#safe_divide"><code>SAFE_DIVIDE</code></a>&nbsp;&nbsp;
        <a href="#safe_multiply"><code>SAFE_MULTIPLY</code></a>&nbsp;&nbsp;
        <a href="#safe_negate"><code>SAFE_NEGATE</code></a>&nbsp;&nbsp;
        <a href="#safe_subtract"><code>SAFE_SUBTRACT</code></a>&nbsp;&nbsp;
      </td>
    </tr>
    
    <tr>
      <td>Bucket</td>
      <td>
        <a href="#range_bucket"><code>RANGE_BUCKET</code></a>&nbsp;&nbsp;
      </td>
    </tr>
    
    
    <tr>
      <td>Numerical constants</td>
      <td>
        <a href="#pi"><code>PI</code></a>&nbsp;&nbsp;
        <a href="#pi_bignumeric"><code>PI_BIGNUMERIC</code></a>&nbsp;&nbsp;
        <a href="#pi_numeric"><code>PI_NUMERIC</code></a>&nbsp;&nbsp;
      </td>
    </tr>
    
  </tbody>
</table>
<!-- enableFinding(LINK_ID) -->

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
  <td><a href="https://github.com/google/googlesql/blob/master/docs/mathematical_functions.md#abs"><code>ABS</code></a>
</td>
  <td>
    Computes the absolute value of <code>X</code>.
  </td>
</tr>

<tr>
  <td><a href="https://github.com/google/googlesql/blob/master/docs/mathematical_functions.md#acos"><code>ACOS</code></a>
</td>
  <td>
    Computes the inverse cosine of <code>X</code>.
  </td>
</tr>

<tr>
  <td><a href="https://github.com/google/googlesql/blob/master/docs/mathematical_functions.md#acosh"><code>ACOSH</code></a>
</td>
  <td>
    Computes the inverse hyperbolic cosine of <code>X</code>.
  </td>
</tr>

<tr>
  <td><a href="https://github.com/google/googlesql/blob/master/docs/mathematical_functions.md#asin"><code>ASIN</code></a>
</td>
  <td>
    Computes the inverse sine of <code>X</code>.
  </td>
</tr>

<tr>
  <td><a href="https://github.com/google/googlesql/blob/master/docs/mathematical_functions.md#asinh"><code>ASINH</code></a>
</td>
  <td>
    Computes the inverse hyperbolic sine of <code>X</code>.
  </td>
</tr>

<tr>
  <td><a href="https://github.com/google/googlesql/blob/master/docs/mathematical_functions.md#atan"><code>ATAN</code></a>
</td>
  <td>
    Computes the inverse tangent of <code>X</code>.
  </td>
</tr>

<tr>
  <td><a href="https://github.com/google/googlesql/blob/master/docs/mathematical_functions.md#atan2"><code>ATAN2</code></a>
</td>
  <td>
    Computes the inverse tangent of <code>X/Y</code>, using the signs of
    <code>X</code> and <code>Y</code> to determine the quadrant.
  </td>
</tr>

<tr>
  <td><a href="https://github.com/google/googlesql/blob/master/docs/mathematical_functions.md#atanh"><code>ATANH</code></a>
</td>
  <td>
    Computes the inverse hyperbolic tangent of <code>X</code>.
  </td>
</tr>

<tr>
  <td><a href="https://github.com/google/googlesql/blob/master/docs/aggregate_functions.md#avg"><code>AVG</code></a>
</td>
  <td>
    Gets the average of non-<code>NULL</code> values.
    <br>For more information, see <a href="https://github.com/google/googlesql/blob/master/docs/aggregate_functions.md">Aggregate functions</a>.

  </td>
</tr>

<tr>
  <td><a href="https://github.com/google/googlesql/blob/master/docs/aggregate-dp-functions.md#dp_avg"><code>AVG</code> (Differential Privacy)</a>
</td>
  <td>
    <code>DIFFERENTIAL_PRIVACY</code>-supported <code>AVG</code>.<br/><br/>
    Gets the differentially-private average of non-<code>NULL</code>,
    non-<code>NaN</code> values in a query with a
    <code>DIFFERENTIAL_PRIVACY</code> clause.
    <br><br>For more information, see <a href="https://github.com/google/googlesql/blob/master/docs/aggregate-dp-functions.md">Differential privacy functions</a>.

  </td>
</tr>

<tr>
  <td><a href="https://github.com/google/googlesql/blob/master/docs/mathematical_functions.md#cbrt"><code>CBRT</code></a>
</td>
  <td>
    Computes the cube root of <code>X</code>.
  </td>
</tr>

<tr>
  <td><a href="https://github.com/google/googlesql/blob/master/docs/mathematical_functions.md#ceil"><code>CEIL</code></a>
</td>
  <td>
    Gets the smallest integral value that isn't less than <code>X</code>.
  </td>
</tr>

<tr>
  <td><a href="https://github.com/google/googlesql/blob/master/docs/mathematical_functions.md#ceiling"><code>CEILING</code></a>
</td>
  <td>
    Synonym of <code>CEIL</code>.
  </td>
</tr>

<tr>
  <td><a href="https://github.com/google/googlesql/blob/master/docs/mathematical_functions.md#cos"><code>COS</code></a>
</td>
  <td>
    Computes the cosine of <code>X</code>.
  </td>
</tr>

<tr>
  <td><a href="https://github.com/google/googlesql/blob/master/docs/mathematical_functions.md#cosh"><code>COSH</code></a>
</td>
  <td>
    Computes the hyperbolic cosine of <code>X</code>.
  </td>
</tr>

<tr>
  <td><a href="https://github.com/google/googlesql/blob/master/docs/mathematical_functions.md#cosine_distance"><code>COSINE_DISTANCE</code></a>
</td>
  <td>Computes the cosine distance between two vectors.</td>
</tr>

<tr>
  <td><a href="https://github.com/google/googlesql/blob/master/docs/mathematical_functions.md#cot"><code>COT</code></a>
</td>
  <td>
    Computes the cotangent of <code>X</code>.
  </td>
</tr>

<tr>
  <td><a href="https://github.com/google/googlesql/blob/master/docs/mathematical_functions.md#coth"><code>COTH</code></a>
</td>
  <td>
    Computes the hyperbolic cotangent of <code>X</code>.
  </td>
</tr>

<tr>
  <td><a href="https://github.com/google/googlesql/blob/master/docs/mathematical_functions.md#csc"><code>CSC</code></a>
</td>
  <td>
    Computes the cosecant of <code>X</code>.
  </td>
</tr>

<tr>
  <td><a href="https://github.com/google/googlesql/blob/master/docs/mathematical_functions.md#csch"><code>CSCH</code></a>
</td>
  <td>
    Computes the hyperbolic cosecant of <code>X</code>.
  </td>
</tr>

<tr>
  <td><a href="https://github.com/google/googlesql/blob/master/docs/mathematical_functions.md#div"><code>DIV</code></a>
</td>
  <td>
    Divides integer <code>X</code> by integer <code>Y</code>.
  </td>
</tr>

<tr>
  <td><a href="https://github.com/google/googlesql/blob/master/docs/mathematical_functions.md#exp"><code>EXP</code></a>
</td>
  <td>
    Computes <code>e</code> to the power of <code>X</code>.
  </td>
</tr>

<tr>
  <td><a href="https://github.com/google/googlesql/blob/master/docs/mathematical_functions.md#euclidean_distance"><code>EUCLIDEAN_DISTANCE</code></a>
</td>
  <td>Computes the Euclidean distance between two vectors.</td>
</tr>

<tr>
  <td><a href="https://github.com/google/googlesql/blob/master/docs/mathematical_functions.md#floor"><code>FLOOR</code></a>
</td>
  <td>
    Gets the largest integral value that isn't greater than <code>X</code>.
  </td>
</tr>

<tr>
  <td><a href="https://github.com/google/googlesql/blob/master/docs/mathematical_functions.md#greatest"><code>GREATEST</code></a>
</td>
  <td>
    Gets the greatest value among <code>X1,...,XN</code>.
  </td>
</tr>

<tr>
  <td><a href="https://github.com/google/googlesql/blob/master/docs/mathematical_functions.md#ieee_divide"><code>IEEE_DIVIDE</code></a>
</td>
  <td>
    Divides <code>X</code> by <code>Y</code>, but doesn't generate errors for
    division by zero or overflow.
  </td>
</tr>

<tr>
  <td><a href="https://github.com/google/googlesql/blob/master/docs/mathematical_functions.md#is_inf"><code>IS_INF</code></a>
</td>
  <td>
    Checks if <code>X</code> is positive or negative infinity.
  </td>
</tr>

<tr>
  <td><a href="https://github.com/google/googlesql/blob/master/docs/mathematical_functions.md#is_nan"><code>IS_NAN</code></a>
</td>
  <td>
    Checks if <code>X</code> is a <code>NaN</code> value.
  </td>
</tr>

<tr>
  <td><a href="https://github.com/google/googlesql/blob/master/docs/mathematical_functions.md#least"><code>LEAST</code></a>
</td>
  <td>
    Gets the least value among <code>X1,...,XN</code>.
  </td>
</tr>

<tr>
  <td><a href="https://github.com/google/googlesql/blob/master/docs/mathematical_functions.md#ln"><code>LN</code></a>
</td>
  <td>
    Computes the natural logarithm of <code>X</code>.
  </td>
</tr>

<tr>
  <td><a href="https://github.com/google/googlesql/blob/master/docs/mathematical_functions.md#log"><code>LOG</code></a>
</td>
  <td>
   Computes the natural logarithm of <code>X</code> or the logarithm of
   <code>X</code> to base <code>Y</code>.
  </td>
</tr>

<tr>
  <td><a href="https://github.com/google/googlesql/blob/master/docs/mathematical_functions.md#log10"><code>LOG10</code></a>
</td>
  <td>
    Computes the natural logarithm of <code>X</code> to base 10.
  </td>
</tr>

<tr>
  <td><a href="https://github.com/google/googlesql/blob/master/docs/aggregate_functions.md#max"><code>MAX</code></a>
</td>
  <td>
    Gets the maximum non-<code>NULL</code> value.
    <br>For more information, see <a href="https://github.com/google/googlesql/blob/master/docs/aggregate_functions.md">Aggregate functions</a>.

  </td>
</tr>

<tr>
  <td><a href="https://github.com/google/googlesql/blob/master/docs/mathematical_functions.md#mod"><code>MOD</code></a>
</td>
  <td>
    Gets the remainder of the division of <code>X</code> by <code>Y</code>.
  </td>
</tr>

<tr>
  <td><a href="https://github.com/google/googlesql/blob/master/docs/mathematical_functions.md#pi"><code>PI</code></a>
</td>
  <td>
    Produces the mathematical constant π as a
    <code>DOUBLE</code> value.
  </td>
</tr>

<tr>
  <td><a href="https://github.com/google/googlesql/blob/master/docs/mathematical_functions.md#pi_bignumeric"><code>PI_BIGNUMERIC</code></a>
</td>
  <td>
    Produces the mathematical constant π as a <code>BIGNUMERIC</code> value.
  </td>
</tr>

<tr>
  <td><a href="https://github.com/google/googlesql/blob/master/docs/mathematical_functions.md#pi_numeric"><code>PI_NUMERIC</code></a>
</td>
  <td>
    Produces the mathematical constant π as a <code>NUMERIC</code> value.
  </td>
</tr>

<tr>
  <td><a href="https://github.com/google/googlesql/blob/master/docs/mathematical_functions.md#pow"><code>POW</code></a>
</td>
  <td>
    Produces the value of <code>X</code> raised to the power of <code>Y</code>.
  </td>
</tr>

<tr>
  <td><a href="https://github.com/google/googlesql/blob/master/docs/mathematical_functions.md#power"><code>POWER</code></a>
</td>
  <td>
    Synonym of <code>POW</code>.
  </td>
</tr>

<tr>
  <td><a href="https://github.com/google/googlesql/blob/master/docs/mathematical_functions.md#rand"><code>RAND</code></a>
</td>
  <td>
    Generates a pseudo-random value of type
    <code>DOUBLE</code> in the range of
    <code>[0, 1)</code>.
  </td>
</tr>

<tr>
  <td><a href="https://github.com/google/googlesql/blob/master/docs/mathematical_functions.md#range_bucket"><code>RANGE_BUCKET</code></a>
</td>
  <td>
    Scans through a sorted array and returns the 0-based position
    of a point's upper bound.
    
  </td>
</tr>

<tr>
  <td><a href="https://github.com/google/googlesql/blob/master/docs/mathematical_functions.md#round"><code>ROUND</code></a>
</td>
  <td>
    Rounds <code>X</code> to the nearest integer or rounds <code>X</code>
    to <code>N</code> decimal places after the decimal point.
  </td>
</tr>

<tr>
  <td><a href="https://github.com/google/googlesql/blob/master/docs/mathematical_functions.md#safe_add"><code>SAFE_ADD</code></a>
</td>
  <td>
    Equivalent to the addition operator (<code>X + Y</code>), but returns
    <code>NULL</code> if overflow occurs.
  </td>
</tr>

<tr>
  <td><a href="https://github.com/google/googlesql/blob/master/docs/mathematical_functions.md#safe_divide"><code>SAFE_DIVIDE</code></a>
</td>
  <td>
    Equivalent to the division operator (<code>X / Y</code>), but returns
    <code>NULL</code> if an error occurs.
  </td>
</tr>

<tr>
  <td><a href="https://github.com/google/googlesql/blob/master/docs/mathematical_functions.md#safe_multiply"><code>SAFE_MULTIPLY</code></a>
</td>
  <td>
    Equivalent to the multiplication operator (<code>X * Y</code>),
    but returns <code>NULL</code> if overflow occurs.
  </td>
</tr>

<tr>
  <td><a href="https://github.com/google/googlesql/blob/master/docs/mathematical_functions.md#safe_negate"><code>SAFE_NEGATE</code></a>
</td>
  <td>
    Equivalent to the unary minus operator (<code>-X</code>), but returns
    <code>NULL</code> if overflow occurs.
  </td>
</tr>

<tr>
  <td><a href="https://github.com/google/googlesql/blob/master/docs/mathematical_functions.md#safe_subtract"><code>SAFE_SUBTRACT</code></a>
</td>
  <td>
    Equivalent to the subtraction operator (<code>X - Y</code>), but
    returns <code>NULL</code> if overflow occurs.
  </td>
</tr>

<tr>
  <td><a href="https://github.com/google/googlesql/blob/master/docs/mathematical_functions.md#sec"><code>SEC</code></a>
</td>
  <td>
    Computes the secant of <code>X</code>.
  </td>
</tr>

<tr>
  <td><a href="https://github.com/google/googlesql/blob/master/docs/mathematical_functions.md#sech"><code>SECH</code></a>
</td>
  <td>
    Computes the hyperbolic secant of <code>X</code>.
  </td>
</tr>

<tr>
  <td><a href="https://github.com/google/googlesql/blob/master/docs/mathematical_functions.md#sign"><code>SIGN</code></a>
</td>
  <td>
    Produces -1 , 0, or +1 for negative, zero, and positive arguments
    respectively.
  </td>
</tr>

<tr>
  <td><a href="https://github.com/google/googlesql/blob/master/docs/mathematical_functions.md#sin"><code>SIN</code></a>
</td>
  <td>
    Computes the sine of <code>X</code>.
  </td>
</tr>

<tr>
  <td><a href="https://github.com/google/googlesql/blob/master/docs/mathematical_functions.md#sinh"><code>SINH</code></a>
</td>
  <td>
    Computes the hyperbolic sine of <code>X</code>.
  </td>
</tr>

<tr>
  <td><a href="https://github.com/google/googlesql/blob/master/docs/mathematical_functions.md#sqrt"><code>SQRT</code></a>
</td>
  <td>
    Computes the square root of <code>X</code>.
  </td>
</tr>

<tr>
  <td><a href="https://github.com/google/googlesql/blob/master/docs/aggregate_functions.md#sum"><code>SUM</code></a>
</td>
  <td>
    Gets the sum of non-<code>NULL</code> values.
    <br>For more information, see <a href="https://github.com/google/googlesql/blob/master/docs/aggregate_functions.md">Aggregate functions</a>.

  </td>
</tr>

<tr>
  <td><a href="https://github.com/google/googlesql/blob/master/docs/aggregate-dp-functions.md#dp_sum"><code>SUM</code> (Differential Privacy)</a>
</td>
  <td>
    <code>DIFFERENTIAL_PRIVACY</code>-supported <code>SUM</code>.<br/><br/>
    Gets the differentially-private sum of non-<code>NULL</code>,
    non-<code>NaN</code> values in a query with a
    <code>DIFFERENTIAL_PRIVACY</code> clause.
    <br><br>For more information, see <a href="https://github.com/google/googlesql/blob/master/docs/aggregate-dp-functions.md">Differential privacy functions</a>.

  </td>
</tr>

<tr>
  <td><a href="https://github.com/google/googlesql/blob/master/docs/mathematical_functions.md#tan"><code>TAN</code></a>
</td>
  <td>
    Computes the tangent of <code>X</code>.
  </td>
</tr>

<tr>
  <td><a href="https://github.com/google/googlesql/blob/master/docs/mathematical_functions.md#tanh"><code>TANH</code></a>
</td>
  <td>
    Computes the hyperbolic tangent of <code>X</code>.
  </td>
</tr>

<tr>
  <td><a href="https://github.com/google/googlesql/blob/master/docs/mathematical_functions.md#trunc"><code>TRUNC</code></a>
</td>
  <td>
    Rounds a number like <code>ROUND(X)</code> or <code>ROUND(X, N)</code>,
    but always rounds towards zero and never overflows.
  </td>
</tr>

  </tbody>
</table>

## `ABS`

```
ABS(X)
```

**Description**

Computes absolute value. Returns an error if the argument is an integer and the
output value can't be represented as the same type; this happens only for the
largest negative input value, which has no positive representation.

<table>
  <thead>
    <tr>
      <th>X</th>
      <th>ABS(X)</th>
    </tr>
    </thead>
    <tbody>
    <tr>
      <td>25</td>
      <td>25</td>
    </tr>
    <tr>
      <td>-25</td>
      <td>25</td>
    </tr>
    <tr>
      <td><code>+inf</code></td>
      <td><code>+inf</code></td>
    </tr>
    <tr>
      <td><code>-inf</code></td>
      <td><code>+inf</code></td>
    </tr>
  </tbody>
</table>

**Return Data Type**

<table>

<thead>
<tr>
<th>INPUT</th><th><code>INT32</code></th><th><code>INT64</code></th><th><code>UINT32</code></th><th><code>UINT64</code></th><th><code>NUMERIC</code></th><th><code>BIGNUMERIC</code></th><th><code>FLOAT</code></th><th><code>DOUBLE</code></th>
</tr>
</thead>
<tbody>
<tr><th>OUTPUT</th><td style="vertical-align:middle"><code>INT32</code></td><td style="vertical-align:middle"><code>INT64</code></td><td style="vertical-align:middle"><code>UINT32</code></td><td style="vertical-align:middle"><code>UINT64</code></td><td style="vertical-align:middle"><code>NUMERIC</code></td><td style="vertical-align:middle"><code>BIGNUMERIC</code></td><td style="vertical-align:middle"><code>FLOAT</code></td><td style="vertical-align:middle"><code>DOUBLE</code></td></tr>
</tbody>

</table>

## `ACOS`

```
ACOS(X)
```

**Description**

Computes the principal value of the inverse cosine of X. The return value is in
the range [0,&pi;]. Generates an error if X is a value outside of the
range [-1, 1].

<table>
  <thead>
    <tr>
      <th>X</th>
      <th>ACOS(X)</th>
    </tr>
  </thead>
  <tbody>
    <tr>
      <td><code>+inf</code></td>
      <td><code>NaN</code></td>
    </tr>
    <tr>
      <td><code>-inf</code></td>
      <td><code>NaN</code></td>
    </tr>
    <tr>
      <td><code>NaN</code></td>
      <td><code>NaN</code></td>
    </tr>
    <tr>
      <td>X &lt; -1</td>
      <td>Error</td>
    </tr>
    <tr>
      <td>X &gt; 1</td>
      <td>Error</td>
    </tr>
  </tbody>
</table>

## `ACOSH`

```
ACOSH(X)
```

**Description**

Computes the inverse hyperbolic cosine of X. Generates an error if X is a value
less than 1.

<table>
  <thead>
    <tr>
      <th>X</th>
      <th>ACOSH(X)</th>
    </tr>
  </thead>
  <tbody>
    <tr>
      <td><code>+inf</code></td>
      <td><code>+inf</code></td>
    </tr>
    <tr>
      <td><code>-inf</code></td>
      <td><code>NaN</code></td>
    </tr>
    <tr>
      <td><code>NaN</code></td>
      <td><code>NaN</code></td>
    </tr>
    <tr>
      <td>X &lt; 1</td>
      <td>Error</td>
    </tr>
  </tbody>
</table>

## `ASIN`

```
ASIN(X)
```

**Description**

Computes the principal value of the inverse sine of X. The return value is in
the range [-&pi;/2,&pi;/2]. Generates an error if X is outside of
the range [-1, 1].

<table>
  <thead>
    <tr>
      <th>X</th>
      <th>ASIN(X)</th>
    </tr>
  </thead>
  <tbody>
    <tr>
      <td><code>+inf</code></td>
      <td><code>NaN</code></td>
    </tr>
    <tr>
      <td><code>-inf</code></td>
      <td><code>NaN</code></td>
    </tr>
    <tr>
      <td><code>NaN</code></td>
      <td><code>NaN</code></td>
    </tr>
    <tr>
      <td>X &lt; -1</td>
      <td>Error</td>
    </tr>
    <tr>
      <td>X &gt; 1</td>
      <td>Error</td>
    </tr>
  </tbody>
</table>

## `ASINH`

```
ASINH(X)
```

**Description**

Computes the inverse hyperbolic sine of X. Doesn't fail.

<table>
  <thead>
    <tr>
      <th>X</th>
      <th>ASINH(X)</th>
    </tr>
  </thead>
  <tbody>
    <tr>
      <td><code>+inf</code></td>
      <td><code>+inf</code></td>
    </tr>
    <tr>
      <td><code>-inf</code></td>
      <td><code>-inf</code></td>
    </tr>
    <tr>
      <td><code>NaN</code></td>
      <td><code>NaN</code></td>
    </tr>
  </tbody>
</table>

## `ATAN`

```
ATAN(X)
```

**Description**

Computes the principal value of the inverse tangent of X. The return value is
in the range [-&pi;/2,&pi;/2]. Doesn't fail.

<table>
  <thead>
    <tr>
      <th>X</th>
      <th>ATAN(X)</th>
    </tr>
  </thead>
  <tbody>
    <tr>
      <td><code>+inf</code></td>
      <td>&pi;/2</td>
    </tr>
    <tr>
      <td><code>-inf</code></td>
      <td>-&pi;/2</td>
    </tr>
    <tr>
      <td><code>NaN</code></td>
      <td><code>NaN</code></td>
    </tr>
  </tbody>
</table>

## `ATAN2`

```
ATAN2(X, Y)
```

**Description**

Calculates the principal value of the inverse tangent of X/Y using the signs of
the two arguments to determine the quadrant. The return value is in the range
[-&pi;,&pi;].

<table>
  <thead>
    <tr>
      <th>X</th>
      <th>Y</th>
      <th>ATAN2(X, Y)</th>
    </tr>
  </thead>
  <tbody>
    <tr>
      <td><code>NaN</code></td>
      <td>Any value</td>
      <td><code>NaN</code></td>
    </tr>
    <tr>
      <td>Any value</td>
      <td><code>NaN</code></td>
      <td><code>NaN</code></td>
    </tr>
    <tr>
      <td>0.0</td>
      <td>0.0</td>
      <td>0.0</td>
    </tr>
    <tr>
      <td>Positive Finite value</td>
      <td><code>-inf</code></td>
      <td>&pi;</td>
    </tr>
    <tr>
      <td>Negative Finite value</td>
      <td><code>-inf</code></td>
      <td>-&pi;</td>
    </tr>
    <tr>
      <td>Finite value</td>
      <td><code>+inf</code></td>
      <td>0.0</td>
    </tr>
    <tr>
      <td><code>+inf</code></td>
      <td>Finite value</td>
      <td>&pi;/2</td>
    </tr>
    <tr>
      <td><code>-inf</code></td>
      <td>Finite value</td>
      <td>-&pi;/2</td>
    </tr>
    <tr>
      <td><code>+inf</code></td>
      <td><code>-inf</code></td>
      <td>&frac34;&pi;</td>
    </tr>
    <tr>
      <td><code>-inf</code></td>
      <td><code>-inf</code></td>
      <td>-&frac34;&pi;</td>
    </tr>
    <tr>
      <td><code>+inf</code></td>
      <td><code>+inf</code></td>
      <td>&pi;/4</td>
    </tr>
    <tr>
      <td><code>-inf</code></td>
      <td><code>+inf</code></td>
      <td>-&pi;/4</td>
    </tr>
  </tbody>
</table>

## `ATANH`

```
ATANH(X)
```

**Description**

Computes the inverse hyperbolic tangent of X. Generates an error if X is outside
of the range (-1, 1).

<table>
  <thead>
    <tr>
      <th>X</th>
      <th>ATANH(X)</th>
    </tr>
  </thead>
  <tbody>
    <tr>
      <td><code>+inf</code></td>
      <td><code>NaN</code></td>
    </tr>
    <tr>
      <td><code>-inf</code></td>
      <td><code>NaN</code></td>
    </tr>
    <tr>
      <td><code>NaN</code></td>
      <td><code>NaN</code></td>
    </tr>
    <tr>
      <td>X &lt; -1</td>
      <td>Error</td>
    </tr>
    <tr>
      <td>X &gt; 1</td>
      <td>Error</td>
    </tr>
  </tbody>
</table>

## `CBRT`

```
CBRT(X)
```

**Description**

Computes the cube root of `X`. `X` can be any data type
that [coerces to `DOUBLE`][conversion-rules].
Supports the `SAFE.` prefix.

<table>
  <thead>
    <tr>
      <th>X</th>
      <th>CBRT(X)</th>
    </tr>
    </thead>
    <tbody>
    <tr>
      <td><code>+inf</code></td>
      <td><code>inf</code></td>
    </tr>
    <tr>
      <td><code>-inf</code></td>
      <td><code>-inf</code></td>
    </tr>
    <tr>
      <td><code>NaN</code></td>
      <td><code>NaN</code></td>
    </tr>
    <tr>
      <td><code>0</code></td>
      <td><code>0</code></td>
    </tr>
    <tr>
      <td><code>NULL</code></td>
      <td><code>NULL</code></td>
    </tr>
  </tbody>
</table>

**Return Data Type**

`DOUBLE`

**Example**

```googlesql
SELECT CBRT(27) AS cube_root;

/*--------------------+
 | cube_root          |
 +--------------------+
 | 3.0000000000000004 |
 +--------------------*/
```

[conversion-rules]: https://github.com/google/googlesql/blob/master/docs/conversion_rules.md

## `CEIL`

```
CEIL(X)
```

**Description**

Returns the smallest integral value that isn't less than X.

<table>
  <thead>
    <tr>
      <th>X</th>
      <th>CEIL(X)</th>
    </tr>
  </thead>
  <tbody>
    <tr>
      <td>2.0</td>
      <td>2.0</td>
    </tr>
    <tr>
      <td>2.3</td>
      <td>3.0</td>
    </tr>
    <tr>
      <td>2.8</td>
      <td>3.0</td>
    </tr>
    <tr>
      <td>2.5</td>
      <td>3.0</td>
    </tr>
    <tr>
      <td>-2.3</td>
      <td>-2.0</td>
    </tr>
    <tr>
      <td>-2.8</td>
      <td>-2.0</td>
    </tr>
    <tr>
      <td>-2.5</td>
      <td>-2.0</td>
    </tr>
    <tr>
      <td>0</td>
      <td>0</td>
    </tr>
    <tr>
      <td><code>+inf</code></td>
      <td><code>+inf</code></td>
    </tr>
    <tr>
      <td><code>-inf</code></td>
      <td><code>-inf</code></td>
    </tr>
    <tr>
      <td><code>NaN</code></td>
      <td><code>NaN</code></td>
    </tr>
  </tbody>
</table>

**Return Data Type**

<table>

<thead>
<tr>
<th>INPUT</th><th><code>INT32</code></th><th><code>INT64</code></th><th><code>UINT32</code></th><th><code>UINT64</code></th><th><code>NUMERIC</code></th><th><code>BIGNUMERIC</code></th><th><code>FLOAT</code></th><th><code>DOUBLE</code></th>
</tr>
</thead>
<tbody>
<tr><th>OUTPUT</th><td style="vertical-align:middle"><code>DOUBLE</code></td><td style="vertical-align:middle"><code>DOUBLE</code></td><td style="vertical-align:middle"><code>DOUBLE</code></td><td style="vertical-align:middle"><code>DOUBLE</code></td><td style="vertical-align:middle"><code>NUMERIC</code></td><td style="vertical-align:middle"><code>BIGNUMERIC</code></td><td style="vertical-align:middle"><code>DOUBLE</code></td><td style="vertical-align:middle"><code>DOUBLE</code></td></tr>
</tbody>

</table>

## `CEILING`

```
CEILING(X)
```

**Description**

Synonym of CEIL(X)

## `COS`

```
COS(X)
```

**Description**

Computes the cosine of X where X is specified in radians. Never fails.

<table>
  <thead>
    <tr>
      <th>X</th>
      <th>COS(X)</th>
    </tr>
    </thead>
    <tbody>
    <tr>
      <td><code>+inf</code></td>
      <td><code>NaN</code></td>
    </tr>
    <tr>
      <td><code>-inf</code></td>
      <td><code>NaN</code></td>
    </tr>
    <tr>
      <td><code>NaN</code></td>
      <td><code>NaN</code></td>
    </tr>
  </tbody>
</table>

## `COSH`

```
COSH(X)
```

**Description**

Computes the hyperbolic cosine of X where X is specified in radians.
Generates an error if overflow occurs.

<table>
  <thead>
    <tr>
      <th>X</th>
      <th>COSH(X)</th>
    </tr>
  </thead>
  <tbody>
    <tr>
      <td><code>+inf</code></td>
      <td><code>+inf</code></td>
    </tr>
    <tr>
      <td><code>-inf</code></td>
      <td><code>+inf</code></td>
    </tr>
    <tr>
      <td><code>NaN</code></td>
      <td><code>NaN</code></td>
    </tr>
  </tbody>
</table>

## `COSINE_DISTANCE`

```googlesql
COSINE_DISTANCE(vector1, vector2)
```

**Description**

Computes the [cosine distance][wiki-cosine-distance] between two vectors.

**Definitions**

+   `vector1`: A vector that's represented by an
    `ARRAY<T>` value or a sparse vector that is
    represented by an `ARRAY<STRUCT<dimension,magnitude>>` value.
+   `vector2`: A vector that's represented by an
    `ARRAY<T>` value or a sparse vector that is
    represented by an `ARRAY<STRUCT<dimension,magnitude>>` value.

**Details**

+   `ARRAY<T>` can be used to represent a vector. Each zero-based index in this
    array represents a dimension. The value for each element in this array
    represents a magnitude.

    `T` can represent the following and must be the same for both
    vectors:

    
    
    

    + `FLOAT`
    + `DOUBLE`

    
    

    In the following example vector, there are four dimensions. The magnitude
    is `10.0` for dimension `0`, `55.0` for dimension `1`, `40.0` for
    dimension `2`, and `34.0` for dimension `3`:

    ```
    [10.0, 55.0, 40.0, 34.0]
    ```
+   `ARRAY<STRUCT<dimension,magnitude>>` can be used to represent a
    sparse vector. With a sparse vector, you only need to include
    dimension-magnitude pairs for non-zero magnitudes. If a magnitude isn't
    present in the sparse vector, the magnitude is implicitly understood to be
    zero.

    For example, if you have a vector with 10,000 dimensions, but only 10
    dimensions have non-zero magnitudes, then the vector is a sparse vector.
    As a result, it's more efficient to describe a sparse vector by only
    mentioning its non-zero magnitudes.

    In `ARRAY<STRUCT<dimension,magnitude>>`, `STRUCT<dimension,magnitude>`
    represents a dimension-magnitude pair for each non-zero magnitude in a
    sparse vector. These parts need to be included for each dimension-magnitude
    pair:

    + `dimension`: A `STRING` or `INT64` value that represents a
      dimension in a vector.

    + `magnitude`: A `DOUBLE` value that represents a
      non-zero magnitude for a specific dimension in a vector.

    You don't need to include empty dimension-magnitude pairs in a
    sparse vector. For example, the following sparse vector and
    non-sparse vector are equivalent:

    ```googlesql
    -- sparse vector ARRAY<STRUCT<INT64, DOUBLE>>
    [(1, 10.0), (2, 30.0), (5, 40.0)]
    ```

    ```googlesql
    -- vector ARRAY<DOUBLE>
    [0.0, 10.0, 30.0, 0.0, 0.0, 40.0]
    ```

    In a sparse vector, dimension-magnitude pairs don't need to be in any
    particular order. The following sparse vectors are equivalent:

    ```googlesql
    [('a', 10.0), ('b', 30.0), ('d', 40.0)]
    ```

    ```googlesql
    [('d', 40.0), ('a', 10.0), ('b', 30.0)]
    ```
+   Both non-sparse vectors
    in this function must share the same dimensions, and if they don't, an error
    is produced.
+   A vector can't be a zero vector. A vector is a zero vector if it has
    no dimensions or all dimensions have a magnitude of `0`, such as `[]` or
    `[0.0, 0.0]`. If a zero vector is encountered, an error is produced.
+   An error is produced if a magnitude in a vector is `NULL`.
+   If a vector is `NULL`, `NULL` is returned.

**Return type**

`DOUBLE`

**Examples**

In the following example, non-sparsevectors
are used to compute the cosine distance:

```googlesql
SELECT COSINE_DISTANCE([1.0, 2.0], [3.0, 4.0]) AS results;

/*----------+
 | results  |
 +----------+
 | 0.016130 |
 +----------*/
```

In the following example, sparse vectors are used to compute the
cosine distance:

```googlesql
SELECT COSINE_DISTANCE(
 [(1, 1.0), (2, 2.0)],
 [(2, 4.0), (1, 3.0)]) AS results;

 /*----------+
  | results  |
  +----------+
  | 0.016130 |
  +----------*/
```

The ordering of numeric values in a vector doesn't impact the results
produced by this function. For example these queries produce the same results
even though the numeric values in each vector is in a different order:

```googlesql
SELECT COSINE_DISTANCE([1.0, 2.0], [3.0, 4.0]) AS results;
```

```googlesql
SELECT COSINE_DISTANCE([2.0, 1.0], [4.0, 3.0]) AS results;
```

```googlesql
SELECT COSINE_DISTANCE([(1, 1.0), (2, 2.0)], [(1, 3.0), (2, 4.0)]) AS results;
```

```googlesql
 /*----------+
  | results  |
  +----------+
  | 0.016130 |
  +----------*/
```

In the following example, the function can't compute cosine distance against
the first vector, which is a zero vector:

```googlesql
-- ERROR
SELECT COSINE_DISTANCE([0.0, 0.0], [3.0, 4.0]) AS results;
```

```googlesql
-- ERROR
SELECT COSINE_DISTANCE([(1, 0.0), (2, 0.0)], [(1, 3.0), (2, 4.0)]) AS results;
```

Both non-sparse vectors must have the same
dimensions. If not, an error is produced. In the following example, the
first vector has two dimensions and the second vector has three:

```googlesql
-- ERROR
SELECT COSINE_DISTANCE([9.0, 7.0], [8.0, 4.0, 5.0]) AS results;
```

If you use sparse vectors and you repeat a dimension, an error is
produced:

```googlesql
-- ERROR
SELECT COSINE_DISTANCE(
  [(1, 9.0), (2, 7.0), (2, 8.0)], [(1, 8.0), (2, 4.0), (3, 5.0)]) AS results;
```

[wiki-cosine-distance]: https://en.wikipedia.org/wiki/Cosine_similarity#Cosine_distance

## `COT`

```
COT(X)
```

**Description**

Computes the cotangent for the angle of `X`, where `X` is specified in radians.
`X` can be any data type
that [coerces to `DOUBLE`][conversion-rules].
Supports the `SAFE.` prefix.

<table>
  <thead>
    <tr>
      <th>X</th>
      <th>COT(X)</th>
    </tr>
    </thead>
    <tbody>
    <tr>
      <td><code>+inf</code></td>
      <td><code>NaN</code></td>
    </tr>
    <tr>
      <td><code>-inf</code></td>
      <td><code>NaN</code></td>
    </tr>
    <tr>
      <td><code>NaN</code></td>
      <td><code>NaN</code></td>
    </tr>
    <tr>
      <td><code>0</code></td>
      <td><code>Error</code></td>
    </tr>
    <tr>
      <td><code>NULL</code></td>
      <td><code>NULL</code></td>
    </tr>
  </tbody>
</table>

**Return Data Type**

`DOUBLE`

**Example**

```googlesql
SELECT COT(1) AS a, SAFE.COT(0) AS b;

/*---------------------+------+
 | a                   | b    |
 +---------------------+------+
 | 0.64209261593433065 | NULL |
 +---------------------+------*/
```

[conversion-rules]: https://github.com/google/googlesql/blob/master/docs/conversion_rules.md

## `COTH`

```
COTH(X)
```

**Description**

Computes the hyperbolic cotangent for the angle of `X`, where `X` is specified
in radians. `X` can be any data type
that [coerces to `DOUBLE`][conversion-rules].
Supports the `SAFE.` prefix.

<table>
  <thead>
    <tr>
      <th>X</th>
      <th>COTH(X)</th>
    </tr>
    </thead>
    <tbody>
    <tr>
      <td><code>+inf</code></td>
      <td><code>1</code></td>
    </tr>
    <tr>
      <td><code>-inf</code></td>
      <td><code>-1</code></td>
    </tr>
    <tr>
      <td><code>NaN</code></td>
      <td><code>NaN</code></td>
    </tr>
    <tr>
      <td><code>0</code></td>
      <td><code>Error</code></td>
    </tr>
    <tr>
      <td><code>NULL</code></td>
      <td><code>NULL</code></td>
    </tr>
  </tbody>
</table>

**Return Data Type**

`DOUBLE`

**Example**

```googlesql
SELECT COTH(1) AS a, SAFE.COTH(0) AS b;

/*----------------+------+
 | a              | b    |
 +----------------+------+
 | 1.313035285499 | NULL |
 +----------------+------*/
```

[conversion-rules]: https://github.com/google/googlesql/blob/master/docs/conversion_rules.md

## `CSC`

```
CSC(X)
```

**Description**

Computes the cosecant of the input angle, which is in radians.
`X` can be any data type
that [coerces to `DOUBLE`][conversion-rules].
Supports the `SAFE.` prefix.

<table>
  <thead>
    <tr>
      <th>X</th>
      <th>CSC(X)</th>
    </tr>
    </thead>
    <tbody>
    <tr>
      <td><code>+inf</code></td>
      <td><code>NaN</code></td>
    </tr>
    <tr>
      <td><code>-inf</code></td>
      <td><code>NaN</code></td>
    </tr>
    <tr>
      <td><code>NaN</code></td>
      <td><code>NaN</code></td>
    </tr>
    <tr>
      <td><code>0</code></td>
      <td><code>Error</code></td>
    </tr>
    <tr>
      <td><code>NULL</code></td>
      <td><code>NULL</code></td>
    </tr>
  </tbody>
</table>

**Return Data Type**

`DOUBLE`

**Example**

```googlesql
SELECT CSC(100) AS a, CSC(-1) AS b, SAFE.CSC(0) AS c;

/*----------------+-----------------+------+
 | a              | b               | c    |
 +----------------+-----------------+------+
 | -1.97485753142 | -1.188395105778 | NULL |
 +----------------+-----------------+------*/
```

[conversion-rules]: https://github.com/google/googlesql/blob/master/docs/conversion_rules.md

## `CSCH`

```
CSCH(X)
```

**Description**

Computes the hyperbolic cosecant of the input angle, which is in radians.
`X` can be any data type
that [coerces to `DOUBLE`][conversion-rules].
Supports the `SAFE.` prefix.

<table>
  <thead>
    <tr>
      <th>X</th>
      <th>CSCH(X)</th>
    </tr>
    </thead>
    <tbody>
    <tr>
      <td><code>+inf</code></td>
      <td><code>0</code></td>
    </tr>
    <tr>
      <td><code>-inf</code></td>
      <td><code>0</code></td>
    </tr>
    <tr>
      <td><code>NaN</code></td>
      <td><code>NaN</code></td>
    </tr>
    <tr>
      <td><code>0</code></td>
      <td><code>Error</code></td>
    </tr>
    <tr>
      <td><code>NULL</code></td>
      <td><code>NULL</code></td>
    </tr>
  </tbody>
</table>

**Return Data Type**

`DOUBLE`

**Example**

```googlesql
SELECT CSCH(0.5) AS a, CSCH(-2) AS b, SAFE.CSCH(0) AS c;

/*----------------+----------------+------+
 | a              | b              | c    |
 +----------------+----------------+------+
 | 1.919034751334 | -0.27572056477 | NULL |
 +----------------+----------------+------*/
```

[conversion-rules]: https://github.com/google/googlesql/blob/master/docs/conversion_rules.md

## `DIV`

```
DIV(X, Y)
```

**Description**

Returns the result of integer division of X by Y. Division by zero returns
an error. Division by -1 may overflow.

<table>
  <thead>
    <tr>
      <th>X</th>
      <th>Y</th>
      <th>DIV(X, Y)</th>
    </tr>
  </thead>
  <tbody>
    <tr>
      <td>20</td>
      <td>4</td>
      <td>5</td>
    </tr>
    <tr>
      <td>12</td>
      <td>-7</td>
      <td>-1</td>
    </tr>
    <tr>
      <td>20</td>
      <td>3</td>
      <td>6</td>
    </tr>
    <tr>
      <td>0</td>
      <td>20</td>
      <td>0</td>
    </tr>
    <tr>
      <td>20</td>
      <td>0</td>
      <td>Error</td>
    </tr>
  </tbody>
</table>

**Return Data Type**

The return data type is determined by the argument types with the following
table.
<table>

<thead>
<tr>
<th>INPUT</th><th><code>INT32</code></th><th><code>INT64</code></th><th><code>UINT32</code></th><th><code>UINT64</code></th><th><code>NUMERIC</code></th><th><code>BIGNUMERIC</code></th>
</tr>
</thead>
<tbody>
<tr><th><code>INT32</code></th><td style="vertical-align:middle"><code>INT64</code></td><td style="vertical-align:middle"><code>INT64</code></td><td style="vertical-align:middle"><code>INT64</code></td><td style="vertical-align:middle">ERROR</td><td style="vertical-align:middle"><code>NUMERIC</code></td><td style="vertical-align:middle"><code>BIGNUMERIC</code></td></tr>
<tr><th><code>INT64</code></th><td style="vertical-align:middle"><code>INT64</code></td><td style="vertical-align:middle"><code>INT64</code></td><td style="vertical-align:middle"><code>INT64</code></td><td style="vertical-align:middle">ERROR</td><td style="vertical-align:middle"><code>NUMERIC</code></td><td style="vertical-align:middle"><code>BIGNUMERIC</code></td></tr>
<tr><th><code>UINT32</code></th><td style="vertical-align:middle"><code>INT64</code></td><td style="vertical-align:middle"><code>INT64</code></td><td style="vertical-align:middle"><code>UINT64</code></td><td style="vertical-align:middle"><code>UINT64</code></td><td style="vertical-align:middle"><code>NUMERIC</code></td><td style="vertical-align:middle"><code>BIGNUMERIC</code></td></tr>
<tr><th><code>UINT64</code></th><td style="vertical-align:middle">ERROR</td><td style="vertical-align:middle">ERROR</td><td style="vertical-align:middle"><code>UINT64</code></td><td style="vertical-align:middle"><code>UINT64</code></td><td style="vertical-align:middle"><code>NUMERIC</code></td><td style="vertical-align:middle"><code>BIGNUMERIC</code></td></tr>
<tr><th><code>NUMERIC</code></th><td style="vertical-align:middle"><code>NUMERIC</code></td><td style="vertical-align:middle"><code>NUMERIC</code></td><td style="vertical-align:middle"><code>NUMERIC</code></td><td style="vertical-align:middle"><code>NUMERIC</code></td><td style="vertical-align:middle"><code>NUMERIC</code></td><td style="vertical-align:middle"><code>BIGNUMERIC</code></td></tr>
<tr><th><code>BIGNUMERIC</code></th><td style="vertical-align:middle"><code>BIGNUMERIC</code></td><td style="vertical-align:middle"><code>BIGNUMERIC</code></td><td style="vertical-align:middle"><code>BIGNUMERIC</code></td><td style="vertical-align:middle"><code>BIGNUMERIC</code></td><td style="vertical-align:middle"><code>BIGNUMERIC</code></td><td style="vertical-align:middle"><code>BIGNUMERIC</code></td></tr>
</tbody>

</table>

## `EXP`

```
EXP(X)
```

**Description**

Computes *e* to the power of X, also called the natural exponential function. If
the result underflows, this function returns a zero. Generates an error if the
result overflows.

<table>
  <thead>
    <tr>
      <th>X</th>
      <th>EXP(X)</th>
    </tr>
    </thead>
    <tbody>
    <tr>
      <td>0.0</td>
      <td>1.0</td>
    </tr>
    <tr>
      <td><code>+inf</code></td>
      <td><code>+inf</code></td>
    </tr>
    <tr>
      <td><code>-inf</code></td>
      <td>0.0</td>
    </tr>
  </tbody>
</table>

**Return Data Type**

<table>

<thead>
<tr>
<th>INPUT</th><th><code>INT32</code></th><th><code>INT64</code></th><th><code>UINT32</code></th><th><code>UINT64</code></th><th><code>NUMERIC</code></th><th><code>BIGNUMERIC</code></th><th><code>FLOAT</code></th><th><code>DOUBLE</code></th>
</tr>
</thead>
<tbody>
<tr><th>OUTPUT</th><td style="vertical-align:middle"><code>DOUBLE</code></td><td style="vertical-align:middle"><code>DOUBLE</code></td><td style="vertical-align:middle"><code>DOUBLE</code></td><td style="vertical-align:middle"><code>DOUBLE</code></td><td style="vertical-align:middle"><code>NUMERIC</code></td><td style="vertical-align:middle"><code>BIGNUMERIC</code></td><td style="vertical-align:middle"><code>DOUBLE</code></td><td style="vertical-align:middle"><code>DOUBLE</code></td></tr>
</tbody>

</table>

## `EUCLIDEAN_DISTANCE`

```googlesql
EUCLIDEAN_DISTANCE(vector1, vector2)
```

**Description**

Computes the [Euclidean distance][wiki-euclidean-distance] between two vectors.

**Definitions**

+   `vector1`: A vector that's represented by an
    `ARRAY<T>` value or a sparse vector that is
    represented by an `ARRAY<STRUCT<dimension,magnitude>>` value.
+   `vector2`: A vector that's represented by an
    `ARRAY<T>` value or a sparse vector that is
    represented by an `ARRAY<STRUCT<dimension,magnitude>>` value.

**Details**

+   `ARRAY<T>` can be used to represent a vector. Each zero-based index in this
    array represents a dimension. The value for each element in this array
    represents a magnitude.

    `T` can represent the following and must be the same for both
    vectors:

    
    
    

    + `FLOAT`
    + `DOUBLE`

    
    

    In the following example vector, there are four dimensions. The magnitude
    is `10.0` for dimension `0`, `55.0` for dimension `1`, `40.0` for
    dimension `2`, and `34.0` for dimension `3`:

    ```
    [10.0, 55.0, 40.0, 34.0]
    ```
+   `ARRAY<STRUCT<dimension,magnitude>>` can be used to represent a
    sparse vector. With a sparse vector, you only need to include
    dimension-magnitude pairs for non-zero magnitudes. If a magnitude isn't
    present in the sparse vector, the magnitude is implicitly understood to be
    zero.

    For example, if you have a vector with 10,000 dimensions, but only 10
    dimensions have non-zero magnitudes, then the vector is a sparse vector.
    As a result, it's more efficient to describe a sparse vector by only
    mentioning its non-zero magnitudes.

    In `ARRAY<STRUCT<dimension,magnitude>>`, `STRUCT<dimension,magnitude>`
    represents a dimension-magnitude pair for each non-zero magnitude in a
    sparse vector. These parts need to be included for each dimension-magnitude
    pair:

    + `dimension`: A `STRING` or `INT64` value that represents a
      dimension in a vector.

    + `magnitude`: A `DOUBLE` value that represents a
      non-zero magnitude for a specific dimension in a vector.

    You don't need to include empty dimension-magnitude pairs in a
    sparse vector. For example, the following sparse vector and
    non-sparse vector are equivalent:

    ```googlesql
    -- sparse vector ARRAY<STRUCT<INT64, DOUBLE>>
    [(1, 10.0), (2, 30.0), (5, 40.0)]
    ```

    ```googlesql
    -- vector ARRAY<DOUBLE>
    [0.0, 10.0, 30.0, 0.0, 0.0, 40.0]
    ```

    In a sparse vector, dimension-magnitude pairs don't need to be in any
    particular order. The following sparse vectors are equivalent:

    ```googlesql
    [('a', 10.0), ('b', 30.0), ('d', 40.0)]
    ```

    ```googlesql
    [('d', 40.0), ('a', 10.0), ('b', 30.0)]
    ```
+   Both non-sparse vectors
    in this function must share the same dimensions, and if they don't, an error
    is produced.
+   A vector can be a zero vector. A vector is a zero vector if it has
    no dimensions or all dimensions have a magnitude of `0`, such as `[]` or
    `[0.0, 0.0]`.
+   An error is produced if a magnitude in a vector is `NULL`.
+   If a vector is `NULL`, `NULL` is returned.

**Return type**

`DOUBLE`

**Examples**

In the following example, non-sparse vectors
are used to compute the Euclidean distance:

```googlesql
SELECT EUCLIDEAN_DISTANCE([1.0, 2.0], [3.0, 4.0]) AS results;

/*----------+
 | results  |
 +----------+
 | 2.828    |
 +----------*/
```

In the following example, sparse vectors are used to compute the
Euclidean distance:

```googlesql
SELECT EUCLIDEAN_DISTANCE(
 [(1, 1.0), (2, 2.0)],
 [(2, 4.0), (1, 3.0)]) AS results;

 /*----------+
  | results  |
  +----------+
  | 2.828    |
  +----------*/
```

The ordering of magnitudes in a vector doesn't impact the results
produced by this function. For example these queries produce the same results
even though the magnitudes in each vector is in a different order:

```googlesql
SELECT EUCLIDEAN_DISTANCE([1.0, 2.0], [3.0, 4.0]);
```

```googlesql
SELECT EUCLIDEAN_DISTANCE([2.0, 1.0], [4.0, 3.0]);
```

```googlesql
SELECT EUCLIDEAN_DISTANCE([(1, 1.0), (2, 2.0)], [(1, 3.0), (2, 4.0)]) AS results;
```

```googlesql
 /*----------+
  | results  |
  +----------+
  | 2.828    |
  +----------*/
```

Both non-sparse vectors must have the same
dimensions. If not, an error is produced. In the following example, the first
vector has two dimensions and the second vector has three:

```googlesql
-- ERROR
SELECT EUCLIDEAN_DISTANCE([9.0, 7.0], [8.0, 4.0, 5.0]) AS results;
```

If you use sparse vectors and you repeat a dimension, an error is
produced:

```googlesql
-- ERROR
SELECT EUCLIDEAN_DISTANCE(
  [(1, 9.0), (2, 7.0), (2, 8.0)], [(1, 8.0), (2, 4.0), (3, 5.0)]) AS results;
```

[wiki-euclidean-distance]: https://en.wikipedia.org/wiki/Euclidean_distance

## `FLOOR`

```
FLOOR(X)
```

**Description**

Returns the largest integral value that isn't greater than X.

<table>
  <thead>
    <tr>
      <th>X</th>
      <th>FLOOR(X)</th>
    </tr>
  </thead>
  <tbody>
    <tr>
      <td>2.0</td>
      <td>2.0</td>
    </tr>
    <tr>
      <td>2.3</td>
      <td>2.0</td>
    </tr>
    <tr>
      <td>2.8</td>
      <td>2.0</td>
    </tr>
    <tr>
      <td>2.5</td>
      <td>2.0</td>
    </tr>
    <tr>
      <td>-2.3</td>
      <td>-3.0</td>
    </tr>
    <tr>
      <td>-2.8</td>
      <td>-3.0</td>
    </tr>
    <tr>
      <td>-2.5</td>
      <td>-3.0</td>
    </tr>
    <tr>
      <td>0</td>
      <td>0</td>
    </tr>
    <tr>
      <td><code>+inf</code></td>
      <td><code>+inf</code></td>
    </tr>
    <tr>
      <td><code>-inf</code></td>
      <td><code>-inf</code></td>
    </tr>
    <tr>
      <td><code>NaN</code></td>
      <td><code>NaN</code></td>
    </tr>
  </tbody>
</table>

**Return Data Type**

<table>

<thead>
<tr>
<th>INPUT</th><th><code>INT32</code></th><th><code>INT64</code></th><th><code>UINT32</code></th><th><code>UINT64</code></th><th><code>NUMERIC</code></th><th><code>BIGNUMERIC</code></th><th><code>FLOAT</code></th><th><code>DOUBLE</code></th>
</tr>
</thead>
<tbody>
<tr><th>OUTPUT</th><td style="vertical-align:middle"><code>DOUBLE</code></td><td style="vertical-align:middle"><code>DOUBLE</code></td><td style="vertical-align:middle"><code>DOUBLE</code></td><td style="vertical-align:middle"><code>DOUBLE</code></td><td style="vertical-align:middle"><code>NUMERIC</code></td><td style="vertical-align:middle"><code>BIGNUMERIC</code></td><td style="vertical-align:middle"><code>DOUBLE</code></td><td style="vertical-align:middle"><code>DOUBLE</code></td></tr>
</tbody>

</table>

## `GREATEST`

```
GREATEST(X1,...,XN)
```

**Description**

Returns the greatest value among `X1,...,XN`. If any argument is `NULL`, returns
`NULL`. Otherwise, in the case of floating-point arguments, if any argument is
`NaN`, returns `NaN`. In all other cases, returns the value among `X1,...,XN`
that has the greatest value according to the ordering used by the `ORDER BY`
clause. The arguments `X1, ..., XN` must be coercible to a common supertype, and
the supertype must support ordering.

<table>
  <thead>
    <tr>
      <th>X1,...,XN</th>
      <th>GREATEST(X1,...,XN)</th>
    </tr>
    </thead>
    <tbody>
    <tr>
      <td>3,5,1</td>
      <td>5</td>
    </tr>
  </tbody>
</table>

This function supports specifying [collation][collation].

[collation]: https://github.com/google/googlesql/blob/master/docs/collation-concepts.md

**Return Data Types**

Data type of the input values.

## `IEEE_DIVIDE`

```
IEEE_DIVIDE(X, Y)
```

**Description**

Divides X by Y; this function never fails. Returns
`DOUBLE` unless
both X and Y are `FLOAT`, in which case it returns
`FLOAT`. Unlike the division operator (/),
this function doesn't generate errors for division by zero or overflow.

<table>
  <thead>
    <tr>
      <th>X</th>
      <th>Y</th>
      <th>IEEE_DIVIDE(X, Y)</th>
    </tr>
  </thead>
  <tbody>
    <tr>
      <td>20.0</td>
      <td>4.0</td>
      <td>5.0</td>
    </tr>
    <tr>
      <td>20.0</td>
      <td>6.0</td>
      <td>3.3333333333333335</td>
    </tr>
    <tr>
      <td>0.0</td>
      <td>25.0</td>
      <td>0.0</td>
    </tr>
    <tr>
      <td>25.0</td>
      <td>0.0</td>
      <td><code>+inf</code></td>
    </tr>
    <tr>
      <td>-25.0</td>
      <td>0.0</td>
      <td><code>-inf</code></td>
    </tr>
    <tr>
      <td>25.0</td>
      <td>-0.0</td>
      <td><code>-inf</code></td>
    </tr>
    <tr>
      <td>0.0</td>
      <td>0.0</td>
      <td><code>NaN</code></td>
    </tr>
    <tr>
      <td>0.0</td>
      <td><code>NaN</code></td>
      <td><code>NaN</code></td>
    </tr>
    <tr>
      <td><code>NaN</code></td>
      <td>0.0</td>
      <td><code>NaN</code></td>
    </tr>
    <tr>
      <td><code>+inf</code></td>
      <td><code>+inf</code></td>
      <td><code>NaN</code></td>
    </tr>
    <tr>
      <td><code>-inf</code></td>
      <td><code>-inf</code></td>
      <td><code>NaN</code></td>
    </tr>
  </tbody>
</table>

## `IS_INF`

```
IS_INF(X)
```

**Description**

Returns `TRUE` if the value is positive or negative infinity.

<table>
  <thead>
    <tr>
      <th>X</th>
      <th>IS_INF(X)</th>
    </tr>
    </thead>
    <tbody>
    <tr>
      <td><code>+inf</code></td>
      <td><code>TRUE</code></td>
    </tr>
    <tr>
      <td><code>-inf</code></td>
      <td><code>TRUE</code></td>
    </tr>
    <tr>
      <td>25</td>
      <td><code>FALSE</code></td>
    </tr>
  </tbody>
</table>

## `IS_NAN`

```
IS_NAN(X)
```

**Description**

Returns `TRUE` if the value is a `NaN` value.

<table>
  <thead>
    <tr>
      <th>X</th>
      <th>IS_NAN(X)</th>
    </tr>
    </thead>
    <tbody>
    <tr>
      <td><code>NaN</code></td>
      <td><code>TRUE</code></td>
    </tr>
    <tr>
      <td>25</td>
      <td><code>FALSE</code></td>
    </tr>
  </tbody>
</table>

## `LEAST`

```
LEAST(X1,...,XN)
```

**Description**

Returns the least value among `X1,...,XN`. If any argument is `NULL`, returns
`NULL`. Otherwise, in the case of floating-point arguments, if any argument is
`NaN`, returns `NaN`. In all other cases, returns the value among `X1,...,XN`
that has the least value according to the ordering used by the `ORDER BY`
clause. The arguments `X1, ..., XN` must be coercible to a common supertype, and
the supertype must support ordering.

<table>
  <thead>
    <tr>
      <th>X1,...,XN</th>
      <th>LEAST(X1,...,XN)</th>
    </tr>
    </thead>
    <tbody>
    <tr>
      <td>3,5,1</td>
      <td>1</td>
    </tr>
  </tbody>
</table>

This function supports specifying [collation][collation].

[collation]: https://github.com/google/googlesql/blob/master/docs/collation-concepts.md

**Return Data Types**

Data type of the input values.

## `LN`

```
LN(X)
```

**Description**

Computes the natural logarithm of X. Generates an error if X is less than or
equal to zero.

<table>
  <thead>
    <tr>
      <th>X</th>
      <th>LN(X)</th>
    </tr>
    </thead>
    <tbody>
    <tr>
      <td>1.0</td>
      <td>0.0</td>
    </tr>
    <tr>
      <td><code>+inf</code></td>
      <td><code>+inf</code></td>
    </tr>
    <tr>
      <td><code>X &lt;= 0</code></td>
      <td>Error</td>
    </tr>
  </tbody>
</table>

**Return Data Type**

<table>

<thead>
<tr>
<th>INPUT</th><th><code>INT32</code></th><th><code>INT64</code></th><th><code>UINT32</code></th><th><code>UINT64</code></th><th><code>NUMERIC</code></th><th><code>BIGNUMERIC</code></th><th><code>FLOAT</code></th><th><code>DOUBLE</code></th>
</tr>
</thead>
<tbody>
<tr><th>OUTPUT</th><td style="vertical-align:middle"><code>DOUBLE</code></td><td style="vertical-align:middle"><code>DOUBLE</code></td><td style="vertical-align:middle"><code>DOUBLE</code></td><td style="vertical-align:middle"><code>DOUBLE</code></td><td style="vertical-align:middle"><code>NUMERIC</code></td><td style="vertical-align:middle"><code>BIGNUMERIC</code></td><td style="vertical-align:middle"><code>DOUBLE</code></td><td style="vertical-align:middle"><code>DOUBLE</code></td></tr>
</tbody>

</table>

## `LOG`

```
LOG(X [, Y])
```

**Description**

If only X is present, `LOG` is a synonym of `LN`. If Y is also present,
`LOG` computes the logarithm of X to base Y.

<table>
  <thead>
    <tr>
      <th>X</th>
      <th>Y</th>
      <th>LOG(X, Y)</th>
    </tr>
  </thead>
  <tbody>
    <tr>
      <td>100.0</td>
      <td>10.0</td>
      <td>2.0</td>
    </tr>
    <tr>
      <td><code>-inf</code></td>
      <td>Any value</td>
      <td><code>NaN</code></td>
    </tr>
    <tr>
      <td>Any value</td>
      <td><code>+inf</code></td>
      <td><code>NaN</code></td>
    </tr>
    <tr>
      <td><code>+inf</code></td>
      <td>0.0 &lt; Y &lt; 1.0</td>
      <td><code>-inf</code></td>
    </tr>
    <tr>
      <td><code>+inf</code></td>
      <td>Y &gt; 1.0</td>
      <td><code>+inf</code></td>
    </tr>
    <tr>
      <td>X &lt;= 0</td>
      <td>Any value</td>
      <td>Error</td>
    </tr>
    <tr>
      <td>Any value</td>
      <td>Y &lt;= 0</td>
      <td>Error</td>
    </tr>
    <tr>
      <td>Any value</td>
      <td>1.0</td>
      <td>Error</td>
    </tr>
  </tbody>
</table>

**Return Data Type**

<table style="font-size:small">

<thead>
<tr>
<th>INPUT</th><th><code>INT32</code></th><th><code>INT64</code></th><th><code>UINT32</code></th><th><code>UINT64</code></th><th><code>NUMERIC</code></th><th><code>BIGNUMERIC</code></th><th><code>FLOAT</code></th><th><code>DOUBLE</code></th>
</tr>
</thead>
<tbody>
<tr><th><code>INT32</code></th><td style="vertical-align:middle"><code>DOUBLE</code></td><td style="vertical-align:middle"><code>DOUBLE</code></td><td style="vertical-align:middle"><code>DOUBLE</code></td><td style="vertical-align:middle"><code>DOUBLE</code></td><td style="vertical-align:middle"><code>NUMERIC</code></td><td style="vertical-align:middle"><code>BIGNUMERIC</code></td><td style="vertical-align:middle"><code>DOUBLE</code></td><td style="vertical-align:middle"><code>DOUBLE</code></td></tr>
<tr><th><code>INT64</code></th><td style="vertical-align:middle"><code>DOUBLE</code></td><td style="vertical-align:middle"><code>DOUBLE</code></td><td style="vertical-align:middle"><code>DOUBLE</code></td><td style="vertical-align:middle"><code>DOUBLE</code></td><td style="vertical-align:middle"><code>NUMERIC</code></td><td style="vertical-align:middle"><code>BIGNUMERIC</code></td><td style="vertical-align:middle"><code>DOUBLE</code></td><td style="vertical-align:middle"><code>DOUBLE</code></td></tr>
<tr><th><code>UINT32</code></th><td style="vertical-align:middle"><code>DOUBLE</code></td><td style="vertical-align:middle"><code>DOUBLE</code></td><td style="vertical-align:middle"><code>DOUBLE</code></td><td style="vertical-align:middle"><code>DOUBLE</code></td><td style="vertical-align:middle"><code>NUMERIC</code></td><td style="vertical-align:middle"><code>BIGNUMERIC</code></td><td style="vertical-align:middle"><code>DOUBLE</code></td><td style="vertical-align:middle"><code>DOUBLE</code></td></tr>
<tr><th><code>UINT64</code></th><td style="vertical-align:middle"><code>DOUBLE</code></td><td style="vertical-align:middle"><code>DOUBLE</code></td><td style="vertical-align:middle"><code>DOUBLE</code></td><td style="vertical-align:middle"><code>DOUBLE</code></td><td style="vertical-align:middle"><code>NUMERIC</code></td><td style="vertical-align:middle"><code>BIGNUMERIC</code></td><td style="vertical-align:middle"><code>DOUBLE</code></td><td style="vertical-align:middle"><code>DOUBLE</code></td></tr>
<tr><th><code>NUMERIC</code></th><td style="vertical-align:middle"><code>NUMERIC</code></td><td style="vertical-align:middle"><code>NUMERIC</code></td><td style="vertical-align:middle"><code>NUMERIC</code></td><td style="vertical-align:middle"><code>NUMERIC</code></td><td style="vertical-align:middle"><code>NUMERIC</code></td><td style="vertical-align:middle"><code>BIGNUMERIC</code></td><td style="vertical-align:middle"><code>DOUBLE</code></td><td style="vertical-align:middle"><code>DOUBLE</code></td></tr>
<tr><th><code>BIGNUMERIC</code></th><td style="vertical-align:middle"><code>BIGNUMERIC</code></td><td style="vertical-align:middle"><code>BIGNUMERIC</code></td><td style="vertical-align:middle"><code>BIGNUMERIC</code></td><td style="vertical-align:middle"><code>BIGNUMERIC</code></td><td style="vertical-align:middle"><code>BIGNUMERIC</code></td><td style="vertical-align:middle"><code>BIGNUMERIC</code></td><td style="vertical-align:middle"><code>DOUBLE</code></td><td style="vertical-align:middle"><code>DOUBLE</code></td></tr>
<tr><th><code>FLOAT</code></th><td style="vertical-align:middle"><code>DOUBLE</code></td><td style="vertical-align:middle"><code>DOUBLE</code></td><td style="vertical-align:middle"><code>DOUBLE</code></td><td style="vertical-align:middle"><code>DOUBLE</code></td><td style="vertical-align:middle"><code>DOUBLE</code></td><td style="vertical-align:middle"><code>DOUBLE</code></td><td style="vertical-align:middle"><code>DOUBLE</code></td><td style="vertical-align:middle"><code>DOUBLE</code></td></tr>
<tr><th><code>DOUBLE</code></th><td style="vertical-align:middle"><code>DOUBLE</code></td><td style="vertical-align:middle"><code>DOUBLE</code></td><td style="vertical-align:middle"><code>DOUBLE</code></td><td style="vertical-align:middle"><code>DOUBLE</code></td><td style="vertical-align:middle"><code>DOUBLE</code></td><td style="vertical-align:middle"><code>DOUBLE</code></td><td style="vertical-align:middle"><code>DOUBLE</code></td><td style="vertical-align:middle"><code>DOUBLE</code></td></tr>
</tbody>

</table>

## `LOG10`

```
LOG10(X)
```

**Description**

Similar to `LOG`, but computes logarithm to base 10.

<table>
  <thead>
    <tr>
      <th>X</th>
      <th>LOG10(X)</th>
    </tr>
  </thead>
  <tbody>
    <tr>
      <td>100.0</td>
      <td>2.0</td>
    </tr>
    <tr>
      <td><code>-inf</code></td>
      <td><code>NaN</code></td>
    </tr>
    <tr>
      <td><code>+inf</code></td>
      <td><code>+inf</code></td>
    </tr>
    <tr>
      <td>X &lt;= 0</td>
      <td>Error</td>
    </tr>
  </tbody>
</table>

**Return Data Type**

<table>

<thead>
<tr>
<th>INPUT</th><th><code>INT32</code></th><th><code>INT64</code></th><th><code>UINT32</code></th><th><code>UINT64</code></th><th><code>NUMERIC</code></th><th><code>BIGNUMERIC</code></th><th><code>FLOAT</code></th><th><code>DOUBLE</code></th>
</tr>
</thead>
<tbody>
<tr><th>OUTPUT</th><td style="vertical-align:middle"><code>DOUBLE</code></td><td style="vertical-align:middle"><code>DOUBLE</code></td><td style="vertical-align:middle"><code>DOUBLE</code></td><td style="vertical-align:middle"><code>DOUBLE</code></td><td style="vertical-align:middle"><code>NUMERIC</code></td><td style="vertical-align:middle"><code>BIGNUMERIC</code></td><td style="vertical-align:middle"><code>DOUBLE</code></td><td style="vertical-align:middle"><code>DOUBLE</code></td></tr>
</tbody>

</table>

## `MOD`

```
MOD(X, Y)
```

**Description**

Modulo function: returns the remainder of the division of X by Y. Returned
value has the same sign as X. An error is generated if Y is 0.

<table>
  <thead>
    <tr>
      <th>X</th>
      <th>Y</th>
      <th>MOD(X, Y)</th>
    </tr>
  </thead>
  <tbody>
    <tr>
      <td>25</td>
      <td>12</td>
      <td>1</td>
    </tr>
    <tr>
      <td>25</td>
      <td>0</td>
      <td>Error</td>
    </tr>
  </tbody>
</table>

**Return Data Type**

The return data type is determined by the argument types with the following
table.
<table>

<thead>
<tr>
<th>INPUT</th><th><code>INT32</code></th><th><code>INT64</code></th><th><code>UINT32</code></th><th><code>UINT64</code></th><th><code>NUMERIC</code></th><th><code>BIGNUMERIC</code></th>
</tr>
</thead>
<tbody>
<tr><th><code>INT32</code></th><td style="vertical-align:middle"><code>INT64</code></td><td style="vertical-align:middle"><code>INT64</code></td><td style="vertical-align:middle"><code>INT64</code></td><td style="vertical-align:middle">ERROR</td><td style="vertical-align:middle"><code>NUMERIC</code></td><td style="vertical-align:middle"><code>BIGNUMERIC</code></td></tr>
<tr><th><code>INT64</code></th><td style="vertical-align:middle"><code>INT64</code></td><td style="vertical-align:middle"><code>INT64</code></td><td style="vertical-align:middle"><code>INT64</code></td><td style="vertical-align:middle">ERROR</td><td style="vertical-align:middle"><code>NUMERIC</code></td><td style="vertical-align:middle"><code>BIGNUMERIC</code></td></tr>
<tr><th><code>UINT32</code></th><td style="vertical-align:middle"><code>INT64</code></td><td style="vertical-align:middle"><code>INT64</code></td><td style="vertical-align:middle"><code>UINT64</code></td><td style="vertical-align:middle"><code>UINT64</code></td><td style="vertical-align:middle"><code>NUMERIC</code></td><td style="vertical-align:middle"><code>BIGNUMERIC</code></td></tr>
<tr><th><code>UINT64</code></th><td style="vertical-align:middle">ERROR</td><td style="vertical-align:middle">ERROR</td><td style="vertical-align:middle"><code>UINT64</code></td><td style="vertical-align:middle"><code>UINT64</code></td><td style="vertical-align:middle"><code>NUMERIC</code></td><td style="vertical-align:middle"><code>BIGNUMERIC</code></td></tr>
<tr><th><code>NUMERIC</code></th><td style="vertical-align:middle"><code>NUMERIC</code></td><td style="vertical-align:middle"><code>NUMERIC</code></td><td style="vertical-align:middle"><code>NUMERIC</code></td><td style="vertical-align:middle"><code>NUMERIC</code></td><td style="vertical-align:middle"><code>NUMERIC</code></td><td style="vertical-align:middle"><code>BIGNUMERIC</code></td></tr>
<tr><th><code>BIGNUMERIC</code></th><td style="vertical-align:middle"><code>BIGNUMERIC</code></td><td style="vertical-align:middle"><code>BIGNUMERIC</code></td><td style="vertical-align:middle"><code>BIGNUMERIC</code></td><td style="vertical-align:middle"><code>BIGNUMERIC</code></td><td style="vertical-align:middle"><code>BIGNUMERIC</code></td><td style="vertical-align:middle"><code>BIGNUMERIC</code></td></tr>
</tbody>

</table>

## `PI`

```googlesql
PI()
```

**Description**

Returns the mathematical constant `π` as a `DOUBLE`
value.

**Return type**

`DOUBLE`

**Example**

```googlesql
SELECT PI() AS pi

/*--------------------+
 | pi                 |
 +--------------------+
 | 3.1415926535897931 |
 +--------------------*/
```

## `PI_BIGNUMERIC`

```googlesql
PI_BIGNUMERIC()
```

**Description**

Returns the mathematical constant `π` as a `BIGNUMERIC` value.

**Return type**

`BIGNUMERIC`

**Example**

```googlesql
SELECT PI_BIGNUMERIC() AS pi

/*-----------------------------------------+
 | pi                                      |
 +-----------------------------------------+
 | 3.1415926535897932384626433832795028842 |
 +-----------------------------------------*/
```

## `PI_NUMERIC`

```googlesql
PI_NUMERIC()
```

**Description**

Returns the mathematical constant `π` as a `NUMERIC` value.

**Return type**

`NUMERIC`

**Example**

```googlesql
SELECT PI_NUMERIC() AS pi

/*-------------+
 | pi          |
 +-------------+
 | 3.141592654 |
 +-------------*/
```

## `POW`

```
POW(X, Y)
```

**Description**

Returns the value of X raised to the power of Y. If the result underflows and
isn't representable, then the function returns a value of zero.

<table>
  <thead>
    <tr>
      <th>X</th>
      <th>Y</th>
      <th>POW(X, Y)</th>
    </tr>
  </thead>
  <tbody>
    <tr>
      <td>2.0</td>
      <td>3.0</td>
      <td>8.0</td>
    </tr>
    <tr>
      <td>1.0</td>
      <td>Any value including <code>NaN</code></td>
      <td>1.0</td>
    </tr>
    <tr>
      <td>Any value including <code>NaN</code></td>
      <td>0</td>
      <td>1.0</td>
    </tr>
    <tr>
      <td>-1.0</td>
      <td><code>+inf</code></td>
      <td>1.0</td>
    </tr>
    <tr>
      <td>-1.0</td>
      <td><code>-inf</code></td>
      <td>1.0</td>
    </tr>
    <tr>
      <td>ABS(X) &lt; 1</td>
      <td><code>-inf</code></td>
      <td><code>+inf</code></td>
    </tr>
    <tr>
      <td>ABS(X) &gt; 1</td>
      <td><code>-inf</code></td>
      <td>0.0</td>
    </tr>
    <tr>
      <td>ABS(X) &lt; 1</td>
      <td><code>+inf</code></td>
      <td>0.0</td>
    </tr>
    <tr>
      <td>ABS(X) &gt; 1</td>
      <td><code>+inf</code></td>
      <td><code>+inf</code></td>
    </tr>
    <tr>
      <td><code>-inf</code></td>
      <td>Y &lt; 0</td>
      <td>0.0</td>
    </tr>
    <tr>
      <td><code>-inf</code></td>
      <td>Y &gt; 0</td>
      <td><code>-inf</code> if Y is an odd integer, <code>+inf</code> otherwise</td>
    </tr>
    <tr>
      <td><code>+inf</code></td>
      <td>Y &lt; 0</td>
      <td>0</td>
    </tr>
    <tr>
      <td><code>+inf</code></td>
      <td>Y &gt; 0</td>
      <td><code>+inf</code></td>
    </tr>
    <tr>
      <td>Finite value &lt; 0</td>
      <td>Non-integer</td>
      <td>Error</td>
    </tr>
    <tr>
      <td>0</td>
      <td>Finite value &lt; 0</td>
      <td>Error</td>
    </tr>
  </tbody>
</table>

**Return Data Type**

The return data type is determined by the argument types with the following
table.

<table style="font-size:small">

<thead>
<tr>
<th>INPUT</th><th><code>INT32</code></th><th><code>INT64</code></th><th><code>UINT32</code></th><th><code>UINT64</code></th><th><code>NUMERIC</code></th><th><code>BIGNUMERIC</code></th><th><code>FLOAT</code></th><th><code>DOUBLE</code></th>
</tr>
</thead>
<tbody>
<tr><th><code>INT32</code></th><td style="vertical-align:middle"><code>DOUBLE</code></td><td style="vertical-align:middle"><code>DOUBLE</code></td><td style="vertical-align:middle"><code>DOUBLE</code></td><td style="vertical-align:middle"><code>DOUBLE</code></td><td style="vertical-align:middle"><code>NUMERIC</code></td><td style="vertical-align:middle"><code>BIGNUMERIC</code></td><td style="vertical-align:middle"><code>DOUBLE</code></td><td style="vertical-align:middle"><code>DOUBLE</code></td></tr>
<tr><th><code>INT64</code></th><td style="vertical-align:middle"><code>DOUBLE</code></td><td style="vertical-align:middle"><code>DOUBLE</code></td><td style="vertical-align:middle"><code>DOUBLE</code></td><td style="vertical-align:middle"><code>DOUBLE</code></td><td style="vertical-align:middle"><code>NUMERIC</code></td><td style="vertical-align:middle"><code>BIGNUMERIC</code></td><td style="vertical-align:middle"><code>DOUBLE</code></td><td style="vertical-align:middle"><code>DOUBLE</code></td></tr>
<tr><th><code>UINT32</code></th><td style="vertical-align:middle"><code>DOUBLE</code></td><td style="vertical-align:middle"><code>DOUBLE</code></td><td style="vertical-align:middle"><code>DOUBLE</code></td><td style="vertical-align:middle"><code>DOUBLE</code></td><td style="vertical-align:middle"><code>NUMERIC</code></td><td style="vertical-align:middle"><code>BIGNUMERIC</code></td><td style="vertical-align:middle"><code>DOUBLE</code></td><td style="vertical-align:middle"><code>DOUBLE</code></td></tr>
<tr><th><code>UINT64</code></th><td style="vertical-align:middle"><code>DOUBLE</code></td><td style="vertical-align:middle"><code>DOUBLE</code></td><td style="vertical-align:middle"><code>DOUBLE</code></td><td style="vertical-align:middle"><code>DOUBLE</code></td><td style="vertical-align:middle"><code>NUMERIC</code></td><td style="vertical-align:middle"><code>BIGNUMERIC</code></td><td style="vertical-align:middle"><code>DOUBLE</code></td><td style="vertical-align:middle"><code>DOUBLE</code></td></tr>
<tr><th><code>NUMERIC</code></th><td style="vertical-align:middle"><code>NUMERIC</code></td><td style="vertical-align:middle"><code>NUMERIC</code></td><td style="vertical-align:middle"><code>NUMERIC</code></td><td style="vertical-align:middle"><code>NUMERIC</code></td><td style="vertical-align:middle"><code>NUMERIC</code></td><td style="vertical-align:middle"><code>BIGNUMERIC</code></td><td style="vertical-align:middle"><code>DOUBLE</code></td><td style="vertical-align:middle"><code>DOUBLE</code></td></tr>
<tr><th><code>BIGNUMERIC</code></th><td style="vertical-align:middle"><code>BIGNUMERIC</code></td><td style="vertical-align:middle"><code>BIGNUMERIC</code></td><td style="vertical-align:middle"><code>BIGNUMERIC</code></td><td style="vertical-align:middle"><code>BIGNUMERIC</code></td><td style="vertical-align:middle"><code>BIGNUMERIC</code></td><td style="vertical-align:middle"><code>BIGNUMERIC</code></td><td style="vertical-align:middle"><code>DOUBLE</code></td><td style="vertical-align:middle"><code>DOUBLE</code></td></tr>
<tr><th><code>FLOAT</code></th><td style="vertical-align:middle"><code>DOUBLE</code></td><td style="vertical-align:middle"><code>DOUBLE</code></td><td style="vertical-align:middle"><code>DOUBLE</code></td><td style="vertical-align:middle"><code>DOUBLE</code></td><td style="vertical-align:middle"><code>DOUBLE</code></td><td style="vertical-align:middle"><code>DOUBLE</code></td><td style="vertical-align:middle"><code>DOUBLE</code></td><td style="vertical-align:middle"><code>DOUBLE</code></td></tr>
<tr><th><code>DOUBLE</code></th><td style="vertical-align:middle"><code>DOUBLE</code></td><td style="vertical-align:middle"><code>DOUBLE</code></td><td style="vertical-align:middle"><code>DOUBLE</code></td><td style="vertical-align:middle"><code>DOUBLE</code></td><td style="vertical-align:middle"><code>DOUBLE</code></td><td style="vertical-align:middle"><code>DOUBLE</code></td><td style="vertical-align:middle"><code>DOUBLE</code></td><td style="vertical-align:middle"><code>DOUBLE</code></td></tr>
</tbody>

</table>

## `POWER`

```
POWER(X, Y)
```

**Description**

Synonym of [`POW(X, Y)`][pow].

[pow]: #pow

## `RAND`

```
RAND()
```

**Description**

Generates a pseudo-random value of type `DOUBLE` in
the range of [0, 1), inclusive of 0 and exclusive of 1.

## `RANGE_BUCKET`

```googlesql
RANGE_BUCKET(point, boundaries_array)
```

**Description**

`RANGE_BUCKET` scans through a sorted array and returns the 0-based position
of the point's upper bound. This can be useful if you need to group your data to
build partitions, histograms, business-defined rules, and more.

`RANGE_BUCKET` follows these rules:

+  If the point exists in the array, returns the index of the next larger value.

   ```googlesql
   RANGE_BUCKET(20, [0, 10, 20, 30, 40]) -- 3 is return value
   RANGE_BUCKET(20, [0, 10, 20, 20, 40, 40]) -- 4 is return value
   ```
+  If the point doesn't exist in the array, but it falls between two values,
   returns the index of the larger value.

   ```googlesql
   RANGE_BUCKET(25, [0, 10, 20, 30, 40]) -- 3 is return value
   ```
+  If the point is smaller than the first value in the array, returns 0.

   ```googlesql
   RANGE_BUCKET(-10, [5, 10, 20, 30, 40]) -- 0 is return value
   ```
+  If the point is greater than or equal to the last value in the array,
   returns the length of the array.

   ```googlesql
   RANGE_BUCKET(80, [0, 10, 20, 30, 40]) -- 5 is return value
   ```
+  If the array is empty, returns 0.

   ```googlesql
   RANGE_BUCKET(80, []) -- 0 is return value
   ```
+  If the point is `NULL` or `NaN`, returns `NULL`.

   ```googlesql
   RANGE_BUCKET(NULL, [0, 10, 20, 30, 40]) -- NULL is return value
   ```
+  The data type for the point and array must be compatible.

   ```googlesql
   RANGE_BUCKET('a', ['a', 'b', 'c', 'd']) -- 1 is return value
   RANGE_BUCKET(1.2, [1, 1.2, 1.4, 1.6]) -- 2 is return value
   RANGE_BUCKET(1.2, [1, 2, 4, 6]) -- execution failure
   ```

Execution failure occurs when:

+  The array has a `NaN` or `NULL` value in it.

   ```googlesql
   RANGE_BUCKET(80, [NULL, 10, 20, 30, 40]) -- execution failure
   ```
+  The array isn't sorted in ascending order.

   ```googlesql
   RANGE_BUCKET(30, [10, 30, 20, 40, 50]) -- execution failure
   ```

**Parameters**

+  `point`: A generic value.
+  `boundaries_array`: A generic array of values.

Note: The data type for `point` and the element type of `boundaries_array`
must be equivalent. The data type must be [comparable][data-type-properties].

**Return Value**

`INT64`

**Examples**

In a table called `students`, check to see how many records would
exist in each `age_group` bucket, based on a student's age:

+  age_group 0 (age < 10)
+  age_group 1 (age >= 10, age < 20)
+  age_group 2 (age >= 20, age < 30)
+  age_group 3 (age >= 30)

```googlesql
WITH students AS
(
  SELECT 9 AS age UNION ALL
  SELECT 20 AS age UNION ALL
  SELECT 25 AS age UNION ALL
  SELECT 31 AS age UNION ALL
  SELECT 32 AS age UNION ALL
  SELECT 33 AS age
)
SELECT RANGE_BUCKET(age, [10, 20, 30]) AS age_group, COUNT(*) AS count
FROM students
GROUP BY 1

/*--------------+-------+
 | age_group    | count |
 +--------------+-------+
 | 0            | 1     |
 | 2            | 2     |
 | 3            | 3     |
 +--------------+-------*/
```

[data-type-properties]: https://github.com/google/googlesql/blob/master/docs/data-types.md#data_type_properties

## `ROUND`

```
ROUND(X [, N [, rounding_mode]])
```

**Description**

If only X is present, rounds X to the nearest integer. If N is present,
rounds X to N decimal places after the decimal point. If N is negative,
rounds off digits to the left of the decimal point. Rounds halfway cases
away from zero. Generates an error if overflow occurs.

If X is a `NUMERIC` or `BIGNUMERIC` type, then you can
explicitly set `rounding_mode`
to one of the following:

+   [`"ROUND_HALF_AWAY_FROM_ZERO"`][round-half-away-from-zero]: (Default) Rounds
    halfway cases away from zero.
+   [`"ROUND_HALF_EVEN"`][round-half-even]: Rounds halfway cases
    towards the nearest even digit.

If you set the `rounding_mode` and X isn't a `NUMERIC` or `BIGNUMERIC` type,
then the function generates an error.

<table>
  <thead>
    <tr>
      <th>Expression</th>
      <th>Return Value</th>
    </tr>
  </thead>
  <tbody>
    <tr>
      <td><code>ROUND(2.0)</code></td>
      <td>2.0</td>
    </tr>
    <tr>
      <td><code>ROUND(2.3)</code></td>
      <td>2.0</td>
    </tr>
    <tr>
      <td><code>ROUND(2.8)</code></td>
      <td>3.0</td>
    </tr>
    <tr>
      <td><code>ROUND(2.5)</code></td>
      <td>3.0</td>
    </tr>
    <tr>
      <td><code>ROUND(-2.3)</code></td>
      <td>-2.0</td>
    </tr>
    <tr>
      <td><code>ROUND(-2.8)</code></td>
      <td>-3.0</td>
    </tr>
    <tr>
      <td><code>ROUND(-2.5)</code></td>
      <td>-3.0</td>
    </tr>
    <tr>
      <td><code>ROUND(0)</code></td>
      <td>0</td>
    </tr>
    <tr>
      <td><code>ROUND(+inf)</code></td>
      <td><code>+inf</code></td>
    </tr>
    <tr>
      <td><code>ROUND(-inf)</code></td>
      <td><code>-inf</code></td>
    </tr>
    <tr>
      <td><code>ROUND(NaN)</code></td>
      <td><code>NaN</code></td>
    </tr>
    <tr>
      <td><code>ROUND(123.7, -1)</code></td>
      <td>120.0</td>
    </tr>
    <tr>
      <td><code>ROUND(1.235, 2)</code></td>
      <td>1.24</td>
    </tr>
    <tr>
      <td><code>ROUND(NUMERIC "2.25", 1, "ROUND_HALF_EVEN")</code></td>
      <td>2.2</td>
    </tr>
    <tr>
      <td><code>ROUND(NUMERIC "2.35", 1, "ROUND_HALF_EVEN")</code></td>
      <td>2.4</td>
    </tr>
    <tr>
      <td><code>ROUND(NUMERIC "2.251", 1, "ROUND_HALF_EVEN")</code></td>
      <td>2.3</td>
    </tr>
    <tr>
      <td><code>ROUND(NUMERIC "-2.5", 0, "ROUND_HALF_EVEN")</code></td>
      <td>-2</td>
    </tr>
    <tr>
      <td><code>ROUND(NUMERIC "2.5", 0, "ROUND_HALF_AWAY_FROM_ZERO")</code></td>
      <td>3</td>
    </tr>
    <tr>
      <td><code>ROUND(NUMERIC "-2.5", 0, "ROUND_HALF_AWAY_FROM_ZERO")</code></td>
      <td>-3</td>
    </tr>
  </tbody>
</table>

**Return Data Type**

<table>

<thead>
<tr>
<th>INPUT</th><th><code>INT32</code></th><th><code>INT64</code></th><th><code>UINT32</code></th><th><code>UINT64</code></th><th><code>NUMERIC</code></th><th><code>BIGNUMERIC</code></th><th><code>FLOAT</code></th><th><code>DOUBLE</code></th>
</tr>
</thead>
<tbody>
<tr><th>OUTPUT</th><td style="vertical-align:middle"><code>DOUBLE</code></td><td style="vertical-align:middle"><code>DOUBLE</code></td><td style="vertical-align:middle"><code>DOUBLE</code></td><td style="vertical-align:middle"><code>DOUBLE</code></td><td style="vertical-align:middle"><code>NUMERIC</code></td><td style="vertical-align:middle"><code>BIGNUMERIC</code></td><td style="vertical-align:middle"><code>DOUBLE</code></td><td style="vertical-align:middle"><code>DOUBLE</code></td></tr>
</tbody>

</table>

[round-half-away-from-zero]: https://en.wikipedia.org/wiki/Rounding#Rounding_half_away_from_zero

[round-half-even]: https://en.wikipedia.org/wiki/Rounding#Rounding_half_to_even

## `SAFE_ADD`

```
SAFE_ADD(X, Y)
```

**Description**

Equivalent to the addition operator (`+`), but returns
`NULL` if overflow occurs.

<table>
  <thead>
    <tr>
      <th>X</th>
      <th>Y</th>
      <th>SAFE_ADD(X, Y)</th>
    </tr>
  </thead>
  <tbody>
    <tr>
      <td>5</td>
      <td>4</td>
      <td>9</td>
    </tr>
  </tbody>
</table>

**Return Data Type**

<table style="font-size:small">

<thead>
<tr>
<th>INPUT</th><th><code>INT32</code></th><th><code>INT64</code></th><th><code>UINT32</code></th><th><code>UINT64</code></th><th><code>NUMERIC</code></th><th><code>BIGNUMERIC</code></th><th><code>FLOAT</code></th><th><code>DOUBLE</code></th>
</tr>
</thead>
<tbody>
<tr><th><code>INT32</code></th><td style="vertical-align:middle"><code>INT64</code></td><td style="vertical-align:middle"><code>INT64</code></td><td style="vertical-align:middle"><code>INT64</code></td><td style="vertical-align:middle">ERROR</td><td style="vertical-align:middle"><code>NUMERIC</code></td><td style="vertical-align:middle"><code>BIGNUMERIC</code></td><td style="vertical-align:middle"><code>DOUBLE</code></td><td style="vertical-align:middle"><code>DOUBLE</code></td></tr>
<tr><th><code>INT64</code></th><td style="vertical-align:middle"><code>INT64</code></td><td style="vertical-align:middle"><code>INT64</code></td><td style="vertical-align:middle"><code>INT64</code></td><td style="vertical-align:middle">ERROR</td><td style="vertical-align:middle"><code>NUMERIC</code></td><td style="vertical-align:middle"><code>BIGNUMERIC</code></td><td style="vertical-align:middle"><code>DOUBLE</code></td><td style="vertical-align:middle"><code>DOUBLE</code></td></tr>
<tr><th><code>UINT32</code></th><td style="vertical-align:middle"><code>INT64</code></td><td style="vertical-align:middle"><code>INT64</code></td><td style="vertical-align:middle"><code>UINT64</code></td><td style="vertical-align:middle"><code>UINT64</code></td><td style="vertical-align:middle"><code>NUMERIC</code></td><td style="vertical-align:middle"><code>BIGNUMERIC</code></td><td style="vertical-align:middle"><code>DOUBLE</code></td><td style="vertical-align:middle"><code>DOUBLE</code></td></tr>
<tr><th><code>UINT64</code></th><td style="vertical-align:middle">ERROR</td><td style="vertical-align:middle">ERROR</td><td style="vertical-align:middle"><code>UINT64</code></td><td style="vertical-align:middle"><code>UINT64</code></td><td style="vertical-align:middle"><code>NUMERIC</code></td><td style="vertical-align:middle"><code>BIGNUMERIC</code></td><td style="vertical-align:middle"><code>DOUBLE</code></td><td style="vertical-align:middle"><code>DOUBLE</code></td></tr>
<tr><th><code>NUMERIC</code></th><td style="vertical-align:middle"><code>NUMERIC</code></td><td style="vertical-align:middle"><code>NUMERIC</code></td><td style="vertical-align:middle"><code>NUMERIC</code></td><td style="vertical-align:middle"><code>NUMERIC</code></td><td style="vertical-align:middle"><code>NUMERIC</code></td><td style="vertical-align:middle"><code>BIGNUMERIC</code></td><td style="vertical-align:middle"><code>DOUBLE</code></td><td style="vertical-align:middle"><code>DOUBLE</code></td></tr>
<tr><th><code>BIGNUMERIC</code></th><td style="vertical-align:middle"><code>BIGNUMERIC</code></td><td style="vertical-align:middle"><code>BIGNUMERIC</code></td><td style="vertical-align:middle"><code>BIGNUMERIC</code></td><td style="vertical-align:middle"><code>BIGNUMERIC</code></td><td style="vertical-align:middle"><code>BIGNUMERIC</code></td><td style="vertical-align:middle"><code>BIGNUMERIC</code></td><td style="vertical-align:middle"><code>DOUBLE</code></td><td style="vertical-align:middle"><code>DOUBLE</code></td></tr>
<tr><th><code>FLOAT</code></th><td style="vertical-align:middle"><code>DOUBLE</code></td><td style="vertical-align:middle"><code>DOUBLE</code></td><td style="vertical-align:middle"><code>DOUBLE</code></td><td style="vertical-align:middle"><code>DOUBLE</code></td><td style="vertical-align:middle"><code>DOUBLE</code></td><td style="vertical-align:middle"><code>DOUBLE</code></td><td style="vertical-align:middle"><code>DOUBLE</code></td><td style="vertical-align:middle"><code>DOUBLE</code></td></tr>
<tr><th><code>DOUBLE</code></th><td style="vertical-align:middle"><code>DOUBLE</code></td><td style="vertical-align:middle"><code>DOUBLE</code></td><td style="vertical-align:middle"><code>DOUBLE</code></td><td style="vertical-align:middle"><code>DOUBLE</code></td><td style="vertical-align:middle"><code>DOUBLE</code></td><td style="vertical-align:middle"><code>DOUBLE</code></td><td style="vertical-align:middle"><code>DOUBLE</code></td><td style="vertical-align:middle"><code>DOUBLE</code></td></tr>
</tbody>

</table>

## `SAFE_DIVIDE`

```
SAFE_DIVIDE(X, Y)
```

**Description**

Equivalent to the division operator (`X / Y`), but returns
`NULL` if an error occurs, such as a division by zero error.

<table>
  <thead>
    <tr>
      <th>X</th>
      <th>Y</th>
      <th>SAFE_DIVIDE(X, Y)</th>
    </tr>
  </thead>
  <tbody>
    <tr>
      <td>20</td>
      <td>4</td>
      <td>5</td>
    </tr>
    <tr>
      <td>0</td>
      <td>20</td>
      <td><code>0</code></td>
    </tr>
    <tr>
      <td>20</td>
      <td>0</td>
      <td><code>NULL</code></td>
    </tr>
  </tbody>
</table>

**Return Data Type**

<table style="font-size:small">

<thead>
<tr>
<th>INPUT</th><th><code>INT32</code></th><th><code>INT64</code></th><th><code>UINT32</code></th><th><code>UINT64</code></th><th><code>NUMERIC</code></th><th><code>BIGNUMERIC</code></th><th><code>FLOAT</code></th><th><code>DOUBLE</code></th>
</tr>
</thead>
<tbody>
<tr><th><code>INT32</code></th><td style="vertical-align:middle"><code>DOUBLE</code></td><td style="vertical-align:middle"><code>DOUBLE</code></td><td style="vertical-align:middle"><code>DOUBLE</code></td><td style="vertical-align:middle"><code>DOUBLE</code></td><td style="vertical-align:middle"><code>NUMERIC</code></td><td style="vertical-align:middle"><code>BIGNUMERIC</code></td><td style="vertical-align:middle"><code>DOUBLE</code></td><td style="vertical-align:middle"><code>DOUBLE</code></td></tr>
<tr><th><code>INT64</code></th><td style="vertical-align:middle"><code>DOUBLE</code></td><td style="vertical-align:middle"><code>DOUBLE</code></td><td style="vertical-align:middle"><code>DOUBLE</code></td><td style="vertical-align:middle"><code>DOUBLE</code></td><td style="vertical-align:middle"><code>NUMERIC</code></td><td style="vertical-align:middle"><code>BIGNUMERIC</code></td><td style="vertical-align:middle"><code>DOUBLE</code></td><td style="vertical-align:middle"><code>DOUBLE</code></td></tr>
<tr><th><code>UINT32</code></th><td style="vertical-align:middle"><code>DOUBLE</code></td><td style="vertical-align:middle"><code>DOUBLE</code></td><td style="vertical-align:middle"><code>DOUBLE</code></td><td style="vertical-align:middle"><code>DOUBLE</code></td><td style="vertical-align:middle"><code>NUMERIC</code></td><td style="vertical-align:middle"><code>BIGNUMERIC</code></td><td style="vertical-align:middle"><code>DOUBLE</code></td><td style="vertical-align:middle"><code>DOUBLE</code></td></tr>
<tr><th><code>UINT64</code></th><td style="vertical-align:middle"><code>DOUBLE</code></td><td style="vertical-align:middle"><code>DOUBLE</code></td><td style="vertical-align:middle"><code>DOUBLE</code></td><td style="vertical-align:middle"><code>DOUBLE</code></td><td style="vertical-align:middle"><code>NUMERIC</code></td><td style="vertical-align:middle"><code>BIGNUMERIC</code></td><td style="vertical-align:middle"><code>DOUBLE</code></td><td style="vertical-align:middle"><code>DOUBLE</code></td></tr>
<tr><th><code>NUMERIC</code></th><td style="vertical-align:middle"><code>NUMERIC</code></td><td style="vertical-align:middle"><code>NUMERIC</code></td><td style="vertical-align:middle"><code>NUMERIC</code></td><td style="vertical-align:middle"><code>NUMERIC</code></td><td style="vertical-align:middle"><code>NUMERIC</code></td><td style="vertical-align:middle"><code>BIGNUMERIC</code></td><td style="vertical-align:middle"><code>DOUBLE</code></td><td style="vertical-align:middle"><code>DOUBLE</code></td></tr>
<tr><th><code>BIGNUMERIC</code></th><td style="vertical-align:middle"><code>BIGNUMERIC</code></td><td style="vertical-align:middle"><code>BIGNUMERIC</code></td><td style="vertical-align:middle"><code>BIGNUMERIC</code></td><td style="vertical-align:middle"><code>BIGNUMERIC</code></td><td style="vertical-align:middle"><code>BIGNUMERIC</code></td><td style="vertical-align:middle"><code>BIGNUMERIC</code></td><td style="vertical-align:middle"><code>DOUBLE</code></td><td style="vertical-align:middle"><code>DOUBLE</code></td></tr>
<tr><th><code>FLOAT</code></th><td style="vertical-align:middle"><code>DOUBLE</code></td><td style="vertical-align:middle"><code>DOUBLE</code></td><td style="vertical-align:middle"><code>DOUBLE</code></td><td style="vertical-align:middle"><code>DOUBLE</code></td><td style="vertical-align:middle"><code>DOUBLE</code></td><td style="vertical-align:middle"><code>DOUBLE</code></td><td style="vertical-align:middle"><code>DOUBLE</code></td><td style="vertical-align:middle"><code>DOUBLE</code></td></tr>
<tr><th><code>DOUBLE</code></th><td style="vertical-align:middle"><code>DOUBLE</code></td><td style="vertical-align:middle"><code>DOUBLE</code></td><td style="vertical-align:middle"><code>DOUBLE</code></td><td style="vertical-align:middle"><code>DOUBLE</code></td><td style="vertical-align:middle"><code>DOUBLE</code></td><td style="vertical-align:middle"><code>DOUBLE</code></td><td style="vertical-align:middle"><code>DOUBLE</code></td><td style="vertical-align:middle"><code>DOUBLE</code></td></tr>
</tbody>

</table>

## `SAFE_MULTIPLY`

```
SAFE_MULTIPLY(X, Y)
```

**Description**

Equivalent to the multiplication operator (`*`), but returns
`NULL` if overflow occurs.

<table>
  <thead>
    <tr>
      <th>X</th>
      <th>Y</th>
      <th>SAFE_MULTIPLY(X, Y)</th>
    </tr>
  </thead>
  <tbody>
    <tr>
      <td>20</td>
      <td>4</td>
      <td>80</td>
    </tr>
  </tbody>
</table>

**Return Data Type**

<table style="font-size:small">

<thead>
<tr>
<th>INPUT</th><th><code>INT32</code></th><th><code>INT64</code></th><th><code>UINT32</code></th><th><code>UINT64</code></th><th><code>NUMERIC</code></th><th><code>BIGNUMERIC</code></th><th><code>FLOAT</code></th><th><code>DOUBLE</code></th>
</tr>
</thead>
<tbody>
<tr><th><code>INT32</code></th><td style="vertical-align:middle"><code>INT64</code></td><td style="vertical-align:middle"><code>INT64</code></td><td style="vertical-align:middle"><code>INT64</code></td><td style="vertical-align:middle">ERROR</td><td style="vertical-align:middle"><code>NUMERIC</code></td><td style="vertical-align:middle"><code>BIGNUMERIC</code></td><td style="vertical-align:middle"><code>DOUBLE</code></td><td style="vertical-align:middle"><code>DOUBLE</code></td></tr>
<tr><th><code>INT64</code></th><td style="vertical-align:middle"><code>INT64</code></td><td style="vertical-align:middle"><code>INT64</code></td><td style="vertical-align:middle"><code>INT64</code></td><td style="vertical-align:middle">ERROR</td><td style="vertical-align:middle"><code>NUMERIC</code></td><td style="vertical-align:middle"><code>BIGNUMERIC</code></td><td style="vertical-align:middle"><code>DOUBLE</code></td><td style="vertical-align:middle"><code>DOUBLE</code></td></tr>
<tr><th><code>UINT32</code></th><td style="vertical-align:middle"><code>INT64</code></td><td style="vertical-align:middle"><code>INT64</code></td><td style="vertical-align:middle"><code>UINT64</code></td><td style="vertical-align:middle"><code>UINT64</code></td><td style="vertical-align:middle"><code>NUMERIC</code></td><td style="vertical-align:middle"><code>BIGNUMERIC</code></td><td style="vertical-align:middle"><code>DOUBLE</code></td><td style="vertical-align:middle"><code>DOUBLE</code></td></tr>
<tr><th><code>UINT64</code></th><td style="vertical-align:middle">ERROR</td><td style="vertical-align:middle">ERROR</td><td style="vertical-align:middle"><code>UINT64</code></td><td style="vertical-align:middle"><code>UINT64</code></td><td style="vertical-align:middle"><code>NUMERIC</code></td><td style="vertical-align:middle"><code>BIGNUMERIC</code></td><td style="vertical-align:middle"><code>DOUBLE</code></td><td style="vertical-align:middle"><code>DOUBLE</code></td></tr>
<tr><th><code>NUMERIC</code></th><td style="vertical-align:middle"><code>NUMERIC</code></td><td style="vertical-align:middle"><code>NUMERIC</code></td><td style="vertical-align:middle"><code>NUMERIC</code></td><td style="vertical-align:middle"><code>NUMERIC</code></td><td style="vertical-align:middle"><code>NUMERIC</code></td><td style="vertical-align:middle"><code>BIGNUMERIC</code></td><td style="vertical-align:middle"><code>DOUBLE</code></td><td style="vertical-align:middle"><code>DOUBLE</code></td></tr>
<tr><th><code>BIGNUMERIC</code></th><td style="vertical-align:middle"><code>BIGNUMERIC</code></td><td style="vertical-align:middle"><code>BIGNUMERIC</code></td><td style="vertical-align:middle"><code>BIGNUMERIC</code></td><td style="vertical-align:middle"><code>BIGNUMERIC</code></td><td style="vertical-align:middle"><code>BIGNUMERIC</code></td><td style="vertical-align:middle"><code>BIGNUMERIC</code></td><td style="vertical-align:middle"><code>DOUBLE</code></td><td style="vertical-align:middle"><code>DOUBLE</code></td></tr>
<tr><th><code>FLOAT</code></th><td style="vertical-align:middle"><code>DOUBLE</code></td><td style="vertical-align:middle"><code>DOUBLE</code></td><td style="vertical-align:middle"><code>DOUBLE</code></td><td style="vertical-align:middle"><code>DOUBLE</code></td><td style="vertical-align:middle"><code>DOUBLE</code></td><td style="vertical-align:middle"><code>DOUBLE</code></td><td style="vertical-align:middle"><code>DOUBLE</code></td><td style="vertical-align:middle"><code>DOUBLE</code></td></tr>
<tr><th><code>DOUBLE</code></th><td style="vertical-align:middle"><code>DOUBLE</code></td><td style="vertical-align:middle"><code>DOUBLE</code></td><td style="vertical-align:middle"><code>DOUBLE</code></td><td style="vertical-align:middle"><code>DOUBLE</code></td><td style="vertical-align:middle"><code>DOUBLE</code></td><td style="vertical-align:middle"><code>DOUBLE</code></td><td style="vertical-align:middle"><code>DOUBLE</code></td><td style="vertical-align:middle"><code>DOUBLE</code></td></tr>
</tbody>

</table>

## `SAFE_NEGATE`

```
SAFE_NEGATE(X)
```

**Description**

Equivalent to the unary minus operator (`-`), but returns
`NULL` if overflow occurs.

<table>
  <thead>
    <tr>
      <th>X</th>
      <th>SAFE_NEGATE(X)</th>
    </tr>
  </thead>
  <tbody>
    <tr>
      <td>+1</td>
      <td>-1</td>
    </tr>
    <tr>
      <td>-1</td>
      <td>+1</td>
    </tr>
    <tr>
      <td>0</td>
      <td>0</td>
    </tr>
  </tbody>
</table>

**Return Data Type**

<table>

<thead>
<tr>
<th>INPUT</th><th><code>INT32</code></th><th><code>INT64</code></th><th><code>UINT32</code></th><th><code>UINT64</code></th><th><code>NUMERIC</code></th><th><code>BIGNUMERIC</code></th><th><code>FLOAT</code></th><th><code>DOUBLE</code></th>
</tr>
</thead>
<tbody>
<tr><th>OUTPUT</th><td style="vertical-align:middle"><code>INT32</code></td><td style="vertical-align:middle"><code>INT64</code></td><td style="vertical-align:middle">ERROR</td><td style="vertical-align:middle">ERROR</td><td style="vertical-align:middle"><code>NUMERIC</code></td><td style="vertical-align:middle"><code>BIGNUMERIC</code></td><td style="vertical-align:middle"><code>FLOAT</code></td><td style="vertical-align:middle"><code>DOUBLE</code></td></tr>
</tbody>

</table>

## `SAFE_SUBTRACT`

```
SAFE_SUBTRACT(X, Y)
```

**Description**

Returns the result of Y subtracted from X.
Equivalent to the subtraction operator (`-`), but returns
`NULL` if overflow occurs.

<table>
  <thead>
    <tr>
      <th>X</th>
      <th>Y</th>
      <th>SAFE_SUBTRACT(X, Y)</th>
    </tr>
  </thead>
  <tbody>
    <tr>
      <td>5</td>
      <td>4</td>
      <td>1</td>
    </tr>
  </tbody>
</table>

**Return Data Type**

<table style="font-size:small">

<thead>
<tr>
<th>INPUT</th><th><code>INT32</code></th><th><code>INT64</code></th><th><code>UINT32</code></th><th><code>UINT64</code></th><th><code>NUMERIC</code></th><th><code>BIGNUMERIC</code></th><th><code>FLOAT</code></th><th><code>DOUBLE</code></th>
</tr>
</thead>
<tbody>
<tr><th><code>INT32</code></th><td style="vertical-align:middle"><code>INT64</code></td><td style="vertical-align:middle"><code>INT64</code></td><td style="vertical-align:middle"><code>INT64</code></td><td style="vertical-align:middle">ERROR</td><td style="vertical-align:middle"><code>NUMERIC</code></td><td style="vertical-align:middle"><code>BIGNUMERIC</code></td><td style="vertical-align:middle"><code>DOUBLE</code></td><td style="vertical-align:middle"><code>DOUBLE</code></td></tr>
<tr><th><code>INT64</code></th><td style="vertical-align:middle"><code>INT64</code></td><td style="vertical-align:middle"><code>INT64</code></td><td style="vertical-align:middle"><code>INT64</code></td><td style="vertical-align:middle">ERROR</td><td style="vertical-align:middle"><code>NUMERIC</code></td><td style="vertical-align:middle"><code>BIGNUMERIC</code></td><td style="vertical-align:middle"><code>DOUBLE</code></td><td style="vertical-align:middle"><code>DOUBLE</code></td></tr>
<tr><th><code>UINT32</code></th><td style="vertical-align:middle"><code>INT64</code></td><td style="vertical-align:middle"><code>INT64</code></td><td style="vertical-align:middle"><code>INT64</code></td><td style="vertical-align:middle"><code>INT64</code></td><td style="vertical-align:middle"><code>NUMERIC</code></td><td style="vertical-align:middle"><code>BIGNUMERIC</code></td><td style="vertical-align:middle"><code>DOUBLE</code></td><td style="vertical-align:middle"><code>DOUBLE</code></td></tr>
<tr><th><code>UINT64</code></th><td style="vertical-align:middle">ERROR</td><td style="vertical-align:middle">ERROR</td><td style="vertical-align:middle"><code>INT64</code></td><td style="vertical-align:middle"><code>INT64</code></td><td style="vertical-align:middle"><code>NUMERIC</code></td><td style="vertical-align:middle"><code>BIGNUMERIC</code></td><td style="vertical-align:middle"><code>DOUBLE</code></td><td style="vertical-align:middle"><code>DOUBLE</code></td></tr>
<tr><th><code>NUMERIC</code></th><td style="vertical-align:middle"><code>NUMERIC</code></td><td style="vertical-align:middle"><code>NUMERIC</code></td><td style="vertical-align:middle"><code>NUMERIC</code></td><td style="vertical-align:middle"><code>NUMERIC</code></td><td style="vertical-align:middle"><code>NUMERIC</code></td><td style="vertical-align:middle"><code>BIGNUMERIC</code></td><td style="vertical-align:middle"><code>DOUBLE</code></td><td style="vertical-align:middle"><code>DOUBLE</code></td></tr>
<tr><th><code>BIGNUMERIC</code></th><td style="vertical-align:middle"><code>BIGNUMERIC</code></td><td style="vertical-align:middle"><code>BIGNUMERIC</code></td><td style="vertical-align:middle"><code>BIGNUMERIC</code></td><td style="vertical-align:middle"><code>BIGNUMERIC</code></td><td style="vertical-align:middle"><code>BIGNUMERIC</code></td><td style="vertical-align:middle"><code>BIGNUMERIC</code></td><td style="vertical-align:middle"><code>DOUBLE</code></td><td style="vertical-align:middle"><code>DOUBLE</code></td></tr>
<tr><th><code>FLOAT</code></th><td style="vertical-align:middle"><code>DOUBLE</code></td><td style="vertical-align:middle"><code>DOUBLE</code></td><td style="vertical-align:middle"><code>DOUBLE</code></td><td style="vertical-align:middle"><code>DOUBLE</code></td><td style="vertical-align:middle"><code>DOUBLE</code></td><td style="vertical-align:middle"><code>DOUBLE</code></td><td style="vertical-align:middle"><code>DOUBLE</code></td><td style="vertical-align:middle"><code>DOUBLE</code></td></tr>
<tr><th><code>DOUBLE</code></th><td style="vertical-align:middle"><code>DOUBLE</code></td><td style="vertical-align:middle"><code>DOUBLE</code></td><td style="vertical-align:middle"><code>DOUBLE</code></td><td style="vertical-align:middle"><code>DOUBLE</code></td><td style="vertical-align:middle"><code>DOUBLE</code></td><td style="vertical-align:middle"><code>DOUBLE</code></td><td style="vertical-align:middle"><code>DOUBLE</code></td><td style="vertical-align:middle"><code>DOUBLE</code></td></tr>
</tbody>

</table>

## `SEC`

```
SEC(X)
```

**Description**

Computes the secant for the angle of `X`, where `X` is specified in radians.
`X` can be any data type
that [coerces to `DOUBLE`][conversion-rules].

<table>
  <thead>
    <tr>
      <th>X</th>
      <th>SEC(X)</th>
    </tr>
    </thead>
    <tbody>
    <tr>
      <td><code>+inf</code></td>
      <td><code>NaN</code></td>
    </tr>
    <tr>
      <td><code>-inf</code></td>
      <td><code>NaN</code></td>
    </tr>
    <tr>
      <td><code>NaN</code></td>
      <td><code>NaN</code></td>
    </tr>
    <tr>
      <td><code>NULL</code></td>
      <td><code>NULL</code></td>
    </tr>
  </tbody>
</table>

**Return Data Type**

`DOUBLE`

**Example**

```googlesql
SELECT SEC(100) AS a, SEC(-1) AS b;

/*----------------+---------------+
 | a              | b             |
 +----------------+---------------+
 | 1.159663822905 | 1.85081571768 |
 +----------------+---------------*/
```

[conversion-rules]: https://github.com/google/googlesql/blob/master/docs/conversion_rules.md

## `SECH`

```
SECH(X)
```

**Description**

Computes the hyperbolic secant for the angle of `X`, where `X` is specified
in radians. `X` can be any data type
that [coerces to `DOUBLE`][conversion-rules].
Never produces an error.

<table>
  <thead>
    <tr>
      <th>X</th>
      <th>SECH(X)</th>
    </tr>
    </thead>
    <tbody>
    <tr>
      <td><code>+inf</code></td>
      <td><code>0</code></td>
    </tr>
    <tr>
      <td><code>-inf</code></td>
      <td><code>0</code></td>
    </tr>
    <tr>
      <td><code>NaN</code></td>
      <td><code>NaN</code></td>
    </tr>
    <tr>
      <td><code>NULL</code></td>
      <td><code>NULL</code></td>
    </tr>
  </tbody>
</table>

**Return Data Type**

`DOUBLE`

**Example**

```googlesql
SELECT SECH(0.5) AS a, SECH(-2) AS b, SECH(100) AS c;

/*----------------+----------------+---------------------+
 | a              | b              | c                   |
 +----------------+----------------+---------------------+
 | 0.88681888397  | 0.265802228834 | 7.4401519520417E-44 |
 +----------------+----------------+---------------------*/
```

[conversion-rules]: https://github.com/google/googlesql/blob/master/docs/conversion_rules.md

## `SIGN`

```
SIGN(X)
```

**Description**

Returns `-1`, `0`, or `+1` for negative, zero and positive arguments
respectively. For floating point arguments, this function doesn't distinguish
between positive and negative zero.

<table>
  <thead>
    <tr>
      <th>X</th>
      <th>SIGN(X)</th>
    </tr>
    </thead>
    <tbody>
    <tr>
      <td>25</td>
      <td>+1</td>
    </tr>
    <tr>
      <td>0</td>
      <td>0</td>
    </tr>
    <tr>
      <td>-25</td>
      <td>-1</td>
    </tr>
    <tr>
      <td>NaN</td>
      <td>NaN</td>
    </tr>
  </tbody>
</table>

**Return Data Type**

<table>

<thead>
<tr>
<th>INPUT</th><th><code>INT32</code></th><th><code>INT64</code></th><th><code>UINT32</code></th><th><code>UINT64</code></th><th><code>NUMERIC</code></th><th><code>BIGNUMERIC</code></th><th><code>FLOAT</code></th><th><code>DOUBLE</code></th>
</tr>
</thead>
<tbody>
<tr><th>OUTPUT</th><td style="vertical-align:middle"><code>INT32</code></td><td style="vertical-align:middle"><code>INT64</code></td><td style="vertical-align:middle"><code>UINT32</code></td><td style="vertical-align:middle"><code>UINT64</code></td><td style="vertical-align:middle"><code>NUMERIC</code></td><td style="vertical-align:middle"><code>BIGNUMERIC</code></td><td style="vertical-align:middle"><code>FLOAT</code></td><td style="vertical-align:middle"><code>DOUBLE</code></td></tr>
</tbody>

</table>

## `SIN`

```
SIN(X)
```

**Description**

Computes the sine of X where X is specified in radians. Never fails.

<table>
  <thead>
    <tr>
      <th>X</th>
      <th>SIN(X)</th>
    </tr>
  </thead>
  <tbody>
    <tr>
      <td><code>+inf</code></td>
      <td><code>NaN</code></td>
    </tr>
    <tr>
      <td><code>-inf</code></td>
      <td><code>NaN</code></td>
    </tr>
    <tr>
      <td><code>NaN</code></td>
      <td><code>NaN</code></td>
    </tr>
  </tbody>
</table>

## `SINH`

```
SINH(X)
```

**Description**

Computes the hyperbolic sine of X where X is specified in radians. Generates
an error if overflow occurs.

<table>
  <thead>
    <tr>
      <th>X</th>
      <th>SINH(X)</th>
    </tr>
  </thead>
  <tbody>
    <tr>
      <td><code>+inf</code></td>
      <td><code>+inf</code></td>
    </tr>
    <tr>
      <td><code>-inf</code></td>
      <td><code>-inf</code></td>
    </tr>
    <tr>
      <td><code>NaN</code></td>
      <td><code>NaN</code></td>
    </tr>
  </tbody>
</table>

## `SQRT`

```
SQRT(X)
```

**Description**

Computes the square root of X. Generates an error if X is less than 0.

<table>
  <thead>
    <tr>
      <th>X</th>
      <th>SQRT(X)</th>
    </tr>
    </thead>
    <tbody>
    <tr>
      <td><code>25.0</code></td>
      <td><code>5.0</code></td>
    </tr>
    <tr>
      <td><code>+inf</code></td>
      <td><code>+inf</code></td>
    </tr>
    <tr>
      <td><code>X &lt; 0</code></td>
      <td>Error</td>
    </tr>
  </tbody>
</table>

**Return Data Type**

<table>

<thead>
<tr>
<th>INPUT</th><th><code>INT32</code></th><th><code>INT64</code></th><th><code>UINT32</code></th><th><code>UINT64</code></th><th><code>NUMERIC</code></th><th><code>BIGNUMERIC</code></th><th><code>FLOAT</code></th><th><code>DOUBLE</code></th>
</tr>
</thead>
<tbody>
<tr><th>OUTPUT</th><td style="vertical-align:middle"><code>DOUBLE</code></td><td style="vertical-align:middle"><code>DOUBLE</code></td><td style="vertical-align:middle"><code>DOUBLE</code></td><td style="vertical-align:middle"><code>DOUBLE</code></td><td style="vertical-align:middle"><code>NUMERIC</code></td><td style="vertical-align:middle"><code>BIGNUMERIC</code></td><td style="vertical-align:middle"><code>DOUBLE</code></td><td style="vertical-align:middle"><code>DOUBLE</code></td></tr>
</tbody>

</table>

## `TAN`

```
TAN(X)
```

**Description**

Computes the tangent of X where X is specified in radians. Generates an error if
overflow occurs.

<table>
  <thead>
    <tr>
      <th>X</th>
      <th>TAN(X)</th>
    </tr>
  </thead>
  <tbody>
    <tr>
      <td><code>+inf</code></td>
      <td><code>NaN</code></td>
    </tr>
    <tr>
      <td><code>-inf</code></td>
      <td><code>NaN</code></td>
    </tr>
    <tr>
      <td><code>NaN</code></td>
      <td><code>NaN</code></td>
    </tr>
  </tbody>
</table>

## `TANH`

```
TANH(X)
```

**Description**

Computes the hyperbolic tangent of X where X is specified in radians. Doesn't
fail.

<table>
  <thead>
    <tr>
      <th>X</th>
      <th>TANH(X)</th>
    </tr>
  </thead>
  <tbody>
    <tr>
      <td><code>+inf</code></td>
      <td>1.0</td>
    </tr>
    <tr>
      <td><code>-inf</code></td>
      <td>-1.0</td>
    </tr>
    <tr>
      <td><code>NaN</code></td>
      <td><code>NaN</code></td>
    </tr>
  </tbody>
</table>

## `TRUNC`

```
TRUNC(X [, N])
```

**Description**

If only X is present, `TRUNC` rounds X to the nearest integer whose absolute
value isn't greater than the absolute value of X. If N is also present, `TRUNC`
behaves like `ROUND(X, N)`, but always rounds towards zero and never overflows.

<table>
  <thead>
    <tr>
      <th>X</th>
      <th>TRUNC(X)</th>
    </tr>
  </thead>
  <tbody>
    <tr>
      <td>2.0</td>
      <td>2.0</td>
    </tr>
    <tr>
      <td>2.3</td>
      <td>2.0</td>
    </tr>
    <tr>
      <td>2.8</td>
      <td>2.0</td>
    </tr>
    <tr>
      <td>2.5</td>
      <td>2.0</td>
    </tr>
    <tr>
      <td>-2.3</td>
      <td>-2.0</td>
    </tr>
    <tr>
      <td>-2.8</td>
      <td>-2.0</td>
    </tr>
    <tr>
      <td>-2.5</td>
      <td>-2.0</td>
    </tr>
    <tr>
      <td>0</td>
      <td>0</td>
    </tr>
    <tr>
      <td><code>+inf</code></td>
      <td><code>+inf</code></td>
    </tr>
    <tr>
      <td><code>-inf</code></td>
      <td><code>-inf</code></td>
    </tr>
    <tr>
      <td><code>NaN</code></td>
      <td><code>NaN</code></td>
    </tr>
  </tbody>
</table>

**Return Data Type**

<table>

<thead>
<tr>
<th>INPUT</th><th><code>INT32</code></th><th><code>INT64</code></th><th><code>UINT32</code></th><th><code>UINT64</code></th><th><code>NUMERIC</code></th><th><code>BIGNUMERIC</code></th><th><code>FLOAT</code></th><th><code>DOUBLE</code></th>
</tr>
</thead>
<tbody>
<tr><th>OUTPUT</th><td style="vertical-align:middle"><code>DOUBLE</code></td><td style="vertical-align:middle"><code>DOUBLE</code></td><td style="vertical-align:middle"><code>DOUBLE</code></td><td style="vertical-align:middle"><code>DOUBLE</code></td><td style="vertical-align:middle"><code>NUMERIC</code></td><td style="vertical-align:middle"><code>BIGNUMERIC</code></td><td style="vertical-align:middle"><code>DOUBLE</code></td><td style="vertical-align:middle"><code>DOUBLE</code></td></tr>
</tbody>

</table>

