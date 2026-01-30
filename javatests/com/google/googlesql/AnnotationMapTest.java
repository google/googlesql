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
import static com.google.googlesql.AnnotationMapTestConstants.COLLATION_ANNOTATION_ID;
import static com.google.googlesql.AnnotationMapTestConstants.INT64_TYPE;
import static com.google.googlesql.AnnotationMapTestConstants.NESTED_ARRAY_TYPE;
import static com.google.googlesql.AnnotationMapTestConstants.NESTED_STRUCT_TYPE;
import static com.google.googlesql.AnnotationMapTestConstants.SAMPLE_ANNOTATION_ID;
import static com.google.googlesql.AnnotationMapTestConstants.SIMPLE_ARRAY_TYPE;
import static com.google.googlesql.AnnotationMapTestConstants.SIMPLE_STRUCT_TYPE;
import static com.google.googlesql.AnnotationMapTestConstants.STRING_TYPE;
import static com.google.googlesql.AnnotationMapTestConstants.TEST_SIMPLE_VALUE1;
import static com.google.googlesql.AnnotationMapTestConstants.TEST_SIMPLE_VALUE2;
import static org.junit.Assert.assertThrows;

import com.google.common.collect.ImmutableList;
import com.google.googlesql.GoogleSQLAnnotation.AnnotationMapProto;
import org.junit.Test;
import org.junit.runner.RunWith;
import org.junit.runners.JUnit4;

@RunWith(JUnit4.class)
public final class AnnotationMapTest {

  private static final String TEST_STRING_VALUE = "test string value";

  @Test
  public void create_simpleType_returnsSimpleAnnotationMap() {
    AnnotationMap annotationMap = AnnotationMap.create(STRING_TYPE);

    assertThat(annotationMap).isNotNull();
    assertThat(annotationMap.isStructMap()).isFalse();
  }

  @Test
  public void create_structType_returnsStructAnnotationMap() {
    AnnotationMap annotationMap = AnnotationMap.create(SIMPLE_STRUCT_TYPE);

    assertThat(annotationMap).isNotNull();
    assertThat(annotationMap.isStructMap()).isTrue();

    // STRUCT<a STRING, b ARRAY<STRUCT<a STRING, b INT64>>>
    annotationMap = AnnotationMap.create(NESTED_STRUCT_TYPE);
    assertThat(annotationMap).isNotNull();
    assertThat(annotationMap.isStructMap()).isTrue();

    assertThat(annotationMap.asStructMap().getField(1).isStructMap()).isTrue();

    assertThat(annotationMap.asStructMap().getField(1).asStructMap().getField(0).isStructMap())
        .isTrue();
    assertThat(
            annotationMap
                .asStructMap()
                .getField(1)
                .asStructMap()
                .getField(0)
                .asStructMap()
                .getField(0)
                .isStructMap())
        .isFalse();
    assertThat(
            annotationMap
                .asStructMap()
                .getField(1)
                .asStructMap()
                .getField(0)
                .asStructMap()
                .getField(0)
                .isStructMap())
        .isFalse();
  }

  @Test
  public void create_arrayType_returnsArrayAnnotationMap() {
    AnnotationMap annotationMap = AnnotationMap.create(SIMPLE_ARRAY_TYPE);

    assertThat(annotationMap).isNotNull();
    assertThat(annotationMap.isStructMap()).isTrue();
    assertThat(annotationMap.asStructMap().getField(0).isStructMap()).isFalse();
    assertThat(annotationMap.asStructMap().getField(0).isStructMap()).isFalse();

    // ARRAY<STRUCT<a STRING, b ARRAY<INT64>>>
    annotationMap = AnnotationMap.create(NESTED_ARRAY_TYPE);
    assertThat(annotationMap.isStructMap()).isTrue();
    assertThat(annotationMap.asStructMap().getField(0).isStructMap()).isTrue();
    assertThat(annotationMap.asStructMap().getField(0).asStructMap().getField(1).isStructMap())
        .isTrue();
    assertThat(
            annotationMap
                .asStructMap()
                .getField(0)
                .asStructMap()
                .getField(1)
                .asStructMap()
                .getField(0)
                .isStructMap())
        .isFalse();
    assertThat(
            annotationMap
                .asStructMap()
                .getField(0)
                .asStructMap()
                .getField(1)
                .asStructMap()
                .getField(0)
                .isStructMap())
        .isFalse();
  }

  @Test
  public void setAnnotation_invalidSimpleValue_throwsIllegalArgumentException() {
    AnnotationMap annotationMap = AnnotationMap.create(STRING_TYPE);
    SimpleValue invalidValue = SimpleValue.create();

    assertThrows(
        IllegalArgumentException.class,
        () -> annotationMap.setAnnotation(COLLATION_ANNOTATION_ID, invalidValue));
  }

  @Test
  public void setAnnotation_validValue_succeeds() {
    AnnotationMap annotationMap = AnnotationMap.create(STRING_TYPE);
    SimpleValue testValue = SimpleValue.createString(TEST_STRING_VALUE);

    annotationMap.setAnnotation(COLLATION_ANNOTATION_ID, testValue);

    SimpleValue getValue = annotationMap.getAnnotation(COLLATION_ANNOTATION_ID);
    assertThat(getValue).isNotNull();
    assertThat(getValue).isEqualTo(testValue);
  }

  @Test
  public void getAnnotation_validValue_succeeds() {
    AnnotationMap annotationMap = AnnotationMap.create(STRING_TYPE);
    SimpleValue testValue = SimpleValue.createInt64(12345L);
    annotationMap.setAnnotation(COLLATION_ANNOTATION_ID, testValue);

    SimpleValue getValue = annotationMap.getAnnotation(COLLATION_ANNOTATION_ID);

    assertThat(getValue).isNotNull();
    assertThat(getValue).isEqualTo(testValue);
  }

  @Test
  public void getAnnotation_notExistentAnnotationId_returnsNull() {
    AnnotationMap annotationMap = AnnotationMap.create(STRING_TYPE);
    SimpleValue testValue = SimpleValue.createString(TEST_STRING_VALUE);
    annotationMap.setAnnotation(COLLATION_ANNOTATION_ID, testValue);

    SimpleValue getValue = annotationMap.getAnnotation(0x12345);

    assertThat(getValue).isNull();
  }

  @Test
  public void unsetAnnotation_existingAnnotationId_succeeds() {
    AnnotationMap annotationMap = AnnotationMap.create(STRING_TYPE);
    SimpleValue testValue = SimpleValue.createString(TEST_STRING_VALUE);
    annotationMap.setAnnotation(COLLATION_ANNOTATION_ID, testValue);

    // Before unsetAnnotation
    SimpleValue getValue = annotationMap.getAnnotation(COLLATION_ANNOTATION_ID);
    assertThat(getValue).isEqualTo(testValue);

    annotationMap.unsetAnnotation(COLLATION_ANNOTATION_ID);

    // After unsetAnnotation
    getValue = annotationMap.getAnnotation(COLLATION_ANNOTATION_ID);
    assertThat(getValue).isNull();
  }

  @Test
  public void unsetAnnotation_nonExistingAnnotationId_unchanged() {
    AnnotationMap annotationMap = AnnotationMap.create(STRING_TYPE);
    SimpleValue testValue = SimpleValue.createString(TEST_STRING_VALUE);
    annotationMap.setAnnotation(COLLATION_ANNOTATION_ID, testValue);

    // Before unsetAnnotation
    SimpleValue getValue = annotationMap.getAnnotation(COLLATION_ANNOTATION_ID);
    assertThat(getValue).isEqualTo(testValue);

    annotationMap.unsetAnnotation(0x12345);

    // After unsetAnnotation
    getValue = annotationMap.getAnnotation(COLLATION_ANNOTATION_ID);
    assertThat(getValue).isEqualTo(testValue);
  }

  @Test
  public void unsetCompositeAnnotation_existingAnnotationId_succeeds() {
    AnnotationMap annotationMap = AnnotationMap.create(SIMPLE_ARRAY_TYPE);
    SimpleValue testValue = SimpleValue.createString(TEST_STRING_VALUE);
    annotationMap.setAnnotation(COLLATION_ANNOTATION_ID, testValue);
    annotationMap.asStructMap().getField(0).setAnnotation(COLLATION_ANNOTATION_ID, testValue);

    // Before unsetAnnotation
    assertThat(annotationMap.getAnnotation(COLLATION_ANNOTATION_ID)).isEqualTo(testValue);
    assertThat(annotationMap.asStructMap().getField(0).getAnnotation(COLLATION_ANNOTATION_ID))
        .isEqualTo(testValue);

    annotationMap.unsetAnnotationRecursively(COLLATION_ANNOTATION_ID);

    // After unsetAnnotation
    assertThat(annotationMap.getAnnotation(COLLATION_ANNOTATION_ID)).isNull();
    assertThat(annotationMap.asStructMap().getField(0).getAnnotation(COLLATION_ANNOTATION_ID))
        .isNull();
  }

