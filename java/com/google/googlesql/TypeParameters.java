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

import static com.google.common.collect.ImmutableList.toImmutableList;

import com.google.common.base.Joiner;
import com.google.common.base.Preconditions;
import com.google.googlesql.GoogleSQLTypeParameters.ExtendedTypeParametersProto;
import com.google.googlesql.GoogleSQLTypeParameters.NumericTypeParametersProto;
import com.google.googlesql.GoogleSQLTypeParameters.StringTypeParametersProto;
import com.google.googlesql.GoogleSQLTypeParameters.TimestampTypeParametersProto;
import com.google.googlesql.GoogleSQLTypeParameters.TypeParametersProto;
import java.io.Serializable;
import java.util.ArrayList;
import java.util.List;
import java.util.Objects;

/**
 * Type parameters for erasable types (parameterized types), for example STRING(L) is an erasable
 * string type with length limit. L is the type parameter of STRING(L) type.
 *
 * <p>See (broken link) for details.
 */
public final class TypeParameters implements Serializable {
  /**
   * Store type parameters for simple types or extended types. Note, we don't use the child_list
   * field in TypeParametersProto.
   */
  private TypeParametersProto typeParametersProto = TypeParametersProto.getDefaultInstance();
  /** Stores type parameters for subfields for ARRAY/STRUCT types */
  private List<TypeParameters> childList = new ArrayList<>();

  /** Empty type parameters. */
  public TypeParameters() {}

  public TypeParameters(StringTypeParametersProto proto) {
    validateTypeParameters(proto);
    typeParametersProto = TypeParametersProto.newBuilder().setStringTypeParameters(proto).build();
  }

  public TypeParameters(NumericTypeParametersProto proto) {
    validateTypeParameters(proto);
    typeParametersProto = TypeParametersProto.newBuilder().setNumericTypeParameters(proto).build();
  }

  public TypeParameters(TimestampTypeParametersProto proto) {
    validateTypeParameters(proto);
    typeParametersProto =
        TypeParametersProto.newBuilder().setTimestampTypeParameters(proto).build();
  }

  public TypeParameters(ExtendedTypeParametersProto proto) {
    typeParametersProto = TypeParametersProto.newBuilder().setExtendedTypeParameters(proto).build();
  }

  public TypeParameters(List<TypeParameters> childList) {
    this.childList = childList;
  }

  public TypeParameters(ExtendedTypeParametersProto proto, List<TypeParameters> childList) {
    typeParametersProto = TypeParametersProto.newBuilder().setExtendedTypeParameters(proto).build();
    this.childList = childList;
  }

  public TypeParametersProto getTypeParameterProto() {
    return typeParametersProto;
  }

  public StringTypeParametersProto getStringTypeParameters() {
    Preconditions.checkState(isStringTypeParameters());
    return typeParametersProto.getStringTypeParameters();
  }

  public NumericTypeParametersProto getNumericTypeParameters() {
    Preconditions.checkState(isNumericTypeParameters());
    return typeParametersProto.getNumericTypeParameters();
  }

  public TimestampTypeParametersProto getTimestampTypeParameters() {
    Preconditions.checkState(isTimestampTypeParameters());
    return typeParametersProto.getTimestampTypeParameters();
  }

  public ExtendedTypeParametersProto getExtendedTypeParameters() {
    Preconditions.checkState(isExtendedTypeParameters());
    return typeParametersProto.getExtendedTypeParameters();
  }

  public boolean isEmpty() {
    return !typeParametersProto.hasStringTypeParameters()
        && !typeParametersProto.hasNumericTypeParameters()
        && !typeParametersProto.hasTimestampTypeParameters()
        && !typeParametersProto.hasExtendedTypeParameters()
        && childList.isEmpty();
  }

  public boolean isStringTypeParameters() {
    return typeParametersProto.hasStringTypeParameters();
  }

  public boolean isNumericTypeParameters() {
    return typeParametersProto.hasNumericTypeParameters();
  }

  public boolean isTimestampTypeParameters() {
    return typeParametersProto.hasTimestampTypeParameters();
  }

  public boolean isExtendedTypeParameters() {
    return typeParametersProto.hasExtendedTypeParameters();
  }

  public boolean isStructOrArrayParameters() {
    return getChildCount() > 0;
  }

  public int getChildCount() {
    return childList.size();
  }

  public TypeParameters getChild(int index) {
    return childList.get(index);
  }

  public List<TypeParameters> getChildList() {
    return childList;
  }

  public TypeParametersProto serialize() {
    // Copy internal typeParametersProto to a new TypeParametersProto object.
    TypeParametersProto.Builder builder = typeParametersProto.toBuilder();
    for (TypeParameters typeParameters : childList) {
      builder.addChildList(typeParameters.serialize());
    }
    return builder.build();
  }

