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
  s_name,
  s_address
FROM
  supplier,
  nation
WHERE
  s_suppkey IN (
    SELECT ps_suppkey
    FROM
      partsupp,
      (
        SELECT p_partkey
        FROM part
        WHERE p_name LIKE 'tan%'
      ) AS selected_parts
    WHERE
      ps_partkey = p_partkey
      AND ps_availqty > (
        SELECT 0.5 * sum(l_quantity)
        FROM lineitem
        WHERE
          l_partkey = ps_partkey
          AND l_suppkey = ps_suppkey
          AND l_shipdate >= date '1996-01-01'
          AND l_shipdate < date_add(date '1996-01-01', INTERVAL 1 year)
      )
  )
  AND s_nationkey = n_nationkey
  AND n_name = 'PERU'
ORDER BY
  s_name;