  @Test
  public void unsetCompositeAnnotation_nonExistingAnnotationId_unchanged() {
    AnnotationMap annotationMap = AnnotationMap.create(SIMPLE_ARRAY_TYPE);
    SimpleValue testValue = SimpleValue.createString(TEST_STRING_VALUE);
    annotationMap.setAnnotation(COLLATION_ANNOTATION_ID, testValue);
    annotationMap.asStructMap().getField(0).setAnnotation(COLLATION_ANNOTATION_ID, testValue);

    // Before unsetAnnotation
    assertThat(annotationMap.getAnnotation(COLLATION_ANNOTATION_ID)).isEqualTo(testValue);
    assertThat(annotationMap.asStructMap().getField(0).getAnnotation(COLLATION_ANNOTATION_ID))
        .isEqualTo(testValue);

    annotationMap.unsetAnnotationRecursively(0x12345);

    // After unsetAnnotation
    assertThat(annotationMap.getAnnotation(COLLATION_ANNOTATION_ID)).isEqualTo(testValue);
    assertThat(annotationMap.asStructMap().getField(0).getAnnotation(COLLATION_ANNOTATION_ID))
        .isEqualTo(testValue);
  }

  @Test
  public void isStructMap_stringAnnotationMap_returnsFalse() {
    AnnotationMap annotationMap = AnnotationMap.create(STRING_TYPE);

    assertThat(annotationMap.isStructMap()).isFalse();
  }

  @Test
  public void asStructMap_stringAnnotationMap_returnsNull() {
    AnnotationMap annotationMap = AnnotationMap.create(STRING_TYPE);

    assertThat(annotationMap.asStructMap()).isNull();
  }

  @Test
  public void isEmpty_stringAnnotationMap() {
    AnnotationMap annotationMap = AnnotationMap.create(STRING_TYPE);
    assertThat(annotationMap.isEmpty()).isTrue();

    annotationMap.setAnnotation(COLLATION_ANNOTATION_ID, TEST_SIMPLE_VALUE1);
    assertThat(annotationMap.isEmpty()).isFalse();

    annotationMap.unsetAnnotation(COLLATION_ANNOTATION_ID);
    assertThat(annotationMap.isEmpty()).isTrue();
  }

  @Test
  public void isEmpty_structAnnotationMap() {
    AnnotationMap annotationMap = AnnotationMap.create(SIMPLE_STRUCT_TYPE);
    assertThat(annotationMap.isEmpty()).isTrue();

    // Set annotation to subfield type
    AnnotationMap field0AnnotationMap = annotationMap.asStructMap().getField(0);
    AnnotationMap field1AnnotationMap = annotationMap.asStructMap().getField(1);
    field0AnnotationMap.setAnnotation(COLLATION_ANNOTATION_ID, TEST_SIMPLE_VALUE1);

    assertThat(field0AnnotationMap.isEmpty()).isFalse();
    assertThat(field1AnnotationMap.isEmpty()).isTrue();
    assertThat(annotationMap.isEmpty()).isFalse();
  }

  @Test
  public void isEmpty_structAnnotationMapWithNullFields() {
    AnnotationMap annotationMap = AnnotationMap.create(SIMPLE_STRUCT_TYPE);
    assertThat(annotationMap.isEmpty()).isTrue();

    // Set annotation to subfield type
    AnnotationMap field0AnnotationMap = annotationMap.asStructMap().getField(0);
    field0AnnotationMap.setAnnotation(COLLATION_ANNOTATION_ID, TEST_SIMPLE_VALUE1);
    assertThat(annotationMap.isEmpty()).isFalse();

    // Set field0's annotation to null
    annotationMap.asStructMap().setField(0, null);
    assertThat(annotationMap.isEmpty()).isTrue();
  }

  @Test
  public void isEmpty_arrayAnnotationMap() {
    AnnotationMap annotationMap = AnnotationMap.create(SIMPLE_ARRAY_TYPE);
    assertThat(annotationMap.isEmpty()).isTrue();
    assertThat(annotationMap.asStructMap().getField(0).isEmpty()).isTrue();

    // Set annotation to element type
    annotationMap
        .asStructMap()
        .getField(0)
        .setAnnotation(COLLATION_ANNOTATION_ID, TEST_SIMPLE_VALUE1);
    assertThat(annotationMap.asStructMap().getField(0).isEmpty()).isFalse();
    assertThat(annotationMap.isEmpty()).isFalse();
  }

  @Test
  public void isEmpty_arrayAnnotationMapWithNullElement() {
    AnnotationMap annotationMap = AnnotationMap.create(SIMPLE_ARRAY_TYPE);
    assertThat(annotationMap.isEmpty()).isTrue();

    // Set annotation to element type
    annotationMap
        .asStructMap()
        .getField(0)
        .setAnnotation(COLLATION_ANNOTATION_ID, TEST_SIMPLE_VALUE1);
    assertThat(annotationMap.isEmpty()).isFalse();

    // Set the element to null
    annotationMap.asStructMap().setField(0, null);

    assertThat(annotationMap.isEmpty()).isTrue();
  }

  @Test
  public void isEmpty_nestedStructAndArrayType() {
    // STRUCT<a STRING, b ARRAY<STRUCT<a STRING, b INT64>>>
    AnnotationMap annotationMap = AnnotationMap.create(NESTED_STRUCT_TYPE);
    assertThat(annotationMap.isEmpty()).isTrue();

    // Set annotation to element type
    annotationMap
        .asStructMap()
        .getField(1) // ARRAY<STRUCT<a STRING, b INT64>>>
        .asStructMap()
        .getField(0) // STRUCT<a STRING, b INT64>>
        .asStructMap()
        .getField(0) // String
        .setAnnotation(COLLATION_ANNOTATION_ID, TEST_SIMPLE_VALUE1);

    // The nested struct type is not empty
    assertThat(annotationMap.isEmpty()).isFalse();
    // The nested array type is not empty
    assertThat(annotationMap.asStructMap().getField(1).isEmpty()).isFalse();
  }

  @Test
  public void equals_nullOrEmptyCase() {
    AnnotationMap nonEmptyAnnotationMap = AnnotationMap.create(STRING_TYPE);
    nonEmptyAnnotationMap.setAnnotation(COLLATION_ANNOTATION_ID, TEST_SIMPLE_VALUE1);
    assertThat(AnnotationMap.equals(null, null)).isTrue();
    assertThat(AnnotationMap.equals(null, new AnnotationMap())).isTrue();
    assertThat(AnnotationMap.equals(new AnnotationMap(), null)).isTrue();
    assertThat(AnnotationMap.equals(new AnnotationMap(), new AnnotationMap())).isTrue();
    assertThat(AnnotationMap.equals(null, nonEmptyAnnotationMap)).isFalse();
    assertThat(AnnotationMap.equals(new AnnotationMap(), nonEmptyAnnotationMap)).isFalse();
    assertThat(nonEmptyAnnotationMap.equals(null)).isFalse();
    assertThat(nonEmptyAnnotationMap.equals(null)).isFalse();
  }

  @Test
  public void equals_simpleAnnotationMap() {
    AnnotationMap annotationMap = AnnotationMap.create(STRING_TYPE);
    annotationMap.setAnnotation(COLLATION_ANNOTATION_ID, TEST_SIMPLE_VALUE1);

    AnnotationMap anotherAnnotationMap = AnnotationMap.create(INT64_TYPE);
    assertThat(annotationMap.equals(annotationMap)).isTrue();
    assertThat(annotationMap.equals(anotherAnnotationMap)).isFalse();

    anotherAnnotationMap.setAnnotation(COLLATION_ANNOTATION_ID, TEST_SIMPLE_VALUE1);
    assertThat(annotationMap.equals(anotherAnnotationMap)).isTrue();

    anotherAnnotationMap.setAnnotation(SAMPLE_ANNOTATION_ID, TEST_SIMPLE_VALUE2);
    assertThat(annotationMap.equals(anotherAnnotationMap)).isFalse();

    anotherAnnotationMap.unsetAnnotation(SAMPLE_ANNOTATION_ID);
    assertThat(annotationMap.equals(anotherAnnotationMap)).isTrue();
  }

  @Test
  public void equals_structAnnotationMap() {
    AnnotationMap annotationMap = AnnotationMap.create(SIMPLE_STRUCT_TYPE);
    AnnotationMap anotherAnnotationMap = AnnotationMap.create(SIMPLE_STRUCT_TYPE);

    assertThat(annotationMap.equals(AnnotationMap.create(STRING_TYPE))).isFalse();
    assertThat(annotationMap.equals(AnnotationMap.create(NESTED_STRUCT_TYPE))).isFalse();
    assertThat(annotationMap.equals(AnnotationMap.create(SIMPLE_ARRAY_TYPE))).isFalse();
    assertThat(annotationMap.equals(anotherAnnotationMap)).isTrue();

    // Set annotations
    annotationMap.setAnnotation(COLLATION_ANNOTATION_ID, TEST_SIMPLE_VALUE1);
    anotherAnnotationMap.setAnnotation(COLLATION_ANNOTATION_ID, TEST_SIMPLE_VALUE1);
    assertThat(annotationMap.equals(anotherAnnotationMap)).isTrue();

    // Set annotations for subfield0
    annotationMap
        .asStructMap()
        .getField(0)
        .setAnnotation(COLLATION_ANNOTATION_ID, TEST_SIMPLE_VALUE1);
    assertThat(annotationMap.equals(anotherAnnotationMap)).isFalse();

    // Set annotations for subfield1
    anotherAnnotationMap
        .asStructMap()
        .getField(1)
        .setAnnotation(COLLATION_ANNOTATION_ID, TEST_SIMPLE_VALUE1);
    assertThat(annotationMap.equals(anotherAnnotationMap)).isFalse();

    // Clear subfield1's annotation
    anotherAnnotationMap.asStructMap().getField(1).unsetAnnotation(COLLATION_ANNOTATION_ID);
    // Set the same annotations for subfield0
    anotherAnnotationMap
        .asStructMap()
        .getField(0)
        .setAnnotation(COLLATION_ANNOTATION_ID, TEST_SIMPLE_VALUE1);
    assertThat(annotationMap.equals(anotherAnnotationMap)).isTrue();

    // Set different annotation to the subfield0
    anotherAnnotationMap.asStructMap().getField(0).unsetAnnotation(COLLATION_ANNOTATION_ID);
    anotherAnnotationMap
        .asStructMap()
        .getField(0)
        .setAnnotation(SAMPLE_ANNOTATION_ID, TEST_SIMPLE_VALUE2);
    assertThat(annotationMap.equals(anotherAnnotationMap)).isFalse();
  }

