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

package com.google.googlesql.resolvedast;

import static com.google.common.truth.Truth.assertThat;
import static com.google.common.truth.Truth.assertWithMessage;

import com.google.googlesql.DescriptorPool.GoogleSQLFieldDescriptor;
import com.google.googlesql.DescriptorPool.GoogleSQLOneofDescriptor;
import com.google.googlesql.FieldDescriptorRefProto;
import com.google.googlesql.FileDescriptorSetsBuilder;
import com.google.googlesql.Function;
import com.google.googlesql.FunctionArgumentType;
import com.google.googlesql.FunctionProtos.FunctionSignatureOptionsProto;
import com.google.googlesql.FunctionProtos.FunctionSignatureProto;
import com.google.googlesql.FunctionRefProto;
import com.google.googlesql.FunctionSignature;
import com.google.googlesql.GoogleSQLFunctions.FunctionEnums.ArgumentCardinality;
import com.google.googlesql.GoogleSQLFunctions.FunctionEnums.Mode;
import com.google.googlesql.GoogleSQLFunctions.SignatureArgumentKind;
import com.google.googlesql.GoogleSQLType.ProtoTypeProto;
import com.google.googlesql.GoogleSQLType.TypeKind;
import com.google.googlesql.GoogleSQLValue.ValueProto;
import com.google.googlesql.OneofDescriptorRefProto;
import com.google.googlesql.Procedure;
import com.google.googlesql.ProcedureRefProto;
import com.google.googlesql.ResolvedColumnProto;
import com.google.googlesql.ResolvedNodeProto;
import com.google.googlesql.SimpleCatalog;
import com.google.googlesql.SimpleTable;
import com.google.googlesql.Table;
import com.google.googlesql.TableRefProto;
import com.google.googlesql.TestAccess;
import com.google.googlesql.TestUtil;
import com.google.googlesql.TypeFactory;


import java.util.ArrayList;
import java.util.List;
import org.junit.Test;
import org.junit.runner.RunWith;
import org.junit.runners.JUnit4;

@RunWith(JUnit4.class)

public class DeserializationHelperTest {

  @Test
  public void testDeserializeFieldDescriptor() {
    TypeFactory factory = TypeFactory.nonUniqueNames();
    SimpleCatalog catalog = new SimpleCatalog("foo", factory);
    catalog.addType("GoogleSQLValue", factory.createProtoType(ValueProto.class));

    FileDescriptorSetsBuilder fileDescriptorSetsBuilder = new FileDescriptorSetsBuilder();
    catalog.serialize(fileDescriptorSetsBuilder);

    FieldDescriptorRefProto fieldRef =
        FieldDescriptorRefProto.newBuilder()
            .setNumber(ValueProto.DOUBLE_VALUE_FIELD_NUMBER)
            .setContainingProto(ProtoTypeProto.newBuilder().setProtoName("googlesql.ValueProto"))
            .build();

    DeserializationHelper helper =
        new DeserializationHelper(
            factory, TestAccess.getDescriptorPools(fileDescriptorSetsBuilder), catalog);
    GoogleSQLFieldDescriptor field = helper.deserialize(fieldRef);
    assertThat(field).isNotNull();
    assertThat(field.getDescriptor().getName()).isEqualTo("double_value");

    assertThat(TestAccess.getDescriptorPools(fileDescriptorSetsBuilder))
        .containsExactly(field.getDescriptorPool());
  }

  @Test
  public void testDeserializeOneofDescriptor() {
    TypeFactory factory = TypeFactory.nonUniqueNames();
    SimpleCatalog catalog = new SimpleCatalog("foo", factory);
    catalog.addType("GoogleSQLValue", factory.createProtoType(ValueProto.class));

    FileDescriptorSetsBuilder fileDescriptorSetsBuilder = new FileDescriptorSetsBuilder();
    catalog.serialize(fileDescriptorSetsBuilder);

    OneofDescriptorRefProto oneofRef =
        OneofDescriptorRefProto.newBuilder()
            .setIndex(0)
            .setContainingProto(ProtoTypeProto.newBuilder().setProtoName("googlesql.ValueProto"))
            .build();

    DeserializationHelper helper =
        new DeserializationHelper(
            factory, TestAccess.getDescriptorPools(fileDescriptorSetsBuilder), catalog);
    GoogleSQLOneofDescriptor field = helper.deserialize(oneofRef);
    assertThat(field).isNotNull();
    assertThat(field.getDescriptor().getName()).isEqualTo("value");
    assertThat(field.getDescriptor().getIndex()).isEqualTo(0);
    assertThat(TestAccess.getDescriptorPools(fileDescriptorSetsBuilder))
        .containsExactly(field.getDescriptorPool());
  }

  @Test
  public void testDeserializeTable() {
    TypeFactory factory = TypeFactory.nonUniqueNames();
    SimpleCatalog catalog = new SimpleCatalog("foo", factory);
    SimpleTable simpleTable = new SimpleTable("bar");
    catalog.addSimpleTable(simpleTable);

    FileDescriptorSetsBuilder fileDescriptorSetsBuilder = new FileDescriptorSetsBuilder();
    catalog.serialize(fileDescriptorSetsBuilder);

    DeserializationHelper helper =
        new DeserializationHelper(
            factory, TestAccess.getDescriptorPools(fileDescriptorSetsBuilder), catalog);
    TableRefProto tableRef =
        TableRefProto.newBuilder()
            .setSerializationId(simpleTable.getId())
            .setName(simpleTable.getName())
            .build();
    Table table = helper.deserialize(tableRef);
    assertThat(table).isEqualTo(simpleTable);
  }

