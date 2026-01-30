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

import static com.google.common.base.Verify.verify;

import com.google.common.base.Ascii;
import com.google.common.base.Preconditions;
import com.google.common.collect.ImmutableMap;
import com.google.common.collect.Queues;
import com.google.common.util.concurrent.ListenableFuture;
import com.google.common.util.concurrent.SettableFuture;
import com.google.errorprone.annotations.CanIgnoreReturnValue;
import com.google.googlesql.LocalService.EvaluateRequest;
import com.google.googlesql.LocalService.EvaluateRequestBatch;
import com.google.googlesql.LocalService.EvaluateResponse;
import com.google.googlesql.LocalService.EvaluateResponseBatch;
import com.google.googlesql.LocalService.PrepareRequest;
import com.google.googlesql.LocalService.PrepareResponse;
import com.google.googlesql.LocalService.PreparedState;
import com.google.googlesql.LocalService.UnprepareRequest;
import io.grpc.Channel;
import io.grpc.ManagedChannel;
import io.grpc.StatusRuntimeException;
import io.grpc.stub.ClientCallStreamObserver;
import io.grpc.stub.ClientResponseObserver;
import java.util.Collections;
import java.util.HashMap;
import java.util.List;
import java.util.Map;
import java.util.Map.Entry;
import java.util.Optional;
import java.util.Queue;
import java.util.concurrent.atomic.AtomicLong;

/**
 * GoogleSQL expression evaluation using Service RPC.
 *
 * <p>When evaluating an expression, callers can provide
 *
 * <ul>
 *   <li>A set of expression columns - column names usable in the expression.
 *   <li>A set of parameters - parameters usable in the expression as \@param.
 * </ul>
 *
 * <p>Columns / Parameters are passed as String:Value Maps.
 *
 * <p>A prepared expression will create server side state, which should be released by calling
 * close() when the expression is no longer used. Note that the close() method is called in
 * finalize(), but you should not rely on that because the Java garbage collector has no idea how
 * much memory is used on C++ side and GC may happen later than necessary.
 *
 * <p>Read the unit tests for examples.
 *
 * <p>This class is not thread-safe. External synchronization is needed when it is shared by
 * multiple threads.
 */
public class PreparedExpression implements AutoCloseable {
  private final String sql;
  private boolean prepared = false;
  private boolean closed = false;
  private Type outputType;
  private long preparedId;
  private FileDescriptorSetsBuilder fileDescriptorSetsBuilder;
  private TypeFactory factory = TypeFactory.nonUniqueNames();
  private AnalyzerOptions options;

  // expected are the fields we expect the user to provide to execute, but not all are required.
  private Map<String, Type> expectedColumns;
  private Map<String, Type> expectedParameters;

  // referenced is the subset of expected that the analyzer determined are actually required.
  private List<String> referencedColumns;
  private List<String> referencedParameters;

  public PreparedExpression(String sql) {
    this.sql = sql;
  }

  /**
   * Prepare the expression with given options. Throwing SqlException if there's an error (not
   * necessarily network/server failure). See unit tests for examples how options can be set.
   *
   * @param options
   */
  public void prepare(AnalyzerOptions options) {
    prepareInternal(options, Optional.empty());
  }

  public void prepareWithCatalog(AnalyzerOptions options, SimpleCatalog catalog) {
    Preconditions.checkNotNull(catalog);
    prepareInternal(options, Optional.of(catalog));
  }

  private void prepareInternal(AnalyzerOptions options, Optional<SimpleCatalog> maybeCatalog) {
    Preconditions.checkState(!prepared);
    Preconditions.checkState(!closed);
    this.options = options;

    fileDescriptorSetsBuilder = new FileDescriptorSetsBuilder();
    fileDescriptorSetsBuilder.addAllFileDescriptors(BuiltinDescriptorPool.getInstance());

    PrepareRequest.Builder request = PrepareRequest.newBuilder();
    request.setSql(sql);
    request.setOptions(options.serialize(fileDescriptorSetsBuilder));
    Map<DescriptorPool, Long> catalogRegisteredDescriptorPoolIds = Collections.emptyMap();
    if (maybeCatalog.isPresent()) {
      SimpleCatalog catalog = maybeCatalog.get();
      if (catalog.isRegistered()) {
        catalogRegisteredDescriptorPoolIds = catalog.getRegisteredDescriptorPoolIds();
        for (DescriptorPool pool : catalogRegisteredDescriptorPoolIds.keySet()) {
          fileDescriptorSetsBuilder.addAllFileDescriptors(pool);
        }
        request.setRegisteredCatalogId(catalog.getRegisteredId());
      } else {
        request.setSimpleCatalog(catalog.serialize(fileDescriptorSetsBuilder));
      }
    } else {
      // A catalog must be provided, so we request a default initialized catalog with all of
      // the builtin functions.
      request
          .getSimpleCatalogBuilder()
          .getBuiltinFunctionOptionsBuilder()
          .setLanguageOptions(request.getOptions().getLanguageOptions());
    }
    request.setDescriptorPoolList(
        DescriptorPoolSerializer.createDescriptorPoolListWithRegisteredIds(
            fileDescriptorSetsBuilder, catalogRegisteredDescriptorPoolIds));
    PrepareResponse resp;
    try {
      resp = Client.getStub().prepare(request.build());
    } catch (StatusRuntimeException e) {
      throw new SqlException(e);
    }

    setPrepared(resp.getPrepared());
  }