  @Test
  public void equals_structAnnotationMapWithNull() {
    AnnotationMap annotationMap = AnnotationMap.create(SIMPLE_STRUCT_TYPE);
    AnnotationMap anotherAnnotationMap = AnnotationMap.create(SIMPLE_STRUCT_TYPE);

    // Set subfield0 to null
    annotationMap.asStructMap().setField(0, null);
    assertThat(annotationMap.equals(anotherAnnotationMap)).isTrue();

    anotherAnnotationMap.asStructMap().setField(0, null);
    assertThat(annotationMap.equals(anotherAnnotationMap)).isTrue();
  }

  @Test
  public void equals_arrayAnnotationMap() {
    AnnotationMap annotationMap = AnnotationMap.create(SIMPLE_ARRAY_TYPE);
    AnnotationMap anotherAnnotationMap = AnnotationMap.create(SIMPLE_ARRAY_TYPE);

    assertThat(annotationMap.equals(AnnotationMap.create(STRING_TYPE))).isFalse();
    assertThat(annotationMap.equals(AnnotationMap.create(NESTED_ARRAY_TYPE))).isFalse();
    assertThat(annotationMap.equals(AnnotationMap.create(SIMPLE_STRUCT_TYPE))).isFalse();
    assertThat(annotationMap.equals(anotherAnnotationMap)).isTrue();

    // Set annotations
    annotationMap.setAnnotation(COLLATION_ANNOTATION_ID, TEST_SIMPLE_VALUE1);
    anotherAnnotationMap.setAnnotation(COLLATION_ANNOTATION_ID, TEST_SIMPLE_VALUE1);
    assertThat(annotationMap.equals(anotherAnnotationMap)).isTrue();

    // Set annotations for the element
    annotationMap
        .asStructMap()
        .getField(0)
        .setAnnotation(COLLATION_ANNOTATION_ID, TEST_SIMPLE_VALUE1);
    assertThat(annotationMap.equals(anotherAnnotationMap)).isFalse();

    anotherAnnotationMap
        .asStructMap()
        .getField(0)
        .setAnnotation(COLLATION_ANNOTATION_ID, TEST_SIMPLE_VALUE1);
    assertThat(annotationMap.equals(anotherAnnotationMap)).isTrue();

    // Set different annotation to the subfield0
    anotherAnnotationMap.asStructMap().getField(0).unsetAnnotation(COLLATION_ANNOTATION_ID);
    anotherAnnotationMap
        .asStructMap()
        .getField(0)
        .setAnnotation(SAMPLE_ANNOTATION_ID, TEST_SIMPLE_VALUE2);
    assertThat(annotationMap.equals(anotherAnnotationMap)).isFalse();
  }

  @Test
  public void equals_arrayAnnotationMapWithNull() {
    AnnotationMap annotationMap = AnnotationMap.create(SIMPLE_ARRAY_TYPE);
    AnnotationMap anotherAnnotationMap = AnnotationMap.create(SIMPLE_ARRAY_TYPE);

    annotationMap.asStructMap().setField(0, null);
    assertThat(annotationMap.equals(anotherAnnotationMap)).isTrue();

    anotherAnnotationMap.asStructMap().setField(0, null);
    assertThat(annotationMap.equals(anotherAnnotationMap)).isTrue();
  }

  @Test
  public void equals_nestedStructAndArrayType() {
    // STRUCT<a STRING, b ARRAY<STRUCT<a STRING, b INT64>>>
    AnnotationMap annotationMap = AnnotationMap.create(NESTED_STRUCT_TYPE);
    AnnotationMap anotherAnnotationMap = AnnotationMap.create(NESTED_STRUCT_TYPE);
    assertThat(annotationMap.equals(AnnotationMap.create(NESTED_ARRAY_TYPE))).isFalse();

    // Set annotation to element type
    annotationMap
        .asStructMap()
        .getField(1) // ARRAY<STRUCT<a STRING, b INT64>>>
        .asStructMap()
        .setAnnotation(COLLATION_ANNOTATION_ID, TEST_SIMPLE_VALUE1);
    assertThat(annotationMap.equals(anotherAnnotationMap)).isFalse();

    // Set annotation to element type
    annotationMap
        .asStructMap()
        .getField(1) // ARRAY<STRUCT<a STRING, b INT64>>>
        .asStructMap()
        .getField(0) // STRUCT<a STRING, b INT64>>
        .asStructMap()
        .getField(0) // String
        .setAnnotation(SAMPLE_ANNOTATION_ID, TEST_SIMPLE_VALUE2);
    assertThat(annotationMap.equals(anotherAnnotationMap)).isFalse();

    // Set annotations for anotherAnnotationMap similarly
    anotherAnnotationMap
        .asStructMap()
        .getField(1) // ARRAY<STRUCT<a STRING, b INT64>>>
        .asStructMap()
        .setAnnotation(COLLATION_ANNOTATION_ID, TEST_SIMPLE_VALUE1);
    anotherAnnotationMap
        .asStructMap()
        .getField(1) // ARRAY<STRUCT<a STRING, b INT64>>>
        .asStructMap()
        .getField(0) // STRUCT<a STRING, b INT64>>
        .asStructMap()
        .getField(0) // String
        .setAnnotation(SAMPLE_ANNOTATION_ID, TEST_SIMPLE_VALUE2);
    assertThat(annotationMap.equals(anotherAnnotationMap)).isTrue();
  }

  @Test
  public void hashCode_stringAnnotationMap() {
    AnnotationMap annotationMap = AnnotationMap.create(STRING_TYPE);
    AnnotationMap anotherAnnotationMap = AnnotationMap.create(STRING_TYPE);

    annotationMap.setAnnotation(COLLATION_ANNOTATION_ID, TEST_SIMPLE_VALUE1);
    assertThat(annotationMap.hashCode()).isNotEqualTo(anotherAnnotationMap.hashCode());

    anotherAnnotationMap.setAnnotation(COLLATION_ANNOTATION_ID, TEST_SIMPLE_VALUE1);
    assertThat(annotationMap.hashCode()).isEqualTo(anotherAnnotationMap.hashCode());
  }

  @Test
  public void hashCode_structAnnotationMap() {
    AnnotationMap annotationMap = AnnotationMap.create(SIMPLE_STRUCT_TYPE);
    AnnotationMap anotherAnnotationMap = AnnotationMap.create(SIMPLE_STRUCT_TYPE);

    annotationMap.setAnnotation(COLLATION_ANNOTATION_ID, TEST_SIMPLE_VALUE1);
    annotationMap.asStructMap().getField(0).setAnnotation(SAMPLE_ANNOTATION_ID, TEST_SIMPLE_VALUE2);
    assertThat(annotationMap.hashCode()).isNotEqualTo(anotherAnnotationMap.hashCode());

    anotherAnnotationMap.setAnnotation(COLLATION_ANNOTATION_ID, TEST_SIMPLE_VALUE1);
    assertThat(annotationMap.hashCode()).isNotEqualTo(anotherAnnotationMap.hashCode());

    anotherAnnotationMap
        .asStructMap()
        .getField(0)
        .setAnnotation(SAMPLE_ANNOTATION_ID, TEST_SIMPLE_VALUE2);
    assertThat(annotationMap.hashCode()).isEqualTo(anotherAnnotationMap.hashCode());
  }

  @Test
  public void hashCode_arrayAnnotationMap() {
    AnnotationMap annotationMap = AnnotationMap.create(SIMPLE_ARRAY_TYPE);
    AnnotationMap anotherAnnotationMap = AnnotationMap.create(SIMPLE_ARRAY_TYPE);

    annotationMap.setAnnotation(COLLATION_ANNOTATION_ID, TEST_SIMPLE_VALUE1);
    annotationMap.asStructMap().getField(0).setAnnotation(SAMPLE_ANNOTATION_ID, TEST_SIMPLE_VALUE2);
    assertThat(annotationMap.hashCode()).isNotEqualTo(anotherAnnotationMap.hashCode());

    anotherAnnotationMap.setAnnotation(COLLATION_ANNOTATION_ID, TEST_SIMPLE_VALUE1);
    assertThat(annotationMap.hashCode()).isNotEqualTo(anotherAnnotationMap.hashCode());

    anotherAnnotationMap
        .asStructMap()
        .getField(0)
        .setAnnotation(SAMPLE_ANNOTATION_ID, TEST_SIMPLE_VALUE2);
    assertThat(annotationMap.hashCode()).isEqualTo(anotherAnnotationMap.hashCode());
  }

