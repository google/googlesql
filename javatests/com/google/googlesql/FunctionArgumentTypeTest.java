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

import com.google.googlesql.FunctionArgumentType.FunctionArgumentTypeOptions;
import com.google.googlesql.FunctionProtos.FunctionArgumentTypeOptionsProto;
import com.google.googlesql.FunctionProtos.FunctionArgumentTypeProto;
import com.google.googlesql.GoogleSQLFunctions.FunctionEnums;
import com.google.googlesql.GoogleSQLFunctions.FunctionEnums.ArgumentCardinality;
import com.google.googlesql.GoogleSQLFunctions.FunctionEnums.NamedArgumentKind;
import com.google.googlesql.GoogleSQLFunctions.FunctionEnums.ProcedureArgumentMode;
import com.google.googlesql.GoogleSQLFunctions.SignatureArgumentKind;
import com.google.googlesql.GoogleSQLType.TypeKind;
import com.google.googlesql.GoogleSQLType.TypeProto;

import java.util.ArrayList;
import java.util.List;
import org.junit.Test;
import org.junit.runner.RunWith;
import org.junit.runners.JUnit4;

@RunWith(JUnit4.class)

public class FunctionArgumentTypeTest {

  @Test
  public void testFixedType() {
    FunctionArgumentType fixedTypeInt32 =
        new FunctionArgumentType(TypeFactory.createSimpleType(TypeKind.TYPE_INT32));
    assertThat(fixedTypeInt32.isConcrete()).isFalse();
    assertThat(fixedTypeInt32.getType()).isNotNull();
    assertThat(fixedTypeInt32.isRepeated()).isFalse();
    assertThat(fixedTypeInt32.isOptional()).isFalse();
    assertThat(fixedTypeInt32.isRequired()).isTrue();
    assertThat(fixedTypeInt32.getNumOccurrences()).isEqualTo(-1);
    assertThat(fixedTypeInt32.debugString()).isEqualTo("INT32");

    FunctionArgumentType concreteFixedType =
        new FunctionArgumentType(
            TypeFactory.createSimpleType(TypeKind.TYPE_INT32), ArgumentCardinality.REQUIRED, 0);
    assertThat(concreteFixedType.isConcrete()).isTrue();
    assertThat(concreteFixedType.getType()).isNotNull();
    assertThat(concreteFixedType.isRepeated()).isFalse();
    assertThat(concreteFixedType.isOptional()).isFalse();
    assertThat(concreteFixedType.isRequired()).isTrue();
    assertThat(concreteFixedType.getNumOccurrences()).isEqualTo(0);
    assertThat(concreteFixedType.debugString()).isEqualTo("INT32");

    FunctionArgumentType repeatedFixedType =
        new FunctionArgumentType(
            TypeFactory.createSimpleType(TypeKind.TYPE_INT32), ArgumentCardinality.REPEATED, 1);
    assertThat(repeatedFixedType.isConcrete()).isTrue();
    assertThat(repeatedFixedType.getType()).isNotNull();
    assertThat(repeatedFixedType.isRepeated()).isTrue();
    assertThat(repeatedFixedType.isOptional()).isFalse();
    assertThat(repeatedFixedType.isRequired()).isFalse();
    assertThat(repeatedFixedType.getNumOccurrences()).isEqualTo(1);
    assertThat(repeatedFixedType.debugString()).isEqualTo("repeated(1) INT32");

    FunctionArgumentType optionalFixedType =
        new FunctionArgumentType(
            TypeFactory.createSimpleType(TypeKind.TYPE_INT32), ArgumentCardinality.OPTIONAL, 1);
    assertThat(optionalFixedType.isConcrete()).isTrue();
    assertThat(optionalFixedType.getType()).isNotNull();
    assertThat(optionalFixedType.isRepeated()).isFalse();
    assertThat(optionalFixedType.isOptional()).isTrue();
    assertThat(optionalFixedType.isRequired()).isFalse();
    assertThat(optionalFixedType.getNumOccurrences()).isEqualTo(1);
    assertThat(optionalFixedType.debugString()).isEqualTo("optional(1) INT32");

    FunctionArgumentType typeNull =
        new FunctionArgumentType((Type) null, ArgumentCardinality.REPEATED, -1);
    assertThat(typeNull.isConcrete()).isFalse();
    assertThat(typeNull.getType()).isNull();
    assertThat(typeNull.isRepeated()).isTrue();
    assertThat(typeNull.isOptional()).isFalse();
    assertThat(typeNull.isRequired()).isFalse();
    assertThat(typeNull.getNumOccurrences()).isEqualTo(-1);
    assertThat(typeNull.debugString()).isEqualTo("repeated FIXED");
  }

