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

import com.google.errorprone.annotations.CanIgnoreReturnValue;
import com.google.googlesql.LocalService.AnalyzeRequest;
import com.google.googlesql.LocalService.AnalyzeResponse;
import com.google.googlesql.LocalService.BuildSqlRequest;
import com.google.googlesql.LocalService.BuildSqlResponse;
import com.google.googlesql.LocalService.ExtractTableNamesFromNextStatementRequest;
import com.google.googlesql.LocalService.ExtractTableNamesFromNextStatementResponse;
import com.google.googlesql.LocalService.ExtractTableNamesFromStatementRequest;
import com.google.googlesql.LocalService.ExtractTableNamesFromStatementResponse;
import com.google.googlesql.resolvedast.ResolvedNodes.ResolvedExpr;
import com.google.googlesql.resolvedast.ResolvedNodes.ResolvedStatement;
import io.grpc.StatusRuntimeException;
import java.io.Serializable;
import java.util.ArrayList;
import java.util.LinkedHashMap;
import java.util.List;
import java.util.Map;

/** The Analyzer class provides static methods to analyze GoogleSQL statements or expressions. */
public class Analyzer implements Serializable {

  final SimpleCatalog catalog;
  final AnalyzerOptions options;

  public Analyzer(AnalyzerOptions options, SimpleCatalog catalog) {
    this.options = options;
    this.catalog = catalog;
  }

  @CanIgnoreReturnValue // TODO: consider removing this?
  public ResolvedStatement analyzeStatement(String sql) {
    return analyzeStatement(sql, options, catalog);
  }

  @CanIgnoreReturnValue // TODO: consider removing this?
  public static ResolvedStatement analyzeStatement(
      String sql, AnalyzerOptions options, SimpleCatalog catalog) {
    AnalyzeRequest.Builder request = AnalyzeRequest.newBuilder().setSqlStatement(sql);

    FileDescriptorSetsBuilder fileDescriptorSetsBuilder =
        AnalyzerHelper.serializeSimpleCatalog(catalog, options, request);

    AnalyzeResponse response;
    try {
      response = Client.getStub().analyze(request.build());
    } catch (StatusRuntimeException e) {
      throw new SqlException(e);
    }

    return AnalyzerHelper.deserializeResolvedStatement(
        catalog, fileDescriptorSetsBuilder, response);
  }

  public static ResolvedExpr analyzeExpression(
      String expression, AnalyzerOptions options, SimpleCatalog catalog) {
    AnalyzeRequest.Builder request = AnalyzeRequest.newBuilder().setSqlExpression(expression);

    FileDescriptorSetsBuilder fileDescriptorSetsBuilder =
        AnalyzerHelper.serializeSimpleCatalog(catalog, options, request);

    AnalyzeResponse response;
    try {
      response = Client.getStub().analyze(request.build());
    } catch (StatusRuntimeException e) {
      throw new SqlException(e);
    }

    return AnalyzerHelper.deserializeResolvedExpression(
        catalog, fileDescriptorSetsBuilder, response);
  }

  /**
   * Renders expression as a sql string.
   *
   * <p>Note, there is a bug that prevents DatePart enum from serializing correctly when @arg
   * catalog is registered.
   *
   * <p>TODO: fix DatePart serializing in registered catalogs.
   */
  public static String buildExpression(ResolvedExpr expression, SimpleCatalog catalog) {

    Map<DescriptorPool, Long> registeredDescriptorPoolIds = new LinkedHashMap<>();
    BuildSqlRequest.Builder request = BuildSqlRequest.newBuilder();
    FileDescriptorSetsBuilder fileDescriptorSetsBuilder =
        AnalyzerHelper.serializeSimpleCatalog(catalog, request, registeredDescriptorPoolIds);
    AnyResolvedExprProto.Builder resolvedExpr = AnyResolvedExprProto.newBuilder();
    expression.serialize(fileDescriptorSetsBuilder, resolvedExpr);
    request.setResolvedExpression(resolvedExpr.build());
    request.setDescriptorPoolList(
        DescriptorPoolSerializer.createDescriptorPoolListWithRegisteredIds(
            fileDescriptorSetsBuilder, registeredDescriptorPoolIds));

    BuildSqlResponse response;
    try {
      response = Client.getStub().buildSql(request.build());
    } catch (StatusRuntimeException e) {
      throw new SqlException(e);
    }

    return response.getSql();
  }

  /**
   * Renders statement as a sql string.
   */
  public static String buildStatement(ResolvedStatement statement, SimpleCatalog catalog) {
    BuildSqlRequest.Builder request = BuildSqlRequest.newBuilder();
    Map<DescriptorPool, Long> registeredDescriptorPoolIds = new LinkedHashMap<>();
    FileDescriptorSetsBuilder fileDescriptorSetsBuilder =
        AnalyzerHelper.serializeSimpleCatalog(catalog, request, registeredDescriptorPoolIds);
    AnyResolvedStatementProto.Builder resolvedStatement = AnyResolvedStatementProto.newBuilder();
    statement.serialize(fileDescriptorSetsBuilder, resolvedStatement);
    request.setResolvedStatement(resolvedStatement.build());
    request.setDescriptorPoolList(
        DescriptorPoolSerializer.createDescriptorPoolListWithRegisteredIds(
            fileDescriptorSetsBuilder, registeredDescriptorPoolIds));

    BuildSqlResponse response;
    try {
      response = Client.getStub().buildSql(request.build());
    } catch (StatusRuntimeException e) {
      throw new SqlException(e);
    }

    return response.getSql();
  }

