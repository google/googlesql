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
  100.00
  * sum(
    CASE
      WHEN p_type LIKE 'PROMO%'
        THEN l_extendedprice * (1 - l_discount)
      ELSE 0
      END)
  / sum(l_extendedprice * (1 - l_discount)) AS promo_revenue
FROM
  lineitem,
  part
WHERE
  l_partkey = p_partkey
  AND l_shipdate >= date '1994-03-01'
  AND l_shipdate < date_add(date '1994-03-01', INTERVAL 1 month);
