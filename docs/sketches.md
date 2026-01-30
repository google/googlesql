

<!-- mdlint off(WHITESPACE_LINE_LENGTH) -->

# Sketches

GoogleSQL supports data sketches.
A data sketch is a compact summary of a data aggregation. It captures all the
necessary information to either extract an aggregation result, continue a
data aggregation, or merge it with another sketch, enabling re-aggregation.

Computing a metric using a sketch is substantially less expensive than computing
an exact value. If your computation is too slow or requires too much temporary
storage, use sketches to reduce query time and resources.

Additionally, computing [cardinalities][cardinality]{: .external}, such as the
number of distinct users, or [quantiles][quantiles-wiki]{: .external}, such as
median visit duration, without sketches is usually only possible by running jobs
over the raw data because already-aggregated data can't be combined anymore.

Consider a table with the following data:

<table>
  <thead>
    <tr>
      <th>Product</th>
      <th>Number of users</th>
      <th>Median visit duration</th>
    </tr>
  </thead>
  <tbody>
    <tr>
      <td>Product A</td>
      <td>500 million</td>
      <td>10 minutes</td>
    </tr>
    <tr>
      <td>Product B</td>
      <td>20 million</td>
      <td>2 minutes</td>
    </tr>
  </tbody>
</table>

Computing the total number of users for both products isn't possible because we
don't know how many users used both products in the table.

A solution is to store sketches in the table instead. Each sketch is an
approximate and compact representation of a particular input property, such as
cardinality, that you can store, merge (or re-aggregate), and query for
near-exact results. In the previous example, you can estimate the number of
distinct users for Product A and Product B by creating and merging
(re-aggregating) the sketches for each product.

Because a sketch has lossy compression of the original data, it introduces a
statistical error that's represented by an error bound or confidence interval
(CI). For most applications, this uncertainty is small. For example, a typical
cardinality-counting sketch has a relative error of about 1% in 95% of all
cases. A sketch trades some accuracy, or _precision_, for faster and less
expensive computations, and less storage.

In summary, a sketch has these main properties:

+   Represents an approximate aggregate for a specific metric
+   Is compact
+   Is a serialized form of an in-memory, sublinear data structure
+   Is typically a fixed size and asymptotically smaller than the input
+   Can introduce a statistical error that you determine with a precision
    level
+   Can be merged with other sketches to summarize the union of the underlying
    data sets

## Re-aggregation with sketch merging

Sketches let you store and merge data for efficient re-aggregation. This makes
sketches particularly useful for materialized views of data sets. You can merge
sketches to construct a summary of multiple data streams based on partial
sketches created for each stream.

For example, if you create a sketch for the estimated number of distinct users
every day, you can get the number of distinct users during the last seven days
by merging daily sketches. Re-aggregating the merged daily sketches helps you
avoid reading the full input of the data set.

Sketch re-aggregation is also useful in online analytical processing (OLAP). You
can merge sketches to create a roll-up of an
[OLAP cube][olap]{: .external}, where the
sketch summarizes data along one or more specific dimensions of the cube. OLAP
roll-ups aren't possible with true distinct counts.

### Estimate cardinality

If you need to estimate [cardinality][cardinality]{: .external}, use an [HLL++ sketch][hll-sketch].

For example, to get the number of unique users who actively used a product in a
given month (MAU or 28DAU metrics), use an HLL++ sketch.

## HLL++ sketches 
<a id="sketches_hll"></a>

HyperLogLog++ (HLL++) is a sketching algorithm for estimating cardinality. HLL++
is based on the paper [HyperLogLog in Practice][hll]{: .external}, where the
_++_ denotes the augmentations made to the HyperLogLog algorithm.

[Cardinality][cardinality]{: .external} is the number of distinct elements in the
input for a sketch. For example, you could use an HLL++ sketch to get the number
of unique users who have opened an application.

HLL++ estimates very small and very large cardinalities. HLL++ includes a
64-bit hash function, sparse representation to reduce memory requirements for
small cardinality estimates, and empirical bias correction for
small cardinality estimates.

<a id="precision_hll"></a>

