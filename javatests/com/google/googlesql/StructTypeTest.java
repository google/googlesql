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
import static com.google.googlesql.TypeTestBase.checkSerializable;
import static com.google.googlesql.TypeTestBase.checkTypeSerializationAndDeserialization;
import static com.google.googlesql.TypeTestBase.checkTypeSerializationAndDeserializationExistingPools;
import static com.google.googlesql.TypeTestBase.getDescriptorPoolWithTypeProtoAndTypeKind;

import com.google.common.collect.ImmutableList;
import com.google.common.collect.Lists;
import com.google.googlesql.GoogleSQLOptions.ProductMode;
import com.google.googlesql.GoogleSQLType.StructFieldProto;
import com.google.googlesql.GoogleSQLType.StructTypeProto;
import com.google.googlesql.GoogleSQLType.TypeKind;
import com.google.googlesql.GoogleSQLType.TypeProto;

import java.util.ArrayList;
import org.junit.Test;
import org.junit.runner.RunWith;
import org.junit.runners.JUnit4;

@RunWith(JUnit4.class)

public class StructTypeTest {

  @Test
  public void testComponentTypes() {
    assertThat(
            TypeFactory.createStructType(
                    ImmutableList.of(
                        new StructType.StructField(
                            "a", TypeFactory.createSimpleType(TypeKind.TYPE_INT32)),
                        new StructType.StructField(
                            "b", TypeFactory.createSimpleType(TypeKind.TYPE_INT64))))
                .componentTypes())
        .containsExactly(
            TypeFactory.createSimpleType(TypeKind.TYPE_INT32),
            TypeFactory.createSimpleType(TypeKind.TYPE_INT64));
  }

  @Test
  public void testSerializationAndDeserializationZeroFields() {
    ArrayList<StructType.StructField> fields = new ArrayList<>();

    StructType struct = TypeFactory.createStructType(fields);
    checkTypeSerializationAndDeserialization(struct);

    TypeProto proto = struct.serialize();
    assertThat(proto.getFileDescriptorSetCount()).isEqualTo(0);
  }

  @Test
  public void testSerializationAndDeserializationSimpleFields() {
    ArrayList<StructType.StructField> fields = new ArrayList<>();
    fields.add(new StructType.StructField("a", TypeFactory.createSimpleType(TypeKind.TYPE_INT32)));
    fields.add(new StructType.StructField("b", TypeFactory.createSimpleType(TypeKind.TYPE_INT64)));

    StructType struct = TypeFactory.createStructType(fields);
    checkTypeSerializationAndDeserialization(struct);

    TypeProto proto = struct.serialize();
    assertThat(proto.getFileDescriptorSetCount()).isEqualTo(0);
  }

  @Test
  public void testSerializationAndDeserializationAnonymousFields() {
    TypeFactory factory = TypeFactory.nonUniqueNames();
    ArrayList<StructType.StructField> fields = new ArrayList<>();
    fields.add(new StructType.StructField("", TypeFactory.createSimpleType(TypeKind.TYPE_INT32)));
    fields.add(new StructType.StructField("b", TypeFactory.createSimpleType(TypeKind.TYPE_INT64)));
    fields.add(new StructType.StructField("", factory.createEnumType(TypeKind.class)));

    StructType struct = TypeFactory.createStructType(fields);
    checkTypeSerializationAndDeserialization(struct);

    TypeProto proto = struct.serialize();
    assertThat(proto.getFileDescriptorSetCount()).isEqualTo(1);
  }

  @Test
  public void testSerializationAndDeserializationSamePool() {
    TypeFactory factory = TypeFactory.nonUniqueNames();
    EnumType enumType = factory.createEnumType(TypeKind.class);
    ProtoType protoType = factory.createProtoType(TypeProto.class);

    ArrayList<StructType.StructField> fields = new ArrayList<>();
    fields.add(new StructType.StructField("enum", enumType));
    fields.add(new StructType.StructField("proto", protoType));

    StructType struct = TypeFactory.createStructType(fields);
    checkTypeSerializationAndDeserialization(struct);

    TypeProto proto = struct.serialize();
    assertThat(proto.getFileDescriptorSetCount()).isEqualTo(1);
  }