  @Test
  public void has_simpleAnnotationMap() {
    AnnotationMap annotationMap = AnnotationMap.create(STRING_TYPE);
    assertThat(annotationMap.has(COLLATION_ANNOTATION_ID)).isFalse();

    annotationMap.setAnnotation(COLLATION_ANNOTATION_ID, TEST_SIMPLE_VALUE1);
    assertThat(annotationMap.has(COLLATION_ANNOTATION_ID)).isTrue();
    assertThat(annotationMap.has(SAMPLE_ANNOTATION_ID)).isFalse();

    annotationMap.setAnnotation(SAMPLE_ANNOTATION_ID, TEST_SIMPLE_VALUE2);
    assertThat(annotationMap.has(COLLATION_ANNOTATION_ID)).isTrue();
    assertThat(annotationMap.has(SAMPLE_ANNOTATION_ID)).isTrue();
  }

  @Test
  public void has_structAnnotationMap() {
    AnnotationMap annotationMap = AnnotationMap.create(SIMPLE_STRUCT_TYPE);
    assertThat(annotationMap.has(COLLATION_ANNOTATION_ID)).isFalse();

    annotationMap
        .asStructMap()
        .getField(0)
        .setAnnotation(COLLATION_ANNOTATION_ID, TEST_SIMPLE_VALUE1);
    assertThat(annotationMap.has(COLLATION_ANNOTATION_ID)).isTrue();
    assertThat(annotationMap.has(SAMPLE_ANNOTATION_ID)).isFalse();

    // STRUCT<a STRING, b ARRAY<STRUCT<a STRING, b INT64>>>
    AnnotationMap nestedAnnotationMap = AnnotationMap.create(NESTED_STRUCT_TYPE);
    assertThat(nestedAnnotationMap.has(COLLATION_ANNOTATION_ID)).isFalse();
    assertThat(nestedAnnotationMap.has(SAMPLE_ANNOTATION_ID)).isFalse();

    nestedAnnotationMap
        .asStructMap()
        .getField(1)
        .asStructMap()
        .getField(0)
        .setAnnotation(COLLATION_ANNOTATION_ID, TEST_SIMPLE_VALUE1);
    assertThat(nestedAnnotationMap.has(COLLATION_ANNOTATION_ID)).isTrue();
    assertThat(nestedAnnotationMap.has(SAMPLE_ANNOTATION_ID)).isFalse();

    nestedAnnotationMap
        .asStructMap()
        .getField(1)
        .asStructMap()
        .getField(0)
        .asStructMap()
        .getField(0)
        .setAnnotation(SAMPLE_ANNOTATION_ID, TEST_SIMPLE_VALUE2);
    assertThat(nestedAnnotationMap.has(COLLATION_ANNOTATION_ID)).isTrue();
    assertThat(nestedAnnotationMap.has(SAMPLE_ANNOTATION_ID)).isTrue();
  }

  @Test
  public void has_arrayAnnotationMap() {
    AnnotationMap annotationMap = AnnotationMap.create(SIMPLE_ARRAY_TYPE);
    assertThat(annotationMap.has(COLLATION_ANNOTATION_ID)).isFalse();

    annotationMap
        .asStructMap()
        .getField(0)
        .setAnnotation(COLLATION_ANNOTATION_ID, TEST_SIMPLE_VALUE1);
    assertThat(annotationMap.has(COLLATION_ANNOTATION_ID)).isTrue();
    assertThat(annotationMap.has(SAMPLE_ANNOTATION_ID)).isFalse();

    // ARRAY<STRUCT<a STRING, b ARRAY<INT64>>>
    AnnotationMap nestedAnnotationMap = AnnotationMap.create(NESTED_ARRAY_TYPE);
    assertThat(nestedAnnotationMap.has(COLLATION_ANNOTATION_ID)).isFalse();
    assertThat(nestedAnnotationMap.has(SAMPLE_ANNOTATION_ID)).isFalse();

    nestedAnnotationMap
        .asStructMap()
        .getField(0) // STRUCT<a STRING, b ARRAY<INT64>>
        .asStructMap()
        .getField(0) // STRING
        .setAnnotation(COLLATION_ANNOTATION_ID, TEST_SIMPLE_VALUE1);
    assertThat(nestedAnnotationMap.has(COLLATION_ANNOTATION_ID)).isTrue();
    assertThat(nestedAnnotationMap.has(SAMPLE_ANNOTATION_ID)).isFalse();

    nestedAnnotationMap
        .asStructMap()
        .getField(0) // STRUCT<a STRING, b ARRAY<INT64>>
        .asStructMap()
        .getField(1) // ARRAY<INT64>
        .asStructMap()
        .setAnnotation(SAMPLE_ANNOTATION_ID, TEST_SIMPLE_VALUE2);
    assertThat(nestedAnnotationMap.has(COLLATION_ANNOTATION_ID)).isTrue();
    assertThat(nestedAnnotationMap.has(SAMPLE_ANNOTATION_ID)).isTrue();
  }

  @Test
  public void hasEqualAnnotation_nullOrEmptyAnnotationMap() {
    AnnotationMap nonEmptyAnnotationMap = AnnotationMap.create(STRING_TYPE);
    nonEmptyAnnotationMap.setAnnotation(COLLATION_ANNOTATION_ID, TEST_SIMPLE_VALUE1);

    assertThat(AnnotationMap.hasEqualAnnotationsBetween(null, null, COLLATION_ANNOTATION_ID))
        .isTrue();
    assertThat(
            AnnotationMap.hasEqualAnnotationsBetween(
                null, new AnnotationMap(), COLLATION_ANNOTATION_ID))
        .isTrue();
    assertThat(
            AnnotationMap.hasEqualAnnotationsBetween(
                new AnnotationMap(), null, COLLATION_ANNOTATION_ID))
        .isTrue();
    assertThat(
            AnnotationMap.hasEqualAnnotationsBetween(
                new AnnotationMap(), new AnnotationMap(), COLLATION_ANNOTATION_ID))
        .isTrue();
    assertThat(
            AnnotationMap.hasEqualAnnotationsBetween(
                null, nonEmptyAnnotationMap, COLLATION_ANNOTATION_ID))
        .isFalse();
    assertThat(
            AnnotationMap.hasEqualAnnotationsBetween(
                new AnnotationMap(), nonEmptyAnnotationMap, COLLATION_ANNOTATION_ID))
        .isFalse();
    assertThat(nonEmptyAnnotationMap.hasEqualAnnotations(null, COLLATION_ANNOTATION_ID)).isFalse();
    assertThat(nonEmptyAnnotationMap.hasEqualAnnotations(null, COLLATION_ANNOTATION_ID)).isFalse();
  }

  @Test
  public void hasEqualAnnotation_simpleAnnotationMap() {
    AnnotationMap annotationMap = AnnotationMap.create(STRING_TYPE);
    AnnotationMap anotherAnnotationMap = AnnotationMap.create(STRING_TYPE);
    annotationMap.setAnnotation(COLLATION_ANNOTATION_ID, TEST_SIMPLE_VALUE1);

    assertThat(annotationMap.hasEqualAnnotations(anotherAnnotationMap, COLLATION_ANNOTATION_ID))
        .isFalse();
    assertThat(annotationMap.hasEqualAnnotations(anotherAnnotationMap, SAMPLE_ANNOTATION_ID))
        .isTrue();

    anotherAnnotationMap.setAnnotation(COLLATION_ANNOTATION_ID, TEST_SIMPLE_VALUE1);
    assertThat(annotationMap.hasEqualAnnotations(anotherAnnotationMap, COLLATION_ANNOTATION_ID))
        .isTrue();
    assertThat(annotationMap.hasEqualAnnotations(anotherAnnotationMap, SAMPLE_ANNOTATION_ID))
        .isTrue();

    anotherAnnotationMap.setAnnotation(COLLATION_ANNOTATION_ID, TEST_SIMPLE_VALUE2);
    assertThat(annotationMap.hasEqualAnnotations(anotherAnnotationMap, COLLATION_ANNOTATION_ID))
        .isFalse();
  }

