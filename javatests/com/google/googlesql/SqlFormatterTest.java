/*
 * Copyright 2019 Google LLC
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 *
 */

package com.google.googlesql;

import static com.google.common.truth.Truth.assertThat;
import static org.junit.Assert.fail;

import com.google.common.collect.ImmutableList;
import com.google.googlesql.FormatterOptions.FormatterOptionsProto;
import com.google.googlesql.FormatterOptions.FormatterRangeProto;


import org.junit.Test;
import org.junit.runner.RunWith;
import org.junit.runners.JUnit4;

@RunWith(JUnit4.class)

public class SqlFormatterTest {

  @Test
  public void testFormatSql_query() throws Exception {
    String formatted =
        new SqlFormatter().formatSql("SELECT foo, bar from some_table where something limit 1;");
    assertThat(formatted)
        .isEqualTo(
            ""
                + "SELECT\n"
                + "  foo,\n"
                + "  bar\n"
                + "FROM\n"
                + "  some_table\n"
                + "WHERE\n"
                + "  something\n"
                + "LIMIT 1;");
  }

  @Test
  public void testFormatSql_createFunction() throws Exception {
    String formatted =
        new SqlFormatter().formatSql("CREATE FUNCTION foo(INT64 a) RETURNS INT64 AS ( a );");
    assertThat(formatted).isEqualTo("CREATE FUNCTION foo(INT64 a)\nRETURNS INT64 AS (\n  a\n);");
  }

  @Test
  public void testFormatSql_invalidInput_throws() throws Exception {
    try {
      new SqlFormatter().formatSql("SEL 1;");
      fail("Formatter should've thrown");
    } catch (SqlException e) {
      assertThat(e).hasMessageThat().contains("Syntax error");
    }
  }

  @Test
  public void lenientFormatter_query() throws Exception {
    // We don't want to assert on the exact format here. As long as it returns something it's fine.

    String formatted = new SqlFormatter().lenientFormatSql("SELECT 1");

    assertThat(formatted).isEqualTo("SELECT 1\n");
  }

  @Test
  public void lenientFormatter_queryWithOptions() throws Exception {
    String formattedWithDefaultOptions =
        new SqlFormatter()
            .lenientFormatSql("SELECT columns, that, fit, default_line_length FROM Table;");
    assertThat(formattedWithDefaultOptions)
        .isEqualTo("SELECT columns, that, fit, default_line_length FROM Table;\n");

    FormatterOptionsProto options =
        FormatterOptionsProto.newBuilder().setLineLengthLimit(30).build();
    String formattedWithCustomOptions =
        new SqlFormatter()
            .lenientFormatSql(
                "SELECT columns, that, fit, default_line_length FROM Table;", options);

    assertThat(formattedWithCustomOptions)
        .isEqualTo("SELECT\n  columns,\n  that,\n  fit,\n  default_line_length\nFROM Table;\n");
  }

  @Test
  public void lenientFormatter_queryWithRanges() throws Exception {
    String formatted =
        new SqlFormatter()
            .lenientFormatSql(
                "select 1; select 2; select 3;",
                ImmutableList.of(
                    FormatterRangeProto.newBuilder().setStart(0).setEnd(1).build(),
                    FormatterRangeProto.newBuilder().setStart(22).setEnd(26).build()),
                FormatterOptionsProto.newBuilder().setExpandFormatRanges(true).build());

    assertThat(formatted).isEqualTo("SELECT 1;\nselect 2; SELECT 3;\n");
  }

  @Test
  public void lenientFormatter_interval() throws Exception {
    String formatted = new SqlFormatter().lenientFormatSql("SELECT CAST('1:2:3' AS Interval)");

    assertThat(formatted).isEqualTo("SELECT CAST('1:2:3' AS INTERVAL)\n");
  }
}
