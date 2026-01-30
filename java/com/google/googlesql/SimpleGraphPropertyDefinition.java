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

import com.google.common.collect.ImmutableList;
import com.google.googlesql.SimplePropertyGraphProtos.SimpleGraphPropertyDefinitionProto;
import java.util.Map;

/** A concrete implementation of the {@link GraphPropertyDefinition} interface. */
public final class SimpleGraphPropertyDefinition implements GraphPropertyDefinition {
  private final GraphPropertyDeclaration propertyDeclaration;
  private final String exprSql;

  public SimpleGraphPropertyDefinition(
      GraphPropertyDeclaration propertyDeclaration, String exprSql) {
    checkNotNull(exprSql);
    this.propertyDeclaration = propertyDeclaration;
    this.exprSql = exprSql;
  }

  public SimpleGraphPropertyDefinitionProto serialize(
      FileDescriptorSetsBuilder fileDescriptorSetsBuilder) {
    return SimpleGraphPropertyDefinitionProto.newBuilder()
        .setPropertyDeclarationName(propertyDeclaration.getName())
        .setValueExpressionSql(exprSql)
        .build();
  }

  public static SimpleGraphPropertyDefinition deserialize(
      SimpleGraphPropertyDefinitionProto proto,
      SimpleCatalog catalog,
      ImmutableList<? extends DescriptorPool> pools,
      Map<String, SimpleGraphPropertyDeclaration> propertyDclMap) {
    String name = proto.getPropertyDeclarationName();
    String exprSql = proto.getValueExpressionSql();
    return new SimpleGraphPropertyDefinition(
        checkNotNull(propertyDclMap.get(name), "label not found: %s", name), exprSql);
  }

  @Override
  public GraphPropertyDeclaration getDeclaration() {
    return propertyDeclaration;
  }

  @Override
  public String getExpressionSql() {
    return exprSql;
  }
}
