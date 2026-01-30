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

import com.google.common.base.Ascii;
import com.google.common.base.Joiner;
import com.google.common.collect.ImmutableList;
import com.google.common.collect.ImmutableSet;
import com.google.googlesql.SimplePropertyGraphProtos.SimpleGraphElementTableProto;
import java.util.HashMap;
import java.util.List;
import java.util.Map;
import java.util.Optional;
import java.util.Set;
import java.util.TreeMap;

/** A concrete implementation of the {@link GraphElementTable} interface. */
public abstract class SimpleGraphElementTable implements GraphElementTable {
  protected final String name;
  protected final ImmutableList<String> propertyGraphNamePath;
  protected final Table table;
  protected final List<Integer> keyColumns;
  protected final Map<String, GraphElementLabel> labelMap;
  protected final Map<String, GraphPropertyDefinition> propertyDefMap;
  protected final GraphDynamicLabel dynamicLabel;
  protected final GraphDynamicProperties dynamicProperties;

  public SimpleGraphElementTable(
      String name,
      List<String> propertyGraphNamePath,
      Table table,
      List<Integer> keyColumns,
      Set<GraphElementLabel> labels,
      Set<GraphPropertyDefinition> propertyDefinitions,
      GraphDynamicLabel dynamicLabel,
      GraphDynamicProperties dynamicProperties) {
    this.name = name;
    this.propertyGraphNamePath = ImmutableList.copyOf(propertyGraphNamePath);
    this.table = table;
    this.keyColumns = keyColumns;

    labelMap = new HashMap<>();
    propertyDefMap = new HashMap<>();

    labels.forEach(this::addLabel);
    propertyDefinitions.forEach(this::addPropertyDefinition);

    this.dynamicLabel = dynamicLabel;
    this.dynamicProperties = dynamicProperties;
  }

  public void addLabel(GraphElementLabel label) {
    labelMap.putIfAbsent(Ascii.toLowerCase(label.getName()), label);
  }

  public void addPropertyDefinition(GraphPropertyDefinition propertyDefinition) {
    propertyDefMap.putIfAbsent(
        Ascii.toLowerCase(propertyDefinition.getDeclaration().getName()), propertyDefinition);
  }

  @SuppressWarnings("PatternMatchingInstanceof")
  public SimpleGraphElementTableProto serialize(
      FileDescriptorSetsBuilder fileDescriptorSetsBuilder) {
    SimpleGraphElementTableProto.Builder proto =
        SimpleGraphElementTableProto.newBuilder()
            .setName(name)
            .addAllPropertyGraphNamePath(propertyGraphNamePath)
            .setInputTableName(table.getName())
            .addAllKeyColumns(keyColumns)
            .addAllLabelNames(
                labelMap.values().stream()
                    .map(GraphElementLabel::getName)
                    .sorted()
                    .collect(toImmutableList()));

    TreeMap<String, GraphPropertyDefinition> sortedPropertyDefMap = new TreeMap<>(propertyDefMap);
    for (String name : sortedPropertyDefMap.keySet()) {
      GraphPropertyDefinition propertyDef = sortedPropertyDefMap.get(name);
      if (propertyDef instanceof SimpleGraphPropertyDefinition) {
        proto.addPropertyDefinitions(
            ((SimpleGraphPropertyDefinition) propertyDef).serialize(fileDescriptorSetsBuilder));
      } else {
        throw new IllegalArgumentException(
            "Cannot serialize non-SimpleGraphPropertyDefinition " + name);
      }
    }

    if (hasDynamicLabel()) {
      if (dynamicLabel instanceof SimpleGraphDynamicLabel) {
        SimpleGraphDynamicLabel simpleDynamicLabel = (SimpleGraphDynamicLabel) dynamicLabel;
        proto.setDynamicLabel(simpleDynamicLabel.serialize());
      } else {
        throw new IllegalArgumentException("Cannot serialize non-SimpleGraphDynamicLabel ");
      }
    }
    if (hasDynamicProperties()) {
      if (dynamicProperties instanceof SimpleGraphDynamicProperties) {
        SimpleGraphDynamicProperties simpleDynamicProperties =
            (SimpleGraphDynamicProperties) dynamicProperties;
        proto.setDynamicProperties(simpleDynamicProperties.serialize());
      } else {
        throw new IllegalArgumentException("Cannot serialize non-SimpleGraphDynamicProperties ");
      }
    }

    return proto.build();
  }

  @Override
  public String getName() {
    return name;
  }

  @Override
  public String getFullName() {
    return Joiner.on('.').join(propertyGraphNamePath) + "." + name;
  }

  public ImmutableList<String> getPropertyGraphNamePath() {
    return propertyGraphNamePath;
  }

  @Override
  public Table getTable() {
    return table;
  }

  @Override
  public List<Integer> getKeyColumns() {
    return keyColumns;
  }

  @Override
  public GraphPropertyDefinition findPropertyDefinitionByName(String name) {
    return propertyDefMap.get(Ascii.toLowerCase(name));
  }

  @Override
  public GraphElementLabel findLabelByName(String name) {
    return labelMap.get(Ascii.toLowerCase(name));
  }

  @Override
  public Set<GraphPropertyDefinition> getPropertyDefinitions() {
    return ImmutableSet.copyOf(propertyDefMap.values());
  }

  @Override
  public Set<GraphElementLabel> getLabels() {
    return ImmutableSet.copyOf(labelMap.values());
  }

  @Override
  public boolean hasDynamicLabel() {
    return dynamicLabel != null;
  }

  @Override
  public boolean hasDynamicProperties() {
    return dynamicProperties != null;
  }

  @Override
  public GraphDynamicLabel getDynamicLabel() {
    return dynamicLabel;
  }

  @Override
  public GraphDynamicProperties getDynamicProperties() {
    return dynamicProperties;
  }

  @Override
  public DynamicLabelCardinality dynamicLabelCardinality() {
    if (!hasDynamicLabel()) {
      return DynamicLabelCardinality.UNKNOWN;
    }
    Type type = table.findColumnByName(getDynamicLabel().getLabelExpression()).getType();
    if (type.isString()) {
      return DynamicLabelCardinality.SINGLE;
    }
    if (type.isArray() && type.asArray().getElementType().isString()) {
      return DynamicLabelCardinality.MULTIPLE;
    }
    return DynamicLabelCardinality.UNKNOWN;
  }
}
