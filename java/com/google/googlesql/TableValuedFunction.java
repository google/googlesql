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
import static com.google.common.collect.ImmutableList.toImmutableList;
import static com.google.common.collect.Iterables.getOnlyElement;
import static java.nio.charset.StandardCharsets.UTF_8;

import com.google.common.base.Joiner;
import com.google.common.base.Preconditions;
import com.google.common.collect.ImmutableList;
import com.google.errorprone.annotations.InlineMe;
import com.google.protobuf.InvalidProtocolBufferException;
import com.google.googlesql.FunctionProtos.TVFRelationColumnProto;
import com.google.googlesql.FunctionProtos.TVFRelationProto;
import com.google.googlesql.FunctionProtos.TableValuedFunctionOptionsProto;
import com.google.googlesql.FunctionProtos.TableValuedFunctionProto;
import com.google.googlesql.GoogleSQLFunctions.FunctionEnums;
import com.google.googlesql.GoogleSQLFunctions.FunctionEnums.TableValuedFunctionType;
import com.google.googlesql.GoogleSQLFunctions.FunctionEnums.Volatility;
import java.io.Serializable;
import java.util.List;
import javax.annotation.Nullable;

/**
 * This interface describes a table-valued function (TVF) available in a query engine.
 *
 * <p>More information in googlesql/public/table_valued_function.h
 */
public class TableValuedFunction implements Serializable {

  private final ImmutableList<String> namePath;
  private final String group;
  private final ImmutableList<FunctionSignature> signatures;
  private final ImmutableList<TVFRelation.Column> columns;
  @Nullable private final String customContext;
  @Nullable private final Volatility volatility;
  private final TableValuedFunctionOptionsProto options;

  /**
   * Constructs a new TVF object with the given name and argument signature.
   *
   * <p>Each TVF may accept value or relation arguments. The signature specifies whether each
   * argument should be a value or a relation. For a value argument, the signature may specify a
   * concrete {@code Type} or a (possibly templated) {@code SignatureArgumentKind}. For relation
   * arguments, the signature should use {@code ARG_TYPE_RELATION}, and any relation will be
   * accepted as an argument.
   */
  protected TableValuedFunction(
      ImmutableList<String> namePath,
      String group,
      ImmutableList<FunctionSignature> signatures,
      ImmutableList<TVFRelation.Column> columns,
      @Nullable String customContext,
      @Nullable Volatility volatility,
      TableValuedFunctionOptionsProto options) {
    checkArgument(!signatures.isEmpty());
    this.namePath = namePath;
    this.group = group;
    this.signatures = signatures;
    this.columns = columns;
    this.customContext = customContext;
    this.volatility = volatility;
    this.options = options;
  }

  /** Deserializes a table-valued function from a protocol buffer. */
  public static TableValuedFunction deserialize(
      TableValuedFunctionProto proto,
      ImmutableList<? extends DescriptorPool> pools,
      TypeFactory typeFactory) {
    // Deserialize here if type is `BASIS_TVF` otherwise dispatch to corresponding class.
    switch (proto.getType()) {
      case BASIS_TVF:
        return new TableValuedFunction(
            ImmutableList.copyOf(proto.getNamePathList()),
            proto.getGroup(),
            deserializeSignatures(proto, pools),
            ImmutableList.of(),
            null,
            null,
            proto.getOptions());
      case FIXED_OUTPUT_SCHEMA_TVF:
        return FixedOutputSchemaTVF.deserialize(proto, pools, typeFactory);
      case FORWARD_INPUT_SCHEMA_TO_OUTPUT_SCHEMA_TVF:
        return ForwardInputSchemaToOutputSchemaTVF.deserialize(proto, pools, typeFactory);
      case TEMPLATED_SQL_TVF:
        return TemplatedSQLTVF.deserialize(proto, pools, typeFactory);
      case FORWARD_INPUT_SCHEMA_TO_OUTPUT_SCHEMA_WITH_APPENDED_COLUMNS:
        return ForwardInputSchemaToOutputSchemaWithAppendedColumnTVF.deserialize(
            proto, pools, typeFactory);
      default:
        throw new IllegalArgumentException(
            "Serialization is not implemented yet for table-valued function: "
                + String.join(".", proto.getNamePathList()));
    }
  }

