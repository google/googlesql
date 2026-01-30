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

import com.google.protobuf.TextFormat;
import com.google.protobuf.TextFormat.ParseException;
import com.google.googlesql.GoogleSQLType.TypeKind;
import com.google.googlesql.GoogleSQLType.TypeProto;
import com.google.googlesql.SimpleTableProtos.SimpleColumnProto;

import java.util.ArrayList;
import org.junit.Test;
import org.junit.runner.RunWith;
import org.junit.runners.JUnit4;

@RunWith(JUnit4.class)

public class SimpleColumnTest {

  @Test
  public void testSimpleTypeColumn() {
    TypeFactory factory = TypeFactory.nonUniqueNames();
    FileDescriptorSetsBuilder descriptor = new FileDescriptorSetsBuilder();
    SimpleType type = TypeFactory.createSimpleType(TypeKind.TYPE_BOOL);
    SimpleColumn column1 = new SimpleColumn("t1", "c1", type);

    assertThat(column1.getName()).isEqualTo("c1");
    assertThat(column1.getFullName()).isEqualTo("t1.c1");
    assertThat(type.equivalent(column1.getType())).isTrue();
    assertThat(column1.isPseudoColumn()).isFalse();

    SimpleColumn column =
        SimpleColumn.deserialize(
            column1.serialize(descriptor), "t1", descriptor.getDescriptorPools(), factory);
    assertThat(column.serialize(descriptor).equals(column1.serialize(descriptor))).isTrue();
    assertThat(descriptor.getFileDescriptorSetCount()).isEqualTo(0);
  }

  @Test
  public void testArrayTypeColumn() {
    TypeFactory factory = TypeFactory.nonUniqueNames();
    FileDescriptorSetsBuilder descriptor = new FileDescriptorSetsBuilder();
    ArrayType array =
        TypeFactory.createArrayType(TypeFactory.createSimpleType(TypeKind.TYPE_INT32));
    SimpleColumn column2 =
        new SimpleColumn(
            "t2", "c2", array, /* isPseudoColumn = */ true, /* isWritableColumn = */ true);

    assertThat(column2.getName()).isEqualTo("c2");
    assertThat(column2.getFullName()).isEqualTo("t2.c2");
    assertThat(array.equivalent(column2.getType())).isTrue();
    assertThat(column2.isPseudoColumn()).isTrue();

    SimpleColumn column =
        SimpleColumn.deserialize(
            column2.serialize(descriptor), "t2", descriptor.getDescriptorPools(), factory);
    assertThat(column.serialize(descriptor).equals(column2.serialize(descriptor))).isTrue();
    assertThat(descriptor.getFileDescriptorSetCount()).isEqualTo(0);
  }

  @Test
  public void testEnumTypeColumn() {
    TypeFactory factory = TypeFactory.nonUniqueNames();
    FileDescriptorSetsBuilder descriptor = new FileDescriptorSetsBuilder();
    GoogleSQLDescriptorPool pool = new GoogleSQLDescriptorPool();
    TypeProto proto = factory.createProtoType(TypeProto.class).serialize();
    pool.importFileDescriptorSet(proto.getFileDescriptorSet(0));
    EnumType enumType = factory.createEnumType(pool.findEnumTypeByName("googlesql.TypeKind"));
    SimpleColumn column3 =
        new SimpleColumn(
            "t3", "c3", enumType, /* isPseudoColumn = */ false, /* isWritableColumn = */ true);

    assertThat(column3.getName()).isEqualTo("c3");
    assertThat(column3.getFullName()).isEqualTo("t3.c3");
    assertThat(enumType.equivalent(column3.getType())).isTrue();
    assertThat(column3.isPseudoColumn()).isFalse();

    SimpleColumn column =
        SimpleColumn.deserialize(
            column3.serialize(descriptor), "t3", descriptor.getDescriptorPools(), factory);
    assertThat(column.serialize(descriptor).equals(column3.serialize(descriptor))).isTrue();
    assertThat(descriptor.getFileDescriptorSetCount()).isEqualTo(1);
  }

