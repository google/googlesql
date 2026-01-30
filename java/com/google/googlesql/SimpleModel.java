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
import com.google.common.base.Preconditions;
import com.google.common.collect.ImmutableList;
import com.google.googlesql.SimpleModelProtos.SimpleModelProto;
import com.google.googlesql.SimpleTableProtos.SimpleColumnProto;
import java.util.HashMap;
import java.util.List;
import java.util.Map;
import java.util.Objects;
import java.util.concurrent.atomic.AtomicLong;

/** SimpleModel is a concrete implementation of the Model interface. */
public final class SimpleModel implements Model {

  private final String name;
  private final long id;
  private final Map<String, SimpleColumn> inputs = new HashMap<>();
  private final Map<String, SimpleColumn> outputs = new HashMap<>();

  private static final AtomicLong nextModelId = new AtomicLong(0);

  /** Ensures the next Model id is greater that the provided id */
  private static void updateNextIdIfNotGreaterThan(long id) {
    nextModelId.getAndAccumulate(
        id, (previousValue, minimumValue) -> previousValue <= id ? id + 1 : previousValue);
  }

  /**
   * A pair of a name and a type.
   *
   * <p>Used to represent inputs and outputs when constructing SimpleModels
   */
  public static class NameAndType {
    private final String name;
    private final Type type;

    public NameAndType(String name, Type type) {
      this.name = name;
      this.type = type;
    }

    public String getName() {
      return this.name;
    }

    public Type getType() {
      return this.type;
    }

    private SimpleColumn toSimpleColumn(String modelName) {
      return new SimpleColumn(modelName, name, type, false, false, false);
    }

    private static NameAndType deserialize(
        SimpleColumnProto proto,
        ImmutableList<? extends DescriptorPool> pools,
        TypeFactory typeFactory) {
      Type type = typeFactory.deserialize(proto.getType(), pools);
      return new NameAndType(proto.getName(), type);
    }
  }

  SimpleModel(String name, long id) {
    this.name = name;
    this.id = id;
    updateNextIdIfNotGreaterThan(id);
  }

  /** Build a SimpleModel with the specified name, initually without any inputs or outputs. */
  public SimpleModel(String name) {
    this(name, nextModelId.getAndIncrement());
  }

  SimpleModel(String name, long id, List<NameAndType> inputs, List<NameAndType> outputs) {
    this(name, id);
    inputs.forEach(this::addInput);
    outputs.forEach(this::addOutput);
  }

  /**
   * Builds a model with the specified name, inputs and outputs.
   *
   * <p>Fails if there are duplicate column names in either the inputs or the outputs.
   */
  public SimpleModel(String name, List<NameAndType> inputs, List<NameAndType> outputs) {
    this(name, nextModelId.getAndIncrement(), inputs, outputs);
  }

  @Override
  public String getName() {
    return this.name;
  }

  @Override
  public String getFullName() {
    return this.name;
  }

  @Override
  public long getId() {
    return this.id;
  }

  @Override
  public ImmutableList<Column> getInputs() {
    return ImmutableList.copyOf(this.inputs.values());
  }

  @Override
  public ImmutableList<Column> getOutputs() {
    return ImmutableList.copyOf(this.outputs.values());
  }

  /**
   * Adds an input to this model.
   *
   * <p>Fails if there's an existing input with the same name.
   */
  public void addInput(NameAndType input) {
    String inputNameLowercase = Ascii.toLowerCase(input.getName());
    Preconditions.checkArgument(
        !this.inputs.containsKey(inputNameLowercase),
        "Duplicate input name %s for model %s",
        input.getName(),
        this.name);
    this.inputs.put(inputNameLowercase, input.toSimpleColumn(this.name));
  }

  /**
   * Adds an output to this model.
   *
   * <p>Fails if there's an existing output with the same name.
   */
  public void addOutput(NameAndType output) {
    String outputNameLowercase = Ascii.toLowerCase(output.getName());
    Preconditions.checkArgument(
        !this.outputs.containsKey(outputNameLowercase),
        "Duplicate output name %s for model %s",
        output.getName(),
        this.name);
    this.outputs.put(outputNameLowercase, output.toSimpleColumn(this.name));
  }

  @Override
  public boolean equals(Object other) {
    if (!(other instanceof SimpleModel)) {
      return false;
    }

    SimpleModel otherAsModel = (SimpleModel) other;

    return Objects.equals(this.name, otherAsModel.name)
        && Objects.equals(this.getInputs(), otherAsModel.getInputs())
        && Objects.equals(this.getOutputs(), otherAsModel.getOutputs())
        && this.id == otherAsModel.id;
  }

  @Override
  public int hashCode() {
    return Objects.hash(name, inputs, outputs, id);
  }

  public SimpleModelProto serialize(FileDescriptorSetsBuilder fileDescriptorSetsBuilder) {
    SimpleModelProto.Builder builder = SimpleModelProto.newBuilder();

    builder.setName(this.name);
    builder.setId(this.id);

    for (SimpleColumn input : this.inputs.values()) {
      SimpleColumnProto.Builder inputBuilder = SimpleColumnProto.newBuilder();
      inputBuilder.setName(input.getName());
      input.getType().serialize(inputBuilder.getTypeBuilder(), fileDescriptorSetsBuilder);
      builder.addInput(inputBuilder.build());
    }

    for (SimpleColumn output : this.outputs.values()) {
      SimpleColumnProto.Builder outputBuilder = SimpleColumnProto.newBuilder();
      outputBuilder.setName(output.getName());
      output.getType().serialize(outputBuilder.getTypeBuilder(), fileDescriptorSetsBuilder);
      builder.addOutput(outputBuilder.build());
    }

    return builder.build();
  }

  public static SimpleModel deserialize(
      SimpleModelProto proto,
      ImmutableList<? extends DescriptorPool> pools,
      TypeFactory typeFactory) {

    String name = proto.getName();
    long id = proto.getId();
    ImmutableList<NameAndType> inputs =
        proto.getInputList().stream()
            .map(input -> NameAndType.deserialize(input, pools, typeFactory))
            .collect(ImmutableList.toImmutableList());
    ImmutableList<NameAndType> outputs =
        proto.getOutputList().stream()
            .map(output -> NameAndType.deserialize(output, pools, typeFactory))
            .collect(ImmutableList.toImmutableList());

    return new SimpleModel(name, id, inputs, outputs);
  }
}
