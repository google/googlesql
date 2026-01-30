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

import static com.google.common.base.Preconditions.checkNotNull;
import static com.google.common.collect.ImmutableSet.toImmutableSet;

import com.google.common.base.Joiner;
import com.google.common.collect.ImmutableList;
import com.google.common.collect.ImmutableSet;
import com.google.googlesql.SimplePropertyGraphProtos.SimpleGraphElementLabelProto;
import java.util.List;
import java.util.Map;
import java.util.Set;
import java.util.TreeMap;

/** A concrete implementation of the {@link GraphElementLabel} interface. */
public final class SimpleGraphElementLabel implements GraphElementLabel {
  private final String name;
  private final ImmutableList<String> propertyGraphNamePath;
  private final TreeMap<String, GraphPropertyDeclaration> propertyDclMap;

  public SimpleGraphElementLabel(
      String name,
      List<String> propertyGraphNamePath,
      Set<GraphPropertyDeclaration> propertyDeclarations) {
    this.name = name;
    this.propertyGraphNamePath = ImmutableList.copyOf(propertyGraphNamePath);
    propertyDclMap = new TreeMap<>();
    for (GraphPropertyDeclaration propertyDcl : propertyDeclarations) {
      propertyDclMap.put(propertyDcl.getName(), propertyDcl);
    }
  }

  public SimpleGraphElementLabelProto serialize() {
    return SimpleGraphElementLabelProto.newBuilder()
        .setName(name)
        .addAllPropertyGraphNamePath(propertyGraphNamePath)
        .addAllPropertyDeclarationNames(propertyDclMap.navigableKeySet())
        .build();
  }

  public static SimpleGraphElementLabel deserialize(
      SimpleGraphElementLabelProto proto,
      Map<String, SimpleGraphPropertyDeclaration> propertyDclMap) {

    return new SimpleGraphElementLabel(
        proto.getName(),
        proto.getPropertyGraphNamePathList(),
        proto.getPropertyDeclarationNamesList().stream()
            .map(
                name ->
                    checkNotNull(
                        propertyDclMap.get(name), "Property Declaration not found: %s", name))
            .collect(toImmutableSet()));
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
  public ImmutableSet<GraphPropertyDeclaration> getPropertyDeclarations() {
    return ImmutableSet.copyOf(propertyDclMap.values());
  }
}