  private void setPrepared(PreparedState resp) {
    preparedId = resp.getPreparedExpressionId();
    outputType =
        factory.deserialize(resp.getOutputType(), fileDescriptorSetsBuilder.getDescriptorPools());

    expectedColumns = toLower(options.getExpressionColumns());
    expectedParameters = toLower(options.getQueryParameters());

    referencedColumns = resp.getReferencedColumnsList();
    referencedParameters = resp.getReferencedParametersList();

    prepared = true;
  }

  /** Get the output type of this expression. */
  public Type getOutputType() {
    Preconditions.checkState(prepared);
    Preconditions.checkState(!closed);
    return outputType;
  }

  /**
   * Get the list of column names referenced in this expression.
   *
   * <p>The columns will be returned in lower case, as column expressions are case-insensitive when
   * evaluated. This can be used for efficiency, the list of columns returned from this method are
   * the minimal set that must be provided to execute().
   */
  public List<String> getReferencedColumns() {
    Preconditions.checkState(prepared);
    Preconditions.checkState(!closed);
    return referencedColumns;
  }

  /**
   * Get the list of parameters referenced in this expression.
   *
   * <p>This is similar to getReferencedColumns(), but for parameters.
   */
  public List<String> getReferencedParameters() {
    Preconditions.checkState(prepared);
    Preconditions.checkState(!closed);
    return referencedParameters;
  }

  /**
   * Evaluate the sql expression via Service RPC.
   *
   * @return The evaluation result.
   */
  @CanIgnoreReturnValue // TODO: consider removing this?
  public Value execute() {
    return execute(Collections.<String, Value>emptyMap(), Collections.<String, Value>emptyMap());
  }

  /**
   * Evaluate the sql expression via Service RPC.
   *
   * @param columns Map of column name:value pairs used in the sql expression.
   * @param parameters Map of parameter name:value pairs.
   * @return The evaluation result.
   */
  @CanIgnoreReturnValue // TODO: consider removing this?
  public Value execute(Map<String, Value> columns, Map<String, Value> parameters) {
    return execute(buildRequest(columns, parameters));
  }

  private Value execute(EvaluateRequest request) {
    Preconditions.checkState(!closed);

    final EvaluateResponse resp;
    try {
      resp = Client.getStub().evaluate(request);
    } catch (StatusRuntimeException e) {
      throw new SqlException(e);
    }

    if (!prepared) {
      setPrepared(resp.getPrepared());
    }
    return Value.deserialize(outputType, resp.getValue());
  }

