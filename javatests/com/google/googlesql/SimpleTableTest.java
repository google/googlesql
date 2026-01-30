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
import com.google.common.testing.SerializableTester;
import com.google.protobuf.TextFormat;
import com.google.protobuf.TextFormat.ParseException;
import com.google.googlesql.GoogleSQLType.TypeKind;
import com.google.googlesql.GoogleSQLType.TypeProto;
import com.google.googlesql.SimpleTableProtos.SimpleTableProto;
import com.google.googlesqltest.BadTestSchemaProto.InvalidSQLTable1;
import com.google.googlesqltest.BadTestSchemaProto.InvalidSQLTable2;
import com.google.googlesqltest.BadTestSchemaProto.InvalidSQLTable3;
import com.google.googlesqltest.TestSchemaProto.TestSQLTable;


import java.util.ArrayList;
import java.util.List;
import org.junit.Test;
import org.junit.runner.RunWith;
import org.junit.runners.JUnit4;

@RunWith(JUnit4.class)

public class SimpleTableTest {

  private static List<SimpleColumn> createColumns(String tableName) {
    List<SimpleColumn> columns = new ArrayList<>();
    TypeFactory factory = TypeFactory.nonUniqueNames();
    SimpleType type = TypeFactory.createSimpleType(TypeKind.TYPE_BOOL);
    SimpleColumn column1 = new SimpleColumn(tableName, "Column1", type);
    ArrayType array =
        TypeFactory.createArrayType(TypeFactory.createSimpleType(TypeKind.TYPE_INT32));
    SimpleColumn column2 =
        new SimpleColumn(
            tableName,
            "column2",
            array,
            /* isPseudoColumn = */ true,
            /* isWritableColumn = */ true);
    GoogleSQLDescriptorPool pool = new GoogleSQLDescriptorPool();
    TypeProto proto = factory.createProtoType(TypeProto.class).serialize();
    pool.importFileDescriptorSet(proto.getFileDescriptorSet(0));
    EnumType enumType = factory.createEnumType(pool.findEnumTypeByName("googlesql.TypeKind"));
    SimpleColumn column3 =
        new SimpleColumn(
            tableName,
            "coLumn3",
            enumType,
            /* isPseudoColumn = */ false,
            /* isWritableColumn = */ true);
    ProtoType protoType = factory.createProtoType(TypeProto.class);
    SimpleColumn column4 = new SimpleColumn(tableName, "colum_4", protoType);
    ArrayList<StructType.StructField> fields = new ArrayList<>();
    fields.add(new StructType.StructField("enum", enumType));
    fields.add(new StructType.StructField("proto", protoType));
    StructType struct = TypeFactory.createStructType(fields);
    SimpleColumn column5 = new SimpleColumn(tableName, "_column5", struct);

    columns.add(column1);
    columns.add(column2);
    columns.add(column3);
    columns.add(column4);
    columns.add(column5);
    return columns;
  }

