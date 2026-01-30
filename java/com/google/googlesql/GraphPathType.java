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
import com.google.common.hash.HashCode;
import com.google.common.hash.Hashing;
import com.google.googlesql.GoogleSQLOptions.ProductMode;
import com.google.googlesql.GoogleSQLType.TypeKind;
import com.google.googlesql.GoogleSQLType.TypeProto;
import java.util.Arrays;

/** A graph path type. GraphPathType contains the type of all nodes and edges in the path. */
public class GraphPathType extends Type {

  private final GraphElementType nodeType;
  private final GraphElementType edgeType;

  GraphPathType(GraphElementType nodeType, GraphElementType edgeType) {
    super(TypeKind.TYPE_GRAPH_PATH);
    this.nodeType = nodeType;
    this.edgeType = edgeType;
  }

  static boolean equalsImpl(GraphPathType type1, GraphPathType type2, boolean equivalent) {
    return GraphElementType.equalsImpl(type1.nodeType, type2.nodeType, equivalent)
        && GraphElementType.equalsImpl(type1.edgeType, type2.edgeType, equivalent);
  }

  @Override
  public int hashCode() {
    return Hashing.combineOrdered(
            Arrays.asList(
                HashCode.fromInt(nodeType.hashCode()), HashCode.fromInt(edgeType.hashCode())))
        .asInt();
  }

  @Override
  public void serialize(
      TypeProto.Builder typeProtoBuilder, FileDescriptorSetsBuilder fileDescriptorSetsBuilder) {
    TypeProto.Builder nodeTypeProtoBuilder = TypeProto.newBuilder();
    nodeType.serialize(nodeTypeProtoBuilder, fileDescriptorSetsBuilder);
    TypeProto.Builder edgeTypeProtoBuilder = TypeProto.newBuilder();
    edgeType.serialize(edgeTypeProtoBuilder, fileDescriptorSetsBuilder);
    typeProtoBuilder.setTypeKind(TypeKind.TYPE_GRAPH_PATH);
    typeProtoBuilder
        .getGraphPathTypeBuilder()
        .setNodeType(nodeTypeProtoBuilder.getGraphElementType())
        .setEdgeType(edgeTypeProtoBuilder.getGraphElementType());
  }

  @Override
  public String typeName(ProductMode productMode) {
    return String.format(
        "PATH<node: %s, edge: %s>", nodeType.typeName(productMode), edgeType.typeName(productMode));
  }

  @Override
  public String debugString(boolean details) {
    return String.format(
        "PATH<node: %s, edge: %s>", nodeType.debugString(details), edgeType.debugString(details));
  }

  @Override
  public GraphPathType asGraphPath() {
    return this;
  }

  GraphElementType getNodeType() {
    return nodeType;
  }

  GraphElementType getEdgeType() {
    return edgeType;
  }

  @Override
  public ImmutableList<Type> componentTypes() {
    return ImmutableList.of(nodeType, edgeType);
  }
}
