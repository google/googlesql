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
import static org.junit.Assert.assertThrows;

import com.google.googlesql.FunctionProtos.FunctionSignatureOptionsProto;
import com.google.googlesql.FunctionProtos.FunctionSignatureProto;
import com.google.googlesql.GoogleSQLFunctions.FunctionEnums.ArgumentCardinality;
import com.google.googlesql.GoogleSQLFunctions.SignatureArgumentKind;
import com.google.googlesql.GoogleSQLType.TypeKind;

import java.util.ArrayList;
import java.util.List;
import org.junit.Test;
import org.junit.runner.RunWith;
import org.junit.runners.JUnit4;

@RunWith(JUnit4.class)

public class FunctionSignatureTest {

  @Test
  public void testFunctionSignature() {
    FunctionSignatureOptionsProto options = FunctionSignatureOptionsProto.getDefaultInstance();
    List<FunctionSignature> signatures = new ArrayList<>();
    // Model a nullary function such as NOW()
    List<FunctionArgumentType> arguments = new ArrayList<>();
    FunctionSignature nullaryFunction =
        new FunctionSignature(
            new FunctionArgumentType(TypeFactory.createSimpleType(TypeKind.TYPE_TIMESTAMP)),
            arguments,
            0,
            options);
    signatures.add(nullaryFunction);
    assertThat(nullaryFunction.isConcrete()).isFalse();
    assertThat(nullaryFunction.debugString("NOW")).isEqualTo("NOW() -> TIMESTAMP");

    // Model simple operator like '+'
    FunctionArgumentType typeInt64 =
        new FunctionArgumentType(TypeFactory.createSimpleType(TypeKind.TYPE_INT64));
    arguments.add(typeInt64);
    arguments.add(typeInt64);

    FunctionSignature addFunction = new FunctionSignature(typeInt64, arguments, 0, options);
    signatures.add(addFunction);
    assertThat(addFunction.isConcrete()).isFalse();
    assertThat(addFunction.debugString("ADD")).isEqualTo("ADD(INT64, INT64) -> INT64");

    // Model signature for 'IF <bool> THEN <any> ELSE <any> END'
    arguments.clear();
    FunctionArgumentType typeAny1 = new FunctionArgumentType(SignatureArgumentKind.ARG_TYPE_ANY_1);
    arguments.add(new FunctionArgumentType(TypeFactory.createSimpleType(TypeKind.TYPE_BOOL)));
    arguments.add(typeAny1);
    arguments.add(typeAny1);

    FunctionSignature ifThenElseSignature = new FunctionSignature(typeAny1, arguments, 0, options);
    signatures.add(ifThenElseSignature);
    assertThat(ifThenElseSignature.isConcrete()).isFalse();
    assertThat(ifThenElseSignature.debugString("IF")).isEqualTo("IF(BOOL, <T1>, <T1>) -> <T1>");

    // Model signature for:
    // CASE WHEN <x1> THEN <y1>
    //      WHEN <x2> THEN <y2> ELSE <z> END
    arguments.clear();
    arguments.add(
        new FunctionArgumentType(
            TypeFactory.createSimpleType(TypeKind.TYPE_BOOL), ArgumentCardinality.REPEATED));
    arguments.add(
        new FunctionArgumentType(
            SignatureArgumentKind.ARG_TYPE_ANY_1, ArgumentCardinality.REPEATED, -1));
    arguments.add(
        new FunctionArgumentType(
            SignatureArgumentKind.ARG_TYPE_ANY_1, ArgumentCardinality.OPTIONAL, -1));

    FunctionSignature caseWhenSignature = new FunctionSignature(typeAny1, arguments, 0, options);
    signatures.add(caseWhenSignature);
    assertThat(caseWhenSignature.isConcrete()).isFalse();
    assertThat(caseWhenSignature.debugString("CASE"))
        .isEqualTo("CASE(repeated BOOL, repeated <T1>, optional <T1>) -> <T1>");

    // Model signature for:
    // CASE <w> WHEN <x1> THEN <y1>
    //          WHEN <x2> THEN <y2> ... ELSE <z> END
    arguments.clear();
    arguments.add(typeAny1);
    arguments.add(
        new FunctionArgumentType(
            SignatureArgumentKind.ARG_TYPE_ANY_1, ArgumentCardinality.REPEATED, -1));
    arguments.add(
        new FunctionArgumentType(
            SignatureArgumentKind.ARG_TYPE_ANY_2, ArgumentCardinality.REPEATED, -1));
    arguments.add(
        new FunctionArgumentType(
            SignatureArgumentKind.ARG_TYPE_ANY_2, ArgumentCardinality.OPTIONAL, -1));

    FunctionSignature caseValueSignature =
        new FunctionSignature(
            new FunctionArgumentType(SignatureArgumentKind.ARG_TYPE_ANY_2), arguments, -1, options);
    signatures.add(caseValueSignature);
    assertThat(caseValueSignature.isConcrete()).isFalse();
    assertThat(caseValueSignature.debugString("CASE"))
        .isEqualTo("CASE(<T1>, repeated <T1>, repeated <T2>, optional <T2>) -> <T2>");

    // test signaturesToString()
    assertThat(FunctionSignature.signaturesToString(signatures))
        .isEqualTo(
            "  () -> TIMESTAMP\n"
                + "  (INT64, INT64) -> INT64\n"
                + "  (BOOL, <T1>, <T1>) -> <T1>\n"
                + "  (repeated BOOL, repeated <T1>, optional <T1>) -> <T1>\n"
                + "  (<T1>, repeated <T1>, repeated <T2>, optional <T2>) -> <T2>");
  }

