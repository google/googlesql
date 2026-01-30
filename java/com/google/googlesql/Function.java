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
import com.google.googlesql.FunctionProtos.FunctionOptionsProto;
import com.google.googlesql.FunctionProtos.FunctionProto;
import com.google.googlesql.FunctionProtos.FunctionSignatureProto;
import com.google.googlesql.GoogleSQLFunctions.FunctionEnums.Mode;
import com.google.googlesql.GoogleSQLFunctions.FunctionEnums.WindowOrderSupport;
import java.io.Serializable;
import java.util.ArrayList;
import java.util.Arrays;
import java.util.List;

/**
 * The Function interface identifies the functions available in a query engine. Each Function
 * includes a set of FunctionSignatures, where a signature indicates:
 *
 * <ol>
 *   <li>Argument and result types.
 *   <li>A 'context' for the signature.
 * </ol>
 *
 * <p>A Function also indicates the 'group' it belongs to.
 *
 * <p>The Function also includes {@code <options>} for specifying additional resolution
 * requirements, if any. Additionally, {@code <options>} can identify a function alias/synonym that
 * Catalogs must expose for function lookups by name.
 */
public class Function implements Serializable {

  static final String GOOGLESQL_FUNCTION_GROUP_NAME = "GoogleSQL";

  private final ImmutableList<String> namePath;
  private final String group;
  private final Mode mode;
  private final ImmutableList<FunctionSignature> signatures;
  private final FunctionOptionsProto options;

  /**
   * Construct a Function.
   *
   * @param namePath Name (and namespaces) of the function.
   * @param group
   * @param mode Enum value, one of SCALAR, AGGREGATE, ANALYTIC.
   * @param signatures
   * @param options
   */
  public Function(
      List<String> namePath,
      String group,
      Mode mode,
      List<FunctionSignature> signatures,
      FunctionOptionsProto options) {
    Preconditions.checkArgument(!namePath.isEmpty());
    this.namePath = ImmutableList.copyOf(namePath);
    this.group = Preconditions.checkNotNull(group);
    this.mode = Preconditions.checkNotNull(mode);
    this.signatures = ImmutableList.copyOf(signatures);
    this.options = Preconditions.checkNotNull(options);
    checkWindowSupportOptions();
  }

  public Function(
      List<String> namePath, String group, Mode mode, List<FunctionSignature> signatures) {
    this(namePath, group, mode, signatures, FunctionOptionsProto.getDefaultInstance());
  }

  public Function(
      String name,
      String group,
      Mode mode,
      List<FunctionSignature> signatures,
      FunctionOptionsProto options) {
    this(Arrays.asList(name), group, mode, signatures, options);
  }

  public Function(String name, String group, Mode mode, List<FunctionSignature> signatures) {
    this(Arrays.asList(name), group, mode, signatures);
  }

  public Function(String name, String group, Mode mode) {
    this(name, group, mode, ImmutableList.<FunctionSignature>of());
  }

  public FunctionProto serialize(FileDescriptorSetsBuilder fileDescriptorSetsBuilder) {
    FunctionProto.Builder builder = FunctionProto.newBuilder()
        .addAllNamePath(namePath)
        .setGroup(group)
        .setMode(mode)
        .setOptions(options);
    for (FunctionSignature signature : signatures) {
      builder.addSignature(signature.serialize(fileDescriptorSetsBuilder));
    }
    if (group.equals(TemplatedSQLFunction.TEMPLATED_SQL_FUNCTION_GROUP)) {
      builder.setParseResumeLocation(((TemplatedSQLFunction) this).parseResumeLocation.serialize());
      for (String name : ((TemplatedSQLFunction) this).argumentNames) {
        builder.addTemplatedSqlFunctionArgumentName(name);
      }
    }
    return builder.build();
  }

  public static Function deserialize(
      FunctionProto proto, final ImmutableList<? extends DescriptorPool> pools) {
    if (proto.getGroup().equals(TemplatedSQLFunction.TEMPLATED_SQL_FUNCTION_GROUP)) {
      return TemplatedSQLFunction.deserialize(proto, pools);
    }
    List<FunctionSignature> signatures = new ArrayList<>();
    for (FunctionSignatureProto signature : proto.getSignatureList()) {
      signatures.add(FunctionSignature.deserialize(signature, pools));
    }
    return new Function(
        proto.getNamePathList(), proto.getGroup(), proto.getMode(), signatures,
        proto.getOptions());
  }

  public String getName() {
    return namePath.get(namePath.size() - 1);
  }

  public ImmutableList<String> getNamePath() {
    return namePath;
  }

  public String getFullName() {
    return getFullName(true);
  }

  public String getFullName(boolean includeGroup) {
    String pathname = Joiner.on('.').join(namePath);
    if (includeGroup) {
      return group + ":" + pathname;
    } else {
      return pathname;
    }
  }

