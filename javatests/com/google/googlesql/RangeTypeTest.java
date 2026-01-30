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
import static org.junit.Assert.assertThrows;

import com.google.common.testing.EqualsTester;
import com.google.googlesql.GoogleSQLOptions.ProductMode;
import com.google.googlesql.GoogleSQLType.RangeTypeProto;
import com.google.googlesql.GoogleSQLType.TypeKind;
import com.google.googlesql.GoogleSQLType.TypeProto;
import java.util.ArrayList;
import org.junit.Test;
import org.junit.runner.RunWith;
import org.junit.runners.JUnit4;

@RunWith(JUnit4.class)
public class RangeTypeTest {

  @Test
  public void testComponentTypes() {
    assertThat(
            TypeFactory.createRangeType(TypeFactory.createSimpleType(TypeKind.TYPE_DATE))
                .componentTypes())
        .containsExactly(TypeFactory.createSimpleType(TypeKind.TYPE_DATE));
  }

  @Test
  public void testSerializationAndDeserialization() {
    checkTypeSerializationAndDeserialization(
        TypeFactory.createRangeType(TypeFactory.createSimpleType(TypeKind.TYPE_DATE)));
    checkTypeSerializationAndDeserialization(
        TypeFactory.createRangeType(TypeFactory.createSimpleType(TypeKind.TYPE_DATETIME)));
    checkTypeSerializationAndDeserialization(
        TypeFactory.createRangeType(TypeFactory.createSimpleType(TypeKind.TYPE_TIMESTAMP)));
  }

  @Test
  public void testSerializable() {
    checkSerializable(
        TypeFactory.createRangeType(TypeFactory.createSimpleType(TypeKind.TYPE_DATE)));
    checkSerializable(
        TypeFactory.createRangeType(TypeFactory.createSimpleType(TypeKind.TYPE_DATETIME)));
    checkSerializable(
        TypeFactory.createRangeType(TypeFactory.createSimpleType(TypeKind.TYPE_TIMESTAMP)));
  }

  @Test
  public void testIsValidElementType() {
    assertThat(RangeType.isValidElementType(TypeKind.TYPE_DATE)).isTrue();
    assertThat(RangeType.isValidElementType(TypeKind.TYPE_DATETIME)).isTrue();
    assertThat(RangeType.isValidElementType(TypeKind.TYPE_TIMESTAMP)).isTrue();

    assertThat(RangeType.isValidElementType(TypeKind.TYPE_STRING)).isFalse();
  }

  @Test
  public void testInvalidRangeElementTypeThrowsError() {
    IllegalArgumentException exception =
        assertThrows(
            IllegalArgumentException.class,
            () -> TypeFactory.createRangeType(TypeFactory.createSimpleType(TypeKind.TYPE_INT32)));
    assertThat(exception).hasMessageThat().isEqualTo("Invalid range element type: TYPE_INT32");
  }

  @Test
  public void testEquivalent() {
    RangeType dateRange1 =
        TypeFactory.createRangeType(TypeFactory.createSimpleType(TypeKind.TYPE_DATE));
    RangeType dateRange2 =
        TypeFactory.createRangeType(TypeFactory.createSimpleType(TypeKind.TYPE_DATE));
    RangeType datetimeRange1 =
        TypeFactory.createRangeType(TypeFactory.createSimpleType(TypeKind.TYPE_DATETIME));
    RangeType datetimeRange2 =
        TypeFactory.createRangeType(TypeFactory.createSimpleType(TypeKind.TYPE_DATETIME));
    RangeType timestampRange1 =
        TypeFactory.createRangeType(TypeFactory.createSimpleType(TypeKind.TYPE_TIMESTAMP));
    RangeType timestampRange2 =
        TypeFactory.createRangeType(TypeFactory.createSimpleType(TypeKind.TYPE_TIMESTAMP));

    assertThat(dateRange1.equivalent(dateRange1)).isTrue();
    assertThat(dateRange1.equivalent(dateRange2)).isTrue();
    assertThat(dateRange2.equivalent(dateRange1)).isTrue();
    assertThat(dateRange1.equivalent(datetimeRange1)).isFalse();
    assertThat(dateRange1.equivalent(dateRange1.getElementType())).isFalse();

    assertThat(datetimeRange1.equivalent(datetimeRange1)).isTrue();
    assertThat(datetimeRange1.equivalent(datetimeRange2)).isTrue();
    assertThat(datetimeRange2.equivalent(datetimeRange1)).isTrue();
    assertThat(datetimeRange1.equivalent(timestampRange1)).isFalse();
    assertThat(datetimeRange1.equivalent(datetimeRange1.getElementType())).isFalse();

    assertThat(timestampRange1.equivalent(timestampRange1)).isTrue();
    assertThat(timestampRange1.equivalent(timestampRange2)).isTrue();
    assertThat(timestampRange2.equivalent(timestampRange1)).isTrue();
    assertThat(timestampRange1.equivalent(dateRange1)).isFalse();
    assertThat(timestampRange1.equivalent(timestampRange1.getElementType())).isFalse();
  }

