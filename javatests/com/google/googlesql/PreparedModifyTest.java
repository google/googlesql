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
import static com.google.common.truth.Truth.assertWithMessage;
import static org.junit.Assert.fail;

import com.google.common.collect.ImmutableList;
import com.google.common.collect.ImmutableMap;
import com.google.common.collect.ImmutableSet;
import com.google.googlesql.EvaluatorTableModifyResponse.Row.Operation;
import com.google.googlesql.GoogleSQLResolvedNodeKind.ResolvedNodeKind;
import com.google.googlesql.GoogleSQLType.TypeKind;

import java.util.Map;
import org.junit.Test;
import org.junit.runner.RunWith;
import org.junit.runners.JUnit4;

@RunWith(JUnit4.class)

public final class PreparedModifyTest {

  private static final SimpleType stringType = TypeFactory.createSimpleType(TypeKind.TYPE_STRING);
  private static final SimpleType int32Type = TypeFactory.createSimpleType(TypeKind.TYPE_INT32);

  @Test
  public void testPrepareWithRegisteredCatalog_sqlStringWithoutParameters() {
    // Register catalog
    SimpleTable table = createTable("table");
    SimpleCatalog catalog = createCatalog("catalog", table);

    try (SimpleCatalog.AutoUnregister unregisterCatalog = catalog.register()) {
      AnalyzerOptions options = createAnalyzerOptions();
      PreparedModify.Builder pmBuilder =
          PreparedModify.builder().setAnalyzerOptions(options).setCatalog(catalog);

      try (PreparedModify modify = pmBuilder.setSql("DELETE FROM table WHERE true").prepare()) {
        assertThat(modify.getReferencedParameters()).isEmpty();
      }

      try (PreparedModify modify =
          pmBuilder
              .setSql("INSERT INTO table (col_str, col_int32) VALUES ('string', 3)")
              .prepare()) {
        assertThat(modify.getReferencedParameters()).isEmpty();
      }

      try (PreparedModify modify =
          pmBuilder.setSql("UPDATE table SET col_str = 'string' WHERE col_int32 = 123").prepare()) {
        assertThat(modify.getReferencedParameters()).isEmpty();
      }
    }
  }

  @Test
  public void testPrepareWithRegisteredCatalog_sqlStringWithParameters() {
    // Register catalog
    SimpleTable table = createTable("table");
    SimpleCatalog catalog = createCatalog("catalog", table);

    try (SimpleCatalog.AutoUnregister unregisterCatalog = catalog.register()) {
      AnalyzerOptions options = createAnalyzerOptions();
      options.addQueryParameter("a", stringType);
      PreparedModify.Builder pmBuilder =
          PreparedModify.builder().setAnalyzerOptions(options).setCatalog(catalog);

      try (PreparedModify modify =
          pmBuilder.setSql("DELETE FROM table WHERE col_str = @a").prepare()) {
        assertThat(modify.getReferencedParameters()).containsExactly("a");
      }

      try (PreparedModify modify =
          pmBuilder.setSql("INSERT INTO table (col_str, col_int32) VALUES (@a, 3)").prepare()) {
        assertThat(modify.getReferencedParameters()).containsExactly("a");
      }

      try (PreparedModify modify =
          pmBuilder.setSql("UPDATE table SET col_str = @a WHERE col_int32 = 123").prepare()) {
        assertThat(modify.getReferencedParameters()).containsExactly("a");
      }
    }
  }

  @Test
  public void testPrepareWithNonRegisteredCatalog_sqlStringWithoutParameters() {
    // Define the catalog but don't register
    SimpleTable table = createTable("table");
    SimpleCatalog catalog = createCatalog("catalog", table);
    ImmutableMap<String, TableContent> tablesContents =
        ImmutableMap.of("table", createTableContent());
    AnalyzerOptions options = createAnalyzerOptions();

    PreparedModify.Builder pmBuilder =
        PreparedModify.builder()
            .setAnalyzerOptions(options)
            .setCatalog(catalog)
            .setTablesContents(tablesContents);

    try (PreparedModify modify = pmBuilder.setSql("DELETE FROM table WHERE true").prepare()) {
      assertThat(modify.getReferencedParameters()).isEmpty();
    }

    try (PreparedModify modify =
        pmBuilder.setSql("INSERT INTO table (col_str, col_int32) VALUES ('string', 3)").prepare()) {
      assertThat(modify.getReferencedParameters()).isEmpty();
    }

    try (PreparedModify modify =
        pmBuilder.setSql("UPDATE table SET col_str = 'string' WHERE col_int32 = 123").prepare()) {
      assertThat(modify.getReferencedParameters()).isEmpty();
    }
  }

