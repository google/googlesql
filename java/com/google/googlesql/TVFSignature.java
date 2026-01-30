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

import static java.util.stream.Collectors.joining;

import com.google.auto.value.AutoValue;
import com.google.common.collect.ImmutableList;
import com.google.googlesql.DeprecationWarningProtos.DeprecationWarning;
import com.google.googlesql.DeprecationWarningProtos.DeprecationWarning.Kind;
import com.google.googlesql.DeprecationWarningProtos.FreestandingDeprecationWarning;
import com.google.googlesql.ErrorLocationOuterClass.ErrorLocation;
import com.google.googlesql.ErrorLocationOuterClass.ErrorSource;
import com.google.googlesql.FunctionProtos.TVFArgumentProto;
import com.google.googlesql.FunctionProtos.TVFConnectionProto;
import com.google.googlesql.FunctionProtos.TVFDescriptorProto;
import com.google.googlesql.FunctionProtos.TVFGraphProto;
import com.google.googlesql.FunctionProtos.TVFModelProto;
import com.google.googlesql.FunctionProtos.TVFSignatureOptionsProto;
import com.google.googlesql.FunctionProtos.TVFSignatureProto;
import com.google.googlesql.GoogleSQLType.TypeKind;
import java.io.Serializable;
import java.util.Objects;
import javax.annotation.Nullable;

/** Describes the signature of a table-valued-function. */
public final class TVFSignature implements Serializable {
  private final ImmutableList<TVFArgument> args;
  private final TVFSignatureOptions options;
  private final TVFRelation outputSchema;

  TVFSignature(
      ImmutableList<TVFArgument> args, TVFSignatureOptions options, TVFRelation outputSchema) {
    this.args = args;
    this.options = options;
    this.outputSchema = outputSchema;
  }

  /** Deserializes a signature from a proto. */
  public static TVFSignature deserialize(
      TVFSignatureProto proto, final ImmutableList<? extends DescriptorPool> pools) {

    ImmutableList.Builder<TVFArgument> builder = ImmutableList.builder();
    proto.getArgumentList().forEach(arg -> builder.add(TVFArgument.deserialize(arg, pools)));
    ImmutableList<TVFArgument> args = builder.build();

    TVFSignatureOptions options = TVFSignatureOptions.deserialize(proto.getOptions());

    TVFRelation tvfRelation =
        TVFRelation.deserialize(
            proto.getOutputSchema(), ImmutableList.copyOf(pools), TypeFactory.nonUniqueNames());

    return new TVFSignature(args, options, tvfRelation);
  }

  /** Serializes this signature into a proto. */
  public TVFSignatureProto serialize(FileDescriptorSetsBuilder fileDescriptorSetsBuilder) {
    TVFSignatureProto.Builder builder = TVFSignatureProto.newBuilder();
    for (TVFArgument arg : args) {
      builder.addArgument(arg.serialize(fileDescriptorSetsBuilder));
    }
    return builder
        .setOptions(options.serialize())
        .setOutputSchema(outputSchema.serialize(fileDescriptorSetsBuilder))
        .build();
  }

  @Override
  public String toString() {
    return "("
        + args.stream().map(a -> a.toDebugString(true)).collect(joining(", "))
        + ") -> "
        + this.outputSchema;
  }

  @Override
  public boolean equals(Object o) {
    if (this == o) {
      return true;
    }
    if (!(o instanceof TVFSignature)) {
      return false;
    }
    TVFSignature that = (TVFSignature) o;
    return args.equals(that.args)
        && options.equals(that.options)
        && outputSchema.equals(that.outputSchema);
  }

  @Override
  public int hashCode() {
    return Objects.hash(args, options, outputSchema);
  }

  public boolean isDefaultValue() {
    return true;
  }

  /** Optional aspects of a signature. */
  public static class TVFSignatureOptions implements Serializable {
    private final ImmutableList<AdditionalDeprecationWarning> additionalDeprecationWarnings;

    TVFSignatureOptions(ImmutableList<AdditionalDeprecationWarning> additionalDeprecationWarnings) {
      this.additionalDeprecationWarnings = additionalDeprecationWarnings;
    }

    public static TVFSignatureOptions deserialize(TVFSignatureOptionsProto proto) {
      ImmutableList.Builder<AdditionalDeprecationWarning> builder = ImmutableList.builder();
      proto
          .getAdditionalDeprecationWarningList()
          .forEach(warning -> builder.add(AdditionalDeprecationWarning.deserialize(warning)));
      ImmutableList<AdditionalDeprecationWarning> additionalDeprecationWarnings = builder.build();
      return new TVFSignatureOptions(additionalDeprecationWarnings);
    }

