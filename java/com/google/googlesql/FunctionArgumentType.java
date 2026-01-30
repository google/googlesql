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

import com.google.auto.value.AutoValue;
import com.google.common.base.Preconditions;
import com.google.common.collect.ImmutableList;
import com.google.errorprone.annotations.CanIgnoreReturnValue;
import com.google.googlesql.FunctionProtos.ArgumentTypeLambdaProto;
import com.google.googlesql.FunctionProtos.FunctionArgumentTypeOptionsProto;
import com.google.googlesql.FunctionProtos.FunctionArgumentTypeProto;
import com.google.googlesql.GoogleSQLFunctions.FunctionEnums;
import com.google.googlesql.GoogleSQLFunctions.FunctionEnums.ArgumentCardinality;
import com.google.googlesql.GoogleSQLFunctions.FunctionEnums.ProcedureArgumentMode;
import com.google.googlesql.GoogleSQLFunctions.SignatureArgumentKind;
import java.io.Serializable;
import java.util.ArrayList;
import java.util.List;
import javax.annotation.Nullable;

/**
 * A type for an argument or result value in a function signature. Types can be fixed or templated.
 * Arguments can be marked as repeated (denoting it can occur zero or more times in a function
 * invocation) or optional. Result types cannot be marked as repeated or optional. A
 * FunctionArgumentType is concrete if it is not templated and numOccurrences indicates how many
 * times the argument appears in a concrete FunctionSignature. FunctionArgumentTypeOptions can be
 * used to apply additional constraints on legal values for the argument.
 */
public final class FunctionArgumentType implements Serializable {

  private static class LambdaArgument implements Serializable {
    List<FunctionArgumentType> argumentTypes;
    FunctionArgumentType bodyType;
  }

  private final SignatureArgumentKind kind;
  private final Type type;
  private final int numOccurrences;
  private final FunctionArgumentTypeOptions options;
  private final LambdaArgument lambda;

  public FunctionArgumentType(
      SignatureArgumentKind kind, FunctionArgumentTypeOptions options, int numOccurrences) {
    Preconditions.checkArgument(kind != SignatureArgumentKind.ARG_TYPE_FIXED);
    this.kind = kind;
    this.type = null;
    this.numOccurrences = numOccurrences;
    this.options = options;
    this.lambda = null;
    validate();
  }

  public FunctionArgumentType(Type type, FunctionArgumentTypeOptions options, int numOccurrences) {
    this.kind = SignatureArgumentKind.ARG_TYPE_FIXED;
    this.type = type;
    this.numOccurrences = numOccurrences;
    this.options = options;
    this.lambda = null;
    validate();
  }

  public FunctionArgumentType(
      SignatureArgumentKind kind, ArgumentCardinality cardinality, int numOccurrences) {
    Preconditions.checkArgument(kind != SignatureArgumentKind.ARG_TYPE_FIXED);
    this.kind = kind;
    this.type = null;
    this.numOccurrences = numOccurrences;
    this.options = FunctionArgumentTypeOptions.builder().setCardinality(cardinality).build();
    this.lambda = null;
    validate();
  }

  public FunctionArgumentType(Type type, ArgumentCardinality cardinality, int numOccurrences) {
    this.kind = SignatureArgumentKind.ARG_TYPE_FIXED;
    this.type = type;
    this.numOccurrences = numOccurrences;
    this.options = FunctionArgumentTypeOptions.builder().setCardinality(cardinality).build();
    this.lambda = null;
    validate();
  }

  public FunctionArgumentType(
      List<FunctionArgumentType> lambdaArgumentTypes,
      FunctionArgumentType lambdaBodyType,
      FunctionArgumentTypeOptions options) {
    Preconditions.checkNotNull(lambdaArgumentTypes);
    Preconditions.checkNotNull(lambdaBodyType);
    this.kind = SignatureArgumentKind.ARG_TYPE_LAMBDA;
    this.type = null;
    this.numOccurrences = -1;
    this.options = options;
    LambdaArgument lambda = new LambdaArgument();
    lambda.argumentTypes = lambdaArgumentTypes;
    lambda.bodyType = lambdaBodyType;
    this.lambda = lambda;
    validate();
  }

