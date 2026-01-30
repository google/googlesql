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

-- These are examples of pipe PIVOT and UNPIVOT operators.
-- They can be executed in `execute_query` with the `tpch` catalog.

-- PIVOT
--
-- This produces one column for each value listed in the IN, computing the
-- requested aggregate functions over the rows for that value.
--
-- More detail: Columns not mentioned in the PIVOT clause are grouping columns.
-- The FOR column is the source column to pivot from.  Output includes one row
-- for each group.  Output has the grouping columns, and then one column for each
-- value in the IN list, with the aggregate computed where the source column had
-- that value.
--
-- See
-- https://github.com/google/googlesql/blob/master/docs/pipe-syntax.md#pivot
-- for more details and options.

FROM Orders
|> SELECT EXTRACT(YEAR FROM o_orderdate) AS year, o_orderpriority, o_totalprice
|> PIVOT
     (
       COUNT(*) AS count,
       SUM(o_totalprice) AS price
           FOR o_orderpriority IN ('1-URGENT', '2-HIGH', '3-MEDIUM'));

-- UNPIVOT
--
-- For input rows with values in N columns, this produces N rows in a key/value
-- representation, with one row for each of the N columns.
--
-- More detail:
-- For each input row, this produces multiple output rows - one for each of
-- columns listed in IN.  The IN columns are removed and replaced by one column
-- (`animal` here), which indicates which source column this row came from.
-- The single `cnt` output column stores the values from each of the IN columns.

-- See
-- https://github.com/google/googlesql/blob/master/docs/pipe-syntax.md#unpivot
-- for more details and options.

WITH
  animals AS (
    SELECT 2000 AS year, 10 AS dogs, 12 AS cats, 14 AS birds
    UNION ALL
    SELECT 2001 AS year, 11 AS dogs, 15 AS cats, 17 AS birds
    UNION ALL
    SELECT 2002 AS year, 6 AS dogs, 5 AS cats, 2 AS birds
  )
FROM animals
|> UNPIVOT (cnt FOR animal IN (dogs, cats, birds))
|> ORDER BY year, cnt;