  @Test
  public void hasEqualAnnotation_simpleStructAnnotationMap() {
    AnnotationMap annotationMap = AnnotationMap.create(SIMPLE_STRUCT_TYPE);
    AnnotationMap anotherAnnotationMap = AnnotationMap.create(SIMPLE_STRUCT_TYPE);

    assertThat(annotationMap.hasEqualAnnotations(anotherAnnotationMap, COLLATION_ANNOTATION_ID))
        .isTrue();
    // nested annotation map struct is different
    assertThat(
            annotationMap.hasEqualAnnotations(
                AnnotationMap.create(NESTED_STRUCT_TYPE), COLLATION_ANNOTATION_ID))
        .isFalse();

    annotationMap
        .asStructMap()
        .getField(0)
        .setAnnotation(COLLATION_ANNOTATION_ID, TEST_SIMPLE_VALUE1);
    // The annotation value doesn't exist
    assertThat(annotationMap.hasEqualAnnotations(anotherAnnotationMap, COLLATION_ANNOTATION_ID))
        .isFalse();
    assertThat(annotationMap.hasEqualAnnotations(anotherAnnotationMap, SAMPLE_ANNOTATION_ID))
        .isTrue();

    anotherAnnotationMap
        .asStructMap()
        .getField(0)
        .setAnnotation(COLLATION_ANNOTATION_ID, TEST_SIMPLE_VALUE2);
    // The annotation value is different
    assertThat(annotationMap.hasEqualAnnotations(anotherAnnotationMap, COLLATION_ANNOTATION_ID))
        .isFalse();

    anotherAnnotationMap
        .asStructMap()
        .getField(0)
        .setAnnotation(COLLATION_ANNOTATION_ID, TEST_SIMPLE_VALUE1);
    assertThat(annotationMap.hasEqualAnnotations(anotherAnnotationMap, COLLATION_ANNOTATION_ID))
        .isTrue();
  }

  @Test
  public void hasEqualAnnotation_nestedStructAnnotationMap() {
    // STRUCT<a STRING, b ARRAY<STRUCT<a STRING, b INT64>>>
    AnnotationMap nestedAnnotationMap = AnnotationMap.create(NESTED_STRUCT_TYPE);
    AnnotationMap anotherNestedAnnotationMap = AnnotationMap.create(NESTED_STRUCT_TYPE);
    assertThat(
            nestedAnnotationMap.hasEqualAnnotations(
                anotherNestedAnnotationMap, COLLATION_ANNOTATION_ID))
        .isTrue();

    // Set annotation for one sub-field
    nestedAnnotationMap
        .asStructMap()
        .getField(1)
        .asStructMap()
        .getField(0)
        .setAnnotation(SAMPLE_ANNOTATION_ID, TEST_SIMPLE_VALUE2);
    assertThat(
            nestedAnnotationMap.hasEqualAnnotations(
                anotherNestedAnnotationMap, COLLATION_ANNOTATION_ID))
        .isTrue();
    assertThat(
            nestedAnnotationMap.hasEqualAnnotations(
                anotherNestedAnnotationMap, SAMPLE_ANNOTATION_ID))
        .isFalse();

    // Set a different annotation value to sub-fields
    anotherNestedAnnotationMap
        .asStructMap()
        .getField(1)
        .asStructMap()
        .getField(0)
        .setAnnotation(SAMPLE_ANNOTATION_ID, TEST_SIMPLE_VALUE1);
    assertThat(
            nestedAnnotationMap.hasEqualAnnotations(
                anotherNestedAnnotationMap, SAMPLE_ANNOTATION_ID))
        .isFalse();

    // Set the correct annotation value
    anotherNestedAnnotationMap
        .asStructMap()
        .getField(1)
        .asStructMap()
        .getField(0)
        .setAnnotation(SAMPLE_ANNOTATION_ID, TEST_SIMPLE_VALUE2);
    // Add another unrelated annotation
    anotherNestedAnnotationMap
        .asStructMap()
        .getField(0)
        .setAnnotation(COLLATION_ANNOTATION_ID, TEST_SIMPLE_VALUE1);
    assertThat(
            nestedAnnotationMap.hasEqualAnnotations(
                anotherNestedAnnotationMap, SAMPLE_ANNOTATION_ID))
        .isTrue();
    assertThat(
            nestedAnnotationMap.hasEqualAnnotations(
                anotherNestedAnnotationMap, COLLATION_ANNOTATION_ID))
        .isFalse();
  }

  @Test
  public void hasEqualAnnotation_simpleArrayAnnotationMap() {
    AnnotationMap annotationMap = AnnotationMap.create(SIMPLE_ARRAY_TYPE);
    AnnotationMap anotherAnnotationMap = AnnotationMap.create(SIMPLE_ARRAY_TYPE);

    // nested annotation map struct is different
    assertThat(
            annotationMap.hasEqualAnnotations(
                AnnotationMap.create(NESTED_ARRAY_TYPE), COLLATION_ANNOTATION_ID))
        .isFalse();

    // Set annotation for the top-level and nested element
    annotationMap.setAnnotation(COLLATION_ANNOTATION_ID, TEST_SIMPLE_VALUE1);
    annotationMap
        .asStructMap()
        .getField(0)
        .setAnnotation(COLLATION_ANNOTATION_ID, TEST_SIMPLE_VALUE1);

    // Set annotation for the nest element only
    anotherAnnotationMap
        .asStructMap()
        .getField(0)
        .setAnnotation(COLLATION_ANNOTATION_ID, TEST_SIMPLE_VALUE1);
    // The annotation value doesn't exist
    assertThat(annotationMap.hasEqualAnnotations(anotherAnnotationMap, COLLATION_ANNOTATION_ID))
        .isFalse();
    assertThat(annotationMap.hasEqualAnnotations(anotherAnnotationMap, SAMPLE_ANNOTATION_ID))
        .isTrue();

    anotherAnnotationMap.setAnnotation(COLLATION_ANNOTATION_ID, TEST_SIMPLE_VALUE2);
    // The annotation value is different
    assertThat(annotationMap.hasEqualAnnotations(anotherAnnotationMap, COLLATION_ANNOTATION_ID))
        .isFalse();

    anotherAnnotationMap.setAnnotation(COLLATION_ANNOTATION_ID, TEST_SIMPLE_VALUE1);
    assertThat(annotationMap.hasEqualAnnotations(anotherAnnotationMap, COLLATION_ANNOTATION_ID))
        .isTrue();
  }

  @Test
  public void hasEqualAnnotation_nestedArrayAnnotationMap() {
    // ARRAY<STRUCT<a STRING, b ARRAY<INT64>>>
    AnnotationMap nestedAnnotationMap = AnnotationMap.create(NESTED_ARRAY_TYPE);
    AnnotationMap anotherNestedAnnotationMap = AnnotationMap.create(NESTED_ARRAY_TYPE);
    assertThat(
            nestedAnnotationMap.hasEqualAnnotations(
                anotherNestedAnnotationMap, COLLATION_ANNOTATION_ID))
        .isTrue();

    // Set annotation for one sub-field
    nestedAnnotationMap
        .asStructMap()
        .getField(0)
        .setAnnotation(SAMPLE_ANNOTATION_ID, TEST_SIMPLE_VALUE2);
    assertThat(
            nestedAnnotationMap.hasEqualAnnotations(
                anotherNestedAnnotationMap, COLLATION_ANNOTATION_ID))
        .isTrue();
    assertThat(
            nestedAnnotationMap.hasEqualAnnotations(
                anotherNestedAnnotationMap, SAMPLE_ANNOTATION_ID))
        .isFalse();

    // Set a different annotation value to sub-fields
    anotherNestedAnnotationMap
        .asStructMap()
        .getField(0)
        .setAnnotation(SAMPLE_ANNOTATION_ID, TEST_SIMPLE_VALUE1);
    assertThat(
            nestedAnnotationMap.hasEqualAnnotations(
                anotherNestedAnnotationMap, SAMPLE_ANNOTATION_ID))
        .isFalse();

    // Set the correct annotation value
    anotherNestedAnnotationMap
        .asStructMap()
        .getField(0)
        .setAnnotation(SAMPLE_ANNOTATION_ID, TEST_SIMPLE_VALUE2);
    assertThat(
            nestedAnnotationMap.hasEqualAnnotations(
                anotherNestedAnnotationMap, SAMPLE_ANNOTATION_ID))
        .isTrue();

    // Add another unrelated annotation
    anotherNestedAnnotationMap
        .asStructMap()
        .getField(0)
        .asStructMap()
        .getField(0)
        .setAnnotation(COLLATION_ANNOTATION_ID, TEST_SIMPLE_VALUE1);
    assertThat(
            nestedAnnotationMap.hasEqualAnnotations(
                anotherNestedAnnotationMap, SAMPLE_ANNOTATION_ID))
        .isTrue();
    assertThat(
            nestedAnnotationMap.hasEqualAnnotations(
                anotherNestedAnnotationMap, COLLATION_ANNOTATION_ID))
        .isFalse();
  }

  @Test
  public void debugString_stringAnnotationMap() {
    AnnotationMap annotationMap = AnnotationMapTestConstants.createStringAnnotationMap();
    annotationMap.setAnnotation(SAMPLE_ANNOTATION_ID, TEST_SIMPLE_VALUE2);

    assertThat(annotationMap.debugString())
        .isEqualTo(
            "{Collation:\"TestAnnotation1 value\", SampleAnnotation:\"TestAnnotation2 value\"}");
    assertThat(annotationMap.debugString(COLLATION_ANNOTATION_ID))
        .isEqualTo("\"TestAnnotation1 value\"");
    assertThat(annotationMap.debugString(SAMPLE_ANNOTATION_ID))
        .isEqualTo("\"TestAnnotation2 value\"");
    assertThat(annotationMap.debugString(0x123456)).isEmpty();
  }