  public FunctionArgumentType(
      List<FunctionArgumentType> lambdaArgumentTypes, FunctionArgumentType lambdaBodyType) {
    this(lambdaArgumentTypes, lambdaBodyType, FunctionArgumentTypeOptions.builder().build());
  }

  public FunctionArgumentType(Type type, ArgumentCardinality cardinality) {
    this(type, cardinality, -1);
  }

  public FunctionArgumentType(SignatureArgumentKind kind) {
    this(kind, ArgumentCardinality.REQUIRED, -1);
  }

  public FunctionArgumentType(Type type) {
    this(type, ArgumentCardinality.REQUIRED, -1);
  }

  public boolean isConcrete() {
    if (kind != SignatureArgumentKind.ARG_TYPE_FIXED
        && kind != SignatureArgumentKind.ARG_TYPE_RELATION
        && kind != SignatureArgumentKind.ARG_TYPE_MODEL
        && kind != SignatureArgumentKind.ARG_TYPE_CONNECTION
        && kind != SignatureArgumentKind.ARG_TYPE_LAMBDA
        && kind != SignatureArgumentKind.ARG_TYPE_GRAPH) {
      return false;
    }
    if (numOccurrences < 0) {
      return false;
    }

    // Lambda is concrete if all args and body are concrete.
    if (kind == SignatureArgumentKind.ARG_TYPE_LAMBDA) {
      for (FunctionArgumentType arg : lambda.argumentTypes) {
        if (!arg.isConcrete()) {
          return false;
        }
      }
      return lambda.bodyType.isConcrete();
    }

    return true;
  }

  public int getNumOccurrences() {
    return numOccurrences;
  }

  public ArgumentCardinality getCardinality() {
    return options.getCardinality();
  }

  public boolean isRepeated() {
    return getCardinality() == ArgumentCardinality.REPEATED;
  }

  public boolean isRequired() {
    return getCardinality() == ArgumentCardinality.REQUIRED;
  }

  public boolean isOptional() {
    return getCardinality() == ArgumentCardinality.OPTIONAL;
  }

  /** Returns Type of the argument when it's fixed, or null if it's templated. */
  @Nullable
  public Type getType() {
    return type;
  }

  public SignatureArgumentKind getKind() {
    return kind;
  }

  public String debugString(boolean verbose) {
    StringBuilder builder = new StringBuilder();
    builder.append(isRepeated() ? "repeated" : isOptional() ? "optional" : "");
    if (isConcrete() && !isRequired()) {
      builder.append("(").append(numOccurrences).append(")");
    }
    if (!isRequired()) {
      builder.append(" ");
    }

    if (type != null) {
      builder.append(type.debugString());
    } else if (kind == SignatureArgumentKind.ARG_TYPE_RELATION
        && options.getRelationInputSchema() != null) {
      builder.append(options.getRelationInputSchema());
    } else if (kind == SignatureArgumentKind.ARG_TYPE_ARBITRARY) {
      builder.append("ANY TYPE");
    } else if (kind == SignatureArgumentKind.ARG_TYPE_LAMBDA) {
      Preconditions.checkNotNull(lambda);
      builder.append("FUNCTION<");
      List<String> args = new ArrayList<>();
      for (FunctionArgumentType argType : lambda.argumentTypes) {
        args.add(argType.debugString(verbose));
      }
      String argStr = String.join(", ", args);
      if (lambda.argumentTypes.size() == 1) {
        builder.append(argStr);
      } else {
        builder.append("(").append(argStr).append(")");
      }
      builder.append("->").append(lambda.bodyType.debugString(verbose)).append(">");
    } else {
      builder.append(signatureArgumentKindToString(kind));
    }

    if (verbose) {
      builder.append(options.toDebugString());
    }

    if (options.getArgumentName() != null) {
      builder.append(" ").append(options.getArgumentName());
    }

    return builder.toString();
  }

  public String debugString() {
    return debugString(false);
  }

  public FunctionArgumentTypeOptions getOptions() {
    return options;
  }

  public TVFRelation getRelation() {
    Preconditions.checkArgument(kind == SignatureArgumentKind.ARG_TYPE_RELATION);
    return options.getRelationInputSchema();
  }

  @Override
  public String toString() {
    return debugString(false);
  }

