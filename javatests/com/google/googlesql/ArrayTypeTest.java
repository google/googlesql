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
import static org.junit.Assert.fail;

import com.google.common.testing.EqualsTester;
import com.google.googlesql.GoogleSQLDescriptorPool.GeneratedDescriptorPool;
import com.google.googlesql.GoogleSQLOptions.ProductMode;
import com.google.googlesql.GoogleSQLType.ArrayTypeProto;
import com.google.googlesql.GoogleSQLType.TypeKind;
import com.google.googlesql.GoogleSQLType.TypeProto;
import java.util.ArrayList;
import org.junit.Test;
import org.junit.runner.RunWith;
import org.junit.runners.JUnit4;

@RunWith(JUnit4.class)
public class ArrayTypeTest {

  @Test
  public void testComponentTypes() {
    assertThat(
            TypeFactory.createArrayType(TypeFactory.createSimpleType(TypeKind.TYPE_INT32))
                .componentTypes())
        .containsExactly(TypeFactory.createSimpleType(TypeKind.TYPE_INT32));
  }

  @Test
  public void testSerializationAndDeserialization() {
    TypeFactory factory = TypeFactory.nonUniqueNames();
    checkTypeSerializationAndDeserialization(
        TypeFactory.createArrayType(TypeFactory.createSimpleType(TypeKind.TYPE_INT32)));
    ProtoType protoType = factory.createProtoType(TypeProto.class);
    checkTypeSerializationAndDeserialization(TypeFactory.createArrayType(protoType));
  }

  @Test
  public void testSerializable() {
    TypeFactory factory = TypeFactory.nonUniqueNames();
    checkTypeSerializationAndDeserialization(
        TypeFactory.createArrayType(TypeFactory.createSimpleType(TypeKind.TYPE_INT32)));
    ProtoType protoType = factory.createProtoType(TypeProto.class);
    assertThat(
            GeneratedDescriptorPool.getGeneratedPool()
                .findMessageTypeByName(TypeProto.getDescriptor().getFullName()))
        .isNotNull();

    checkSerializable(TypeFactory.createArrayType(protoType));
  }

  @Test
  public void testNoArrayOfArray() {
    ArrayType array =
        TypeFactory.createArrayType(TypeFactory.createSimpleType(TypeKind.TYPE_INT32));
    try {
      TypeFactory.createArrayType(array);
      fail("Should not be able to create array of array.");
    } catch (IllegalArgumentException expected) {
    }
  }

  @Test
  public void testEquivalent() {
    ArrayType array1 =
        TypeFactory.createArrayType(TypeFactory.createSimpleType(TypeKind.TYPE_INT32));
    ArrayType array2 =
        TypeFactory.createArrayType(TypeFactory.createSimpleType(TypeKind.TYPE_INT32));
    ArrayType array3 =
        TypeFactory.createArrayType(TypeFactory.createSimpleType(TypeKind.TYPE_INT64));

    assertThat(array1.equivalent(array1)).isTrue();
    assertThat(array1.equivalent(array2)).isTrue();
    assertThat(array1.equivalent(array3)).isFalse();
    assertThat(array2.equivalent(array1)).isTrue();
    assertThat(array2.equivalent(array2)).isTrue();
    assertThat(array2.equivalent(array3)).isFalse();
    assertThat(array3.equivalent(array1)).isFalse();
    assertThat(array3.equivalent(array2)).isFalse();
    assertThat(array3.equivalent(array3)).isTrue();

    assertThat(array1.equivalent(array1.getElementType())).isFalse();
  }

  @Test
  public void testEquals() {
    ArrayType array1 =
        TypeFactory.createArrayType(TypeFactory.createSimpleType(TypeKind.TYPE_INT32));
    ArrayType array2 =
        TypeFactory.createArrayType(TypeFactory.createSimpleType(TypeKind.TYPE_INT32));
    ArrayType array3 =
        TypeFactory.createArrayType(TypeFactory.createSimpleType(TypeKind.TYPE_INT64));

    new EqualsTester().addEqualityGroup(array1).testEquals();
    assertThat(array1.equals(array2)).isTrue();
    assertThat(array1.equals(array3)).isFalse();
    assertThat(array2.equals(array1)).isTrue();
    new EqualsTester().addEqualityGroup(array2).testEquals();
    assertThat(array2.equals(array3)).isFalse();
    assertThat(array3.equals(array1)).isFalse();
    assertThat(array3.equals(array2)).isFalse();
    new EqualsTester().addEqualityGroup(array3).testEquals();

    assertThat(array1.equals(array1.getElementType())).isFalse();
  }