  @Test
  public void debugString_annotationSpecIdExceedsMaxAnnotationId() {
    AnnotationMap annotationMap = AnnotationMapTestConstants.createStringAnnotationMap();
    annotationMap.setAnnotation(
        0x123456, SimpleValue.createString("Annotations for a very large annotation id"));

    assertThat(annotationMap.debugString())
        .isEqualTo(
            "{Collation:\"TestAnnotation1 value\", 1193046:\"Annotations for a very large"
                + " annotation id\"}");
    assertThat(annotationMap.debugString(0x123456))
        .isEqualTo("\"Annotations for a very large annotation id\"");
  }

  @Test
  public void debugString_emptyAnnotationMap() {
    AnnotationMap annotationMap = AnnotationMap.create(STRING_TYPE);
    assertThat(annotationMap.debugString()).isEmpty();
  }

  @Test
  public void debugString_simpleStructAnnotationMap() {
    AnnotationMap annotationMap = AnnotationMapTestConstants.createSimpleStructAnnotationMap();

    assertThat(annotationMap.debugString())
        .isEqualTo(
            "{Collation:\"TestAnnotation1 value\"}<{SampleAnnotation:\"TestAnnotation2"
                + " value\"},{}>");
    assertThat(annotationMap.debugString(COLLATION_ANNOTATION_ID))
        .isEqualTo("\"TestAnnotation1 value\"<,>");
    assertThat(annotationMap.debugString(SAMPLE_ANNOTATION_ID))
        .isEqualTo("<\"TestAnnotation2 value\",>");
  }

  @Test
  public void debugString_nestedStructAnnotationMap() {
    //  STRUCT<a STRING, b ARRAY<STRUCT<a STRING, b INT64>>>
    AnnotationMap annotationMap = AnnotationMapTestConstants.createNestedStructAnnotationMap();

    assertThat(annotationMap.debugString())
        .isEqualTo(
            "{Collation:\"TestAnnotation1 value\"}<{SampleAnnotation:\"TestAnnotation2"
                + " value\"},<{Collation:\"TestAnnotation1 value\"}<{},{}>>>");
    assertThat(annotationMap.debugString(COLLATION_ANNOTATION_ID))
        .isEqualTo("\"TestAnnotation1 value\"<,<\"TestAnnotation1 value\"<,>>>");
    assertThat(annotationMap.debugString(SAMPLE_ANNOTATION_ID))
        .isEqualTo("<\"TestAnnotation2 value\",<<,>>>");

    annotationMap.normalize();
    assertThat(annotationMap.debugString())
        .isEqualTo(
            "{Collation:\"TestAnnotation1 value\"}<{SampleAnnotation:\"TestAnnotation2"
                + " value\"},<{Collation:\"TestAnnotation1 value\"}<_,_>>>");
    assertThat(annotationMap.debugString(COLLATION_ANNOTATION_ID))
        .isEqualTo("\"TestAnnotation1 value\"<,<\"TestAnnotation1 value\"<_,_>>>");
    assertThat(annotationMap.debugString(SAMPLE_ANNOTATION_ID))
        .isEqualTo("<\"TestAnnotation2 value\",<<_,_>>>");
  }

  @Test
  public void debugString_simpleArrayAnnotationMap() {
    AnnotationMap annotationMap = AnnotationMapTestConstants.createSimpleArrayAnnotationMap();

    assertThat(annotationMap.debugString())
        .isEqualTo(
            "{Collation:\"TestAnnotation1 value\"}<{SampleAnnotation:\"TestAnnotation2 value\"}>");
    assertThat(annotationMap.debugString(COLLATION_ANNOTATION_ID))
        .isEqualTo("\"TestAnnotation1 value\"<>");
    assertThat(annotationMap.debugString(SAMPLE_ANNOTATION_ID))
        .isEqualTo("<\"TestAnnotation2 value\">");
  }

  @Test
  public void debugString_nestedArrayAnnotationMap() {
    // ARRAY<STRUCT<a STRING, b ARRAY<INT64>>>
    AnnotationMap annotationMap = AnnotationMapTestConstants.createNestedArrayAnnotationMap();

    assertThat(annotationMap.debugString())
        .isEqualTo(
            "{Collation:\"TestAnnotation1 value\"}<<{SampleAnnotation:\"TestAnnotation2"
                + " value\"},{Collation:\"TestAnnotation1 value\"}<{}>>>");
    assertThat(annotationMap.debugString(COLLATION_ANNOTATION_ID))
        .isEqualTo("\"TestAnnotation1 value\"<<,\"TestAnnotation1 value\"<>>>");
    assertThat(annotationMap.debugString(SAMPLE_ANNOTATION_ID))
        .isEqualTo("<<\"TestAnnotation2 value\",<>>>");

    annotationMap.normalize();

    assertThat(annotationMap.debugString())
        .isEqualTo(
            "{Collation:\"TestAnnotation1 value\"}<<{SampleAnnotation:\"TestAnnotation2"
                + " value\"},{Collation:\"TestAnnotation1 value\"}<_>>>");
    assertThat(annotationMap.debugString(COLLATION_ANNOTATION_ID))
        .isEqualTo("\"TestAnnotation1 value\"<<,\"TestAnnotation1 value\"<_>>>");
    assertThat(annotationMap.debugString(SAMPLE_ANNOTATION_ID))
        .isEqualTo("<<\"TestAnnotation2 value\",<_>>>");
  }

  @Test
  public void hasCompatibleStructure_simpleType_compatibleWithSimpleTypes() {
    AnnotationMap annotationMap = AnnotationMap.create(STRING_TYPE);

    assertThat(annotationMap.hasCompatibleStructure(STRING_TYPE)).isTrue();
    assertThat(annotationMap.hasCompatibleStructure(INT64_TYPE)).isTrue();
    assertThat(annotationMap.hasCompatibleStructure(SIMPLE_STRUCT_TYPE)).isFalse();
    assertThat(annotationMap.hasCompatibleStructure(SIMPLE_ARRAY_TYPE)).isFalse();
  }

  @Test
  public void hasCompatibleStructure_simpleStructType_compatibleWithSameStructType() {
    // STRUCT<a INT64, b STRING>
    AnnotationMap annotationMap = AnnotationMap.create(SIMPLE_STRUCT_TYPE);
    // STRUCT<a STRING>
    StructType oneFieldStructType =
        TypeFactory.createStructType(
            ImmutableList.of(new StructType.StructField("a", STRING_TYPE)));
    // STRUCT<a INT64, b ARRAY<STRING>>
    StructType mismatchedFieldTypeAndIncompatibleStructType =
        TypeFactory.createStructType(
            ImmutableList.of(
                new StructType.StructField("a", STRING_TYPE),
                new StructType.StructField("b", TypeFactory.createArrayType(INT64_TYPE))));
    // STRUCT<a INT64, b STRING>
    StructType mismatchedFieldTypeButCompatibleStructType =
        TypeFactory.createStructType(
            ImmutableList.of(
                new StructType.StructField("a", STRING_TYPE),
                new StructType.StructField("b", INT64_TYPE)));

    assertThat(annotationMap.hasCompatibleStructure(STRING_TYPE)).isFalse();
    assertThat(annotationMap.hasCompatibleStructure(SIMPLE_ARRAY_TYPE)).isFalse();
    assertThat(annotationMap.hasCompatibleStructure(SIMPLE_STRUCT_TYPE)).isTrue();
    assertThat(annotationMap.hasCompatibleStructure(oneFieldStructType)).isFalse();
    assertThat(annotationMap.hasCompatibleStructure(mismatchedFieldTypeAndIncompatibleStructType))
        .isFalse();
    assertThat(annotationMap.hasCompatibleStructure(mismatchedFieldTypeButCompatibleStructType))
        .isTrue();
  }

  @Test
  public void hasCompatibleStructure_simpleArrayType_compatibleWithSameArrayType() {
    // ARRAY<STRING>
    AnnotationMap annotationMap = AnnotationMap.create(SIMPLE_ARRAY_TYPE);

    assertThat(annotationMap.hasCompatibleStructure(STRING_TYPE)).isFalse();
    assertThat(annotationMap.hasCompatibleStructure(SIMPLE_STRUCT_TYPE)).isFalse();
    assertThat(annotationMap.hasCompatibleStructure(SIMPLE_ARRAY_TYPE)).isTrue();

    // mismatched but compatible type
    assertThat(annotationMap.hasCompatibleStructure(TypeFactory.createArrayType(INT64_TYPE)))
        .isTrue();
    assertThat(
            annotationMap.hasCompatibleStructure(TypeFactory.createArrayType(SIMPLE_STRUCT_TYPE)))
        .isFalse();
  }

  @Test
  public void hasCompatibleStructure_nestedStruct_compatibleWithSameNestedStruct() {
    // STRUCT<a STRING, b ARRAY<STRUCT<a STRING, b INT64>>>
    AnnotationMap annotationMap = AnnotationMap.create(NESTED_STRUCT_TYPE);

    assertThat(annotationMap.hasCompatibleStructure(SIMPLE_STRUCT_TYPE)).isFalse();
    assertThat(annotationMap.hasCompatibleStructure(SIMPLE_ARRAY_TYPE)).isFalse();
    assertThat(annotationMap.hasCompatibleStructure(NESTED_ARRAY_TYPE)).isFalse();
    assertThat(annotationMap.hasCompatibleStructure(NESTED_STRUCT_TYPE)).isTrue();
  }