  @Test
  public void testPrepareWithNonRegisteredCatalog_sqlStringWithParameters() {
    // Define the catalog but don't register
    SimpleTable table = createTable("table");
    SimpleCatalog catalog = createCatalog("catalog", table);
    ImmutableMap<String, TableContent> tablesContents =
        ImmutableMap.of("table", createTableContent());

    AnalyzerOptions options = createAnalyzerOptions();
    options.addQueryParameter("a", stringType);

    PreparedModify.Builder pmBuilder =
        PreparedModify.builder()
            .setAnalyzerOptions(options)
            .setCatalog(catalog)
            .setTablesContents(tablesContents);

    try (PreparedModify modify =
        pmBuilder.setSql("DELETE FROM table WHERE col_str = @a").prepare()) {
      assertThat(modify.getReferencedParameters()).containsExactly("a");
    }

    try (PreparedModify modify =
        pmBuilder.setSql("INSERT INTO table (col_str, col_int32) VALUES (@a, 3)").prepare()) {
      assertThat(modify.getReferencedParameters()).containsExactly("a");
    }

    try (PreparedModify modify =
        pmBuilder.setSql("UPDATE table SET col_str = @a WHERE col_int32 = 123").prepare()) {
      assertThat(modify.getReferencedParameters()).containsExactly("a");
    }
  }

  @Test
  public void testPrepareThrows() {
    String sql = "DELETE FROM table WHERE true";
    AnalyzerOptions options = createAnalyzerOptions();
    SimpleCatalog catalog = new SimpleCatalog("catalog");

    // throws IllegalStateException if sql is not set
    try {
      PreparedModify.builder().setAnalyzerOptions(options).setCatalog(catalog).prepare();
      fail();
    } catch (IllegalStateException expected) {
    }

    // throws IllegalStateException if options is not set
    try {
      PreparedModify.builder().setSql(sql).setCatalog(catalog).prepare();
      fail();
    } catch (IllegalStateException expected) {
    }

    // throws IllegalStateException if catalog is not set
    try {
      PreparedModify.builder().setSql(sql).setAnalyzerOptions(options).prepare();
      fail();
    } catch (IllegalStateException expected) {
    }

    SimpleCatalog registeredCatalog = new SimpleCatalog("RegisteredCatalog");
    try (SimpleCatalog.AutoUnregister unregisterCatalog = registeredCatalog.register()) {
      // throws IllegalStateException if the given catalog is registered and new tables contents are
      // being provided
      try {
        PreparedModify.builder()
            .setSql(sql)
            .setAnalyzerOptions(options)
            .setCatalog(registeredCatalog)
            .setTablesContents(ImmutableMap.of())
            .prepare();
        fail();
      } catch (IllegalStateException expected) {
      }
    }
  }

  @Test
  public void testPrepareAfterPrepare() {
    AnalyzerOptions options = createAnalyzerOptions();
    SimpleTable table = createTable("table");
    SimpleCatalog catalog = createCatalog("catalog", table);

    PreparedModify.Builder pmBuilder =
        PreparedModify.builder()
            .setSql("DELETE FROM table WHERE true")
            .setAnalyzerOptions(options)
            .setCatalog(catalog);

    try (PreparedModify modify1 = pmBuilder.prepare();
        PreparedModify modify2 = pmBuilder.prepare(); ) {
      assertThat(modify1).isNotSameInstanceAs(modify2);
    }
  }

  @Test
  public void testPrepareAndClose() {
    AnalyzerOptions options = createAnalyzerOptions();
    SimpleTable table = createTable("table");
    SimpleCatalog catalog = createCatalog("catalog", table);

    PreparedModify modify =
        PreparedModify.builder()
            .setSql("DELETE FROM table WHERE true")
            .setAnalyzerOptions(options)
            .setCatalog(catalog)
            .prepare();

    modify.close();
  }

