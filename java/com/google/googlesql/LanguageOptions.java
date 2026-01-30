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
import com.google.errorprone.annotations.CanIgnoreReturnValue;
import com.google.googlesql.GoogleSQLOptions.LanguageFeature;
import com.google.googlesql.GoogleSQLOptions.LanguageVersion;
import com.google.googlesql.GoogleSQLOptions.NameResolutionMode;
import com.google.googlesql.GoogleSQLOptions.ProductMode;
import com.google.googlesql.GoogleSQLOptionsProto.LanguageOptionsProto;
import com.google.googlesql.GoogleSQLResolvedNodeKind.ResolvedNodeKind;
import com.google.googlesql.LocalService.LanguageOptionsRequest;
import java.io.IOException;
import java.io.Serializable;
import java.util.HashSet;
import java.util.Set;

/**
 * This class contains options controlling the language that should be accepted, and the desired
 * semantics. This is used for libraries where behavior differs by language version, flags, or other
 * options.
 */
public class LanguageOptions implements Serializable {

  private static LanguageOptionsProto maxFeatures = null;

  private static synchronized LanguageOptionsProto getMaxFeatures() {
    if (maxFeatures == null) {
      maxFeatures =
          Client.getStub()
              .getLanguageOptions(
                  LanguageOptionsRequest.newBuilder().setMaximumFeatures(true).build());
    }
    return maxFeatures;
  }

  private static LanguageOptionsProto defaultFeatures = null;

  private static synchronized LanguageOptionsProto getDefaultFeatures() {
    if (defaultFeatures == null) {
      defaultFeatures =
          Client.getStub().getLanguageOptions(LanguageOptionsRequest.getDefaultInstance());
    }
    return defaultFeatures;
  }

  private transient LanguageOptionsProto.Builder builder = LanguageOptionsProto.newBuilder();

  public LanguageOptions() {
    this(getDefaultFeatures());
  }

  public LanguageOptions(LanguageOptionsProto proto) {
    builder.mergeFrom(proto);
  }

  @CanIgnoreReturnValue
  public LanguageOptions enableMaximumLanguageFeatures() {
    Set<LanguageFeature> features = new HashSet<>();
    features.addAll(getMaxFeatures().getEnabledLanguageFeaturesList());
    setEnabledLanguageFeatures(features);
    return this;
  }

  protected void assign(LanguageOptions options) {
    builder.clear();
    builder.mergeFrom(options.builder.build());
  }

  public LanguageOptionsProto serialize() {
    return builder.build();
  }

  public boolean supportsStatementKind(ResolvedNodeKind kind) {
    if (builder.getSupportedStatementKindsCount() == 0) {
      return true;
    }
    for (ResolvedNodeKind supported : builder.getSupportedStatementKindsList()) {
      if (kind.equals(supported)) {
        return true;
      }
    }
    return false;
  }

  public void setSupportedStatementKinds(Set<ResolvedNodeKind> supportedStatementKinds) {
    builder.clearSupportedStatementKinds();
    for (ResolvedNodeKind kind : supportedStatementKinds) {
      builder.addSupportedStatementKinds(kind);
    }
  }

  public void setSupportsAllStatementKinds() {
    builder.clearSupportedStatementKinds();
  }

  public void setLanguageVersion(LanguageVersion version) {
    setEnabledLanguageFeatures(getLanguageFeaturesForVersion(version));
  }

  public static Set<LanguageFeature> getLanguageFeaturesForVersion(LanguageVersion version) {
    Preconditions.checkArgument(
        !version.equals(LanguageVersion.__LanguageVersion__switch_must_have_a_default__));
    Set<LanguageFeature> features = new HashSet<>();
    features.addAll(
        Client.getStub()
            .getLanguageOptions(
                LanguageOptionsRequest.newBuilder().setLanguageVersion(version).build())
            .getEnabledLanguageFeaturesList());
    return features;
  }

  public boolean languageFeatureEnabled(LanguageFeature feature) {
    for (LanguageFeature enabled : builder.getEnabledLanguageFeaturesList()) {
      if (enabled.equals(feature)) {
        return true;
      }
    }
    return false;
  }

  public void setEnabledLanguageFeatures(Set<LanguageFeature> enabledLanguageFeatures) {
    builder.clearEnabledLanguageFeatures();
    for (LanguageFeature feature : enabledLanguageFeatures) {
      builder.addEnabledLanguageFeatures(feature);
    }
  }

  public void enableLanguageFeature(LanguageFeature feature) {
    if (!languageFeatureEnabled(feature)) {
      builder.addEnabledLanguageFeatures(feature);
    }
  }

  public void disableAllLanguageFeatures() {
    builder.clearEnabledLanguageFeatures();
  }

  public void setNameResolutionMode(NameResolutionMode mode) {
    builder.setNameResolutionMode(mode);
  }

  public NameResolutionMode getNameResolutionMode() {
    return builder.getNameResolutionMode();
  }

  public void setProductMode(ProductMode mode) {
    builder.setProductMode(mode);
  }

  public ProductMode getProductMode() {
    return builder.getProductMode();
  }

  public void setErrorOnDeprecatedSyntax(boolean value) {
    builder.setErrorOnDeprecatedSyntax(value);
  }

  public boolean getErrorOnDeprecatedSyntax() {
    return builder.getErrorOnDeprecatedSyntax();
  }

  public void enableReservableKeyword(String keyword) {
    if (!reservableKeywordEnabled(keyword)) {
      builder.addReservedKeywords(keyword);
    }
  }

  public boolean reservableKeywordEnabled(String keyword) {
    return builder.getReservedKeywordsList().contains(keyword);
  }

  private void writeObject(java.io.ObjectOutputStream out) throws IOException {
    out.defaultWriteObject();
    builder.build().writeTo(out);
  }

  private void readObject(java.io.ObjectInputStream in) throws IOException, ClassNotFoundException {
    in.defaultReadObject();
    this.builder = LanguageOptionsProto.newBuilder().mergeFrom(in);
  }
}