  @Test
  public void hasCompatibleStructure_nestedArrayType_compatibleWithSameNestedArray() {
    // ARRAY<STRUCT<a STRING, b ARRAY<INT64>>>
    AnnotationMap annotationMap = AnnotationMap.create(NESTED_ARRAY_TYPE);

    assertThat(annotationMap.hasCompatibleStructure(SIMPLE_STRUCT_TYPE)).isFalse();
    assertThat(annotationMap.hasCompatibleStructure(SIMPLE_ARRAY_TYPE)).isFalse();
    assertThat(annotationMap.hasCompatibleStructure(NESTED_STRUCT_TYPE)).isFalse();
    assertThat(annotationMap.hasCompatibleStructure(NESTED_ARRAY_TYPE)).isTrue();
  }

  @Test
  public void hasCompatibleStructure_normalizedStruct_compatibleWithNormalizedStruct() {
    AnnotationMap emptyNestedStructAnnotationMap = AnnotationMap.create(NESTED_STRUCT_TYPE);
    AnnotationMap nestedStructAnnotationMap =
        AnnotationMapTestConstants.createNestedStructAnnotationMap();
    AnnotationMap emptyNestedArrayAnnotationMap = AnnotationMap.create(NESTED_ARRAY_TYPE);
    AnnotationMap nestedArrayAnnotationMap =
        AnnotationMapTestConstants.createNestedArrayAnnotationMap();

    assertThat(emptyNestedStructAnnotationMap.hasCompatibleStructure(NESTED_STRUCT_TYPE)).isTrue();
    assertThat(nestedStructAnnotationMap.hasCompatibleStructure(NESTED_STRUCT_TYPE)).isTrue();
    assertThat(emptyNestedArrayAnnotationMap.hasCompatibleStructure(NESTED_ARRAY_TYPE)).isTrue();
    assertThat(emptyNestedArrayAnnotationMap.hasCompatibleStructure(NESTED_ARRAY_TYPE)).isTrue();

    emptyNestedStructAnnotationMap.normalize();
    nestedStructAnnotationMap.normalize();
    emptyNestedArrayAnnotationMap.normalize();
    nestedArrayAnnotationMap.normalize();

    assertThat(emptyNestedStructAnnotationMap.hasCompatibleStructure(NESTED_STRUCT_TYPE)).isTrue();
    assertThat(nestedStructAnnotationMap.hasCompatibleStructure(NESTED_STRUCT_TYPE)).isTrue();
    assertThat(emptyNestedArrayAnnotationMap.hasCompatibleStructure(NESTED_ARRAY_TYPE)).isTrue();
    assertThat(emptyNestedArrayAnnotationMap.hasCompatibleStructure(NESTED_ARRAY_TYPE)).isTrue();
  }

  @Test
  public void serializeAndDeserialize_stringAnnotationMap() {
    // arrange
    AnnotationMap annotationMap = AnnotationMapTestConstants.createStringAnnotationMap();
    // act
    AnnotationMapProto proto = annotationMap.serialize();
    AnnotationMap deserializedAnnotationMap = AnnotationMap.deserialize(proto);
    // assert
    assertThat(deserializedAnnotationMap).isEqualTo(annotationMap);
    assertThat(deserializedAnnotationMap.debugString()).isEqualTo(annotationMap.debugString());

    // arrange
    annotationMap.normalize();
    // act
    proto = annotationMap.serialize();
    deserializedAnnotationMap = AnnotationMap.deserialize(proto);
    // assert
    assertThat(deserializedAnnotationMap).isEqualTo(annotationMap);
    assertThat(deserializedAnnotationMap.debugString()).isEqualTo(annotationMap.debugString());
  }

  @Test
  public void serializeAndDeserialize_structAnnotationMap() {
    // arrange
    AnnotationMap simpleStructAnnotationMap =
        AnnotationMapTestConstants.createSimpleStructAnnotationMap();
    AnnotationMap nestedStructAnnotationMap =
        AnnotationMapTestConstants.createNestedStructAnnotationMap();
    // act
    AnnotationMapProto simpleStructProto = simpleStructAnnotationMap.serialize();
    AnnotationMapProto nestedStructProto = nestedStructAnnotationMap.serialize();
    AnnotationMap deserializedSimpleStructAnnotationMap =
        AnnotationMap.deserialize(simpleStructProto);
    AnnotationMap deserializedNestedStructAnnotationMap =
        AnnotationMap.deserialize(nestedStructProto);
    // assert
    assertThat(deserializedSimpleStructAnnotationMap).isEqualTo(simpleStructAnnotationMap);
    assertThat(deserializedNestedStructAnnotationMap).isEqualTo(nestedStructAnnotationMap);
    assertThat(deserializedSimpleStructAnnotationMap.debugString())
        .isEqualTo(simpleStructAnnotationMap.debugString());
    assertThat(deserializedNestedStructAnnotationMap.debugString())
        .isEqualTo(nestedStructAnnotationMap.debugString());

    // arrange
    simpleStructAnnotationMap.normalize();
    nestedStructAnnotationMap.normalize();
    // act
    simpleStructProto = simpleStructAnnotationMap.serialize();
    nestedStructProto = nestedStructAnnotationMap.serialize();
    deserializedSimpleStructAnnotationMap = AnnotationMap.deserialize(simpleStructProto);
    deserializedNestedStructAnnotationMap = AnnotationMap.deserialize(nestedStructProto);
    // assert
    assertThat(deserializedSimpleStructAnnotationMap).isEqualTo(simpleStructAnnotationMap);
    assertThat(deserializedNestedStructAnnotationMap).isEqualTo(nestedStructAnnotationMap);
    assertThat(deserializedSimpleStructAnnotationMap.debugString())
        .isEqualTo(simpleStructAnnotationMap.debugString());
    assertThat(deserializedNestedStructAnnotationMap.debugString())
        .isEqualTo(nestedStructAnnotationMap.debugString());
  }

  @Test
  public void serializeAndDeserialize_arrayAnnotationMap() {
    // arrange
    AnnotationMap simpleArrayAnnotationMap =
        AnnotationMapTestConstants.createSimpleArrayAnnotationMap();
    AnnotationMap nestedArrayAnnotationMap =
        AnnotationMapTestConstants.createNestedArrayAnnotationMap();
    // act
    AnnotationMapProto simpleArrayProto = simpleArrayAnnotationMap.serialize();
    AnnotationMapProto nestedArrayProto = nestedArrayAnnotationMap.serialize();
    AnnotationMap deserializedSimpleArrayAnnotationMap =
        AnnotationMap.deserialize(simpleArrayProto);
    AnnotationMap deserializedNestedArrayAnnotationMap =
        AnnotationMap.deserialize(nestedArrayProto);
    // assert
    assertThat(deserializedSimpleArrayAnnotationMap).isEqualTo(simpleArrayAnnotationMap);
    assertThat(deserializedNestedArrayAnnotationMap).isEqualTo(nestedArrayAnnotationMap);
    assertThat(deserializedSimpleArrayAnnotationMap.debugString())
        .isEqualTo(simpleArrayAnnotationMap.debugString());
    assertThat(deserializedNestedArrayAnnotationMap.debugString())
        .isEqualTo(nestedArrayAnnotationMap.debugString());

    // arrange
    simpleArrayAnnotationMap.normalize();
    nestedArrayAnnotationMap.normalize();
    // act
    simpleArrayProto = simpleArrayAnnotationMap.serialize();
    nestedArrayProto = nestedArrayAnnotationMap.serialize();
    deserializedSimpleArrayAnnotationMap = AnnotationMap.deserialize(simpleArrayProto);
    deserializedNestedArrayAnnotationMap = AnnotationMap.deserialize(nestedArrayProto);
    // assert
    assertThat(deserializedSimpleArrayAnnotationMap).isEqualTo(simpleArrayAnnotationMap);
    assertThat(deserializedNestedArrayAnnotationMap).isEqualTo(nestedArrayAnnotationMap);
    assertThat(deserializedSimpleArrayAnnotationMap.debugString())
        .isEqualTo(simpleArrayAnnotationMap.debugString());
    assertThat(deserializedNestedArrayAnnotationMap.debugString())
        .isEqualTo(nestedArrayAnnotationMap.debugString());
  }