  // TODO: Break this test into multiple smaller unit tests.
  @Test
  public void testNotFixedType() {
    FunctionArgumentType arrayTypeAny1 =
        new FunctionArgumentType(
            SignatureArgumentKind.ARG_ARRAY_TYPE_ANY_1, ArgumentCardinality.REPEATED, 0);
    assertThat(arrayTypeAny1.isConcrete()).isFalse();
    assertThat(arrayTypeAny1.getType()).isNull();
    assertThat(arrayTypeAny1.isRepeated()).isTrue();
    assertThat(arrayTypeAny1.debugString()).isEqualTo("repeated <array<T1>>");

    FunctionArgumentType arrayTypeAny2 =
        new FunctionArgumentType(
            SignatureArgumentKind.ARG_ARRAY_TYPE_ANY_2, ArgumentCardinality.REQUIRED, 1);
    assertThat(arrayTypeAny2.isConcrete()).isFalse();
    assertThat(arrayTypeAny2.getType()).isNull();
    assertThat(arrayTypeAny2.isRepeated()).isFalse();
    assertThat(arrayTypeAny2.debugString()).isEqualTo("<array<T2>>");

    FunctionArgumentType enumAny =
        new FunctionArgumentType(
            SignatureArgumentKind.ARG_ENUM_ANY, ArgumentCardinality.OPTIONAL, -1);
    assertThat(enumAny.isConcrete()).isFalse();
    assertThat(enumAny.getType()).isNull();
    assertThat(enumAny.isRepeated()).isFalse();
    assertThat(enumAny.debugString()).isEqualTo("optional <enum>");

    FunctionArgumentType protoAny =
        new FunctionArgumentType(
            SignatureArgumentKind.ARG_PROTO_ANY, ArgumentCardinality.OPTIONAL, 3);
    assertThat(protoAny.isConcrete()).isFalse();
    assertThat(protoAny.getType()).isNull();
    assertThat(protoAny.isRepeated()).isFalse();
    assertThat(protoAny.debugString()).isEqualTo("optional <proto>");

    FunctionArgumentType structAny =
        new FunctionArgumentType(
            SignatureArgumentKind.ARG_STRUCT_ANY, ArgumentCardinality.OPTIONAL, 0);
    assertThat(structAny.isConcrete()).isFalse();
    assertThat(structAny.getType()).isNull();
    assertThat(structAny.isRepeated()).isFalse();
    assertThat(structAny.debugString()).isEqualTo("optional <struct>");

    FunctionArgumentType typeAny1 =
        new FunctionArgumentType(
            SignatureArgumentKind.ARG_TYPE_ANY_1, ArgumentCardinality.REQUIRED, 0);
    assertThat(typeAny1.isConcrete()).isFalse();
    assertThat(typeAny1.getType()).isNull();
    assertThat(typeAny1.isRepeated()).isFalse();
    assertThat(typeAny1.debugString()).isEqualTo("<T1>");

    FunctionArgumentType typeAny2 =
        new FunctionArgumentType(
            SignatureArgumentKind.ARG_TYPE_ANY_2, ArgumentCardinality.REQUIRED, 2);
    assertThat(typeAny2.isConcrete()).isFalse();
    assertThat(typeAny2.getType()).isNull();
    assertThat(typeAny2.isRepeated()).isFalse();
    assertThat(typeAny2.debugString()).isEqualTo("<T2>");

    FunctionArgumentType typeAny3 =
        new FunctionArgumentType(
            SignatureArgumentKind.ARG_TYPE_ANY_3, ArgumentCardinality.REQUIRED, 2);
    assertThat(typeAny3.isConcrete()).isFalse();
    assertThat(typeAny3.getType()).isNull();
    assertThat(typeAny3.isRepeated()).isFalse();
    assertThat(typeAny3.debugString()).isEqualTo("<T3>");

    FunctionArgumentType arrayTypeAny3 =
        new FunctionArgumentType(
            SignatureArgumentKind.ARG_ARRAY_TYPE_ANY_3, ArgumentCardinality.REQUIRED, 2);
    assertThat(arrayTypeAny3.isConcrete()).isFalse();
    assertThat(arrayTypeAny3.getType()).isNull();
    assertThat(arrayTypeAny3.isRepeated()).isFalse();
    assertThat(arrayTypeAny3.debugString()).isEqualTo("<array<T3>>");

    FunctionArgumentType typeAny4 =
        new FunctionArgumentType(
            SignatureArgumentKind.ARG_TYPE_ANY_4, ArgumentCardinality.REQUIRED, 2);
    assertThat(typeAny4.isConcrete()).isFalse();
    assertThat(typeAny4.getType()).isNull();
    assertThat(typeAny4.isRepeated()).isFalse();
    assertThat(typeAny4.debugString()).isEqualTo("<T4>");

    FunctionArgumentType arrayTypeAny4 =
        new FunctionArgumentType(
            SignatureArgumentKind.ARG_ARRAY_TYPE_ANY_4, ArgumentCardinality.REQUIRED, 2);
    assertThat(arrayTypeAny4.isConcrete()).isFalse();
    assertThat(arrayTypeAny4.getType()).isNull();
    assertThat(arrayTypeAny4.isRepeated()).isFalse();
    assertThat(arrayTypeAny4.debugString()).isEqualTo("<array<T4>>");

    FunctionArgumentType typeAny5 =
        new FunctionArgumentType(
            SignatureArgumentKind.ARG_TYPE_ANY_5, ArgumentCardinality.REQUIRED, 2);
    assertThat(typeAny5.isConcrete()).isFalse();
    assertThat(typeAny5.getType()).isNull();
    assertThat(typeAny5.isRepeated()).isFalse();
    assertThat(typeAny5.debugString()).isEqualTo("<T5>");

    FunctionArgumentType arrayTypeAny5 =
        new FunctionArgumentType(
            SignatureArgumentKind.ARG_ARRAY_TYPE_ANY_5, ArgumentCardinality.REQUIRED, 2);
    assertThat(arrayTypeAny5.isConcrete()).isFalse();
    assertThat(arrayTypeAny5.getType()).isNull();
    assertThat(arrayTypeAny5.isRepeated()).isFalse();
    assertThat(arrayTypeAny5.debugString()).isEqualTo("<array<T5>>");

    FunctionArgumentType typeArbitrary =
        new FunctionArgumentType(
            SignatureArgumentKind.ARG_TYPE_ARBITRARY, ArgumentCardinality.REQUIRED, -1);
    assertThat(typeArbitrary.isConcrete()).isFalse();
    assertThat(typeArbitrary.getType()).isNull();
    assertThat(typeArbitrary.isRepeated()).isFalse();
    assertThat(typeArbitrary.debugString()).isEqualTo("ANY TYPE");

    FunctionArgumentType typeUnknown =
        new FunctionArgumentType(
            SignatureArgumentKind.__SignatureArgumentKind__switch_must_have_a_default__,
            ArgumentCardinality.REPEATED,
            1);
    assertThat(typeUnknown.isConcrete()).isFalse();
    assertThat(typeUnknown.getType()).isNull();
    assertThat(typeUnknown.isRepeated()).isTrue();
    assertThat(typeUnknown.debugString()).isEqualTo("repeated UNKNOWN_ARG_KIND");
  }