  @Test
  public void testProtoTypeColumn() {
    TypeFactory factory = TypeFactory.nonUniqueNames();
    FileDescriptorSetsBuilder descriptor = new FileDescriptorSetsBuilder();
    ProtoType protoType = factory.createProtoType(TypeProto.class);
    SimpleColumn column4 = new SimpleColumn("t4", "c4", protoType);

    assertThat(column4.getName()).isEqualTo("c4");
    assertThat(column4.getFullName()).isEqualTo("t4.c4");
    assertThat(protoType.equivalent(column4.getType())).isTrue();
    assertThat(column4.isPseudoColumn()).isFalse();

    SimpleColumn column =
        SimpleColumn.deserialize(
            column4.serialize(descriptor), "t4", descriptor.getDescriptorPools(), factory);
    assertThat(column.serialize(descriptor).equals(column4.serialize(descriptor))).isTrue();
    assertThat(descriptor.getFileDescriptorSetCount()).isEqualTo(1);
    assertThat(descriptor.getDescriptorPools().get(0).findMessageTypeByName("googlesql.TypeProto"))
        .isNotNull();
  }

  @Test
  public void testStructTypeColumn() {
    TypeFactory factory = TypeFactory.nonUniqueNames();
    FileDescriptorSetsBuilder descriptor = new FileDescriptorSetsBuilder();
    GoogleSQLDescriptorPool pool = new GoogleSQLDescriptorPool();
    ProtoType protoType = factory.createProtoType(TypeProto.class);
    TypeProto proto = factory.createProtoType(TypeProto.class).serialize();
    pool.importFileDescriptorSet(proto.getFileDescriptorSet(0));
    EnumType enumType = factory.createEnumType(pool.findEnumTypeByName("googlesql.TypeKind"));
    ArrayList<StructType.StructField> fields = new ArrayList<>();
    fields.add(new StructType.StructField("enum", enumType));
    fields.add(new StructType.StructField("proto", protoType));
    StructType struct = TypeFactory.createStructType(fields);
    SimpleColumn column5 = new SimpleColumn("t5", "c5", struct);

    assertThat(column5.getName()).isEqualTo("c5");
    assertThat(column5.getFullName()).isEqualTo("t5.c5");
    assertThat(struct.equivalent(column5.getType())).isTrue();
    assertThat(column5.isPseudoColumn()).isFalse();

    SimpleColumn column =
        SimpleColumn.deserialize(
            column5.serialize(descriptor), "t5", descriptor.getDescriptorPools(), factory);
    assertThat(column.serialize(descriptor).equals(column5.serialize(descriptor))).isTrue();
    assertThat(descriptor.getFileDescriptorSetCount()).isEqualTo(2);
    assertThat(descriptor.getDescriptorPools().get(0).findMessageTypeByName("googlesql.TypeProto"))
        .isNotNull();
    assertThat(descriptor.getDescriptorPools().get(1).findEnumTypeByName("googlesql.TypeKind"))
        .isNotNull();
  }

  @Test
  public void testAnonymousColumn() throws ParseException {
    SimpleType type = TypeFactory.createSimpleType(TypeKind.TYPE_BOOL);
    SimpleColumn unused = new SimpleColumn("t1", "", type);

    SimpleColumnProto.Builder builder = SimpleColumnProto.newBuilder();
    TextFormat.merge("type { type_kind: TYPE_INT32 }", builder);
    SimpleColumn.deserialize(builder.build(), "t1", null, TypeFactory.nonUniqueNames());
  }

  @Test
  public void testIllegalColumn() throws ParseException {
    SimpleColumnProto.Builder builder = SimpleColumnProto.newBuilder();
    TextFormat.merge("", builder);
    try {
      SimpleColumn.deserialize(builder.build(), "t1", null, TypeFactory.nonUniqueNames());
      fail();
    } catch (IllegalArgumentException expected) {
    }
  }

  @Test
  public void testClassAndProtoSize() {
    // TODO: add serialization code for default values.
    assertWithMessage(
            "The number of fields of SimpleColumnProto has changed, "
                + "please also update the serialization code accordingly.")
        .that(SimpleColumnProto.getDescriptor().getFields())
        .hasSize(8);
    assertWithMessage(
            "The number of fields in SimpleColumn class has changed, "
                + "please also update the proto and serialization code accordingly.")
        .that(TestUtil.getNonStaticFieldCount(SimpleColumn.class))
        .isEqualTo(6);
  }
}
