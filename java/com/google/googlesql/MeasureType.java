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
import com.google.googlesql.GoogleSQLOptions.ProductMode;
import com.google.googlesql.GoogleSQLType.MeasureTypeProto;
import com.google.googlesql.GoogleSQLType.TypeKind;
import com.google.googlesql.GoogleSQLType.TypeProto;
import java.util.Objects;

/** Represents a MEASURE<T> type where T is the result type when the measure is aggregated. */
final class MeasureType extends Type {
  private final Type resultType;

  /** Private constructor, instances must be created with {@link TypeFactory} */
  MeasureType(Type resultType) {
    super(TypeKind.TYPE_MEASURE);
    this.resultType = resultType;
  }

  public Type getResultType() {
    return resultType;
  }

  @Override
  public ImmutableList<Type> componentTypes() {
    return ImmutableList.of(resultType);
  }

  @Override
  public void serialize(
      TypeProto.Builder typeProtoBuilder, FileDescriptorSetsBuilder fileDescriptorSetsBuilder) {
    typeProtoBuilder.setTypeKind(getKind());
    MeasureTypeProto.Builder measure = typeProtoBuilder.getMeasureTypeBuilder();
    resultType.serialize(measure.getResultTypeBuilder(), fileDescriptorSetsBuilder);
  }

  @Override
  public int hashCode() {
    return Objects.hash(resultType, getKind());
  }

  @Override
  public String typeName(ProductMode productMode) {
    return String.format("MEASURE<%s>", resultType.typeName(productMode));
  }

  @Override
  public String debugString(boolean details) {
    return String.format("MEASURE<%s>", resultType.debugString(details));
  }

  @Override
  public MeasureType asMeasure() {
    return this;
  }
}
