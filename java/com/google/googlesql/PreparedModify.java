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

import static com.google.googlesql.Parameter.normalize;
import static com.google.googlesql.Parameter.normalizeAndValidate;
import static com.google.googlesql.Parameter.serialize;

import com.google.common.base.Preconditions;
import com.google.common.collect.ImmutableList;
import com.google.common.collect.ImmutableMap;
import com.google.errorprone.annotations.CanIgnoreReturnValue;
import com.google.errorprone.annotations.CheckReturnValue;
import com.google.googlesql.LocalService.EvaluateModifyRequest;
import com.google.googlesql.LocalService.EvaluateModifyResponse;
import com.google.googlesql.LocalService.PrepareModifyRequest;
import com.google.googlesql.LocalService.PrepareModifyResponse;
import com.google.googlesql.LocalService.PreparedModifyState;
import com.google.googlesql.LocalService.UnprepareModifyRequest;
import io.grpc.StatusRuntimeException;
import java.util.Map;

/**
 * GoogleSQL modify statement evaluation using Service RPC.
 *
 * <p>A prepared modify will create server side state, which should be released by calling close()
 * when the statement is no longer used. Note that the close() method is called in finalize(), but
 * you should not rely on that because the Java garbage collector has no idea how much memory is
 * used on C++ side and GC may happen later than necessary.
 *
 * <p>Read the unit tests for examples.
 *
 * <p>This class is thread-safe.
 */
@CheckReturnValue
public final class PreparedModify implements AutoCloseable {

  private boolean closed = false;

  private final long preparedModifyId;
  // expected are the fields we expect the user to provide to execute, but not all are required.
  private final ImmutableMap<String, Type> expectedParameters;
  // referenced is the subset of expected that the analyzer determined are actually required.
  private final ImmutableList<String> referencedParameters;
  private final SimpleCatalog catalog;

  private PreparedModify(
      long preparedModifyId,
      ImmutableMap<String, Type> expectedParameters,
      ImmutableList<String> referencedParameters,
      SimpleCatalog catalog) {
    this.preparedModifyId = preparedModifyId;
    this.expectedParameters = expectedParameters;
    this.referencedParameters = referencedParameters;
    this.catalog = catalog;
  }

  /**
   * Get the list of parameters referenced in this statement.
   *
   * <p>The parameters will be returned in lower case, as parameters are case-insensitive. The list
   * of parameters returned from this method are the minimal set that must be provided to execute().
   */
  public ImmutableList<String> getReferencedParameters() {
    return referencedParameters;
  }

  /**
   * Evaluate the SQL statement via Service RPC.
   *
   * <p>NullPointerException is being thrown if parameters is null
   *
   * <p>IllegalStatementException is being thrown if the prepare modify is closed.
   *
   * @param parameters Map of parameter name:value pairs used in the SQL expression.
   * @return The result in the form of a table content.
   */
  public EvaluatorTableModifyResponse execute(Map<String, Value> parameters) {
    Preconditions.checkNotNull(parameters);
    Preconditions.checkState(!closed);

    EvaluateModifyRequest request = buildEvaluateRequest(parameters);
    try {
      final EvaluateModifyResponse response = Client.getStub().evaluateModify(request);
      return EvaluatorTableModifyResponse.deserialize(this.catalog, response);
    } catch (StatusRuntimeException e) {
      throw new SqlException(e);
    }
  }

  private EvaluateModifyRequest buildEvaluateRequest(Map<String, Value> parameters) {
    EvaluateModifyRequest.Builder requestBuilder = EvaluateModifyRequest.newBuilder();
    requestBuilder.setPreparedModifyId(preparedModifyId);

    final ImmutableMap<String, Value> normalizedParameters =
        normalizeAndValidate(parameters, expectedParameters, "query");
    for (String param : referencedParameters) {
      Value value = normalizedParameters.get(param);
      if (value == null) {
        throw new SqlException("Incomplete sql expression parameters " + param);
      }
      requestBuilder.addParams(serialize(param, value));
    }

    return requestBuilder.build();
  }

  /** Release the server side state for this prepared modify statement. */
  @Override
  public void close() {
    if (closed) {
      return;
    }

    try {
      UnprepareModifyRequest request =
          UnprepareModifyRequest.newBuilder().setPreparedModifyId(preparedModifyId).build();
      Client.getStub().unprepareModify(request);
    } catch (StatusRuntimeException e) {
      // ignore
    }

    closed = true;
  }

  @Override
  protected void finalize() throws Throwable {
    super.finalize();
    close();
  }

  /** Get a new builder. */
  public static Builder builder() {
    return new PreparedModify.Builder();
  }

