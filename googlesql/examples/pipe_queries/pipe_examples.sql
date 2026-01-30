#
# Copyright 2019 Google LLC
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#     http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.
#

-- This file has example queries demonstrating several features of
-- GoogleSQL pipe syntax, running against the TPCH table schemas.
--
-- The queries are runnable in the `execute_query` query tool.
-- Run with
--   execute_query --catalog=tpch --web
-- and then paste in this script and execute.

--
-- First example
-- -------------
--

-- Many operators look just like the standard SQL clauses, but can be applied in
-- any order, any number of times.
FROM Orders
|> SELECT o_orderkey, o_orderpriority, o_orderdate AS date
|> SELECT *, EXTRACT(MONTH FROM date) AS month
|> WHERE month = 2
|> WHERE o_orderpriority = '1-URGENT'
|> ORDER BY date
|> LIMIT 20;

--
-- Two-level aggregation example
-- -----------------------------
--

-- Standard syntax - inside-out data flow starting from the middle of a subquery.
SELECT c_count, COUNT(*) AS custdist
FROM
  (
    SELECT c_custkey, COUNT(o_orderkey) c_count
    FROM customer
    LEFT OUTER JOIN orders
      ON c_custkey = o_custkey
    GROUP BY c_custkey
  ) AS c_orders
GROUP BY c_count
ORDER BY custdist DESC, c_count DESC;

-- Pipe syntax - top-to-bottom linear data flow.
FROM customer
|> LEFT OUTER JOIN orders ON c_custkey = o_custkey
|> AGGREGATE COUNT(o_orderkey) c_count
   GROUP BY c_custkey
|> AGGREGATE COUNT(*) AS custdist
   GROUP BY c_count
|> ORDER BY custdist DESC, c_count DESC;

--
-- FROM queries - starting a query with FROM
-- -----------------------------------------
--

-- Querying with just a FROM clause - returns the whole table.
FROM Region;

-- A standard FROM clause containing a JOIN.
FROM Nation JOIN Region ON n_regionkey = r_regionkey;

-- A standard FROM clause with comma joins, with post-filtering in a WHERE.
FROM Nation, Region
|> WHERE n_regionkey = r_regionkey;

--
-- JOIN works as a pipe operator, using the usual syntax
-- -----------------------------------------------------
--

FROM Nation
|> JOIN Region ON n_regionkey = r_regionkey;

-- Same thing with a LEFT JOIN
FROM Nation
|> LEFT JOIN Region ON n_regionkey = r_regionkey AND r_name LIKE 'A%';

-- Join with UNNEST of an array
FROM Nation
|> SELECT N_NAME, split(n_name, '') AS letters
|> JOIN UNNEST(letters) AS letter
|> WHERE letter = 'U';

--
-- SELECT, and other syntaxes to update columns
-- --------------------------------------------
--

-- SELECT supports all standard syntax SELECT features and modifiers.
FROM Region
|> SELECT *;

FROM Nation
|> SELECT DISTINCT n_regionkey;

FROM Region
|> SELECT r_name, length(r_name) AS name_length;

-- Alternatively, use EXTEND to compute an expression and add it, keeping
-- existing columns.
FROM Region
|> EXTEND length(r_name) AS name_length;

-- Applying SELECT multiple times.  Each can reference previously selected
-- columns.
FROM Region
|> SELECT r_name
|> SELECT r_name, LOWER(r_name) AS lower_name
|> SELECT lower_name, SUBSTR(r_name, 1, 1) || SUBSTR(lower_name, 2) AS mixed_name;

-- Same thing, using EXTEND.
FROM Region
|> SELECT r_name
|> EXTEND LOWER(r_name) AS lower_name
|> EXTEND SUBSTR(r_name, 1, 1) || SUBSTR(lower_name, 2) AS mixed_name;

-- DROP: Drop columns I don't want in the output.
FROM Nation
|> DROP n_regionkey, n_comment;

-- SET: I can use SET to replace a column with a computed expression.
-- I can even apply it multiple times to update a column value incrementally.
FROM Region
|> SET r_name = INITCAP(r_name)
|> SET r_name = CONCAT(r_name, "!");

-- RENAME: I can use RENAME to change the name of an output column.
FROM Region
|> DROP r_comment
|> RENAME r_name AS RegionName;

--
-- AGGREGATE in pipe syntax
-- ------------------------
--

-- Full-table aggregation
FROM Orders
|> AGGREGATE COUNT(*) AS num_orders, COUNT(DISTINCT o_custkey) AS num_customers;

-- Aggregation with grouping
FROM Orders
|> AGGREGATE COUNT(*) AS num_orders, COUNT(DISTINCT o_custkey) AS num_customers
   GROUP BY o_orderpriority;

-- The GROUP BY can include computed expressions, and assign them column names.
-- We can add an ORDER BY, but it seems repetitive to list the column names.
FROM Orders
|> AGGREGATE COUNT(*) AS num_orders, SUM(o_totalprice) AS price
   GROUP BY EXTRACT(YEAR FROM o_orderdate) AS year, o_orderpriority
|> ORDER BY year, o_orderpriority
|> LIMIT 12;

-- Aggregation ordering shorthand #1:
-- - GROUP AND ORDER BY to order by everything in GROUP BY.
-- - Optionally order some columns DESC.
FROM Orders
|> AGGREGATE COUNT(*) AS num_orders, SUM(o_totalprice) AS price
   GROUP AND ORDER BY EXTRACT(YEAR FROM o_orderdate) AS year, o_orderpriority DESC
|> LIMIT 12;

-- Aggregation ordering shorthand #2:
-- - ASC/DESC on GROUP BY columns
-- - ASC/DESC on aggregate columns
-- Orders by selected grouping columns first, then selected aggregate columns.
FROM Orders
|> AGGREGATE COUNT(*) AS num_orders, SUM(o_totalprice) AS price DESC
   GROUP BY EXTRACT(YEAR FROM o_orderdate) AS year, o_orderpriority ASC
|> LIMIT 12;

--
-- AGGREGATE example, with standard and pipe syntax
-- ------------------------------------------------
--

-- Aggregation in standard syntax, with ordering and a computed
-- grouping expression.
SELECT
  EXTRACT(YEAR FROM o_orderdate) AS year,
  SUM(o_totalprice) AS price
FROM Orders
GROUP BY year
ORDER BY price DESC;

-- The same aggregation in pipe syntax in one step.
FROM Orders
|> AGGREGATE SUM(o_totalprice) AS price DESC
   GROUP BY EXTRACT(YEAR FROM o_orderdate) AS year;

--
-- Adding pipe operators on the end of standard syntax queries
-- -----------------------------------------------------------
--

-- e.g. Add an aggregate on the end of a query to compute some stats.
SELECT
  EXTRACT(YEAR FROM o_orderdate) AS year,
  SUM(o_totalprice) AS price
FROM Orders
GROUP BY year
ORDER BY price DESC
|> AGGREGATE COUNT(*), MIN(year), MAX(year);
