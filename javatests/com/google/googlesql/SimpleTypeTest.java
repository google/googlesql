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

import com.google.googlesql.GoogleSQLOptions.ProductMode;
import com.google.googlesql.GoogleSQLType.TypeKind;

import org.junit.Test;
import org.junit.runner.RunWith;
import org.junit.runners.JUnit4;

@RunWith(JUnit4.class)

public class SimpleTypeTest {

  @Test
  public void testComponentTypes() {
    assertThat(TypeFactory.createSimpleType(TypeKind.TYPE_INT32).componentTypes()).isEmpty();
  }

  @Test
  public void testSerializationAndDeserialization() {
    for (TypeKind kind : TypeKind.values()) {
      if (TypeFactory.isSimpleType(kind)) {
        SimpleType type = TypeFactory.createSimpleType(kind);
        checkTypeSerializationAndDeserialization(type);
      }
    }
  }

  @Test
  public void testSerializable() {
    for (TypeKind kind : TypeKind.values()) {
      if (TypeFactory.isSimpleType(kind)) {
        SimpleType type = TypeFactory.createSimpleType(kind);
        checkSerializable(type);
      }
    }
  }

  @Test
  public void testEquivalent() {
    SimpleType type1 = TypeFactory.createSimpleType(TypeKind.TYPE_BOOL);
    SimpleType type2 = TypeFactory.createSimpleType(TypeKind.TYPE_BOOL);
    SimpleType type3 = TypeFactory.createSimpleType(TypeKind.TYPE_DOUBLE);

    assertThat(type1.equivalent(type1)).isTrue();
    assertThat(type1.equivalent(type2)).isTrue();
    assertThat(type1.equivalent(type3)).isFalse();
    assertThat(type2.equivalent(type1)).isTrue();
    assertThat(type2.equivalent(type2)).isTrue();
    assertThat(type2.equivalent(type3)).isFalse();
    assertThat(type3.equivalent(type1)).isFalse();
    assertThat(type3.equivalent(type2)).isFalse();
    assertThat(type3.equivalent(type3)).isTrue();

    assertThat(type1.equivalent(TypeFactory.createArrayType(type1))).isFalse();
  }

