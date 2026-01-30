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

import com.google.common.collect.ImmutableList;
import com.google.errorprone.annotations.CanIgnoreReturnValue;
import com.google.googlesql.FormatterOptions.FormatterOptionsProto;
import com.google.googlesql.FormatterOptions.FormatterRangeProto;
import com.google.googlesql.LocalService.FormatSqlRequest;
import io.grpc.StatusRuntimeException;
import java.util.Collection;

/** Formatter for GoogleSQL. */
public class SqlFormatter {

  @CanIgnoreReturnValue // TODO: consider removing this?
  public String formatSql(String sql) {
    try {
      return Client.getStub().formatSql(request(sql)).getSql();
    } catch (StatusRuntimeException e) {
      throw new SqlException(e);
    }
  }

  public String lenientFormatSql(String sql) {
    try {
      return Client.getStub().lenientFormatSql(request(sql)).getSql();
    } catch (StatusRuntimeException e) {
      throw new SqlException(e);
    }
  }

  public String lenientFormatSql(String sql, FormatterOptionsProto options) {
    return lenientFormatSql(sql, ImmutableList.of(), options);
  }

  public String lenientFormatSql(
      String sql, Collection<FormatterRangeProto> byteRanges, FormatterOptionsProto options) {
    try {
      return Client.getStub().lenientFormatSql(request(sql, byteRanges, options)).getSql();
    } catch (StatusRuntimeException e) {
      throw new SqlException(e);
    }
  }

  protected static FormatSqlRequest request(String sql) {
    return FormatSqlRequest.newBuilder().setSql(sql).build();
  }

  protected static FormatSqlRequest request(
      String sql, Collection<FormatterRangeProto> byteRanges, FormatterOptionsProto options) {
    return FormatSqlRequest.newBuilder()
        .setSql(sql)
        .addAllByteRanges(byteRanges)
        .setOptions(options)
        .build();
  }
}
