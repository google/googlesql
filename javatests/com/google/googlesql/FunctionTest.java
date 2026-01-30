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
import static org.junit.Assert.fail;

import com.google.common.collect.ImmutableList;
import com.google.common.collect.Lists;
import com.google.googlesql.FunctionProtos.FunctionOptionsProto;
import com.google.googlesql.FunctionProtos.FunctionProto;
import com.google.googlesql.FunctionProtos.FunctionSignatureOptionsProto;
import com.google.googlesql.GoogleSQLFunctions.FunctionEnums.Mode;
import com.google.googlesql.GoogleSQLFunctions.FunctionEnums.WindowOrderSupport;
import com.google.googlesql.GoogleSQLType.TypeKind;

import java.util.ArrayList;
import java.util.List;
import org.junit.Test;
import org.junit.runner.RunWith;
import org.junit.runners.JUnit4;

@RunWith(JUnit4.class)

public class FunctionTest {

  @Test
  public void testGetAndIsMethod() {
    FunctionOptionsProto.Builder options = FunctionOptionsProto.newBuilder();
    options
        .setAliasName("test_function")
        .setAllowExternalUsage(false)
        .setArgumentsAreCoercible(false)
        .setIsDeprecated(true)
        .setSupportsOverClause(false)
        .setSupportsWindowFraming(true)
        .setWindowOrderingSupport(WindowOrderSupport.ORDER_REQUIRED);
    Function fn1 =
        new Function(
            "test_function_name",
            Function.GOOGLESQL_FUNCTION_GROUP_NAME,
            Mode.SCALAR,
            new ArrayList<FunctionSignature>(),
            options.build());

    assertThat(fn1.getMode()).isEqualTo(Mode.SCALAR);
    assertThat(fn1.isScalar()).isTrue();
    assertThat(fn1.isAggregate()).isFalse();
    assertThat(fn1.isGoogleSQLBuiltin()).isTrue();
    assertThat(fn1.getOptions().getIsDeprecated()).isTrue();
    assertThat(fn1.requireWindowOrdering()).isTrue();
    assertThat(fn1.getOptions().getSupportsWindowFraming()).isTrue();
    assertThat(fn1.getOptions().getAliasName()).isEqualTo("test_function");
    assertThat(fn1.getFullName()).isEqualTo("GoogleSQL:test_function_name");
    assertThat(fn1.getFullName(false)).isEqualTo("test_function_name");
    assertThat(fn1.getGroup()).isEqualTo("GoogleSQL");
    assertThat(fn1.getName()).isEqualTo("test_function_name");
    assertThat(fn1.getSqlName()).isEqualTo("TEST_FUNCTION_NAME");

    options
        .clear()
        .setSqlName("TEST_FUNCTION_NAME")
        .setSupportsOverClause(true)
        .setSupportsWindowFraming(true)
        .setWindowOrderingSupport(WindowOrderSupport.ORDER_OPTIONAL);
    Function fn2 =
        new Function(
            "test_function_2",
            "GoogleSQLTest",
            Mode.AGGREGATE,
            new ArrayList<FunctionSignature>(),
            options.build());
    assertThat(fn2.getSignatureList()).isEmpty();
    assertThat(fn2.debugString(true)).isEqualTo("GoogleSQLTest:test_function_2");

    assertThat(fn2.getMode()).isEqualTo(Mode.AGGREGATE);
    assertThat(fn2.isScalar()).isFalse();
    assertThat(fn2.isAggregate()).isTrue();
    assertThat(fn2.isGoogleSQLBuiltin()).isFalse();
    assertThat(fn2.getOptions().getIsDeprecated()).isFalse();
    assertThat(fn2.requireWindowOrdering()).isFalse();
    assertThat(fn2.getOptions().getSupportsWindowFraming()).isTrue();
    assertThat(fn2.getOptions().getAliasName()).isEmpty();
    assertThat(fn2.getFullName()).isEqualTo("GoogleSQLTest:test_function_2");
    assertThat(fn2.getFullName(false)).isEqualTo("test_function_2");
    assertThat(fn2.getGroup()).isEqualTo("GoogleSQLTest");
    assertThat(fn2.getName()).isEqualTo("test_function_2");
    assertThat(fn2.getSqlName()).isEqualTo("TEST_FUNCTION_NAME");

    options
        .clear()
        .setSupportsOverClause(true)
        .setSupportsWindowFraming(true)
        .setWindowOrderingSupport(WindowOrderSupport.ORDER_OPTIONAL);
    Function fn3 =
        new Function(
            "test_function_3",
            "GoogleSQLTest",
            Mode.ANALYTIC,
            new ArrayList<FunctionSignature>(),
            options.build());
    assertThat(fn3.isScalar()).isFalse();
    assertThat(fn3.isAggregate()).isFalse();
    assertThat(fn3.requireWindowOrdering()).isFalse();
    assertThat(fn3.getOptions().getSupportsWindowFraming()).isTrue();

    options
        .clear()
        .setSupportsOverClause(false)
        .setSupportsWindowFraming(true)
        .setWindowOrderingSupport(WindowOrderSupport.ORDER_UNSUPPORTED);
    try {
      new Function(
          "test_function_4",
          "GoogleSQLTest",
          Mode.ANALYTIC,
          new ArrayList<FunctionSignature>(),
          options.build());
      fail();
    } catch (IllegalArgumentException expected) {
      assertThat(expected)
          .hasMessageThat()
          .isEqualTo("Analytic functions must support over clause");
    }

    options.clear().setSupportsOverClause(true);
    try {
      new Function(
          "test_function_4",
          "GoogleSQLTest",
          Mode.SCALAR,
          new ArrayList<FunctionSignature>(),
          options.build());
      fail();
    } catch (IllegalArgumentException expected) {
      assertThat(expected)
          .hasMessageThat()
          .isEqualTo("Scalar functions cannot support over clause");
    }
  }