  @Test
  public void testTypeName() {
    SimpleType type1 = TypeFactory.createSimpleType(TypeKind.TYPE_INT32);
    SimpleType type2 = TypeFactory.createSimpleType(TypeKind.TYPE_INT64);
    SimpleType type3 = TypeFactory.createSimpleType(TypeKind.TYPE_UINT32);
    SimpleType type4 = TypeFactory.createSimpleType(TypeKind.TYPE_UINT64);
    SimpleType type5 = TypeFactory.createSimpleType(TypeKind.TYPE_BOOL);
    SimpleType type6 = TypeFactory.createSimpleType(TypeKind.TYPE_FLOAT);
    SimpleType type7 = TypeFactory.createSimpleType(TypeKind.TYPE_DOUBLE);
    SimpleType type8 = TypeFactory.createSimpleType(TypeKind.TYPE_STRING);
    SimpleType type9 = TypeFactory.createSimpleType(TypeKind.TYPE_BYTES);
    SimpleType type10 = TypeFactory.createSimpleType(TypeKind.TYPE_DATE);
    SimpleType type11 = TypeFactory.createSimpleType(TypeKind.TYPE_TIMESTAMP);

    assertThat(type1.typeName()).isEqualTo("INT32");
    assertThat(type2.typeName()).isEqualTo("INT64");
    assertThat(type3.typeName()).isEqualTo("UINT32");
    assertThat(type4.typeName()).isEqualTo("UINT64");
    assertThat(type5.typeName()).isEqualTo("BOOL");
    assertThat(type6.typeName()).isEqualTo("FLOAT");
    assertThat(type7.typeName()).isEqualTo("DOUBLE");
    assertThat(type8.typeName()).isEqualTo("STRING");
    assertThat(type9.typeName()).isEqualTo("BYTES");
    assertThat(type10.typeName()).isEqualTo("DATE");
    assertThat(type11.typeName()).isEqualTo("TIMESTAMP");

    assertThat(type1.typeName(ProductMode.PRODUCT_INTERNAL)).isEqualTo("INT32");
    assertThat(type2.typeName(ProductMode.PRODUCT_INTERNAL)).isEqualTo("INT64");
    assertThat(type3.typeName(ProductMode.PRODUCT_INTERNAL)).isEqualTo("UINT32");
    assertThat(type4.typeName(ProductMode.PRODUCT_INTERNAL)).isEqualTo("UINT64");
    assertThat(type5.typeName(ProductMode.PRODUCT_INTERNAL)).isEqualTo("BOOL");
    assertThat(type6.typeName(ProductMode.PRODUCT_INTERNAL)).isEqualTo("FLOAT");
    assertThat(type7.typeName(ProductMode.PRODUCT_INTERNAL)).isEqualTo("DOUBLE");
    assertThat(type8.typeName(ProductMode.PRODUCT_INTERNAL)).isEqualTo("STRING");
    assertThat(type9.typeName(ProductMode.PRODUCT_INTERNAL)).isEqualTo("BYTES");
    assertThat(type10.typeName(ProductMode.PRODUCT_INTERNAL)).isEqualTo("DATE");
    assertThat(type11.typeName(ProductMode.PRODUCT_INTERNAL)).isEqualTo("TIMESTAMP");

    assertThat(type1.typeName(ProductMode.PRODUCT_EXTERNAL)).isEqualTo("INT32");
    assertThat(type2.typeName(ProductMode.PRODUCT_EXTERNAL)).isEqualTo("INT64");
    assertThat(type3.typeName(ProductMode.PRODUCT_EXTERNAL)).isEqualTo("UINT32");
    assertThat(type4.typeName(ProductMode.PRODUCT_EXTERNAL)).isEqualTo("UINT64");
    assertThat(type5.typeName(ProductMode.PRODUCT_EXTERNAL)).isEqualTo("BOOL");
    assertThat(type6.typeName(ProductMode.PRODUCT_EXTERNAL)).isEqualTo("FLOAT");
    assertThat(type7.typeName(ProductMode.PRODUCT_EXTERNAL)).isEqualTo("FLOAT64");
    assertThat(type8.typeName(ProductMode.PRODUCT_EXTERNAL)).isEqualTo("STRING");
    assertThat(type9.typeName(ProductMode.PRODUCT_EXTERNAL)).isEqualTo("BYTES");
    assertThat(type10.typeName(ProductMode.PRODUCT_EXTERNAL)).isEqualTo("DATE");
    assertThat(type11.typeName(ProductMode.PRODUCT_EXTERNAL)).isEqualTo("TIMESTAMP");
  }