    public TVFSignatureOptionsProto serialize() {
      TVFSignatureOptionsProto.Builder builder = TVFSignatureOptionsProto.newBuilder();
      additionalDeprecationWarnings.forEach(
          warning -> builder.addAdditionalDeprecationWarning(warning.serialize()));
      return builder.build();
    }

    @Override
    public String toString() {
      int size = additionalDeprecationWarnings.size();
      if (size > 0) {
        return "(" + size + " deprecation warning" + (size != 1 ? "s" : "") + ")";
      }
      return "";
    }

    @Override
    public boolean equals(Object o) {
      if (this == o) {
        return true;
      }
      if (!(o instanceof TVFSignatureOptions)) {
        return false;
      }
      TVFSignatureOptions that = (TVFSignatureOptions) o;
      return additionalDeprecationWarnings.equals(that.additionalDeprecationWarnings);
    }

    @Override
    public int hashCode() {
      return Objects.hashCode(additionalDeprecationWarnings);
    }
  }

  /** More deprecation warnings. */
  public static class AdditionalDeprecationWarning implements Serializable {
    private final String message;
    private final String caretString;
    private final SqlErrorLocation errorLocation;
    private final SqlDeprecationWarning deprecationWarning;

    public AdditionalDeprecationWarning(
        String message,
        String caretString,
        SqlErrorLocation errorLocation,
        SqlDeprecationWarning deprecationWarning) {
      this.message = message;
      this.caretString = caretString;
      this.errorLocation = errorLocation;
      this.deprecationWarning = deprecationWarning;
    }

    public static AdditionalDeprecationWarning deserialize(FreestandingDeprecationWarning proto) {
      return new AdditionalDeprecationWarning(
          proto.getMessage(),
          proto.getCaretString(),
          SqlErrorLocation.deserialize(proto.getErrorLocation()),
          SqlDeprecationWarning.deserialize(proto.getDeprecationWarning()));
    }

    public FreestandingDeprecationWarning serialize() {
      return FreestandingDeprecationWarning.newBuilder()
          .setMessage(message)
          .setCaretString(caretString)
          .setErrorLocation(errorLocation.serialize())
          .setDeprecationWarning(deprecationWarning.serialize())
          .build();
    }

    @Override
    public boolean equals(Object o) {
      if (this == o) {
        return true;
      }
      if (!(o instanceof AdditionalDeprecationWarning)) {
        return false;
      }
      AdditionalDeprecationWarning that = (AdditionalDeprecationWarning) o;
      return message.equals(that.message)
          && caretString.equals(that.caretString)
          && errorLocation.equals(that.errorLocation)
          && deprecationWarning.equals(that.deprecationWarning);
    }

    @Override
    public int hashCode() {
      return Objects.hash(message, caretString, errorLocation, deprecationWarning);
    }

    static class SqlErrorLocation implements Serializable {
      private final int line;
      private final int column;
      private final String filename;
      private final ImmutableList<SqlErrorSource> errorSources;

      public SqlErrorLocation(
          int line, int column, String filename, ImmutableList<SqlErrorSource> errorSources) {
        this.line = line;
        this.column = column;
        this.filename = filename;
        this.errorSources = errorSources;
      }

      public static SqlErrorLocation deserialize(ErrorLocation proto) {
        ImmutableList.Builder<SqlErrorSource> errorSourceBuilder = ImmutableList.builder();
        proto
            .getErrorSourceList()
            .forEach(es -> errorSourceBuilder.add(SqlErrorSource.deserialize(es)));
        return new SqlErrorLocation(
            proto.getLine(), proto.getColumn(), proto.getFilename(), errorSourceBuilder.build());
      }

      public ErrorLocation serialize() {
        ErrorLocation.Builder builder =
            ErrorLocation.newBuilder().setLine(line).setColumn(column).setFilename(filename);
        errorSources.forEach(es -> builder.addErrorSource(es.serialize()));
        return builder.build();
      }

      @Override
      public boolean equals(Object o) {
        if (this == o) {
          return true;
        }
        if (!(o instanceof SqlErrorLocation)) {
          return false;
        }
        SqlErrorLocation that = (SqlErrorLocation) o;
        return line == that.line
            && column == that.column
            && filename.equals(that.filename)
            && errorSources.equals(that.errorSources);
      }

      @Override
      public int hashCode() {
        return Objects.hash(line, column, filename, errorSources);
      }
    }

    static class SqlErrorSource implements Serializable {
      private final String errorMessage;
      private final String errorMessageCaretString;
      private final ErrorLocation errorLocation;

