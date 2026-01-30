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
  c_count,
  COUNT(*) AS custdist
FROM
  (
    SELECT
      c_custkey,
      COUNT(o_orderkey) c_count
    FROM
      customer
    LEFT OUTER JOIN orders
      ON
        c_custkey = o_custkey
        AND o_comment NOT LIKE '%unusual%packages%'
    GROUP BY
      c_custkey
  ) AS c_orders
GROUP BY
  c_count
ORDER BY
  custdist DESC,
  c_count DESC;