  /** Serializes this table-valued function to a protocol buffer. */
  public TableValuedFunctionProto serialize(FileDescriptorSetsBuilder fileDescriptorSetsBuilder) {
    TableValuedFunctionProto.Builder builder =
        TableValuedFunctionProto.newBuilder()
            .addAllNamePath(namePath)
            .setOptions(options)
            .setGroup(group)
            .setType(getType());
    for (FunctionSignature signature : signatures) {
      builder.addSignatures(signature.serialize(fileDescriptorSetsBuilder));
    }
    // TODO: Remove once the signature field is no longer used.
    // For backward compatibility, set both signatures and signature field if there is only one TVF
    // signature.
    if (signatures.size() == 1) {
      builder.setSignature(getOnlyElement(signatures).serialize(fileDescriptorSetsBuilder));
    }
    builder.setType(getType());
    if (getCustomContext() != null) {
      builder.setCustomContext(getCustomContext());
    }
    if (getVolatility() != null) {
      builder.setVolatility(getVolatility());
    }
    switch (getType()) {
      case BASIS_TVF:
      // TODO - Set the output schema of FixedOutputSchemaTVF in proto custom context.
      // Currently the output schema is ignored while serialization and the deserializer uses the
      // schema from resultType of the FunctionSignature.
      case FIXED_OUTPUT_SCHEMA_TVF:
      case FORWARD_INPUT_SCHEMA_TO_OUTPUT_SCHEMA_TVF:
        break;
      case FORWARD_INPUT_SCHEMA_TO_OUTPUT_SCHEMA_WITH_APPENDED_COLUMNS:
        TVFRelationProto.Builder relationBuilder = TVFRelationProto.newBuilder();
        for (TVFRelation.Column column : columns) {
          relationBuilder.addColumn(
              TVFRelationColumnProto.newBuilder()
                  .setName(column.getName())
                  .setType(column.getType().serialize(fileDescriptorSetsBuilder))
                  .build());
        }
        builder.setCustomContext(new String(relationBuilder.build().toByteArray(), UTF_8));
        break;
      case TEMPLATED_SQL_TVF:
        for (String name : ((TemplatedSQLTVF) this).argumentNames) {
          builder.addArgumentName(name);
        }
        builder.setParseResumeLocation(((TemplatedSQLTVF) this).parseResumeLocation.serialize());
        break;
      default:
        throw new IllegalArgumentException(
            "Serialization is not implemented yet for table-valued function: " + getFullName());
    }
    return builder.build();
  }

  FunctionEnums.TableValuedFunctionType getType() {
    return FunctionEnums.TableValuedFunctionType.BASIS_TVF;
  }

  public String getName() {
    return namePath.get(namePath.size() - 1);
  }

  public ImmutableList<String> getNamePath() {
    return namePath;
  }

  public String getGroup() {
    return group;
  }

  public boolean isGoogleSQLBuiltin() {
    return group.equals(Function.GOOGLESQL_FUNCTION_GROUP_NAME);
  }

  /**
   * @deprecated Use {@link #getFunctionSignatures()} instead. This function only returns the first
   *     signature of the TVF.
   */
  @InlineMe(replacement = "this.getFunctionSignatures().get(0)")
  @Deprecated
  public final FunctionSignature getFunctionSignature() {
    return getFunctionSignatures().get(0);
  }

  public ImmutableList<FunctionSignature> getFunctionSignatures() {
    return signatures;
  }

  public String getFullName() {
    return getFullName(true);
  }

  public String getFullName(boolean includeGroup) {
    String pathname = Joiner.on('.').join(namePath);
    if (includeGroup && !group.isEmpty()) {
      return group + ":" + pathname;
    } else {
      return pathname;
    }
  }

  @Nullable
  public String getCustomContext() {
    return customContext;
  }