  private static String signatureArgumentKindToString(SignatureArgumentKind kind) {
    switch (kind) {
      case ARG_TYPE_FIXED:
        return "FIXED";
      case ARG_TYPE_ANY_1:
        return "<T1>";
      case ARG_TYPE_ANY_2:
        return "<T2>";
      case ARG_TYPE_ANY_3:
        return "<T3>";
      case ARG_TYPE_ANY_4:
        return "<T4>";
      case ARG_TYPE_ANY_5:
        return "<T5>";
      case ARG_ARRAY_TYPE_ANY_1:
        return "<array<T1>>";
      case ARG_ARRAY_TYPE_ANY_2:
        return "<array<T2>>";
      case ARG_ARRAY_TYPE_ANY_3:
        return "<array<T3>>";
      case ARG_ARRAY_TYPE_ANY_4:
        return "<array<T4>>";
      case ARG_ARRAY_TYPE_ANY_5:
        return "<array<T5>>";
      case ARG_ENUM_ANY:
        return "<enum>";
      case ARG_PROTO_MAP_ANY:
        return "<map<K, V>>";
      case ARG_PROTO_MAP_KEY_ANY:
        return "<K>";
      case ARG_PROTO_MAP_VALUE_ANY:
        return "<V>";
      case ARG_PROTO_ANY:
        return "<proto>";
      case ARG_STRUCT_ANY:
        return "<struct>";
      case ARG_TYPE_RELATION:
        return "ANY TABLE";
      case ARG_TYPE_CONNECTION:
        return "ANY CONNECTION";
      case ARG_TYPE_DESCRIPTOR:
        return "ANY DESCRIPTOR";
      case ARG_TYPE_MODEL:
        return "ANY MODEL";
      case ARG_TYPE_ARBITRARY:
        return "<arbitrary>";
      case ARG_TYPE_VOID:
        return "<void>";
      case ARG_TYPE_LAMBDA:
        return "<function<T->T>>";
      case ARG_TYPE_GRAPH_NODE:
        return "<graph_node>";
      case ARG_TYPE_GRAPH_EDGE:
        return "<graph_edge>";
      case ARG_TYPE_GRAPH_ELEMENT:
        return "<graph_element>";
      case ARG_TYPE_GRAPH:
        return "ANY GRAPH";
      case ARG_TYPE_GRAPH_PATH:
        return "<graph_path>";
      case ARG_TYPE_SEQUENCE:
        return "ANY SEQUENCE";
      case __SignatureArgumentKind__switch_must_have_a_default__:
      default:
        return "UNKNOWN_ARG_KIND";
    }
  }

  public FunctionArgumentTypeProto serialize(FileDescriptorSetsBuilder fileDescriptorSetsBuilder) {
    FunctionArgumentTypeProto.Builder builder =
        FunctionArgumentTypeProto.newBuilder().setKind(kind);
    if (numOccurrences != 0) {
      builder.setNumOccurrences(numOccurrences);
    }
    FunctionArgumentTypeOptionsProto optionsProto =
        options.serialize(type, fileDescriptorSetsBuilder);
    if (!optionsProto.equals(FunctionArgumentTypeOptionsProto.getDefaultInstance())) {
      builder.setOptions(optionsProto);
    }
    if (type != null) {
      type.serialize(builder.getTypeBuilder(), fileDescriptorSetsBuilder);
    }

    if (kind == SignatureArgumentKind.ARG_TYPE_LAMBDA) {
      Preconditions.checkArgument(lambda != null);
      ArgumentTypeLambdaProto.Builder lambdaBuilder = ArgumentTypeLambdaProto.newBuilder();
      for (FunctionArgumentType arg : lambda.argumentTypes) {
        lambdaBuilder.addArgument(arg.serialize(fileDescriptorSetsBuilder));
      }
      lambdaBuilder.setBody(lambda.bodyType.serialize(fileDescriptorSetsBuilder));
      builder.setLambda(lambdaBuilder.build());
    }
    return builder.build();
  }

