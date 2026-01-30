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
import static com.google.googlesql.TypeTestBase.getDescriptorPoolWithTypeProtoAndTypeKind;
import static org.junit.Assert.fail;

import com.google.common.collect.ImmutableList;
import com.google.common.collect.ImmutableMap;
import com.google.protobuf.ByteString;
import com.google.googlesql.GoogleSQLType.TypeKind;
import com.google.googlesql.GoogleSQLType.TypeProto;

import java.util.Map;
import org.junit.Test;
import org.junit.runner.RunWith;
import org.junit.runners.JUnit4;

@RunWith(JUnit4.class)

public final class PreparedQueryTest {

  private static final SimpleType stringType = TypeFactory.createSimpleType(TypeKind.TYPE_STRING);
  private static final SimpleType int32Type = TypeFactory.createSimpleType(TypeKind.TYPE_INT32);

  @Test
  public void testPrepareWithoutCatalog_sqlStringWithoutParameters() {
    AnalyzerOptions options = new AnalyzerOptions();
    try (PreparedQuery query =
        PreparedQuery.builder()
            .setSql("SELECT \"apple\" AS fruit")
            .setAnalyzerOptions(options)
            .prepare()) {
      assertThat(query.getReferencedParameters()).isEmpty();
      assertThat(query.getColumns()).hasSize(1);

      SimpleColumn col0 = query.getColumns().get(0);
      assertThat(col0.getName()).isEqualTo("fruit");
      assertThat(col0.getType().isString()).isTrue();
    }
  }

  @Test
  public void testPrepareWithoutCatalog_sqlStringWithParameters() {
    AnalyzerOptions options = new AnalyzerOptions();
    options.addQueryParameter("a", stringType);
    try (PreparedQuery query =
        PreparedQuery.builder()
            .setSql("SELECT @a AS fruit")
            .setAnalyzerOptions(options)
            .prepare()) {
      assertThat(query.getReferencedParameters()).containsExactly("a");
      assertThat(query.getColumns()).hasSize(1);

      SimpleColumn col0 = query.getColumns().get(0);
      assertThat(col0.getName()).isEqualTo("fruit");
      assertThat(col0.getType()).isEqualTo(stringType);
    }
  }

  @Test
  public void testPrepareWithRegisteredCatalog_sqlStringWithoutParameters() {
    // Register catalog
    SimpleTable table = new SimpleTable("table");
    table.addSimpleColumn("col_str", stringType);
    SimpleCatalog catalog = new SimpleCatalog("catalog");
    catalog.addSimpleTable(table);

    AnalyzerOptions options = new AnalyzerOptions();

    try (SimpleCatalog.AutoUnregister unregisterCatalog = catalog.register();
        PreparedQuery query =
            PreparedQuery.builder()
                .setSql("SELECT col_str FROM table")
                .setAnalyzerOptions(options)
                .setCatalog(catalog)
                .prepare()) {
      assertThat(query.getReferencedParameters()).isEmpty();
      assertThat(query.getColumns()).hasSize(1);

      SimpleColumn col0 = query.getColumns().get(0);
      assertThat(col0.getName()).isEqualTo("col_str");
      assertThat(col0.getType()).isEqualTo(stringType);
    }
  }

  @Test
  public void testPrepareWithRegisteredCatalog_sqlStringWithParameters() {
    // Register catalog
    SimpleTable table = new SimpleTable("table");
    table.addSimpleColumn("col_str", stringType);
    SimpleCatalog catalog = new SimpleCatalog("catalog");
    catalog.addSimpleTable(table);

    AnalyzerOptions options = new AnalyzerOptions();
    options.addQueryParameter("a", int32Type);

    try (SimpleCatalog.AutoUnregister unregisterCatalog = catalog.register();
        PreparedQuery query =
            PreparedQuery.builder()
                .setSql("SELECT col_str, @a AS col_int32 FROM table")
                .setAnalyzerOptions(options)
                .setCatalog(catalog)
                .prepare()) {
      assertThat(query.getReferencedParameters()).containsExactly("a");
      assertThat(query.getColumns()).hasSize(2);

      SimpleColumn col0 = query.getColumns().get(0);
      assertThat(col0.getName()).isEqualTo("col_str");
      assertThat(col0.getType()).isEqualTo(stringType);

      SimpleColumn col1 = query.getColumns().get(1);
      assertThat(col1.getName()).isEqualTo("col_int32");
      assertThat(col1.getType()).isEqualTo(int32Type);
    }
  }