  @Test
  public void testGets() {
    SimpleType type = TypeFactory.createSimpleType(TypeKind.TYPE_BOOL);
    SimpleTable table1 = new SimpleTable("Table1", createColumns("Table1"));
    SimpleTable table2 = new SimpleTable("TABLE2", createColumns("TABLE2"));
    SimpleTable table3 = new SimpleTable("Table1", createColumns("Table1"));
    SimpleTable table4 = new SimpleTable("Table_3", type);
    SimpleTable table5 = new SimpleTable("_table4");

    assertThat(table1.getName()).isEqualTo("Table1");
    assertThat(table2.getName()).isEqualTo("TABLE2");
    assertThat(table3.getName()).isEqualTo("Table1");
    assertThat(table4.getName()).isEqualTo("Table_3");
    assertThat(table5.getName()).isEqualTo("_table4");

    assertThat(table1.getFullName()).isEqualTo("Table1");
    assertThat(table2.getFullName()).isEqualTo("TABLE2");
    assertThat(table3.getFullName()).isEqualTo("Table1");
    assertThat(table4.getFullName()).isEqualTo("Table_3");
    assertThat(table5.getFullName()).isEqualTo("_table4");

    assertThat(table1.getColumnCount()).isEqualTo(5);
    assertThat(table2.getColumnCount()).isEqualTo(5);
    assertThat(table3.getColumnCount()).isEqualTo(5);
    assertThat(table4.getColumnCount()).isEqualTo(1);
    assertThat(table5.getColumnCount()).isEqualTo(0);

    assertThat(table1.getId() + 1 == table2.getId()).isTrue();
    assertThat(table2.getId() + 1 == table3.getId()).isTrue();
    assertThat(table3.getId() + 1 == table4.getId()).isTrue();
    assertThat(table4.getId() + 1 == table5.getId()).isTrue();

    SimpleColumn column1 = new SimpleColumn("Table1", "Column1", type);
    SimpleColumn column2 = new SimpleColumn("TABLE2", "Column1", type);
    SimpleColumn column3 = new SimpleColumn("Table_3", "value", type);
    FileDescriptorSetsBuilder descriptor = new FileDescriptorSetsBuilder();
    assertThat(table1.getColumn(0).serialize(descriptor).equals(column1.serialize(descriptor)))
        .isTrue();
    assertThat(table2.getColumn(0).serialize(descriptor).equals(column2.serialize(descriptor)))
        .isTrue();
    assertThat(table4.getColumn(0).serialize(descriptor).equals(column3.serialize(descriptor)))
        .isTrue();

    assertThat(table1.isValueTable()).isFalse();
    assertThat(table2.isValueTable()).isFalse();
    assertThat(table3.isValueTable()).isFalse();
    assertThat(table4.isValueTable()).isFalse();
    assertThat(table5.isValueTable()).isFalse();
    table1.setIsValueTable(true);
    assertThat(table1.isValueTable()).isTrue();
  }

  @Test
  public void testAddColumn() {
    SimpleTable table1 = new SimpleTable("t1", createColumns("t1"));
    SimpleType type = TypeFactory.createSimpleType(TypeKind.TYPE_INT32);
    SimpleColumn column1 = new SimpleColumn("t1", "cadd", type);
    FileDescriptorSetsBuilder descriptor = new FileDescriptorSetsBuilder();
    table1.addSimpleColumn("cadd", type);
    table1.addSimpleColumn("Add", type);
    assertThat(table1.getColumnCount()).isEqualTo(7);
    assertThat(table1.getColumn(5).serialize(descriptor).equals(column1.serialize(descriptor)))
        .isTrue();
    try {
      table1.addSimpleColumn("cadd", type);
      fail();
    } catch (IllegalArgumentException e) {
      assertThat(e).hasMessageThat().isEqualTo("Duplicate column in t1: cadd");
    }
    try {
      table1.addSimpleColumn("cADD", type);
      fail();
    } catch (IllegalArgumentException e) {
      assertThat(e).hasMessageThat().isEqualTo("Duplicate column in t1: cADD");
    }
    try {
      table1.addSimpleColumn("add", type);
      fail();
    } catch (IllegalArgumentException e) {
      assertThat(e).hasMessageThat().isEqualTo("Duplicate column in t1: add");
    }
    try {
      table1.addSimpleColumn("aDD", type);
      fail();
    } catch (IllegalArgumentException e) {
      assertThat(e).hasMessageThat().isEqualTo("Duplicate column in t1: aDD");
    }

    try {
      table1.addSimpleColumn("", type);
      fail();
    } catch (IllegalArgumentException e) {
      assertThat(e).hasMessageThat().isEqualTo("Empty column names not allowed");
    }

    try {
      table1.addSimpleColumn(null, type);
      fail();
    } catch (IllegalArgumentException e) {
      assertThat(e).hasMessageThat().isEqualTo("Empty column names not allowed");
    }
  }

  @Test
  public void testAnonymousColumn() {
    SimpleType intType = TypeFactory.createSimpleType(TypeKind.TYPE_INT32);

    SimpleTable table = new SimpleTable("Table");
    assertThat(table.allowAnonymousColumnName()).isFalse();
    assertThat(table.getColumnCount()).isEqualTo(0);
    SimpleColumn column = new SimpleColumn("Table", /* name= */ "", intType);
    try {
      table.addSimpleColumn(column);
      fail();
    } catch (IllegalArgumentException expected) {
    }
    assertThat(table.getColumnCount()).isEqualTo(0);
    table.setAllowAnonymousColumnName(true);
    assertThat(table.allowAnonymousColumnName()).isTrue();
    table.addSimpleColumn(column);
    assertThat(table.getColumnCount()).isEqualTo(1);
    assertThat(table.findColumnByName(null)).isNull();
    assertThat(table.findColumnByName("")).isNull();
  }