  @Test
  public void testNotFixedGraphTypes() {
    FunctionArgumentType graphNode =
        new FunctionArgumentType(
            SignatureArgumentKind.ARG_TYPE_GRAPH_NODE, ArgumentCardinality.REPEATED, 0);
    assertThat(graphNode.isConcrete()).isFalse();
    assertThat(graphNode.getType()).isNull();
    assertThat(graphNode.isRepeated()).isTrue();
    assertThat(graphNode.debugString()).isEqualTo("repeated <graph_node>");

    FunctionArgumentType graphEdge =
        new FunctionArgumentType(
            SignatureArgumentKind.ARG_TYPE_GRAPH_EDGE, ArgumentCardinality.REPEATED, 0);
    assertThat(graphEdge.isConcrete()).isFalse();
    assertThat(graphEdge.getType()).isNull();
    assertThat(graphEdge.isRepeated()).isTrue();
    assertThat(graphEdge.debugString()).isEqualTo("repeated <graph_edge>");

    FunctionArgumentType graphElement =
        new FunctionArgumentType(
            SignatureArgumentKind.ARG_TYPE_GRAPH_ELEMENT, ArgumentCardinality.REPEATED, 0);
    assertThat(graphElement.isConcrete()).isFalse();
    assertThat(graphElement.getType()).isNull();
    assertThat(graphElement.isRepeated()).isTrue();
    assertThat(graphElement.debugString()).isEqualTo("repeated <graph_element>");

    FunctionArgumentType graphPath =
        new FunctionArgumentType(
            SignatureArgumentKind.ARG_TYPE_GRAPH_PATH, ArgumentCardinality.REPEATED, 0);
    assertThat(graphPath.isConcrete()).isFalse();
    assertThat(graphPath.getType()).isNull();
    assertThat(graphPath.isRepeated()).isTrue();
    assertThat(graphPath.debugString()).isEqualTo("repeated <graph_path>");
  }

  @Test
  public void testLambdaArgumentType() {
    SimpleType boolType = TypeFactory.createSimpleType(TypeKind.TYPE_BOOL);
    FunctionArgumentType boolArgType = new FunctionArgumentType(boolType);
    SimpleType int64Type = TypeFactory.createSimpleType(TypeKind.TYPE_INT64);
    FunctionArgumentType int64ArgType = new FunctionArgumentType(int64Type);
    FunctionArgumentType t1ArgType = new FunctionArgumentType(SignatureArgumentKind.ARG_TYPE_ANY_1);
    List<FunctionArgumentType> lambdaArgs = new ArrayList<>();
    FunctionArgumentType lambdaArg = new FunctionArgumentType(lambdaArgs, boolArgType);
    assertThat(lambdaArg.debugString()).isEqualTo(" FUNCTION<()->BOOL>");
    assertThat(lambdaArg.getKind()).isEqualTo(SignatureArgumentKind.ARG_TYPE_LAMBDA);
    assertThat(lambdaArg.getType()).isNull();
    checkSerializeAndDeserialize(lambdaArg);

    lambdaArgs.add(t1ArgType);
    lambdaArg = new FunctionArgumentType(lambdaArgs, boolArgType);
    assertThat(lambdaArg.debugString()).isEqualTo(" FUNCTION<<T1>->BOOL>");
    assertThat(lambdaArg.getKind()).isEqualTo(SignatureArgumentKind.ARG_TYPE_LAMBDA);
    assertThat(lambdaArg.getType()).isNull();
    checkSerializeAndDeserialize(lambdaArg);

    lambdaArgs.add(int64ArgType);
    lambdaArg = new FunctionArgumentType(lambdaArgs, boolArgType);
    assertThat(lambdaArg.debugString()).isEqualTo(" FUNCTION<(<T1>, INT64)->BOOL>");
    assertThat(lambdaArg.getKind()).isEqualTo(SignatureArgumentKind.ARG_TYPE_LAMBDA);
    assertThat(lambdaArg.getType()).isNull();
    checkSerializeAndDeserialize(lambdaArg);

    lambdaArg =
        new FunctionArgumentType(
            lambdaArgs,
            boolArgType,
            FunctionArgumentTypeOptions.builder()
                .setArgumentName("mylambda")
                .setNamedArgumentKind(NamedArgumentKind.NAMED_ONLY)
                .build());
    assertThat(lambdaArg.debugString()).isEqualTo(" FUNCTION<(<T1>, INT64)->BOOL> mylambda");
    assertThat(lambdaArg.getKind()).isEqualTo(SignatureArgumentKind.ARG_TYPE_LAMBDA);
    assertThat(lambdaArg.getType()).isNull();
    assertThat(lambdaArg.getOptions()).isNotNull();
    assertThat(lambdaArg.getOptions().getArgumentName()).isEqualTo("mylambda");
    checkSerializeAndDeserialize(lambdaArg);
  }