**Precision**

HLL++ sketches support custom precision. The following table shows the supported
precision values, the maximum storage size, and the confidence interval (CI) of
typical precision levels:

Precision    | Max storage size | 65% CI | 95% CI | 99% CI
------------ | -------------------- | ------ | ------ | ------
10           | 1 KiB + 28 B         | ±3.25% | ±6.50% | ±9.75%
11           | 2 KiB + 28 B         | ±2.30% | ±4.60% | ±6.89%
12           | 4 KiB + 28 B         | ±1.63% | ±3.25% | ±4.88%
13           | 8 KiB + 28 B         | ±1.15% | ±2.30% | ±3.45%
14           | 16 KiB + 30 B        | ±0.81% | ±1.63% | ±2.44%
15 (default) | 32 KiB + 30 B        | ±0.57% | ±1.15% | ±1.72%
16           | 64 KiB + 30 B        | ±0.41% | ±0.81% | ±1.22%
17           | 128 KiB + 30 B       | ±0.29% | ±0.57% | ±0.86%
18           | 256 KiB + 30 B       | ±0.20% | ±0.41% | ±0.61%
19           | 512 KiB + 30 B       | ±0.14% | ±0.29% | ±0.43%
20           | 1024 KiB + 30 B      | ±0.10% | ±0.20% | ±0.30%
21           | 2048 KiB + 32 B      | ±0.07% | ±0.14% | ±0.22%
22           | 4096 KiB + 32 B      | ±0.05% | ±0.10% | ±0.15%
23           | 8192 KiB + 32 B      | ±0.04% | ±0.07% | ±0.11%
24           | 16384 KiB + 32 B     | ±0.03% | ±0.05% | ±0.08%

You can define precision for an HLL++ sketch when you initialize it with the
[`HLL_COUNT.INIT`][hll-init] function.

**Deletion**

You can't delete values from an HLL++ sketch.

**Additional details**

For a list of functions that you can use with HLL++ sketches, see
[HLL++ functions][hll-functions].

### Sketch integration

You can integrate HLL++ sketches with other systems. For example, you can build
sketches in external applications, like [Dataflow][dataflow]{: .external},
[Apache Spark][spark]{: .external}, and [ZetaSketch][zetasketch]{: .external}
and then consume them in GoogleSQL or vice versa.

In addition to GoogleSQL, you can use HLL++ sketches
with [Java][java-zetasketch]{: .external}.

## Approximate aggregate functions 
<a id="approx_functions"></a>

As an alternative to specific sketch-based approximation functions,
GoogleSQL provides predefined approximate aggregate
functions. These approximate aggregate functions support sketches for common
estimations such as distinct count, quantiles, and top count, but they don't
allow custom precision. They also don't expose and store the sketch for
re-aggregation like other types of sketches. The approximate aggregate functions
are designed for running quick sketch-based queries without detailed
configuration.

For a list of approximate aggregate functions that you can use with
sketch-based approximation, see
[Approximate aggregate functions][approx-aggregate-functions].

<!-- mdlint off(WHITESPACE_LINE_LENGTH) -->

[cardinality]: https://en.wikipedia.org/wiki/Cardinality

[quantiles-wiki]: https://en.wikipedia.org/wiki/Quantile

[spark]: https://spark.apache.org

[dataflow]: https://cloud.google.com/dataflow

[olap]: https://en.wikipedia.org/wiki/OLAP_cube

[zetasketch]: https://github.com/google/zetasketch

[java-zetasketch]: https://github.com/google/zetasketch/blob/master/java/com/google/zetasketch/HyperLogLogPlusPlus.java

[hll]: https://research.google.com/pubs/archive/40671.pdf

[hll-sketch]: #sketches_hll

[hll-functions]: https://github.com/google/googlesql/blob/master/docs/hll_functions.md

[hll-init]: https://github.com/google/googlesql/blob/master/docs/hll_functions.md#hll_countinit

[approx-aggregate-functions]: https://github.com/google/googlesql/blob/master/docs/approximate_aggregate_functions.md

[approx-functions]: #approx_functions

<!-- mdlint on -->