  @Nullable
  public Volatility getVolatility() {
    return volatility;
  }

  @Override
  public final String toString() {
    return getFullName()
        + "("
        + FunctionSignature.signaturesToString(
            signatures, /* verbose= */ true, /* prefix= */ "", /* separator= */ "; ")
        + ")";
  }

  public String toDebugString(boolean verbose) {
    return getFullName()
        + "\n"
        + FunctionSignature.signaturesToString(
            signatures, verbose, /* prefix= */ "  ", /* separator= */ "\n");
  }

  public boolean isDefaultValue() {
    return true;
  }

  private static ImmutableList<FunctionSignature> deserializeSignatures(
      TableValuedFunctionProto proto, ImmutableList<? extends DescriptorPool> pools) {
    Preconditions.checkArgument(
        proto.getSignaturesCount() > 0 || proto.hasSignature(),
        "Expected either signature or signatures field to be set: %s",
        proto);
    if (proto.getSignaturesCount() > 0) {
      return proto.getSignaturesList().stream()
          .map(signature -> FunctionSignature.deserialize(signature, pools))
          .collect(toImmutableList());
    } else {
      return ImmutableList.of(FunctionSignature.deserialize(proto.getSignature(), pools));
    }
  }

  /** A TVF that always returns a relation with the same fixed output schema. */
  public static class FixedOutputSchemaTVF extends TableValuedFunction {
    private final TVFRelation outputSchema;

    @InlineMe(
        replacement = "this(namePath, ImmutableList.of(signature), outputSchema)",
        imports = "com.google.common.collect.ImmutableList")
    @Deprecated
    public FixedOutputSchemaTVF(
        ImmutableList<String> namePath, FunctionSignature signature, TVFRelation outputSchema) {
      this(namePath, ImmutableList.of(signature), outputSchema);
    }

    public FixedOutputSchemaTVF(
        List<String> namePath, List<FunctionSignature> signatures, TVFRelation outputSchema) {
      this(
          namePath, signatures, outputSchema, TableValuedFunctionOptionsProto.getDefaultInstance());
    }

    @InlineMe(
        replacement = "this(namePath, ImmutableList.of(signature), outputSchema, options)",
        imports = "com.google.common.collect.ImmutableList")
    @Deprecated
    public FixedOutputSchemaTVF(
        ImmutableList<String> namePath,
        FunctionSignature signature,
        TVFRelation outputSchema,
        TableValuedFunctionOptionsProto options) {
      this(namePath, ImmutableList.of(signature), outputSchema, options);
    }

    public FixedOutputSchemaTVF(
        List<String> namePath,
        List<FunctionSignature> signatures,
        TVFRelation outputSchema,
        TableValuedFunctionOptionsProto options) {
      super(
          ImmutableList.copyOf(namePath),
          /* group= */ "",
          ImmutableList.copyOf(signatures),
          ImmutableList.of(),
          /* customContext= */ null,
          /* volatility= */ null,
          options);
      this.outputSchema = outputSchema;
    }

    public TVFRelation getOutputSchema() {
      return outputSchema;
    }

    @Override
    public FunctionEnums.TableValuedFunctionType getType() {
      return FunctionEnums.TableValuedFunctionType.FIXED_OUTPUT_SCHEMA_TVF;
    }

    /** Deserializes this table-valued function from a protocol buffer. */
    public static FixedOutputSchemaTVF deserialize(
        TableValuedFunctionProto proto,
        ImmutableList<? extends DescriptorPool> pools,
        TypeFactory typeFactory) {
      ImmutableList<String> namePath = ImmutableList.copyOf(proto.getNamePathList());
      ImmutableList<FunctionSignature> signatures = deserializeSignatures(proto, pools);
      // TODO - Get output schema from proto custom context.
      TVFRelation outputSchema =
          signatures.get(0).getResultType().getOptions().getRelationInputSchema();
      Preconditions.checkArgument(outputSchema != null, proto);
      Preconditions.checkArgument(
          proto.getType() == FunctionEnums.TableValuedFunctionType.FIXED_OUTPUT_SCHEMA_TVF, proto);
      return new FixedOutputSchemaTVF(namePath, signatures, outputSchema, proto.getOptions());
    }
  }

