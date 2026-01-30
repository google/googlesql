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
import static com.google.googlesql.AnnotationMapTestConstants.NESTED_STRUCT_TYPE;
import static com.google.googlesql.AnnotationMapTestConstants.SIMPLE_STRUCT_TYPE;
import static org.junit.Assert.assertThrows;

import org.junit.Test;
import org.junit.runner.RunWith;
import org.junit.runners.JUnit4;

@RunWith(JUnit4.class)
public final class StructAnnotationMapTest {

  @Test
  public void isStructMap_simpleStructAnnotationMap_returnsTrue() {
    AnnotationMap annotationMap = AnnotationMap.create(SIMPLE_STRUCT_TYPE);

    assertThat(annotationMap.isStructMap()).isTrue();
  }

  @Test
  public void isStructMap_nestedStructAnnotationMap() {
    // AnnotationMap for STRUCT<a STRING, b ARRAY<STRUCT<a STRING, b INT64>>>
    AnnotationMap annotationMap = AnnotationMap.create(NESTED_STRUCT_TYPE);

    assertThat(annotationMap.isStructMap()).isTrue();

    // AnnotationMap for STRING
    AnnotationMap field0AnnotationMap = annotationMap.asStructMap().getField(0);
    // AnnotationMap for ARRAY<STRUCT<a STRING, b INT64>>
    AnnotationMap field1AnnotationMap = annotationMap.asStructMap().getField(1);

    assertThat(field0AnnotationMap).isNotNull();
    assertThat(field1AnnotationMap).isNotNull();
    assertThat(field0AnnotationMap.isStructMap()).isFalse();
    assertThat(field1AnnotationMap.isStructMap()).isTrue();

    // AnnotationMap for STRUCT<a STRING, b INT64>
    AnnotationMap arrayElementAnnotationMap = field1AnnotationMap.asStructMap().getField(0);

    assertThat(arrayElementAnnotationMap).isNotNull();
    assertThat(arrayElementAnnotationMap.isStructMap()).isTrue();
  }

  @Test
  public void asStructMap_simpleStructAnnotationMap() {
    AnnotationMap annotationMap = AnnotationMap.create(SIMPLE_STRUCT_TYPE);

    assertThat(annotationMap.isStructMap()).isTrue();
    StructAnnotationMap structAnnotationMap = annotationMap.asStructMap();

    assertThat(structAnnotationMap).isNotNull();
    assertThat(structAnnotationMap.getFieldCount()).isEqualTo(2);
  }

  @Test
  public void asStructMap_nestedStructAnnotationMap() {
    // AnnotationMap for STRUCT<a STRING, b ARRAY<STRUCT<a STRING, b INT64>>>
    AnnotationMap annotationMap = AnnotationMap.create(NESTED_STRUCT_TYPE);

    assertThat(annotationMap.isStructMap()).isTrue();
    StructAnnotationMap structrAnnotationMap = annotationMap.asStructMap();
    assertThat(structrAnnotationMap).isNotNull();
    assertThat(structrAnnotationMap.getFieldCount()).isEqualTo(2);

    // AnnotationMap for STRING
    AnnotationMap field0AnnotationMap = structrAnnotationMap.getField(0);
    // AnnotationMap for ARRAY<STRUCT<a STRING, b INT64>>
    AnnotationMap field1AnnotationMap = structrAnnotationMap.getField(1);
    assertThat(field0AnnotationMap.asStructMap()).isNull();

    assertThat(field1AnnotationMap.isStructMap()).isTrue();
    assertThat(field1AnnotationMap.asStructMap().getFieldCount()).isEqualTo(1);
    // AnnotationMap for STRUCT<a STRING, b INT64>
    AnnotationMap arrayElementAnnotationMap = field1AnnotationMap.asStructMap().getField(0);

    assertThat(arrayElementAnnotationMap.asStructMap()).isNotNull();
    assertThat(arrayElementAnnotationMap.asStructMap().getFieldCount()).isEqualTo(2);
  }

  @Test
  public void getField_simpleStructAnnotationMap_returnsProperlySubfield() {
    AnnotationMap annotationMap = AnnotationMap.create(SIMPLE_STRUCT_TYPE);

    StructAnnotationMap structAnnotationMap = annotationMap.asStructMap();
    assertThat(structAnnotationMap).isNotNull();

    assertThat(structAnnotationMap.getField(0)).isNotNull();
    assertThat(structAnnotationMap.getField(1)).isNotNull();
  }

  @Test
  public void getField_simpleStructAnnotationMap_indexOutOfBound() {
    AnnotationMap annotationMap = AnnotationMap.create(SIMPLE_STRUCT_TYPE);

    StructAnnotationMap structAnnotationMap = annotationMap.asStructMap();
    assertThat(structAnnotationMap).isNotNull();

    assertThrows(IndexOutOfBoundsException.class, () -> structAnnotationMap.getField(2));
  }
}
