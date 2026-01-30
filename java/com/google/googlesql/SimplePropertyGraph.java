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

import com.google.common.base.Ascii;
import com.google.common.base.Joiner;
import com.google.common.collect.ImmutableList;
import com.google.common.collect.ImmutableMap;
import com.google.common.collect.ImmutableSet;
import com.google.common.collect.Iterables;
import com.google.googlesql.SimplePropertyGraphProtos.SimpleGraphElementLabelProto;
import com.google.googlesql.SimplePropertyGraphProtos.SimpleGraphElementTableProto;
import com.google.googlesql.SimplePropertyGraphProtos.SimpleGraphPropertyDeclarationProto;
import com.google.googlesql.SimplePropertyGraphProtos.SimplePropertyGraphProto;
import java.io.Serializable;
import java.util.HashMap;
import java.util.List;
import java.util.Map;
import java.util.Set;
import java.util.TreeMap;

/** A concrete implementation of the {@link PropertyGraph} interface. */
public final class SimplePropertyGraph implements PropertyGraph, Serializable {
  private final ImmutableList<String> namePath;
  private final Map<String, GraphNodeTable> nodeTableMap;
  private final Map<String, GraphEdgeTable> edgeTableMap;
  private final Map<String, GraphElementLabel> labelMap;
  private final Map<String, GraphPropertyDeclaration> propertyDclMap;

  public SimplePropertyGraph(List<String> namePath) {
    this.namePath = ImmutableList.copyOf(namePath);
    nodeTableMap = new HashMap<>();
    edgeTableMap = new HashMap<>();
    labelMap = new HashMap<>();
    propertyDclMap = new HashMap<>();
  }

  public SimplePropertyGraph(
      List<String> namePath,
      Set<GraphNodeTable> nodeTables,
      Set<GraphEdgeTable> edgeTables,
      Set<GraphElementLabel> labels,
      Set<GraphPropertyDeclaration> propertyDcls) {
    this.namePath = ImmutableList.copyOf(namePath);

    nodeTableMap = new HashMap<>();
    edgeTableMap = new HashMap<>();
    labelMap = new HashMap<>();
    propertyDclMap = new HashMap<>();

    nodeTables.forEach(this::addNodeTable);
    edgeTables.forEach(this::addEdgeTable);
    labels.forEach(this::addLabel);
    propertyDcls.forEach(this::addPropertyDeclaration);
  }

  public void addNodeTable(GraphNodeTable nodeTable) {
    nodeTableMap.putIfAbsent(Ascii.toLowerCase(nodeTable.getName()), nodeTable);
  }

  public void addEdgeTable(GraphEdgeTable nodeTable) {
    edgeTableMap.putIfAbsent(Ascii.toLowerCase(nodeTable.getName()), nodeTable);
  }

  public void addLabel(GraphElementLabel label) {
    labelMap.putIfAbsent(Ascii.toLowerCase(label.getName()), label);
  }

  public void addPropertyDeclaration(GraphPropertyDeclaration propertyDeclaration) {
    propertyDclMap.putIfAbsent(
        Ascii.toLowerCase(propertyDeclaration.getName()), propertyDeclaration);
  }

  public SimplePropertyGraphProto serialize(FileDescriptorSetsBuilder fileDescriptorSetsBuilder) {
    SimplePropertyGraphProto.Builder proto =
        SimplePropertyGraphProto.newBuilder().addAllNamePath(namePath);

    // Sort by name to ensure it's deterministic
    TreeMap<String, GraphElementLabel> sortedLabelMap = new TreeMap<>(labelMap);
    for (String name : sortedLabelMap.keySet()) {
      GraphElementLabel label = sortedLabelMap.get(name);
      if (label instanceof SimpleGraphElementLabel) {
        proto.addLabels(((SimpleGraphElementLabel) label).serialize());
      } else {
        throw new IllegalArgumentException("Cannot serialize non-SimpleGraphElementLabel " + name);
      }
    }

    TreeMap<String, GraphNodeTable> sortedNodeTableMap = new TreeMap<>(nodeTableMap);
    for (String name : sortedNodeTableMap.keySet()) {
      GraphNodeTable nodeTable = sortedNodeTableMap.get(name);
      if (nodeTable instanceof SimpleGraphNodeTable) {
        proto.addNodeTables(
            ((SimpleGraphNodeTable) nodeTable).serialize(fileDescriptorSetsBuilder));
      } else {
        throw new IllegalArgumentException("Cannot serialize non-SimpleGraphNodeTable " + name);
      }
    }

    TreeMap<String, GraphEdgeTable> sortedEdgeTableMap = new TreeMap<>(edgeTableMap);
    for (String name : sortedEdgeTableMap.keySet()) {
      GraphEdgeTable edgeTable = sortedEdgeTableMap.get(name);
      if (edgeTable instanceof SimpleGraphEdgeTable) {
        proto.addEdgeTables(
            ((SimpleGraphEdgeTable) edgeTable).serialize(fileDescriptorSetsBuilder));
      } else {
        throw new IllegalArgumentException("Cannot serialize non-SimpleGraphEdgeTable " + name);
      }
    }

    TreeMap<String, GraphPropertyDeclaration> sortedPropertyDclMap = new TreeMap<>(propertyDclMap);
    for (String name : sortedPropertyDclMap.keySet()) {
      GraphPropertyDeclaration propertyDcl = sortedPropertyDclMap.get(name);
      if (propertyDcl instanceof SimpleGraphPropertyDeclaration) {
        proto.addPropertyDeclarations(
            ((SimpleGraphPropertyDeclaration) propertyDcl).serialize(fileDescriptorSetsBuilder));
      } else {
        throw new IllegalArgumentException(
            "Cannot serialize non-SimpleGraphPropertyDeclaration " + name);
      }
    }

    return proto.build();
  }

