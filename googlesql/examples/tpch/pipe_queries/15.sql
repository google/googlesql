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

# This uses a WITH clause, with the subquery in pipe syntax.
# It aliases the grouping column like the original query, although
# this is unnecessary.
WITH
  revenue AS (
    FROM lineitem
    |> WHERE
        l_shipdate >= date '1997-05-01'
        AND l_shipdate < date_add(date '1997-05-01', INTERVAL 3 month)
    |> AGGREGATE
        sum(l_extendedprice * (1 - l_discount)) AS total_revenue
       GROUP BY l_suppkey AS supplier_no
  )
FROM
  supplier,
  revenue
|> WHERE
    s_suppkey = supplier_no
    AND total_revenue = (
      SELECT max(total_revenue)
      FROM revenue
    )
|> SELECT
    s_suppkey,
    s_name,
    s_address,
    s_phone,
    total_revenue
|> ORDER BY s_suppkey;
