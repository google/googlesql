

<!-- mdlint off(WHITESPACE_LINE_LENGTH) -->

# Use differential privacy 
<a id="differential-privacy"></a>

<!-- BEGIN CONTENT -->

This document provides general information about differential privacy for
GoogleSQL. For syntax, see the [differential privacy clause][dp-clause].
For a list of functions that you can use with this syntax, see
[differentially private aggregate functions][dp-functions].

Note: In this topic, the privacy parameters in the examples aren't
recommendations. You should work with your privacy or security officer to
determine the optimal privacy parameters for your dataset and organization.

## What is differential privacy?

Differential privacy is a standard for computations on data that limits the
personal information that's revealed by an output. Differential privacy is
commonly used to share data and to allow inferences about groups of people
while preventing someone from learning information about an individual.

Differential privacy is useful:

+ Where a risk of re-identification exists.
+ To quantify the tradeoff between risk and analytical utility.

To better understand differential privacy, let's look at a simple example.

This bar chart shows the busyness of a small restaurant on one particular
evening. Lots of guests come at 7 PM, and the restaurant is completely empty
at 1 AM:

<center>
![Chart shows busyness of a small rest by mapping visitors at specific hours of the day.][dp-chart-a]
</center>

This chart looks useful, but there's a catch. When a new guest arrives, this
fact is immediately revealed by the bar chart. In the following chart, it's
clear that there's a new guest, and that this guest arrived at roughly 1 AM:

<center>
![Chart shows outlier arrival.][dp-chart-b]
</center>

Showing this detail isn't great from a privacy perspective, as anonymized
statistics shouldn't reveal individual contributions. Putting those two charts
side by side makes it even more apparent: the orange bar chart has one extra
guest that has arrived around 1 AM:

<center>
![Chart comparison highlights an individual contribution.][dp-chart-c]
</center>

Again, that's not great. To avoid this kind privacy issue, you can add random
noise to the bar charts by using differential privacy. In the following
comparison chart, the results are anonymized and no longer reveal individual
contributions.

<center>
![Differential privacy is applied to comparisons.][dp-chart-d]
</center>

## How differential privacy works on queries

The goal of [differential privacy][dp-paper-cis]{: .external} is to mitigate
disclosure risk: the risk that someone can learn information about an entity in
a dataset. Differential privacy balances the need to safeguard privacy
against the need for statistical analytical utility. As privacy increases,
statistical analytical utility decreases, and vice versa.

With GoogleSQL, you can transform the results of a query with
differentially private aggregations. When the query is executed, it performs
the following:

1.  Computes per-entity aggregations for each group if groups are specified with
    a `GROUP BY` clause. Limits the number of groups each entity can
    contribute to, based on the `kappa` or `max_groups_contributed` differential privacy parameter.
1.  [Clamps][dp-clamping] each per-entity aggregate contribution to be within
    the clamping bounds. If the clamping bounds aren't specified, they are
    implicitly calculated in a differentially private way.
1.  Aggregates the clamped per-entity aggregate contributions for each group.
1.  Adds noise to the final aggregate value for each group. The scale of
    random noise is a function of all of the clamped bounds and privacy
    parameters.
1.  Computes a noisy entity count for each group and eliminates groups with
    few entities. A noisy entity count helps eliminate a non-deterministic set
    of groups.

The final result is a dataset where each group has noisy aggregate results
and small groups have been eliminated.

