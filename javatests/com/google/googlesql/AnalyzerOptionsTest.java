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
import static com.google.common.truth.Truth.assertWithMessage;
import static com.google.common.truth.extensions.proto.ProtoTruth.assertThat;
import static org.junit.Assert.fail;

import com.google.googlesql.GoogleSQLOptions.ErrorMessageMode;
import com.google.googlesql.GoogleSQLOptions.GroupingSetRewriteOptions;
import com.google.googlesql.GoogleSQLOptions.ParameterMode;
import com.google.googlesql.GoogleSQLOptions.ParseLocationRecordType;
import com.google.googlesql.GoogleSQLOptions.ResolvedASTRewrite;
import com.google.googlesql.GoogleSQLOptions.RewriteOptions;
import com.google.googlesql.GoogleSQLOptionsProto.AnalyzerOptionsProto;
import com.google.googlesql.GoogleSQLType.TypeKind;
import com.google.googlesql.GoogleSQLType.TypeProto;


import java.util.List;
import org.junit.Test;
import org.junit.runner.RunWith;
import org.junit.runners.JUnit4;

@RunWith(JUnit4.class)

public class AnalyzerOptionsTest {

  @Test
  public void testRecordParseLocations() {
    FileDescriptorSetsBuilder builder = new FileDescriptorSetsBuilder();
    AnalyzerOptions options = new AnalyzerOptions();
    assertThat(options.getParseLocationRecordType())
        .isEqualTo(ParseLocationRecordType.PARSE_LOCATION_RECORD_NONE);
    AnalyzerOptionsProto proto = options.serialize(builder);
    assertThat(proto.getParseLocationRecordType())
        .isEqualTo(ParseLocationRecordType.PARSE_LOCATION_RECORD_NONE);
    checkDeserialize(proto, builder.getDescriptorPools());
    options.setParseLocationRecordType(ParseLocationRecordType.PARSE_LOCATION_RECORD_CODE_SEARCH);
    assertThat(options.getParseLocationRecordType())
        .isEqualTo(ParseLocationRecordType.PARSE_LOCATION_RECORD_CODE_SEARCH);
    proto = options.serialize(builder);
    assertThat(proto.getParseLocationRecordType())
        .isEqualTo(ParseLocationRecordType.PARSE_LOCATION_RECORD_CODE_SEARCH);
    checkDeserialize(proto, builder.getDescriptorPools());
  }

  @Test
  public void testPruneUnusedColumns() {
    FileDescriptorSetsBuilder builder = new FileDescriptorSetsBuilder();
    AnalyzerOptions options = new AnalyzerOptions();
    assertThat(options.getPruneUnusedColumns()).isFalse();
    AnalyzerOptionsProto proto = options.serialize(builder);
    assertThat(proto.getPruneUnusedColumns()).isFalse();
    checkDeserialize(proto, builder.getDescriptorPools());
    options.setPruneUnusedColumns(true);
    assertThat(options.getPruneUnusedColumns()).isTrue();
    proto = options.serialize(builder);
    assertThat(proto.getPruneUnusedColumns()).isTrue();
    checkDeserialize(proto, builder.getDescriptorPools());
  }

  @Test
  public void testAllowUndeclaredParameters() {
    FileDescriptorSetsBuilder builder = new FileDescriptorSetsBuilder();
    AnalyzerOptions options = new AnalyzerOptions();
    assertThat(options.getAllowUndeclaredParameters()).isFalse();
    AnalyzerOptionsProto proto = options.serialize(builder);
    assertThat(proto.getAllowUndeclaredParameters()).isFalse();
    checkDeserialize(proto, builder.getDescriptorPools());
    options.setAllowUndeclaredParameters(true);
    assertThat(options.getAllowUndeclaredParameters()).isTrue();
    proto = options.serialize(builder);
    assertThat(proto.getAllowUndeclaredParameters()).isTrue();
    checkDeserialize(proto, builder.getDescriptorPools());
  }