  @Test
  public void testSerializationAndDeserializationOfFunctionArgumentType() {
    FunctionArgumentType arrayTypeAny1 =
        new FunctionArgumentType(
            SignatureArgumentKind.ARG_ARRAY_TYPE_ANY_1, ArgumentCardinality.REPEATED, 0);
    FunctionArgumentType typeFixed1 =
        new FunctionArgumentType(
            TypeFactory.createSimpleType(TypeKind.TYPE_INT32), ArgumentCardinality.REPEATED, 0);
    TypeFactory factory = TypeFactory.nonUniqueNames();
    FunctionArgumentType typeFixed2 =
        new FunctionArgumentType(
            factory.createProtoType(TypeProto.class), ArgumentCardinality.REPEATED, 0);

    checkSerializeAndDeserialize(arrayTypeAny1);
    checkSerializeAndDeserialize(typeFixed1);
    checkSerializeAndDeserialize(typeFixed2);
  }

  private static void checkSerializeAndDeserialize(FunctionArgumentType functionArgumentType) {
    FileDescriptorSetsBuilder fileDescriptorSetsBuilder = new FileDescriptorSetsBuilder();
    checkEquals(
        functionArgumentType,
        FunctionArgumentType.deserialize(
            functionArgumentType.serialize(fileDescriptorSetsBuilder),
            fileDescriptorSetsBuilder.getDescriptorPools()));
    assertThat(functionArgumentType.serialize(fileDescriptorSetsBuilder))
        .isEqualTo(
            FunctionArgumentType.deserialize(
                    functionArgumentType.serialize(fileDescriptorSetsBuilder),
                    fileDescriptorSetsBuilder.getDescriptorPools())
                .serialize(fileDescriptorSetsBuilder));
  }

