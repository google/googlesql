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

import static com.google.common.truth.Truth.assertThat;
import static org.junit.Assert.fail;

import com.google.googlesql.GoogleSQLOptions.LanguageFeature;
import com.google.googlesql.GoogleSQLOptions.LanguageVersion;
import com.google.googlesql.GoogleSQLOptions.NameResolutionMode;
import com.google.googlesql.GoogleSQLOptions.ProductMode;
import com.google.googlesql.GoogleSQLResolvedNodeKind.ResolvedNodeKind;


import java.util.HashSet;
import java.util.Set;
import org.junit.Test;
import org.junit.runner.RunWith;
import org.junit.runners.JUnit4;

@RunWith(JUnit4.class)

public class LanguageOptionsTest {

  static LanguageOptions maximumFeatures() {
    LanguageOptions options = new LanguageOptions();
    options.enableMaximumLanguageFeatures();
    return options;
  }

  @Test
  public void testAssign() {
    LanguageOptions options = new LanguageOptions();
    LanguageOptions options2 = maximumFeatures();
    assertThat(options.serialize().equals(options2.serialize())).isFalse();
    assertThat(options.languageFeatureEnabled(LanguageFeature.FEATURE_ANALYTIC_FUNCTIONS))
        .isFalse();
    options.assign(options2);
    assertThat(options.languageFeatureEnabled(LanguageFeature.FEATURE_ANALYTIC_FUNCTIONS)).isTrue();
    assertThat(options.serialize().equals(options2.serialize())).isTrue();
  }

  @Test
  public void testSupportsStatementKind() {
    LanguageOptions options = new LanguageOptions();
    assertThat(options.serialize().getSupportedStatementKindsCount()).isEqualTo(1);
    assertThat(options.supportsStatementKind(ResolvedNodeKind.RESOLVED_QUERY_STMT)).isTrue();
    assertThat(options.supportsStatementKind(ResolvedNodeKind.RESOLVED_BEGIN_STMT)).isFalse();
    Set<ResolvedNodeKind> supported = new HashSet<ResolvedNodeKind>();
    options.setSupportedStatementKinds(supported);
    assertThat(options.serialize().getSupportedStatementKindsCount()).isEqualTo(0);
    assertThat(options.supportsStatementKind(ResolvedNodeKind.RESOLVED_QUERY_STMT)).isTrue();
    assertThat(options.supportsStatementKind(ResolvedNodeKind.RESOLVED_BEGIN_STMT)).isTrue();
    supported.add(ResolvedNodeKind.RESOLVED_BEGIN_STMT);
    assertThat(options.serialize().getSupportedStatementKindsCount()).isEqualTo(0);
    assertThat(options.supportsStatementKind(ResolvedNodeKind.RESOLVED_QUERY_STMT)).isTrue();
    assertThat(options.supportsStatementKind(ResolvedNodeKind.RESOLVED_BEGIN_STMT)).isTrue();
    options.setSupportedStatementKinds(supported);
    assertThat(options.serialize().getSupportedStatementKindsCount()).isEqualTo(1);
    assertThat(options.supportsStatementKind(ResolvedNodeKind.RESOLVED_QUERY_STMT)).isFalse();
    assertThat(options.supportsStatementKind(ResolvedNodeKind.RESOLVED_BEGIN_STMT)).isTrue();
    options.setSupportsAllStatementKinds();
    assertThat(options.serialize().getSupportedStatementKindsCount()).isEqualTo(0);
    assertThat(options.supportsStatementKind(ResolvedNodeKind.RESOLVED_QUERY_STMT)).isTrue();
    assertThat(options.supportsStatementKind(ResolvedNodeKind.RESOLVED_BEGIN_STMT)).isTrue();
    assertThat(options.supportsStatementKind(ResolvedNodeKind.RESOLVED_EXPLAIN_STMT)).isTrue();
  }