  public static FunctionArgumentType deserialize(
      FunctionArgumentTypeProto proto, ImmutableList<? extends DescriptorPool> pools) {
    SignatureArgumentKind kind = proto.getKind();
    TypeFactory factory = TypeFactory.nonUniqueNames();

    if (kind == SignatureArgumentKind.ARG_TYPE_FIXED) {
      Type argType = factory.deserialize(proto.getType(), pools);
      return new FunctionArgumentType(
          argType,
          FunctionArgumentTypeOptions.deserialize(proto.getOptions(), pools, argType, factory),
          proto.getNumOccurrences());
    } else if (kind == SignatureArgumentKind.ARG_TYPE_LAMBDA) {
      List<FunctionArgumentType> argumentTypes = new ArrayList<>();
      for (FunctionArgumentTypeProto argType : proto.getLambda().getArgumentList()) {
        argumentTypes.add(deserialize(argType, pools));
      }
      FunctionArgumentType bodyType = deserialize(proto.getLambda().getBody(), pools);
      return new FunctionArgumentType(
          argumentTypes,
          bodyType,
          FunctionArgumentTypeOptions.deserialize(
              proto.getOptions(), pools, /* argType= */ null, factory));

    } else {
      return new FunctionArgumentType(
          kind,
          FunctionArgumentTypeOptions.deserialize(
              proto.getOptions(), pools, /* argType= */ null, factory),
          proto.getNumOccurrences());
    }
  }

  private void validate() {
    if (getOptions().getDefault() != null) {
      Preconditions.checkArgument(
          kind != SignatureArgumentKind.ARG_TYPE_RELATION
              && kind != SignatureArgumentKind.ARG_TYPE_VOID
              && kind != SignatureArgumentKind.ARG_TYPE_MODEL
              && kind != SignatureArgumentKind.ARG_TYPE_DESCRIPTOR
              && kind != SignatureArgumentKind.ARG_TYPE_CONNECTION
              && kind != SignatureArgumentKind.ARG_TYPE_GRAPH
              && kind != SignatureArgumentKind.ARG_TYPE_LAMBDA,
          "%s argument cannot have a default value",
          signatureArgumentKindToString(kind));
      if (type != null) {
        Preconditions.checkArgument(
            type.equals(getOptions().getDefault().getType()),
            "Default value type does not match the argument type: %s vs %s",
            type,
            getOptions().getDefault().getType());
      }
    }
  }

  /** Optional parameters associated with a function argument type. */
  @AutoValue
  public abstract static class FunctionArgumentTypeOptions implements Serializable {
    @Nullable
    public abstract ArgumentCardinality getCardinality();

    @Nullable
    abstract ConstnessLevelProto.Level getConstnessLevel();

    public boolean getMustBeConstant() {
      return getConstnessLevel() == ConstnessLevelProto.Level.LEGACY_LITERAL_OR_PARAMETER;
    }

    public boolean getMustBeConstantExpression() {
      return getConstnessLevel() == ConstnessLevelProto.Level.LEGACY_CONSTANT_EXPRESSION;
    }

    public boolean getMustBeAnalysisConstant() {
      return getConstnessLevel() == ConstnessLevelProto.Level.ANALYSIS_CONST;
    }

    public boolean getMustBeImmutableConstant() {
      return getConstnessLevel() == ConstnessLevelProto.Level.IMMUTABLE_CONST;
    }

    @Nullable
    public abstract Boolean getMustBeNonNull();

    @Nullable
    public abstract Boolean getIsNotAggregate();

    @Nullable
    public abstract Boolean getMustSupportEquality();

    @Nullable
    public abstract Boolean getMustSupportOrdering();

    @Nullable
    public abstract Boolean getMustSupportGrouping();

    @Nullable
    public abstract Boolean getArrayElementMustSupportEquality();

    @Nullable
    public abstract Boolean getArrayElementMustSupportOrdering();

    @Nullable
    public abstract Boolean getArrayElementMustSupportGrouping();

    @Nullable
    public abstract Long getMinValue();

    @Nullable
    public abstract Long getMaxValue();

    @Nullable
    public abstract Boolean getExtraRelationInputColumnsAllowed();

    @Nullable
    public abstract TVFRelation getRelationInputSchema();

    @Nullable
    public abstract String getArgumentName();

    @Nullable
    public abstract ParseLocationRange getArgumentNameParseLocation();

    @Nullable
    public abstract ParseLocationRange getArgumentTypeParseLocation();

