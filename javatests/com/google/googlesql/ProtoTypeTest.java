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
import com.google.protobuf.DescriptorProtos.FieldOptions;
import com.google.protobuf.DescriptorProtos.FileDescriptorSet;
import com.google.protobuf.Descriptors.FieldDescriptor;
import com.google.googlesql.GoogleSQLType.ProtoTypeProto;
import com.google.googlesql.GoogleSQLType.TypeKind;
import com.google.googlesql.GoogleSQLType.TypeProto;
import com.google.googlesql.TypeAnnotationProto.FieldFormat;
import com.google.googlesqltest.TestSchemaProto.FieldFormatsProto;

import java.util.ArrayList;
import java.util.List;
import org.junit.Test;
import org.junit.runner.RunWith;
import org.junit.runners.JUnit4;

@RunWith(JUnit4.class)

public class ProtoTypeTest {

  @Test
  public void testSerializationAndDeserialization() {
    TypeFactory factory = TypeFactory.nonUniqueNames();
    checkTypeSerializationAndDeserialization(factory.createProtoType(TypeProto.class));

    GoogleSQLDescriptorPool pool = getDescriptorPoolWithTypeProtoAndTypeKind();
    ProtoType type = factory.createProtoType(pool.findMessageTypeByName("googlesql.TypeProto"));
    checkTypeSerializationAndDeserialization(type);

    List<DescriptorPool> pools = Lists.newArrayList(pool);
    checkTypeSerializationAndDeserializationExistingPools(type, pools);

    ProtoType type2 =
        factory.createProtoType(pool.findMessageTypeByName("googlesql.ArrayTypeProto"));
    checkTypeSerializationAndDeserialization(type2);

    ProtoType type3 =
        factory.createProtoType(pool.findMessageTypeByName("googlesql.ProtoTypeProto"));
    checkTypeSerializationAndDeserialization(type3);
  }

  @Test
  public void testSerializeationAndDeserializationMultipleTypesWithSharedPools() {
    GoogleSQLDescriptorPool pool = getDescriptorPoolWithTypeProtoAndTypeKind();
    GoogleSQLDescriptorPool pool2 = getDescriptorPoolWithTypeProtoAndTypeKind();
    TypeFactory factory = TypeFactory.nonUniqueNames();
    List<Type> types = new ArrayList<>();
    // some proto
    types.add(factory.createProtoType(pool.findMessageTypeByName("googlesql.StructTypeProto")));
    // another proto
    types.add(factory.createProtoType(pool.findMessageTypeByName("googlesql.EnumTypeProto")));
    // duplicated proto from different pool
    types.add(factory.createProtoType(pool2.findMessageTypeByName("googlesql.EnumTypeProto")));
    // duplicated proto from same pool
    types.add(factory.createProtoType(pool2.findMessageTypeByName("googlesql.EnumTypeProto")));
    // and an enum
    types.add(factory.createEnumType(pool.findEnumTypeByName("googlesql.TypeKind")));
    // add some simple types
    types.add(TypeFactory.createSimpleType(TypeKind.TYPE_BOOL));
    types.add(TypeFactory.createSimpleType(TypeKind.TYPE_DOUBLE));
    FileDescriptorSetsBuilder fileDescriptorSetsBuilder = new FileDescriptorSetsBuilder();
    List<TypeProto> protos = new ArrayList<>();
    for (Type type : types) {
      TypeProto.Builder builder = TypeProto.newBuilder();
      type.serialize(builder, fileDescriptorSetsBuilder);
      protos.add(builder.build());
    }

    List<FileDescriptorSet> sets = fileDescriptorSetsBuilder.build();
    // total number of FileDescriptorSet serialized:
    // matches the number of DescriptorPools used above.
    assertThat(sets).hasSize(2);
    List<DescriptorPool> pools = new ArrayList<>();
    for (FileDescriptorSet fileDescriptorSet : sets) {
      pool = new GoogleSQLDescriptorPool();
      pool.importFileDescriptorSet(fileDescriptorSet);
      pools.add(pool);
    }

    assertThat(protos).hasSize(types.size());
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
    checkTypeSerializationAndDeserialization(factory.createProtoType(TypeProto.class));

    GoogleSQLDescriptorPool pool = getDescriptorPoolWithTypeProtoAndTypeKind();
    ProtoType type = factory.createProtoType(pool.findMessageTypeByName("googlesql.TypeProto"));
    checkSerializable(type);

    ProtoType type2 =
        factory.createProtoType(pool.findMessageTypeByName("googlesql.ArrayTypeProto"));
    checkSerializable(type2);

    ProtoType type3 =
        factory.createProtoType(pool.findMessageTypeByName("googlesql.ProtoTypeProto"));
    checkSerializable(type3);
  }