  /**
   * This represents a TVF that accepts a relation for its first argument. The TVF returns a
   * relation with the same output schema as this input relation.
   */
  public static class ForwardInputSchemaToOutputSchemaTVF extends TableValuedFunction {

    @InlineMe(
        replacement = "this(namePath, ImmutableList.of(signature))",
        imports = "com.google.common.collect.ImmutableList")
    @Deprecated
    public ForwardInputSchemaToOutputSchemaTVF(
        ImmutableList<String> namePath, FunctionSignature signature) {
      this(namePath, ImmutableList.of(signature));
    }

    public ForwardInputSchemaToOutputSchemaTVF(
        List<String> namePath, List<FunctionSignature> signatures) {
      this(
          namePath,
          signatures,
          /* customContext= */ null,
          /* volatility= */ null,
          TableValuedFunctionOptionsProto.getDefaultInstance());
    }

    @InlineMe(
        replacement = "this(namePath, ImmutableList.of(signature), options)",
        imports = "com.google.common.collect.ImmutableList")
    @Deprecated
    public ForwardInputSchemaToOutputSchemaTVF(
        ImmutableList<String> namePath,
        FunctionSignature signature,
        TableValuedFunctionOptionsProto options) {
      this(namePath, ImmutableList.of(signature), options);
    }

    public ForwardInputSchemaToOutputSchemaTVF(
        List<String> namePath,
        List<FunctionSignature> signatures,
        TableValuedFunctionOptionsProto options) {
      this(namePath, signatures, /* customContext= */ null, /* volatility= */ null, options);
    }

    @InlineMe(
        replacement = "this(namePath, ImmutableList.of(signature), customContext, volatility)",
        imports = "com.google.common.collect.ImmutableList")
    @Deprecated
    public ForwardInputSchemaToOutputSchemaTVF(
        ImmutableList<String> namePath,
        FunctionSignature signature,
        @Nullable String customContext,
        @Nullable Volatility volatility) {
      this(namePath, ImmutableList.of(signature), customContext, volatility);
    }

    public ForwardInputSchemaToOutputSchemaTVF(
        List<String> namePath,
        List<FunctionSignature> signatures,
        @Nullable String customContext,
        @Nullable Volatility volatility) {
      this(
          namePath,
          signatures,
          customContext,
          volatility,
          TableValuedFunctionOptionsProto.getDefaultInstance());
    }

    @InlineMe(
        replacement =
            "this(namePath, ImmutableList.of(signature), customContext, volatility, options)",
        imports = "com.google.common.collect.ImmutableList")
    @Deprecated
    public ForwardInputSchemaToOutputSchemaTVF(
        ImmutableList<String> namePath,
        FunctionSignature signature,
        @Nullable String customContext,
        @Nullable Volatility volatility,
        TableValuedFunctionOptionsProto options) {
      this(namePath, ImmutableList.of(signature), customContext, volatility, options);
    }

    public ForwardInputSchemaToOutputSchemaTVF(
        List<String> namePath,
        List<FunctionSignature> signatures,
        @Nullable String customContext,
        @Nullable Volatility volatility,
        TableValuedFunctionOptionsProto options) {
      super(
          ImmutableList.copyOf(namePath),
          /* group= */ "",
          ImmutableList.copyOf(signatures),
          ImmutableList.of(),
          customContext,
          volatility,
          options);
    }

    @Override
    public FunctionEnums.TableValuedFunctionType getType() {
      return FunctionEnums.TableValuedFunctionType.FORWARD_INPUT_SCHEMA_TO_OUTPUT_SCHEMA_TVF;
    }