    @Nullable
    public abstract FunctionEnums.ProcedureArgumentMode getProcedureArgumentMode();

    @Nullable
    public abstract FunctionEnums.NamedArgumentKind getNamedArgumentKind();

    @Nullable
    public abstract Integer getDescriptorResolutionTableOffset();

    @Nullable
    public abstract Value getDefault();

    @Nullable
    public abstract FunctionEnums.ArgumentCollationMode getArgumentCollationMode();

    @Nullable
    public abstract FunctionEnums.ArgumentAliasKind getArgumentAliasKind();

    @SuppressWarnings("deprecation") // Sets deprecated field for serialization compat.
    public FunctionArgumentTypeOptionsProto serialize(
        @Nullable Type argType, FileDescriptorSetsBuilder fileDescriptorSetsBuilder) {
      FunctionArgumentTypeOptionsProto.Builder builder =
          FunctionArgumentTypeOptionsProto.newBuilder();
      if (getCardinality() != null) {
        builder.setCardinality(getCardinality());
      }
      if (getConstnessLevel() != null) {
        builder.setConstnessLevel(getConstnessLevel());
      } else {
        // TODO: b/434986689 These fields are deprecated. They should be removed and only
        // ConstnessLevel should be used.
        if (getMustBeConstant()) {
          builder.setMustBeConstant(true);
        }
        if (getMustBeConstantExpression()) {
          builder.setMustBeConstantExpression(true);
        }
      }
      if (getMustBeNonNull() != null) {
        builder.setMustBeNonNull(getMustBeNonNull());
      }
      if (getIsNotAggregate() != null) {
        builder.setIsNotAggregate(getIsNotAggregate());
      }
      if (getMustSupportEquality() != null) {
        builder.setMustSupportEquality(getMustSupportEquality());
      }
      if (getMustSupportOrdering() != null) {
        builder.setMustSupportOrdering(getMustSupportOrdering());
      }
      if (getMustSupportGrouping() != null) {
        builder.setMustSupportGrouping(getMustSupportGrouping());
      }
      if (getArrayElementMustSupportEquality() != null) {
        builder.setArrayElementMustSupportEquality(getArrayElementMustSupportEquality());
      }
      if (getArrayElementMustSupportOrdering() != null) {
        builder.setArrayElementMustSupportOrdering(getArrayElementMustSupportOrdering());
      }
      if (getArrayElementMustSupportGrouping() != null) {
        builder.setArrayElementMustSupportGrouping(getArrayElementMustSupportGrouping());
      }
      if (getMinValue() != null) {
        builder.setMinValue(getMinValue());
      }
      if (getMaxValue() != null) {
        builder.setMaxValue(getMaxValue());
      }
      if (getExtraRelationInputColumnsAllowed() != null) {
        builder.setExtraRelationInputColumnsAllowed(getExtraRelationInputColumnsAllowed());
      }
      if (getRelationInputSchema() != null) {
        builder.setRelationInputSchema(
            getRelationInputSchema().serialize(fileDescriptorSetsBuilder));
      }
      if (getArgumentName() != null) {
        builder.setArgumentName(getArgumentName());
        builder.setNamedArgumentKind(getNamedArgumentKind());
        if (getNamedArgumentKind() != null) {
          builder.setArgumentNameIsMandatory(
              getNamedArgumentKind() == FunctionEnums.NamedArgumentKind.NAMED_ONLY);
        }
      }
      if (getArgumentNameParseLocation() != null) {
        builder.setArgumentNameParseLocation(getArgumentNameParseLocation().serialize());
      }
      if (getArgumentTypeParseLocation() != null) {
        builder.setArgumentTypeParseLocation(getArgumentTypeParseLocation().serialize());
      }
      if (getProcedureArgumentMode() != null) {
        builder.setProcedureArgumentMode(getProcedureArgumentMode());
      }
      if (getDescriptorResolutionTableOffset() != null) {
        builder.setDescriptorResolutionTableOffset(getDescriptorResolutionTableOffset());
      }
      if (getDefault() != null) {
        if (argType == null) {
          getDefault()
              .getType()
              .serialize(builder.getDefaultValueTypeBuilder(), fileDescriptorSetsBuilder);
        }
        builder.setDefaultValue(getDefault().getProto());
      }
      if (getArgumentCollationMode() != null) {
        builder.setArgumentCollationMode(getArgumentCollationMode());
      }
      if (getArgumentAliasKind() != null
          && getArgumentAliasKind() != FunctionEnums.ArgumentAliasKind.ARGUMENT_NON_ALIASED) {
        builder.setArgumentAliasKind(getArgumentAliasKind());
      }
      return builder.build();
    }