  @Test
  public void testPrepareWithNonRegisteredCatalog_sqlStringWithoutParameters() {
    // Define the catalog but don't register
    SimpleTable table = new SimpleTable("table");
    table.addSimpleColumn("col_str", stringType);
    SimpleCatalog catalog = new SimpleCatalog("catalog");
    catalog.addSimpleTable(table);

    ImmutableMap<String, TableContent> tablesContents =
        ImmutableMap.of(
            "table",
            TableContent.create(
                ImmutableList.of(
                    ImmutableList.of(Value.createStringValue("string1")),
                    ImmutableList.of(Value.createStringValue("string2")))));

    AnalyzerOptions options = new AnalyzerOptions();

    try (PreparedQuery query =
        PreparedQuery.builder()
            .setSql("SELECT col_str FROM table")
            .setAnalyzerOptions(options)
            .setCatalog(catalog)
            .setTablesContents(tablesContents)
            .prepare()) {
      assertThat(query.getReferencedParameters()).isEmpty();
      assertThat(query.getColumns()).hasSize(1);

      SimpleColumn col0 = query.getColumns().get(0);
      assertThat(col0.getName()).isEqualTo("col_str");
      assertThat(col0.getType()).isEqualTo(stringType);
    }
  }

  @Test
  public void testPrepareWithNonRegisteredCatalog_sqlStringWithParameters() {
    // Define the catalog but don't register
    SimpleTable table = new SimpleTable("table");
    table.addSimpleColumn("col_str", stringType);
    SimpleCatalog catalog = new SimpleCatalog("catalog");
    catalog.addSimpleTable(table);

    ImmutableMap<String, TableContent> tablesContents =
        ImmutableMap.of(
            "table",
            TableContent.create(
                ImmutableList.of(
                    ImmutableList.of(Value.createStringValue("string1")),
                    ImmutableList.of(Value.createStringValue("string2")))));

    AnalyzerOptions options = new AnalyzerOptions();
    options.addQueryParameter("a", int32Type);

    try (PreparedQuery query =
        PreparedQuery.builder()
            .setSql("SELECT col_str, @a AS col_int32 FROM table")
            .setAnalyzerOptions(options)
            .setCatalog(catalog)
            .setTablesContents(tablesContents)
            .prepare()) {
      assertThat(query.getReferencedParameters()).containsExactly("a");
      assertThat(query.getColumns()).hasSize(2);

      SimpleColumn col0 = query.getColumns().get(0);
      assertThat(col0.getName()).isEqualTo("col_str");
      assertThat(col0.getType()).isEqualTo(stringType);

      SimpleColumn col1 = query.getColumns().get(1);
      assertThat(col1.getName()).isEqualTo("col_int32");
      assertThat(col1.getType()).isEqualTo(int32Type);
    }
  }