  @Test
  public void testSqlName() {
    FunctionOptionsProto.Builder options = FunctionOptionsProto.newBuilder();
    options.setSqlName("test_function");
    Function fn =
        new Function(
            "test_function_name",
            Function.GOOGLESQL_FUNCTION_GROUP_NAME,
            Mode.SCALAR,
            new ArrayList<FunctionSignature>(),
            options.build());
    assertThat(fn.getSqlName()).isEqualTo("TEST_FUNCTION");

    options.clear().setSqlName("Test_Function");
    fn =
        new Function(
            "test_function_name",
            Function.GOOGLESQL_FUNCTION_GROUP_NAME,
            Mode.SCALAR,
            new ArrayList<FunctionSignature>(),
            options.build());
    assertThat(fn.getSqlName()).isEqualTo("TEST_FUNCTION");

    options.clear().setSqlName("TEST_FUNCTION");
    fn =
        new Function(
            "test_function_name",
            Function.GOOGLESQL_FUNCTION_GROUP_NAME,
            Mode.SCALAR,
            new ArrayList<FunctionSignature>(),
            options.build());
    assertThat(fn.getSqlName()).isEqualTo("TEST_FUNCTION");

    options.clear().setSqlName("Test_Function").setUsesUpperCaseSqlName(false);
    fn =
        new Function(
            "test_function_name",
            Function.GOOGLESQL_FUNCTION_GROUP_NAME,
            Mode.SCALAR,
            new ArrayList<FunctionSignature>(),
            options.build());
    assertThat(fn.getSqlName()).isEqualTo("Test_Function");

    options.clear();
    fn =
        new Function(
            "$test_function_name",
            Function.GOOGLESQL_FUNCTION_GROUP_NAME,
            Mode.SCALAR,
            new ArrayList<FunctionSignature>(),
            options.build());
    assertThat(fn.getSqlName()).isEqualTo("TEST FUNCTION NAME");

    fn =
        new Function(
            "$Test_Function_Name",
            Function.GOOGLESQL_FUNCTION_GROUP_NAME,
            Mode.SCALAR,
            new ArrayList<FunctionSignature>(),
            options.build());
    assertThat(fn.getSqlName()).isEqualTo("TEST FUNCTION NAME");

    fn =
        new Function(
            "$test-function-name",
            Function.GOOGLESQL_FUNCTION_GROUP_NAME,
            Mode.SCALAR,
            new ArrayList<FunctionSignature>(),
            options.build());
    assertThat(fn.getSqlName()).isEqualTo("TEST-FUNCTION-NAME");

    fn =
        new Function(
            ImmutableList.of("teST", "function_name"),
            Function.GOOGLESQL_FUNCTION_GROUP_NAME,
            Mode.SCALAR,
            new ArrayList<FunctionSignature>(),
            options.build());
    assertThat(fn.getSqlName()).isEqualTo("TEST.FUNCTION_NAME");

    fn =
        new Function(
            "test_function_name",
            "GoogleSQLTest",
            Mode.SCALAR,
            new ArrayList<FunctionSignature>(),
            options.build());
    assertThat(fn.getSqlName()).isEqualTo("GOOGLESQLTEST:TEST_FUNCTION_NAME");

    options.setUsesUpperCaseSqlName(false);
    fn =
        new Function(
            ImmutableList.of("teST", "Function_name"),
            "GoogleSQLTest",
            Mode.SCALAR,
            new ArrayList<FunctionSignature>(),
            options.build());
    assertThat(fn.getSqlName()).isEqualTo("GoogleSQLTest:teST.Function_name");
  }