Note: GoogleSQL relies on
[Google's open source differential privacy library][dp-library]{: .external}
to implement differential privacy functionality. The library
provides low-level differential privacy primitives that you can use to
implement end-to-end privacy systems. For additional information on guarantees,
see [Limitations on privacy guarantees][dp-privacy-guarantees].

For additional context on what differential privacy is and its use cases, see
the following articles:

+ [A friendly, non-technical introduction to differential privacy][friendly-dp]{: .external}
+ [Differentially private SQL with bounded user contribution][dp-paper]{: .external}
+ [Differential privacy on Wikipedia][wiki-diff-privacy]{: .external}

## Produce a valid differentially private query 
<a id="dp_rules"></a>

The following rules must be met for the differentially private query to be
valid:

+ A [privacy unit column][dp-define-privacy-unit-id] is defined.
+ The `SELECT` list contains a [differentially private clause][dp-clause].
+ Only [differentially private aggregate functions][dp-functions] are
  in the `SELECT` list with the differentially private clause.
+ [`FROM` clause rules][dp-from] are used for
  differentially private table expressions.

## Define a privacy unit column 
<a id="dp_define_privacy_unit_id"></a>

A privacy unit is the entity in a dataset that's being protected, using
differential privacy. An entity can be an individual, a company, a location,
or any column that you choose.

A differentially private query must include one and only one
_privacy unit column_. A privacy unit column is a unique identifier for a
privacy unit and can exist within multiple groups. Because multiple groups
are supported, the data type for the
privacy unit column must be [groupable][data-types-groupable].

You can define a privacy unit column in the `OPTIONS` clause of a view or
differential privacy clause with one of the following unique identifiers:

+ In views: `anonymization_userid_column`
+ In the differential privacy clause: `privacy_unit_column`

In the following example, a privacy unit column,
`anonymization_userid_column='id'`, is added to a view. `id` represents a
privacy unit column in a table called `students`.

```googlesql
CREATE OR REPLACE VIEW view_on_students
OPTIONS(anonymization_userid_column='id')
AS (SELECT * FROM students);
```

In the following examples, a privacy unit column is added to a
differential privacy clause. `id` represents a column that originates from a
table called `students`.

```googlesql
SELECT WITH DIFFERENTIAL_PRIVACY
  OPTIONS (epsilon=10, delta=.01, privacy_unit_column=id)
  item,
  COUNT(*, contribution_bounds_per_group=>(0, 100))
FROM students;
```

```googlesql
SELECT WITH DIFFERENTIAL_PRIVACY
  OPTIONS (epsilon=10, delta=.01, privacy_unit_column=members.id)
  item,
  COUNT(*, contribution_bounds_per_group=>(0, 100))
FROM (SELECT * FROM students) AS members;
```

The following query is invalid because
[`view_on_students`][dp-example-views] contains a
privacy unit column and so does
[`view_on_professors`][dp-example-views]. To learn more about
`FROM` clause rules, see [Review `FROM` clause rules for differentially private
table expressions][dp-from-rules].

```googlesql {.bad}
-- This produces an error
SELECT WITH DIFFERENTIAL_PRIVACY
  OPTIONS(epsilon=10, delta=.01, max_groups_contributed=2)
  item,
  AVG(quantity, contribution_bounds_per_group => (0,100)) average_quantity
FROM {{USERNAME}}.view_on_professors, {{USERNAME}}.view_on_students
GROUP BY item;
```

## Remove noise from a differentially private query 
<a id="remove_noise"></a>

In the "Query syntax" reference, see [Remove noise][qs-remove-noise].

## Add noise to a differentially private query 
<a id="add_noise"></a>

In the "Query syntax" reference, see [Add noise][qs-add-noise].

## Limit the groups in which a privacy unit ID can exist 
<a id="limit_groups"></a>

In the "Query syntax" reference, see
[Limit the groups in which a privacy unit ID can exist][qs-limit-groups].

## Limitations

This section describes limitations of differential privacy.

### `FROM` clause rules for differentially private table expressions 
<a id="dp_from_rules"></a>

If a query contains a
differential privacy clause and the
privacy unit column isn't defined in that clause, these rules apply:

+ The query must contain at least one table expression or table path in the
  `FROM` clause that has a defined privacy unit column. The
  privacy unit column for a table expression is defined in a
  [view][dp-example-views].
+ If a `FROM` subquery contains a differentially private table expression,
  the subquery must produce a privacy unit column in its output or
  an error is returned.
+ In the `FROM` clause, joins are allowed between tables with and without a
  defined privacy unit column.
+ If the `FROM` clause contains multiple
  differentially private table expressions,
  then all joins between those relations must include the
  privacy unit column name in the join predicate or an error
  is returned.
+ Cross joins are disallowed between two differentially private
  table expressions, since they aren't joined on the privacy unit column.

### Performance implications of differential privacy

Differentially private queries execute more slowly than standard queries
because per-entity aggregation is performed and the `kappa` or `max_groups_contributed` limitation
is applied. Limiting contribution bounds can help improve the performance of
your differentially private queries.

The performance profiles of the following queries aren't similar:

```googlesql
SELECT
  WITH DIFFERENTIAL_PRIVACY OPTIONS(epsilon=1, delta=1e-10, privacy_unit_column=id)
  column_a, COUNT(column_b)
FROM table_a
GROUP BY column_a;
```

```googlesql
SELECT column_a, COUNT(column_b)
FROM table_a
GROUP BY column_a;
```

The reason for the performance difference is that an additional
finer-granularity level of grouping is performed for
differentially private queries, because per-entity aggregation must also be
performed.

The performance profiles of the following queries should be similar,
although the differentially private query is slightly slower:

```googlesql
SELECT
  WITH DIFFERENTIAL_PRIVACY OPTIONS(epsilon=1, delta=1e-10, privacy_unit_column=id)
  column_a, COUNT(column_b)
FROM table_a
GROUP BY column_a;
```

```googlesql
SELECT column_a, id, COUNT(column_b)
FROM table_a
GROUP BY column_a, id;
```

The differentially private query performs more slowly because it has a high
number of distinct values for the privacy unit column.

### Implicit bounding limitations for small datasets 
<a id="implicit_limits"></a>

Implicit bounding works best when computed using large datasets.
Implicit bounding can fail with datasets that contain a low number of
[privacy units][dp-define-privacy-unit-id], returning no results. Furthermore,
implicit bounding on a dataset with a low number of privacy units can clamp a
large portion of non-outliers, leading to underreported aggregations and
results that are altered more by clamping than by added noise. Datasets that
have a low number of privacy units or are thinly partitioned should use
explicit rather than implicit clamping.

### Privacy vulnerabilities

Any differential privacy algorithm—including this one—incurs the risk of a
private data leak when an analyst acts in bad faith, especially when computing
basic statistics like sums, due to arithmetic limitations.

#### Limitations on privacy guarantees 
<a id="privacy_guarantees"></a>

While GoogleSQL differential privacy applies the
[differential privacy algorithm][dp-paper]{: .external}, it doesn't make a
guarantee regarding the privacy properties of the resulting dataset.

#### Runtime errors

An analyst acting in bad faith with the ability to write queries or control
input data could trigger a runtime error on private data.

#### Floating point noise

Vulnerabilities related to rounding, repeated rounding, and
re-ordering attacks should be considered before using differential privacy.
These vulnerabilities are particularly concerning when an attacker can
control some of the contents of a dataset or the order of contents in a dataset.

Differentially private noise additions on floating-point data types are subject
to the vulnerabilities described in [Widespread Underestimation of Sensitivity
in Differentially Private Libraries and How to Fix It][dp-vulnerabilities]{: .external}.
Noise additions on integer data types aren't subject to the vulnerabilities
described in the paper.

#### Timing attack risks

An analyst acting in bad faith could execute a sufficiently complex query to
make an inference about input data based on a query's execution duration.

#### Misclassification

Creating a differential privacy query assumes that your data is in a well-known
and understood structure. If you apply differential privacy on the wrong
identifiers, such as one that represents a transaction ID instead of an
individual's ID, you could expose sensitive data.

<!-- mdlint off(WHITESPACE_LINE_LENGTH) -->

[wiki-diff-privacy]: https://en.wikipedia.org/wiki/Differential_privacy

[friendly-dp]: https://desfontain.es/privacy/friendly-intro-to-differential-privacy.html

[dp-paper]: https://arxiv.org/abs/1909.01917

[dp-paper-cis]: https://www.cis.upenn.edu/~aaroth/Papers/privacybook.pdf

[dp-vulnerabilities]: https://arxiv.org/abs/2207.10635

[dp-library]: https://github.com/google/differential-privacy

[dp-define-privacy-unit-id]: #dp_define_privacy_unit_id

[dp-from-rules]: #dp_from_rules

[dp-privacy-guarantees]: #privacy_guarantees

[dp-example-views]: https://github.com/google/googlesql/blob/master/docs/query-syntax.md#dp_example_views

[dp-clause]: https://github.com/google/googlesql/blob/master/docs/query-syntax.md#dp_clause

[dp-from]: https://github.com/google/googlesql/blob/master/docs/query-syntax.md#from_clause

[qs-add-noise]: https://github.com/google/googlesql/blob/master/docs/query-syntax.md#add_noise

[qs-remove-noise]: https://github.com/google/googlesql/blob/master/docs/query-syntax.md#eliminate_noise

[qs-limit-groups]: https://github.com/google/googlesql/blob/master/docs/query-syntax.md#limit_groups_for_privacy_unit

[data-types-groupable]: https://github.com/google/googlesql/blob/master/docs/data-types.md#groupable_data_types

[dp-functions]: https://github.com/google/googlesql/blob/master/docs/aggregate-dp-functions.md

[dp-clamping]: https://github.com/google/googlesql/blob/master/docs/aggregate-dp-functions.md#dp_clamping

[dp-chart-a]: https://cloud.google.com/docs/images/googlesql-dp-chart-a.png

[dp-chart-b]: https://cloud.google.com/docs/images/googlesql-dp-chart-b.png

[dp-chart-c]: https://cloud.google.com/docs/images/googlesql-dp-chart-c.png

[dp-chart-d]: https://cloud.google.com/docs/images/googlesql-dp-chart-d.gif

<!-- mdlint on -->