  @Test
  public void testConcreteArgumentType() {
    List<FunctionArgumentType> arguments = new ArrayList<>();
    FunctionSignature signature;
    FunctionSignatureOptionsProto options = FunctionSignatureOptionsProto.getDefaultInstance();
    final ArgumentCardinality required = ArgumentCardinality.REQUIRED;
    final ArgumentCardinality repeated = ArgumentCardinality.REPEATED;
    final ArgumentCardinality optional = ArgumentCardinality.OPTIONAL;
    Type typeInt64 = TypeFactory.createSimpleType(TypeKind.TYPE_INT64);
    Type typeInt32 = TypeFactory.createSimpleType(TypeKind.TYPE_INT32);
    Type typeBool = TypeFactory.createSimpleType(TypeKind.TYPE_BOOL);
    Type typeUint64 = TypeFactory.createSimpleType(TypeKind.TYPE_UINT64);
    Type typeUint32 = TypeFactory.createSimpleType(TypeKind.TYPE_UINT32);
    Type typeString = TypeFactory.createSimpleType(TypeKind.TYPE_STRING);
    Type typeDate = TypeFactory.createSimpleType(TypeKind.TYPE_DATE);
    Type typeBytes = TypeFactory.createSimpleType(TypeKind.TYPE_BYTES);
    FunctionArgumentType resultType = new FunctionArgumentType(typeInt64, required, 0);

    // 0 arguments.
    arguments.clear();
    signature = new FunctionSignature(resultType, arguments, 0, options);
    assertThat(signature.getConcreteArgumentsCount()).isEqualTo(0);

    // 1 required.
    arguments.add(new FunctionArgumentType(typeInt64, required, 1));
    signature = new FunctionSignature(resultType, arguments, 0, options);
    assertThat(signature.getConcreteArgumentsCount()).isEqualTo(1);
    checkConcreteArgumentType(typeInt64, signature, 0);

    // 2 required.
    arguments.add(new FunctionArgumentType(typeInt64, required, 1));
    signature = new FunctionSignature(resultType, arguments, 0, options);
    assertThat(signature.getConcreteArgumentsCount()).isEqualTo(2);
    checkConcreteArgumentType(typeInt64, signature, 0);
    checkConcreteArgumentType(typeInt64, signature, 1);

    // 3 required - simulates IF().
    arguments.clear();
    arguments.add(new FunctionArgumentType(typeBool, required, 1));
    arguments.add(new FunctionArgumentType(typeInt64, required, 1));
    arguments.add(new FunctionArgumentType(typeInt64, required, 1));
    signature = new FunctionSignature(resultType, arguments, 0, options);
    assertThat(signature.getConcreteArgumentsCount()).isEqualTo(3);
    checkConcreteArgumentType(typeBool, signature, 0);
    checkConcreteArgumentType(typeInt64, signature, 1);
    checkConcreteArgumentType(typeInt64, signature, 2);

    // 2 repeateds (2), 1 optional (0) -
    //   CASE WHEN . THEN . WHEN . THEN . END
    arguments.clear();
    arguments.add(new FunctionArgumentType(typeBool, repeated, 2));
    arguments.add(new FunctionArgumentType(typeInt64, repeated, 2));
    arguments.add(new FunctionArgumentType(typeInt64, optional, 0));
    signature = new FunctionSignature(resultType, arguments, 0, options);
    assertThat(signature.getConcreteArgumentsCount()).isEqualTo(4);
    checkConcreteArgumentType(typeBool, signature, 0);
    checkConcreteArgumentType(typeInt64, signature, 1);
    checkConcreteArgumentType(typeBool, signature, 2);
    checkConcreteArgumentType(typeInt64, signature, 3);

    // 2 required, 3 repeateds (2), 1 required, 2 optional (0,0) -
    arguments.clear();
    arguments.add(new FunctionArgumentType(typeBool, required, 1));
    arguments.add(new FunctionArgumentType(typeString, required, 1));
    arguments.add(new FunctionArgumentType(typeUint64, repeated, 2));
    arguments.add(new FunctionArgumentType(typeInt64, repeated, 2));
    arguments.add(new FunctionArgumentType(typeBytes, repeated, 2));
    arguments.add(new FunctionArgumentType(typeUint32, required, 1));
    arguments.add(new FunctionArgumentType(typeInt32, optional, 0));
    arguments.add(new FunctionArgumentType(typeDate, optional, 0));
    signature = new FunctionSignature(resultType, arguments, 0, options);
    assertThat(signature.getConcreteArgumentsCount()).isEqualTo(9);
    checkConcreteArgumentType(typeBool, signature, 0);
    checkConcreteArgumentType(typeString, signature, 1);
    checkConcreteArgumentType(typeUint64, signature, 2);
    checkConcreteArgumentType(typeInt64, signature, 3);
    checkConcreteArgumentType(typeBytes, signature, 4);
    checkConcreteArgumentType(typeUint64, signature, 5);
    checkConcreteArgumentType(typeInt64, signature, 6);
    checkConcreteArgumentType(typeBytes, signature, 7);
    checkConcreteArgumentType(typeUint32, signature, 8);

    // 2 required, 3 repeateds (2), 1 required, 2 optional (1,0) -
    arguments.clear();
    arguments.add(new FunctionArgumentType(typeBool, required, 1));
    arguments.add(new FunctionArgumentType(typeString, required, 1));
    arguments.add(new FunctionArgumentType(typeUint64, repeated, 2));
    arguments.add(new FunctionArgumentType(typeInt64, repeated, 2));
    arguments.add(new FunctionArgumentType(typeBytes, repeated, 2));
    arguments.add(new FunctionArgumentType(typeUint32, required, 1));
    arguments.add(new FunctionArgumentType(typeInt32, optional, 1));
    arguments.add(new FunctionArgumentType(typeDate, optional, 0));
    signature = new FunctionSignature(resultType, arguments, 0, options);
    assertThat(signature.getConcreteArgumentsCount()).isEqualTo(10);
    checkConcreteArgumentType(typeBool, signature, 0);
    checkConcreteArgumentType(typeString, signature, 1);
    checkConcreteArgumentType(typeUint64, signature, 2);
    checkConcreteArgumentType(typeInt64, signature, 3);
    checkConcreteArgumentType(typeBytes, signature, 4);
    checkConcreteArgumentType(typeUint64, signature, 5);
    checkConcreteArgumentType(typeInt64, signature, 6);
    checkConcreteArgumentType(typeBytes, signature, 7);
    checkConcreteArgumentType(typeUint32, signature, 8);
    checkConcreteArgumentType(typeInt32, signature, 9);

    // 2 required, 2 optional (1,0) -
    arguments.clear();
    arguments.add(new FunctionArgumentType(typeBool, required, 1));
    arguments.add(new FunctionArgumentType(typeString, required, 1));
    arguments.add(new FunctionArgumentType(typeInt32, optional, 1));
    arguments.add(new FunctionArgumentType(typeDate, optional, 0));
    signature = new FunctionSignature(resultType, arguments, 0, options);
    assertThat(signature.getConcreteArgumentsCount()).isEqualTo(3);
    checkConcreteArgumentType(typeBool, signature, 0);
    checkConcreteArgumentType(typeString, signature, 1);
    checkConcreteArgumentType(typeInt32, signature, 2);
  }

