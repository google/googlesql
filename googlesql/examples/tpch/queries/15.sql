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

WITH
  revenue AS (
    SELECT
      l_suppkey AS supplier_no,
      sum(l_extendedprice * (1 - l_discount)) AS total_revenue
    FROM lineitem
    WHERE
      l_shipdate >= date '1997-05-01'
      AND l_shipdate < date_add(date '1997-05-01', INTERVAL 3 month)
    GROUP BY l_suppkey
  )
SELECT
  s_suppkey,
  s_name,
  s_address,
  s_phone,
  total_revenue
FROM
  supplier,
  revenue
WHERE
  s_suppkey = supplier_no
  AND total_revenue = (
    SELECT max(total_revenue)
    FROM revenue
  )
ORDER BY s_suppkey;
