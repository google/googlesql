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

FROM
  customer,
  orders,
  lineitem,
  (
    FROM lineitem
    |> AGGREGATE sum(l_quantity) AS sum_quantity
       GROUP BY l_orderkey AS selected_l_orderkey
    |> WHERE sum_quantity > 230
  )
|> WHERE
    o_orderkey = selected_l_orderkey
    AND c_custkey = o_custkey
    AND o_orderkey = l_orderkey
|> AGGREGATE sum(l_quantity)
   GROUP BY
    c_name,
    c_custkey,
    o_orderkey,
    o_orderdate,
    o_totalprice
|> ORDER BY
    o_totalprice DESC,
    o_orderdate
|> LIMIT 100;
