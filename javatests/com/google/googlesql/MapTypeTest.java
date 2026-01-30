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

import com.google.common.testing.EqualsTester;
import com.google.googlesql.GoogleSQLDescriptorPool.GeneratedDescriptorPool;
import com.google.googlesql.GoogleSQLOptions.ProductMode;
import com.google.googlesql.GoogleSQLType.MapTypeProto;
import com.google.googlesql.GoogleSQLType.TypeKind;
import com.google.googlesql.GoogleSQLType.TypeProto;
import java.util.ArrayList;
import org.junit.Test;
import org.junit.runner.RunWith;
import org.junit.runners.JUnit4;

@RunWith(JUnit4.class)
public class MapTypeTest {

  @Test
  public void testComponentTypes() {
    assertThat(
            TypeFactory.createMapType(
                    TypeFactory.createSimpleType(TypeKind.TYPE_INT32),
                    TypeFactory.createSimpleType(TypeKind.TYPE_BOOL))
                .componentTypes())
        .containsExactly(
            TypeFactory.createSimpleType(TypeKind.TYPE_INT32),
            TypeFactory.createSimpleType(TypeKind.TYPE_BOOL));
  }

  @Test
  public void testSerializationAndDeserialization() {
    TypeFactory factory = TypeFactory.nonUniqueNames();
    checkTypeSerializationAndDeserialization(
        TypeFactory.createMapType(
            TypeFactory.createSimpleType(TypeKind.TYPE_INT32),
            TypeFactory.createSimpleType(TypeKind.TYPE_BOOL)));
    checkTypeSerializationAndDeserialization(
        TypeFactory.createMapType(
            TypeFactory.createSimpleType(TypeKind.TYPE_STRING),
            TypeFactory.createSimpleType(TypeKind.TYPE_STRING)));
    ProtoType protoType = factory.createProtoType(TypeProto.class);
    checkTypeSerializationAndDeserialization(
        TypeFactory.createMapType(protoType, TypeFactory.createSimpleType(TypeKind.TYPE_STRING)));

    Type arrayType =
        TypeFactory.createArrayType(TypeFactory.createSimpleType(TypeKind.TYPE_STRING));
    Type mapType =
        TypeFactory.createMapType(arrayType, TypeFactory.createSimpleType(TypeKind.TYPE_BOOL));
    checkTypeSerializationAndDeserialization(TypeFactory.createMapType(mapType, arrayType));
  }

  @Test
  public void testSerializable() {
    TypeFactory factory = TypeFactory.nonUniqueNames();
    checkSerializable(
        TypeFactory.createMapType(
            TypeFactory.createSimpleType(TypeKind.TYPE_INT32),
            TypeFactory.createSimpleType(TypeKind.TYPE_BOOL)));
    ProtoType protoType = factory.createProtoType(TypeProto.class);
    assertThat(
            GeneratedDescriptorPool.getGeneratedPool()
                .findMessageTypeByName(TypeProto.getDescriptor().getFullName()))
        .isNotNull();

    checkSerializable(
        TypeFactory.createMapType(protoType, TypeFactory.createSimpleType(TypeKind.TYPE_STRING)));
  }

  @Test
  public void testEquivalent() {
    MapType map1 =
        TypeFactory.createMapType(
            TypeFactory.createSimpleType(TypeKind.TYPE_INT32),
            TypeFactory.createSimpleType(TypeKind.TYPE_BOOL));
    MapType map2 =
        TypeFactory.createMapType(
            TypeFactory.createSimpleType(TypeKind.TYPE_INT32),
            TypeFactory.createSimpleType(TypeKind.TYPE_BOOL));
    MapType map3 =
        TypeFactory.createMapType(
            TypeFactory.createSimpleType(TypeKind.TYPE_INT64),
            TypeFactory.createSimpleType(TypeKind.TYPE_INT64));

    assertThat(map1.equivalent(map1)).isTrue();
    assertThat(map1.equivalent(map2)).isTrue();
    assertThat(map1.equivalent(map3)).isFalse();
    assertThat(map2.equivalent(map1)).isTrue();
    assertThat(map2.equivalent(map2)).isTrue();
    assertThat(map2.equivalent(map3)).isFalse();
    assertThat(map3.equivalent(map1)).isFalse();
    assertThat(map3.equivalent(map2)).isFalse();
    assertThat(map3.equivalent(map3)).isTrue();

    assertThat(map1.equivalent(map1.getKeyType())).isFalse();
    assertThat(map1.equivalent(map1.getValueType())).isFalse();
  }