  public String getSqlName() {
    String name;
    if (!options.getSqlName().isEmpty()) {
      name = options.getSqlName();
    } else if (getName().startsWith("$")) {
      // The name starts with '$' so it is an internal function name.  Strip off
      // the leading '$' and convert all '_' to ' '.
      name = getName().substring(1).replace('_', ' ');
    } else if (isGoogleSQLBuiltin()) {
      name = getFullName(/* includeGroup= */ false);
    } else {
      name = getFullName();
    }
    if (options.getUsesUpperCaseSqlName()) {
      name = name.toUpperCase();
    }
    return name;
  }

  public String getGroup() {
    return group;
  }

  public ImmutableList<FunctionSignature> getSignatureList() {
    return signatures;
  }

  public Mode getMode() {
    return mode;
  }

  public FunctionOptionsProto getOptions() {
    return options;
  }

  public boolean isScalar() {
    return mode == Mode.SCALAR;
  }

  public boolean isAggregate() {
    return mode == Mode.AGGREGATE;
  }

  public boolean isAnalytic() {
    return mode == Mode.ANALYTIC;
  }

  public String debugString(boolean verbose) {
    if (!verbose) {
      return getFullName();
    }

    StringBuilder builder = new StringBuilder(getFullName());
    String alias = options.getAliasName();
    if (!alias.isEmpty()) {
      builder.append('|').append(alias);
    }

    if (!signatures.isEmpty()) {
      builder.append('\n').append(FunctionSignature.signaturesToString(signatures));
    }

    return builder.toString();
  }

  @Override
  public String toString() {
    return debugString(false);
  }

  public boolean isGoogleSQLBuiltin() {
    return group.equals(GOOGLESQL_FUNCTION_GROUP_NAME);
  }

  public boolean supportsWindowOrdering() {
    WindowOrderSupport windowOrderingSupport = options.getWindowOrderingSupport();
    return windowOrderingSupport == WindowOrderSupport.ORDER_REQUIRED
        || windowOrderingSupport == WindowOrderSupport.ORDER_OPTIONAL;
  }

  public boolean requireWindowOrdering() {
    return options.getWindowOrderingSupport() == WindowOrderSupport.ORDER_REQUIRED;
  }

  private void checkWindowSupportOptions() {
    if (isScalar() && options.getSupportsOverClause()) {
      throw new IllegalArgumentException("Scalar functions cannot support over clause");
    }
    if (isAnalytic() && !options.getSupportsOverClause()) {
      throw new IllegalArgumentException("Analytic functions must support over clause");
    }
  }

  /**
   * This represents a templated function with a SQL body.
   *
   * <p>The purpose of this class is to help support statements of the form "CREATE FUNCTION
   * <name>(<arguments>) AS <query>", where the <arguments> may have templated types like "ANY
   * TYPE". In this case, GoogleSQL cannot resolve the function expression right away and must defer
   * this work until later when the function is called with concrete argument types.
   */
  public static class TemplatedSQLFunction extends Function {
    private final ImmutableList<String> argumentNames;
    private final ParseResumeLocation parseResumeLocation;

    public static final String TEMPLATED_SQL_FUNCTION_GROUP = "Templated_SQL_Function";

    public TemplatedSQLFunction(
        ImmutableList<String> namePath,
        FunctionSignature signature,
        ImmutableList<String> argumentNames,
        ParseResumeLocation parseResumeLocation) {
      this(
          namePath,
          signature,
          argumentNames,
          parseResumeLocation,
          Mode.SCALAR,
          FunctionOptionsProto.getDefaultInstance());
    }

    public TemplatedSQLFunction(
        ImmutableList<String> namePath,
        FunctionSignature signature,
        ImmutableList<String> argumentNames,
        ParseResumeLocation parseResumeLocation,
        Mode mode,
        FunctionOptionsProto options) {
      super(namePath, TEMPLATED_SQL_FUNCTION_GROUP, mode, ImmutableList.of(signature), options);
      this.argumentNames = argumentNames;
      this.parseResumeLocation = parseResumeLocation;
    }

    public ImmutableList<String> getArgumentNames() {
      return argumentNames;
    }

    public String getSqlBody() {
      return this.parseResumeLocation.getInput().substring(this.parseResumeLocation.getBytePosition());
    }

    /* Deserializes this function from a protocol buffer. */
    public static TemplatedSQLFunction deserialize(
        FunctionProto proto, final ImmutableList<? extends DescriptorPool> pools) {
      Preconditions.checkArgument(proto.getGroup().equals(TEMPLATED_SQL_FUNCTION_GROUP), proto);
      ImmutableList<String> namePath = ImmutableList.copyOf(proto.getNamePathList());
      FunctionSignature signature = FunctionSignature.deserialize(proto.getSignature(0), pools);
      ImmutableList.Builder<String> builder = new ImmutableList.Builder<String>();
      builder.addAll(proto.getTemplatedSqlFunctionArgumentNameList());
      return new TemplatedSQLFunction(
          namePath,
          signature,
          builder.build(),
          new ParseResumeLocation(proto.getParseResumeLocation()),
          proto.getMode(),
          proto.getOptions());
    }
  }
}
