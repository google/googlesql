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

import com.google.googlesql.GoogleSQLType.TypeKind;
import com.google.googlesql.GoogleSQLType.TypeProto;

import java.util.ArrayList;
import java.util.Arrays;
import java.util.Collection;
import java.util.List;
import java.util.function.Predicate;
import org.junit.Test;
import org.junit.runner.RunWith;
import org.junit.runners.JUnit4;

@RunWith(JUnit4.class)

public class TypeTest {
  @Test
  public void isType() {
    verifyIsType(Arrays.asList(TypeKind.TYPE_BOOL), Type::isBool, "isBool");

    verifyIsType(Arrays.asList(TypeKind.TYPE_INT32), Type::isInt32, "isInt32");
    verifyIsType(Arrays.asList(TypeKind.TYPE_INT64), Type::isInt64, "isInt64");
    verifyIsType(
        Arrays.asList(TypeKind.TYPE_INT32, TypeKind.TYPE_INT64),
        Type::isSignedInteger,
        "isSignedInteger");
    verifyIsType(Arrays.asList(TypeKind.TYPE_UINT32), Type::isUint32, "isUint32");
    verifyIsType(Arrays.asList(TypeKind.TYPE_UINT64), Type::isUint64, "isUint64");
    verifyIsType(
        Arrays.asList(TypeKind.TYPE_UINT32, TypeKind.TYPE_UINT64),
        Type::isUnsignedInteger,
        "isUnsignedInteger");
    verifyIsType(
        Arrays.asList(
            TypeKind.TYPE_INT32, TypeKind.TYPE_INT64, TypeKind.TYPE_UINT32, TypeKind.TYPE_UINT64),
        Type::isInteger,
        "isInteger");
    verifyIsType(Arrays.asList(TypeKind.TYPE_FLOAT), Type::isFloat, "isFloat");
    verifyIsType(Arrays.asList(TypeKind.TYPE_DOUBLE), Type::isDouble, "isDouble");
    verifyIsType(
        Arrays.asList(TypeKind.TYPE_FLOAT, TypeKind.TYPE_DOUBLE),
        Type::isFloatingPoint,
        "isFloatingPoint");
    verifyIsType(Arrays.asList(TypeKind.TYPE_NUMERIC), Type::isNumeric, "isNumeric");
    verifyIsType(Arrays.asList(TypeKind.TYPE_BIGNUMERIC), Type::isBigNumeric, "isBigNumeric");
    verifyIsType(
        Arrays.asList(
            TypeKind.TYPE_INT32,
            TypeKind.TYPE_INT64,
            TypeKind.TYPE_UINT32,
            TypeKind.TYPE_UINT64,
            TypeKind.TYPE_FLOAT,
            TypeKind.TYPE_DOUBLE,
            TypeKind.TYPE_NUMERIC,
            TypeKind.TYPE_BIGNUMERIC),
        Type::isNumerical,
        "isNumerical");

    verifyIsType(Arrays.asList(TypeKind.TYPE_STRING), Type::isString, "isString");
    verifyIsType(Arrays.asList(TypeKind.TYPE_BYTES), Type::isBytes, "isBytes");
    verifyIsType(Arrays.asList(TypeKind.TYPE_DATE), Type::isDate, "isDate");
    verifyIsType(Arrays.asList(TypeKind.TYPE_TIMESTAMP), Type::isTimestamp, "isTimestamp");
    verifyIsType(Arrays.asList(TypeKind.TYPE_DATETIME), Type::isDatetime, "isDatetime");
    verifyIsType(Arrays.asList(TypeKind.TYPE_TIME), Type::isTime, "isTime");
    verifyIsType(Arrays.asList(TypeKind.TYPE_INTERVAL), Type::isInterval, "isInterval");
    verifyIsType(Arrays.asList(TypeKind.TYPE_UUID), Type::isUuid, "isUuid");
    verifyIsType(Arrays.asList(TypeKind.TYPE_GEOGRAPHY), Type::isGeography, "isGeography");
    verifyIsType(Arrays.asList(TypeKind.TYPE_JSON), Type::isJson, "isJson");
    verifyIsType(Arrays.asList(TypeKind.TYPE_ENUM), Type::isEnum, "isEnum");
    verifyIsType(Arrays.asList(TypeKind.TYPE_ARRAY), Type::isArray, "isArray");
    verifyIsType(Arrays.asList(TypeKind.TYPE_STRUCT), Type::isStruct, "isStruct");
    verifyIsType(Arrays.asList(TypeKind.TYPE_PROTO), Type::isProto, "isProto");
    verifyIsType(
        Arrays.asList(TypeKind.TYPE_STRUCT, TypeKind.TYPE_PROTO),
        Type::isStructOrProto,
        "isStructOrProto");
  }

