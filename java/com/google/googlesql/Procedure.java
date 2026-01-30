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
import com.google.common.base.Preconditions;
import com.google.common.collect.ImmutableList;
import com.google.googlesql.FunctionProtos.ProcedureProto;
import java.io.Serializable;
import java.util.Arrays;
import java.util.List;

/**
 * The Procedure identifies the procedures available in a query engine.
 * Each Procedure includes one FunctionSignatures, where a signature indicates:
 * <ol>
 *   <li> Argument and result types.
 *   <li> A 'context' for the signature.
 * </ol>
 */
public final class Procedure implements Serializable {
  private final ImmutableList<String> namePath;
  private FunctionSignature signature;

  /**
   * Construct a Procedure.
   * @param namePath Name (and namespaces) of the procedure.
   * @param signature
   */
  public Procedure(List<String> namePath, FunctionSignature signature) {
    Preconditions.checkArgument(!namePath.isEmpty());
    this.namePath = ImmutableList.copyOf(namePath);
    this.signature = Preconditions.checkNotNull(signature);
  }

  public Procedure(String name, FunctionSignature signature) {
    this(Arrays.asList(name), signature);
  }

  public String getName() {
    return namePath.get(namePath.size() - 1);
  }

  public ImmutableList<String> getNamePath() {
    return namePath;
  }

  public String getFullName() {
    return Joiner.on('.').join(namePath);
  }

  public ProcedureProto serialize(FileDescriptorSetsBuilder fileDescriptorSetsBuilder) {
    ProcedureProto.Builder builder = ProcedureProto.newBuilder()
        .addAllNamePath(namePath)
        .setSignature(signature.serialize(fileDescriptorSetsBuilder));
    return builder.build();
  }

  static Procedure deserialize(
      ProcedureProto proto, final ImmutableList<? extends DescriptorPool> pools) {
    FunctionSignature signature =
        FunctionSignature.deserialize(proto.getSignature(), pools);
    return new Procedure(proto.getNamePathList(), signature);
  }

  @Override
  public String toString() {
    return getFullName();
  }
}