  @Test
  public void testParameterMode() {
    FileDescriptorSetsBuilder builder = new FileDescriptorSetsBuilder();
    AnalyzerOptions options = new AnalyzerOptions();
    assertThat(options.getParameterMode()).isEqualTo(ParameterMode.PARAMETER_NAMED);
    AnalyzerOptionsProto proto = options.serialize(builder);
    assertThat(proto.getParameterMode()).isEqualTo(ParameterMode.PARAMETER_NAMED);
    checkDeserialize(proto, builder.getDescriptorPools());
    options.setParameterMode(ParameterMode.PARAMETER_POSITIONAL);
    assertThat(options.getParameterMode()).isEqualTo(ParameterMode.PARAMETER_POSITIONAL);
    proto = options.serialize(builder);
    assertThat(proto.getParameterMode()).isEqualTo(ParameterMode.PARAMETER_POSITIONAL);
    checkDeserialize(proto, builder.getDescriptorPools());
  }

  @Test
  public void testPreserveColumnAliases() {
    FileDescriptorSetsBuilder builder = new FileDescriptorSetsBuilder();
    AnalyzerOptions options = new AnalyzerOptions();
    assertThat(options.getPreserveColumnAliases()).isTrue();
    AnalyzerOptionsProto proto = options.serialize(builder);
    assertThat(proto.getPreserveColumnAliases()).isTrue();
    checkDeserialize(proto, builder.getDescriptorPools());
    options.setPreserveColumnAliases(false);
    assertThat(options.getPreserveColumnAliases()).isFalse();
    proto = options.serialize(builder);
    assertThat(proto.getPreserveColumnAliases()).isFalse();
    checkDeserialize(proto, builder.getDescriptorPools());
  }

  @Test
  public void testPreserveUnnecessaryCast() {
    FileDescriptorSetsBuilder builder = new FileDescriptorSetsBuilder();
    AnalyzerOptions options = new AnalyzerOptions();
    assertThat(options.getPreserveUnnecessaryCast()).isFalse();
    AnalyzerOptionsProto proto = options.serialize(builder);
    assertThat(proto.getPreserveUnnecessaryCast()).isFalse();
    checkDeserialize(proto, builder.getDescriptorPools());
    options.setPreserveUnnecessaryCast(true);
    assertThat(options.getPreserveUnnecessaryCast()).isTrue();
    proto = options.serialize(builder);
    assertThat(proto.getPreserveUnnecessaryCast()).isTrue();
    checkDeserialize(proto, builder.getDescriptorPools());
  }

  @Test
  public void testSetLanguageOptions() {
    AnalyzerOptions options = new AnalyzerOptions();
    LanguageOptions languageOptions = LanguageOptionsTest.maximumFeatures();
    options.setLanguageOptions(languageOptions);
    FileDescriptorSetsBuilder builder = new FileDescriptorSetsBuilder();
    assertThat(options.getLanguageOptions()).isEqualTo(languageOptions);
    AnalyzerOptionsProto proto = options.serialize(builder);
    assertThat(proto.getLanguageOptions()).isEqualTo(languageOptions.serialize());
    checkDeserialize(proto, builder.getDescriptorPools());
  }

  @Test
  public void testRewriteEnabled() {
    AnalyzerOptions options = new AnalyzerOptions();
    FileDescriptorSetsBuilder builder = new FileDescriptorSetsBuilder();
    int defaultRewrites = options.serialize(builder).getEnabledRewritesCount();
    assertThat(defaultRewrites).isGreaterThan(0);

    options.enableRewrite(ResolvedASTRewrite.REWRITE_FLATTEN, false);
    assertThat(options.rewriteEnabled(ResolvedASTRewrite.REWRITE_FLATTEN)).isFalse();
    assertThat(options.serialize(builder).getEnabledRewritesCount()).isEqualTo(defaultRewrites - 1);
    options.enableRewrite(ResolvedASTRewrite.REWRITE_FLATTEN);
    assertThat(options.rewriteEnabled(ResolvedASTRewrite.REWRITE_FLATTEN)).isTrue();
    assertThat(options.serialize(builder).getEnabledRewritesCount()).isEqualTo(defaultRewrites);
  }