  @Test
  public void testPrepareAndCloseTwice() {
    AnalyzerOptions options = createAnalyzerOptions();
    SimpleTable table = createTable("table");
    SimpleCatalog catalog = createCatalog("catalog", table);

    PreparedModify modify =
        PreparedModify.builder()
            .setSql("DELETE FROM table WHERE true")
            .setAnalyzerOptions(options)
            .setCatalog(catalog)
            .prepare();

    modify.close();
    modify.close();
  }

  @Test
  public void testExecuteWithNonRegisteredCatalog_sqlStringWithoutParameters() {
    // Define the catalog but don't register
    SimpleTable table = createTable("table");
    SimpleCatalog catalog = createCatalog("catalog", table);
    ImmutableMap<String, TableContent> tablesContents =
        ImmutableMap.of("table", createTableContent());

    AnalyzerOptions options = createAnalyzerOptions();

    PreparedModify.Builder pmBuilder =
        PreparedModify.builder()
            .setAnalyzerOptions(options)
            .setCatalog(catalog)
            .setTablesContents(tablesContents);

    try (PreparedModify modify = pmBuilder.setSql("DELETE FROM table WHERE true").prepare()) {
      EvaluatorTableModifyResponse results = modify.execute(ImmutableMap.of());
      assertThat(results.getTableName()).isEqualTo("table");
      ImmutableList<EvaluatorTableModifyResponse.Row> rows = results.getContent();
      assertThat(rows).hasSize(3);

      EvaluatorTableModifyResponse.Row row0 = rows.get(0);
      assertThat(row0.getOperation()).isEqualTo(Operation.DELETE);
      assertThat(row0.getContent()).isEmpty();
      assertThat(row0.getOriginalPrimaryKeysValues()).isEmpty();

      EvaluatorTableModifyResponse.Row row1 = rows.get(1);
      assertThat(row1.getOperation()).isEqualTo(Operation.DELETE);
      assertThat(row1.getContent()).isEmpty();
      assertThat(row1.getOriginalPrimaryKeysValues()).isEmpty();

      EvaluatorTableModifyResponse.Row row2 = rows.get(2);
      assertThat(row2.getOperation()).isEqualTo(Operation.DELETE);
      assertThat(row2.getContent()).isEmpty();
      assertThat(row2.getOriginalPrimaryKeysValues()).isEmpty();
    }

    try (PreparedModify modify =
        pmBuilder
            .setSql("INSERT INTO table (col_str, col_int32) VALUES ('new_string', 3)")
            .prepare()) {
      EvaluatorTableModifyResponse results = modify.execute(ImmutableMap.of());
      assertThat(results.getTableName()).isEqualTo("table");
      ImmutableList<EvaluatorTableModifyResponse.Row> rows = results.getContent();
      assertThat(rows).hasSize(1);

      EvaluatorTableModifyResponse.Row row0 = rows.get(0);
      assertThat(row0.getOperation()).isEqualTo(Operation.INSERT);
      assertThat(row0.getContent()).hasSize(2);
      assertThat(row0.getContent().get(0).getType()).isEqualTo(stringType);
      assertThat(row0.getContent().get(0).getStringValue()).isEqualTo("new_string");
      assertThat(row0.getContent().get(1).getType()).isEqualTo(int32Type);
      assertThat(row0.getContent().get(1).getInt32Value()).isEqualTo(3);
      assertThat(row0.getOriginalPrimaryKeysValues()).isEmpty();
    }

    try (PreparedModify modify =
        pmBuilder.setSql("UPDATE table SET col_str = 'new_string' WHERE col_int32 = 1").prepare()) {
      EvaluatorTableModifyResponse results = modify.execute(ImmutableMap.of());
      assertThat(results.getTableName()).isEqualTo("table");
      ImmutableList<EvaluatorTableModifyResponse.Row> rows = results.getContent();
      assertThat(rows).hasSize(1);

      EvaluatorTableModifyResponse.Row row0 = rows.get(0);
      assertThat(row0.getOperation()).isEqualTo(Operation.UPDATE);
      assertThat(row0.getContent()).hasSize(2);
      assertThat(row0.getContent().get(0).getType()).isEqualTo(stringType);
      assertThat(row0.getContent().get(0).getStringValue()).isEqualTo("new_string");
      assertThat(row0.getContent().get(1).getType()).isEqualTo(int32Type);
      assertThat(row0.getContent().get(1).getInt32Value()).isEqualTo(1);
      assertThat(row0.getOriginalPrimaryKeysValues()).isEmpty();
    }
  }

