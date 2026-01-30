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

import com.google.common.collect.ImmutableSet;
import com.google.googlesql.GoogleSQLFunction.FunctionSignatureId;
import com.google.googlesql.GoogleSQLOptionsProto.GoogleSQLBuiltinFunctionOptionsProto;
import java.io.Serializable;
import java.util.HashSet;
import java.util.Set;

/**
 * Controls whether builtin FunctionSignatureIds and their matching FunctionSignatures are included
 * or excluded for an implementation.
 *
 * <p>LanguageOptions are applied to determine set of candidate functions before inclusion/exclusion
 * lists are applied.
 */
public class GoogleSQLBuiltinFunctionOptions implements Serializable {
  private final GoogleSQLBuiltinFunctionOptionsProto.Builder builder =
      GoogleSQLBuiltinFunctionOptionsProto.newBuilder();
  private final LanguageOptions languageOptions;
  private final Set<FunctionSignatureId> includeFunctionIds = new HashSet<>();
  private final Set<FunctionSignatureId> excludeFunctionIds = new HashSet<>();

  /**
   * Default constructor.  If LanguageOptions are not passed in, default to
   * including all possible functions.
   */
  public GoogleSQLBuiltinFunctionOptions() {
    this(new LanguageOptions().enableMaximumLanguageFeatures());
  }

  public GoogleSQLBuiltinFunctionOptions(LanguageOptions languageOptions) {
    this.languageOptions = languageOptions;
  }

  GoogleSQLBuiltinFunctionOptions(GoogleSQLBuiltinFunctionOptionsProto proto) {
    this(new LanguageOptions(proto.getLanguageOptions()));
    builder.mergeFrom(proto);
  }

  GoogleSQLBuiltinFunctionOptionsProto serialize() {
    builder.setLanguageOptions(languageOptions.serialize());
    return builder.build();
  }

  public LanguageOptions getLanguageOptions() {
    return languageOptions;
  }

  public void includeFunctionSignatureId(FunctionSignatureId id) {
    if (!includeFunctionIds.contains(id)) {
      includeFunctionIds.add(id);
      builder.addIncludeFunctionIds(id);
    }
  }

  public void excludeFunctionSignatureId(FunctionSignatureId id) {
    if (!excludeFunctionIds.contains(id)) {
      excludeFunctionIds.add(id);
      builder.addExcludeFunctionIds(id);
    }
  }

  public ImmutableSet<FunctionSignatureId> getIncludeFunctionSignatureIds() {
    return ImmutableSet.copyOf(includeFunctionIds);
  }

  public ImmutableSet<FunctionSignatureId> getExcludeFunctionSignatureIds() {
    return ImmutableSet.copyOf(excludeFunctionIds);
  }
}