  @Test
  public void testErrorMessageMode() {
    FileDescriptorSetsBuilder builder = new FileDescriptorSetsBuilder();
    AnalyzerOptions options = new AnalyzerOptions();
    assertThat(options.getErrorMessageMode()).isEqualTo(ErrorMessageMode.ERROR_MESSAGE_ONE_LINE);
    AnalyzerOptionsProto proto = options.serialize(builder);
    assertThat(proto.getErrorMessageMode()).isEqualTo(ErrorMessageMode.ERROR_MESSAGE_ONE_LINE);
    checkDeserialize(proto, builder.getDescriptorPools());
    options.setErrorMessageMode(ErrorMessageMode.ERROR_MESSAGE_WITH_PAYLOAD);
    assertThat(options.getErrorMessageMode())
        .isEqualTo(ErrorMessageMode.ERROR_MESSAGE_WITH_PAYLOAD);
    proto = options.serialize(builder);
    assertThat(proto.getErrorMessageMode()).isEqualTo(ErrorMessageMode.ERROR_MESSAGE_WITH_PAYLOAD);
    checkDeserialize(proto, builder.getDescriptorPools());
  }

  @Test
  public void testDefaultTimezone() {
    FileDescriptorSetsBuilder builder = new FileDescriptorSetsBuilder();
    AnalyzerOptions options = new AnalyzerOptions();
    assertThat(options.getDefaultTimezone()).isEqualTo("America/Los_Angeles");
    AnalyzerOptionsProto proto = options.serialize(builder);
    assertThat(proto.getDefaultTimezone()).isEqualTo("America/Los_Angeles");
    checkDeserialize(proto, builder.getDescriptorPools());
    options.setDefaultTimezone("Asia/Shanghai");
    assertThat(options.getDefaultTimezone()).isEqualTo("Asia/Shanghai");
    proto = options.serialize(builder);
    assertThat(proto.getDefaultTimezone()).isEqualTo("Asia/Shanghai");
    checkDeserialize(proto, builder.getDescriptorPools());
  }

  @Test
  public void testInScopeExpressionColumn() {
    FileDescriptorSetsBuilder builder = new FileDescriptorSetsBuilder();
    AnalyzerOptions options = new AnalyzerOptions();
    assertThat(options.hasInScopeExpressionColumn()).isFalse();
    assertThat(options.getInScopeExpressionColumnName()).isNull();
    assertThat(options.getInScopeExpressionColumnType()).isNull();
    AnalyzerOptionsProto proto = options.serialize(builder);
    assertThat(proto.hasInScopeExpressionColumn()).isFalse();
    checkDeserialize(proto, builder.getDescriptorPools());
    options.setInScopeExpressionColumn("foo", TypeFactory.createSimpleType(TypeKind.TYPE_INT32));
    assertThat(options.hasInScopeExpressionColumn()).isTrue();
    assertThat(options.getInScopeExpressionColumnName()).isEqualTo("foo");
    assertThat(options.getInScopeExpressionColumnType().getKind()).isEqualTo(TypeKind.TYPE_INT32);
    proto = options.serialize(builder);
    assertThat(proto.hasInScopeExpressionColumn()).isTrue();
    assertThat(proto.getInScopeExpressionColumn().getName()).isEqualTo("foo");
    assertThat(proto.getInScopeExpressionColumn().getType().getTypeKind())
        .isEqualTo(TypeKind.TYPE_INT32);
    checkDeserialize(proto, builder.getDescriptorPools());
    try {
      options.setInScopeExpressionColumn("bar", TypeFactory.createSimpleType(TypeKind.TYPE_INT32));
      fail();
    } catch (IllegalStateException expected) {
    }
  }