  @Test
  public void testExecuteWithNonRegisteredCatalog_sqlStringWithParameters() {
    // Define the catalog but don't register
    SimpleTable table = createTable("table");
    SimpleCatalog catalog = createCatalog("catalog", table);
    ImmutableMap<String, TableContent> tablesContents =
        ImmutableMap.of("table", createTableContent());

    AnalyzerOptions options = createAnalyzerOptions();
    options.addQueryParameter("a", stringType);

    PreparedModify.Builder pmBuilder =
        PreparedModify.builder()
            .setAnalyzerOptions(options)
            .setCatalog(catalog)
            .setTablesContents(tablesContents);

    try (PreparedModify modify =
        pmBuilder.setSql("DELETE FROM table WHERE col_str = @a").prepare()) {
      ImmutableMap<String, Value> parameters =
          ImmutableMap.of("a", Value.createStringValue("string1"));
      EvaluatorTableModifyResponse results = modify.execute(parameters);

      assertThat(results.getTableName()).isEqualTo("table");
      ImmutableList<EvaluatorTableModifyResponse.Row> rows = results.getContent();
      assertThat(rows).hasSize(1);

      EvaluatorTableModifyResponse.Row row0 = rows.get(0);
      assertThat(row0.getOperation()).isEqualTo(Operation.DELETE);
      assertThat(row0.getContent()).isEmpty();
      assertThat(row0.getOriginalPrimaryKeysValues()).isEmpty();
    }

    try (PreparedModify modify =
        pmBuilder.setSql("INSERT INTO table (col_str, col_int32) VALUES (@a, 3)").prepare()) {
      ImmutableMap<String, Value> parameters =
          ImmutableMap.of("a", Value.createStringValue("new_string"));
      EvaluatorTableModifyResponse results = modify.execute(parameters);

      assertThat(results.getTableName()).isEqualTo("table");
      ImmutableList<EvaluatorTableModifyResponse.Row> rows = results.getContent();
      assertThat(rows).hasSize(1);

      EvaluatorTableModifyResponse.Row row0 = rows.get(0);
      assertThat(row0.getOperation()).isEqualTo(Operation.INSERT);
      assertThat(row0.getContent()).hasSize(2);
      assertThat(row0.getContent().get(0).getType()).isEqualTo(stringType);
      assertThat(row0.getContent().get(0).getStringValue()).isEqualTo("new_string");
      assertThat(row0.getContent().get(1).getType()).isEqualTo(int32Type);
      assertThat(row0.getContent().get(1).getInt32Value()).isEqualTo(3);
      assertThat(row0.getOriginalPrimaryKeysValues()).isEmpty();
    }

    try (PreparedModify modify =
        pmBuilder.setSql("UPDATE table SET col_str = @a WHERE col_int32 = 1").prepare()) {
      ImmutableMap<String, Value> parameters =
          ImmutableMap.of("a", Value.createStringValue("new_string"));
      EvaluatorTableModifyResponse results = modify.execute(parameters);

      assertThat(results.getTableName()).isEqualTo("table");
      ImmutableList<EvaluatorTableModifyResponse.Row> rows = results.getContent();
      assertThat(rows).hasSize(1);

      EvaluatorTableModifyResponse.Row row0 = rows.get(0);
      assertThat(row0.getOperation()).isEqualTo(Operation.UPDATE);
      assertThat(row0.getContent()).hasSize(2);
      assertThat(row0.getContent().get(0).getType()).isEqualTo(stringType);
      assertThat(row0.getContent().get(0).getStringValue()).isEqualTo("new_string");
      assertThat(row0.getContent().get(1).getType()).isEqualTo(int32Type);
      assertThat(row0.getContent().get(1).getInt32Value()).isEqualTo(1);
      assertThat(row0.getOriginalPrimaryKeysValues()).isEmpty();
    }
  }