  @Test
  public void testDuplicateColumnName() {
    SimpleType intType = TypeFactory.createSimpleType(TypeKind.TYPE_INT32);
    SimpleType stringType = TypeFactory.createSimpleType(TypeKind.TYPE_STRING);

    SimpleTable table = new SimpleTable("Table");
    assertThat(table.allowDuplicateColumnNames()).isFalse();
    assertThat(table.getColumnCount()).isEqualTo(0);
    assertThat(table.findColumnByName("col")).isNull();
    SimpleColumn column1 = new SimpleColumn("Table", "col", intType);
    table.addSimpleColumn(column1);
    assertThat(table.getColumnCount()).isEqualTo(1);
    assertThat(table.findColumnByName("col")).isSameInstanceAs(column1);

    SimpleColumn column2 = new SimpleColumn("Table", "col", stringType);
    try {
      table.addSimpleColumn(column2);
      fail();
    } catch (IllegalArgumentException e) {
      assertThat(e).hasMessageThat().isEqualTo("Duplicate column in Table: col");
    }
    assertThat(table.getColumnCount()).isEqualTo(1);
    assertThat(table.findColumnByName("col")).isSameInstanceAs(column1);

    table.setAllowDuplicateColumnNames(true);
    assertThat(table.allowDuplicateColumnNames()).isTrue();
    table.addSimpleColumn(column2);
    assertThat(table.getColumnCount()).isEqualTo(2);
    assertThat(table.findColumnByName("col")).isNull();

    SimpleColumn column3 = new SimpleColumn("Table", "col", stringType);
    table.addSimpleColumn(column3);
    assertThat(table.getColumnCount()).isEqualTo(3);
    assertThat(table.findColumnByName("col")).isNull();
  }

  @Test
  public void testPrimaryKey() {
    SimpleTable table1 = new SimpleTable("t1", createColumns("t1"));
    assertThat(table1.getPrimaryKey()).isEmpty();

    table1.setPrimaryKey(ImmutableList.of(1));
    assertThat(table1.getPrimaryKey().get()).containsExactly(1);

    table1.setPrimaryKey(ImmutableList.of(1, 2));
    assertThat(table1.getPrimaryKey().get()).containsExactly(1, 2).inOrder();

    try {
      table1.setPrimaryKey(ImmutableList.of(1, 5));
      fail();
    } catch (IllegalArgumentException e) {
      assertThat(e).hasMessageThat().isEqualTo("Invalid column index 5 in primary key");
    }
    assertThat(table1.getPrimaryKey().get()).containsExactly(1, 2).inOrder();
  }

  @Test
  public void testRowIdentity() {
    SimpleTable table1 = new SimpleTable("t1", createColumns("t1"));
    assertThat(table1.getRowIdentityColumns()).isEmpty();

    table1.setPrimaryKey(ImmutableList.of(1));
    assertThat(table1.getPrimaryKey().get()).containsExactly(1);
    assertThat(table1.getRowIdentityColumns().get()).containsExactly(1);

    table1.setRowIdentity(ImmutableList.of(0, 1));
    assertThat(table1.getRowIdentityColumns().get()).containsExactly(0, 1).inOrder();
    assertThat(table1.getPrimaryKey().get()).containsExactly(1);

    try {
      table1.setRowIdentity(ImmutableList.of(1, 5));
      fail();
    } catch (IllegalArgumentException e) {
      assertThat(e).hasMessageThat().isEqualTo("Invalid column index 5 in row identity");
    }

    assertThat(table1.getRowIdentityColumns().get()).containsExactly(0, 1).inOrder();
    assertThat(table1.getPrimaryKey().get()).containsExactly(1);
  }

