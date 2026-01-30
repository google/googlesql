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

import com.google.common.collect.Lists;
import com.google.common.testing.EqualsTester;
import com.google.protobuf.DescriptorProtos.FieldDescriptorProto;
import com.google.protobuf.DescriptorProtos.FileDescriptorSet;
import com.google.googlesql.GoogleSQLType.EnumTypeProto;
import com.google.googlesql.GoogleSQLType.TypeKind;
import com.google.googlesql.GoogleSQLType.TypeProto;
import com.google.googlesql.functions.GoogleSQLRoundingMode.RoundingMode;
import java.util.ArrayList;
import java.util.List;
import org.junit.Test;
import org.junit.runner.RunWith;
import org.junit.runners.JUnit4;

@RunWith(JUnit4.class)
public class EnumTypeTest {

  @Test
  public void testSerializationAndDeserialization() {
    TypeFactory factory = TypeFactory.nonUniqueNames();
    checkTypeSerializationAndDeserialization(factory.createEnumType(TypeKind.class));

    // nested enum type
    checkTypeSerializationAndDeserialization(
        factory.createEnumType(FieldDescriptorProto.Type.class));

    checkTypeSerializationAndDeserialization(
        factory.createEnumType(FieldDescriptorProto.Label.class));

    GoogleSQLDescriptorPool pool = getDescriptorPoolWithTypeProtoAndTypeKind();
    EnumType type = factory.createEnumType(pool.findEnumTypeByName("googlesql.TypeKind"));
    checkTypeSerializationAndDeserialization(type);

    List<DescriptorPool> pools = Lists.newArrayList(pool);
    checkTypeSerializationAndDeserializationExistingPools(type, pools);
  }

  @Test
  public void testSerializationAndDeserializationSharedFileDescriptorSets() {
    GoogleSQLDescriptorPool pool = getDescriptorPoolWithTypeProtoAndTypeKind();
    TypeFactory factory = TypeFactory.nonUniqueNames();
    List<Type> types = new ArrayList<>();
    // loaded enum
    types.add(factory.createEnumType(pool.findEnumTypeByName("googlesql.TypeKind")));
    // generated enum with the same name
    types.add(factory.createEnumType(TypeKind.class));
    // other generated enums
    types.add(factory.createEnumType(FieldDescriptorProto.Type.class));
    types.add(factory.createEnumType(FieldDescriptorProto.Label.class));
    // duplicated ones
    types.add(factory.createEnumType(pool.findEnumTypeByName("googlesql.TypeKind")));
    types.add(factory.createEnumType(FieldDescriptorProto.Type.class));
    FileDescriptorSetsBuilder fileDescriptorSetsBuilder = new FileDescriptorSetsBuilder();
    List<TypeProto> protos = new ArrayList<>();
    for (Type type : types) {
      TypeProto.Builder builder = TypeProto.newBuilder();
      type.serialize(builder, fileDescriptorSetsBuilder);
      protos.add(builder.build());
    }

    List<FileDescriptorSet> sets = fileDescriptorSetsBuilder.build();
    // total number of FileDescriptorSet serialized:
    // 1 loaded GoogleSQLDescriptorPool + 1 dummy pool for generated protos.
    assertThat(sets).hasSize(2);
    List<DescriptorPool> pools = new ArrayList<>();
    for (FileDescriptorSet fileDescriptorSet : sets) {
      GoogleSQLDescriptorPool descriptorPool = new GoogleSQLDescriptorPool();
      descriptorPool.importFileDescriptorSet(fileDescriptorSet);
      pools.add(descriptorPool);
    }

    assertThat(types).hasSize(protos.size());
    for (TypeProto proto : protos) {
      // type protos are not self-contained
      assertThat(proto.getFileDescriptorSetCount()).isEqualTo(0);
      // but can be deserialized with existing pools
      Type type = factory.deserialize(proto, pools);
      checkTypeSerializationAndDeserialization(type);
    }
  }

  @Test
  public void testSerializable() {
    TypeFactory factory = TypeFactory.nonUniqueNames();
    checkTypeSerializationAndDeserialization(factory.createEnumType(TypeKind.class));

    // nested enum type
    checkSerializable(factory.createEnumType(FieldDescriptorProto.Type.class));

    checkSerializable(factory.createEnumType(FieldDescriptorProto.Label.class));

    GoogleSQLDescriptorPool pool = getDescriptorPoolWithTypeProtoAndTypeKind();
    EnumType type = factory.createEnumType(pool.findEnumTypeByName("googlesql.TypeKind"));
    checkSerializable(type);
  }