    @SuppressWarnings("deprecation") // Reads deprecated field for serialization compat.
    public static FunctionArgumentTypeOptions deserialize(
        FunctionArgumentTypeOptionsProto proto,
        ImmutableList<? extends DescriptorPool> pools,
        @Nullable Type argType,
        TypeFactory typeFactory) {
      Builder builder = builder();
      if (proto.hasCardinality()) {
        builder.setCardinality(proto.getCardinality());
      }
      if (proto.hasConstnessLevel()) {
        builder.setConstnessLevel(proto.getConstnessLevel());
      } else {
        if (proto.hasMustBeConstant()) {
          builder.setMustBeConstant(proto.getMustBeConstant());
        }
        if (proto.hasMustBeConstantExpression()) {
          builder.setMustBeConstantExpression(proto.getMustBeConstantExpression());
        }
      }
      if (proto.hasMustBeNonNull()) {
        builder.setMustBeNonNull(proto.getMustBeNonNull());
      }
      if (proto.hasIsNotAggregate()) {
        builder.setIsNotAggregate(proto.getIsNotAggregate());
      }
      if (proto.hasMustSupportEquality()) {
        builder.setMustSupportEquality(proto.getMustSupportEquality());
      }
      if (proto.hasMustSupportOrdering()) {
        builder.setMustSupportOrdering(proto.getMustSupportOrdering());
      }
      if (proto.hasMustSupportGrouping()) {
        builder.setMustSupportGrouping(proto.getMustSupportGrouping());
      }
      if (proto.hasArrayElementMustSupportEquality()) {
        builder.setArrayElementMustSupportEquality(proto.getArrayElementMustSupportEquality());
      }
      if (proto.hasArrayElementMustSupportOrdering()) {
        builder.setArrayElementMustSupportOrdering(proto.getArrayElementMustSupportOrdering());
      }
      if (proto.hasArrayElementMustSupportGrouping()) {
        builder.setArrayElementMustSupportGrouping(proto.getArrayElementMustSupportGrouping());
      }
      if (proto.hasMinValue()) {
        builder.setMinValue(proto.getMinValue());
      }
      if (proto.hasMaxValue()) {
        builder.setMaxValue(proto.getMaxValue());
      }
      if (proto.hasExtraRelationInputColumnsAllowed()) {
        builder.setExtraRelationInputColumnsAllowed(proto.getExtraRelationInputColumnsAllowed());
      }
      if (proto.hasRelationInputSchema()) {
        builder.setRelationInputSchema(
            TVFRelation.deserialize(proto.getRelationInputSchema(), pools, typeFactory));
      }
      if (proto.hasArgumentName()) {
        builder.setArgumentName(proto.getArgumentName());
      }
      if (proto.hasArgumentNameParseLocation()) {
        builder.setArgumentNameParseLocation(
            ParseLocationRange.deserialize(proto.getArgumentNameParseLocation()));
      }
      if (proto.hasArgumentTypeParseLocation()) {
        builder.setArgumentTypeParseLocation(
            ParseLocationRange.deserialize(proto.getArgumentTypeParseLocation()));
      }
      if (proto.hasProcedureArgumentMode()) {
        builder.setProcedureArgumentMode(proto.getProcedureArgumentMode());
      }
      if (proto.hasArgumentName()) {
        if (proto.hasNamedArgumentKind()
            && proto.getNamedArgumentKind()
                != FunctionEnums.NamedArgumentKind.NAMED_ARGUMENT_KIND_UNSPECIFIED) {
          builder.setArgumentName(proto.getArgumentName(), proto.getNamedArgumentKind());
        } else if (proto.hasArgumentNameIsMandatory() && proto.getArgumentNameIsMandatory()) {
          builder.setArgumentName(
              proto.getArgumentName(), FunctionEnums.NamedArgumentKind.NAMED_ONLY);
        } else {
          builder.setArgumentName(
              proto.getArgumentName(), FunctionEnums.NamedArgumentKind.POSITIONAL_OR_NAMED);
        }
      }
      if (proto.hasDescriptorResolutionTableOffset()) {
        builder.setDescriptorResolutionTableOffset(proto.getDescriptorResolutionTableOffset());
      }
      if (proto.hasDefaultValue()) {
        Type type = argType;
        if (type == null) {
          Preconditions.checkArgument(proto.hasDefaultValueType());
          type = typeFactory.deserialize(proto.getDefaultValueType(), pools);
        }
        builder.setDefault(Value.deserialize(type, proto.getDefaultValue()));
      }
      if (proto.hasArgumentCollationMode()) {
        builder.setArgumentCollationMode(proto.getArgumentCollationMode());
      }
      if (proto.hasArgumentAliasKind()) {
        builder.setArgumentAliasKind(proto.getArgumentAliasKind());
      }
      return builder.build();
    }