  @Test
  public void testFullName() {
    SimpleTable table1 = new SimpleTable("t1", createColumns("t1"));
    assertThat(table1.getName()).isEqualTo("t1");
    assertThat(table1.getFullName()).isEqualTo("t1");
    table1.setFullName("s1.t1");
    assertThat(table1.getName()).isEqualTo("t1");
    assertThat(table1.getFullName()).isEqualTo("s1.t1");
    table1.setFullName("");
    assertThat(table1.getName()).isEqualTo("t1");
    assertThat(table1.getFullName()).isEqualTo("t1");
    table1.setFullName("s1.t1");
    table1.setFullName("t1");
    assertThat(table1.getName()).isEqualTo("t1");
    assertThat(table1.getFullName()).isEqualTo("t1");
    table1.setFullName("s1.t1");
    table1.setFullName(null);
    assertThat(table1.getName()).isEqualTo("t1");
    assertThat(table1.getFullName()).isEqualTo("t1");
  }

  @Test
  public void testFindColumnByName() {
    SimpleTable table1 = new SimpleTable("t1", createColumns("t1"));
    SimpleType type = TypeFactory.createSimpleType(TypeKind.TYPE_BOOL);
    SimpleColumn column1 = new SimpleColumn("t1", "Column1", type);
    FileDescriptorSetsBuilder descriptor = new FileDescriptorSetsBuilder();

    assertThat(table1.findColumnByName("Column1").serialize(descriptor))
        .isEqualTo(column1.serialize(descriptor));
    assertThat(table1.findColumnByName("column1")).isNotNull();
    assertThat(table1.findColumnByName("Column2")).isNotNull();
    assertThat(table1.findColumnByName("cOLUmn1")).isNotNull();
    assertThat(table1.findColumnByName("noName")).isNull();
  }

  @Test
  public void testSerializationAndDeserialization() throws ParseException {
    TypeFactory factory = TypeFactory.nonUniqueNames();
    SimpleTable table1 = new SimpleTable("t1");
    SimpleColumn column1 =
        new SimpleColumn("t1", "column1", TypeFactory.createSimpleType(TypeKind.TYPE_INT32));
    SimpleColumn column2 =
        new SimpleColumn("t1", "column2", factory.createProtoType(TypeProto.class));
    table1.addSimpleColumn(column1);
    table1.addSimpleColumn(column2);
    table1.setPrimaryKey(ImmutableList.of(0));
    table1.setRowIdentity(ImmutableList.of(0, 1));
    table1.setIsValueTable(true);
    FileDescriptorSetsBuilder descriptor = new FileDescriptorSetsBuilder();

    SimpleTable table2 =
        SimpleTable.deserialize(
            table1.serialize(descriptor), descriptor.getDescriptorPools(), factory);

    assertThat(table2.getFullName()).isEqualTo(table1.getFullName());
    assertThat(table2.getColumnCount()).isEqualTo(table1.getColumnCount());
    assertThat(table2.getName()).isEqualTo(table1.getName());
    for (int i = 0; i < table1.getColumnCount(); i++) {
      assertThat(table1.getColumn(i).serialize(descriptor).toString())
          .isEqualTo(table1.getColumn(i).serialize(descriptor).toString());
    }
    assertThat(table2.getPrimaryKey().get()).isEqualTo(table1.getPrimaryKey().get());
    assertThat(table2.getRowIdentityColumns().get())
        .isEqualTo(table1.getRowIdentityColumns().get());
    assertThat(table1.getId() == table2.getId()).isTrue();
    assertThat(descriptor.getFileDescriptorSetCount()).isEqualTo(1);

    table1.setFullName("s1.t1");
    table2 =
        SimpleTable.deserialize(
            table1.serialize(descriptor), descriptor.getDescriptorPools(), factory);
    assertThat(table2.getFullName()).isEqualTo(table1.getFullName());

    SimpleTableProto proto1 = table1.serialize(descriptor);
    table2 =
        SimpleTable.deserialize(
            proto1.toBuilder().clearFullName().build(), descriptor.getDescriptorPools(), factory);
    assertThat(table2.getFullName()).isEqualTo(table1.getName());

    SimpleTableProto.Builder builder = SimpleTableProto.newBuilder();
    TextFormat.merge(
        "name: \"t1\"\n"
            + "is_value_table: false\n"
            + "column {\n"
            + "  type {\n"
            + "    type_kind: TYPE_INT32\n"
            + "  }\n"
            + "}",
        builder);
    try {
      SimpleTable.deserialize(
          builder.build(), descriptor.getDescriptorPools(), TypeFactory.nonUniqueNames());
      fail();
    } catch (IllegalArgumentException expected) {
    }
  }

