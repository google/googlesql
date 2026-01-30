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

import com.google.common.collect.ImmutableList;
import com.google.googlesql.GoogleSQLType.TypeKind;

/** The test constants for annotation map related tests. */
final class AnnotationMapTestConstants {

  static final String TEST_STRING_VALUE1 = "TestAnnotation1 value";
  static final SimpleValue TEST_SIMPLE_VALUE1 = SimpleValue.createString(TEST_STRING_VALUE1);
  static final String TEST_STRING_VALUE2 = "TestAnnotation2 value";
  static final SimpleValue TEST_SIMPLE_VALUE2 = SimpleValue.createString(TEST_STRING_VALUE2);

  static final int COLLATION_ANNOTATION_ID = AnnotationMap.AnnotationKind.COLLATION.getValue();
  static final int SAMPLE_ANNOTATION_ID = AnnotationMap.AnnotationKind.SAMPLE_ANNOTATION.getValue();

  static final Type STRING_TYPE = TypeFactory.createSimpleType(TypeKind.TYPE_STRING);
  static final Type INT64_TYPE = TypeFactory.createSimpleType(TypeKind.TYPE_INT64);

  // The Simple StructType STRUCT<a INT64, b STRING>
  static final StructType SIMPLE_STRUCT_TYPE =
      TypeFactory.createStructType(
          ImmutableList.of(
              new StructType.StructField("a", STRING_TYPE),
              new StructType.StructField("b", INT64_TYPE)));

  // The simple ArrayType ARRAY<STRING>
  static final ArrayType SIMPLE_ARRAY_TYPE = TypeFactory.createArrayType(STRING_TYPE);

  // The nested StructType STRUCT<a STRING, b ARRAY<STRUCT<a STRING, b INT64>>>
  static final StructType NESTED_STRUCT_TYPE =
      TypeFactory.createStructType(
          ImmutableList.of(
              new StructType.StructField("a", STRING_TYPE),
              new StructType.StructField("b", TypeFactory.createArrayType(SIMPLE_STRUCT_TYPE))));

  // The nested ArrayType ARRAY<STRUCT<a STRING, b ARRAY<INT64>>>
  static final ArrayType NESTED_ARRAY_TYPE =
      TypeFactory.createArrayType(
          TypeFactory.createStructType(
              ImmutableList.of(
                  new StructType.StructField("a", STRING_TYPE),
                  new StructType.StructField("b", TypeFactory.createArrayType(INT64_TYPE)))));

  static final AnnotationMap createStringAnnotationMap() {
    AnnotationMap annotationMap = AnnotationMap.create(STRING_TYPE);
    annotationMap.setAnnotation(COLLATION_ANNOTATION_ID, TEST_SIMPLE_VALUE1);
    return annotationMap;
  }

  static final AnnotationMap createSimpleStructAnnotationMap() {
    AnnotationMap annotationMap = AnnotationMap.create(SIMPLE_STRUCT_TYPE);

    annotationMap.setAnnotation(COLLATION_ANNOTATION_ID, TEST_SIMPLE_VALUE1);
    annotationMap.asStructMap().getField(0).setAnnotation(SAMPLE_ANNOTATION_ID, TEST_SIMPLE_VALUE2);

    return annotationMap;
  }

  static final AnnotationMap createSimpleArrayAnnotationMap() {
    AnnotationMap annotationMap = AnnotationMap.create(SIMPLE_ARRAY_TYPE);

    annotationMap.setAnnotation(COLLATION_ANNOTATION_ID, TEST_SIMPLE_VALUE1);
    annotationMap.asStructMap().getField(0).setAnnotation(SAMPLE_ANNOTATION_ID, TEST_SIMPLE_VALUE2);

    return annotationMap;
  }

  static final AnnotationMap createNestedStructAnnotationMap() {
    // STRUCT<a STRING, b ARRAY<STRUCT<a STRING, b INT64>>>
    AnnotationMap annotationMap = AnnotationMap.create(NESTED_STRUCT_TYPE);

    // Set annotation for the whole struct
    annotationMap.setAnnotation(COLLATION_ANNOTATION_ID, TEST_SIMPLE_VALUE1);
    // Set annotation for a STRING
    annotationMap.asStructMap().getField(0).setAnnotation(SAMPLE_ANNOTATION_ID, TEST_SIMPLE_VALUE2);
    // Set annotation for the STRUCT<a STRING, b INT64>
    annotationMap
        .asStructMap()
        .getField(1)
        .asStructMap()
        .getField(0)
        .setAnnotation(COLLATION_ANNOTATION_ID, TEST_SIMPLE_VALUE1);

    return annotationMap;
  }

  static final AnnotationMap createNestedArrayAnnotationMap() {
    // ARRAY<STRUCT<a STRING, b ARRAY<INT64>>>
    AnnotationMap annotationMap = AnnotationMap.create(NESTED_ARRAY_TYPE);

    // Set annotation for the whole array type
    annotationMap.setAnnotation(COLLATION_ANNOTATION_ID, TEST_SIMPLE_VALUE1);
    // Set annotation for the "a STRING" field
    annotationMap
        .asStructMap()
        .getField(0)
        .asStructMap()
        .getField(0)
        .setAnnotation(SAMPLE_ANNOTATION_ID, TEST_SIMPLE_VALUE2);
    // Set annotation for the ARRAY<INT64> field
    annotationMap
        .asStructMap()
        .getField(0)
        .asStructMap()
        .getField(1)
        .setAnnotation(COLLATION_ANNOTATION_ID, TEST_SIMPLE_VALUE1);

    return annotationMap;
  }

  private AnnotationMapTestConstants() {}
}
