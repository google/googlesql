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
import com.google.protobuf.ByteString;
import com.google.googlesql.GoogleSQLType.TypeKind;
import com.google.googlesql.GoogleSQLType.TypeProto;
import com.google.googlesql.GoogleSQLValue.ValueProto;
import com.google.googlesql.LocalService.TableData;
import com.google.googlesql.LocalService.TableData.Row;

import java.util.List;
import org.junit.Test;
import org.junit.runner.RunWith;
import org.junit.runners.JUnit4;

@RunWith(JUnit4.class)

public final class TableContentTest {

  private static final SimpleType stringType = TypeFactory.createSimpleType(TypeKind.TYPE_STRING);
  private static final SimpleType int32Type = TypeFactory.createSimpleType(TypeKind.TYPE_INT32);

  @Test
  public void testSerializeEmptyContent() {
    TableContent tableContent = TableContent.create(ImmutableList.of());

    com.google.googlesql.LocalService.TableContent serializedContent =
        tableContent.serialize();
    assertThat(serializedContent.hasTableData()).isTrue();
    assertThat(serializedContent.getTableData().getRowList()).isEmpty();
  }

  @Test
  public void testSerializeActualContent() {
    ImmutableList<List<Value>> tableData =
        ImmutableList.of(
            ImmutableList.of(Value.createInt32Value(1), Value.createStringValue("1")),
            ImmutableList.of(Value.createInt32Value(2), Value.createStringValue("2")));
    TableContent tableContent = TableContent.create(tableData);

    com.google.googlesql.LocalService.TableContent serializedContent =
        tableContent.serialize();
    assertThat(serializedContent.hasTableData()).isTrue();
    assertThat(serializedContent.getTableData().getRowCount()).isEqualTo(tableData.size());
    for (int i = 0; i < serializedContent.getTableData().getRowCount(); i++) {
      assertThat(serializedContent.getTableData().getRow(i).getCellCount())
          .isEqualTo(tableData.get(i).size());
      for (int j = 0; j < serializedContent.getTableData().getRow(i).getCellCount(); j++) {
        assertThat(serializedContent.getTableData().getRow(i).getCell(j))
            .isEqualTo(tableData.get(i).get(j).serialize());
      }
    }
  }

  @Test
  public void testDeserializeTableContentWithNoTableData() {
    com.google.googlesql.LocalService.TableContent tableContent =
        com.google.googlesql.LocalService.TableContent.getDefaultInstance();
    TableContent deserializedContent = TableContent.deserialize(ImmutableList.of(), tableContent);
    assertThat(deserializedContent.getTableData()).isEmpty();
  }

  @Test
  public void testDeserializeTableContentWithEmptyTableData() {
    com.google.googlesql.LocalService.TableContent tableContent =
        com.google.googlesql.LocalService.TableContent.newBuilder()
            .setTableData(TableData.getDefaultInstance())
            .build();
    TableContent deserializedContent = TableContent.deserialize(ImmutableList.of(), tableContent);
    assertThat(deserializedContent.getTableData()).isEmpty();
  }