  private EvaluateRequest buildRequest(Map<String, Value> columns, Map<String, Value> parameters) {
    Preconditions.checkNotNull(columns);
    Preconditions.checkNotNull(parameters);
    EvaluateRequest.Builder request = EvaluateRequest.newBuilder();
    if (prepared) {
      request.setPreparedExpressionId(preparedId);
      final Map<String, Value> normalizedColumns =
          normalizeParameters(columns, expectedColumns, "column");
      final Map<String, Value> normalizedParameters =
          normalizeParameters(parameters, expectedParameters, "query");

      for (String column : referencedColumns) {
        Value value = normalizedColumns.get(column);
        if (value == null) {
          throw new SqlException("Incomplete column parameters " + column);
        }
        request.addColumns(serializeParameter(column, value));
      }
      for (String param : referencedParameters) {
        Value value = normalizedParameters.get(param);
        if (value == null) {
          throw new SqlException("Incomplete query parameters " + param);
        }
        request.addParams(serializeParameter(param, value));
      }
      // TODO: Remove once new descriptor pool is the default
      // Force adding this field, as empty to ensure we use the 'new' local_service
      // descriptor pool codepath.  It is not really needed, as all of the descriptor
      // pools are already synchronized via prepare.
      request.getDescriptorPoolListBuilder();
    } else {
      request.setSql(sql);
      fileDescriptorSetsBuilder = new FileDescriptorSetsBuilder();
      fileDescriptorSetsBuilder.addAllFileDescriptors(BuiltinDescriptorPool.getInstance());
      ImmutableMap<DescriptorPool, Long> registeredDescriptorPoolIds = ImmutableMap.of();
      options = new AnalyzerOptions();

      for (Entry<String, Value> column : columns.entrySet()) {
        options.addExpressionColumn(column.getKey(), column.getValue().getType());
        request.addColumns(serializeParameter(column.getKey(), column.getValue()));
      }
      for (Entry<String, Value> param : parameters.entrySet()) {
        options.addQueryParameter(param.getKey(), param.getValue().getType());
        request.addParams(serializeParameter(param.getKey(), param.getValue()));
      }

      request.setOptions(options.serialize(fileDescriptorSetsBuilder));
      request.setDescriptorPoolList(
          DescriptorPoolSerializer.createDescriptorPoolListWithRegisteredIds(
              fileDescriptorSetsBuilder, registeredDescriptorPoolIds));
    }
    return request.build();
  }

  private static Map<String, Value> normalizeParameters(
      Map<String, Value> parameters, Map<String, Type> expected, String kind) {
    HashMap<String, Value> lower = new HashMap<>();
    for (Map.Entry<String, Value> entry : parameters.entrySet()) {
      String name = Ascii.toLowerCase(entry.getKey());
      if (!expected.containsKey(name)) {
        throw new SqlException("Unexpected " + kind + " parameter '" + name + "'");
      }
      Value value = entry.getValue();
      Type type = expected.get(name);
      if (!type.equals(value.getType())) {
        throw new SqlException(
            "Expected " + kind + " parameter '" + name + "' to be of type " + type);
      }
      if (lower.putIfAbsent(name, value) != null) {
        throw new SqlException("Duplicate expression " + kind + " name '" + name + "'");
      }
    }
    return lower;
  }

  private static ImmutableMap<String, Type> toLower(Map<String, Type> parameters) {
    ImmutableMap.Builder<String, Type> b = new ImmutableMap.Builder<>();
    for (Map.Entry<String, Type> entry : parameters.entrySet()) {
      b.put(Ascii.toLowerCase(entry.getKey()), entry.getValue());
    }
    return b.buildOrThrow();
  }


  /** Opens a handle for streaming execution */
  public Stream stream() {
    Preconditions.checkState(prepared);
    Preconditions.checkState(!closed);
    return new Stream();
  }

  /**
   * GoogleSQL streaming expression evaluation.
   *
   * <p>Streaming evaluation requests are buffered until there is either no outstanding work or an
   * optimal number of pending requests (tuned by benchmarks) to send as a single batch. This
   * minimizes the average latency, but increases deviation in request latency.
   *
   * <p>This class is not thread-safe. External synchronization is needed when it is shared by
   * multiple threads.
   */
  public final class Stream implements AutoCloseable {
    private final Queue<SettableFuture<Value>> pending;

    private final Queue<EvaluateRequest> batch;
    private final AtomicLong batchCount; // eventually consistent
    private final AtomicLong batchSerializedSize; // eventually consistent

    private final AtomicLong outstandingCount;

    private final Channel channel;
    private final ClientCallStreamObserver<EvaluateRequestBatch> requestObserver;

    private Stream() {
      pending = Queues.newConcurrentLinkedQueue();

      batch = Queues.newConcurrentLinkedQueue();
      batchCount = new AtomicLong();
      batchSerializedSize = new AtomicLong();

      outstandingCount = new AtomicLong();

      channel = ClientChannelProvider.loadChannel();
      GoogleSqlLocalServiceGrpc.GoogleSqlLocalServiceStub stub =
          GoogleSqlLocalServiceGrpc.newStub(channel);
      requestObserver =
          (ClientCallStreamObserver<EvaluateRequestBatch>)
              stub.evaluateStream(new ResponseObserver());
    }

