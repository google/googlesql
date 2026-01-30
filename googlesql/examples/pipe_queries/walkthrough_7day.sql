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

-- These queries are intended as part of a tutorial on working with
-- pipe syntax. They walk through construcing a query incrementally
-- using pipe syntax, explaining the thinking as the query evolves.

-- These can be executed in `execute_query` with the `tpch` catalog.
-- See the README for more information.

-- Examples with TVFs depend on the `REWRITE_INLINE_SQL_TVFS` rewriter, which is in-development and
-- may return incorrect results. This rewriter must be enabled manually when invoking execute_query,
-- for example: `--enabled_ast_rewrites=ALL_MINUS_DEV,+INLINE_SQL_TVFS`
-- TODO: b/202167428 - Remove this note once this rewriter is out of development.

-- Goal: Compute a metric for 7-day-active users from an event log.
--
-- These queries have been adapted to run against the Orders table since
-- that is available in the TPC-H catalog available in `execute_query`.
-- They can be adapted for any table with a date and user ID.

-- This is the final query, as a standalone query:

-- Compute 7-day-active distinct users, with sliding windows.
FROM Orders
|> RENAME o_orderdate AS date, o_custkey AS user_id
|> EXTEND max(date) OVER () AS max_date
|> JOIN UNNEST(generate_array(0, 7)) diff_days
|> EXTEND date_add(date, INTERVAL diff_days DAY) active_date
|> WHERE active_date <= max_date
|> AGGREGATE COUNT(DISTINCT user_id) active_7d_users
   GROUP BY active_date AS date DESC
|> LIMIT 20;

-- Now I'll show the steps, and my thought process as I built that up
-- incrementally, using pipe syntax.

-- First, look at the table I'm starting with.
DESCRIBE Orders;

-- And see what the data looks like.
FROM Orders
|> LIMIT 10;

-- I'll rename the columns so the queries can work with `date` and
-- `user_id` columns.
FROM Orders
|> RENAME o_orderdate AS date, o_custkey AS user_id
|> LIMIT 10;

-- Let's take a look at it, grouped by date, and count distinct users per day.

FROM Orders
|> RENAME o_orderdate AS date, o_custkey AS user_id
|> AGGREGATE
     COUNT(*) cnt,
     COUNT(DISTINCT user_id) cnt_distinct
   GROUP BY date
|> LIMIT 10;

-- It's out of order - add DESC order on the date.

FROM Orders
|> RENAME o_orderdate AS date, o_custkey AS user_id
|> AGGREGATE
     COUNT(*) cnt,
     COUNT(DISTINCT user_id) cnt_distinct
   GROUP BY date DESC
|> LIMIT 20;

-- I see I have rows up to 1998-08-02, and just a few distinct users per day.
-- That's a short list, so I can take a look at them with ARRAY_AGG.

FROM Orders
|> RENAME o_orderdate AS date, o_custkey AS user_id
|> AGGREGATE
     COUNT(*) cnt,
     COUNT(DISTINCT user_id) cnt_distinct,
     array_agg(DISTINCT user_id ORDER BY user_id) user_ids
   GROUP BY date DESC
|> LIMIT 20;

-- Now, how do I do 7-day active counts?

-- The easiest way is to add a DATE_TRUNC expression in my GROUP BY,
-- while will bucket rows by WEEK.

FROM Orders
|> RENAME o_orderdate AS date, o_custkey AS user_id
|> AGGREGATE
     COUNT(*) cnt,
     COUNT(DISTINCT user_id) cnt_distinct,
     array_agg(DISTINCT user_id ORDER BY user_id) user_ids
   GROUP BY DATE_TRUNC(date, WEEK) DESC
|> LIMIT 10;

-- That's nice, but I really wanted sliding windows in my dashboard.
-- So I'll need a new approach.

-- For every day a user is active, that user also counts as active for the next 6
-- days.
-- So it should work if I copy the rows forward with those extra dates.

-- I can add a join to an array to get a count of days to add.

FROM Orders
|> RENAME o_orderdate AS date, o_custkey AS user_id
|> JOIN UNNEST([0, 1, 2, 3, 4, 5, 6]) diff_days
|> LIMIT 10;

-- Using GENERATE_ARRAY is a nicer way.

FROM Orders
|> RENAME o_orderdate AS date, o_custkey AS user_id
|> JOIN UNNEST(generate_array(0, 6)) diff_days
|> LIMIT 10;

-- And then I can compute the new date by adding `diff_days`.

FROM Orders
|> RENAME o_orderdate AS date, o_custkey AS user_id
|> JOIN UNNEST(generate_array(0, 6)) diff_days
|> EXTEND date_add(date, INTERVAL diff_days DAY) active_date
|> LIMIT 10;

-- Now, let's group by `active_date` and count distinct users.

FROM Orders
|> RENAME o_orderdate AS date, o_custkey AS user_id
|> JOIN UNNEST(generate_array(0, 6)) diff_days
|> EXTEND date_add(date, INTERVAL diff_days DAY) active_date
|> AGGREGATE COUNT(DISTINCT user_id) active_7d_users
   GROUP BY active_date AS date DESC