  @Test
  public void testEquals() {
    MapType mapSimple1 =
        TypeFactory.createMapType(
            TypeFactory.createSimpleType(TypeKind.TYPE_INT32),
            TypeFactory.createSimpleType(TypeKind.TYPE_BOOL));
    MapType mapSimple2 =
        TypeFactory.createMapType(
            TypeFactory.createSimpleType(TypeKind.TYPE_INT32),
            TypeFactory.createSimpleType(TypeKind.TYPE_BOOL));

    Type arrayType =
        TypeFactory.createArrayType(TypeFactory.createSimpleType(TypeKind.TYPE_STRING));
    Type mapType =
        TypeFactory.createMapType(arrayType, TypeFactory.createSimpleType(TypeKind.TYPE_BOOL));
    MapType mapMapArray1 = TypeFactory.createMapType(mapType, arrayType);
    MapType mapMapArray2 = TypeFactory.createMapType(mapType, arrayType);

    new EqualsTester()
        .addEqualityGroup(mapSimple1, mapSimple2)
        .addEqualityGroup(mapSimple1.getKeyType())
        .addEqualityGroup(mapSimple1.getValueType())
        .addEqualityGroup(mapMapArray1, mapMapArray2)
        .addEqualityGroup(mapMapArray1.getKeyType())
        .addEqualityGroup(mapMapArray1.getValueType())
        .testEquals();
  }

  @Test
  public void testDebugString() {
    TypeFactory factory = TypeFactory.nonUniqueNames();

    MapType map1 =
        TypeFactory.createMapType(
            TypeFactory.createSimpleType(TypeKind.TYPE_DOUBLE),
            TypeFactory.createSimpleType(TypeKind.TYPE_DOUBLE));
    MapType map2 =
        TypeFactory.createMapType(
            TypeFactory.createSimpleType(TypeKind.TYPE_INT64),
            TypeFactory.createSimpleType(TypeKind.TYPE_INT64));
    MapType map3 =
        TypeFactory.createMapType(
            factory.createEnumType(TypeKind.class),
            TypeFactory.createSimpleType(TypeKind.TYPE_INT64));
    MapType map4 =
        TypeFactory.createMapType(
            TypeFactory.createArrayType(factory.createProtoType(TypeProto.class)),
            TypeFactory.createSimpleType(TypeKind.TYPE_INT64));

    assertThat(map1.debugString(false)).isEqualTo("MAP<DOUBLE, DOUBLE>");
    assertThat(map2.debugString(false)).isEqualTo("MAP<INT64, INT64>");
    assertThat(map3.debugString(false)).isEqualTo("MAP<ENUM<googlesql.TypeKind>, INT64>");
    assertThat(map4.debugString(false)).isEqualTo("MAP<ARRAY<PROTO<googlesql.TypeProto>>, INT64>");

    assertThat(map1.debugString(true)).isEqualTo("MAP<DOUBLE, DOUBLE>");
    assertThat(map2.debugString(true)).isEqualTo("MAP<INT64, INT64>");

    String typeProtoPath = "googlesql/public/type.proto";
    assertThat(map3.debugString(true))
        .isEqualTo(
            "MAP<ENUM<googlesql.TypeKind, file name: " + typeProtoPath + ", <TypeKind>>, INT64>");
    assertThat(map4.debugString(true))
        .isEqualTo(
            "MAP<ARRAY<PROTO<googlesql.TypeProto, file name: "
                + typeProtoPath
                + ", <TypeProto>>>, INT64>");
  }