  @Test
  public void testSerializationAndDeserializationDifferentPools() {
    TypeFactory factory = TypeFactory.nonUniqueNames();
    GoogleSQLDescriptorPool pool = getDescriptorPoolWithTypeProtoAndTypeKind();
    EnumType enumType = factory.createEnumType(pool.findEnumTypeByName("googlesql.TypeKind"));
    GoogleSQLDescriptorPool pool2 = getDescriptorPoolWithTypeProtoAndTypeKind();
    ProtoType protoType =
        factory.createProtoType(pool2.findMessageTypeByName("googlesql.TypeProto"));

    ArrayList<StructType.StructField> fields = new ArrayList<>();
    fields.add(new StructType.StructField("enum", enumType));
    fields.add(new StructType.StructField("proto", protoType));

    StructType struct = TypeFactory.createStructType(fields);
    checkTypeSerializationAndDeserialization(struct);

    TypeProto proto = struct.serialize();
    assertThat(proto.getFileDescriptorSetCount()).isEqualTo(2);

    checkTypeSerializationAndDeserializationExistingPools(struct, Lists.newArrayList(pool, pool2));
  }

  @Test
  public void testSerializableZeroFields() {
    ArrayList<StructType.StructField> fields = new ArrayList<>();

    StructType struct = TypeFactory.createStructType(fields);
    checkSerializable(struct);
  }

  @Test
  public void testSerializableSimpleFields() {
    ArrayList<StructType.StructField> fields = new ArrayList<>();
    fields.add(new StructType.StructField("a", TypeFactory.createSimpleType(TypeKind.TYPE_INT32)));
    fields.add(new StructType.StructField("b", TypeFactory.createSimpleType(TypeKind.TYPE_INT64)));

    StructType struct = TypeFactory.createStructType(fields);
    checkSerializable(struct);
  }

  @Test
  public void testSerializableAnonymousFields() {
    TypeFactory factory = TypeFactory.nonUniqueNames();
    ArrayList<StructType.StructField> fields = new ArrayList<>();
    fields.add(new StructType.StructField("", TypeFactory.createSimpleType(TypeKind.TYPE_INT32)));
    fields.add(new StructType.StructField("b", TypeFactory.createSimpleType(TypeKind.TYPE_INT64)));
    fields.add(new StructType.StructField("", factory.createEnumType(TypeKind.class)));

    StructType struct = TypeFactory.createStructType(fields);
    checkSerializable(struct);
  }

  @Test
  public void testSerializableSamePool() {
    TypeFactory factory = TypeFactory.nonUniqueNames();
    EnumType enumType = factory.createEnumType(TypeKind.class);
    ProtoType protoType = factory.createProtoType(TypeProto.class);

    ArrayList<StructType.StructField> fields = new ArrayList<>();
    fields.add(new StructType.StructField("enum", enumType));
    fields.add(new StructType.StructField("proto", protoType));

    StructType struct = TypeFactory.createStructType(fields);
    checkSerializable(struct);
  }

  @Test
  public void testSerializableDifferentPools() {
    TypeFactory factory = TypeFactory.nonUniqueNames();
    GoogleSQLDescriptorPool pool = getDescriptorPoolWithTypeProtoAndTypeKind();
    EnumType enumType = factory.createEnumType(pool.findEnumTypeByName("googlesql.TypeKind"));
    GoogleSQLDescriptorPool pool2 = getDescriptorPoolWithTypeProtoAndTypeKind();
    ProtoType protoType =
        factory.createProtoType(pool2.findMessageTypeByName("googlesql.TypeProto"));

    ArrayList<StructType.StructField> fields = new ArrayList<>();
    fields.add(new StructType.StructField("enum", enumType));
    fields.add(new StructType.StructField("proto", protoType));

    StructType struct = TypeFactory.createStructType(fields);
    checkSerializable(struct);
  }