  @Test
  public void testSetDdlPseudoColumns() {
    FileDescriptorSetsBuilder builder = new FileDescriptorSetsBuilder();
    AnalyzerOptions options = new AnalyzerOptions();
    assertThat(options.getDdlPseudoColumns()).isEmpty();
    options.addDdlPseudoColumn("foo", TypeFactory.createSimpleType(TypeKind.TYPE_INT32));
    options.addDdlPseudoColumn("bar", TypeFactory.createSimpleType(TypeKind.TYPE_INT64));

    assertThat(options.getDdlPseudoColumns()).hasSize(2);
    assertThat(options.getDdlPseudoColumns().get("foo").getKind()).isEqualTo(TypeKind.TYPE_INT32);
    assertThat(options.getDdlPseudoColumns().get("bar").getKind()).isEqualTo(TypeKind.TYPE_INT64);

    AnalyzerOptionsProto proto = options.serialize(builder);
    assertThat(proto.getDdlPseudoColumnsCount()).isEqualTo(2);
    checkDeserialize(proto, builder.getDescriptorPools());
  }

  @Test
  public void testQueryParameters() {
    TypeFactory factory = TypeFactory.nonUniqueNames();
    FileDescriptorSetsBuilder builder = new FileDescriptorSetsBuilder();
    AnalyzerOptions options = new AnalyzerOptions();
    assertThat(options.getQueryParameters()).isEmpty();
    options.addQueryParameter("foo", TypeFactory.createSimpleType(TypeKind.TYPE_STRING));
    assertThat(options.getQueryParameters()).hasSize(1);
    assertThat(options.getQueryParameters().get("foo").getKind()).isEqualTo(TypeKind.TYPE_STRING);
    EnumType generatedEnum = factory.createEnumType(TypeKind.class);
    options.addQueryParameter("bar", generatedEnum);
    assertThat(options.getQueryParameters()).hasSize(2);
    assertThat(options.getQueryParameters().get("bar")).isEqualTo(generatedEnum);
    ProtoType generatedProto = factory.createProtoType(TypeProto.class);
    options.addQueryParameter("baz", generatedProto);
    assertThat(options.getQueryParameters()).hasSize(3);
    assertThat(options.getQueryParameters().get("baz")).isEqualTo(generatedProto);
    GoogleSQLDescriptorPool pool = TypeTestBase.getDescriptorPoolWithTypeProtoAndTypeKind();
    ProtoType poolProto =
        factory.createProtoType(pool.findMessageTypeByName("googlesql.TypeProto"));
    options.addQueryParameter("qux", poolProto);
    assertThat(options.getQueryParameters()).hasSize(4);
    assertThat(options.getQueryParameters().get("qux")).isEqualTo(poolProto);

    AnalyzerOptionsProto proto = options.serialize(builder);
    assertThat(proto.getQueryParametersCount()).isEqualTo(4);
    assertThat(builder.getFileDescriptorSetCount()).isEqualTo(2);
    checkDeserialize(proto, builder.getDescriptorPools());
  }

  @Test
  public void testPositionalQueryParameters() {
    TypeFactory factory = TypeFactory.nonUniqueNames();
    FileDescriptorSetsBuilder builder = new FileDescriptorSetsBuilder();
    AnalyzerOptions options = new AnalyzerOptions();
    assertThat(options.getPositionalQueryParameters()).isEmpty();
    options.addPositionalQueryParameter(TypeFactory.createSimpleType(TypeKind.TYPE_STRING));
    assertThat(options.getPositionalQueryParameters()).hasSize(1);
    assertThat(options.getPositionalQueryParameters().get(0).getKind())
        .isEqualTo(TypeKind.TYPE_STRING);
    EnumType generatedEnum = factory.createEnumType(TypeKind.class);
    options.addPositionalQueryParameter(generatedEnum);
    assertThat(options.getPositionalQueryParameters()).hasSize(2);
    assertThat(options.getPositionalQueryParameters().get(1)).isEqualTo(generatedEnum);
    ProtoType generatedProto = factory.createProtoType(TypeProto.class);
    options.addPositionalQueryParameter(generatedProto);
    assertThat(options.getPositionalQueryParameters()).hasSize(3);
    assertThat(options.getPositionalQueryParameters().get(2)).isEqualTo(generatedProto);
    GoogleSQLDescriptorPool pool = TypeTestBase.getDescriptorPoolWithTypeProtoAndTypeKind();
    ProtoType poolProto =
        factory.createProtoType(pool.findMessageTypeByName("googlesql.TypeProto"));
    options.addPositionalQueryParameter(poolProto);
    assertThat(options.getPositionalQueryParameters()).hasSize(4);
    assertThat(options.getPositionalQueryParameters().get(3)).isEqualTo(poolProto);

    AnalyzerOptionsProto proto = options.serialize(builder);
    assertThat(proto.getPositionalQueryParametersCount()).isEqualTo(4);
    assertThat(builder.getFileDescriptorSetCount()).isEqualTo(2);
    checkDeserialize(proto, builder.getDescriptorPools());
  }

