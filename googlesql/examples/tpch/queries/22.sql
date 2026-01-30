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
  cntrycode,
  COUNT(*) AS numcust,
  sum(c_acctbal) AS totacctbal
FROM
  (
    SELECT
      substr(c_phone, 1, 2) AS cntrycode,
      c_acctbal
    FROM customer
    WHERE
      substr(c_phone, 1, 2) IN ('10', '19', '14', '22', '23', '31', '13')
      AND c_acctbal > (
        SELECT avg(c_acctbal)
        FROM customer
        WHERE
          c_acctbal > 0.00
          AND substr(c_phone, 1, 2) IN ('10', '19', '14', '22', '23', '31', '13')
      )
      AND NOT EXISTS(
        SELECT *
        FROM orders
        WHERE o_custkey = c_custkey
      )
  ) AS custsale
GROUP BY cntrycode
ORDER BY cntrycode;