  @Test
  public void testSerializable() {
    TypeFactory factory = TypeFactory.nonUniqueNames();
    SimpleTable table1 = new SimpleTable("t1");
    SimpleColumn column1 =
        new SimpleColumn("t1", "column1", TypeFactory.createSimpleType(TypeKind.TYPE_INT32));
    SimpleColumn column2 =
        new SimpleColumn("t1", "column2", factory.createProtoType(TypeProto.class));
    table1.addSimpleColumn(column1);
    table1.addSimpleColumn(column2);
    table1.setIsValueTable(true);
    FileDescriptorSetsBuilder descriptor = new FileDescriptorSetsBuilder();

    SimpleTable table2 = SerializableTester.reserialize(table1);

    assertThat(table2.getFullName()).isEqualTo(table1.getFullName());
    assertThat(table2.getColumnCount()).isEqualTo(table1.getColumnCount());
    assertThat(table2.getName()).isEqualTo(table1.getName());
    for (int i = 0; i < table1.getColumnCount(); i++) {
      assertThat(table1.getColumn(i).serialize(descriptor).toString())
          .isEqualTo(table1.getColumn(i).serialize(descriptor).toString());
    }
    assertThat(table1.getId() == table2.getId()).isTrue();
    assertThat(descriptor.getFileDescriptorSetCount()).isEqualTo(1);
  }

  @Test
  public void testTableFromProto() {
    // Test SQL table
    TypeFactory factory = TypeFactory.nonUniqueNames();
    SimpleTable t0 = SimpleTable.tableFromProto(factory.createProtoType(TestSQLTable.class));
    assertThat(t0.getFullName()).isEqualTo("TestSQLTable");
    assertThat(t0.isValueTable()).isFalse();
    assertThat(t0.getColumnCount()).isEqualTo(2);
    SimpleColumn column1 = t0.getColumn(0);
    assertThat(column1.getName()).isEqualTo("f1");
    assertThat(column1.getFullName()).isEqualTo("TestSQLTable.f1");
    assertThat(column1.getType().debugString()).isEqualTo("INT32");
    SimpleColumn column2 = t0.getColumn(1);
    assertThat(column2.getName()).isEqualTo("f2");
    assertThat(column2.getFullName()).isEqualTo("TestSQLTable.f2");
    assertThat(column2.getType().debugString()).isEqualTo("INT32");

    // Test non GoogleSQL table
    SimpleTable t1 = SimpleTable.tableFromProto(factory.createProtoType(TypeProto.class));
    assertThat(t1.getFullName()).isEqualTo("TypeProto");
    assertThat(t1.isValueTable()).isTrue();
    assertThat(t1.getColumnCount()).isEqualTo(1);
    SimpleColumn column3 = t1.getColumn(0);
    assertThat(column3.getName()).isEqualTo("value");
    assertThat(column3.getFullName()).isEqualTo("TypeProto.value");
    assertThat(column3.getType().debugString()).isEqualTo("PROTO<googlesql.TypeProto>");
    assertThat(TypeProto.getDescriptor().toProto().toString())
        .isEqualTo(column3.getType().asProto().getDescriptor().toProto().toString());

    // Test bad proto
    try {
      SimpleTable.tableFromProto(factory.createProtoType(InvalidSQLTable1.class));
      fail();
    } catch (SqlException expected) {
      assertThat(expected).hasMessageThat().contains("PROTO<googlesql_test.InvalidSQLTable1>");
      assertThat(expected).hasMessageThat().contains("decodes to non-struct type");
    }

    try {
      SimpleTable.tableFromProto(factory.createProtoType(InvalidSQLTable2.class));
      fail();
    } catch (SqlException expected) {
      assertThat(expected).hasMessageThat().contains("has anonymous fields");
    }

    try {
      SimpleTable.tableFromProto(factory.createProtoType(InvalidSQLTable3.class));
      fail();
    } catch (SqlException expected) {
      assertThat(expected).hasMessageThat().contains("Duplicate column");
    }
  }

  @Test
  public void testNextId() {
    SimpleTable t1 = new SimpleTable("t1");
    SimpleTable t2 = new SimpleTable("t2", 100L);
    SimpleTable t3 = new SimpleTable("t3");
    assertThat(t2.getId()).isGreaterThan(t1.getId() + 1);
    assertThat(t3.getId()).isEqualTo(101);
  }
}