  @Test
  public void testPrepareThrows() {
    String sql = "SELECT \"apple\" AS fruit";
    AnalyzerOptions options = new AnalyzerOptions();

    // throws IllegalStateException if sql is not set
    try {
      PreparedQuery.builder().setAnalyzerOptions(options).prepare();
      fail();
    } catch (IllegalStateException expected) {
    }

    // throws IllegalStateException if options is not set
    try {
      PreparedQuery.builder().setSql(sql).prepare();
      fail();
    } catch (IllegalStateException expected) {
    }

    // throws IllegalStateException if tables contents are provided and catalog is not
    try {
      PreparedQuery.builder()
          .setSql(sql)
          .setAnalyzerOptions(options)
          .setTablesContents(ImmutableMap.of())
          .prepare();
      fail();
    } catch (IllegalStateException expected) {
    }

    SimpleCatalog registeredCatalog = new SimpleCatalog("RegisteredCatalog");
    try (SimpleCatalog.AutoUnregister unregisterCatalog = registeredCatalog.register()) {
      // throws IllegalStateException if the given catalog is registered and new tables contents are
      // being provided
      try {
        PreparedQuery.builder()
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
    PreparedQuery.Builder pqBuilder =
        PreparedQuery.builder()
            .setSql("SELECT \"apple\" AS fruit")
            .setAnalyzerOptions(new AnalyzerOptions());

    try (PreparedQuery query1 = pqBuilder.prepare();
        PreparedQuery query2 = pqBuilder.prepare()) {
      assertThat(query1).isNotSameInstanceAs(query2);
    }
  }

  @Test
  public void testPrepareAndClose() {
    PreparedQuery query =
        PreparedQuery.builder()
            .setSql("SELECT \"apple\" AS fruit")
            .setAnalyzerOptions(new AnalyzerOptions())
            .prepare();
    query.close();
  }

  @Test
  public void testPrepareAndCloseTwice() {
    PreparedQuery query =
        PreparedQuery.builder()
            .setSql("SELECT \"apple\" AS fruit")
            .setAnalyzerOptions(new AnalyzerOptions())
            .prepare();

    query.close();
    query.close();
  }

  @Test
  public void testExecuteWithoutCatalog_sqlStringWithoutParameters() {
    AnalyzerOptions options = new AnalyzerOptions();

    try (PreparedQuery query =
        PreparedQuery.builder()
            .setSql("SELECT \"apple\" AS fruit")
            .setAnalyzerOptions(options)
            .prepare()) {
      TableContent results = query.execute(ImmutableMap.of());
      assertThat(results.getTableData()).hasSize(1);

      ImmutableList<Value> row0 = results.getTableData().get(0);
      assertThat(row0).hasSize(1);
      assertThat(row0.get(0).getType()).isEqualTo(stringType);
      assertThat(row0.get(0).getStringValue()).isEqualTo("apple");
    }
  }

  @Test
  public void testExecuteWithoutCatalog_sqlStringWithParameters() {
    AnalyzerOptions options = new AnalyzerOptions();
    options.addQueryParameter("a", stringType);

    try (PreparedQuery query =
        PreparedQuery.builder()
            .setSql("SELECT @a AS fruit")
            .setAnalyzerOptions(options)
            .prepare()) {
      ImmutableMap<String, Value> parameters =
          ImmutableMap.of("a", Value.createStringValue("apple"));
      TableContent results = query.execute(parameters);
      assertThat(results.getTableData()).hasSize(1);

      ImmutableList<Value> row0 = results.getTableData().get(0);
      assertThat(row0).hasSize(1);
      assertThat(row0.get(0).getType()).isEqualTo(stringType);
      assertThat(row0.get(0).getStringValue()).isEqualTo("apple");
    }
  }

  @Test
  public void testExecuteWithRegisteredCatalog_sqlStringWithoutParameters() {
    // Register catalog
    SimpleTable table = new SimpleTable("table");
    table.addSimpleColumn("col_str", stringType);
    SimpleCatalog catalog = new SimpleCatalog("catalog");
    catalog.addSimpleTable(table);

    ImmutableMap<String, TableContent> tablesContents =
        ImmutableMap.of(
            "table",
            TableContent.create(
                ImmutableList.of(
                    ImmutableList.of(Value.createStringValue("string1")),
                    ImmutableList.of(Value.createStringValue("string2")))));

    AnalyzerOptions options = new AnalyzerOptions();

    try (SimpleCatalog.AutoUnregister unregisterCatalog = catalog.register(tablesContents);
        PreparedQuery query =
            PreparedQuery.builder()
                .setSql("SELECT col_str FROM table")
                .setAnalyzerOptions(options)
                .setCatalog(catalog)
                .prepare()) {
      TableContent results = query.execute(ImmutableMap.of());
      assertThat(results.getTableData()).hasSize(2);

      ImmutableList<Value> row0 = results.getTableData().get(0);
      assertThat(row0).hasSize(1);
      assertThat(row0.get(0).getType()).isEqualTo(stringType);
      assertThat(row0.get(0).getStringValue()).isEqualTo("string1");

      ImmutableList<Value> row1 = results.getTableData().get(1);
      assertThat(row1).hasSize(1);
      assertThat(row1.get(0).getType()).isEqualTo(stringType);
      assertThat(row1.get(0).getStringValue()).isEqualTo("string2");
    }
  }

  @Test
  public void testExecuteWithRegisteredCatalog_sqlStringWithParameters() {
    // Register catalog
    SimpleTable table = new SimpleTable("table");
    table.addSimpleColumn("col_str", stringType);
    SimpleCatalog catalog = new SimpleCatalog("catalog");
    catalog.addSimpleTable(table);

    ImmutableMap<String, TableContent> tablesContents =
        ImmutableMap.of(
            "table",
            TableContent.create(
                ImmutableList.of(
                    ImmutableList.of(Value.createStringValue("string1")),
                    ImmutableList.of(Value.createStringValue("string2")))));

    AnalyzerOptions options = new AnalyzerOptions();
    options.addQueryParameter("a", int32Type);

    try (SimpleCatalog.AutoUnregister unregisterCatalog = catalog.register(tablesContents);
        PreparedQuery query =
            PreparedQuery.builder()
                .setSql("SELECT col_str, @a AS col_int32 FROM table")
                .setAnalyzerOptions(options)
                .setCatalog(catalog)
                .prepare()) {
      ImmutableMap<String, Value> parameters = ImmutableMap.of("a", Value.createInt32Value(123));
      TableContent results = query.execute(parameters);
      assertThat(results.getTableData()).hasSize(2);

      ImmutableList<Value> row0 = results.getTableData().get(0);
      assertThat(row0).hasSize(2);
      assertThat(row0.get(0).getType()).isEqualTo(stringType);
      assertThat(row0.get(0).getStringValue()).isEqualTo("string1");
      assertThat(row0.get(1).getType()).isEqualTo(int32Type);
      assertThat(row0.get(1).getInt32Value()).isEqualTo(123);

      ImmutableList<Value> row1 = results.getTableData().get(1);
      assertThat(row1).hasSize(2);
      assertThat(row1.get(0).getType()).isEqualTo(stringType);
      assertThat(row1.get(0).getStringValue()).isEqualTo("string2");
      assertThat(row1.get(1).getType()).isEqualTo(int32Type);
      assertThat(row1.get(1).getInt32Value()).isEqualTo(123);
    }
  }

  @Test
  public void testExecuteWithNonRegisteredCatalog_sqlStringWithoutParameters() {
    // Define the catalog but don't register
    SimpleTable table = new SimpleTable("table");
    table.addSimpleColumn("col_str", stringType);
    SimpleCatalog catalog = new SimpleCatalog("catalog");
    catalog.addSimpleTable(table);

    ImmutableMap<String, TableContent> tablesContents =
        ImmutableMap.of(
            "table",
            TableContent.create(
                ImmutableList.of(
                    ImmutableList.of(Value.createStringValue("string1")),
                    ImmutableList.of(Value.createStringValue("string2")))));

    AnalyzerOptions options = new AnalyzerOptions();

    try (PreparedQuery query =
        PreparedQuery.builder()
            .setSql("SELECT col_str FROM table")
            .setAnalyzerOptions(options)
            .setCatalog(catalog)
            .setTablesContents(tablesContents)
            .prepare()) {
      TableContent results = query.execute(ImmutableMap.of());
      assertThat(results.getTableData()).hasSize(2);

      ImmutableList<Value> row0 = results.getTableData().get(0);
      assertThat(row0).hasSize(1);
      assertThat(row0.get(0).getType()).isEqualTo(stringType);
      assertThat(row0.get(0).getStringValue()).isEqualTo("string1");

      ImmutableList<Value> row1 = results.getTableData().get(1);
      assertThat(row1).hasSize(1);
      assertThat(row1.get(0).getType()).isEqualTo(stringType);
      assertThat(row1.get(0).getStringValue()).isEqualTo("string2");
    }
  }

  @Test
  public void testExecuteWithNonRegisteredCatalog_sqlStringWithParameters() {
    // Define the catalog but don't register
    SimpleTable table = new SimpleTable("table");
    table.addSimpleColumn("col_str", stringType);
    SimpleCatalog catalog = new SimpleCatalog("catalog");
    catalog.addSimpleTable(table);

    ImmutableMap<String, TableContent> tablesContents =
        ImmutableMap.of(
            "table",
            TableContent.create(
                ImmutableList.of(
                    ImmutableList.of(Value.createStringValue("string1")),
                    ImmutableList.of(Value.createStringValue("string2")))));

    AnalyzerOptions options = new AnalyzerOptions();
    options.addQueryParameter("a", int32Type);

    try (PreparedQuery query =
        PreparedQuery.builder()
            .setSql("SELECT col_str, @a AS col_int32 FROM table")
            .setAnalyzerOptions(options)
            .setCatalog(catalog)
            .setTablesContents(tablesContents)
            .prepare()) {
      ImmutableMap<String, Value> parameters = ImmutableMap.of("a", Value.createInt32Value(123));
      TableContent results = query.execute(parameters);
      assertThat(results.getTableData()).hasSize(2);

      ImmutableList<Value> row0 = results.getTableData().get(0);
      assertThat(row0).hasSize(2);
      assertThat(row0.get(0).getType()).isEqualTo(stringType);
      assertThat(row0.get(0).getStringValue()).isEqualTo("string1");
      assertThat(row0.get(1).getType()).isEqualTo(int32Type);
      assertThat(row0.get(1).getInt32Value()).isEqualTo(123);

      ImmutableList<Value> row1 = results.getTableData().get(1);
      assertThat(row1).hasSize(2);
      assertThat(row1.get(0).getType()).isEqualTo(stringType);
      assertThat(row1.get(0).getStringValue()).isEqualTo("string2");
      assertThat(row1.get(1).getType()).isEqualTo(int32Type);
      assertThat(row1.get(1).getInt32Value()).isEqualTo(123);
    }
  }

  @Test
  public void testExecuteWithProtoTypes() {
    // Define the catalog but don't register
    TypeFactory factory = TypeFactory.uniqueNames();
    ProtoType protoType = factory.createProtoType(TypeProto.class);
    EnumType enumProtoType =
        factory.createEnumType(
            getDescriptorPoolWithTypeProtoAndTypeKind().findEnumTypeByName("googlesql.TypeKind"));

    SimpleTable table = new SimpleTable("table");
    table.addSimpleColumn("col_str", stringType);
    table.addSimpleColumn("col_proto", protoType);
    table.addSimpleColumn("col_enum_proto", enumProtoType);

    SimpleCatalog catalog = new SimpleCatalog("catalog");
    catalog.addSimpleTable(table);

    ByteString protoValue1 =
        TypeProto.newBuilder().setTypeKind(TypeKind.TYPE_DATE).build().toByteString();
    ByteString protoValue2 =
        TypeProto.newBuilder().setTypeKind(TypeKind.TYPE_STRING).build().toByteString();
    ImmutableMap<String, TableContent> tablesContents =
        ImmutableMap.of(
            "table",
            TableContent.create(
                ImmutableList.of(
                    ImmutableList.of(
                        Value.createStringValue("string1"),
                        Value.createProtoValue(protoType, protoValue1),
                        Value.createEnumValue(enumProtoType, TypeKind.TYPE_DATE_VALUE)),
                    ImmutableList.of(
                        Value.createStringValue("string2"),
                        Value.createProtoValue(protoType, protoValue2),
                        Value.createEnumValue(enumProtoType, TypeKind.TYPE_STRING_VALUE)))));

    AnalyzerOptions options = new AnalyzerOptions();

    try (PreparedQuery query =
        PreparedQuery.builder()
            .setSql("SELECT * FROM table")
            .setAnalyzerOptions(options)
            .setCatalog(catalog)
            .setTablesContents(tablesContents)
            .prepare()) {
      TableContent results = query.execute(ImmutableMap.of());
      assertThat(results.getTableData()).hasSize(2);

      ImmutableList<Value> row0 = results.getTableData().get(0);
      assertThat(row0).hasSize(3);
      assertThat(row0.get(0).getType()).isEqualTo(stringType);
      assertThat(row0.get(0).getStringValue()).isEqualTo("string1");
      assertThat(row0.get(1).getType()).isEqualTo(protoType);
      assertThat(row0.get(1).getProtoValue()).isEqualTo(protoValue1);
      assertThat(row0.get(2).getType()).isEqualTo(enumProtoType);
      assertThat(row0.get(2).getEnumName()).isEqualTo(TypeKind.TYPE_DATE.name());

      ImmutableList<Value> row1 = results.getTableData().get(1);
      assertThat(row1).hasSize(3);
      assertThat(row1.get(0).getType()).isEqualTo(stringType);
      assertThat(row1.get(0).getStringValue()).isEqualTo("string2");
      assertThat(row1.get(1).getType()).isEqualTo(protoType);
      assertThat(row1.get(1).getProtoValue()).isEqualTo(protoValue2);
      assertThat(row1.get(2).getType()).isEqualTo(enumProtoType);
      assertThat(row1.get(2).getEnumName()).isEqualTo(TypeKind.TYPE_STRING.name());
    }
  }

  @Test
  public void testPrepareThenExecuteMissingParameter() {
    AnalyzerOptions options = new AnalyzerOptions();
    options.addQueryParameter("a", stringType);

    try (PreparedQuery query =
        PreparedQuery.builder()
            .setSql("SELECT @a AS fruit")
            .setAnalyzerOptions(options)
            .prepare()) {
      query.execute(ImmutableMap.of());
      fail();
    } catch (SqlException exception) {
      assertWithMessage(exception.getMessage())
          .that(exception.getMessage().contains("Incomplete query parameters a"))
          .isTrue();
    }
  }

  @Test
  public void testPrepareThenExecuteWrongParameterType() {
    AnalyzerOptions options = new AnalyzerOptions();
    options.addQueryParameter("a", stringType);

    try (PreparedQuery query =
        PreparedQuery.builder()
            .setSql("SELECT @a AS fruit")
            .setAnalyzerOptions(options)
            .prepare()) {
      ImmutableMap<String, Value> parameters = ImmutableMap.of("a", Value.createInt32Value(123));
      query.execute(parameters);
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
    AnalyzerOptions options = new AnalyzerOptions();
    PreparedQuery.Builder pqBuilder =
        PreparedQuery.builder().setSql("SELECT \"apple\" AS fruit").setAnalyzerOptions(options);

    try (PreparedQuery query = pqBuilder.prepare()) {
      // throws NPE if parameters is null
      try {
        query.execute((Map<String, Value>) null);
        fail();
      } catch (NullPointerException expected) {
      }
    }

    // throws IllegalStateException if the query was closed when execute was called
    PreparedQuery query = pqBuilder.prepare();
    query.close();
    try {
      query.execute(ImmutableMap.of());
      fail();
    } catch (IllegalStateException expected) {
    }
  }
}