    /** Deserializes this table-valued function from a protocol buffer. */
    public static ForwardInputSchemaToOutputSchemaTVF deserialize(
        TableValuedFunctionProto proto,
        ImmutableList<? extends DescriptorPool> pools,
        TypeFactory typeFactory) {
      Preconditions.checkArgument(
          proto.getType()
              == FunctionEnums.TableValuedFunctionType.FORWARD_INPUT_SCHEMA_TO_OUTPUT_SCHEMA_TVF,
          proto);
      String customContext = proto.hasCustomContext() ? proto.getCustomContext() : null;
      Volatility volatility = proto.hasVolatility() ? proto.getVolatility() : null;
      ImmutableList<String> namePath = ImmutableList.copyOf(proto.getNamePathList());
      ImmutableList<FunctionSignature> signatures = deserializeSignatures(proto, pools);
      return new ForwardInputSchemaToOutputSchemaTVF(
          namePath, signatures, customContext, volatility, proto.getOptions());
    }
  }

  /**
   * This represents a templated function with a SQL body.
   *
   * <p>The purpose of this class is to help support statements of the form "{@code CREATE FUNCTION
   * <name>(<arguments>) AS <query>}", where the <arguments> may have templated types like "{@code
   * ANY TYPE}". In this case, GoogleSQL cannot resolve the function expression right away and must
   * defer this work until later when the function is called with concrete argument types.
   */
  // TODO - Support multiple signatures in TemplatedSQLTVFs.
  public static class TemplatedSQLTVF extends TableValuedFunction {
    private final ImmutableList<String> argumentNames;
    private final ParseResumeLocation parseResumeLocation;

    public TemplatedSQLTVF(
        ImmutableList<String> namePath,
        FunctionSignature signature,
        ImmutableList<String> argumentNames,
        ParseResumeLocation parseResumeLocation) {
      this(
          namePath,
          signature,
          argumentNames,
          parseResumeLocation,
          TableValuedFunctionOptionsProto.getDefaultInstance());
    }

    public TemplatedSQLTVF(
        ImmutableList<String> namePath,
        FunctionSignature signature,
        ImmutableList<String> argumentNames,
        ParseResumeLocation parseResumeLocation,
        TableValuedFunctionOptionsProto options) {
      super(
          namePath,
          /* group= */ "",
          ImmutableList.of(signature),
          ImmutableList.of(),
          /* customContext= */ null,
          /* volatility= */ null,
          options);
      this.argumentNames = argumentNames;
      this.parseResumeLocation = parseResumeLocation;
    }

    public ImmutableList<String> getArgumentNames() {
      return argumentNames;
    }

    public String getSqlBody() {
      return this.parseResumeLocation
          .getInput()
          .substring(this.parseResumeLocation.getBytePosition());
    }

    @Override
    public FunctionEnums.TableValuedFunctionType getType() {
      return FunctionEnums.TableValuedFunctionType.TEMPLATED_SQL_TVF;
    }

    /** Deserializes this table-valued function from a protocol buffer. */
    public static TemplatedSQLTVF deserialize(
        TableValuedFunctionProto proto,
        ImmutableList<? extends DescriptorPool> pools,
        TypeFactory typeFactory) {
      Preconditions.checkArgument(
          proto.getType() == FunctionEnums.TableValuedFunctionType.TEMPLATED_SQL_TVF, proto);
      ImmutableList<String> namePath = ImmutableList.copyOf(proto.getNamePathList());
      FunctionSignature signature = FunctionSignature.deserialize(proto.getSignature(), pools);
      ImmutableList<String> args = ImmutableList.copyOf(proto.getArgumentNameList());
      return new TemplatedSQLTVF(
          namePath,
          signature,
          args,
          new ParseResumeLocation(proto.getParseResumeLocation()),
          proto.getOptions());
    }
  }