    public static Builder builder() {
      return new AutoValue_FunctionArgumentType_FunctionArgumentTypeOptions.Builder();
    }

    public String toDebugString() {
      List<String> options = new ArrayList<>();
      if (getMustBeConstant()) {
        options.add("must_be_constant: true");
      }
      if (getMustBeAnalysisConstant()) {
        options.add("must_be_analysis_constant: true");
      }
      if (getMustBeImmutableConstant()) {
        options.add("must_be_immutable_constant: true");
      }
      if (getMustBeConstantExpression()) {
        options.add("must_be_constant_expression: true");
      }
      if (getMustBeNonNull() != null) {
        options.add("must_be_non_null: true");
      }
      if (getIsNotAggregate() != null) {
        options.add("is_not_aggregate: true");
      }
      if (getProcedureArgumentMode() != null
          && getProcedureArgumentMode() != ProcedureArgumentMode.NOT_SET) {
        options.add("procedure_argument_mode: " + getProcedureArgumentMode().name());
      }
      if (getDefault() != null) {
        options.add("default_value: " + getDefault());
      }
      if (getArgumentAliasKind() != null
          && getArgumentAliasKind() != FunctionEnums.ArgumentAliasKind.ARGUMENT_NON_ALIASED) {
        options.add("argument_alias_kind: " + getArgumentAliasKind().name());
      }

      if (options.isEmpty()) {
        return "";
      }
      return " {" + String.join(", ", options) + "}";
    }

    /** Builder for FunctionArgumentTypeOptions. */
    @AutoValue.Builder
    public abstract static class Builder {
      public abstract Builder setCardinality(ArgumentCardinality cardinality);

      abstract Builder setConstnessLevel(ConstnessLevelProto.Level constnessLevel);

      @Nullable
      abstract ConstnessLevelProto.Level getConstnessLevel();

      @CanIgnoreReturnValue
      public Builder setMustBeConstant(boolean constant) {
        if (constant) {
          Preconditions.checkState(
              getConstnessLevel() == null
                  || getConstnessLevel() == ConstnessLevelProto.Level.CONSTNESS_UNSPECIFIED
                  || getConstnessLevel() == ConstnessLevelProto.Level.LEGACY_LITERAL_OR_PARAMETER,
              "Cannot set mustBeConstant when another constness level is already set.");
          setConstnessLevel(ConstnessLevelProto.Level.LEGACY_LITERAL_OR_PARAMETER);
        } else {
          setConstnessLevel(ConstnessLevelProto.Level.CONSTNESS_UNSPECIFIED);
        }
        return this;
      }

      @CanIgnoreReturnValue
      public Builder setMustBeConstantExpression(boolean constantExpression) {
        if (constantExpression) {
          Preconditions.checkState(
              getConstnessLevel() == null
                  || getConstnessLevel() == ConstnessLevelProto.Level.CONSTNESS_UNSPECIFIED
                  || getConstnessLevel() == ConstnessLevelProto.Level.LEGACY_CONSTANT_EXPRESSION,
              "Cannot set mustBeConstantExpression when another constness level is already set.");
          setConstnessLevel(ConstnessLevelProto.Level.LEGACY_CONSTANT_EXPRESSION);
        } else {
          setConstnessLevel(ConstnessLevelProto.Level.CONSTNESS_UNSPECIFIED);
        }
        return this;
      }