    /**
     * Evaluate the sql expression via streaming RPC.
     *
     * <p>This method is not thread-safe. External synchronization is needed when it is shared by
     * multiple threads.
     *
     * @param columns Map of column name:value pairs used in the sql expression.
     * @param parameters Map of parameter name:value pairs.
     * @return The evaluation result.
     */
    public ListenableFuture<Value> execute(
        Map<String, Value> columns, Map<String, Value> parameters) {
      final SettableFuture<Value> f = SettableFuture.create();
      final EvaluateRequest r = buildRequest(columns, parameters);
      final int size = r.getSerializedSize();

      /* critical block: This block is not thread safe, as documented in the
       * PreparedExpression class comment, for maximal performance. The order
       * of these operations is critical to the correctness of this code.
       */
      pending.add(f);
      batch.add(r);
      batchCount.getAndIncrement();
      batchSerializedSize.getAndAdd(size);
      // end critical block

      maybeFlush();

      return f;
    }

    private void maybeFlush() {
      /* batchCount and batchSerializedSize are eventually consistent hints.
       * They may be too low if called concurrently with execute, which will
       * be mitigated when execute calls this method. They may be too high
       * if called concurrently with flush, which will result in an unneeded
       * call to flush.
       */
      final long batchCount = this.batchCount.get();
      if (batchCount <= 0) {
        return;
      }

      if (outstandingCount.get() != 0
          && batchSerializedSize.get() < 65536
          && (batchCount < 16 || !requestObserver.isReady())) {
        return;
      }

      flush();
    }

    /** Flush buffered requests to RPC. */
    public synchronized void flush() {
      try {
        final EvaluateRequestBatch.Builder b = EvaluateRequestBatch.newBuilder();
        for (EvaluateRequest r; (r = batch.poll()) != null; ) {
          batchSerializedSize.getAndAdd(-r.getSerializedSize());
          b.addRequest(r);
        }
        if (b.getRequestCount() > 0) {
          batchCount.getAndAdd(-b.getRequestCount());
          outstandingCount.getAndAdd(b.getRequestCount());
          requestObserver.onNext(b.build());
        }
      } catch (RuntimeException e) {
        requestObserver.onError(e);
        throw e;
      }
    }

    private class ResponseObserver
        implements ClientResponseObserver<EvaluateRequestBatch, EvaluateResponseBatch> {

      @Override
      public void onNext(EvaluateResponseBatch respb) {
        long remain = outstandingCount.addAndGet(-respb.getResponseCount());
        verify(remain >= 0);
        for (EvaluateResponse resp : respb.getResponseList()) {
          final SettableFuture<Value> f = pending.remove();
          final Value v;
          try {
            v = Value.deserialize(outputType, resp.getValue());
          } catch (RuntimeException e) {
            f.setException(e);
            continue;
          }
          f.set(v);
        }
        if (remain == 0) {
          /* If no outstanding requests remain, flush the current batch. This
           * avoids a hang if the caller stops calling evaluate but doesn't
           * call flush and limits the latency of a request to 2T (where T is
           * the round-trip latency of a single request). This may result in
           * suboptimal behavior for a time if the request rate slows to
           * between T and 2T.
           */
          flush();
        }
      }

      private void setException(Throwable t) {
        for (SettableFuture<Value> f; (f = pending.poll()) != null; ) {
          f.setException(t);
        }
      }

      @Override
      public void onError(Throwable t) {
        if (t instanceof StatusRuntimeException) {
          setException(new SqlException((StatusRuntimeException) t));
        } else {
          setException(t);
        }
      }

      @Override
      public void onCompleted() {
        setException(new RuntimeException("Stream closed"));
      }

      @Override
      public void beforeStart(final ClientCallStreamObserver<EvaluateRequestBatch> requestStream) {
        requestStream.setOnReadyHandler(this::onReady);
      }

      private void onReady() {
        maybeFlush();
      }
    }

    @Override
    public void close() {
      synchronized (this) {
        requestObserver.onCompleted();
      }
      if (channel instanceof ManagedChannel) {
        ((ManagedChannel) channel).shutdown();
      }
    }

    @Override
    protected void finalize() throws Throwable {
      super.finalize();
      close();
    }
  }

  /** Release the server side state for this prepared expression. */
  @Override
  public void close() {
    if (prepared && !closed) {
      try {
        Client.getStub()
            .unprepare(UnprepareRequest.newBuilder().setPreparedExpressionId(preparedId).build());
      } catch (StatusRuntimeException e) {
        // ignore
      }
      prepared = false;
      closed = true;
    }
  }

  @Override
  protected void finalize() throws Throwable {
    super.finalize();
    close();
  }

  private static EvaluateRequest.Parameter serializeParameter(String name, Value value) {
    return EvaluateRequest.Parameter.newBuilder()
        .setName(name)
        .setValue(value.serialize())
        .build();
  }
}
