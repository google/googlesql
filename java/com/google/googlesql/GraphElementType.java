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
import static com.google.common.collect.ImmutableSortedMap.toImmutableSortedMap;
import static java.util.function.Function.identity;
import static java.util.stream.Collectors.joining;

import com.google.common.base.Ascii;
import com.google.common.base.Joiner;
import com.google.common.collect.ImmutableList;
import com.google.common.collect.ImmutableSortedMap;
import com.google.common.hash.HashCode;
import com.google.common.hash.Hashing;
import com.google.errorprone.annotations.Immutable;
import com.google.googlesql.GoogleSQLOptions.ProductMode;
import com.google.googlesql.GoogleSQLType.GraphElementTypeProto;
import com.google.googlesql.GoogleSQLType.GraphElementTypeProto.ElementKind;
import com.google.googlesql.GoogleSQLType.GraphElementTypeProto.PropertyTypeProto;
import com.google.googlesql.GoogleSQLType.TypeKind;
import com.google.googlesql.GoogleSQLType.TypeProto;
import java.io.Serializable;
import java.util.ArrayList;
import java.util.Comparator;
import java.util.List;
import java.util.Objects;
import java.util.Set;

/**
 * A graph element type. GraphElementType contains a set of property types. Property has an unique
 * name and a type.
 */
public class GraphElementType extends Type {
  static boolean equalsImpl(GraphElementType type1, GraphElementType type2, boolean equivalent) {
    if (type1.getGraphReference().size() != type2.getGraphReference().size()) {
      return false;
    }

    for (int i = 0; i < type1.getGraphReference().size(); ++i) {
      if (!Ascii.equalsIgnoreCase(
          type1.getGraphReference().get(i), type2.getGraphReference().get(i))) {
        return false;
      }
    }

    if (type1.isDynamic() != type2.isDynamic()) {
      return false;
    }
    if (type1.elementKind != type2.elementKind) {
      return false;
    }

    ImmutableList<PropertyType> propertyTypeList1 = type1.getPropertyTypeList();
    ImmutableList<PropertyType> propertyTypeList2 = type2.getPropertyTypeList();

    if (propertyTypeList1.size() != propertyTypeList2.size()) {
      return false;
    }

    for (int i = 0; i < propertyTypeList1.size(); ++i) {
      if (!propertyTypeList1.get(i).equalsImpl(propertyTypeList2.get(i), equivalent)) {
        return false;
      }
    }
    return true;
  }

  private final ImmutableList<String> graphReference;
  private final String graphReferenceString;
  private final ElementKind elementKind;
  private final boolean isDynamic;
  private final ImmutableSortedMap<String, PropertyType> propertyTypes;

  /** Private constructor, instances must be created with {@link TypeFactory} */
  GraphElementType(
      List<String> graphReference,
      ElementKind elementKind,
      boolean isDynamic,
      Set<PropertyType> propertyTypes) {
    super(TypeKind.TYPE_GRAPH_ELEMENT);
    this.graphReference = ImmutableList.copyOf(graphReference);
    this.graphReferenceString =
        this.graphReference.stream()
            .map(GoogleSQLStrings::toIdentifierLiteral)
            .collect(joining("."));
    this.elementKind = elementKind;
    this.isDynamic = isDynamic;
    this.propertyTypes =
        propertyTypes.stream()
            .collect(
                toImmutableSortedMap(
                    String.CASE_INSENSITIVE_ORDER, PropertyType::getName, identity()));
  }

  public ImmutableList<String> getGraphReference() {
    return graphReference;
  }

  public ElementKind getElementKind() {
    return elementKind;
  }

  public boolean isDynamic() {
    return isDynamic;
  }

  public int getPropertyTypeCount() {
    return propertyTypes.size();
  }

  public ImmutableList<PropertyType> getPropertyTypeList() {
    return ImmutableList.copyOf(propertyTypes.values());
  }

  public PropertyType getPropertyType(String propertyName) {
    return propertyTypes.get(propertyName);
  }

  @Override
  public ImmutableList<Type> componentTypes() {
    return propertyTypes.values().stream()
        .sorted(Comparator.comparing(PropertyType::getName))
        .map(PropertyType::getType)
        .collect(toImmutableList());
  }