  @Test
  public void testDeserializeFunction() {
    TypeFactory factory = TypeFactory.nonUniqueNames();
    SimpleCatalog catalog = new SimpleCatalog("foo", factory);
    Function fn = new Function("test_function_name", "GoogleSQL", Mode.SCALAR);
    catalog.addFunction(fn);

    FileDescriptorSetsBuilder fileDescriptorSetsBuilder = new FileDescriptorSetsBuilder();
    catalog.serialize(fileDescriptorSetsBuilder);

    DeserializationHelper helper =
        new DeserializationHelper(
            factory, TestAccess.getDescriptorPools(fileDescriptorSetsBuilder), catalog);
    FunctionRefProto functionRef = FunctionRefProto.newBuilder().setName(fn.getFullName()).build();
    Function function = helper.deserialize(functionRef);
    assertThat(function).isEqualTo(fn);
  }

  @Test
  public void testDeserializeProcedure() {
    List<FunctionArgumentType> arguments = new ArrayList<>();
    FunctionArgumentType typeInt64 =
        new FunctionArgumentType(TypeFactory.createSimpleType(TypeKind.TYPE_INT64));
    arguments.add(typeInt64);
    FunctionSignature signature = new FunctionSignature(typeInt64, arguments, -1);

    TypeFactory factory = TypeFactory.nonUniqueNames();
    SimpleCatalog catalog = new SimpleCatalog("foo", factory);
    Procedure procedure = new Procedure("test_procedure_name", signature);
    catalog.addProcedure(procedure);

    FileDescriptorSetsBuilder fileDescriptorSetsBuilder = new FileDescriptorSetsBuilder();
    catalog.serialize(fileDescriptorSetsBuilder);

    DeserializationHelper helper =
        new DeserializationHelper(
            factory, TestAccess.getDescriptorPools(fileDescriptorSetsBuilder), catalog);
    ProcedureRefProto procedureRef =
        ProcedureRefProto.newBuilder().setName(procedure.getFullName()).build();
    Procedure deserializedProcedure = helper.deserialize(procedureRef);
    assertThat(deserializedProcedure).isEqualTo(procedure);
  }

  @Test
  public void testDeserializeFunctionSignature() {
    TypeFactory factory = TypeFactory.nonUniqueNames();
    SimpleCatalog catalog = new SimpleCatalog("foo", factory);
    FunctionSignatureOptionsProto options =
        FunctionSignatureOptionsProto.newBuilder().setIsDeprecated(true).build();
    List<FunctionArgumentType> arguments = new ArrayList<>();
    arguments.add(
        new FunctionArgumentType(
            SignatureArgumentKind.ARG_TYPE_ANY_1, ArgumentCardinality.REPEATED, -1));
    arguments.add(
        new FunctionArgumentType(
            SignatureArgumentKind.ARG_TYPE_ANY_2, ArgumentCardinality.REQUIRED, -1));
    arguments.add(
        new FunctionArgumentType(
            SignatureArgumentKind.ARG_TYPE_ANY_2, ArgumentCardinality.OPTIONAL, -1));
    arguments.add(
        new FunctionArgumentType(
            SignatureArgumentKind.ARG_TYPE_ANY_3, ArgumentCardinality.OPTIONAL, -1));
    arguments.add(
        new FunctionArgumentType(
            SignatureArgumentKind.ARG_ARRAY_TYPE_ANY_3, ArgumentCardinality.OPTIONAL, -1));
    arguments.add(
        new FunctionArgumentType(
            SignatureArgumentKind.ARG_TYPE_ANY_4, ArgumentCardinality.OPTIONAL, -1));
    arguments.add(
        new FunctionArgumentType(
            SignatureArgumentKind.ARG_ARRAY_TYPE_ANY_4, ArgumentCardinality.OPTIONAL, -1));
    arguments.add(
        new FunctionArgumentType(
            SignatureArgumentKind.ARG_TYPE_ANY_5, ArgumentCardinality.OPTIONAL, -1));
    arguments.add(
        new FunctionArgumentType(
            SignatureArgumentKind.ARG_ARRAY_TYPE_ANY_5, ArgumentCardinality.OPTIONAL, -1));
    FunctionSignature signature =
        new FunctionSignature(
            new FunctionArgumentType(SignatureArgumentKind.ARG_TYPE_ANY_2), arguments, -1, options);
    FileDescriptorSetsBuilder fileDescriptorSetsBuilder = new FileDescriptorSetsBuilder();
    signature.serialize(fileDescriptorSetsBuilder);
    DeserializationHelper helper =
        new DeserializationHelper(
            factory, TestAccess.getDescriptorPools(fileDescriptorSetsBuilder), catalog);
    FunctionSignatureProto functionSignature = signature.serialize(fileDescriptorSetsBuilder);
    FunctionSignature signature2 = helper.deserialize(functionSignature);
    assertThat(signature.serialize(fileDescriptorSetsBuilder))
        .isEqualTo(signature2.serialize(fileDescriptorSetsBuilder));
  }

  @Test
  public void testClassAndProtoSize() {
    assertWithMessage(
            "The number of fields of ResolvedNodeProto has changed, "
                + "please also update the serialization code accordingly.")
        .that(ResolvedNodeProto.getDescriptor().getFields())
        .hasSize(1);
    assertWithMessage(
            "The number of fields of ResolvedColumnProto has changed, "
                + "please also update the serialization code accordingly.")
        .that(ResolvedColumnProto.getDescriptor().getFields())
        .hasSize(5);
    assertWithMessage(
            "The number of fields in ResolvedColumn class has changed, "
                + "please also update the proto and serialization code accordingly.")
        .that(TestUtil.getNonStaticFieldCount(ResolvedColumn.class))
        .isEqualTo(4);
  }
}