      public SqlErrorSource(
          String errorMessage, String errorMessageCaretString, ErrorLocation errorLocation) {
        this.errorMessage = errorMessage;
        this.errorMessageCaretString = errorMessageCaretString;
        this.errorLocation = errorLocation;
      }

      public static SqlErrorSource deserialize(ErrorSource proto) {
        String errorMessage = proto.getErrorMessage();
        String errorMessageCaretString = proto.getErrorMessageCaretString();
        ErrorLocation errorLocation = proto.hasErrorLocation() ? proto.getErrorLocation() : null;
        return new SqlErrorSource(errorMessage, errorMessageCaretString, errorLocation);
      }

      public ErrorSource serialize() {
        ErrorSource.Builder builder =
            ErrorSource.newBuilder()
                .setErrorMessage(errorMessage)
                .setErrorMessageCaretString(errorMessageCaretString);
        if (errorLocation != null) {
          builder.setErrorLocation(errorLocation);
        }
        return builder.build();
      }

      @Override
      public boolean equals(Object o) {
        if (this == o) {
          return true;
        }
        if (!(o instanceof SqlErrorSource)) {
          return false;
        }
        SqlErrorSource that = (SqlErrorSource) o;
        return errorMessage.equals(that.errorMessage)
            && errorMessageCaretString.equals(that.errorMessageCaretString)
            && errorLocation.equals(that.errorLocation);
      }

      @Override
      public int hashCode() {
        return Objects.hash(errorMessage, errorMessageCaretString, errorLocation);
      }
    }

    static class SqlDeprecationWarning implements Serializable {
      private final Kind kind;

      public SqlDeprecationWarning(Kind kind) {
        this.kind = kind;
      }

      public static SqlDeprecationWarning deserialize(DeprecationWarning proto) {
        Kind kind = proto.getKind();
        return new SqlDeprecationWarning(kind);
      }

      public DeprecationWarning serialize() {
        return DeprecationWarning.newBuilder().setKind(kind).build();
      }

      @Override
      public boolean equals(Object o) {
        if (this == o) {
          return true;
        }
        if (!(o instanceof SqlDeprecationWarning)) {
          return false;
        }
        SqlDeprecationWarning that = (SqlDeprecationWarning) o;
        return kind == that.kind;
      }

      @Override
      public int hashCode() {
        return Objects.hashCode(kind);
      }
    }
  }

  /** Defines a single argument for a table-valued-function. */
  public static class TVFArgument implements Serializable {
    private final ValueWithType scalar;
    private final TVFRelation relation;
    private final TVFModel model;
    private final TVFConnection connection;
    private final TVFDescriptor descriptor;
    private final TVFGraph graph;

    public TVFArgument(
        ValueWithType scalar,
        TVFRelation relation,
        TVFModel model,
        TVFConnection connection,
        TVFDescriptor descriptor,
        TVFGraph graph) {
      this.scalar = scalar;
      this.relation = relation;
      this.model = model;
      this.connection = connection;
      this.descriptor = descriptor;
      this.graph = graph;
    }

    /** Deserializes an argument from a proto. */
    public static TVFArgument deserialize(
        TVFArgumentProto proto, final ImmutableList<? extends DescriptorPool> pools) {
      ValueWithType arg =
          proto.hasScalarArgument()
              ? ValueWithType.deserialize(proto.getScalarArgument(), pools)
              : null;
      TVFRelation relation =
          proto.hasRelationArgument()
              ? TVFRelation.deserialize(
                  proto.getRelationArgument(), pools, TypeFactory.nonUniqueNames())
              : null;
      TVFModel model =
          proto.hasModelArgument() ? TVFModel.deserialize(proto.getModelArgument()) : null;
      TVFConnection connection =
          proto.hasConnectionArgument()
              ? TVFConnection.deserialize(proto.getConnectionArgument())
              : null;
      TVFDescriptor descriptor =
          proto.hasDescriptorArgument()
              ? TVFDescriptor.deserialize(proto.getDescriptorArgument())
              : null;
      TVFGraph graph =
          proto.hasGraphArgument() ? TVFGraph.deserialize(proto.getGraphArgument()) : null;
      return new TVFArgument(arg, relation, model, connection, descriptor, graph);
    }

