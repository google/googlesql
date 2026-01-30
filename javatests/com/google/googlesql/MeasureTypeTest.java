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
import static com.google.googlesql.TypeTestBase.checkTypeSerializationAndDeserialization;

import com.google.googlesql.GoogleSQLOptions.ProductMode;
import com.google.googlesql.GoogleSQLType.MeasureTypeProto;
import com.google.googlesql.GoogleSQLType.TypeKind;
import com.google.googlesql.GoogleSQLType.TypeProto;
import java.util.ArrayList;
import org.junit.Test;
import org.junit.runner.RunWith;
import org.junit.runners.JUnit4;

@RunWith(JUnit4.class)
public final class MeasureTypeTest {

  @Test
  public void testComponentTypes() {
    assertThat(
            TypeFactory.createMeasureType(TypeFactory.createSimpleType(TypeKind.TYPE_INT64))
                .componentTypes())
        .containsExactly(TypeFactory.createSimpleType(TypeKind.TYPE_INT64));
  }

  @Test
  public void testSerializationAndDeserialization() {
    TypeFactory factory = TypeFactory.nonUniqueNames();
    checkTypeSerializationAndDeserialization(
        TypeFactory.createMeasureType(TypeFactory.createSimpleType(TypeKind.TYPE_INT64)));
    checkTypeSerializationAndDeserialization(
        TypeFactory.createMeasureType(TypeFactory.createSimpleType(TypeKind.TYPE_STRING)));
    ProtoType protoType = factory.createProtoType(TypeProto.class);
    checkTypeSerializationAndDeserialization(TypeFactory.createMeasureType(protoType));
    Type arrayType =
        TypeFactory.createArrayType(TypeFactory.createSimpleType(TypeKind.TYPE_STRING));
    checkTypeSerializationAndDeserialization(TypeFactory.createMeasureType(arrayType));
  }

  @Test
  public void testEquivalent() {
    MeasureType measure1 =
        TypeFactory.createMeasureType(TypeFactory.createSimpleType(TypeKind.TYPE_INT64));
    MeasureType measure2 =
        TypeFactory.createMeasureType(TypeFactory.createSimpleType(TypeKind.TYPE_INT64));

    assertThat(measure1.equivalent(measure1)).isTrue();
    assertThat(measure1.equivalent(measure2)).isFalse();
    assertThat(measure1.equivalent(measure2.getResultType())).isFalse();
  }

  @Test
  public void testEquals() {
    MeasureType measure1 =
        TypeFactory.createMeasureType(TypeFactory.createSimpleType(TypeKind.TYPE_INT64));
    MeasureType measure2 =
        TypeFactory.createMeasureType(TypeFactory.createSimpleType(TypeKind.TYPE_INT64));

    assertThat(measure1.equals(measure1)).isTrue();
    assertThat(measure1.equals(measure2)).isFalse();
    assertThat(measure1.equals(measure2.getResultType())).isFalse();
  }

  @Test
  public void testTypeName() {
    TypeFactory factory = TypeFactory.nonUniqueNames();

    MeasureType measureSimple =
        TypeFactory.createMeasureType(TypeFactory.createSimpleType(TypeKind.TYPE_INT64));
    assertThat(measureSimple.typeName()).isEqualTo("MEASURE<INT64>");

    MeasureType measureExternalDifferent =
        TypeFactory.createMeasureType(TypeFactory.createSimpleType(TypeKind.TYPE_DOUBLE));
    assertThat(measureExternalDifferent.typeName()).isEqualTo("MEASURE<DOUBLE>");
    assertThat(measureExternalDifferent.typeName(ProductMode.PRODUCT_INTERNAL))
        .isEqualTo("MEASURE<DOUBLE>");
    assertThat(measureExternalDifferent.typeName(ProductMode.PRODUCT_EXTERNAL))
        .isEqualTo("MEASURE<FLOAT64>");
  }

  @Test
  public void testDebugString() {
    TypeFactory factory = TypeFactory.nonUniqueNames();

    MeasureType measureSimple =
        TypeFactory.createMeasureType(TypeFactory.createSimpleType(TypeKind.TYPE_INT64));
    assertThat(measureSimple.debugString(false)).isEqualTo("MEASURE<INT64>");
    assertThat(measureSimple.debugString(true)).isEqualTo("MEASURE<INT64>");
  }

  @Test
  public void testToString() {
    TypeFactory factory = TypeFactory.nonUniqueNames();

    MeasureType measureSimple =
        TypeFactory.createMeasureType(TypeFactory.createSimpleType(TypeKind.TYPE_INT64));
    assertThat(measureSimple.toString()).isEqualTo("MEASURE<INT64>");
  }

  @Test
  public void testAsMeasure() {
    MeasureType measure =
        TypeFactory.createMeasureType(TypeFactory.createSimpleType(TypeKind.TYPE_INT64));
    ArrayType array =
        TypeFactory.createArrayType(TypeFactory.createSimpleType(TypeKind.TYPE_INT32));
    assertThat(measure.asMeasure()).isEqualTo(measure);
    assertThat(array.asMeasure()).isNull();
    assertThat(TypeFactory.createSimpleType(TypeKind.TYPE_STRING).asMeasure()).isNull();
  }

  @Test
  public void testIsMeasure() {
    MeasureType measure =
        TypeFactory.createMeasureType(TypeFactory.createSimpleType(TypeKind.TYPE_INT64));
    ArrayType array =
        TypeFactory.createArrayType(TypeFactory.createSimpleType(TypeKind.TYPE_INT32));
    assertThat(measure.isMeasure()).isTrue();
    assertThat(array.isMeasure()).isFalse();
    assertThat(TypeFactory.createSimpleType(TypeKind.TYPE_STRING).isMeasure()).isFalse();
  }

  @Test
  public void testClassAndProtoSize() {
    assertWithMessage(
            "The number of fields of MeasureTypeProto has changed, please also update the "
                + "serialization code accordingly.")
        .that(MeasureTypeProto.getDescriptor().getFields())
        .hasSize(1);
    assertWithMessage(
            "The number of fields in MeasureType class has changed, please also update the proto"
                + " and serialization code accordingly.")
        .that(TestUtil.getNonStaticFieldCount(MeasureType.class))
        .isEqualTo(1);
  }

  @Test
  public void testHashCode() {
    MeasureType measure1 =
        TypeFactory.createMeasureType(TypeFactory.createSimpleType(TypeKind.TYPE_INT64));
    MeasureType measure2 =
        TypeFactory.createMeasureType(TypeFactory.createSimpleType(TypeKind.TYPE_INT64));
    MeasureType measure3 =
        TypeFactory.createMeasureType(TypeFactory.createSimpleType(TypeKind.TYPE_STRING));
    assertThat(measure1.hashCode()).isEqualTo(measure2.hashCode());
    assertThat(measure1.hashCode()).isNotEqualTo(measure3.hashCode());
  }
}