  private static void checkConcreteArgumentType(
      Type expectedType, FunctionSignature signature, int index) {
    assertThat(signature.getConcreteArgumentType(index)).isNotNull();
    assertThat(signature.getConcreteArgumentType(index)).isEqualTo(expectedType);
  }

  @Test
  public void testSerializationAndDeserialization() {
    FunctionSignatureOptionsProto options =
        FunctionSignatureOptionsProto.newBuilder()
            .setIsDeprecated(true)
            .build();

    List<FunctionArgumentType> arguments = new ArrayList<>();
    FunctionSignature nullaryFunction =
        new FunctionSignature(
            new FunctionArgumentType(TypeFactory.createSimpleType(TypeKind.TYPE_TIMESTAMP)),
            arguments,
            0,
            options);
    checkSerializeAndDeserialize(nullaryFunction);

    arguments.clear();
    arguments.add(
        new FunctionArgumentType(
            SignatureArgumentKind.ARG_TYPE_ANY_1, ArgumentCardinality.REPEATED, -1));
    arguments.add(
        new FunctionArgumentType(
            SignatureArgumentKind.ARG_TYPE_ANY_2, ArgumentCardinality.REQUIRED, -1));
    arguments.add(
        new FunctionArgumentType(
            SignatureArgumentKind.ARG_TYPE_ANY_2, ArgumentCardinality.OPTIONAL, -1));
    FunctionSignature signature =
        new FunctionSignature(
            new FunctionArgumentType(SignatureArgumentKind.ARG_TYPE_ANY_2), arguments, -1, options);
    checkSerializeAndDeserialize(signature);

    arguments.add(
        new FunctionArgumentType(
            TypeFactory.createSimpleType(TypeKind.TYPE_BOOL), ArgumentCardinality.REPEATED, 1));
    arguments.add(
        new FunctionArgumentType(
            TypeFactory.createSimpleType(TypeKind.TYPE_STRING), ArgumentCardinality.REQUIRED, 1));
    arguments.add(
        new FunctionArgumentType(
            TypeFactory.createSimpleType(TypeKind.TYPE_DOUBLE), ArgumentCardinality.OPTIONAL, 0));
    signature =
        new FunctionSignature(
            new FunctionArgumentType(SignatureArgumentKind.ARG_TYPE_ANY_2), arguments, -1, options);
    checkSerializeAndDeserialize(signature);

    // Test with a function signature with default arguments.
    arguments.clear();
    arguments.add(
        new FunctionArgumentType(
            SignatureArgumentKind.ARG_TYPE_ANY_1, ArgumentCardinality.REPEATED, -1));
    arguments.add(
        new FunctionArgumentType(
            SignatureArgumentKind.ARG_TYPE_ANY_2, ArgumentCardinality.REQUIRED, -1));
    arguments.add(
        new FunctionArgumentType(
            SignatureArgumentKind.ARG_TYPE_ANY_2,
            FunctionArgumentType.FunctionArgumentTypeOptions.builder()
                .setCardinality(ArgumentCardinality.OPTIONAL)
                .setDefault(Value.createInt32Value(314)).build(),
            1));
    arguments.add(
        new FunctionArgumentType(
            TypeFactory.createSimpleType(TypeKind.TYPE_STRING),
            FunctionArgumentType.FunctionArgumentTypeOptions.builder()
                .setCardinality(ArgumentCardinality.OPTIONAL)
                .setDefault(Value.createStringValue("abc")).build(),
            1));
    signature =
        new FunctionSignature(
            new FunctionArgumentType(SignatureArgumentKind.ARG_TYPE_ANY_2), arguments, -1, options);
    checkSerializeAndDeserialize(signature);

    // Test with an invalid function signature with a default argument.
    arguments.clear();
    arguments.add(
        new FunctionArgumentType(
            SignatureArgumentKind.ARG_TYPE_ANY_1, ArgumentCardinality.REPEATED, -1));
    arguments.add(
        new FunctionArgumentType(
            SignatureArgumentKind.ARG_TYPE_ANY_2, ArgumentCardinality.REQUIRED, -1));
    arguments.add(
        new FunctionArgumentType(
            SignatureArgumentKind.ARG_TYPE_ANY_2,
            FunctionArgumentType.FunctionArgumentTypeOptions.builder()
                .setCardinality(ArgumentCardinality.OPTIONAL)
                .setDefault(Value.createInt32Value(314)).build(),
            1));
    arguments.add(
        new FunctionArgumentType(
            TypeFactory.createSimpleType(TypeKind.TYPE_STRING), ArgumentCardinality.OPTIONAL, 1));
    assertThat(
            assertThrows(
                IllegalArgumentException.class,
                () ->
                    new FunctionSignature(
                        new FunctionArgumentType(SignatureArgumentKind.ARG_TYPE_ANY_2),
                        arguments,
                        -1,
                        options)))
        .hasMessageThat()
        .isEqualTo(
            "Optional arguments with default values must be at the end of the argument list");
  }

