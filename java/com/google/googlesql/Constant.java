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
import com.google.googlesql.SimpleConstantProtos.SimpleConstantProto;
import java.io.Serializable;
import java.util.List;

/** Represents a symbol which holds a constant value. */
public final class Constant implements Serializable {
  Constant(List<String> namePath, Type type, Value value) {
    this.namePath = ImmutableList.copyOf(namePath);
    this.type = type;
    this.value = value;
  }

  /** Returns the name of this Constant. */
  public ImmutableList<String> getNamePath() {
    return namePath;
  }

  /** Returns the fully-qualified name of this Constant. */
  public String getFullName() {
    return String.join(".", namePath);
  }

  /** Returns the type of this Constant. */
  public Type getType() {
    return type;
  }

  /** Returns the value of this Constant. */
  public Value getValue() {
    return value;
  }

  /**
   * Serialize this Constant into protobuf, with FileDescriptors emitted to the builder as needed.
   */
  public SimpleConstantProto serialize(FileDescriptorSetsBuilder fileDescriptorSetsBuilder) {
    SimpleConstantProto.Builder builder = SimpleConstantProto.newBuilder().addAllNamePath(namePath);
    type.serialize(builder.getTypeBuilder(), fileDescriptorSetsBuilder);
    builder.setValue(value.serialize());
    return builder.build();
  }

  /**
   * Deserialize a Constant object from a protobuf, using typeFactory and pools to construct type
   * objects as needed.
   */
  public static Constant deserialize(
      SimpleConstantProto proto,
      final ImmutableList<? extends DescriptorPool> pools,
      TypeFactory typeFactory) {
    Type type = typeFactory.deserialize(proto.getType(), pools);
    return new Constant(proto.getNamePathList(), type, Value.deserialize(type, proto.getValue()));
  }

  private final ImmutableList<String> namePath;
  private final Type type;
  private final Value value;
}