  @Test
  public void testLanguageFeatureEnabled() {
    LanguageOptions options = new LanguageOptions();
    assertThat(options.serialize().getEnabledLanguageFeaturesCount()).isEqualTo(0);
    assertThat(options.languageFeatureEnabled(LanguageFeature.FEATURE_ANALYTIC_FUNCTIONS))
        .isFalse();

    options.enableLanguageFeature(LanguageFeature.FEATURE_ANALYTIC_FUNCTIONS);
    assertThat(options.serialize().getEnabledLanguageFeaturesCount()).isEqualTo(1);
    assertThat(options.languageFeatureEnabled(LanguageFeature.FEATURE_ANALYTIC_FUNCTIONS)).isTrue();
    assertThat(options.languageFeatureEnabled(LanguageFeature.FEATURE_DISALLOW_GROUP_BY_FLOAT))
        .isFalse();

    Set<LanguageFeature> features = new HashSet<>();
    options.setEnabledLanguageFeatures(features);
    assertThat(options.serialize().getEnabledLanguageFeaturesCount()).isEqualTo(0);
    assertThat(options.languageFeatureEnabled(LanguageFeature.FEATURE_DISALLOW_GROUP_BY_FLOAT))
        .isFalse();
    assertThat(options.languageFeatureEnabled(LanguageFeature.FEATURE_ANALYTIC_FUNCTIONS))
        .isFalse();
    assertThat(options.languageFeatureEnabled(LanguageFeature.FEATURE_SELECT_STAR_EXCEPT_REPLACE))
        .isFalse();
    assertThat(options.languageFeatureEnabled(LanguageFeature.FEATURE_TABLESAMPLE)).isFalse();
    assertThat(options.languageFeatureEnabled(LanguageFeature.FEATURE_ORDER_BY_COLLATE)).isFalse();
    assertThat(options.languageFeatureEnabled(LanguageFeature.FEATURE_WITH_ON_SUBQUERY)).isFalse();

    features.add(LanguageFeature.FEATURE_ANALYTIC_FUNCTIONS);
    assertThat(options.serialize().getEnabledLanguageFeaturesCount()).isEqualTo(0);
    assertThat(options.languageFeatureEnabled(LanguageFeature.FEATURE_DISALLOW_GROUP_BY_FLOAT))
        .isFalse();
    assertThat(options.languageFeatureEnabled(LanguageFeature.FEATURE_ANALYTIC_FUNCTIONS))
        .isFalse();
    assertThat(options.languageFeatureEnabled(LanguageFeature.FEATURE_SELECT_STAR_EXCEPT_REPLACE))
        .isFalse();
    assertThat(options.languageFeatureEnabled(LanguageFeature.FEATURE_TABLESAMPLE)).isFalse();
    assertThat(options.languageFeatureEnabled(LanguageFeature.FEATURE_ORDER_BY_COLLATE)).isFalse();
    assertThat(options.languageFeatureEnabled(LanguageFeature.FEATURE_WITH_ON_SUBQUERY)).isFalse();

    options.setEnabledLanguageFeatures(features);
    assertThat(options.serialize().getEnabledLanguageFeaturesCount()).isEqualTo(1);
    assertThat(options.languageFeatureEnabled(LanguageFeature.FEATURE_DISALLOW_GROUP_BY_FLOAT))
        .isFalse();
    assertThat(options.languageFeatureEnabled(LanguageFeature.FEATURE_ANALYTIC_FUNCTIONS)).isTrue();
    assertThat(options.languageFeatureEnabled(LanguageFeature.FEATURE_SELECT_STAR_EXCEPT_REPLACE))
        .isFalse();
    assertThat(options.languageFeatureEnabled(LanguageFeature.FEATURE_TABLESAMPLE)).isFalse();
    assertThat(options.languageFeatureEnabled(LanguageFeature.FEATURE_ORDER_BY_COLLATE)).isFalse();
    assertThat(options.languageFeatureEnabled(LanguageFeature.FEATURE_WITH_ON_SUBQUERY)).isFalse();

    features.add(LanguageFeature.FEATURE_DISALLOW_GROUP_BY_FLOAT);
    options.setEnabledLanguageFeatures(features);
    assertThat(options.serialize().getEnabledLanguageFeaturesCount()).isEqualTo(2);
    assertThat(options.languageFeatureEnabled(LanguageFeature.FEATURE_DISALLOW_GROUP_BY_FLOAT))
        .isTrue();
    assertThat(options.languageFeatureEnabled(LanguageFeature.FEATURE_ANALYTIC_FUNCTIONS)).isTrue();
    assertThat(options.languageFeatureEnabled(LanguageFeature.FEATURE_SELECT_STAR_EXCEPT_REPLACE))
        .isFalse();
    assertThat(options.languageFeatureEnabled(LanguageFeature.FEATURE_TABLESAMPLE)).isFalse();
    assertThat(options.languageFeatureEnabled(LanguageFeature.FEATURE_ORDER_BY_COLLATE)).isFalse();
    assertThat(options.languageFeatureEnabled(LanguageFeature.FEATURE_WITH_ON_SUBQUERY)).isFalse();

    features.add(LanguageFeature.FEATURE_DISALLOW_GROUP_BY_FLOAT);
    options.setEnabledLanguageFeatures(features);
    assertThat(options.serialize().getEnabledLanguageFeaturesCount()).isEqualTo(2);
    assertThat(options.languageFeatureEnabled(LanguageFeature.FEATURE_DISALLOW_GROUP_BY_FLOAT))
        .isTrue();
    assertThat(options.languageFeatureEnabled(LanguageFeature.FEATURE_ANALYTIC_FUNCTIONS)).isTrue();
    assertThat(options.languageFeatureEnabled(LanguageFeature.FEATURE_SELECT_STAR_EXCEPT_REPLACE))
        .isFalse();
    assertThat(options.languageFeatureEnabled(LanguageFeature.FEATURE_TABLESAMPLE)).isFalse();
    assertThat(options.languageFeatureEnabled(LanguageFeature.FEATURE_ORDER_BY_COLLATE)).isFalse();
    assertThat(options.languageFeatureEnabled(LanguageFeature.FEATURE_WITH_ON_SUBQUERY)).isFalse();

    options.disableAllLanguageFeatures();
    assertThat(options.serialize().getEnabledLanguageFeaturesCount()).isEqualTo(0);
    assertThat(options.languageFeatureEnabled(LanguageFeature.FEATURE_DISALLOW_GROUP_BY_FLOAT))
        .isFalse();
    assertThat(options.languageFeatureEnabled(LanguageFeature.FEATURE_ANALYTIC_FUNCTIONS))
        .isFalse();
    assertThat(options.languageFeatureEnabled(LanguageFeature.FEATURE_SELECT_STAR_EXCEPT_REPLACE))
        .isFalse();
    assertThat(options.languageFeatureEnabled(LanguageFeature.FEATURE_TABLESAMPLE)).isFalse();
    assertThat(options.languageFeatureEnabled(LanguageFeature.FEATURE_ORDER_BY_COLLATE)).isFalse();
    assertThat(options.languageFeatureEnabled(LanguageFeature.FEATURE_WITH_ON_SUBQUERY)).isFalse();

    // We just need some basic tests, consistency with C++ is assured because we
    // now use SWIG.
    options.enableMaximumLanguageFeatures();
    assertThat(options.languageFeatureEnabled(LanguageFeature.FEATURE_ANALYTIC_FUNCTIONS)).isTrue();

    options.setLanguageVersion(LanguageVersion.VERSION_1_0);
    assertThat(options.serialize().getEnabledLanguageFeaturesCount()).isEqualTo(0);

    options.setLanguageVersion(LanguageVersion.VERSION_1_1);
    assertThat(options.languageFeatureEnabled(LanguageFeature.FEATURE_ANALYTIC_FUNCTIONS))
        .isFalse();
    assertThat(options.languageFeatureEnabled(LanguageFeature.FEATURE_WITH_ON_SUBQUERY)).isTrue();

    options.setLanguageVersion(LanguageVersion.VERSION_CURRENT);
    assertThat(options.serialize().getEnabledLanguageFeaturesCount()).isGreaterThan(0);

    try {
      options.setLanguageVersion(LanguageVersion.__LanguageVersion__switch_must_have_a_default__);
      fail();
    } catch (IllegalArgumentException expected) {
    }
  }

