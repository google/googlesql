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
import com.google.googlesql.SimplePropertyGraphProtos.SimpleGraphNodeTableReferenceProto;
import java.util.List;
import java.util.Map;

/** A concrete implementation of the {@link GraphNodeTableReference} interface. */
public final class SimpleGraphNodeTableReference implements GraphNodeTableReference {
  private final GraphNodeTable nodeTable;
  private final List<Integer> nodeTableColumns;
  private final List<Integer> edgeTableColumns;

  public SimpleGraphNodeTableReference(
      GraphNodeTable nodeTable, List<Integer> nodeTableColumns, List<Integer> edgeTableColumns) {
    this.nodeTable = nodeTable;
    this.nodeTableColumns = nodeTableColumns;
    this.edgeTableColumns = edgeTableColumns;
  }

  public SimpleGraphNodeTableReferenceProto serialize() {
    return SimpleGraphNodeTableReferenceProto.newBuilder()
        .setNodeTableName(nodeTable.getName())
        .addAllNodeTableColumns(nodeTableColumns)
        .addAllEdgeTableColumns(edgeTableColumns)
        .build();
  }

  public static SimpleGraphNodeTableReference deserialize(
      SimpleGraphNodeTableReferenceProto proto, Map<String, SimpleGraphNodeTable> nodeTableMap) {
    return new SimpleGraphNodeTableReference(
        nodeTableMap.get(proto.getNodeTableName()),
        proto.getNodeTableColumnsList(),
        proto.getEdgeTableColumnsList());
  }

  @Override
  public GraphNodeTable getReferencedNodeTable() {
    return nodeTable;
  }

  @Override
  public ImmutableList<Integer> getEdgeTableColumns() {
    return ImmutableList.copyOf(edgeTableColumns);
  }

  @Override
  public ImmutableList<Integer> getNodeTableColumns() {
    return ImmutableList.copyOf(nodeTableColumns);
  }
}