  @Test
  public void testExpressionColumn() {
    TypeFactory factory = TypeFactory.nonUniqueNames();
    FileDescriptorSetsBuilder builder = new FileDescriptorSetsBuilder();
    AnalyzerOptions options = new AnalyzerOptions();
    assertThat(options.getExpressionColumns()).isEmpty();
    options.addExpressionColumn("foo", TypeFactory.createSimpleType(TypeKind.TYPE_STRING));
    assertThat(options.getExpressionColumns()).hasSize(1);
    assertThat(options.getExpressionColumns().get("foo").getKind()).isEqualTo(TypeKind.TYPE_STRING);
    EnumType generatedEnum = factory.createEnumType(TypeKind.class);
    options.addExpressionColumn("bar", generatedEnum);
    assertThat(options.getExpressionColumns()).hasSize(2);
    assertThat(options.getExpressionColumns().get("bar")).isEqualTo(generatedEnum);
    ProtoType generatedProto = factory.createProtoType(TypeProto.class);
    options.addExpressionColumn("baz", generatedProto);
    assertThat(options.getExpressionColumns()).hasSize(3);
    assertThat(options.getExpressionColumns().get("baz")).isEqualTo(generatedProto);
    GoogleSQLDescriptorPool pool = TypeTestBase.getDescriptorPoolWithTypeProtoAndTypeKind();
    ProtoType poolProto =
        factory.createProtoType(pool.findMessageTypeByName("googlesql.TypeProto"));
    options.addExpressionColumn("qux", poolProto);
    assertThat(options.getExpressionColumns()).hasSize(4);
    assertThat(options.getExpressionColumns().get("qux")).isEqualTo(poolProto);

    AnalyzerOptionsProto proto = options.serialize(builder);
    assertThat(proto.getExpressionColumnsCount()).isEqualTo(4);
    assertThat(builder.getFileDescriptorSetCount()).isEqualTo(2);
    checkDeserialize(proto, builder.getDescriptorPools());
  }

  @Test
  public void testGraphProperty() {
    TypeFactory factory = TypeFactory.nonUniqueNames();
    FileDescriptorSetsBuilder builder = new FileDescriptorSetsBuilder();
    AnalyzerOptions options = new AnalyzerOptions();
    assertThat(options.getGraphProperties()).isEmpty();
    options.addGraphProperty("foo", TypeFactory.createSimpleType(TypeKind.TYPE_STRING));
    assertThat(options.getGraphProperties()).hasSize(1);
    assertThat(options.getGraphProperties().get("foo").getKind()).isEqualTo(TypeKind.TYPE_STRING);
    EnumType generatedEnum = factory.createEnumType(TypeKind.class);
    options.addGraphProperty("bar", generatedEnum);
    assertThat(options.getGraphProperties()).hasSize(2);
    assertThat(options.getGraphProperties().get("bar")).isEqualTo(generatedEnum);
    ProtoType generatedProto = factory.createProtoType(TypeProto.class);
    options.addGraphProperty("baz", generatedProto);
    assertThat(options.getGraphProperties()).hasSize(3);
    assertThat(options.getGraphProperties().get("baz")).isEqualTo(generatedProto);
    GoogleSQLDescriptorPool pool = TypeTestBase.getDescriptorPoolWithTypeProtoAndTypeKind();
    ProtoType poolProto =
        factory.createProtoType(pool.findMessageTypeByName("googlesql.TypeProto"));
    options.addGraphProperty("qux", poolProto);
    assertThat(options.getGraphProperties()).hasSize(4);
    assertThat(options.getGraphProperties().get("qux")).isEqualTo(poolProto);

    AnalyzerOptionsProto proto = options.serialize(builder);
    assertThat(proto.getGraphPropertiesCount()).isEqualTo(4);
    assertThat(builder.getFileDescriptorSetCount()).isEqualTo(2);
    checkDeserialize(proto, builder.getDescriptorPools());
  }