  @Test
  public void testSerializeableMultipleTypesWithSharedPools() {
    GoogleSQLDescriptorPool pool = getDescriptorPoolWithTypeProtoAndTypeKind();
    GoogleSQLDescriptorPool pool2 = getDescriptorPoolWithTypeProtoAndTypeKind();
    TypeFactory factory = TypeFactory.nonUniqueNames();
    List<Type> types = new ArrayList<>();
    // some proto
    types.add(factory.createProtoType(pool.findMessageTypeByName("googlesql.StructTypeProto")));
    // another proto
    types.add(factory.createProtoType(pool.findMessageTypeByName("googlesql.EnumTypeProto")));
    // duplicated proto from different pool
    types.add(factory.createProtoType(pool2.findMessageTypeByName("googlesql.EnumTypeProto")));
    // duplicated proto from same pool
    types.add(factory.createProtoType(pool2.findMessageTypeByName("googlesql.EnumTypeProto")));
    // and an enum
    types.add(factory.createEnumType(pool.findEnumTypeByName("googlesql.TypeKind")));
    // add some simple types
    types.add(TypeFactory.createSimpleType(TypeKind.TYPE_BOOL));
    types.add(TypeFactory.createSimpleType(TypeKind.TYPE_DOUBLE));
    FileDescriptorSetsBuilder fileDescriptorSetsBuilder = new FileDescriptorSetsBuilder();
    List<TypeProto> protos = new ArrayList<>();
    for (Type type : types) {
      TypeProto.Builder builder = TypeProto.newBuilder();
      type.serialize(builder, fileDescriptorSetsBuilder);
      protos.add(builder.build());
    }

    List<FileDescriptorSet> sets = fileDescriptorSetsBuilder.build();
    // total number of FileDescriptorSet serialized:
    // matches the number of DescriptorPools used above.
    assertThat(sets).hasSize(2);
    List<DescriptorPool> pools = new ArrayList<>();
    for (FileDescriptorSet fileDescriptorSet : sets) {
      pool = new GoogleSQLDescriptorPool();
      pool.importFileDescriptorSet(fileDescriptorSet);
      pools.add(pool);
    }

    assertThat(protos).hasSize(types.size());
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

    ProtoType proto1 = factory.createProtoType(TypeProto.class);
    ProtoType proto2 =
        factory.createProtoType(
            getDescriptorPoolWithTypeProtoAndTypeKind()
                .findMessageTypeByName("googlesql.TypeProto"));
    ProtoType proto3 = factory.createProtoType(FieldDescriptorProto.class);

    assertThat(proto1.equivalent(proto1)).isTrue();
    assertThat(proto1.equivalent(proto2)).isTrue();
    assertThat(proto1.equivalent(proto3)).isFalse();
    assertThat(proto2.equivalent(proto1)).isTrue();
    assertThat(proto2.equivalent(proto2)).isTrue();
    assertThat(proto2.equivalent(proto3)).isFalse();
    assertThat(proto3.equivalent(proto1)).isFalse();
    assertThat(proto3.equivalent(proto2)).isFalse();
    assertThat(proto3.equivalent(proto3)).isTrue();

    assertThat(proto1.equivalent(TypeFactory.createSimpleType(TypeKind.TYPE_BOOL))).isFalse();
  }

  private void verifyFormatAnnotation(FieldFormat.Format format, String fieldName) {
    FieldDescriptor field = FieldFormatsProto.getDescriptor().findFieldByName(fieldName);
    assertThat(ProtoType.getFormatAnnotation(field)).isEqualTo(format);
    assertThat(ProtoType.hasFormatAnnotation(field))
        .isEqualTo(format != FieldFormat.Format.DEFAULT_FORMAT);
  }