  @Test
  public void testSerializationAndDeserializationOfFunctionArgumentTypeOptions() {
    FunctionArgumentTypeOptions options =
        FunctionArgumentTypeOptions.builder()
            .setCardinality(ArgumentCardinality.REPEATED)
            .setMustBeConstant(true)
            .setMustBeNonNull(true)
            .setIsNotAggregate(true)
            .setMustSupportEquality(true)
            .setMustSupportOrdering(true)
            .setMustSupportGrouping(true)
            .setArrayElementMustSupportEquality(true)
            .setArrayElementMustSupportOrdering(true)
            .setArrayElementMustSupportGrouping(true)
            .setMinValue(Long.MIN_VALUE)
            .setMaxValue(Long.MAX_VALUE)
            .setExtraRelationInputColumnsAllowed(true)
            .setRelationInputSchema(
                TVFRelation.createValueTableBased(
                    TypeFactory.createSimpleType(TypeKind.TYPE_INT64)))
            .setArgumentName("name", FunctionEnums.NamedArgumentKind.NAMED_ONLY)
            .setArgumentNameParseLocation(ParseLocationRange.create("filename1", 0, 1))
            .setArgumentTypeParseLocation(ParseLocationRange.create("fielname2", 2, 3))
            .setProcedureArgumentMode(ProcedureArgumentMode.INOUT)
            .setDescriptorResolutionTableOffset(1234)
            .setArgumentCollationMode(FunctionEnums.ArgumentCollationMode.AFFECTS_PROPAGATION)
            .setArgumentAliasKind(FunctionEnums.ArgumentAliasKind.ARGUMENT_ALIASED)
            .build();
    FileDescriptorSetsBuilder fileDescriptorSetsBuilder = new FileDescriptorSetsBuilder();
    assertThat(options)
        .isEqualTo(
            FunctionArgumentTypeOptions.deserialize(
                options.serialize(/* argType= */ null, fileDescriptorSetsBuilder),
                fileDescriptorSetsBuilder.getDescriptorPools(),
                /* argType= */ null,
                TypeFactory.nonUniqueNames()));
    assertThat(options.serialize(/* argType= */ null, fileDescriptorSetsBuilder))
        .isEqualTo(
            FunctionArgumentTypeOptions.deserialize(
                    options.serialize(/* argType= */ null, fileDescriptorSetsBuilder),
                    fileDescriptorSetsBuilder.getDescriptorPools(),
                    /* argType= */ null,
                    TypeFactory.nonUniqueNames())
                .serialize(/* argType= */ null, fileDescriptorSetsBuilder));

    FunctionArgumentTypeOptions optionsConstantExpression =
        FunctionArgumentTypeOptions.builder().setMustBeConstantExpression(true).build();
    assertThat(optionsConstantExpression)
        .isEqualTo(
            FunctionArgumentTypeOptions.deserialize(
                optionsConstantExpression.serialize(/* argType= */ null, fileDescriptorSetsBuilder),
                fileDescriptorSetsBuilder.getDescriptorPools(),
                /* argType= */ null,
                TypeFactory.nonUniqueNames()));
    assertThat(optionsConstantExpression.serialize(/* argType= */ null, fileDescriptorSetsBuilder))
        .isEqualTo(
            FunctionArgumentTypeOptions.deserialize(
                    optionsConstantExpression.serialize(
                        /* argType= */ null, fileDescriptorSetsBuilder),
                    fileDescriptorSetsBuilder.getDescriptorPools(),
                    /* argType= */ null,
                    TypeFactory.nonUniqueNames())
                .serialize(/* argType= */ null, fileDescriptorSetsBuilder));

    FunctionArgumentTypeOptions optionsAnalysisConstant =
        FunctionArgumentTypeOptions.builder().setMustBeAnalysisConstant(true).build();
    assertThat(optionsAnalysisConstant)
        .isEqualTo(
            FunctionArgumentTypeOptions.deserialize(
                optionsAnalysisConstant.serialize(/* argType= */ null, fileDescriptorSetsBuilder),
                fileDescriptorSetsBuilder.getDescriptorPools(),
                /* argType= */ null,
                TypeFactory.nonUniqueNames()));
    assertThat(optionsAnalysisConstant.serialize(/* argType= */ null, fileDescriptorSetsBuilder))
        .isEqualTo(
            FunctionArgumentTypeOptions.deserialize(
                    optionsAnalysisConstant.serialize(
                        /* argType= */ null, fileDescriptorSetsBuilder),
                    fileDescriptorSetsBuilder.getDescriptorPools(),
                    /* argType= */ null,
                    TypeFactory.nonUniqueNames())
                .serialize(/* argType= */ null, fileDescriptorSetsBuilder));

    FunctionArgumentTypeOptions optionsImmutableConstant =
        FunctionArgumentTypeOptions.builder().setMustBeImmutableConstant(true).build();
    assertThat(optionsImmutableConstant)
        .isEqualTo(
            FunctionArgumentTypeOptions.deserialize(
                optionsImmutableConstant.serialize(/* argType= */ null, fileDescriptorSetsBuilder),
                fileDescriptorSetsBuilder.getDescriptorPools(),
                /* argType= */ null,
                TypeFactory.nonUniqueNames()));
    assertThat(optionsImmutableConstant.serialize(/* argType= */ null, fileDescriptorSetsBuilder))
        .isEqualTo(
            FunctionArgumentTypeOptions.deserialize(
                    optionsImmutableConstant.serialize(
                        /* argType= */ null, fileDescriptorSetsBuilder),
                    fileDescriptorSetsBuilder.getDescriptorPools(),
                    /* argType= */ null,
                    TypeFactory.nonUniqueNames())
                .serialize(/* argType= */ null, fileDescriptorSetsBuilder));
  }

  @Test
  public void testSettingSameConstnessLevelTwiceIsAllowed() {
    FunctionArgumentTypeOptions options1 =
        FunctionArgumentTypeOptions.builder().setMustBeAnalysisConstant(true).build();
    FunctionArgumentTypeOptions options2 =
        FunctionArgumentTypeOptions.builder()
            .setMustBeAnalysisConstant(true)
            .setMustBeAnalysisConstant(true)
            .build();
    assertThat(options2).isEqualTo(options1);

    FunctionArgumentTypeOptions options3 =
        FunctionArgumentTypeOptions.builder().setMustBeConstant(true).build();
    FunctionArgumentTypeOptions options4 =
        FunctionArgumentTypeOptions.builder()
            .setMustBeConstant(true)
            .setMustBeConstant(true)
            .build();
    assertThat(options4).isEqualTo(options3);

    FunctionArgumentTypeOptions options5 =
        FunctionArgumentTypeOptions.builder().setMustBeConstantExpression(true).build();
    FunctionArgumentTypeOptions options6 =
        FunctionArgumentTypeOptions.builder()
            .setMustBeConstantExpression(true)
            .setMustBeConstantExpression(true)
            .build();
    assertThat(options6).isEqualTo(options5);

    FunctionArgumentTypeOptions options7 =
        FunctionArgumentTypeOptions.builder().setMustBeImmutableConstant(true).build();
    FunctionArgumentTypeOptions options8 =
        FunctionArgumentTypeOptions.builder()
            .setMustBeImmutableConstant(true)
            .setMustBeImmutableConstant(true)
            .build();
    assertThat(options8).isEqualTo(options7);
  }