  @Test
  public void testEquals() {
    RangeType dateRange1 =
        TypeFactory.createRangeType(TypeFactory.createSimpleType(TypeKind.TYPE_DATE));
    RangeType dateRange2 =
        TypeFactory.createRangeType(TypeFactory.createSimpleType(TypeKind.TYPE_DATE));
    RangeType datetimeRange1 =
        TypeFactory.createRangeType(TypeFactory.createSimpleType(TypeKind.TYPE_DATETIME));
    RangeType datetimeRange2 =
        TypeFactory.createRangeType(TypeFactory.createSimpleType(TypeKind.TYPE_DATETIME));
    RangeType timestampRange1 =
        TypeFactory.createRangeType(TypeFactory.createSimpleType(TypeKind.TYPE_TIMESTAMP));
    RangeType timestampRange2 =
        TypeFactory.createRangeType(TypeFactory.createSimpleType(TypeKind.TYPE_TIMESTAMP));

    new EqualsTester()
        .addEqualityGroup(dateRange1, dateRange2)
        .addEqualityGroup(dateRange1.getElementType())
        .addEqualityGroup(datetimeRange1, datetimeRange2)
        .addEqualityGroup(datetimeRange1.getElementType())
        .addEqualityGroup(timestampRange1, timestampRange2)
        .addEqualityGroup(timestampRange1.getElementType())
        .testEquals();
  }

  @Test
  public void testDebugString() {
    RangeType dateRange =
        TypeFactory.createRangeType(TypeFactory.createSimpleType(TypeKind.TYPE_DATE));
    RangeType datetimeRange =
        TypeFactory.createRangeType(TypeFactory.createSimpleType(TypeKind.TYPE_DATETIME));
    RangeType timestampRange =
        TypeFactory.createRangeType(TypeFactory.createSimpleType(TypeKind.TYPE_TIMESTAMP));

    assertThat(dateRange.debugString(false)).isEqualTo("RANGE<DATE>");
    assertThat(datetimeRange.debugString(false)).isEqualTo("RANGE<DATETIME>");
    assertThat(timestampRange.debugString(false)).isEqualTo("RANGE<TIMESTAMP>");

    assertThat(dateRange.debugString(true)).isEqualTo("RANGE<DATE>");
    assertThat(datetimeRange.debugString(true)).isEqualTo("RANGE<DATETIME>");
    assertThat(timestampRange.debugString(true)).isEqualTo("RANGE<TIMESTAMP>");
  }

  @Test
  public void testToString() {
    RangeType dateRange =
        TypeFactory.createRangeType(TypeFactory.createSimpleType(TypeKind.TYPE_DATE));
    RangeType datetimeRange =
        TypeFactory.createRangeType(TypeFactory.createSimpleType(TypeKind.TYPE_DATETIME));
    RangeType timestampRange =
        TypeFactory.createRangeType(TypeFactory.createSimpleType(TypeKind.TYPE_TIMESTAMP));

    assertThat(dateRange.toString()).isEqualTo("RANGE<DATE>");
    assertThat(datetimeRange.toString()).isEqualTo("RANGE<DATETIME>");
    assertThat(timestampRange.toString()).isEqualTo("RANGE<TIMESTAMP>");
  }

  @Test
  public void testAsRange() {
    TypeFactory factory = TypeFactory.nonUniqueNames();

    RangeType range = TypeFactory.createRangeType(TypeFactory.createSimpleType(TypeKind.TYPE_DATE));
    ArrayType array =
        TypeFactory.createArrayType(TypeFactory.createSimpleType(TypeKind.TYPE_INT32));
    EnumType enumType = factory.createEnumType(TypeKind.class);
    ProtoType proto = factory.createProtoType(TypeProto.class);
    ArrayList<StructType.StructField> fields = new ArrayList<>();
    fields.add(new StructType.StructField("", TypeFactory.createSimpleType(TypeKind.TYPE_STRING)));
    fields.add(new StructType.StructField("a", TypeFactory.createSimpleType(TypeKind.TYPE_INT32)));
    StructType struct = TypeFactory.createStructType(fields);

    assertThat(range.asRange()).isEqualTo(range);
    assertThat(array.asRange()).isNull();
    assertThat(enumType.asRange()).isNull();
    assertThat(proto.asRange()).isNull();
    assertThat(struct.asRange()).isNull();
    assertThat(TypeFactory.createSimpleType(TypeKind.TYPE_INT32).asRange()).isNull();
  }

  @Test
  public void testIsRange() {
    TypeFactory factory = TypeFactory.nonUniqueNames();

    RangeType range = TypeFactory.createRangeType(TypeFactory.createSimpleType(TypeKind.TYPE_DATE));
    ArrayType array =
        TypeFactory.createArrayType(TypeFactory.createSimpleType(TypeKind.TYPE_INT32));
    EnumType enumType = factory.createEnumType(TypeKind.class);
    ProtoType proto = factory.createProtoType(TypeProto.class);
    ArrayList<StructType.StructField> fields = new ArrayList<>();
    fields.add(new StructType.StructField("", TypeFactory.createSimpleType(TypeKind.TYPE_STRING)));
    fields.add(new StructType.StructField("a", TypeFactory.createSimpleType(TypeKind.TYPE_INT32)));
    StructType struct = TypeFactory.createStructType(fields);

    assertThat(range.isRange()).isTrue();
    assertThat(array.isRange()).isFalse();
    assertThat(enumType.isRange()).isFalse();
    assertThat(proto.isRange()).isFalse();
    assertThat(struct.isRange()).isFalse();
    assertThat(TypeFactory.createSimpleType(TypeKind.TYPE_INT32).isRange()).isFalse();
  }

  @Test
  public void testClassAndProtoSize() {
    assertWithMessage(
            "The number of fields of RangeTypeProto has changed, please also update the "
                + "serialization code accordingly.")
        .that(RangeTypeProto.getDescriptor().getFields())
        .hasSize(1);
    assertWithMessage(
            "The number of fields in RangeType class has changed, please also update the proto and "
                + "serialization code accordingly.")
        .that(TestUtil.getNonStaticFieldCount(RangeType.class))
        .isEqualTo(1);
  }
}