  /**
   * A builder for creating immutable PreparedModify instances. Example:
   *
   * <pre>{@code
   * public final PreparedModify modify =
   *     PreparedModify.builder()
   *         .setSql(sqlString)
   *         .setAnalyzerOptions(options)
   *         .setCatalog(catalog)
   *         .prepare();
   *
   * public final PreparedModify modifyWithTablesContents =
   *     PreparedModify.builder()
   *         .setSql(sqlString)
   *         .setAnalyzerOptions(options)
   *         .setCatalog(catalog)
   *         .setTablesContents(tablesContents)
   *         .prepare();
   * }</pre>
   *
   * <p>Builder instances can be reused; it is safe to call {@link #prepare} multiple times to
   * create multiple instances of PreparedModify. Every time {@link #prepare} is called a new modify
   * is registered with the local service and a new instance is returned.
   */
  public static final class Builder {

    private String sql;
    private AnalyzerOptions options;
    private SimpleCatalog catalog;
    private Map<String, TableContent> tablesContents;

    private Builder() {}

    /**
     * Set the string SQL statement.
     *
     * <p>Mandatory.
     */
    @CanIgnoreReturnValue
    public Builder setSql(String sql) {
      this.sql = sql;
      return this;
    }

    /**
     * Set the options to customize the SQL statement's analyzer behavior.
     *
     * <p>Mandatory.
     */
    @CanIgnoreReturnValue
    public Builder setAnalyzerOptions(AnalyzerOptions options) {
      this.options = options;
      return this;
    }

    /**
     * Set the catalog with which this SQL statement interacts.
     *
     * <p>Mandatory.
     */
    @CanIgnoreReturnValue
    public Builder setCatalog(SimpleCatalog catalog) {
      this.catalog = catalog;
      return this;
    }

    /**
     * Set the content for the tables within the catalog.
     *
     * <p>Optional.
     *
     * <p>It can be used only together with a catalog that is not registered.
     */
    @CanIgnoreReturnValue
    public Builder setTablesContents(Map<String, TableContent> tablesContents) {
      this.tablesContents = tablesContents;
      return this;
    }

    /**
     * Prepare the SQL statement and return a new instance of PreparedModify. Throwing SqlException
     * if there's an error (not necessarily network/server failure).
     *
     * <p>IllegalStateException is being thrown if the SQL string is null
     *
     * <p>IllegalStateException is being thrown if the options is null
     *
     * <p>IllegalStateException is being thrown if the catalog is null
     *
     * <p>IllegalStateException is being thrown if the provided catalog is a registered catalog and
     * tables contents were provided. At the moment changing the contents of the tables of a
     * registered catalog is not supported yet.
     */
    public PreparedModify prepare() {
      Preconditions.checkState(sql != null, "SQL string must not be null");
      Preconditions.checkState(options != null, "AnalyzerOptions must not be null");
      Preconditions.checkState(catalog != null, "Catalog must not be null");
      Preconditions.checkState(
          tablesContents == null || !catalog.isRegistered(),
          "Can only provide tables contents with a catalog that has not been registered");

      final PrepareModifyRequest request = buildPrepareRequest();
      try {
        final PrepareModifyResponse response = Client.getStub().prepareModify(request);
        return processResponse(response.getPrepared());
      } catch (StatusRuntimeException e) {
        throw new SqlException(e);
      }
    }

    private PrepareModifyRequest buildPrepareRequest() {
      FileDescriptorSetsBuilder fileDescriptorSetsBuilder = new FileDescriptorSetsBuilder();
      fileDescriptorSetsBuilder.addAllFileDescriptors(BuiltinDescriptorPool.getInstance());

      PrepareModifyRequest.Builder builder = PrepareModifyRequest.newBuilder();
      builder.setSql(sql);
      builder.setOptions(options.serialize(fileDescriptorSetsBuilder));

      Map<DescriptorPool, Long> catalogRegisteredDescriptorPoolIds = ImmutableMap.of();
      if (catalog.isRegistered()) {
        catalogRegisteredDescriptorPoolIds = catalog.getRegisteredDescriptorPoolIds();
        for (DescriptorPool pool : catalogRegisteredDescriptorPoolIds.keySet()) {
          fileDescriptorSetsBuilder.addAllFileDescriptors(pool);
        }
        builder.setRegisteredCatalogId(catalog.getRegisteredId());
      } else {
        builder.setSimpleCatalog(catalog.serialize(fileDescriptorSetsBuilder));
        if (tablesContents != null) {
          tablesContents.forEach((t, c) -> builder.putTableContent(t, c.serialize()));
        }
      }

      builder.setDescriptorPoolList(
          DescriptorPoolSerializer.createDescriptorPoolListWithRegisteredIds(
              fileDescriptorSetsBuilder, catalogRegisteredDescriptorPoolIds));

      return builder.build();
    }

    private PreparedModify processResponse(PreparedModifyState preparedModifyState) {
      long preparedModifyId = preparedModifyState.getPreparedModifyId();

      ImmutableMap<String, Type> expectedParameters = normalize(options.getQueryParameters());
      ImmutableList<String> referencedParameters =
          ImmutableList.copyOf(preparedModifyState.getReferencedParametersList());

      return new PreparedModify(
          preparedModifyId, expectedParameters, referencedParameters, catalog);
    }
  }
}
