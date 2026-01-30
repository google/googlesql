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

import com.google.common.base.Preconditions;
import com.google.common.collect.ImmutableList;
import com.google.common.collect.ImmutableMap;
import com.google.errorprone.annotations.CanIgnoreReturnValue;
import com.google.googlesql.GoogleSQLOptions.ErrorMessageMode;
import com.google.googlesql.GoogleSQLOptions.ParameterMode;
import com.google.googlesql.GoogleSQLOptions.ParseLocationRecordType;
import com.google.googlesql.GoogleSQLOptions.ResolvedASTRewrite;
import com.google.googlesql.GoogleSQLOptions.RewriteOptions;
import com.google.googlesql.GoogleSQLOptions.StatementContext;
import com.google.googlesql.GoogleSQLOptionsProto.AnalyzerOptionsProto;
import com.google.googlesql.GoogleSQLOptionsProto.AnalyzerOptionsProto.QueryParameterProto;
import com.google.googlesql.GoogleSQLOptionsProto.AnalyzerOptionsProto.SystemVariableProto;
import com.google.googlesql.GoogleSQLType.TypeProto;
import com.google.googlesql.LocalService.AnalyzerOptionsRequest;
import java.io.IOException;
import java.io.Serializable;
import java.util.AbstractMap.SimpleImmutableEntry;
import java.util.ArrayList;
import java.util.HashMap;
import java.util.HashSet;
import java.util.List;
import java.util.Map;
import java.util.Map.Entry;
import java.util.Set;

/**
 * AnalyzerOptions contains options that affect analyzer behavior, other than LanguageOptions that
 * control the language accepted.
 */
public class AnalyzerOptions implements Serializable {
  private final Map<String, Type> queryParameters = new HashMap<>();
  private final Map<List<String>, Type> systemVariables = new HashMap<>();
  private final List<Type> positionalQueryParameters = new ArrayList<>();
  private final Map<String, Type> expressionColumns = new HashMap<>();
  private final Map<String, Type> graphProperties = new HashMap<>();
  private Entry<String, Type> inScopeExpressionColumn;
  private final Map<String, Type> ddlPseudoColumns = new HashMap<>();
  private transient AnalyzerOptionsProto.Builder builder = AnalyzerOptionsProto.newBuilder();
  private LanguageOptions languageOptions = new LanguageOptions();
  private AllowedHintsAndOptions allowedHintsAndOptions = new AllowedHintsAndOptions();
  private final List<Type> targetColumnTypes = new ArrayList<>();

  public AnalyzerOptions() {
    deserializeFrom(
        Client.getStub().getAnalyzerOptions(AnalyzerOptionsRequest.getDefaultInstance()),
        null,
        null);
  }