  @Test
  public void testNameResolutionMode() {
    LanguageOptions options = new LanguageOptions();
    assertThat(options.getNameResolutionMode())
        .isEqualTo(NameResolutionMode.NAME_RESOLUTION_DEFAULT);
    assertThat(options.serialize().getNameResolutionMode())
        .isEqualTo(NameResolutionMode.NAME_RESOLUTION_DEFAULT);
    options.setNameResolutionMode(NameResolutionMode.NAME_RESOLUTION_STRICT);
    assertThat(options.getNameResolutionMode())
        .isEqualTo(NameResolutionMode.NAME_RESOLUTION_STRICT);
    assertThat(options.serialize().getNameResolutionMode())
        .isEqualTo(NameResolutionMode.NAME_RESOLUTION_STRICT);
  }

  @Test
  public void testProductMode() {
    LanguageOptions options = new LanguageOptions();
    assertThat(options.getProductMode()).isEqualTo(ProductMode.PRODUCT_INTERNAL);
    assertThat(options.serialize().getProductMode()).isEqualTo(ProductMode.PRODUCT_INTERNAL);
    options.setProductMode(ProductMode.PRODUCT_EXTERNAL);
    assertThat(options.getProductMode()).isEqualTo(ProductMode.PRODUCT_EXTERNAL);
    assertThat(options.serialize().getProductMode()).isEqualTo(ProductMode.PRODUCT_EXTERNAL);
  }

  @Test
  public void testErrorOnDeprecatedSyntax() {
    LanguageOptions options = new LanguageOptions();
    assertThat(options.getErrorOnDeprecatedSyntax()).isFalse();
    assertThat(options.serialize().getErrorOnDeprecatedSyntax()).isFalse();
    options.setErrorOnDeprecatedSyntax(true);
    assertThat(options.getErrorOnDeprecatedSyntax()).isTrue();
    assertThat(options.serialize().getErrorOnDeprecatedSyntax()).isTrue();
  }

  @Test
  public void testReservedKeywords() {
    LanguageOptions options = new LanguageOptions();
    assertThat(options.reservableKeywordEnabled("QUALIFY")).isFalse();
    assertThat(options.serialize().getReservedKeywordsList()).isEmpty();
    options.enableReservableKeyword("QUALIFY");
    assertThat(options.reservableKeywordEnabled("QUALIFY")).isTrue();
    assertThat(options.serialize().getReservedKeywordsList()).contains("QUALIFY");
  }
}