  public static TypeParameters deserialize(TypeParametersProto proto) {
    Preconditions.checkNotNull(proto);
    if (proto.hasStringTypeParameters()) {
      return new TypeParameters(proto.getStringTypeParameters());
    }
    if (proto.hasNumericTypeParameters()) {
      return new TypeParameters(proto.getNumericTypeParameters());
    }
    if (proto.hasTimestampTypeParameters()) {
      return new TypeParameters(proto.getTimestampTypeParameters());
    }
    List<TypeParameters> childList = new ArrayList<>();
    for (TypeParametersProto child : proto.getChildListList()) {
      childList.add(deserialize(child));
    }
    if (proto.hasExtendedTypeParameters()) {
      return new TypeParameters(proto.getExtendedTypeParameters(), childList);
    }
    return new TypeParameters(childList);
  }

  @Override
  public boolean equals(Object other) {
    if (this == other) {
      return true;
    }
    if (!(other instanceof TypeParameters)) {
      return false;
    }
    TypeParameters theOther = (TypeParameters) other;
    if (!typeParametersProto.equals(theOther.getTypeParameterProto())) {
      return false;
    }
    List<TypeParameters> otherChildList = theOther.getChildList();
    if (childList.size() != otherChildList.size()) {
      return false;
    }
    for (int i = 0; i < childList.size(); ++i) {
      if (!childList.get(i).equals(otherChildList.get(i))) {
        return false;
      }
    }
    return true;
  }

  @Override
  public int hashCode() {
    return Objects.hash(typeParametersProto, childList);
  }

  public String debugString() {
    if (isStringTypeParameters()) {
      return stringTypeParametersDebugString(typeParametersProto.getStringTypeParameters());
    }
    if (isNumericTypeParameters()) {
      return numericTypeParametersDebugString(typeParametersProto.getNumericTypeParameters());
    }
    if (isTimestampTypeParameters()) {
      return timestampTypeParametersDebugString(typeParametersProto.getTimestampTypeParameters());
    }
    // Extended type may has childList.
    StringBuilder debugStringBuilder = new StringBuilder();
    if (isExtendedTypeParameters()) {
      debugStringBuilder.append(extendedTypeParametersDebugString());
    }
    // STRUCT or ARRAY type whose subfields has parameters.
    if (!childList.isEmpty()) {
      debugStringBuilder.append(arrayOrStructTypeParametersDebugString(childList));
    }
    String debugString = debugStringBuilder.toString();
    if (!debugString.isEmpty()) {
      return debugString;
    }
    // Type without parameters, usually are subfields in a struct or array.
    return "null";
  }

  private static String stringTypeParametersDebugString(
      StringTypeParametersProto stringTypeParameters) {
    if (stringTypeParameters.getIsMaxLength()) {
      return "(max_length=MAX)";
    }
    return "(max_length=" + stringTypeParameters.getMaxLength() + ")";
  }

  private static String numericTypeParametersDebugString(NumericTypeParametersProto parameters) {
    String precision =
        parameters.hasIsMaxPrecision() ? "MAX" : String.valueOf(parameters.getPrecision());
    long scale = parameters.getScale();
    return "(precision=" + precision + ",scale=" + scale + ")";
  }

  private static String timestampTypeParametersDebugString(
      TimestampTypeParametersProto parameters) {
    return "(precision=" + parameters.getPrecision() + ")";
  }

  private static String extendedTypeParametersDebugString() {
    // TODO: Return dummy debug string now. We need add SimpleValue.java class first to
    // implement proper extended type parameters.
    return "Dummy extended type parameters";
  }

  private static String arrayOrStructTypeParametersDebugString(List<TypeParameters> childList) {
    StringBuilder sb = new StringBuilder();
    sb.append("[");
    Joiner.on(",")
        .appendTo(
            sb, childList.stream().map(TypeParameters::debugString).collect(toImmutableList()));
    sb.append("]");
    return sb.toString();
  }

  private static void validateTypeParameters(NumericTypeParametersProto proto) {
    long precision = proto.getPrecision();
    long scale = proto.getScale();
    if (proto.hasIsMaxPrecision()) {
      Preconditions.checkArgument(
          proto.getIsMaxPrecision(), "is_max_precision should either be unset or true");
    } else {
      Preconditions.checkArgument(
          precision >= 1 && precision <= 76,
          "precision must be within range [1, 76] or MAX, actual precision: %s",
          String.valueOf(precision));
      Preconditions.checkArgument(
          precision >= scale,
          "precision must be equal to or larger than scale, actual precision: %s, scale: %s",
          String.valueOf(precision),
          String.valueOf(scale));
    }
    Preconditions.checkArgument(
        scale >= 0 && scale <= 38,
        "scale must be within range [0, 38], actual scale: %s",
        String.valueOf(scale));
  }

  private static void validateTypeParameters(TimestampTypeParametersProto proto) {
    long precision = proto.getPrecision();
    Preconditions.checkArgument(
        precision >= 0 && precision <= 12 && precision % 3 == 0,
        "precision can only be 0, 3, 6, 9, or 12, actual precision: %s",
        String.valueOf(precision));
  }

  private static void validateTypeParameters(StringTypeParametersProto proto) {
    if (proto.hasIsMaxLength()) {
      Preconditions.checkArgument(
          proto.getIsMaxLength(), "is_max_length should either be unset or true");
    } else {
      Preconditions.checkArgument(
          proto.getMaxLength() > 0,
          "max_length must be larger than 0, actual max_length: %s",
          String.valueOf(proto.getMaxLength()));
    }
  }
}
