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

import com.google.common.base.Joiner;
import com.google.common.collect.ImmutableList;
import com.google.googlesql.SimplePropertyGraphProtos.SimpleGraphPropertyDeclarationProto;
import java.util.List;

/** A concrete implementation of the {@link GraphPropertyDeclaration} interface. */
public final class SimpleGraphPropertyDeclaration implements GraphPropertyDeclaration {
  private final String name;
  private final ImmutableList<String> propertyGraphNamePath;
  private final Type type;

  public SimpleGraphPropertyDeclaration(
      String name, List<String> propertyGraphNamePath, Type type) {
    this.name = name;
    this.propertyGraphNamePath = ImmutableList.copyOf(propertyGraphNamePath);
    this.type = type;
  }

  public SimpleGraphPropertyDeclarationProto serialize(
      FileDescriptorSetsBuilder fileDescriptorSetsBuilder) {
    return SimpleGraphPropertyDeclarationProto.newBuilder()
        .setName(name)
        .addAllPropertyGraphNamePath(propertyGraphNamePath)
        .setType(type.serialize(fileDescriptorSetsBuilder))
        .build();
  }

  public static SimpleGraphPropertyDeclaration deserialize(
      SimpleGraphPropertyDeclarationProto proto,
      ImmutableList<? extends DescriptorPool> pools,
      TypeFactory factory) {
    return new SimpleGraphPropertyDeclaration(
        proto.getName(),
        proto.getPropertyGraphNamePathList(),
        factory.deserialize(proto.getType(), pools));
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
  public Type getType() {
    return type;
  }
}