  public static SimplePropertyGraph deserialize(
      SimplePropertyGraphProto proto,
      ImmutableList<? extends DescriptorPool> pools,
      SimpleCatalog catalog) {

    ImmutableSet.Builder<GraphNodeTable> nodeTables = ImmutableSet.builder();
    ImmutableSet.Builder<GraphEdgeTable> edgeTables = ImmutableSet.builder();
    ImmutableSet.Builder<GraphElementLabel> labels = ImmutableSet.builder();
    ImmutableSet.Builder<GraphPropertyDeclaration> propertyDcls = ImmutableSet.builder();

    ImmutableMap.Builder<String, SimpleGraphNodeTable> nodeTableMap = ImmutableMap.builder();
    ImmutableMap.Builder<String, SimpleGraphElementLabel> labelMap = ImmutableMap.builder();
    ImmutableMap.Builder<String, SimpleGraphPropertyDeclaration> propertyDclMap =
        ImmutableMap.builder();

    for (SimpleGraphPropertyDeclarationProto propertyDclProto :
        proto.getPropertyDeclarationsList()) {
      SimpleGraphPropertyDeclaration propertyDcl =
          SimpleGraphPropertyDeclaration.deserialize(
              propertyDclProto, pools, catalog.getTypeFactory());
      propertyDcls.add(propertyDcl);
      propertyDclMap.put(propertyDcl.getName(), propertyDcl);
    }
    for (SimpleGraphElementLabelProto labelProto : proto.getLabelsList()) {
      SimpleGraphElementLabel label =
          SimpleGraphElementLabel.deserialize(labelProto, propertyDclMap.buildOrThrow());
      labels.add(label);
      labelMap.put(label.getName(), label);
    }

    for (SimpleGraphElementTableProto nodeProto : proto.getNodeTablesList()) {
      SimpleGraphNodeTable nodeTable =
          SimpleGraphNodeTable.deserialize(
              nodeProto, catalog, pools, labelMap.buildOrThrow(), propertyDclMap.buildOrThrow());
      nodeTables.add(nodeTable);
      nodeTableMap.put(nodeTable.getName(), nodeTable);
    }
    for (SimpleGraphElementTableProto edgeProto : proto.getEdgeTablesList()) {
      edgeTables.add(
          SimpleGraphEdgeTable.deserialize(
              edgeProto,
              catalog,
              pools,
              nodeTableMap.buildOrThrow(),
              labelMap.buildOrThrow(),
              propertyDclMap.buildOrThrow()));
    }

    return new SimplePropertyGraph(
        proto.getNamePathList(),
        nodeTables.build(),
        edgeTables.build(),
        labels.build(),
        propertyDcls.build());
  }

  @Override
  public String getName() {
    return Iterables.getLast(namePath);
  }

  @Override
  public ImmutableList<String> getNamePath() {
    return namePath;
  }

  @Override
  public String getFullName() {
    return Joiner.on('.').join(namePath);
  }

  @Override
  public GraphElementLabel findLabelByName(String name) {
    return labelMap.get(Ascii.toLowerCase(name));
  }

  @Override
  public GraphPropertyDeclaration findPropertyDeclarationByName(String name) {
    return propertyDclMap.get(Ascii.toLowerCase(name));
  }

  @Override
  public GraphElementTable findElementTableByName(String name) {
    String lowerCaseName = Ascii.toLowerCase(name);
    GraphElementTable elementTable = nodeTableMap.get(lowerCaseName);
    if (elementTable != null) {
      return elementTable;
    }
    return edgeTableMap.get(lowerCaseName);
  }

  @Override
  public ImmutableSet<GraphNodeTable> getNodeTables() {
    return ImmutableSet.copyOf(nodeTableMap.values());
  }

  @Override
  public ImmutableSet<GraphEdgeTable> getEdgeTables() {
    return ImmutableSet.copyOf(edgeTableMap.values());
  }

  @Override
  public ImmutableSet<GraphElementLabel> getLabels() {
    return ImmutableSet.copyOf(labelMap.values());
  }

  @Override
  public ImmutableSet<GraphPropertyDeclaration> getPropertyDeclarations() {
    return ImmutableSet.copyOf(propertyDclMap.values());
  }
}