  @CanIgnoreReturnValue // TODO: consider removing this?
  public AnalyzerOptionsProto serialize(FileDescriptorSetsBuilder fileDescriptorSetsBuilder) {
    // Ensure serialization thread safety, by merging all options in a local builder.
    // Note: This only ensures that multiple concurrent calls to serialize are safe. If there is
    // another thread calling any of AnalyzerOptions.set* methods, it's still unsafe.
    AnalyzerOptionsProto.Builder mergedOptions = builder.clone();
    mergedOptions.clearInScopeExpressionColumn();
    if (inScopeExpressionColumn != null) {
      TypeProto.Builder typeBuilder = TypeProto.newBuilder();
      inScopeExpressionColumn.getValue().serialize(typeBuilder, fileDescriptorSetsBuilder);
      String name = inScopeExpressionColumn.getKey();
      mergedOptions.getInScopeExpressionColumnBuilder().setName(name).setType(typeBuilder.build());
    }

    mergedOptions.clearSystemVariables();
    for (Entry<List<String>, Type> systemVariable : systemVariables.entrySet()) {
      TypeProto.Builder typeProtoBuilder = TypeProto.newBuilder();
      systemVariable.getValue().serialize(typeProtoBuilder, fileDescriptorSetsBuilder);
      mergedOptions
          .addSystemVariablesBuilder()
          .addAllNamePath(systemVariable.getKey())
          .setType(typeProtoBuilder.build());
    }

    mergedOptions.clearQueryParameters();
    for (Entry<String, Type> param : queryParameters.entrySet()) {
      TypeProto.Builder typeProtoBuilder = TypeProto.newBuilder();
      param.getValue().serialize(typeProtoBuilder, fileDescriptorSetsBuilder);
      mergedOptions
          .addQueryParametersBuilder()
          .setName(param.getKey())
          .setType(typeProtoBuilder.build());
    }

    mergedOptions.clearPositionalQueryParameters();
    for (Type type : positionalQueryParameters) {
      TypeProto.Builder typeProtoBuilder = TypeProto.newBuilder();
      type.serialize(typeProtoBuilder, fileDescriptorSetsBuilder);
      mergedOptions.addPositionalQueryParameters(typeProtoBuilder.build());
    }

    mergedOptions.clearExpressionColumns();
    for (Entry<String, Type> column : expressionColumns.entrySet()) {
      if (column.getKey().equals(getInScopeExpressionColumnName())) {
        continue;
      }
      TypeProto.Builder typeProtoBuilder = TypeProto.newBuilder();
      column.getValue().serialize(typeProtoBuilder, fileDescriptorSetsBuilder);
      mergedOptions
          .addExpressionColumnsBuilder()
          .setName(column.getKey())
          .setType(typeProtoBuilder.build());
    }

    mergedOptions.clearGraphProperties();
    for (Entry<String, Type> property : graphProperties.entrySet()) {
      TypeProto.Builder typeProtoBuilder = TypeProto.newBuilder();
      property.getValue().serialize(typeProtoBuilder, fileDescriptorSetsBuilder);
      mergedOptions
          .addGraphPropertiesBuilder()
          .setName(property.getKey())
          .setType(typeProtoBuilder.build());
    }

    mergedOptions.clearDdlPseudoColumns();
    for (Entry<String, Type> column : ddlPseudoColumns.entrySet()) {
      TypeProto.Builder typeProtoBuilder = TypeProto.newBuilder();
      column.getValue().serialize(typeProtoBuilder, fileDescriptorSetsBuilder);
      mergedOptions
          .addDdlPseudoColumnsBuilder()
          .setName(column.getKey())
          .setType(typeProtoBuilder.build());
    }

    mergedOptions.clearTargetColumnTypes();
    for (Type type : targetColumnTypes) {
      TypeProto.Builder typeProtoBuilder = TypeProto.newBuilder();
      type.serialize(typeProtoBuilder, fileDescriptorSetsBuilder);
      mergedOptions.addTargetColumnTypes(typeProtoBuilder.build());
    }

    mergedOptions.setLanguageOptions(languageOptions.serialize());
    mergedOptions.setAllowedHintsAndOptions(
        allowedHintsAndOptions.serialize(fileDescriptorSetsBuilder));
    return mergedOptions.build();
  }

  public void setLanguageOptions(LanguageOptions languageOptions) {
    this.languageOptions = languageOptions;
  }

  public void setEnabledRewrites(Set<ResolvedASTRewrite> enabledRewrites) {
    builder.clearEnabledRewrites();
    for (ResolvedASTRewrite rewrite : enabledRewrites) {
      builder.addEnabledRewrites(rewrite);
    }
  }

  public void enableRewrite(ResolvedASTRewrite rewrite) {
    enableRewrite(rewrite, true);
  }

  public void enableRewrite(ResolvedASTRewrite rewrite, boolean enable) {
    boolean alreadyEnabled = rewriteEnabled(rewrite);
    if (enable && !alreadyEnabled) {
      builder.addEnabledRewrites(rewrite);
    } else if (!enable && alreadyEnabled) {
      Set<ResolvedASTRewrite> rewrites = new HashSet<>();
      for (ResolvedASTRewrite enabled : builder.getEnabledRewritesList()) {
        if (enabled != rewrite) {
          rewrites.add(enabled);
        }
      }
      setEnabledRewrites(rewrites);
    }
  }

  public boolean rewriteEnabled(ResolvedASTRewrite rewrite) {
    for (ResolvedASTRewrite enabled : builder.getEnabledRewritesList()) {
      if (enabled.equals(rewrite)) {
        return true;
      }
    }
    return false;
  }

  public LanguageOptions getLanguageOptions() {
    return languageOptions;
  }

  public void addSystemVariable(List<String> namePath, Type type) {
    systemVariables.put(ImmutableList.copyOf(namePath), type);
  }

  public void addQueryParameter(String name, Type type) {
    queryParameters.put(name, type);
  }

  public void addPositionalQueryParameter(Type type) {
    positionalQueryParameters.add(type);
  }

  public void addExpressionColumn(String name, Type type) {
    expressionColumns.put(name, type);
  }

  public Map<String, Type> getExpressionColumns() {
    return ImmutableMap.copyOf(expressionColumns);
  }

  public void addGraphProperty(String name, Type type) {
    graphProperties.put(name, type);
  }

  public Map<String, Type> getGraphProperties() {
    return ImmutableMap.copyOf(graphProperties);
  }

  public void addDdlPseudoColumn(String name, Type type) {
    ddlPseudoColumns.put(name, type);
  }

  public Map<String, Type> getDdlPseudoColumns() {
    return ImmutableMap.copyOf(ddlPseudoColumns);
  }

  public Map<String, Type> getQueryParameters() {
    return ImmutableMap.copyOf(queryParameters);
  }

