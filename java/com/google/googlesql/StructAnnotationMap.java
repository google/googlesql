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

import com.google.common.base.Preconditions;
import com.google.common.collect.ImmutableList;
import com.google.googlesql.GoogleSQLAnnotation.AnnotationMapProto;
import com.google.googlesql.StructType.StructField;
import java.util.ArrayList;
import java.util.List;
import java.util.Optional;

/**
 * An {@link AnnotationMap} that represents a {@link StructType} type. In addition to the annotation
 * on the whole type, this class also keeps an {@link AnnotationMap} for each field of the {@link
 * StructType}.
 */
public final class StructAnnotationMap extends AnnotationMap {
  /**
   * AnnotationMap on each struct field. Number of fields always match the number of fields of the
   * struct type that is used to create this {@link StructAnnotationMap}. Each field can be null,
   * which incidates that the {@link AnnotationMap} for the field is empty.
   */
  private final List<AnnotationMap> fields;

  /** Creates a {@link StructArrayMap} with the given {@link StructType}. */
  static StructAnnotationMap create(StructType type) {
    return new StructAnnotationMap(type);
  }

  /** Creates an empty {@link StructArrayMap}. */
  static StructAnnotationMap create() {
    return new StructAnnotationMap();
  }

  static StructAnnotationMap create(ImmutableList<Type> componentTypes) {
    return new StructAnnotationMap(componentTypes);
  }

  @Override
  public boolean isStructMap() {
    return true;
  }

  @Override
  public StructAnnotationMap asStructMap() {
    return this;
  }

  /**
   * Gets the {@link AnnotationMap} for the field with index.
   *
   * <p>The returned value will be null if the field at this index is null.
   */
  public final AnnotationMap getField(int index) {
    Preconditions.checkElementIndex(index, getFieldCount());
    return fields.get(index);
  }

  /** Returns the number fields in this {@link StructAnnotationMap}. */
  public final int getFieldCount() {
    return fields.size();
  }

  @Override
  protected String debugStringInternal(Optional<Integer> annotationSpecId) {
    StringBuilder builder = new StringBuilder(super.debugStringInternal(annotationSpecId));
    builder.append("<");
    for (int i = 0; i < getFieldCount(); i++) {
      String fieldDebugString =
          fields.get(i) == null ? "_" : fields.get(i).debugStringInternal(annotationSpecId);
      if (fieldDebugString.isEmpty() && !annotationSpecId.isPresent()) {
        fieldDebugString = "{}";
      }
      builder.append(fieldDebugString);
      if (i != getFieldCount() - 1) {
        builder.append(",");
      }
    }
    builder.append(">");
    return builder.toString();
  }

  @Override
  public AnnotationMapProto serialize() {
    AnnotationMapProto proto = super.serialize();
    AnnotationMapProto.Builder builder = proto.toBuilder();
    for (AnnotationMap field : fields) {
      if (field == null) {
        builder.addStructFields(AnnotationMapProto.newBuilder().setIsNull(true).build());
      } else {
        builder.addStructFields(field.serialize());
      }
    }
    return builder.build();
  }

  void addField(AnnotationMap field) {
    fields.add(field);
  }

  void setField(int index, AnnotationMap field) {
    Preconditions.checkElementIndex(index, fields.size());
    fields.set(index, field);
  }

  List<AnnotationMap> getFields() {
    return fields;
  }

  // TODO: Keeping this temporarily until all callers are migrated to use the
  // component_types() call.
  private StructAnnotationMap(StructType structType) {
    fields = new ArrayList<>();
    for (StructField field : structType.getFieldList()) {
      fields.add(AnnotationMap.create(field.getType()));
    }
  }

  private StructAnnotationMap() {
    this(ImmutableList.of());
  }

  private StructAnnotationMap(ImmutableList<Type> componentTypes) {
    fields = new ArrayList<>();
    for (Type componentType : componentTypes) {
      fields.add(AnnotationMap.create(componentType));
    }
  }
}