  /**
   * This represents a TVF that accepts a relation for its first argument. The TVF returns a
   * relation with a schema that is constructed by copying the schema of input relation and
   * appending extra columns to the schema.
   */
  public static class ForwardInputSchemaToOutputSchemaWithAppendedColumnTVF
      extends TableValuedFunction {
    @InlineMe(
        replacement =
            "this(namePath, ImmutableList.of(signature), columns, customContext, volatility)",
        imports = "com.google.common.collect.ImmutableList")
    @Deprecated
    public ForwardInputSchemaToOutputSchemaWithAppendedColumnTVF(
        ImmutableList<String> namePath,
        FunctionSignature signature,
        ImmutableList<TVFRelation.Column> columns,
        @Nullable String customContext,
        @Nullable Volatility volatility) {
      this(namePath, ImmutableList.of(signature), columns, customContext, volatility);
    }

    public ForwardInputSchemaToOutputSchemaWithAppendedColumnTVF(
        List<String> namePath,
        List<FunctionSignature> signatures,
        List<TVFRelation.Column> columns,
        @Nullable String customContext,
        @Nullable Volatility volatility) {
      this(
          namePath,
          signatures,
          columns,
          customContext,
          volatility,
          TableValuedFunctionOptionsProto.getDefaultInstance());
    }

    @InlineMe(
        replacement =
            "this(namePath, ImmutableList.of(signature), columns, customContext, volatility,"
                + " options)",
        imports = "com.google.common.collect.ImmutableList")
    @Deprecated
    public ForwardInputSchemaToOutputSchemaWithAppendedColumnTVF(
        ImmutableList<String> namePath,
        FunctionSignature signature,
        ImmutableList<TVFRelation.Column> columns,
        @Nullable String customContext,
        @Nullable Volatility volatility,
        TableValuedFunctionOptionsProto options) {
      this(namePath, ImmutableList.of(signature), columns, customContext, volatility, options);
    }

    public ForwardInputSchemaToOutputSchemaWithAppendedColumnTVF(
        List<String> namePath,
        List<FunctionSignature> signatures,
        List<TVFRelation.Column> columns,
        @Nullable String customContext,
        @Nullable Volatility volatility,
        TableValuedFunctionOptionsProto options) {
      super(
          ImmutableList.copyOf(namePath),
          /* group= */ "",
          ImmutableList.copyOf(signatures),
          ImmutableList.copyOf(columns),
          customContext,
          volatility,
          options);
    }

    public static ForwardInputSchemaToOutputSchemaWithAppendedColumnTVF deserialize(
        TableValuedFunctionProto proto,
        ImmutableList<? extends DescriptorPool> pools,
        TypeFactory typeFactory) {
      Preconditions.checkArgument(
          proto.getType()
              == TableValuedFunctionType
                  .FORWARD_INPUT_SCHEMA_TO_OUTPUT_SCHEMA_WITH_APPENDED_COLUMNS,
          proto);
      ImmutableList<String> namePath = ImmutableList.copyOf(proto.getNamePathList());
      ImmutableList<FunctionSignature> signatures = deserializeSignatures(proto, pools);
      ImmutableList.Builder<TVFRelation.Column> builder = ImmutableList.builder();
      if (proto.hasCustomContext()) {
        try {
          TVFRelationProto relationProto =
              TVFRelationProto.parseFrom(proto.getCustomContextBytes());
          for (TVFRelationColumnProto columnProto : relationProto.getColumnList()) {
            Type type = typeFactory.deserialize(columnProto.getType(), pools);
            builder.add(
                TVFRelation.Column.create(
                    columnProto.getName(), type, columnProto.getIsPseudoColumn()));
          }
        } catch (InvalidProtocolBufferException e) {
          throw new IllegalArgumentException(
              "Failed to deserialize TVFRelationProto from custom_context in "
                  + proto.getNamePath(proto.getNamePathCount() - 1),
              e);
        }
      }
      String customContext = proto.hasCustomContext() ? proto.getCustomContext() : null;
      Volatility volatility = proto.getVolatility();
      return new ForwardInputSchemaToOutputSchemaWithAppendedColumnTVF(
          namePath, signatures, builder.build(), customContext, volatility, proto.getOptions());
    }

    @Override
    TableValuedFunctionType getType() {
      return TableValuedFunctionType.FORWARD_INPUT_SCHEMA_TO_OUTPUT_SCHEMA_WITH_APPENDED_COLUMNS;
    }
  }
}
