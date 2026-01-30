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

import com.google.googlesql.LocalService.AnalyzeRequest;
import com.google.googlesql.LocalService.AnalyzeResponse;
import com.google.googlesql.LocalService.BuildSqlRequest;
import com.google.googlesql.functions.GoogleSQLDateTime.DateTimestampPart;
import com.google.googlesql.resolvedast.DeserializationHelper;
import com.google.googlesql.resolvedast.ResolvedNodes.ResolvedExpr;
import com.google.googlesql.resolvedast.ResolvedNodes.ResolvedStatement;
import java.util.Map;

class AnalyzerHelper {
  static {
    GoogleSQLDescriptorPool.importIntoGeneratedPool(DateTimestampPart.getDescriptor());
  }

  public static FileDescriptorSetsBuilder serializeSimpleCatalog(
      SimpleCatalog catalog, AnalyzerOptions options, AnalyzeRequest.Builder request) {
    FileDescriptorSetsBuilder fileDescriptorSetsBuilder;
    if (catalog.isRegistered()) {
      fileDescriptorSetsBuilder = new FileDescriptorSetsBuilder();
      fileDescriptorSetsBuilder.addAllFileDescriptors(BuiltinDescriptorPool.getInstance());

      Map<DescriptorPool, Long> registeredDescriptorPoolIds =
          catalog.getRegisteredDescriptorPoolIds();
      for (DescriptorPool pool : registeredDescriptorPoolIds.keySet()) {
        fileDescriptorSetsBuilder.addAllFileDescriptors(pool);
      }

      request.setRegisteredCatalogId(catalog.getRegisteredId());
      request.setOptions(options.serialize(fileDescriptorSetsBuilder));
      request.setDescriptorPoolList(
          DescriptorPoolSerializer.createDescriptorPoolListWithRegisteredIds(
              fileDescriptorSetsBuilder, registeredDescriptorPoolIds));
    } else {
      fileDescriptorSetsBuilder = new FileDescriptorSetsBuilder();
      fileDescriptorSetsBuilder.addAllFileDescriptors(BuiltinDescriptorPool.getInstance());
      request.setSimpleCatalog(catalog.serialize(fileDescriptorSetsBuilder));
      request.setOptions(options.serialize(fileDescriptorSetsBuilder));
      request.setDescriptorPoolList(
          DescriptorPoolSerializer.createDescriptorPoolList(fileDescriptorSetsBuilder));
    }
    return fileDescriptorSetsBuilder;
  }

  /**
   * Includes a hack to allow DatePart enums to be properly serialized, however this doesn't support
   * registered catalogs.
   */
  public static FileDescriptorSetsBuilder serializeSimpleCatalog(
      SimpleCatalog catalog,
      BuildSqlRequest.Builder request,
      Map<DescriptorPool, Long> registeredDescriptorPoolIds) {
    FileDescriptorSetsBuilder fileDescriptorSetsBuilder;
    if (catalog.isRegistered()) {
      fileDescriptorSetsBuilder = new FileDescriptorSetsBuilder();
      fileDescriptorSetsBuilder.addAllFileDescriptors(BuiltinDescriptorPool.getInstance());

      registeredDescriptorPoolIds.putAll(catalog.getRegisteredDescriptorPoolIds());
      for (DescriptorPool pool : registeredDescriptorPoolIds.keySet()) {
        fileDescriptorSetsBuilder.addAllFileDescriptors(pool);
      }
      request.setRegisteredCatalogId(catalog.getRegisteredId());
      registeredDescriptorPoolIds.putAll(catalog.getRegisteredDescriptorPoolIds());
    } else {
      fileDescriptorSetsBuilder = new FileDescriptorSetsBuilder();
      fileDescriptorSetsBuilder.addAllFileDescriptors(BuiltinDescriptorPool.getInstance());

      request.setSimpleCatalog(catalog.serialize(fileDescriptorSetsBuilder));
    }
    return fileDescriptorSetsBuilder;
  }

  public static ResolvedStatement deserializeResolvedStatement(
      SimpleCatalog catalog,
      FileDescriptorSetsBuilder fileDescriptorSetsBuilder,
      AnalyzeResponse response) {
    DeserializationHelper deserializationHelper =
        getDesializationHelper(catalog, fileDescriptorSetsBuilder);
    return ResolvedStatement.deserialize(response.getResolvedStatement(), deserializationHelper);
  }

  public static ResolvedExpr deserializeResolvedExpression(
      SimpleCatalog catalog,
      FileDescriptorSetsBuilder fileDescriptorSetsBuilder,
      AnalyzeResponse response) {
    DeserializationHelper deserializationHelper =
        getDesializationHelper(catalog, fileDescriptorSetsBuilder);
    return ResolvedExpr.deserialize(response.getResolvedExpression(), deserializationHelper);
  }

  public static DeserializationHelper getDesializationHelper(
      SimpleCatalog catalog, FileDescriptorSetsBuilder fileDescriptorSetsBuilder) {
    return new DeserializationHelper(
        catalog.getTypeFactory(), fileDescriptorSetsBuilder.getDescriptorPools(), catalog);
  }
}