    /** Serializes this argument to a proto. */
    public TVFArgumentProto serialize(FileDescriptorSetsBuilder fileDescriptorSetsBuilder) {
      TVFArgumentProto.Builder builder = TVFArgumentProto.newBuilder();
      if (scalar != null) {
        builder.setScalarArgument(scalar.serialize(fileDescriptorSetsBuilder));
      }
      if (relation != null) {
        builder.setRelationArgument(relation.serialize(fileDescriptorSetsBuilder));
      }
      if (model != null) {
        builder.setModelArgument(model.serialize());
      }
      if (connection != null) {
        builder.setConnectionArgument(connection.serialize());
      }
      if (descriptor != null) {
        builder.setDescriptorArgument(descriptor.serialize());
      }
      if (graph != null) {
        builder.setGraphArgument(graph.serialize());
      }
      return builder.build();
    }

    @Override
    public String toString() {
      return toDebugString(false);
    }

    public String toDebugString(boolean verbose) {
      if (scalar != null) {
        return scalar.toDebugString(verbose);
      }
      if (relation != null) {
        return relation.toString();
      }
      if (model != null) {
        return model.toString();
      }
      if (connection != null) {
        return connection.toString();
      }
      if (descriptor != null) {
        return descriptor.toString();
      }
      if (graph != null) {
        return graph.toString();
      }
      return "TVFArgument";
    }

    @Override
    public boolean equals(Object o) {
      if (this == o) {
        return true;
      }
      if (!(o instanceof TVFArgument)) {
        return false;
      }
      TVFArgument that = (TVFArgument) o;
      return Objects.equals(scalar, that.scalar)
          && Objects.equals(relation, that.relation)
          && Objects.equals(model, that.model)
          && Objects.equals(connection, that.connection)
          && Objects.equals(descriptor, that.descriptor)
          && Objects.equals(graph, that.graph);
    }

    @Override
    public int hashCode() {
      return Objects.hash(scalar, relation, model, connection, descriptor, graph);
    }
  }

  /** Defines a GoogleSQL value along with its type definition. */
  public static class ValueWithType implements Serializable {
    private final Category category;
    @Nullable private final Value value;
    private final Type type;

    enum Category {
      TYPED_EXPRESSION, // non-literal, non-parameter
      TYPED_LITERAL,
      TYPED_PARAMETER,
      UNTYPED_PARAMETER,
      UNTYPED_NULL,
      UNTYPED_EMPTY_ARRAY,
    }

    public ValueWithType(Category category, @Nullable Value value, Type type) {
      this.category = category;
      this.value = value;
      this.type = type;
    }

    public static ValueWithType deserialize(
        ValueWithTypeProto proto, final ImmutableList<? extends DescriptorPool> pools) {
      Type type = TypeFactory.nonUniqueNames().deserialize(proto.getType(), pools);
      Value value = proto.hasValue() ? Value.deserialize(type, proto.getValue()) : null;
      Category category = findCategory(value, type);
      return new ValueWithType(category, value, type);
    }

    private static Category findCategory(@Nullable Value value, Type type) {
      if (type.isSimpleType() || type.isEnum() || type.isStructOrProto()) {
        return Category.TYPED_LITERAL;
      }
      if (type.getKind() == TypeKind.TYPE_UNKNOWN && (value == null || value.isNull())) {
        return Category.UNTYPED_NULL;
      }
      if (type.isArray() && value != null) {
        if (value.isNull()) {
          return Category.UNTYPED_EMPTY_ARRAY;
        }
        return Category.TYPED_LITERAL;
      }
      // When value is null (Java null, NOT value with isNull=true),
      // ValueWithType is used to carry the type information.
      // This is used in cases like: TVFArgumentType.
      if (!type.getKind().equals(TypeKind.TYPE_UNKNOWN) && value == null) {
        return Category.TYPED_EXPRESSION;
      }
      return Category.UNTYPED_NULL;
    }

    public ValueWithTypeProto serialize(FileDescriptorSetsBuilder fileDescriptorSetsBuilder) {
      ValueWithTypeProto.Builder builder = ValueWithTypeProto.newBuilder();
      if (value != null) {
        builder.setValue(value.serialize());
      }
      return builder.setType(type.serialize(fileDescriptorSetsBuilder)).build();
    }

    @Override
    public String toString() {
      return toDebugString(false);
    }

    @Override
    public boolean equals(Object o) {
      if (this == o) {
        return true;
      }
      if (!(o instanceof ValueWithType)) {
        return false;
      }
      ValueWithType that = (ValueWithType) o;
      return category == that.category
          && Objects.equals(value, that.value)
          && type.equals(that.type);
    }

    @Override
    public int hashCode() {
      return Objects.hash(category, value, type);
    }