  @Test
  public void testExecuteWithRegisteredCatalog_sqlStringWithoutParameters() {
    // Register catalog
    SimpleTable table = createTable("table");
    SimpleCatalog catalog = createCatalog("catalog", table);
    ImmutableMap<String, TableContent> tablesContents =
        ImmutableMap.of("table", createTableContent());

    try (SimpleCatalog.AutoUnregister unregisterCatalog = catalog.register(tablesContents)) {
      AnalyzerOptions options = createAnalyzerOptions();

      PreparedModify.Builder pmBuilder =
          PreparedModify.builder().setAnalyzerOptions(options).setCatalog(catalog);

      try (PreparedModify modify = pmBuilder.setSql("DELETE FROM table WHERE true").prepare()) {
        EvaluatorTableModifyResponse results = modify.execute(ImmutableMap.of());

        assertThat(results.getTableName()).isEqualTo("table");
        ImmutableList<EvaluatorTableModifyResponse.Row> rows = results.getContent();
        assertThat(rows).hasSize(3);

        EvaluatorTableModifyResponse.Row row0 = rows.get(0);
        assertThat(row0.getOperation()).isEqualTo(Operation.DELETE);
        assertThat(row0.getContent()).isEmpty();
        assertThat(row0.getOriginalPrimaryKeysValues()).isEmpty();

        EvaluatorTableModifyResponse.Row row1 = rows.get(1);
        assertThat(row1.getOperation()).isEqualTo(Operation.DELETE);
        assertThat(row1.getContent()).isEmpty();
        assertThat(row1.getOriginalPrimaryKeysValues()).isEmpty();

        EvaluatorTableModifyResponse.Row row2 = rows.get(2);
        assertThat(row2.getOperation()).isEqualTo(Operation.DELETE);
        assertThat(row2.getContent()).isEmpty();
        assertThat(row2.getOriginalPrimaryKeysValues()).isEmpty();
      }

      try (PreparedModify modify =
          pmBuilder
              .setSql("INSERT INTO table (col_str, col_int32) VALUES ('new_string', 3)")
              .prepare()) {
        EvaluatorTableModifyResponse results = modify.execute(ImmutableMap.of());

        assertThat(results.getTableName()).isEqualTo("table");
        ImmutableList<EvaluatorTableModifyResponse.Row> rows = results.getContent();
        assertThat(rows).hasSize(1);

        EvaluatorTableModifyResponse.Row row0 = rows.get(0);
        assertThat(row0.getOperation()).isEqualTo(Operation.INSERT);
        assertThat(row0.getContent()).hasSize(2);
        assertThat(row0.getContent().get(0).getType()).isEqualTo(stringType);
        assertThat(row0.getContent().get(0).getStringValue()).isEqualTo("new_string");
        assertThat(row0.getContent().get(1).getType()).isEqualTo(int32Type);
        assertThat(row0.getContent().get(1).getInt32Value()).isEqualTo(3);
        assertThat(row0.getOriginalPrimaryKeysValues()).isEmpty();
      }

      try (PreparedModify modify =
          pmBuilder
              .setSql("UPDATE table SET col_str = 'new_string' WHERE col_int32 = 1")
              .prepare()) {
        EvaluatorTableModifyResponse results = modify.execute(ImmutableMap.of());

        assertThat(results.getTableName()).isEqualTo("table");
        ImmutableList<EvaluatorTableModifyResponse.Row> rows = results.getContent();
        assertThat(rows).hasSize(1);

        EvaluatorTableModifyResponse.Row row0 = rows.get(0);
        assertThat(row0.getOperation()).isEqualTo(Operation.UPDATE);
        assertThat(row0.getContent()).hasSize(2);
        assertThat(row0.getContent().get(0).getType()).isEqualTo(stringType);
        assertThat(row0.getContent().get(0).getStringValue()).isEqualTo("new_string");
        assertThat(row0.getContent().get(1).getType()).isEqualTo(int32Type);
        assertThat(row0.getContent().get(1).getInt32Value()).isEqualTo(1);
        assertThat(row0.getOriginalPrimaryKeysValues()).isEmpty();
      }
    }
  }