|> LIMIT 10;

-- That worked. One thing I notice is that now I have dates going into the
-- future, because I added rows with dates past the max date in the input.
-- So I want to filter those out.

-- I can compute the max date in the input with a window function.
-- Try that with EXTEND, and run that prefix of the query to check.

FROM Orders
|> RENAME o_orderdate AS date, o_custkey AS user_id
|> EXTEND max(date) OVER () AS max_date
/*
|> JOIN UNNEST(generate_array(0, 6)) diff_days
|> EXTEND date_add(date, INTERVAL diff_days day) active_date
|> AGGREGATE COUNT(DISTINCT user_id) active_7d_users
   GROUP BY active_date AS date DESC
*/
|> LIMIT 10;

-- Then I'll add a WHERE clause to filter generated rows past that date.

-- And we're done!  That's 7-day active users, with sliding windows.

FROM Orders
|> RENAME o_orderdate AS date, o_custkey AS user_id
|> EXTEND max(date) OVER () AS max_date
|> JOIN UNNEST(generate_array(0, 6)) diff_days
|> EXTEND date_add(date, INTERVAL diff_days DAY) active_date
|> WHERE active_date <= max_date
|> AGGREGATE COUNT(DISTINCT user_id) active_7d_users
   GROUP BY active_date AS date DESC
|> LIMIT 10;

--
--
--
-- NEXT STEP: Now what if want to reuse that multiple times?
--

-- That's a lot of code to copy-paste, or figure out again.
-- Maybe I can make a reusable TVF to do it.

-- Let's grab the middle part and make a TVF from it.

CREATE TEMP TABLE FUNCTION ExtendDates1(input ANY TABLE)
AS
FROM input
|> EXTEND max(date) OVER () AS max_date
|> JOIN UNNEST(generate_array(0, 6)) diff_days
|> EXTEND date_add(date, INTERVAL diff_days DAY) active_date
|> WHERE active_date <= max_date;

FROM Orders
|> RENAME o_orderdate AS date, o_custkey AS user_id
|> CALL ExtendDates1()
|> LIMIT 10;

-- That worked, but the output has extra columns `max_date`, `diff_days`.
-- So add a DROP to remove those extra columns.
-- Also make num_days an argument.

CREATE TEMP TABLE FUNCTION ExtendDates2(input ANY TABLE, num_days INT64)
AS
FROM input
|> EXTEND max(date) OVER () AS max_date
|> JOIN UNNEST(generate_array(0, num_days - 1)) diff_days
|> EXTEND date_add(date, INTERVAL diff_days DAY) active_date
|> WHERE active_date <= max_date
-- Temporarily use SELECT * EXCEPT instead because of a bug with TVF inlining.
-- |> DROP max_date, diff_days;
|> SELECT * EXCEPT (max_date, diff_days);

FROM Orders
|> RENAME o_orderdate AS date, o_custkey AS user_id
|> CALL ExtendDates2(7)
|> LIMIT 10;

-- That looks better!
-- But it's probably nicer if the modified date comes out as `date`, and the original
-- is called `original_date`, so my following query can still write `GROUP BY date`.

-- So I'll edit this to copy `date` to `original_date` with EXTEND, and
-- change the later EXTEND to a SET that overwrites `date`.

CREATE TEMP TABLE FUNCTION ExtendDates3(input ANY TABLE, num_days INT64)
AS
FROM input
|> EXTEND date AS original_date
|> EXTEND max(date) OVER () AS max_date
|> JOIN UNNEST(generate_array(0, num_days - 1)) diff_days
|> SET date = date_add(date, INTERVAL diff_days DAY)
|> WHERE date <= max_date
-- Temporarily use SELECT * EXCEPT instead because of a bug with TVF inlining.
-- |> DROP max_date, diff_days;
|> SELECT * EXCEPT (max_date, diff_days);

FROM Orders
|> RENAME o_orderdate AS date, o_custkey AS user_id
|> CALL ExtendDates3(7)
|> LIMIT 10;

-- That looks like the reusable function I want.
-- I can call this on anything that has a `date` column.

-- Then I can add my aggregation on the end, to compute my 7-day active users.

CREATE TEMP TABLE FUNCTION ExtendDates(input ANY TABLE, num_days INT64)
AS
FROM input
|> EXTEND date AS original_date
|> EXTEND max(date) OVER () AS max_date
|> JOIN UNNEST(generate_array(0, num_days - 1)) diff_days
|> SET date = date_add(date, INTERVAL diff_days DAY)
|> WHERE date <= max_date
|> DROP max_date, diff_days;

-- Look at that, the date replication is nicely encapsulated in a reusable
-- function!  And calling it as a pipe operator fits cleanly into the query flow.

FROM Orders
|> RENAME o_orderdate AS date, o_custkey AS user_id
|> CALL ExtendDates(7)
|> AGGREGATE COUNT(DISTINCT user_id) active_7d_users
   GROUP BY date DESC
|> LIMIT 20;