  @Test
  public void testSerializationAndDeserialization() {
    Function fn =
        new Function(
            "test_function_name",
            Function.GOOGLESQL_FUNCTION_GROUP_NAME,
            Mode.SCALAR,
            getSignaturesForSerializationDeserialization(),
            getOptionsForSerializationDeserialization());
    checkSerializeAndDeserialize(fn);
  }

  @Test
  public void testTemplateFunctionSerializationAndDeserialization() {
    Function fn =
        new Function.TemplatedSQLFunction(
            ImmutableList.of("test_function_name"),
            getSignaturesForSerializationDeserialization().get(0),
            ImmutableList.of(),
            new ParseResumeLocation("filename", "input"),
            Mode.AGGREGATE,
            getOptionsForSerializationDeserialization());
    checkSerializeAndDeserialize(fn);
  }

  private static FunctionOptionsProto getOptionsForSerializationDeserialization() {
    return FunctionOptionsProto.newBuilder()
        .setAliasName("test_function")
        .setAllowExternalUsage(false)
        .setArgumentsAreCoercible(false)
        .setIsDeprecated(true)
        .setSupportsOverClause(false)
        .setSupportsWindowFraming(true)
        .setWindowOrderingSupport(WindowOrderSupport.ORDER_REQUIRED)
        .build();
  }

  private static ImmutableList<FunctionSignature> getSignaturesForSerializationDeserialization() {
    FunctionArgumentType resultType =
        new FunctionArgumentType(TypeFactory.createSimpleType(TypeKind.TYPE_INT64));
    FunctionSignatureOptionsProto signatureOptions =
        FunctionSignatureOptionsProto.newBuilder().setIsDeprecated(true).build();
    return ImmutableList.of(
        new FunctionSignature(
            resultType, Lists.<FunctionArgumentType>newArrayList(), 1, signatureOptions),
        new FunctionSignature(
            resultType, Lists.<FunctionArgumentType>newArrayList(), -1, signatureOptions));
  }

  private static void checkSerializeAndDeserialize(Function function) {
    FileDescriptorSetsBuilder fileDescriptorSetsBuilder = new FileDescriptorSetsBuilder();
    checkEquals(
        function,
        Function.deserialize(
            function.serialize(fileDescriptorSetsBuilder),
            fileDescriptorSetsBuilder.getDescriptorPools()));
    assertThat(function.serialize(fileDescriptorSetsBuilder))
        .isEqualTo(
            Function.deserialize(
                    function.serialize(fileDescriptorSetsBuilder),
                    fileDescriptorSetsBuilder.getDescriptorPools())
                .serialize(fileDescriptorSetsBuilder));
  }

  private static void checkEquals(Function function1, Function function2) {
    assertThat(function2.getFullName()).isEqualTo(function1.getFullName());
    assertThat(function2.getMode()).isEqualTo(function1.getMode());
    List<FunctionSignature> signatures1 = function1.getSignatureList();
    List<FunctionSignature> signatures2 = function2.getSignatureList();
    assertThat(signatures2).hasSize(signatures1.size());
    for (int i = 0; i < signatures1.size(); i++) {
      FunctionSignatureTest.checkEquals(signatures1.get(i), signatures2.get(i));
    }
    assertThat(function2.getOptions()).isEqualTo(function1.getOptions());
  }

  @Test
  public void testClassAndProtoSize() {
    assertWithMessage(
            "The number of fields of FunctionProto has changed, "
                + "please also update the serialization code accordingly.")
        .that(FunctionProto.getDescriptor().getFields())
        .hasSize(9);
    assertWithMessage(
            "The number of fields in Function class has changed, "
                + "please also update the proto and serialization code accordingly.")
        .that(TestUtil.getNonStaticFieldCount(Function.class))
        .isEqualTo(5);
  }
}