  @Test
  public void testDebugString() {
    SimpleType type1 = TypeFactory.createSimpleType(TypeKind.TYPE_INT32);
    SimpleType type2 = TypeFactory.createSimpleType(TypeKind.TYPE_INT64);
    SimpleType type3 = TypeFactory.createSimpleType(TypeKind.TYPE_UINT32);
    SimpleType type4 = TypeFactory.createSimpleType(TypeKind.TYPE_UINT64);
    SimpleType type5 = TypeFactory.createSimpleType(TypeKind.TYPE_BOOL);
    SimpleType type6 = TypeFactory.createSimpleType(TypeKind.TYPE_FLOAT);
    SimpleType type7 = TypeFactory.createSimpleType(TypeKind.TYPE_DOUBLE);
    SimpleType type8 = TypeFactory.createSimpleType(TypeKind.TYPE_STRING);
    SimpleType type9 = TypeFactory.createSimpleType(TypeKind.TYPE_BYTES);
    SimpleType type10 = TypeFactory.createSimpleType(TypeKind.TYPE_DATE);
    SimpleType type11 = TypeFactory.createSimpleType(TypeKind.TYPE_TIMESTAMP);

    assertThat(type1.debugString(false)).isEqualTo("INT32");
    assertThat(type1.debugString(true)).isEqualTo("INT32");
    assertThat(type2.debugString(false)).isEqualTo("INT64");
    assertThat(type2.debugString(true)).isEqualTo("INT64");
    assertThat(type3.debugString(false)).isEqualTo("UINT32");
    assertThat(type3.debugString(true)).isEqualTo("UINT32");
    assertThat(type4.debugString(false)).isEqualTo("UINT64");
    assertThat(type4.debugString(true)).isEqualTo("UINT64");
    assertThat(type5.debugString(false)).isEqualTo("BOOL");
    assertThat(type5.debugString(true)).isEqualTo("BOOL");
    assertThat(type6.debugString(false)).isEqualTo("FLOAT");
    assertThat(type6.debugString(true)).isEqualTo("FLOAT");
    assertThat(type7.debugString(false)).isEqualTo("DOUBLE");
    assertThat(type7.debugString(true)).isEqualTo("DOUBLE");
    assertThat(type8.debugString(false)).isEqualTo("STRING");
    assertThat(type8.debugString(true)).isEqualTo("STRING");
    assertThat(type9.debugString(false)).isEqualTo("BYTES");
    assertThat(type9.debugString(true)).isEqualTo("BYTES");
    assertThat(type10.debugString(false)).isEqualTo("DATE");
    assertThat(type10.debugString(true)).isEqualTo("DATE");
    assertThat(type11.debugString(false)).isEqualTo("TIMESTAMP");
    assertThat(type11.debugString(true)).isEqualTo("TIMESTAMP");
  }

  @Test
  public void testToString() {
    SimpleType type1 = TypeFactory.createSimpleType(TypeKind.TYPE_INT32);
    SimpleType type2 = TypeFactory.createSimpleType(TypeKind.TYPE_INT64);
    SimpleType type3 = TypeFactory.createSimpleType(TypeKind.TYPE_UINT32);
    SimpleType type4 = TypeFactory.createSimpleType(TypeKind.TYPE_UINT64);
    SimpleType type5 = TypeFactory.createSimpleType(TypeKind.TYPE_BOOL);
    SimpleType type6 = TypeFactory.createSimpleType(TypeKind.TYPE_FLOAT);
    SimpleType type7 = TypeFactory.createSimpleType(TypeKind.TYPE_DOUBLE);
    SimpleType type8 = TypeFactory.createSimpleType(TypeKind.TYPE_STRING);
    SimpleType type9 = TypeFactory.createSimpleType(TypeKind.TYPE_BYTES);
    SimpleType type10 = TypeFactory.createSimpleType(TypeKind.TYPE_DATE);
    SimpleType type11 = TypeFactory.createSimpleType(TypeKind.TYPE_TIMESTAMP);

    assertThat(type1.toString()).isEqualTo("INT32");
    assertThat(type2.toString()).isEqualTo("INT64");
    assertThat(type3.toString()).isEqualTo("UINT32");
    assertThat(type4.toString()).isEqualTo("UINT64");
    assertThat(type5.toString()).isEqualTo("BOOL");
    assertThat(type6.toString()).isEqualTo("FLOAT");
    assertThat(type7.toString()).isEqualTo("DOUBLE");
    assertThat(type8.toString()).isEqualTo("STRING");
    assertThat(type9.toString()).isEqualTo("BYTES");
    assertThat(type10.toString()).isEqualTo("DATE");
    assertThat(type11.toString()).isEqualTo("TIMESTAMP");
  }

  @Test
  public void testClassAndProtoSize() {
    assertWithMessage(
            "The number of fields in SimpleType class has changed, "
                + "please also update the proto and serialization code accordingly.")
        .that(TestUtil.getNonStaticFieldCount(SimpleType.class))
        .isEqualTo(0);
  }
}