  @Test
  public void testEquivalent() {
    ArrayList<StructType.StructField> fields1 = new ArrayList<>();
    fields1.add(new StructType.StructField("", TypeFactory.createSimpleType(TypeKind.TYPE_STRING)));
    fields1.add(new StructType.StructField("a", TypeFactory.createSimpleType(TypeKind.TYPE_INT32)));

    ArrayList<StructType.StructField> fields2 = new ArrayList<>();
    fields2.add(new StructType.StructField("", TypeFactory.createSimpleType(TypeKind.TYPE_STRING)));
    fields2.add(new StructType.StructField("b", TypeFactory.createSimpleType(TypeKind.TYPE_INT32)));

    ArrayList<StructType.StructField> fields3 = new ArrayList<>();
    fields3.add(new StructType.StructField("", TypeFactory.createSimpleType(TypeKind.TYPE_STRING)));
    fields3.add(new StructType.StructField("a", TypeFactory.createSimpleType(TypeKind.TYPE_BOOL)));

    ArrayList<StructType.StructField> fields4 = new ArrayList<>();
    fields4.add(new StructType.StructField("", TypeFactory.createSimpleType(TypeKind.TYPE_STRING)));
    fields4.add(new StructType.StructField("B", TypeFactory.createSimpleType(TypeKind.TYPE_INT32)));

    ArrayList<StructType.StructField> fields5 = new ArrayList<>();
    fields5.add(new StructType.StructField("", TypeFactory.createSimpleType(TypeKind.TYPE_STRING)));
    fields5.add(new StructType.StructField("B", TypeFactory.createSimpleType(TypeKind.TYPE_INT32)));
    fields5.add(new StructType.StructField("c", TypeFactory.createSimpleType(TypeKind.TYPE_INT32)));

    StructType struct1 = TypeFactory.createStructType(fields1);
    StructType struct2 = TypeFactory.createStructType(fields2);
    StructType struct3 = TypeFactory.createStructType(fields3);
    StructType struct4 = TypeFactory.createStructType(fields4);
    StructType struct5 = TypeFactory.createStructType(fields5);

    assertThat(struct1.equivalent(struct1)).isTrue();
    assertThat(struct1.equivalent(struct2)).isFalse();
    assertThat(struct1.equivalent(struct3)).isFalse();
    assertThat(struct1.equivalent(struct4)).isFalse();
    assertThat(struct1.equivalent(struct5)).isFalse();

    assertThat(struct2.equivalent(struct1)).isFalse();
    assertThat(struct2.equivalent(struct2)).isTrue();
    assertThat(struct2.equivalent(struct3)).isFalse();
    assertThat(struct2.equivalent(struct4)).isTrue();
    assertThat(struct2.equivalent(struct5)).isFalse();

    assertThat(struct3.equivalent(struct1)).isFalse();
    assertThat(struct3.equivalent(struct2)).isFalse();
    assertThat(struct3.equivalent(struct3)).isTrue();
    assertThat(struct3.equivalent(struct4)).isFalse();
    assertThat(struct3.equivalent(struct5)).isFalse();

    assertThat(struct4.equivalent(struct1)).isFalse();
    assertThat(struct4.equivalent(struct2)).isTrue();
    assertThat(struct4.equivalent(struct3)).isFalse();
    assertThat(struct4.equivalent(struct4)).isTrue();
    assertThat(struct4.equivalent(struct5)).isFalse();

    assertThat(struct5.equivalent(struct1)).isFalse();
    assertThat(struct5.equivalent(struct2)).isFalse();
    assertThat(struct5.equivalent(struct3)).isFalse();
    assertThat(struct5.equivalent(struct4)).isFalse();
    assertThat(struct5.equivalent(struct5)).isTrue();

    assertThat(struct1.getField(0).equivalent(struct2.getField(0))).isTrue();
    assertThat(struct1.getField(0).equivalent(struct3.getField(0))).isTrue();
    assertThat(struct1.getField(0).equivalent(struct4.getField(0))).isTrue();

    assertThat(struct2.getField(0).equivalent(struct4.getField(0))).isTrue();
    assertThat(struct2.getField(1).equivalent(struct4.getField(1))).isTrue();

    assertThat(struct1.equivalent(TypeFactory.createSimpleType(TypeKind.TYPE_BOOL))).isFalse();
  }