  @Test
  public void testSerializableSharedFileDescriptorSets() {
    GoogleSQLDescriptorPool pool = getDescriptorPoolWithTypeProtoAndTypeKind();
    TypeFactory factory = TypeFactory.nonUniqueNames();
    List<Type> types = new ArrayList<>();
    // loaded enum
    types.add(factory.createEnumType(pool.findEnumTypeByName("googlesql.TypeKind")));
    // generated enum with the same name
    types.add(factory.createEnumType(TypeKind.class));
    // other generated enums
    types.add(factory.createEnumType(FieldDescriptorProto.Type.class));
    types.add(factory.createEnumType(FieldDescriptorProto.Label.class));
    // duplicated ones
    types.add(factory.createEnumType(pool.findEnumTypeByName("googlesql.TypeKind")));
    types.add(factory.createEnumType(FieldDescriptorProto.Type.class));
    FileDescriptorSetsBuilder fileDescriptorSetsBuilder = new FileDescriptorSetsBuilder();
    List<TypeProto> protos = new ArrayList<>();
    for (Type type : types) {
      TypeProto.Builder builder = TypeProto.newBuilder();
      type.serialize(builder, fileDescriptorSetsBuilder);
      protos.add(builder.build());
    }

    List<FileDescriptorSet> sets = fileDescriptorSetsBuilder.build();
    // total number of FileDescriptorSet serialized:
    // 1 loaded GoogleSQLDescriptorPool + 1 dummy pool for generated protos.
    assertThat(sets).hasSize(2);
    List<DescriptorPool> pools = new ArrayList<>();
    for (FileDescriptorSet fileDescriptorSet : sets) {
      GoogleSQLDescriptorPool descriptorPool = new GoogleSQLDescriptorPool();
      descriptorPool.importFileDescriptorSet(fileDescriptorSet);
      pools.add(descriptorPool);
    }

    assertThat(types).hasSize(protos.size());
    for (TypeProto proto : protos) {
      // type protos are not self-contained
      assertThat(proto.getFileDescriptorSetCount()).isEqualTo(0);
      // but can be deserialized with existing pools
      Type type = factory.deserialize(proto, pools);
      checkSerializable(type);
    }
  }

  @Test
  public void testEquivalent() {
    TypeFactory factory = TypeFactory.nonUniqueNames();

    EnumType enum1 = factory.createEnumType(TypeKind.class);
    EnumType enum2 =
        factory.createEnumType(
            getDescriptorPoolWithTypeProtoAndTypeKind().findEnumTypeByName("googlesql.TypeKind"));
    EnumType enum3 = factory.createEnumType(FieldDescriptorProto.Type.class);

    assertThat(enum1.equivalent(enum1)).isTrue();
    assertThat(enum1.equivalent(enum2)).isTrue();
    assertThat(enum1.equivalent(enum3)).isFalse();
    assertThat(enum2.equivalent(enum1)).isTrue();
    assertThat(enum2.equivalent(enum2)).isTrue();
    assertThat(enum2.equivalent(enum3)).isFalse();
    assertThat(enum3.equivalent(enum1)).isFalse();
    assertThat(enum3.equivalent(enum2)).isFalse();
    assertThat(enum3.equivalent(enum3)).isTrue();

    assertThat(enum1.equivalent(TypeFactory.createSimpleType(TypeKind.TYPE_BOOL))).isFalse();
  }

  @Test
  public void testEquals() {
    TypeFactory factory = TypeFactory.nonUniqueNames();

    EnumType enum1 = factory.createEnumType(TypeKind.class);
    EnumType enum2 =
        factory.createEnumType(
            getDescriptorPoolWithTypeProtoAndTypeKind().findEnumTypeByName("googlesql.TypeKind"));
    EnumType enum3 = factory.createEnumType(FieldDescriptorProto.Type.class);

    new EqualsTester().addEqualityGroup(enum1).testEquals();
    assertThat(enum1.equals(enum2)).isFalse();
    assertThat(enum1.equals(enum3)).isFalse();
    assertThat(enum2.equals(enum1)).isFalse();
    new EqualsTester().addEqualityGroup(enum2).testEquals();
    assertThat(enum2.equals(enum3)).isFalse();
    assertThat(enum3.equals(enum1)).isFalse();
    assertThat(enum3.equals(enum2)).isFalse();
    new EqualsTester().addEqualityGroup(enum3).testEquals();

    assertThat(enum1.equals(TypeFactory.createSimpleType(TypeKind.TYPE_BOOL))).isFalse();
  }

  @Test
  public void testAsEnum() {
    TypeFactory factory = TypeFactory.nonUniqueNames();

    ArrayType array =
        TypeFactory.createArrayType(TypeFactory.createSimpleType(TypeKind.TYPE_INT32));
    EnumType enumType = factory.createEnumType(TypeKind.class);
    ProtoType proto = factory.createProtoType(TypeProto.class);
    List<StructType.StructField> fields = new ArrayList<>();
    fields.add(new StructType.StructField("", TypeFactory.createSimpleType(TypeKind.TYPE_STRING)));
    fields.add(new StructType.StructField("a", TypeFactory.createSimpleType(TypeKind.TYPE_INT32)));
    StructType struct = TypeFactory.createStructType(fields);

    assertThat(enumType.asEnum()).isEqualTo(enumType);
    assertThat(array.asEnum()).isNull();
    assertThat(proto.asEnum()).isNull();
    assertThat(struct.asEnum()).isNull();
    assertThat(TypeFactory.createSimpleType(TypeKind.TYPE_INT32).asEnum()).isNull();
  }

  @Test
  public void testClassAndProtoSize() {
    assertWithMessage(
            "The number of fields of EnumTypeProto has changed, please also update the "
                + "serialization code accordingly.")
        .that(EnumTypeProto.getDescriptor().getFields())
        .hasSize(5);

    assertWithMessage(
            "The number of fields in EnumType class has changed, please also update the proto and "
                + "serialization code accordingly.")
        .that(TestUtil.getNonStaticFieldCount(EnumType.class))
        .isEqualTo(3);
  }
}