  @Test
  public void testToString() {
    TypeFactory factory = TypeFactory.nonUniqueNames();

    MapType map1 =
        TypeFactory.createMapType(
            TypeFactory.createSimpleType(TypeKind.TYPE_DOUBLE),
            TypeFactory.createSimpleType(TypeKind.TYPE_DOUBLE));
    MapType map2 =
        TypeFactory.createMapType(
            TypeFactory.createSimpleType(TypeKind.TYPE_INT64),
            TypeFactory.createSimpleType(TypeKind.TYPE_INT64));
    MapType map3 =
        TypeFactory.createMapType(
            factory.createEnumType(TypeKind.class),
            TypeFactory.createSimpleType(TypeKind.TYPE_INT64));
    MapType map4 =
        TypeFactory.createMapType(
            TypeFactory.createArrayType(factory.createProtoType(TypeProto.class)),
            TypeFactory.createSimpleType(TypeKind.TYPE_INT64));

    assertThat(map1.toString()).isEqualTo("MAP<DOUBLE, DOUBLE>");
    assertThat(map2.toString()).isEqualTo("MAP<INT64, INT64>");
    assertThat(map3.toString()).isEqualTo("MAP<ENUM<googlesql.TypeKind>, INT64>");
    assertThat(map4.toString()).isEqualTo("MAP<ARRAY<PROTO<googlesql.TypeProto>>, INT64>");
  }

  @Test
  public void testAsMap() {
    TypeFactory factory = TypeFactory.nonUniqueNames();

    MapType map =
        TypeFactory.createMapType(
            TypeFactory.createSimpleType(TypeKind.TYPE_DOUBLE),
            TypeFactory.createSimpleType(TypeKind.TYPE_DOUBLE));
    ArrayType array =
        TypeFactory.createArrayType(TypeFactory.createSimpleType(TypeKind.TYPE_INT32));
    EnumType enumType = factory.createEnumType(TypeKind.class);
    ProtoType proto = factory.createProtoType(TypeProto.class);
    ArrayList<StructType.StructField> fields = new ArrayList<>();
    fields.add(new StructType.StructField("", TypeFactory.createSimpleType(TypeKind.TYPE_STRING)));
    fields.add(new StructType.StructField("a", TypeFactory.createSimpleType(TypeKind.TYPE_INT32)));
    StructType struct = TypeFactory.createStructType(fields);

    assertThat(map.asMap()).isEqualTo(map);
    assertThat(array.asMap()).isNull();
    assertThat(enumType.asMap()).isNull();
    assertThat(proto.asMap()).isNull();
    assertThat(struct.asMap()).isNull();
    assertThat(TypeFactory.createSimpleType(TypeKind.TYPE_INT32).asMap()).isNull();
  }

  @Test
  public void testIsMap() {
    TypeFactory factory = TypeFactory.nonUniqueNames();

    MapType map =
        TypeFactory.createMapType(
            TypeFactory.createSimpleType(TypeKind.TYPE_DOUBLE),
            TypeFactory.createSimpleType(TypeKind.TYPE_DOUBLE));
    ArrayType array =
        TypeFactory.createArrayType(TypeFactory.createSimpleType(TypeKind.TYPE_INT32));
    EnumType enumType = factory.createEnumType(TypeKind.class);
    ProtoType proto = factory.createProtoType(TypeProto.class);
    ArrayList<StructType.StructField> fields = new ArrayList<>();
    fields.add(new StructType.StructField("", TypeFactory.createSimpleType(TypeKind.TYPE_STRING)));
    fields.add(new StructType.StructField("a", TypeFactory.createSimpleType(TypeKind.TYPE_INT32)));
    StructType struct = TypeFactory.createStructType(fields);

    assertThat(map.isMap()).isTrue();
    assertThat(array.isMap()).isFalse();
    assertThat(enumType.isMap()).isFalse();
    assertThat(proto.isMap()).isFalse();
    assertThat(struct.isMap()).isFalse();
    assertThat(TypeFactory.createSimpleType(TypeKind.TYPE_INT32).isMap()).isFalse();
  }

  @Test
  public void testClassAndProtoSize() {
    assertWithMessage(
            "The number of fields of MapTypeProto has changed, please also update the "
                + "serialization code accordingly.")
        .that(MapTypeProto.getDescriptor().getFields())
        .hasSize(2);
    assertWithMessage(
            "The number of fields in MapType class has changed, please also update the proto and "
                + "serialization code accordingly.")
        .that(TestUtil.getNonStaticFieldCount(MapType.class))
        .isEqualTo(2);
  }
}
