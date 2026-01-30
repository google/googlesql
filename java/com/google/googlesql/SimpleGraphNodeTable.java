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

import static com.google.common.base.Preconditions.checkArgument;
import static com.google.common.base.Preconditions.checkNotNull;
import static com.google.common.collect.ImmutableSet.toImmutableSet;

import com.google.common.base.Splitter;
import com.google.common.collect.ImmutableList;
import com.google.googlesql.SimplePropertyGraphProtos.SimpleGraphElementTableProto;
import java.util.List;
import java.util.Map;
import java.util.Optional;
import java.util.Set;

/** A concrete implementation of the {@link GraphNodeTable} interface. */
public final class SimpleGraphNodeTable extends SimpleGraphElementTable implements GraphNodeTable {

  public SimpleGraphNodeTable(
      String name,
      List<String> propertyGraphNamePath,
      Table table,
      List<Integer> keyColumns,
      Set<GraphElementLabel> labels,
      Set<GraphPropertyDefinition> propertyDefinitions) {
    this(
        name,
        propertyGraphNamePath,
        table,
        keyColumns,
        labels,
        propertyDefinitions,
        /* dynamicLabel= */ null,
        /* dynamicProperties= */ null);
  }

  public SimpleGraphNodeTable(
      String name,
      List<String> propertyGraphNamePath,
      Table table,
      List<Integer> keyColumns,
      Set<GraphElementLabel> labels,
      Set<GraphPropertyDefinition> propertyDefinitions,
      GraphDynamicLabel dynamicLabel,
      GraphDynamicProperties dynamicProperties) {
    super(
        name,
        propertyGraphNamePath,
        table,
        keyColumns,
        labels,
        propertyDefinitions,
        dynamicLabel,
        dynamicProperties);
  }

  @Override
  public SimpleGraphElementTableProto serialize(
      FileDescriptorSetsBuilder fileDescriptorSetsBuilder) {
    return super.serialize(fileDescriptorSetsBuilder).toBuilder()
        .setKind(SimpleGraphElementTableProto.Kind.NODE)
        .build();
  }

  public static SimpleGraphNodeTable deserialize(
      SimpleGraphElementTableProto proto,
      SimpleCatalog catalog,
      ImmutableList<? extends DescriptorPool> pools,
      Map<String, SimpleGraphElementLabel> labelMap,
      Map<String, SimpleGraphPropertyDeclaration> propertyDclMap) {
    try {
      return new SimpleGraphNodeTable(
          proto.getName(),
          proto.getPropertyGraphNamePathList(),
          catalog.findTable(
              ImmutableList.copyOf(Splitter.on('.').split(proto.getInputTableName()))),
          proto.getKeyColumnsList(),
          proto.getLabelNamesList().stream()
              .map(name -> checkNotNull(labelMap.get(name), "label not found: %s", name))
              .collect(toImmutableSet()),
          proto.getPropertyDefinitionsList().stream()
              .map(
                  propertyDefProto ->
                      SimpleGraphPropertyDefinition.deserialize(
                          propertyDefProto, catalog, pools, propertyDclMap))
              .collect(toImmutableSet()),
          proto.hasDynamicLabel()
              ? SimpleGraphDynamicLabel.deserialize(proto.getDynamicLabel())
              : null,
          proto.hasDynamicProperties()
              ? SimpleGraphDynamicProperties.deserialize(proto.getDynamicProperties())
              : null);
    } catch (NotFoundException unused) {
      throw new NullPointerException(
          String.format("Could not find table %s in catalog.", proto.getInputTableName()));
    }
  }

  @Override
  public Kind getKind() {
    return Kind.NODE;
  }
}