  @Test
  public void testEqualsAndHashCode() {
    ArrayList<StructType.StructField> fields1 = new ArrayList<>();
    fields1.add(new StructType.StructField("", TypeFactory.createSimpleType(TypeKind.TYPE_STRING)));
    fields1.add(new StructType.StructField("a", TypeFactory.createSimpleType(TypeKind.TYPE_INT32)));

    ArrayList<StructType.StructField> fields2 = new ArrayList<>();
    fields2.add(new StructType.StructField("", TypeFactory.createSimpleType(TypeKind.TYPE_STRING)));
    fields2.add(new StructType.StructField("b", TypeFactory.createSimpleType(TypeKind.TYPE_INT32)));

    ArrayList<StructType.StructField> fields3 = new ArrayList<>();
    fields3.add(new StructType.StructField("", TypeFactory.createSimpleType(TypeKind.TYPE_STRING)));
    fields3.add(new StructType.StructField("a", TypeFactory.createSimpleType(TypeKind.TYPE_INT32)));

    ArrayList<StructType.StructField> fields4 = new ArrayList<>();
    fields3.add(new StructType.StructField("", TypeFactory.createSimpleType(TypeKind.TYPE_STRING)));
    fields3.add(new StructType.StructField("a", TypeFactory.createSimpleType(TypeKind.TYPE_INT32)));
    fields3.add(new StructType.StructField("b", TypeFactory.createSimpleType(TypeKind.TYPE_INT32)));

    StructType struct1 = TypeFactory.createStructType(fields1);
    StructType struct2 = TypeFactory.createStructType(fields2);
    StructType struct3 = TypeFactory.createStructType(fields1);
    StructType struct4 = TypeFactory.createStructType(fields1);
    StructType struct5 = TypeFactory.createStructType(fields4);

    assertThat(struct1.equals(struct2)).isFalse();
    assertThat(struct1).isEqualTo(struct3);
    assertThat(struct1).isEqualTo(struct4);
    assertThat(struct1.equals(struct5)).isFalse();
    assertThat(struct1.equals(null)).isFalse();
    assertThat(struct1.equals(null)).isFalse();
    assertThat(struct1.equals(TypeFactory.createSimpleType(TypeKind.TYPE_INT32))).isFalse();

    assertThat(struct3.hashCode()).isEqualTo(struct1.hashCode());
    assertThat(struct4.hashCode()).isEqualTo(struct1.hashCode());

    assertThat(struct1.getField(0).equals(struct2.getField(0))).isTrue();
    assertThat(struct1.getField(1).equals(struct2.getField(1))).isFalse();
    assertThat(struct1.getField(0).equals(struct1.getField(1))).isFalse();
    assertThat(struct1.getField(0).equals(null)).isFalse();
    assertThat(struct1.getField(0).equals(null)).isFalse();
  }

  @Test
  public void testAsStruct() {
    TypeFactory factory = TypeFactory.nonUniqueNames();

    ArrayType array =
        TypeFactory.createArrayType(TypeFactory.createSimpleType(TypeKind.TYPE_INT32));
    EnumType enumType = factory.createEnumType(TypeKind.class);
    ProtoType proto = factory.createProtoType(TypeProto.class);
    ArrayList<StructType.StructField> fields = new ArrayList<>();
    fields.add(new StructType.StructField("", TypeFactory.createSimpleType(TypeKind.TYPE_STRING)));
    fields.add(new StructType.StructField("a", TypeFactory.createSimpleType(TypeKind.TYPE_INT32)));
    StructType struct = TypeFactory.createStructType(fields);

    assertThat(struct.asStruct()).isEqualTo(struct);
    assertThat(array.asStruct()).isNull();
    assertThat(enumType.asStruct()).isNull();
    assertThat(proto.asStruct()).isNull();
    assertThat(TypeFactory.createSimpleType(TypeKind.TYPE_INT32).asStruct()).isNull();
  }

  @Test
  public void testClassAndProtoSize() {
    assertWithMessage(
            "The number of fields of StructTypeProto has changed, "
                + "please also update the serialization code accordingly.")
        .that(StructTypeProto.getDescriptor().getFields())
        .hasSize(1);
    assertWithMessage(
            "The number of fields in StructType class has changed, "
                + "please also update the proto and serialization code accordingly.")
        .that(TestUtil.getNonStaticFieldCount(StructType.class))
        .isEqualTo(1);
    assertWithMessage(
            "The number of fields of StructFieldProto has changed, "
                + "please also update the serialization code accordingly.")
        .that(StructFieldProto.getDescriptor().getFields())
        .hasSize(2);
    assertWithMessage(
            "The number of fields in StructField class has changed, "
                + "please also update the proto and serialization code accordingly.")
        .that(TestUtil.getNonStaticFieldCount(StructType.StructField.class))
        .isEqualTo(2);
  }
}