  public void checkDeserialize(AnalyzerOptionsProto proto, List<DescriptorPool> pools) {
    TypeFactory factory = TypeFactory.nonUniqueNames();
    AnalyzerOptions options = AnalyzerOptions.deserialize(proto, pools, factory);
    FileDescriptorSetsBuilder builder = new FileDescriptorSetsBuilder();
    assertThat(options.serialize(builder)).isEqualTo(proto);
  }

  @Test
  public void testAllowedHintsAndOptions() {
    AnalyzerOptions options = new AnalyzerOptions();
    TypeFactory factory = TypeFactory.nonUniqueNames();
    FileDescriptorSetsBuilder builder = new FileDescriptorSetsBuilder();
    AllowedHintsAndOptions allowed = new AllowedHintsAndOptions("qual");
    options.setAllowedHintsAndOptions(allowed);
    allowed.addHint("test_qual", "hint", null, true);
    allowed.addHint("test_qual", "hint2", factory.createProtoType(TypeProto.class), false);
    allowed.addOption("option1", factory.createEnumType(TypeKind.class));
    allowed.addOption("option2", null);

    AnalyzerOptionsProto proto = options.serialize(builder);
    assertThat(proto.hasAllowedHintsAndOptions()).isTrue();
    checkDeserialize(proto, builder.getDescriptorPools());
  }

  @Test
  public void testSetRewriteOptions() {
    AnalyzerOptions options = new AnalyzerOptions();
    RewriteOptions rewriteOptions =
        RewriteOptions.newBuilder()
            .setGroupingSetRewriteOptions(
                GroupingSetRewriteOptions.newBuilder()
                    .setMaxGroupingSets(1)
                    .setMaxColumnsInGroupingSet(1)
                    .build())
            .build();
    options.setRewriteOptions(rewriteOptions);
    FileDescriptorSetsBuilder builder = new FileDescriptorSetsBuilder();

    assertThat(options.getRewriteOptions()).isEqualTo(rewriteOptions);
    AnalyzerOptionsProto proto = options.serialize(builder);
    assertThat(proto.getRewriteOptions()).isEqualTo(rewriteOptions);
    checkDeserialize(proto, builder.getDescriptorPools());
  }

  @Test
  public void testReplaceTableNotFoundErrorWithTvfErrorIfApplicable() {
    FileDescriptorSetsBuilder builder = new FileDescriptorSetsBuilder();
    AnalyzerOptions options = new AnalyzerOptions();
    assertThat(options.getReplaceTableNotFoundErrorWithTvfErrorIfApplicable()).isTrue();
    AnalyzerOptionsProto proto = options.serialize(builder);
    assertThat(proto.getReplaceTableNotFoundErrorWithTvfErrorIfApplicable()).isTrue();
    checkDeserialize(proto, builder.getDescriptorPools());
    options.setReplaceTableNotFoundErrorWithTvfErrorIfApplicable(false);
    assertThat(options.getReplaceTableNotFoundErrorWithTvfErrorIfApplicable()).isFalse();
    proto = options.serialize(builder);
    assertThat(proto.getReplaceTableNotFoundErrorWithTvfErrorIfApplicable()).isFalse();
    checkDeserialize(proto, builder.getDescriptorPools());
  }

  @Test
  public void testClassAndProtoSize() {
    assertWithMessage(
            "The number of fields of AnalyzerOptionsProto has changed, please also update the "
                + "serialization code accordingly.")
        .that(AnalyzerOptionsProto.getDescriptor().getFields())
    .hasSize(26);
    assertWithMessage(
            "The number of fields in AnalyzerOptions class has changed, please also update the "
                + "proto and serialization code accordingly.")
        .that(TestUtil.getNonStaticFieldCount(AnalyzerOptions.class))
        .isEqualTo(11);
  }
}