  @Test
  public void testExecuteWithRegisteredCatalog_sqlStringWithParameters() {
    // Register catalog
    SimpleTable table = createTable("table");
    SimpleCatalog catalog = createCatalog("catalog", table);
    ImmutableMap<String, TableContent> tablesContents =
        ImmutableMap.of("table", createTableContent());

    try (SimpleCatalog.AutoUnregister unregisterCatalog = catalog.register(tablesContents)) {
      AnalyzerOptions options = createAnalyzerOptions();
      options.addQueryParameter("a", stringType);

      PreparedModify.Builder pmBuilder =
          PreparedModify.builder().setAnalyzerOptions(options).setCatalog(catalog);

      try (PreparedModify modify =
          pmBuilder.setSql("DELETE FROM table WHERE col_str = @a").prepare()) {
        ImmutableMap<String, Value> parameters =
            ImmutableMap.of("a", Value.createStringValue("string1"));
        EvaluatorTableModifyResponse results = modify.execute(parameters);

        assertThat(results.getTableName()).isEqualTo("table");
        ImmutableList<EvaluatorTableModifyResponse.Row> rows = results.getContent();
        assertThat(rows).hasSize(1);

        EvaluatorTableModifyResponse.Row row0 = rows.get(0);
        assertThat(row0.getOperation()).isEqualTo(Operation.DELETE);
        assertThat(row0.getContent()).isEmpty();
        assertThat(row0.getOriginalPrimaryKeysValues()).isEmpty();
      }

      try (PreparedModify modify =
          pmBuilder.setSql("INSERT INTO table (col_str, col_int32) VALUES (@a, 3)").prepare()) {
        ImmutableMap<String, Value> parameters =
            ImmutableMap.of("a", Value.createStringValue("new_string"));
        EvaluatorTableModifyResponse results = modify.execute(parameters);

        assertThat(results.getTableName()).isEqualTo("table");
        ImmutableList<EvaluatorTableModifyResponse.Row> rows = results.getContent();
        assertThat(rows).hasSize(1);

        EvaluatorTableModifyResponse.Row row0 = rows.get(0);
        assertThat(row0.getOperation()).isEqualTo(Operation.INSERT);
        assertThat(row0.getContent()).hasSize(2);
        assertThat(row0.getContent().get(0).getType()).isEqualTo(stringType);
        assertThat(row0.getContent().get(0).getStringValue()).isEqualTo("new_string");
        assertThat(row0.getContent().get(1).getType()).isEqualTo(int32Type);
        assertThat(row0.getContent().get(1).getInt32Value()).isEqualTo(3);
        assertThat(row0.getOriginalPrimaryKeysValues()).isEmpty();
      }

      try (PreparedModify modify =
          pmBuilder.setSql("UPDATE table SET col_str = @a WHERE col_int32 = 1").prepare()) {
        ImmutableMap<String, Value> parameters =
            ImmutableMap.of("a", Value.createStringValue("new_string"));
        EvaluatorTableModifyResponse results = modify.execute(parameters);

        assertThat(results.getTableName()).isEqualTo("table");
        ImmutableList<EvaluatorTableModifyResponse.Row> rows = results.getContent();
        assertThat(rows).hasSize(1);

        EvaluatorTableModifyResponse.Row row0 = rows.get(0);
        assertThat(row0.getOperation()).isEqualTo(Operation.UPDATE);
        assertThat(row0.getContent()).hasSize(2);
        assertThat(row0.getContent().get(0).getType()).isEqualTo(stringType);
        assertThat(row0.getContent().get(0).getStringValue()).isEqualTo("new_string");
        assertThat(row0.getContent().get(1).getType()).isEqualTo(int32Type);
        assertThat(row0.getContent().get(1).getInt32Value()).isEqualTo(1);
        assertThat(row0.getOriginalPrimaryKeysValues()).isEmpty();
      }
    }
  }