  /**
   * Verifies that function <func> returns true for types having a kind listed in <kinds> and
   * returns false for types with a kind not listed in <kinds>.
   */
  private static void verifyIsType(
      Collection<TypeKind> kinds, Predicate<Type> func, String funcName) {
    ArrayList<Type> types = new ArrayList<>();

    TypeFactory factory = TypeFactory.nonUniqueNames();
    ArrayList<StructType.StructField> fields1 = new ArrayList<>();
    fields1.add(new StructType.StructField("", TypeFactory.createSimpleType(TypeKind.TYPE_STRING)));
    fields1.add(new StructType.StructField("a", TypeFactory.createSimpleType(TypeKind.TYPE_INT32)));

    types.add(TypeFactory.createArrayType(TypeFactory.createSimpleType(TypeKind.TYPE_INT32)));
    types.add(TypeFactory.createSimpleType(TypeKind.TYPE_BOOL));
    types.add(TypeFactory.createSimpleType(TypeKind.TYPE_BYTES));
    types.add(TypeFactory.createSimpleType(TypeKind.TYPE_DATE));
    types.add(TypeFactory.createSimpleType(TypeKind.TYPE_DOUBLE));
    types.add(factory.createEnumType(TypeKind.class));
    types.add(TypeFactory.createSimpleType(TypeKind.TYPE_FLOAT));
    types.add(TypeFactory.createSimpleType(TypeKind.TYPE_INT32));
    types.add(TypeFactory.createSimpleType(TypeKind.TYPE_INT64));
    types.add(factory.createProtoType(TypeProto.class));
    types.add(TypeFactory.createSimpleType(TypeKind.TYPE_STRING));
    types.add(TypeFactory.createStructType(fields1));
    types.add(TypeFactory.createSimpleType(TypeKind.TYPE_TIMESTAMP));
    types.add(TypeFactory.createSimpleType(TypeKind.TYPE_DATETIME));
    types.add(TypeFactory.createSimpleType(TypeKind.TYPE_TIME));
    types.add(TypeFactory.createSimpleType(TypeKind.TYPE_INTERVAL));
    types.add(TypeFactory.createSimpleType(TypeKind.TYPE_UINT32));
    types.add(TypeFactory.createSimpleType(TypeKind.TYPE_UINT64));
    types.add(TypeFactory.createSimpleType(TypeKind.TYPE_GEOGRAPHY));
    types.add(TypeFactory.createSimpleType(TypeKind.TYPE_NUMERIC));
    types.add(TypeFactory.createSimpleType(TypeKind.TYPE_BIGNUMERIC));
    types.add(TypeFactory.createSimpleType(TypeKind.TYPE_JSON));
    types.add(TypeFactory.createSimpleType(TypeKind.TYPE_UUID));

    for (Type type : types) {
      String typeString = type.getKind().toString();
      if (kinds.contains(type.getKind())) {
        assertWithMessage("Expected %s function to return true for type %s", funcName, typeString)
            .that(func.test(type))
            .isTrue();
      } else {
        assertWithMessage("Expected %s function to return false for type %s", funcName, typeString)
            .that(func.test(type))
            .isFalse();
      }
    }
  }

  @Test
  public void classAndProtoSize() {
    // TODO: b/350555383 - Add serialization/de-serialization code for
    // MeasureType, once MeasureType class exists.
    assertWithMessage(
            "The number of fields of TypeProto has changed, "
                + "please also update the serialization code accordingly.")
        .that(TypeProto.getDescriptor().getFields())
        .hasSize(12);
    assertWithMessage(
            "The number of fields in Type class has changed, "
                + "please also update the proto and serialization code accordingly.")
        .that(TestUtil.getNonStaticFieldCount(Type.class))
        .isEqualTo(1);
  }
}