  public List<Type> getPositionalQueryParameters() {
    return ImmutableList.copyOf(positionalQueryParameters);
  }

  public void addTargetColumnType(Type type) {
    targetColumnTypes.add(type);
  }

  public List<Type> getTargetColumnTypes() {
    return ImmutableList.copyOf(targetColumnTypes);
  }

  public void setInScopeExpressionColumn(String name, Type type) {
    Preconditions.checkState(inScopeExpressionColumn == null);
    inScopeExpressionColumn = new SimpleImmutableEntry<>(name, type);
    addExpressionColumn(name, type);
  }

  public boolean hasInScopeExpressionColumn() {
    return inScopeExpressionColumn != null;
  }

  public String getInScopeExpressionColumnName() {
    return inScopeExpressionColumn == null ? null : inScopeExpressionColumn.getKey();
  }

  public Type getInScopeExpressionColumnType() {
    return inScopeExpressionColumn == null ? null : inScopeExpressionColumn.getValue();
  }

  @SuppressWarnings("GoodTime") // should accept a java.time.ZoneId
  public void setDefaultTimezone(String timezone) {
    builder.setDefaultTimezone(timezone);
  }

  @SuppressWarnings("GoodTime") // should return a java.time.ZoneId
  public String getDefaultTimezone() {
    return builder.getDefaultTimezone();
  }

  public void setErrorMessageMode(ErrorMessageMode mode) {
    builder.setErrorMessageMode(mode);
  }

  public ErrorMessageMode getErrorMessageMode() {
    return builder.getErrorMessageMode();
  }

  public void setStatementContext(StatementContext context) {
    builder.setStatementContext(context);
  }

  public StatementContext getStatementContext() {
    return builder.getStatementContext();
  }

  /**
   * @deprecated Use {@link #setParseLocationRecordType} below for more control on what location to
   *     record.
   */
  @Deprecated
  public void setRecordParseLocations(boolean value) {
    builder.setParseLocationRecordType(
        value
            ? ParseLocationRecordType.PARSE_LOCATION_RECORD_CODE_SEARCH
            : ParseLocationRecordType.PARSE_LOCATION_RECORD_NONE);
  }

  /**
   * @deprecated Use {@link #getParseLocationRecordType} below for more control on what location to
   *     record.
   */
  @Deprecated
  public boolean getRecordParseLocations() {
    return builder.getParseLocationRecordType()
        != ParseLocationRecordType.PARSE_LOCATION_RECORD_NONE;
  }

  public void setParseLocationRecordType(ParseLocationRecordType value) {
    builder.setParseLocationRecordType(value);
  }

  public ParseLocationRecordType getParseLocationRecordType() {
    return builder.getParseLocationRecordType();
  }

  public void setCreateNewColumnForEachProjectedOutput(boolean value) {
    builder.setCreateNewColumnForEachProjectedOutput(value);
  }

  public boolean getCreateNewColumnForEachProjectedOutput() {
    return builder.getCreateNewColumnForEachProjectedOutput();
  }

  public void setPruneUnusedColumns(boolean value) {
    builder.setPruneUnusedColumns(value);
  }

  public boolean getPruneUnusedColumns() {
    return builder.getPruneUnusedColumns();
  }

  public void setAllowedHintsAndOptions(AllowedHintsAndOptions allowedHintsAndOptions) {
    this.allowedHintsAndOptions = Preconditions.checkNotNull(allowedHintsAndOptions);
  }

  public AllowedHintsAndOptions getAllowedHintsAndOptions() {
    return allowedHintsAndOptions;
  }

  public void setAllowUndeclaredParameters(boolean value) {
    builder.setAllowUndeclaredParameters(value);
  }

  public boolean getAllowUndeclaredParameters() {
    return builder.getAllowUndeclaredParameters();
  }

  public void setParameterMode(ParameterMode parameterMode) {
    builder.setParameterMode(parameterMode);
  }

  public ParameterMode getParameterMode() {
    return builder.getParameterMode();
  }

  public void setPreserveColumnAliases(boolean preserveColumnAliases) {
    builder.setPreserveColumnAliases(preserveColumnAliases);
  }

  public boolean getPreserveColumnAliases() {
    return builder.getPreserveColumnAliases();
  }

  public void setPreserveUnnecessaryCast(boolean preserveUnnecessaryCast) {
    builder.setPreserveUnnecessaryCast(preserveUnnecessaryCast);
  }

  public boolean getPreserveUnnecessaryCast() {
    return builder.getPreserveUnnecessaryCast();
  }

  public void setReplaceTableNotFoundErrorWithTvfErrorIfApplicable(
      boolean replaceTableNotFoundErrorWithTvfErrorIfApplicable) {
    builder.setReplaceTableNotFoundErrorWithTvfErrorIfApplicable(
        replaceTableNotFoundErrorWithTvfErrorIfApplicable);
  }

