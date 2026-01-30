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
import com.google.googlesql.GoogleSQLOptions.ProductMode;
import com.google.googlesql.GoogleSQLType.RangeTypeProto;
import com.google.googlesql.GoogleSQLType.TypeKind;
import com.google.googlesql.GoogleSQLType.TypeProto;
import java.util.Objects;

/** Represents a RANGE<T> type, where T is its element type. */
public class RangeType extends Type {
  static boolean isValidElementType(TypeKind elementType) {
    return elementType == TypeKind.TYPE_DATE
        || elementType == TypeKind.TYPE_DATETIME
        || elementType == TypeKind.TYPE_TIMESTAMP;
  }

  static boolean equalsImpl(RangeType type1, RangeType type2, boolean equivalent) {
    return type1.elementType.equalsInternal(type2.elementType, equivalent);
  }

  private final Type elementType;

  /** Private constructor, instances must be created with {@link TypeFactory} */
  RangeType(Type elementType) {
    super(TypeKind.TYPE_RANGE);
    Preconditions.checkArgument(
        isValidElementType(elementType.getKind()),
        "Invalid range element type: %s",
        elementType.getKind());
    this.elementType = elementType;
  }

  public Type getElementType() {
    return elementType;
  }

  @Override
  public ImmutableList<Type> componentTypes() {
    return ImmutableList.of(elementType);
  }

  @Override
  public void serialize(
      TypeProto.Builder typeProtoBuilder, FileDescriptorSetsBuilder fileDescriptorSetsBuilder) {
    typeProtoBuilder.setTypeKind(getKind());
    RangeTypeProto.Builder range = typeProtoBuilder.getRangeTypeBuilder();
    elementType.serialize(range.getElementTypeBuilder(), fileDescriptorSetsBuilder);
  }

  @Override
  public int hashCode() {
    return Objects.hash(elementType, getKind());
  }

  @Override
  public String typeName(ProductMode productMode) {
    return String.format("RANGE<%s>", elementType.typeName(productMode));
  }

  @Override
  public String debugString(boolean details) {
    return String.format("RANGE<%s>", elementType.debugString(details));
  }

  @Override
  public RangeType asRange() {
    return this;
  }
}