  @Test
  public void testSettingMultipleConstnessLevelsThrowsException() {
    // Test setting mustBeConstant then mustBeConstantExpression
    IllegalStateException e1 =
        assertThrows(
            IllegalStateException.class,
            () ->
                FunctionArgumentTypeOptions.builder()
                    .setMustBeConstant(true)
                    .setMustBeConstantExpression(true));
    assertThat(e1)
        .hasMessageThat()
        .contains(
            "Cannot set mustBeConstantExpression when another constness level is already set.");

    // Test setting mustBeConstantExpression then mustBeConstant
    IllegalStateException e2 =
        assertThrows(
            IllegalStateException.class,
            () ->
                FunctionArgumentTypeOptions.builder()
                    .setMustBeConstantExpression(true)
                    .setMustBeConstant(true));
    assertThat(e2)
        .hasMessageThat()
        .contains("Cannot set mustBeConstant when another constness level is already set.");

    // Test setting mustBeConstant then mustBeAnalysisConstant
    IllegalStateException e3 =
        assertThrows(
            IllegalStateException.class,
            () ->
                FunctionArgumentTypeOptions.builder()
                    .setMustBeConstant(true)
                    .setMustBeAnalysisConstant(true));
    assertThat(e3)
        .hasMessageThat()
        .contains("Cannot set mustBeAnalysisConstant when another constness level is already set.");

    // Test setting mustBeAnalysisConstant then mustBeImmutableConstant
    IllegalStateException e4 =
        assertThrows(
            IllegalStateException.class,
            () ->
                FunctionArgumentTypeOptions.builder()
                    .setMustBeAnalysisConstant(true)
                    .setMustBeImmutableConstant(true));
    assertThat(e4)
        .hasMessageThat()
        .contains(
            "Cannot set mustBeImmutableConstant when another constness level is already set.");
  }

  @Test
  @SuppressWarnings("deprecation") // Sets deprecated field for serialization compat.
  public void testSerializationAndDeserializationOfFunctionArgumentTypeOptionsWithLegacyFields() {
    FileDescriptorSetsBuilder fileDescriptorSetsBuilder = new FileDescriptorSetsBuilder();
    Type argType = null;
    FunctionArgumentTypeOptionsProto optionsProtoNameMandatoryUnset =
        FunctionArgumentTypeOptionsProto.newBuilder()
            .setCardinality(ArgumentCardinality.REQUIRED)
            .setArgumentName("argname")
            .build();

    // Deserialize a few proto shapes that use the deprecated "argument_name_is_mandatory" field in
    // different ways. This test is making sure we handle the deprecated field in expected ways.
    FunctionArgumentTypeOptions fromOptionsProtoNameMandatoryUnset =
        FunctionArgumentTypeOptions.deserialize(
            optionsProtoNameMandatoryUnset, fileDescriptorSetsBuilder.getDescriptorPools(),
            argType, TypeFactory.nonUniqueNames());
    FunctionArgumentTypeOptions fromOptionsProtoNameMandatoryFalse =
        FunctionArgumentTypeOptions.deserialize(
            optionsProtoNameMandatoryUnset.toBuilder().setArgumentNameIsMandatory(false).build(),
            fileDescriptorSetsBuilder.getDescriptorPools(),
            argType,
            TypeFactory.nonUniqueNames());
    FunctionArgumentTypeOptions fromOptionsProtoNameMandatoryTrue =
        FunctionArgumentTypeOptions.deserialize(
            optionsProtoNameMandatoryUnset.toBuilder().setArgumentNameIsMandatory(true).build(),
            fileDescriptorSetsBuilder.getDescriptorPools(),
            argType,
            TypeFactory.nonUniqueNames());
    FunctionArgumentTypeOptions fromOptionsProtoMandatoryWithoutName =
        FunctionArgumentTypeOptions.deserialize(
            optionsProtoNameMandatoryUnset.toBuilder()
                .clearArgumentName()
                .setArgumentNameIsMandatory(true)
                .build(),
            fileDescriptorSetsBuilder.getDescriptorPools(),
            argType,
            TypeFactory.nonUniqueNames());

    // Set up several options objects that have the shapes we expect to see in deserializations.
    FunctionArgumentTypeOptions optionsPositionalOrNamed =
        FunctionArgumentTypeOptions.builder()
            .setCardinality(ArgumentCardinality.REQUIRED)
            .setArgumentName("argname", FunctionEnums.NamedArgumentKind.POSITIONAL_OR_NAMED)
            .build();
    FunctionArgumentTypeOptions optionsNamed =
        FunctionArgumentTypeOptions.builder()
            .setCardinality(ArgumentCardinality.REQUIRED)
            .setArgumentName("argname", FunctionEnums.NamedArgumentKind.NAMED_ONLY)
            .build();
    FunctionArgumentTypeOptions optionsUnnamed =
        FunctionArgumentTypeOptions.builder().setCardinality(ArgumentCardinality.REQUIRED).build();

    assertThat(fromOptionsProtoNameMandatoryUnset).isEqualTo(optionsPositionalOrNamed);
    assertThat(fromOptionsProtoNameMandatoryFalse).isEqualTo(optionsPositionalOrNamed);
    assertThat(fromOptionsProtoNameMandatoryTrue).isEqualTo(optionsNamed);
    assertThat(fromOptionsProtoMandatoryWithoutName).isEqualTo(optionsUnnamed);
  }