  public boolean getReplaceTableNotFoundErrorWithTvfErrorIfApplicable() {
    return builder.getReplaceTableNotFoundErrorWithTvfErrorIfApplicable();
  }

  public void setRewriteOptions(RewriteOptions rewriteOptions) {
    builder.setRewriteOptions(rewriteOptions);
  }

  public RewriteOptions getRewriteOptions() {
    return builder.getRewriteOptions();
  }

  public void setLogImpactOfLateralColumnReferences(boolean logImpactOfLateralColumnReferences) {
    builder.setLogImpactOfLateralColumnReferences(logImpactOfLateralColumnReferences);
  }

  public boolean getLogImpactOfLateralColumnReferences() {
    return builder.getLogImpactOfLateralColumnReferences();
  }

  static AnalyzerOptions deserialize(
      AnalyzerOptionsProto proto, List<? extends DescriptorPool> pools, TypeFactory factory) {
    AnalyzerOptions options = new AnalyzerOptions();
    options.deserializeFrom(proto, pools, factory);
    return options;
  }

  private void deserializeFrom(
      AnalyzerOptionsProto proto, List<? extends DescriptorPool> pools, TypeFactory factory) {
    setLanguageOptions(new LanguageOptions(proto.getLanguageOptions()));
    setDefaultTimezone(proto.getDefaultTimezone());
    setErrorMessageMode(proto.getErrorMessageMode());
    setStatementContext(proto.getStatementContext());
    setPruneUnusedColumns(proto.getPruneUnusedColumns());
    setParseLocationRecordType(proto.getParseLocationRecordType());
    setCreateNewColumnForEachProjectedOutput(proto.getCreateNewColumnForEachProjectedOutput());
    setAllowUndeclaredParameters(proto.getAllowUndeclaredParameters());
    setParameterMode(proto.getParameterMode());
    setPreserveColumnAliases(proto.getPreserveColumnAliases());
    setPreserveUnnecessaryCast(proto.getPreserveUnnecessaryCast());
    setReplaceTableNotFoundErrorWithTvfErrorIfApplicable(
        proto.getReplaceTableNotFoundErrorWithTvfErrorIfApplicable());

    if (proto.hasInScopeExpressionColumn()) {
      setInScopeExpressionColumn(
          proto.getInScopeExpressionColumn().getName(),
          factory.deserialize(proto.getInScopeExpressionColumn().getType(), pools));
    }

    for (QueryParameterProto param : proto.getQueryParametersList()) {
      addQueryParameter(param.getName(), factory.deserialize(param.getType(), pools));
    }

    for (SystemVariableProto systemVariable : proto.getSystemVariablesList()) {
      addSystemVariable(
          systemVariable.getNamePathList(), factory.deserialize(systemVariable.getType(), pools));
    }

    for (TypeProto type : proto.getPositionalQueryParametersList()) {
      addPositionalQueryParameter(factory.deserialize(type, pools));
    }

    for (QueryParameterProto column : proto.getExpressionColumnsList()) {
      if (!column.getName().equals(getInScopeExpressionColumnName())) {
        addExpressionColumn(column.getName(), factory.deserialize(column.getType(), pools));
      }
    }

    for (QueryParameterProto graphProperty : proto.getGraphPropertiesList()) {
      addGraphProperty(
          graphProperty.getName(), factory.deserialize(graphProperty.getType(), pools));
    }

    for (QueryParameterProto column : proto.getDdlPseudoColumnsList()) {
      addDdlPseudoColumn(column.getName(), factory.deserialize(column.getType(), pools));
    }

    for (TypeProto type : proto.getTargetColumnTypesList()) {
      addTargetColumnType(factory.deserialize(type, pools));
    }

    if (proto.hasAllowedHintsAndOptions()) {
      setAllowedHintsAndOptions(
          AllowedHintsAndOptions.deserialize(proto.getAllowedHintsAndOptions(), pools, factory));
    }

    builder.clearEnabledRewrites();
    for (ResolvedASTRewrite rewrite : proto.getEnabledRewritesList()) {
      enableRewrite(rewrite);
    }

    if (proto.hasRewriteOptions()) {
      setRewriteOptions(proto.getRewriteOptions());
    }

    if (proto.hasLogImpactOfLateralColumnReferences()) {
      setLogImpactOfLateralColumnReferences(proto.getLogImpactOfLateralColumnReferences());
    }
  }

  private void writeObject(java.io.ObjectOutputStream out) throws IOException {
    out.defaultWriteObject();
    builder.build().writeTo(out);
  }

  private void readObject(java.io.ObjectInputStream in) throws IOException, ClassNotFoundException {
    in.defaultReadObject();
    this.builder = AnalyzerOptionsProto.newBuilder().mergeFrom(in);
  }
}
