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

import static com.google.common.base.Preconditions.checkNotNull;

import com.google.errorprone.annotations.CanIgnoreReturnValue;
import com.google.googlesql.LocalService.ParseRequest;
import com.google.googlesql.LocalService.ParseResponse;
import com.google.googlesql.parser.ASTNodes.ASTScript;
import com.google.googlesql.parser.ASTNodes.ASTStatement;
import io.grpc.StatusRuntimeException;

/** The Parser class provides static methods to parse GoogleSQL statements or scripts */
public class Parser {

  @CanIgnoreReturnValue // TODO: consider removing this?
  public static ASTStatement parseStatement(String sql, LanguageOptions languageOptions) {
    checkNotNull(sql);
    checkNotNull(languageOptions);
    ParseResponse response;
    try {
      ParseRequest request =
          ParseRequest.newBuilder()
              .setSqlStatement(sql)
              .setOptions(languageOptions.serialize())
              .setAllowScript(false)
              .build();
      response = Client.getStub().parse(request);
      return ASTStatement.deserialize(response.getParsedStatement());
    } catch (StatusRuntimeException e) {
      throw new SqlException(e);
    }
  }

  public static ASTScript parseScript(String sql, LanguageOptions languageOptions) {
    checkNotNull(sql);
    checkNotNull(languageOptions);
    ParseResponse response;
    try {
      ParseRequest request =
          ParseRequest.newBuilder()
              .setSqlStatement(sql)
              .setOptions(languageOptions.serialize())
              .setAllowScript(true)
              .build();
      response = Client.getStub().parse(request);
      return ASTScript.deserialize(response.getParsedScript());
    } catch (StatusRuntimeException e) {
      throw new SqlException(e);
    }
  }

  public static ASTStatement parseNextStatement(
      ParseResumeLocation parseResumeLocation, LanguageOptions languageOptions) {
    return parseNextInternal(parseResumeLocation, languageOptions, /*allowScript=*/ false);
  }

  public static ASTStatement parseNextScriptStatement(
      ParseResumeLocation parseResumeLocation, LanguageOptions languageOptions) {
    return parseNextInternal(parseResumeLocation, languageOptions, /*allowScript=*/ true);
  }

  private static ASTStatement parseNextInternal(
      ParseResumeLocation parseResumeLocation,
      LanguageOptions languageOptions,
      boolean allowScript) {
    checkNotNull(parseResumeLocation);
    checkNotNull(languageOptions);

    ParseResponse response;
    try {
      ParseRequest request =
          ParseRequest.newBuilder()
              .setParseResumeLocation(parseResumeLocation.serialize())
              .setAllowScript(allowScript)
              .setOptions(languageOptions.serialize())
              .build();
      response = Client.getStub().parse(request);
    } catch (StatusRuntimeException e) {
      throw new SqlException(e);
    }

    parseResumeLocation.setBytePosition(response.getResumeBytePosition());

    return ASTStatement.deserialize(response.getParsedStatement());
  }

  private Parser() {}
}