  static void checkEquals(FunctionArgumentType type1, FunctionArgumentType type2) {
    assertThat(type2.getNumOccurrences()).isEqualTo(type1.getNumOccurrences());
    assertThat(type2.getCardinality()).isEqualTo(type1.getCardinality());
    assertThat(type2.getKind()).isEqualTo(type1.getKind());
    if (type1.isConcrete()) {
      assertThat(type1.getType()).isEqualTo(type2.getType());
    }
    if (type1.getOptions().getDefault() != null) {
      assertThat(type1.getOptions().getDefault()).isEqualTo(type2.getOptions().getDefault());
    } else {
      assertThat(type2.getOptions().getDefault()).isNull();
    }
  }

  @Test
  public void testClassAndProtoSize() {
    assertWithMessage(
            "The number of fields of FunctionArgumentTypeProto has changed, "
                + "please also update the serialization code accordingly.")
        .that(FunctionArgumentTypeProto.getDescriptor().getFields())
        .hasSize(5);
    assertWithMessage(
            "The number of fields in FunctionArgumentType class has changed, "
                + "please also update the proto and serialization code accordingly.")
        .that(TestUtil.getNonStaticFieldCount(FunctionArgumentType.class))
        .isEqualTo(5);
  }

  @Test
  public void testDefaultValues() {
    assertThat(
            assertThrows(
                IllegalArgumentException.class,
                () ->
                    FunctionArgumentTypeOptions.builder()
                        .setCardinality(ArgumentCardinality.REQUIRED)
                        .setDefault(Value.createStringValue("abc"))
                        .build()))
        .hasMessageThat()
        .contains("Default value cannot be applied to a REQUIRED argument");

    assertThat(
            assertThrows(
                IllegalArgumentException.class,
                () ->
                    FunctionArgumentTypeOptions.builder()
                        .setCardinality(ArgumentCardinality.REPEATED)
                        .setDefault(Value.createDoubleValue(3.14))
                        .build()))
        .hasMessageThat()
        .contains("Default value cannot be applied to a REPEATED argument");

    FunctionArgumentTypeOptions validOptionalArgTypeOption =
        FunctionArgumentTypeOptions.builder()
            .setCardinality(ArgumentCardinality.OPTIONAL)
            .setDefault(Value.createInt32Value(10086))
            .build();
    FunctionArgumentTypeOptions validOptionalArgTypeOptionNull =
        FunctionArgumentTypeOptions.builder()
            .setCardinality(ArgumentCardinality.OPTIONAL)
            .setDefault(Value.createSimpleNullValue(TypeKind.TYPE_INT32))
            .build();

    assertThat(
            assertThrows(
                IllegalArgumentException.class,
                () ->
                    new FunctionArgumentType(
                        TypeFactory.createSimpleType(TypeKind.TYPE_BYTES),
                        validOptionalArgTypeOption,
                        /* numOccurrences= */ 1)))
        .hasMessageThat()
        .contains("Default value type does not match the argument type");

    assertThat(
            assertThrows(
                IllegalArgumentException.class,
                () ->
                    new FunctionArgumentType(
                        TypeFactory.createSimpleType(TypeKind.TYPE_INT64),
                        validOptionalArgTypeOption,
                        /* numOccurrences= */ 1)))
        .hasMessageThat()
        .contains("Default value type does not match the argument type");

    assertThat(
            assertThrows(
                IllegalArgumentException.class,
                () ->
                    new FunctionArgumentType(
                        TypeFactory.createSimpleType(TypeKind.TYPE_INT64),
                        validOptionalArgTypeOptionNull,
                        /* numOccurrences= */ 1)))
        .hasMessageThat()
        .contains("Default value type does not match the argument type");

    FunctionArgumentType optionalFixedTypeInt32 =
        new FunctionArgumentType(
            TypeFactory.createSimpleType(TypeKind.TYPE_INT32),
            validOptionalArgTypeOption,
            /* numOccurrences= */ 1);
    checkSerializeAndDeserialize(optionalFixedTypeInt32);

    FunctionArgumentType optionalFixedTypeInt32Null =
        new FunctionArgumentType(
            TypeFactory.createSimpleType(TypeKind.TYPE_INT32),
            validOptionalArgTypeOptionNull,
            /* numOccurrences= */ 1);
    checkSerializeAndDeserialize(optionalFixedTypeInt32Null);

    FunctionArgumentType templatedTypeNonNull =
        new FunctionArgumentType(
            SignatureArgumentKind.ARG_TYPE_ANY_1,
            validOptionalArgTypeOption,
            /* numOccurrences= */ 1);
    checkSerializeAndDeserialize(templatedTypeNonNull);

    FunctionArgumentType templatedTypeNull =
        new FunctionArgumentType(
            SignatureArgumentKind.ARG_TYPE_ANY_1,
            validOptionalArgTypeOptionNull,
            /* numOccurrences= */ 1);
    checkSerializeAndDeserialize(templatedTypeNull);

    assertThat(
            assertThrows(
                IllegalArgumentException.class,
                () ->
                    new FunctionArgumentType(
                        SignatureArgumentKind.ARG_TYPE_RELATION,
                        validOptionalArgTypeOptionNull,
                        /* numOccurrences= */ 1)))
        .hasMessageThat()
        .contains("ANY TABLE argument cannot have a default value");

    assertThat(
            assertThrows(
                IllegalArgumentException.class,
                () ->
                    new FunctionArgumentType(
                        SignatureArgumentKind.ARG_TYPE_VOID,
                        validOptionalArgTypeOptionNull,
                        /* numOccurrences= */ 1)))
        .hasMessageThat()
        .contains("<void> argument cannot have a default value");

    assertThat(
            assertThrows(
                IllegalArgumentException.class,
                () ->
                    new FunctionArgumentType(
                        SignatureArgumentKind.ARG_TYPE_MODEL,
                        validOptionalArgTypeOptionNull,
                        /* numOccurrences= */ 1)))
        .hasMessageThat()
        .contains("ANY MODEL argument cannot have a default value");

    assertThat(
            assertThrows(
                IllegalArgumentException.class,
                () ->
                    new FunctionArgumentType(
                        SignatureArgumentKind.ARG_TYPE_CONNECTION,
                        validOptionalArgTypeOptionNull,
                        /* numOccurrences= */ 1)))
        .hasMessageThat()
        .contains("ANY CONNECTION argument cannot have a default value");

    assertThat(
            assertThrows(
                IllegalArgumentException.class,
                () ->
                    new FunctionArgumentType(
                        SignatureArgumentKind.ARG_TYPE_DESCRIPTOR,
                        validOptionalArgTypeOptionNull,
                        /* numOccurrences= */ 1)))
        .hasMessageThat()
        .contains("ANY DESCRIPTOR argument cannot have a default value");

    assertThat(
            assertThrows(
                IllegalArgumentException.class,
                () ->
                    new FunctionArgumentType(
                        SignatureArgumentKind.ARG_TYPE_LAMBDA,
                        validOptionalArgTypeOptionNull,
                        /* numOccurrences= */ 1)))
        .hasMessageThat()
        .contains("<function<T->T>> argument cannot have a default value");
  }