  @Override
  public void serialize(
      TypeProto.Builder typeProtoBuilder, FileDescriptorSetsBuilder fileDescriptorSetsBuilder) {
    typeProtoBuilder.setTypeKind(getKind());
    GraphElementTypeProto.Builder graphElementTypeProto =
        typeProtoBuilder.getGraphElementTypeBuilder();
    graphElementTypeProto.addAllGraphReference(getGraphReference());
    graphElementTypeProto.setKind(getElementKind());
    graphElementTypeProto.setIsDynamic(isDynamic());
    for (PropertyType propertyType : propertyTypes.values()) {
      PropertyTypeProto.Builder propertyTypeBuilder =
          graphElementTypeProto.addPropertyTypeBuilder();
      propertyTypeBuilder.setName(propertyType.getName());
      propertyType
          .getType()
          .serialize(propertyTypeBuilder.getValueTypeBuilder(), fileDescriptorSetsBuilder);
    }
  }

  @Override
  public int hashCode() {
    List<HashCode> hashCodes = new ArrayList<>();
    hashCodes.add(HashCode.fromInt(getKind().getNumber()));
    hashCodes.add(HashCode.fromInt(Objects.hashCode(Ascii.toLowerCase(graphReferenceString))));
    hashCodes.add(HashCode.fromInt(isDynamic ? 1 : 0));
    hashCodes.add(HashCode.fromInt(getElementKind().getNumber()));
    for (PropertyType propertyType : propertyTypes.values()) {
      hashCodes.add(HashCode.fromInt(propertyType.hashCode()));
    }
    return Hashing.combineOrdered(hashCodes).asInt();
  }

  /** PropertyType contained in a GraphElementType, representing a name and a type. */
  @Immutable
  public static class PropertyType implements Serializable {
    private final String name;
    private final Type valueType;

    public PropertyType(String name, Type valueType) {
      this.name = name;
      this.valueType = valueType;
    }

    public String getName() {
      return name;
    }

    public Type getType() {
      return valueType;
    }

    public boolean equivalent(PropertyType other) {
      return equalsImpl(other, /* equivalent= */ true);
    }

    public String getTypeName(ProductMode productMode) {
      return String.format(
          "%s %s", GoogleSQLStrings.toIdentifierLiteral(name), valueType.typeName(productMode));
    }

    public String getDebugString(boolean details) {
      return String.format(
          "%s %s", GoogleSQLStrings.toIdentifierLiteral(name), valueType.debugString(details));
    }

    /**
     * Note: {@code equalsImpl} is used in {@code equals}, where this reference equality pattern is
     * allowed.
     */
    @SuppressWarnings("ReferenceEquality")
    boolean equalsImpl(PropertyType other, boolean equivalent) {
      // Skip the name, value type comparison when <this> and <other> are the
      // same object.
      if (this == other) {
        return true;
      }
      if (other == null) {
        return false;
      }
      return Ascii.equalsIgnoreCase(name, other.name)
          && valueType.equalsInternal(other.valueType, equivalent);
    }

    @Override
    public boolean equals(Object other) {
      return (other instanceof PropertyType)
          && equalsImpl((PropertyType) other, /* equivalent= */ false);
    }

    @Override
    public int hashCode() {
      return Objects.hash(Ascii.toLowerCase(name), valueType);
    }
  }

  String typeElementKindName() {
    return getElementKind() == ElementKind.KIND_NODE ? "GRAPH_NODE" : "GRAPH_EDGE";
  }

  String typeNameImpl(List<String> propertyTypeNames) {
    String suffix = "";
    if (isDynamic()) {
      suffix = ", DYNAMIC";
    }
    return String.format(
        "%s(%s)<%s%s>",
        typeElementKindName(),
        graphReferenceString,
        Joiner.on(", ").join(propertyTypeNames),
        suffix);
  }

  @Override
  public String typeName(ProductMode productMode) {
    return typeNameImpl(
        propertyTypes.values().stream()
            .map(propertyType -> propertyType.getTypeName(productMode))
            .collect(toImmutableList()));
  }

  @Override
  public String debugString(boolean details) {
    return typeNameImpl(
        propertyTypes.values().stream()
            .map(propertyType -> propertyType.getDebugString(details))
            .collect(toImmutableList()));
  }

  @Override
  public GraphElementType asGraphElement() {
    return this;
  }
}