  @Test
  public void testPrepareThenExecuteMissingParameter() {
    // Define the catalog but don't register
    SimpleTable table = createTable("table");
    SimpleCatalog catalog = createCatalog("catalog", table);
    ImmutableMap<String, TableContent> tablesContents =
        ImmutableMap.of("table", createTableContent());

    AnalyzerOptions options = createAnalyzerOptions();
    options.addQueryParameter("a", stringType);

    PreparedModify.Builder pmBuilder =
        PreparedModify.builder()
            .setSql("DELETE FROM table WHERE col_str = @a")
            .setAnalyzerOptions(options)
            .setCatalog(catalog)
            .setTablesContents(tablesContents);

    try (PreparedModify modify = pmBuilder.prepare()) {
      modify.execute(ImmutableMap.of());
      fail();
    } catch (SqlException exception) {
      assertWithMessage(exception.getMessage())
          .that(exception.getMessage().contains("Incomplete sql expression parameters a"))
          .isTrue();
    }
  }

  @Test
  public void testPrepareThenExecuteWrongParameterType() {
    // Define the catalog but don't register
    SimpleTable table = createTable("table");
    SimpleCatalog catalog = createCatalog("catalog", table);
    ImmutableMap<String, TableContent> tablesContents =
        ImmutableMap.of("table", createTableContent());

    AnalyzerOptions options = createAnalyzerOptions();
    options.addQueryParameter("a", stringType);

    PreparedModify.Builder pmBuilder =
        PreparedModify.builder()
            .setSql("DELETE FROM table WHERE col_str = @a")
            .setAnalyzerOptions(options)
            .setCatalog(catalog)
            .setTablesContents(tablesContents);

    try (PreparedModify modify = pmBuilder.prepare()) {
      ImmutableMap<String, Value> parameters = ImmutableMap.of("a", Value.createInt32Value(123));
      modify.execute(parameters);
      fail();
    } catch (SqlException exception) {
      assertWithMessage(exception.getMessage())
          .that(
              exception.getMessage().contains("Expected query parameter 'a' to be of type STRING"))
          .isTrue();
    }
  }

  @Test
  public void testExecuteThrows() {
    SimpleTable table = createTable("table");
    SimpleCatalog catalog = createCatalog("catalog", table);
    AnalyzerOptions options = createAnalyzerOptions();

    PreparedModify.Builder pmBuilder =
        PreparedModify.builder()
            .setSql("DELETE FROM table WHERE true")
            .setAnalyzerOptions(options)
            .setCatalog(catalog);

    try (PreparedModify modify = pmBuilder.prepare()) {
      // throws NPE if parameters is null
      try {
        modify.execute((Map<String, Value>) null);
        fail();
      } catch (NullPointerException expected) {
      }
    }

    // throws IllegalStateException if the prepared modify was closed when execute was called
    PreparedModify modify = pmBuilder.prepare();
    modify.close();
    try {
      modify.execute(ImmutableMap.of());
      fail();
    } catch (IllegalStateException expected) {
    }
  }

  private static SimpleTable createTable(String name) {
    SimpleTable table = new SimpleTable(name);
    table.addSimpleColumn("col_str", stringType);
    table.addSimpleColumn("col_int32", int32Type);

    return table;
  }

  private static SimpleCatalog createCatalog(String name, SimpleTable table) {
    SimpleCatalog catalog = new SimpleCatalog(name);
    catalog.addSimpleTable(table);
    catalog.addGoogleSQLFunctions(new GoogleSQLBuiltinFunctionOptions());

    return catalog;
  }

  private static AnalyzerOptions createAnalyzerOptions() {
    AnalyzerOptions analyzerOptions = new AnalyzerOptions();
    analyzerOptions
        .getLanguageOptions()
        .setSupportedStatementKinds(
            ImmutableSet.of(
                ResolvedNodeKind.RESOLVED_DELETE_STMT,
                ResolvedNodeKind.RESOLVED_INSERT_STMT,
                ResolvedNodeKind.RESOLVED_UPDATE_STMT));

    return analyzerOptions;
  }

  private static TableContent createTableContent() {
    return TableContent.create(
        ImmutableList.of(
            ImmutableList.of(Value.createStringValue("string1"), Value.createInt32Value(1)),
            ImmutableList.of(Value.createStringValue("string2"), Value.createInt32Value(2)),
            ImmutableList.of(Value.createStringValue("string3"), Value.createInt32Value(3))));
  }
}