  @Test
  public void testFormatAnnotations() {
    verifyFormatAnnotation(FieldFormat.Format.DEFAULT_FORMAT, "no_annotation");
    verifyFormatAnnotation(FieldFormat.Format.DATE, "date");
    verifyFormatAnnotation(FieldFormat.Format.DATE, "date_64");
    verifyFormatAnnotation(FieldFormat.Format.DATE_DECIMAL, "date_decimal");
    verifyFormatAnnotation(FieldFormat.Format.DATE_DECIMAL, "date_decimal_64");
    verifyFormatAnnotation(FieldFormat.Format.DATE_DECIMAL, "date_decimal_encoding");
    verifyFormatAnnotation(FieldFormat.Format.TIMESTAMP_SECONDS, "seconds");
    verifyFormatAnnotation(FieldFormat.Format.TIMESTAMP_MILLIS, "millis");
    verifyFormatAnnotation(FieldFormat.Format.TIMESTAMP_MICROS, "micros");
    verifyFormatAnnotation(FieldFormat.Format.TIMESTAMP_SECONDS, "seconds_format");
    verifyFormatAnnotation(FieldFormat.Format.TIMESTAMP_MILLIS, "millis_format");
    verifyFormatAnnotation(FieldFormat.Format.TIMESTAMP_MICROS, "micros_format");
    verifyFormatAnnotation(FieldFormat.Format.TIMESTAMP_MICROS, "micros_u64");
    verifyFormatAnnotation(FieldFormat.Format.DATE, "repeated_date");
    verifyFormatAnnotation(FieldFormat.Format.DATE_DECIMAL, "repeated_date_decimal");
    verifyFormatAnnotation(FieldFormat.Format.TIMESTAMP_SECONDS, "repeated_seconds");
    verifyFormatAnnotation(FieldFormat.Format.TIMESTAMP_MILLIS, "repeated_millis");
    verifyFormatAnnotation(FieldFormat.Format.TIMESTAMP_MICROS, "repeated_micros");
    verifyFormatAnnotation(FieldFormat.Format.TIMESTAMP_SECONDS, "repeated_seconds_format");
    verifyFormatAnnotation(FieldFormat.Format.TIMESTAMP_MILLIS, "repeated_millis_format");
    verifyFormatAnnotation(FieldFormat.Format.TIMESTAMP_MICROS, "repeated_micros_format");
  }

  @Test
  public void testEquals() {
    TypeFactory factory = TypeFactory.nonUniqueNames();

    ProtoType proto1 = factory.createProtoType(TypeProto.class);
    ProtoType proto2 =
        factory.createProtoType(
            getDescriptorPoolWithTypeProtoAndTypeKind()
                .findMessageTypeByName("googlesql.TypeProto"));
    ProtoType proto3 = factory.createProtoType(FieldDescriptorProto.class);

    new EqualsTester().addEqualityGroup(proto1).testEquals();
    assertThat(proto1.equals(proto2)).isFalse();
    assertThat(proto1.equals(proto3)).isFalse();
    assertThat(proto2.equals(proto1)).isFalse();
    new EqualsTester().addEqualityGroup(proto2).testEquals();
    assertThat(proto2.equals(proto3)).isFalse();
    assertThat(proto3.equals(proto1)).isFalse();
    assertThat(proto3.equals(proto2)).isFalse();
    new EqualsTester().addEqualityGroup(proto3).testEquals();

    assertThat(proto1.equals(TypeFactory.createSimpleType(TypeKind.TYPE_BOOL))).isFalse();
  }

  @Test
  public void testAsProto() {
    TypeFactory factory = TypeFactory.nonUniqueNames();

    ArrayType array =
        TypeFactory.createArrayType(TypeFactory.createSimpleType(TypeKind.TYPE_INT32));
    EnumType enumType = factory.createEnumType(TypeKind.class);
    ProtoType proto = factory.createProtoType(TypeProto.class);
    List<StructType.StructField> fields = new ArrayList<>();
    fields.add(new StructType.StructField("", TypeFactory.createSimpleType(TypeKind.TYPE_STRING)));
    fields.add(new StructType.StructField("a", TypeFactory.createSimpleType(TypeKind.TYPE_INT32)));
    StructType struct = TypeFactory.createStructType(fields);

    assertThat(proto.asProto()).isEqualTo(proto);
    assertThat(array.asProto()).isNull();
    assertThat(enumType.asProto()).isNull();
    assertThat(struct.asProto()).isNull();
    assertThat(TypeFactory.createSimpleType(TypeKind.TYPE_INT32).asProto()).isNull();
  }

  @Test
  public void testClassAndProtoSize() {
    assertWithMessage(
            "The number of fields of ProtoTypeProto has changed, "
                + "please also update the serialization code accordingly.")
        .that(ProtoTypeProto.getDescriptor().getFields())
        .hasSize(4);
    assertWithMessage(
            "The number of fields in ProtoType class has changed, "
                + "please also update the proto and serialization code accordingly.")
        .that(TestUtil.getNonStaticFieldCount(ProtoType.class))
        .isEqualTo(2);
  }
}