  @Test
  public void testDebugString() {
    TypeFactory factory = TypeFactory.nonUniqueNames();

    ArrayType array1 =
        TypeFactory.createArrayType(TypeFactory.createSimpleType(TypeKind.TYPE_INT32));
    ArrayType array2 =
        TypeFactory.createArrayType(TypeFactory.createSimpleType(TypeKind.TYPE_INT64));
    ArrayType array3 = TypeFactory.createArrayType(factory.createEnumType(TypeKind.class));
    ArrayType array4 = TypeFactory.createArrayType(factory.createProtoType(TypeProto.class));

    assertThat(array1.debugString(false)).isEqualTo("ARRAY<INT32>");
    assertThat(array2.debugString(false)).isEqualTo("ARRAY<INT64>");
    assertThat(array3.debugString(false)).isEqualTo("ARRAY<ENUM<googlesql.TypeKind>>");
    assertThat(array4.debugString(false)).isEqualTo("ARRAY<PROTO<googlesql.TypeProto>>");
    assertThat(array1.debugString(true)).isEqualTo("ARRAY<INT32>");
    assertThat(array2.debugString(true)).isEqualTo("ARRAY<INT64>");
    String typeProtoPath = "googlesql/public/type.proto";
    assertThat(array3.debugString(true))
        .isEqualTo(
            "ARRAY<ENUM<googlesql.TypeKind, file name: " + typeProtoPath + ", " + "<TypeKind>>>");
    assertThat(array4.debugString(true))
        .isEqualTo(
            "ARRAY<PROTO<googlesql.TypeProto, file name: "
                + typeProtoPath
                + ", "
                + "<TypeProto>>>");
  }

  @Test
  public void testToString() {
    TypeFactory factory = TypeFactory.nonUniqueNames();

    ArrayType array1 =
        TypeFactory.createArrayType(TypeFactory.createSimpleType(TypeKind.TYPE_INT32));
    ArrayType array2 =
        TypeFactory.createArrayType(TypeFactory.createSimpleType(TypeKind.TYPE_INT64));
    ArrayType array3 = TypeFactory.createArrayType(factory.createEnumType(TypeKind.class));
    ArrayType array4 = TypeFactory.createArrayType(factory.createProtoType(TypeProto.class));

    assertThat(array1.toString()).isEqualTo("ARRAY<INT32>");
    assertThat(array2.toString()).isEqualTo("ARRAY<INT64>");
    assertThat(array3.toString()).isEqualTo("ARRAY<ENUM<googlesql.TypeKind>>");
    assertThat(array4.toString()).isEqualTo("ARRAY<PROTO<googlesql.TypeProto>>");
  }

  @Test
  public void testAsArray() {
    TypeFactory factory = TypeFactory.nonUniqueNames();

    ArrayType array =
        TypeFactory.createArrayType(TypeFactory.createSimpleType(TypeKind.TYPE_INT32));
    EnumType enumType = factory.createEnumType(TypeKind.class);
    ProtoType proto = factory.createProtoType(TypeProto.class);
    ArrayList<StructType.StructField> fields = new ArrayList<>();
    fields.add(new StructType.StructField("", TypeFactory.createSimpleType(TypeKind.TYPE_STRING)));
    fields.add(new StructType.StructField("a", TypeFactory.createSimpleType(TypeKind.TYPE_INT32)));
    StructType struct = TypeFactory.createStructType(fields);

    assertThat(array.asArray()).isEqualTo(array);
    assertThat(enumType.asArray()).isNull();
    assertThat(proto.asArray()).isNull();
    assertThat(struct.asArray()).isNull();
    assertThat(TypeFactory.createSimpleType(TypeKind.TYPE_INT32).asArray()).isNull();
  }

  @Test
  public void testClassAndProtoSize() {
    assertWithMessage(
            "The number of fields of ArrayTypeProto has changed, please also update the "
                + "serialization code accordingly.")
        .that(ArrayTypeProto.getDescriptor().getFields())
        .hasSize(1);
    assertWithMessage(
            "The number of fields in ArrayType class has changed, please also update the proto and "
                + "serialization code accordingly.")
        .that(TestUtil.getNonStaticFieldCount(ArrayType.class))
        .isEqualTo(1);
  }
}
