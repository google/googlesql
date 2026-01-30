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

SELECT
  supp_nation,
  cust_nation,
  l_year,
  sum(volume) AS revenue
FROM
  (
    SELECT
      n1.n_name AS supp_nation,
      n2.n_name AS cust_nation,
      EXTRACT(year FROM l_shipdate) AS l_year,
      l_extendedprice * (1 - l_discount) AS volume
    FROM
      supplier,
      lineitem,
      orders,
      customer,
      nation n1,
      nation n2
    WHERE
      s_suppkey = l_suppkey
      AND o_orderkey = l_orderkey
      AND c_custkey = o_custkey
      AND s_nationkey = n1.n_nationkey
      AND c_nationkey = n2.n_nationkey
      AND (
        (n1.n_name = 'SAUDI ARABIA' AND n2.n_name = 'UNITED KINGDOM')
        OR (n1.n_name = 'UNITED KINGDOM' AND n2.n_name = 'SAUDI ARABIA'))
      AND l_shipdate BETWEEN date '1995-01-01' AND date '1996-12-31'
  ) AS shipping
GROUP BY
  supp_nation,
  cust_nation,
  l_year
ORDER BY
  supp_nation,
  cust_nation,
  l_year;