  @Test
  public void serializeAndDeserialize_emptyAnnotationMap() {
    // arrange
    AnnotationMap emptyAnnotationMap = AnnotationMap.create(STRING_TYPE);
    AnnotationMap emptyNestedStructAnnotationMap = AnnotationMap.create(NESTED_STRUCT_TYPE);
    AnnotationMap emptyNestedArrayAnnotationMap = AnnotationMap.create(NESTED_ARRAY_TYPE);
    // act
    AnnotationMapProto emptyAnnotationMapProto = emptyAnnotationMap.serialize();
    AnnotationMapProto emptyNestedStructAnnotationMapProto =
        emptyNestedStructAnnotationMap.serialize();
    AnnotationMapProto emptyNestedArrayAnnotationMapProto =
        emptyNestedArrayAnnotationMap.serialize();
    AnnotationMap deserializedEmptyAnnotationMap =
        AnnotationMap.deserialize(emptyAnnotationMapProto);
    AnnotationMap deserializedEmptyNestedStructAnnotationMap =
        AnnotationMap.deserialize(emptyNestedStructAnnotationMapProto);
    AnnotationMap deserializedEmptyNestedArrayAnnotationMap =
        AnnotationMap.deserialize(emptyNestedArrayAnnotationMapProto);
    // assert
    assertThat(deserializedEmptyAnnotationMap).isEqualTo(emptyAnnotationMap);
    assertThat(deserializedEmptyNestedStructAnnotationMap)
        .isEqualTo(emptyNestedStructAnnotationMap);
    assertThat(deserializedEmptyNestedArrayAnnotationMap).isEqualTo(emptyNestedArrayAnnotationMap);
    assertThat(deserializedEmptyAnnotationMap.debugString())
        .isEqualTo(emptyAnnotationMap.debugString());
    assertThat(deserializedEmptyNestedStructAnnotationMap.debugString())
        .isEqualTo(emptyNestedStructAnnotationMap.debugString());
    assertThat(deserializedEmptyNestedArrayAnnotationMap.debugString())
        .isEqualTo(emptyNestedArrayAnnotationMap.debugString());

    // arrange
    emptyAnnotationMap.normalize();
    emptyNestedStructAnnotationMap.normalize();
    emptyNestedArrayAnnotationMap.normalize();
    // act
    emptyAnnotationMapProto = emptyAnnotationMap.serialize();
    emptyNestedStructAnnotationMapProto = emptyNestedStructAnnotationMap.serialize();
    emptyNestedArrayAnnotationMapProto = emptyNestedArrayAnnotationMap.serialize();
    deserializedEmptyAnnotationMap = AnnotationMap.deserialize(emptyAnnotationMapProto);
    deserializedEmptyNestedStructAnnotationMap =
        AnnotationMap.deserialize(emptyNestedStructAnnotationMapProto);
    deserializedEmptyNestedArrayAnnotationMap =
        AnnotationMap.deserialize(emptyNestedArrayAnnotationMapProto);
    // assert
    assertThat(deserializedEmptyAnnotationMap).isEqualTo(emptyAnnotationMap);
    assertThat(deserializedEmptyNestedStructAnnotationMap)
        .isEqualTo(emptyNestedStructAnnotationMap);
    assertThat(deserializedEmptyNestedArrayAnnotationMap).isEqualTo(emptyNestedArrayAnnotationMap);
    assertThat(deserializedEmptyAnnotationMap.debugString())
        .isEqualTo(emptyAnnotationMap.debugString());
    assertThat(deserializedEmptyNestedStructAnnotationMap.debugString())
        .isEqualTo(emptyNestedStructAnnotationMap.debugString());
    assertThat(deserializedEmptyNestedArrayAnnotationMap.debugString())
        .isEqualTo(emptyNestedArrayAnnotationMap.debugString());
  }

  @Test
  public void normalize_stringAnnotationMap() {
    AnnotationMap annotationMap = AnnotationMapTestConstants.createStringAnnotationMap();

    assertThat(annotationMap.isNormailized()).isTrue();

    annotationMap.normalize();

    assertThat(annotationMap.debugString()).isEqualTo("{Collation:\"TestAnnotation1 value\"}");
  }

  @Test
  public void normalize_emptyAnnotationMap() {
    AnnotationMap emptyAnnotationMap = AnnotationMap.create(STRING_TYPE);
    AnnotationMap nestedEmptyStructAnnotationMap = AnnotationMap.create(NESTED_STRUCT_TYPE);
    AnnotationMap nestedEmptyArrayAnnotationMap = AnnotationMap.create(NESTED_ARRAY_TYPE);

    assertThat(emptyAnnotationMap.isNormailized()).isTrue();
    assertThat(nestedEmptyStructAnnotationMap.isNormailized()).isFalse();
    assertThat(nestedEmptyArrayAnnotationMap.isNormailized()).isFalse();

    assertThat(emptyAnnotationMap.debugString()).isEmpty();
    // STRUCT<a STRING, b ARRAY<STRUCT<a STRING, b INT64>>>
    assertThat(nestedEmptyStructAnnotationMap.debugString()).isEqualTo("<{},<<{},{}>>>");
    // ARRAY<STRUCT<a STRING, b ARRAY<INT64>>>
    assertThat(nestedEmptyArrayAnnotationMap.debugString()).isEqualTo("<<{},<{}>>>");

    emptyAnnotationMap.normalize();
    nestedEmptyStructAnnotationMap.normalize();
    nestedEmptyArrayAnnotationMap.normalize();

    assertThat(emptyAnnotationMap.debugString()).isEmpty();
    assertThat(nestedEmptyStructAnnotationMap.debugString()).isEqualTo("<_,_>");
    assertThat(nestedEmptyArrayAnnotationMap.debugString()).isEqualTo("<_>");
    assertThat(emptyAnnotationMap.isNormailized()).isTrue();
    assertThat(nestedEmptyStructAnnotationMap.isNormailized()).isTrue();
    assertThat(nestedEmptyArrayAnnotationMap.isNormailized()).isTrue();
  }

  @Test
  public void normalize_structAnnotationMap() {
    AnnotationMap simpleStructAnnotationMap =
        AnnotationMapTestConstants.createSimpleStructAnnotationMap();
    AnnotationMap nestedStructAnnotationMap =
        AnnotationMapTestConstants.createNestedStructAnnotationMap();

    assertThat(simpleStructAnnotationMap.isNormailized()).isFalse();
    assertThat(nestedStructAnnotationMap.isNormailized()).isFalse();

    simpleStructAnnotationMap.normalize();
    nestedStructAnnotationMap.normalize();

    assertThat(simpleStructAnnotationMap.isNormailized()).isTrue();
    assertThat(nestedStructAnnotationMap.isNormailized()).isTrue();
    assertThat(simpleStructAnnotationMap.debugString())
        .isEqualTo(
            "{Collation:\"TestAnnotation1 value\"}<{SampleAnnotation:\"TestAnnotation2"
                + " value\"},_>");
    assertThat(nestedStructAnnotationMap.debugString())
        .isEqualTo(
            "{Collation:\"TestAnnotation1 value\"}<{SampleAnnotation:\"TestAnnotation2"
                + " value\"},<{Collation:\"TestAnnotation1 value\"}<_,_>>>");
  }

  @Test
  public void normalize_arrayAnnotationMap() {
    AnnotationMap simpleArrayAnnotationMap =
        AnnotationMapTestConstants.createSimpleArrayAnnotationMap();
    AnnotationMap nestedArrayAnnotationMap =
        AnnotationMapTestConstants.createNestedArrayAnnotationMap();

    assertThat(simpleArrayAnnotationMap.isNormailized()).isTrue();
    assertThat(nestedArrayAnnotationMap.isNormailized()).isFalse();

    simpleArrayAnnotationMap.normalize();
    nestedArrayAnnotationMap.normalize();

    assertThat(simpleArrayAnnotationMap.isNormailized()).isTrue();
    assertThat(simpleArrayAnnotationMap.isNormailized()).isTrue();
    assertThat(simpleArrayAnnotationMap.debugString())
        .isEqualTo(
            "{Collation:\"TestAnnotation1 value\"}<{SampleAnnotation:\"TestAnnotation2 value\"}>");
    assertThat(nestedArrayAnnotationMap.debugString())
        .isEqualTo(
            "{Collation:\"TestAnnotation1 value\"}<<{SampleAnnotation:\"TestAnnotation2"
                + " value\"},{Collation:\"TestAnnotation1 value\"}<_>>>");
  }

  @Test
  public void normailize_annotationMapWithNullField() {
    AnnotationMap nestedStructAnnotationMap = AnnotationMap.create(NESTED_STRUCT_TYPE);
    AnnotationMap nestedArrayAnnotationMap = AnnotationMap.create(NESTED_ARRAY_TYPE);
    nestedStructAnnotationMap.normalize();
    nestedArrayAnnotationMap.normalize();

    // There are null nested annotation map now, normalize again
    nestedStructAnnotationMap.normalize();
    nestedArrayAnnotationMap.normalize();

    assertThat(nestedStructAnnotationMap.debugString()).isEqualTo("<_,_>");
    assertThat(nestedArrayAnnotationMap.debugString()).isEqualTo("<_>");
  }

  @Test
  public void getAnnotationKindName_variousAnnotationKind() {
    assertThat(AnnotationMap.getAnnotationKindName(AnnotationMap.AnnotationKind.COLLATION))
        .isEqualTo("Collation");
    assertThat(AnnotationMap.getAnnotationKindName(AnnotationMap.AnnotationKind.SAMPLE_ANNOTATION))
        .isEqualTo("SampleAnnotation");
    assertThat(
            AnnotationMap.getAnnotationKindName(
                AnnotationMap.AnnotationKind.MAX_BUILTIN_ANNOTATION_KIND))
        .isEqualTo("MaxBuiltinAnnotationKind");
  }
}
