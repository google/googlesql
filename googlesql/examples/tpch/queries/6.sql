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
  sum(l_extendedprice * l_discount) AS revenue
FROM
  lineitem
WHERE
  l_shipdate >= date '1994-01-01'
  AND l_shipdate < date_add(date '1994-01-01', INTERVAL 1 year)
  AND l_discount BETWEEN 0.08 - 0.01 AND 0.08 + 0.01
  AND l_quantity < 25;