    public String toDebugString(boolean verbose) {
      StringBuilder sb = new StringBuilder();
      if (category == Category.UNTYPED_NULL) {
        sb.append(verbose ? "untyped " : "").append("NULL");
        return sb.toString();
      }

      if (category == Category.UNTYPED_EMPTY_ARRAY) {
        sb.append(verbose ? "untyped " : "").append("empty array");
        return sb.toString();
      }

      if (value != null) {
        if (value.isNull()) {
          sb.append("null ");
        } else if (type.isSimpleType()) {
          sb.append("literal ");
        }
      } else if (verbose && isQueryParameter()) {
        if (isUntyped()) {
          sb.append("untyped ");
        }
        sb.append("parameter ");
      }

      sb.append(type.debugString());
      return sb.toString();
    }

    boolean isQueryParameter() {
      return category == Category.TYPED_PARAMETER || category == Category.UNTYPED_PARAMETER;
    }

    boolean isUntyped() {
      return category == Category.UNTYPED_NULL
          || category == Category.UNTYPED_PARAMETER
          || category == Category.UNTYPED_EMPTY_ARRAY;
    }
  }

  /** An ML model. */
  public static class TVFModel implements Serializable {
    private final String name;
    private final String fullName;

    public TVFModel(String name, String fullName) {
      this.name = name;
      this.fullName = fullName;
    }

    public static TVFModel deserialize(TVFModelProto proto) {
      return new TVFModel(proto.getName(), proto.getFullName());
    }

    public TVFModelProto serialize() {
      return TVFModelProto.newBuilder().setName(name).setFullName(fullName).build();
    }

    @Override
    public String toString() {
      return "ANY MODEL";
    }

    @Override
    public boolean equals(Object o) {
      if (this == o) {
        return true;
      }
      if (!(o instanceof TVFModel)) {
        return false;
      }
      TVFModel tvfModel = (TVFModel) o;
      return name.equals(tvfModel.name) && fullName.equals(tvfModel.fullName);
    }

    @Override
    public int hashCode() {
      return Objects.hash(name, fullName);
    }
  }

  /** A graph. */
  @AutoValue
  public abstract static class TVFGraph implements Serializable {

    static TVFGraph create(String name, String fullName) {
      return new AutoValue_TVFSignature_TVFGraph(name, fullName);
    }

    abstract String name();

    abstract String fullName();

    public static TVFGraph deserialize(TVFGraphProto proto) {
      return create(proto.getName(), proto.getFullName());
    }

    public TVFGraphProto serialize() {
      return TVFGraphProto.newBuilder().setName(name()).setFullName(fullName()).build();
    }

    @Override
    public String toString() {
      return "ANY GRAPH";
    }
  }

  /** A service connection. */
  public static class TVFConnection implements Serializable {
    private final String name;
    private final String fullName;

    public TVFConnection(String name, String fullName) {
      this.name = name;
      this.fullName = fullName;
    }

    public static TVFConnection deserialize(TVFConnectionProto proto) {
      return new TVFConnection(proto.getName(), proto.getFullName());
    }

    public TVFConnectionProto serialize() {
      return TVFConnectionProto.newBuilder().setName(name).setFullName(fullName).build();
    }

    @Override
    public String toString() {
      return "ANY CONNECTION";
    }

    @Override
    public boolean equals(Object o) {
      if (this == o) {
        return true;
      }
      if (!(o instanceof TVFConnection)) {
        return false;
      }
      TVFConnection that = (TVFConnection) o;
      return name.equals(that.name) && fullName.equals(that.fullName);
    }

    @Override
    public int hashCode() {
      return Objects.hash(name, fullName);
    }
  }

  /** A Descriptor. */
  public static class TVFDescriptor implements Serializable {
    private final ImmutableList<String> columnNames;

    public TVFDescriptor(ImmutableList<String> columnNames) {
      this.columnNames = columnNames;
    }

    public static TVFDescriptor deserialize(TVFDescriptorProto proto) {
      return new TVFDescriptor(ImmutableList.copyOf(proto.getColumnNameList()));
    }

    public TVFDescriptorProto serialize() {
      return TVFDescriptorProto.newBuilder().addAllColumnName(columnNames).build();
    }

    @Override
    public String toString() {
      return "ANY DESCRIPTOR";
    }

    @Override
    public boolean equals(Object o) {
      if (this == o) {
        return true;
      }
      if (!(o instanceof TVFDescriptor)) {
        return false;
      }
      TVFDescriptor that = (TVFDescriptor) o;
      return columnNames.equals(that.columnNames);
    }

    @Override
    public int hashCode() {
      return Objects.hashCode(columnNames);
    }
  }
}