  private static void checkSerializeAndDeserialize(FunctionSignature signature) {
    FileDescriptorSetsBuilder fileDescriptorSetsBuilder = new FileDescriptorSetsBuilder();
    checkEquals(
        signature,
        FunctionSignature.deserialize(
            signature.serialize(fileDescriptorSetsBuilder),
            fileDescriptorSetsBuilder.getDescriptorPools()));
    assertThat(
            FunctionSignature.deserialize(
                    signature.serialize(fileDescriptorSetsBuilder),
                    fileDescriptorSetsBuilder.getDescriptorPools())
                .serialize(fileDescriptorSetsBuilder))
        .isEqualTo(signature.serialize(fileDescriptorSetsBuilder));
  }

  static void checkEquals(FunctionSignature signature1, FunctionSignature signature2) {
    assertThat(signature2.isConcrete()).isEqualTo(signature1.isConcrete());
    if (signature1.isConcrete()) {
      assertThat(signature2.getConcreteArgumentsCount())
          .isEqualTo(signature1.getConcreteArgumentsCount());
      for (int i = 0; i < signature1.getConcreteArgumentsCount(); i++) {
        assertThat(
                signature1.getConcreteArgumentType(i).equals(signature2.getConcreteArgumentType(i)))
            .isTrue();
      }
    }
    List<FunctionArgumentType> args1 = signature1.getFunctionArgumentList();
    List<FunctionArgumentType> args2 = signature2.getFunctionArgumentList();
    assertThat(args2).hasSize(args1.size());
    assertThat(signature2.getContextId()).isEqualTo(signature1.getContextId());
    FunctionArgumentTypeTest.checkEquals(signature1.getResultType(), signature2.getResultType());
    assertThat(signature1.getOptions().equals(signature2.getOptions())).isTrue();
    for (int i = 0; i < args1.size(); i++) {
      FunctionArgumentTypeTest.checkEquals(args1.get(i), args2.get(i));
    }
  }


  @Test
  public void testClassAndProtoSize() {
    assertWithMessage(
            "The number of fields of SignatureProto has changed, "
                + "please also update the serialization code accordingly.")
        .that(FunctionSignatureProto.getDescriptor().getFields())
        .hasSize(4);
    assertWithMessage(
            "The number of fields in FunctionSignature class has changed, "
                + "please also update the proto and serialization code accordingly.")
        .that(TestUtil.getNonStaticFieldCount(FunctionSignature.class))
        .isEqualTo(6);
  }
}