  @Test
  public void testFunctionArgumentOptionsTypeConstraint() {
    FunctionArgumentTypeOptions supportsOrderingOption =
        FunctionArgumentTypeOptions.builder().setMustSupportOrdering(true).build();
    FunctionArgumentTypeOptions supportsEqualityOption =
        FunctionArgumentTypeOptions.builder().setMustSupportEquality(true).build();
    FunctionArgumentTypeOptions supportsGroupingOption =
        FunctionArgumentTypeOptions.builder().setMustSupportGrouping(true).build();
    FunctionArgumentTypeOptions supportsElementOrderingOption =
        FunctionArgumentTypeOptions.builder().setArrayElementMustSupportOrdering(true).build();
    FunctionArgumentTypeOptions supportsElementEqualityOption =
        FunctionArgumentTypeOptions.builder().setArrayElementMustSupportEquality(true).build();
    FunctionArgumentTypeOptions supportsElementGroupingOption =
        FunctionArgumentTypeOptions.builder().setArrayElementMustSupportGrouping(true).build();

    FunctionArgumentType supportOrderingType =
        new FunctionArgumentType(
            SignatureArgumentKind.ARG_TYPE_ANY_1, supportsOrderingOption, /* numOccurrences= */ 1);
    checkSerializeAndDeserialize(supportOrderingType);
    FunctionArgumentType supportEqualityType =
        new FunctionArgumentType(
            SignatureArgumentKind.ARG_TYPE_ANY_1, supportsEqualityOption, /* numOccurrences= */ 1);
    checkSerializeAndDeserialize(supportEqualityType);
    FunctionArgumentType supportGroupingType =
        new FunctionArgumentType(
            SignatureArgumentKind.ARG_TYPE_ANY_1, supportsGroupingOption, /* numOccurrences= */ 1);
    checkSerializeAndDeserialize(supportGroupingType);

    FunctionArgumentType supportElementOrderingType =
        new FunctionArgumentType(
            SignatureArgumentKind.ARG_ARRAY_TYPE_ANY_1,
            supportsElementOrderingOption,
            /* numOccurrences= */ 1);
    checkSerializeAndDeserialize(supportElementOrderingType);
    FunctionArgumentType supportElementEqualityType =
        new FunctionArgumentType(
            SignatureArgumentKind.ARG_ARRAY_TYPE_ANY_1,
            supportsElementEqualityOption,
            /* numOccurrences= */ 1);
    checkSerializeAndDeserialize(supportElementEqualityType);
    FunctionArgumentType supportElementGroupingType =
        new FunctionArgumentType(
            SignatureArgumentKind.ARG_ARRAY_TYPE_ANY_1,
            supportsElementGroupingOption,
            /* numOccurrences= */ 1);
    checkSerializeAndDeserialize(supportElementGroupingType);
  }

  @Test
  public void testSetMustBeImmutableConstantFalse() {
    FunctionArgumentTypeOptions options =
        FunctionArgumentTypeOptions.builder().setMustBeImmutableConstant(false).build();
    assertThat(options.getMustBeImmutableConstant()).isFalse();

    FunctionArgumentTypeOptions options2 =
        FunctionArgumentTypeOptions.builder()
            .setMustBeImmutableConstant(true)
            .setMustBeImmutableConstant(false)
            .build();
    assertThat(options2.getMustBeImmutableConstant()).isFalse();

    FunctionArgumentTypeOptions options3 =
        FunctionArgumentTypeOptions.builder()
            .setMustBeConstant(true)
            .setMustBeImmutableConstant(false)
            .build();
    assertThat(options3.getMustBeConstant()).isFalse();
    assertThat(options3.getMustBeImmutableConstant()).isFalse();
  }
}