  @Test
  public void testDeserializeTableContentWithTableData() {
    TypeFactory factory = TypeFactory.uniqueNames();
    ProtoType protoType = factory.createProtoType(TypeProto.class);
    EnumType enumProtoType =
        factory.createEnumType(
            getDescriptorPoolWithTypeProtoAndTypeKind().findEnumTypeByName("googlesql.TypeKind"));

    ByteString protoValue1 =
        TypeProto.newBuilder().setTypeKind(TypeKind.TYPE_DATE).build().toByteString();
    ByteString protoValue2 =
        TypeProto.newBuilder().setTypeKind(TypeKind.TYPE_STRING).build().toByteString();
    com.google.googlesql.LocalService.TableContent tableContent =
        com.google.googlesql.LocalService.TableContent.newBuilder()
            .setTableData(
                TableData.newBuilder()
                    .addRow(
                        Row.newBuilder()
                            .addCell(ValueProto.newBuilder().setStringValue("string1"))
                            .addCell(ValueProto.newBuilder().setInt32Value(111))
                            .addCell(ValueProto.newBuilder().setProtoValue(protoValue1))
                            .addCell(
                                ValueProto.newBuilder().setEnumValue(TypeKind.TYPE_DATE_VALUE)))
                    .addRow(
                        Row.newBuilder()
                            .addCell(ValueProto.newBuilder().setStringValue("string2"))
                            .addCell(ValueProto.newBuilder().setInt32Value(222))
                            .addCell(ValueProto.newBuilder().setProtoValue(protoValue2))
                            .addCell(
                                ValueProto.newBuilder().setEnumValue(TypeKind.TYPE_STRING_VALUE))))
            .build();

    ImmutableList<Type> columnsTypes =
        ImmutableList.of(stringType, int32Type, protoType, enumProtoType);
    TableContent deserializedContent = TableContent.deserialize(columnsTypes, tableContent);

    ImmutableList<ImmutableList<Value>> deserializedData = deserializedContent.getTableData();
    assertThat(deserializedData).hasSize(2);

    ImmutableList<Value> row0 = deserializedData.get(0);
    assertThat(row0).hasSize(4);
    assertThat(row0.get(0).getType()).isEqualTo(stringType);
    assertThat(row0.get(0).getStringValue()).isEqualTo("string1");
    assertThat(row0.get(1).getType()).isEqualTo(int32Type);
    assertThat(row0.get(1).getInt32Value()).isEqualTo(111);
    assertThat(row0.get(2).getType()).isEqualTo(protoType);
    assertThat(row0.get(2).getProtoValue()).isEqualTo(protoValue1);
    assertThat(row0.get(3).getType()).isEqualTo(enumProtoType);
    assertThat(row0.get(3).getEnumName()).isEqualTo(TypeKind.TYPE_DATE.name());

    ImmutableList<Value> row1 = deserializedData.get(1);
    assertThat(row0).hasSize(4);
    assertThat(row1.get(0).getType()).isEqualTo(stringType);
    assertThat(row1.get(0).getStringValue()).isEqualTo("string2");
    assertThat(row1.get(1).getType()).isEqualTo(int32Type);
    assertThat(row1.get(1).getInt32Value()).isEqualTo(222);
    assertThat(row1.get(2).getType()).isEqualTo(protoType);
    assertThat(row1.get(2).getProtoValue()).isEqualTo(protoValue2);
    assertThat(row1.get(3).getType()).isEqualTo(enumProtoType);
    assertThat(row1.get(3).getEnumName()).isEqualTo(TypeKind.TYPE_STRING.name());
  }

  @Test
  public void testDeserializeTableContentWithTableDataTypeMismatch() {
    com.google.googlesql.LocalService.TableContent tableContent =
        com.google.googlesql.LocalService.TableContent.newBuilder()
            .setTableData(
                TableData.newBuilder()
                    .addRow(
                        Row.newBuilder()
                            .addCell(ValueProto.newBuilder().setStringValue("string1"))))
            .build();
    ImmutableList<Type> columnsTypes = ImmutableList.of(int32Type);
    try {
      TableContent.deserialize(columnsTypes, tableContent);
      fail();
    } catch (IllegalArgumentException exception) {
      assertWithMessage(exception.getMessage())
          .that(exception.getMessage().contains("Type mismatch"))
          .isTrue();
    }
  }

  @Test
  public void testDeserializeTableContentWithTableDataNumberOfColumnsMismatch() {
    com.google.googlesql.LocalService.TableContent tableContent =
        com.google.googlesql.LocalService.TableContent.newBuilder()
            .setTableData(
                TableData.newBuilder()
                    .addRow(
                        Row.newBuilder()
                            .addCell(ValueProto.newBuilder().setStringValue("string1"))
                            .addCell(ValueProto.newBuilder().setInt32Value(111))))
            .build();
    ImmutableList<Type> columnsTypes = ImmutableList.of(stringType);
    try {
      TableContent.deserialize(columnsTypes, tableContent);
      fail();
    } catch (IllegalArgumentException exception) {
      assertWithMessage(exception.getMessage())
          .that(exception.getMessage().contains("Unexpected number of elements for row content"))
          .isTrue();
    }

    com.google.googlesql.LocalService.TableContent tableContent2 =
        com.google.googlesql.LocalService.TableContent.newBuilder()
            .setTableData(
                TableData.newBuilder()
                    .addRow(
                        Row.newBuilder()
                            .addCell(ValueProto.newBuilder().setStringValue("string1"))
                            .addCell(ValueProto.newBuilder().setInt32Value(111)))
                    .addRow(
                        Row.newBuilder()
                            .addCell(ValueProto.newBuilder().setStringValue("string1"))))
            .build();
    ImmutableList<Type> columnsTypes2 = ImmutableList.of(stringType, int32Type);
    try {
      TableContent.deserialize(columnsTypes2, tableContent2);
      fail();
    } catch (IllegalArgumentException exception) {
      assertWithMessage(exception.getMessage())
          .that(exception.getMessage().contains("Unexpected number of elements for row content"))
          .isTrue();
    }
  }
}