  /**
   * Analyze the statement syntax and extract the table names. This function is a one-of which does
   * not require an instance of analyzer.
   *
   * @return List of table names. Every table name is a list of string segments, to retain original
   *     naming structure.
   */
  public static List<List<String>> extractTableNamesFromStatement(
      String sql, AnalyzerOptions options) {
    return extractTableNamesFromStatementInternal(sql, options, /*allowScript=*/ false);
  }

  @CanIgnoreReturnValue // TODO: consider removing this?
  public static List<List<String>> extractTableNamesFromStatement(String sql) {
    return extractTableNamesFromStatement(sql, new AnalyzerOptions());
  }

  public static List<List<String>> extractTableNamesFromScript(
      String sql, AnalyzerOptions options) {
    return extractTableNamesFromStatementInternal(sql, options, /*allowScript=*/ true);
  }

  private static List<List<String>> extractTableNamesFromStatementInternal(
      String sql, AnalyzerOptions options, boolean allowScript) {
    ExtractTableNamesFromStatementRequest request =
        ExtractTableNamesFromStatementRequest.newBuilder()
            .setSqlStatement(sql)
            .setOptions(options.getLanguageOptions().serialize())
            .setAllowScript(allowScript)
            .build();

    ExtractTableNamesFromStatementResponse response;
    try {
      response = Client.getStub().extractTableNamesFromStatement(request);
    } catch (StatusRuntimeException e) {
      throw new SqlException(e);
    }

    ArrayList<List<String>> result = new ArrayList<>(response.getTableNameCount());
    for (ExtractTableNamesFromStatementResponse.TableName name : response.getTableNameList()) {
      ArrayList<String> nameList = new ArrayList<>(name.getTableNameSegmentCount());
      nameList.addAll(name.getTableNameSegmentList());
      result.add(nameList);
    }
    return result;
  }

  public ResolvedStatement analyzeNextStatement(ParseResumeLocation parseResumeLocation) {
    return analyzeNextStatement(parseResumeLocation, options, catalog);
  }

  /**
   * Analyze the next statement in parseResumeLocation, updating its bytePosition to the end of the
   * statement.
   */
  public static ResolvedStatement analyzeNextStatement(
      ParseResumeLocation parseResumeLocation, AnalyzerOptions options, SimpleCatalog catalog) {
    FileDescriptorSetsBuilder fileDescriptorSetsBuilder;
    AnalyzeResponse response;
    synchronized (parseResumeLocation) {
      AnalyzeRequest.Builder request =
          AnalyzeRequest.newBuilder().setParseResumeLocation(parseResumeLocation.serialize());

      fileDescriptorSetsBuilder = AnalyzerHelper.serializeSimpleCatalog(catalog, options, request);

      try {
        response = Client.getStub().analyze(request.build());
      } catch (StatusRuntimeException e) {
        throw new SqlException(e);
      }

      parseResumeLocation.setBytePosition(response.getResumeBytePosition());
    }
    return AnalyzerHelper.deserializeResolvedStatement(
        catalog, fileDescriptorSetsBuilder, response);
  }

  /**
   * Extract the table names from the next statement in parseResumeLocation. This function is a
   * one-off which does not require an analyzer instance.
   *
   * @return List of table names. Every table name is a list of string segments, to retain the
   *     original naming structure.
   */
  public static List<List<String>> extractTableNamesFromNextStatement(
      ParseResumeLocation parseResumeLocation, AnalyzerOptions options) {
    ExtractTableNamesFromNextStatementResponse response;
    synchronized (parseResumeLocation) {
      ExtractTableNamesFromNextStatementRequest request =
          ExtractTableNamesFromNextStatementRequest.newBuilder()
              .setParseResumeLocation(parseResumeLocation.serialize())
              .setOptions(options.getLanguageOptions().serialize())
              .build();

      try {
        response = Client.getStub().extractTableNamesFromNextStatement(request);
      } catch (StatusRuntimeException e) {
        throw new SqlException(e);
      }

      parseResumeLocation.setBytePosition(response.getResumeBytePosition());
    }

    ArrayList<List<String>> result = new ArrayList<>(response.getTableNameCount());
    for (ExtractTableNamesFromNextStatementResponse.TableName name : response.getTableNameList()) {
      ArrayList<String> nameList = new ArrayList<>(name.getTableNameSegmentCount());
      nameList.addAll(name.getTableNameSegmentList());
      result.add(nameList);
    }
    return result;
  }
}