      @CanIgnoreReturnValue
      public Builder setMustBeAnalysisConstant(boolean analysisConstant) {
        if (analysisConstant) {
          Preconditions.checkState(
              getConstnessLevel() == null
                  || getConstnessLevel() == ConstnessLevelProto.Level.CONSTNESS_UNSPECIFIED
                  || getConstnessLevel() == ConstnessLevelProto.Level.ANALYSIS_CONST,
              "Cannot set mustBeAnalysisConstant when another constness level is already set.");
          setConstnessLevel(ConstnessLevelProto.Level.ANALYSIS_CONST);
        } else {
          setConstnessLevel(ConstnessLevelProto.Level.CONSTNESS_UNSPECIFIED);
        }
        return this;
      }

      @CanIgnoreReturnValue
      public Builder setMustBeImmutableConstant(boolean immutableConstant) {
        if (immutableConstant) {
          Preconditions.checkState(
              getConstnessLevel() == null
                  || getConstnessLevel() == ConstnessLevelProto.Level.CONSTNESS_UNSPECIFIED
                  || getConstnessLevel() == ConstnessLevelProto.Level.IMMUTABLE_CONST,
              "Cannot set mustBeImmutableConstant when another constness level "
                  + "is already set.");
          setConstnessLevel(ConstnessLevelProto.Level.IMMUTABLE_CONST);
        } else {
          setConstnessLevel(ConstnessLevelProto.Level.CONSTNESS_UNSPECIFIED);
        }
        return this;
      }

      public abstract Builder setMustBeNonNull(Boolean notNull);

      public abstract Builder setIsNotAggregate(Boolean notAggregate);

      public abstract Builder setMustSupportEquality(Boolean mustSupportEquality);

      public abstract Builder setMustSupportOrdering(Boolean mustSupportOrdering);

      public abstract Builder setMustSupportGrouping(Boolean mustSupportGrouping);

      public abstract Builder setArrayElementMustSupportEquality(
          Boolean elementMustSupportEquality);

      public abstract Builder setArrayElementMustSupportOrdering(
          Boolean elementMustSupportOrdering);

      public abstract Builder setArrayElementMustSupportGrouping(
          Boolean elementMustSupportGrouping);

      public abstract Builder setMinValue(Long minValue);

      public abstract Builder setMaxValue(Long maxValue);

      public abstract Builder setExtraRelationInputColumnsAllowed(Boolean extraColumnsAllowed);

      public abstract Builder setRelationInputSchema(TVFRelation inputSchema);

      abstract Builder setArgumentName(String name);

      @CanIgnoreReturnValue
      public Builder setArgumentName(
          String name, FunctionEnums.NamedArgumentKind namedArgumentKind) {
        return setArgumentName(name).setNamedArgumentKind(namedArgumentKind);
      }

      abstract Builder setNamedArgumentKind(FunctionEnums.NamedArgumentKind namedArgumentKind);

      public abstract Builder setArgumentNameParseLocation(ParseLocationRange nameLocation);

      public abstract Builder setArgumentTypeParseLocation(ParseLocationRange typeLocation);

      public abstract Builder setProcedureArgumentMode(
          FunctionEnums.ProcedureArgumentMode procedureArgumentMode);

      public abstract Builder setDescriptorResolutionTableOffset(Integer offset);

      public abstract Builder setDefault(Value defaultValue);

      public abstract Builder setArgumentCollationMode(
          FunctionEnums.ArgumentCollationMode argumentCollationMode);

      public abstract Builder setArgumentAliasKind(
          FunctionEnums.ArgumentAliasKind argumentAliasKind);

      abstract FunctionArgumentTypeOptions autoBuild();

      public FunctionArgumentTypeOptions build() {
        FunctionArgumentTypeOptions options = autoBuild();
        if (options.getDefault() != null) {
          Preconditions.checkArgument(
              options.getCardinality() == ArgumentCardinality.OPTIONAL,
              "Default value cannot be applied to a %s argument",
              options.getCardinality());
        }
        return options;
      }
    }
  }
}
