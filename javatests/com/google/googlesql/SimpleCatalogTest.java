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
import static org.junit.Assert.fail;

import com.google.common.collect.ImmutableList;
import com.google.common.collect.ImmutableMap;
import com.google.common.collect.ImmutableSet;
import com.google.common.testing.SerializableTester;
import com.google.common.truth.extensions.proto.ProtoTruth;
import com.google.protobuf.DescriptorProtos.DescriptorProto;
import com.google.protobuf.DescriptorProtos.FileDescriptorProto;
import com.google.protobuf.TextFormat;
import com.google.protobuf.TextFormat.ParseException;
import com.google.googlesql.FunctionProtos.FunctionOptionsProto;
import com.google.googlesql.FunctionProtos.TableValuedFunctionOptionsProto;
import com.google.googlesql.GoogleSQLFunction.FunctionSignatureId;
import com.google.googlesql.GoogleSQLFunctions.FunctionEnums.ArgumentCardinality;
import com.google.googlesql.GoogleSQLFunctions.FunctionEnums.Mode;
import com.google.googlesql.GoogleSQLFunctions.SignatureArgumentKind;
import com.google.googlesql.GoogleSQLOptions.LanguageFeature;
import com.google.googlesql.GoogleSQLOptionsProto.GoogleSQLBuiltinFunctionOptionsProto;
import com.google.googlesql.GoogleSQLType.TypeKind;
import com.google.googlesql.SimpleCatalog.AutoUnregister;
import com.google.googlesql.SimpleCatalogProtos.SimpleCatalogProto;
import com.google.googlesql.TableValuedFunction.ForwardInputSchemaToOutputSchemaTVF;
import com.google.googlesql.functions.GoogleSQLRoundingMode.RoundingMode;


import java.util.ArrayList;
import java.util.List;
import org.junit.Test;
import org.junit.runner.RunWith;
import org.junit.runners.JUnit4;

@RunWith(JUnit4.class)

public class SimpleCatalogTest {

  public static final FunctionArgumentType TABLE_TYPE =
      new FunctionArgumentType(SignatureArgumentKind.ARG_TYPE_RELATION);

  @Test
  public void testGets() {
    TypeFactory factory = TypeFactory.nonUniqueNames();
    SimpleCatalog catalog1 = new SimpleCatalog("catalog1");
    SimpleCatalog catalog2 = new SimpleCatalog("catalog2", factory);
    SimpleType type = TypeFactory.createSimpleType(TypeKind.TYPE_BOOL);
    SimpleColumn column = new SimpleColumn("t1", "c1", type);
    SimpleTable table = new SimpleTable("t1", ImmutableList.<SimpleColumn>of(column));
    Function function =
        new Function(
            "test_function_name",
            Function.GOOGLESQL_FUNCTION_GROUP_NAME,
            Mode.SCALAR,
            ImmutableList.of(
                new FunctionSignature(
                    new FunctionArgumentType(
                        TypeFactory.createSimpleType(TypeKind.TYPE_INT64),
                        ArgumentCardinality.REQUIRED),
                    ImmutableList.of(),
                    /* contextId= */ -1)));
    TableValuedFunction tvf =
        new TableValuedFunction(
            ImmutableList.of("test_tvf_name"),
            Function.GOOGLESQL_FUNCTION_GROUP_NAME,
            ImmutableList.of(
                new FunctionSignature(
                    TABLE_TYPE, ImmutableList.of(TABLE_TYPE), /* contextId= */ -1)),
            ImmutableList.of(),
            null,
            null,
            TableValuedFunctionOptionsProto.getDefaultInstance());
    List<FunctionArgumentType> arguments = new ArrayList<>();
    FunctionArgumentType typeInt64 =
        new FunctionArgumentType(TypeFactory.createSimpleType(TypeKind.TYPE_INT64));
    arguments.add(typeInt64);
    FunctionSignature signature = new FunctionSignature(typeInt64, arguments, /* contextId= */ -1);
    Procedure procedure = new Procedure("test_procedure_name", signature);
    SimpleConnection connection = new SimpleConnection("test_connection_name");
    ImmutableList<SimpleModel.NameAndType> modelInputs =
        ImmutableList.of(
            new SimpleModel.NameAndType(
                "input1", TypeFactory.createSimpleType(TypeKind.TYPE_INT64)),
            new SimpleModel.NameAndType(
                "input2", TypeFactory.createSimpleType(TypeKind.TYPE_STRING)));
    ImmutableList<SimpleModel.NameAndType> modelOutputs =
        ImmutableList.of(
            new SimpleModel.NameAndType(
                "output", TypeFactory.createSimpleType(TypeKind.TYPE_INT64)));
    SimpleModel model = new SimpleModel("test_model_name", modelInputs, modelOutputs);
    SimpleSequence sequence = new SimpleSequence("test_sequence_name");

    catalog1.addSimpleTable(table);
    catalog1.addType("type", type);
    catalog1.addSimpleCatalog(catalog2);
    catalog1.addFunction(function);
    catalog1.addTableValuedFunction(tvf);
    catalog1.addProcedure(procedure);
    catalog1.addConnection(connection);
    catalog1.addModel(model);
    catalog1.addSequence(sequence);

    assertThat(catalog1.getFullName()).isEqualTo("catalog1");
    assertThat(catalog2.getFullName()).isEqualTo("catalog2");

    assertThat(catalog1.getTypeFactory()).isNotNull();
    assertThat(catalog2.getTypeFactory()).isEqualTo(factory);

    assertThat(catalog1.getTableNameList().get(0)).isEqualTo("t1");
    assertThat(catalog1.getTableList().get(0)).isEqualTo(table);
    assertThat(catalog1.getTypeList().get(0)).isEqualTo(type);
    assertThat(catalog1.getCatalogList().get(0)).isEqualTo(catalog2);
    assertThat(catalog1.getFunctionList().get(0)).isEqualTo(function);
    assertThat(catalog1.getTVFList().get(0)).isEqualTo(tvf);
    assertThat(catalog1.getProcedureList().get(0)).isEqualTo(procedure);
    assertThat(catalog1.getFunctionNameList().get(0)).isEqualTo("googlesql:test_function_name");
    assertThat(catalog1.getConnectionList().get(0)).isEqualTo(connection);
    assertThat(catalog1.getModelList().get(0)).isEqualTo(model);

    assertThat(catalog1.getTable("t1")).isEqualTo(table);
    assertThat(catalog1.getTable("T1")).isEqualTo(table);
    assertThat(catalog1.getType("type")).isEqualTo(type);
    assertThat(catalog1.getType("TYpe")).isEqualTo(type);
    assertThat(catalog1.getCatalog("CataloG2")).isEqualTo(catalog2);
    assertThat(catalog1.getFunctionByFullName("GoogleSQL:test_function_name")).isEqualTo(function);
    assertThat(catalog1.getSequenceByFullName("test_sequence_name")).isEqualTo(sequence);
    assertThat(catalog1.getTvfByFullName("GoogleSQL:test_tvf_name")).isEqualTo(tvf);
    try {
      assertThat(catalog1.findProcedure(ImmutableList.of("test_procedure_name")))
          .isEqualTo(procedure);
    } catch (NotFoundException e) {
      fail();
    }
    assertThat(catalog1.getConnection("test_connection_name")).isEqualTo(connection);
    assertThat(catalog1.getConnectionByFullName("test_connection_name")).isEqualTo(connection);
    assertThat(catalog1.getModel("test_model_name")).isEqualTo(model);
    assertThat(catalog1.getModelById(model.getId())).isEqualTo(model);

    assertThat(catalog1.getTable("noTable")).isNull();
    assertThat(catalog1.getType("noType")).isNull();
    assertThat(catalog1.getCatalog("noCatalog")).isNull();
    assertThat(catalog1.getFunctionByFullName("nofunction")).isNull();
    assertThat(catalog1.getTvfByFullName("notvf")).isNull();
    assertThat(catalog1.getSequenceByFullName("nosquence")).isNull();
    assertThat(catalog1.getProcedure("noprocedure")).isNull();
    assertThat(catalog1.getConnection("noconnection")).isNull();
    assertThat(catalog1.getModel("nomodel")).isNull();
  }

  @Test
  public void testAdds() throws NotFoundException {
    TypeFactory factory = TypeFactory.nonUniqueNames();
    SimpleType type = TypeFactory.createSimpleType(TypeKind.TYPE_BOOL);
    SimpleColumn column = new SimpleColumn("t1", "c1", type);
    SimpleTable table = new SimpleTable("t1", ImmutableList.<SimpleColumn>of(column));
    SimpleCatalog catalog = new SimpleCatalog("catalog1");
    SimpleCatalog catalog2 = new SimpleCatalog("catalog2", factory);
    Function function =
        new Function(
            "test_function_name",
            Function.GOOGLESQL_FUNCTION_GROUP_NAME,
            Mode.SCALAR,
            ImmutableList.of(
                new FunctionSignature(
                    new FunctionArgumentType(
                        TypeFactory.createSimpleType(TypeKind.TYPE_INT64),
                        ArgumentCardinality.REQUIRED),
                    ImmutableList.of(),
                    /* contextId= */ -1)));
    TableValuedFunction tvf =
        new ForwardInputSchemaToOutputSchemaTVF(
            ImmutableList.of("test_tvf_name"),
            new FunctionSignature(TABLE_TYPE, ImmutableList.of(TABLE_TYPE), /* contextId= */ -1));
    List<FunctionArgumentType> arguments = new ArrayList<>();
    FunctionArgumentType typeInt64 =
        new FunctionArgumentType(TypeFactory.createSimpleType(TypeKind.TYPE_INT64));
    arguments.add(typeInt64);
    FunctionSignature signature = new FunctionSignature(typeInt64, arguments, /* contextId= */ -1);
    Procedure procedure = new Procedure("test_procedure_name", signature);
    SimpleConnection connection = new SimpleConnection("test_connection_name");
    ImmutableList<SimpleModel.NameAndType> modelInputs =
        ImmutableList.of(
            new SimpleModel.NameAndType(
                "input1", TypeFactory.createSimpleType(TypeKind.TYPE_INT64)),
            new SimpleModel.NameAndType(
                "input2", TypeFactory.createSimpleType(TypeKind.TYPE_STRING)));
    ImmutableList<SimpleModel.NameAndType> modelOutputs =
        ImmutableList.of(
            new SimpleModel.NameAndType(
                "output", TypeFactory.createSimpleType(TypeKind.TYPE_INT64)));
    SimpleModel model = new SimpleModel("test_model_name", modelInputs, modelOutputs);

    // Tests for adding tables
    catalog.addSimpleTable(table);
    assertThat(catalog.getTableList().contains(table)).isTrue();
    assertThat(catalog.getTableNameList().contains(table.getName().toLowerCase())).isTrue();
    try {
      catalog.addSimpleTable(table);
      fail();
    } catch (IllegalArgumentException expected) {
    }
    try {
      catalog.addSimpleTable(new SimpleTable("T1"));
      fail();
    } catch (IllegalArgumentException expected) {
    }

    // Tests for adding types
    catalog.addType(type.typeName(), type);
    assertThat(catalog.getTypeList().contains(type)).isTrue();
    try {
      catalog.addType(type.typeName(), type);
      fail();
    } catch (IllegalArgumentException expected) {
    }
    try {
      catalog.addType(type.typeName().toUpperCase(), type);
      fail();
    } catch (IllegalArgumentException expected) {
    }
    try {
      catalog.addType(type.typeName().toLowerCase(), type);
      fail();
    } catch (IllegalArgumentException expected) {
    }

    // Tests for adding catalogs
    catalog.addSimpleCatalog(catalog2);
    assertThat(catalog.getCatalogList().contains(catalog2)).isTrue();
    try {
      catalog.addSimpleCatalog(catalog2);
      fail();
    } catch (IllegalArgumentException expected) {
    }
    try {
      catalog.addSimpleCatalog(new SimpleCatalog("Catalog2"));
      fail();
    } catch (IllegalArgumentException expected) {
    }
    try {
      catalog.addSimpleCatalog(new SimpleCatalog("CATALOG2"));
      fail();
    } catch (IllegalArgumentException expected) {
    }

    catalog.addNewSimpleCatalog("catalog3");
    assertThat(catalog.getCatalog("catalog3")).isNotNull();
    try {
      catalog.addNewSimpleCatalog("catalog3");
      fail();
    } catch (IllegalArgumentException expected) {
    }
    try {
      catalog.addNewSimpleCatalog("cAtAlOg3");
      fail();
    } catch (IllegalArgumentException expected) {
    }

    // Tests for adding functions
    catalog.addFunction(function);
    assertThat(catalog.getFunctionByFullName("GoogleSQL:test_function_name")).isEqualTo(function);
    try {
      catalog.addFunction(
          new Function("Test_Function_Name", Function.GOOGLESQL_FUNCTION_GROUP_NAME, Mode.SCALAR));
      fail();
    } catch (IllegalArgumentException expected) {
    }
    try {
      catalog.addFunction(
          new Function(
              "another_test_function_name",
              Function.GOOGLESQL_FUNCTION_GROUP_NAME,
              Mode.SCALAR,
              new ArrayList<>(),
              FunctionOptionsProto.newBuilder().setAliasName("test_FUNCTION_name").build()));
      fail();
    } catch (IllegalArgumentException expected) {
    }

    // Tests for adding TVFs
    catalog.addTableValuedFunction(tvf);
    assertThat(catalog.getTvfByFullName("test_tvf_name")).isEqualTo(tvf);
    try {
      catalog.addTableValuedFunction(
          new TableValuedFunction.ForwardInputSchemaToOutputSchemaTVF(
              ImmutableList.of("test_tvf_name"),
              new FunctionSignature(
                  new FunctionArgumentType(
                      TypeFactory.createSimpleType(TypeKind.TYPE_INT64),
                      ArgumentCardinality.REQUIRED),
                  ImmutableList.of(),
                  /* contextId= */ -1)));
      fail();
    } catch (IllegalArgumentException expected) {
    }

    // Tests for adding procedure
    catalog.addProcedure(procedure);
    assertThat(catalog.findProcedure(ImmutableList.of("test_procedure_name"))).isEqualTo(procedure);
    try {
      catalog.addProcedure(new Procedure("Test_Procedure_Name", signature));
      fail();
    } catch (IllegalArgumentException expected) {
    }

    // Tests for adding connections
    catalog.addConnection(connection);
    assertThat(catalog.findConnection(ImmutableList.of("test_connection_name")))
        .isEqualTo(connection);
    try {
      catalog.addConnection(new SimpleConnection("Test_Connection_Name"));
      fail();
    } catch (IllegalArgumentException expected) {
    }

    // Tests for adding models
    catalog.addModel(model);
    assertThat(catalog.findModel(ImmutableList.of("test_model_name"))).isEqualTo(model);
    try {
      catalog.addModel(new SimpleModel("Test_Model_Name"));
      fail();
    } catch (IllegalArgumentException expected) {
    }
  }

  @Test
  public void testRemoves() {
    SimpleType type = TypeFactory.createSimpleType(TypeKind.TYPE_BOOL);
    SimpleColumn column = new SimpleColumn("t1", "c1", type);
    SimpleTable table = new SimpleTable("t1", ImmutableList.<SimpleColumn>of(column));
    SimpleCatalog catalog = new SimpleCatalog("catalog1");
    SimpleCatalog catalog2 = new SimpleCatalog("catalog2");
    Function function =
        new Function(
            "test_function_name",
            Function.GOOGLESQL_FUNCTION_GROUP_NAME,
            Mode.SCALAR,
            ImmutableList.of(
                new FunctionSignature(
                    new FunctionArgumentType(
                        TypeFactory.createSimpleType(TypeKind.TYPE_INT64),
                        ArgumentCardinality.REQUIRED),
                    ImmutableList.of(),
                    /* contextId= */ -1)));
    TableValuedFunction tvf =
        new ForwardInputSchemaToOutputSchemaTVF(
            ImmutableList.of("test_tvf_name"),
            new FunctionSignature(TABLE_TYPE, ImmutableList.of(TABLE_TYPE), /* contextId= */ -1));
    TableValuedFunction tvfWithGroupName =
        new TableValuedFunction(
            ImmutableList.of("tvf_name"),
            "group",
            ImmutableList.of(
                new FunctionSignature(
                    TABLE_TYPE, ImmutableList.of(TABLE_TYPE), /* contextId= */ -1)),
            ImmutableList.of(),
            null,
            null,
            TableValuedFunctionOptionsProto.getDefaultInstance());
    List<FunctionArgumentType> arguments = new ArrayList<>();
    FunctionArgumentType typeInt64 =
        new FunctionArgumentType(TypeFactory.createSimpleType(TypeKind.TYPE_INT64));
    arguments.add(typeInt64);
    FunctionSignature signature = new FunctionSignature(typeInt64, arguments, /* contextId= */ -1);
    Procedure procedure = new Procedure("test_procedure_name", signature);
    SimpleConnection connection = new SimpleConnection("test_connection_name");
    ImmutableList<SimpleModel.NameAndType> modelInputs =
        ImmutableList.of(
            new SimpleModel.NameAndType(
                "input1", TypeFactory.createSimpleType(TypeKind.TYPE_INT64)),
            new SimpleModel.NameAndType(
                "input2", TypeFactory.createSimpleType(TypeKind.TYPE_STRING)));
    ImmutableList<SimpleModel.NameAndType> modelOutputs =
        ImmutableList.of(
            new SimpleModel.NameAndType(
                "output", TypeFactory.createSimpleType(TypeKind.TYPE_INT64)));
    SimpleModel model = new SimpleModel("test_model_name", modelInputs, modelOutputs);

    // Tests for removing tables
    try {
      catalog.removeSimpleTable(table);
      fail();
    } catch (IllegalArgumentException expected) {
    }
    catalog.addSimpleTable(table);
    assertThat(catalog.getTableList().contains(table)).isTrue();
    catalog.removeSimpleTable("T1");
    assertThat(catalog.getTableList().contains(table)).isFalse();
    catalog.addSimpleTable(table);
    assertThat(catalog.getTableList().contains(table)).isTrue();

    // Tests for removing types
    try {
      catalog.removeType(type.typeName());
      fail();
    } catch (IllegalArgumentException expected) {
    }

    catalog.addType(type.typeName(), type);
    assertThat(catalog.getTypeList().contains(type)).isTrue();
    catalog.removeType("Bool");
    assertThat(catalog.getTypeList().contains(type)).isFalse();
    catalog.addType(type.typeName(), type);
    assertThat(catalog.getTypeList().contains(type)).isTrue();

    // Tests for removing catalogs
    try {
      catalog.removeSimpleCatalog(catalog2);
      fail();
    } catch (IllegalArgumentException expected) {
    }
    catalog.addSimpleCatalog(catalog2);
    assertThat(catalog.getCatalogList().contains(catalog2)).isTrue();
    catalog.removeSimpleCatalog("CaTaLoG2");
    assertThat(catalog.getCatalogList().contains(catalog2)).isFalse();
    catalog.addSimpleCatalog(catalog2);
    assertThat(catalog.getCatalogList().contains(catalog2)).isTrue();

    // Tests for removing functions
    try {
      catalog.removeFunction(function);
      fail();
    } catch (IllegalArgumentException expected) {
    }
    catalog.addFunction(function);
    assertThat(catalog.getFunctionByFullName("GoogleSQL:test_function_name")).isEqualTo(function);
    catalog.removeFunction("GoogleSQL:Test_Function_Name");
    assertThat(catalog.getFunctionByFullName("GoogleSQL:test_function_name")).isNull();
    catalog.addFunction(function);
    assertThat(catalog.getFunctionByFullName("GoogleSQL:test_function_name")).isEqualTo(function);

    // Tests for removing TVFs
    try {
      catalog.removeTableValuedFunction(tvf);
      fail();
    } catch (IllegalArgumentException expected) {
    }
    catalog.addTableValuedFunction(tvf);
    assertThat(catalog.getTvfByFullName("test_tvf_name")).isEqualTo(tvf);
    catalog.removeTableValuedFunction("test_tvf_name");
    assertThat(catalog.getTvfByFullName("test_tvf_name")).isNull();
    assertThat(catalog.getTVFNameList()).doesNotContain("test_tvf_name");
    catalog.addTableValuedFunction(tvf);
    assertThat(catalog.getTvfByFullName("test_tvf_name")).isEqualTo(tvf);
    catalog.addTableValuedFunction(tvfWithGroupName);
    assertThat(catalog.getTvfByFullName("group:tvf_name")).isEqualTo(tvfWithGroupName);
    catalog.removeTableValuedFunction("group:tvf_name");
    assertThat(catalog.getTvfByFullName("group:tvf_name")).isNull();
    assertThat(catalog.getTVFNameList()).doesNotContain("tvf_name");

    // Tests for removing procedures
    try {
      catalog.removeProcedure(procedure);
      fail();
    } catch (IllegalArgumentException expected) {
    }

    catalog.addProcedure(procedure);
    assertThat(catalog.getProcedureList().contains(procedure)).isTrue();
    catalog.removeProcedure("Test_Procedure_Name");
    assertThat(catalog.getProcedureList().contains(procedure)).isFalse();
    catalog.addProcedure(procedure);
    assertThat(catalog.getProcedureList().contains(procedure)).isTrue();

    // Tests for removing connections
    try {
      catalog.removeConnection(connection);
      fail();
    } catch (IllegalArgumentException expected) {
    }

    catalog.addConnection(connection);
    assertThat(catalog.getConnectionList()).contains(connection);
    catalog.removeConnection("Test_Connection_Name");
    assertThat(catalog.getConnectionList()).doesNotContain(connection);
    catalog.addConnection(connection);
    assertThat(catalog.getConnectionList()).contains(connection);
    catalog.removeConnection(connection);
    assertThat(catalog.getConnectionList()).doesNotContain(connection);

    // Tests for removing models
    try {
      catalog.removeModel(model);
      fail();
    } catch (IllegalArgumentException expected) {
    }

    catalog.addModel(model);
    assertThat(catalog.getModelList()).contains(model);
    catalog.removeModel("Test_Model_Name");
    assertThat(catalog.getModelList()).doesNotContain(model);
    catalog.addModel(model);
    assertThat(catalog.getModelList()).contains(model);
    catalog.removeModel(model);
    assertThat(catalog.getModelList()).doesNotContain(model);
  }

  @Test
  public void testAddBuiltinFunctions() {
    SimpleCatalog catalog1 = new SimpleCatalog("foo");
    catalog1.addGoogleSQLFunctions(new GoogleSQLBuiltinFunctionOptions());

    assertThat(catalog1.getFunctionList().size()).isGreaterThan(100);

    Function add = catalog1.getFunctionByFullName("GoogleSQL:$add");
    assertThat(add).isNotNull();
    assertThat(add.getFullName()).isEqualTo("GoogleSQL:$add");
    assertThat(add.getSignatureList().size()).isGreaterThan(0);

    Function ceil = catalog1.getFunctionByFullName("GoogleSQL:ceil");
    assertThat(ceil).isNotNull();
    assertThat(ceil.getFullName()).isEqualTo("GoogleSQL:ceil");
    assertThat(ceil.getSignatureList().size()).isGreaterThan(0);

    SimpleCatalog catalog2 = new SimpleCatalog("foo2");
    LanguageOptions languageOptions = new LanguageOptions();
    languageOptions.setEnabledLanguageFeatures(
        ImmutableSet.of(LanguageFeature.FEATURE_ANALYTIC_FUNCTIONS));
    GoogleSQLBuiltinFunctionOptionsProto builtinFunctionOptionsproto =
        GoogleSQLBuiltinFunctionOptionsProto.newBuilder()
            .setLanguageOptions(languageOptions.serialize())
            .addIncludeFunctionIds(FunctionSignatureId.FN_EQUAL)
            .addIncludeFunctionIds(FunctionSignatureId.FN_ANY_VALUE)
            .addIncludeFunctionIds(FunctionSignatureId.FN_RANK)
            .addExcludeFunctionIds(FunctionSignatureId.FN_ABS_DOUBLE)
            .addExcludeFunctionIds(FunctionSignatureId.FN_ANY_VALUE)
            .build();
    catalog2.addGoogleSQLFunctions(
        new GoogleSQLBuiltinFunctionOptions(builtinFunctionOptionsproto));

    assertThat(catalog2.getFunctionList().size()).isGreaterThan(0);

    // Included
    Function equal = catalog2.getFunctionByFullName("GoogleSQL:$equal");
    assertThat(equal).isNotNull();
    assertThat(equal.getFullName()).isEqualTo("GoogleSQL:$equal");
    assertThat(!equal.getSignatureList().isEmpty()).isTrue();
    Function rank = catalog2.getFunctionByFullName("GoogleSQL:rank");
    assertThat(rank.getFullName()).isEqualTo("GoogleSQL:rank");
    assertThat(!rank.getSignatureList().isEmpty()).isTrue();

    // Excluded
    Function abs = catalog2.getFunctionByFullName("GoogleSQL:abs");
    assertThat(abs).isNull();

    // Included and excluded
    Function any = catalog2.getFunctionByFullName("GoogleSQL:any_value");
    assertThat(any).isNull();

    // Not added
    Function add2 = catalog2.getFunctionByFullName("GoogleSQL:$add");
    assertThat(add2).isNull();

    try {
      catalog1.addGoogleSQLFunctions(new GoogleSQLBuiltinFunctionOptions());
      fail();
    } catch (IllegalStateException expected) {
    }
  }

  @Test
  public void testAddBuiltinFunctions_roundingModeIsOpaque() {
    SimpleCatalog catalog = new SimpleCatalog("foo2");
    LanguageOptions languageOptions = new LanguageOptions();
    languageOptions.setEnabledLanguageFeatures(
        ImmutableSet.of(
            LanguageFeature.FEATURE_NUMERIC_TYPE,
            LanguageFeature.FEATURE_ROUND_WITH_ROUNDING_MODE));

    catalog.addGoogleSQLFunctions(new GoogleSQLBuiltinFunctionOptions(languageOptions));

    assertThat(catalog.getFunctionList().size()).isGreaterThan(0);

    // Included
    Function round = catalog.getFunctionByFullName("GoogleSQL:round");
    assertThat(round).isNotNull();
    EnumType roundingModeType = null;
    for (FunctionSignature signature : round.getSignatureList()) {
      if (signature.getFunctionArgumentList().size() == 3) {
        FunctionArgumentType thirdArg = signature.getFunctionArgumentList().get(2);
        if (thirdArg.getType().isEnum()
            && thirdArg.getType().asEnum().getDescriptor() == RoundingMode.getDescriptor()) {
          roundingModeType = thirdArg.getType().asEnum();
          break;
        }
      }
    }

    assertThat(roundingModeType).isNotNull();
    assertThat(roundingModeType.isOpaque()).isTrue();
  }

  @Test
  public void testSerialize() throws ParseException {
    TypeFactory factory = TypeFactory.nonUniqueNames();
    SimpleType type = TypeFactory.createSimpleType(TypeKind.TYPE_BOOL);
    SimpleColumn column = new SimpleColumn("t1", "c1", type);
    SimpleTable table = new SimpleTable("t1", ImmutableList.of(column));
    SimpleCatalog catalog = new SimpleCatalog("catalog1");
    SimpleCatalog catalog2 = new SimpleCatalog("catalog2", factory);
    Function function =
        new Function(
            "test_function_name",
            Function.GOOGLESQL_FUNCTION_GROUP_NAME,
            Mode.SCALAR,
            ImmutableList.of(
                new FunctionSignature(
                    new FunctionArgumentType(
                        TypeFactory.createSimpleType(TypeKind.TYPE_INT64),
                        ArgumentCardinality.REQUIRED),
                    ImmutableList.of(),
                    /* contextId= */ -1)));
    TableValuedFunction tvf =
        new ForwardInputSchemaToOutputSchemaTVF(
            ImmutableList.of("test_tvf_name"),
            ImmutableList.of(new FunctionSignature(TABLE_TYPE, ImmutableList.of(TABLE_TYPE), -1)),
            TableValuedFunctionOptionsProto.newBuilder().setUsesUpperCaseSqlName(false).build());
    List<FunctionArgumentType> arguments = new ArrayList<>();
    FunctionArgumentType typeInt64 =
        new FunctionArgumentType(TypeFactory.createSimpleType(TypeKind.TYPE_INT64));
    arguments.add(typeInt64);
    FunctionSignature signature = new FunctionSignature(typeInt64, arguments, /* contextId= */ -1);
    Procedure procedure = new Procedure("test_procedure_name", signature);
    SimpleConnection connection = new SimpleConnection("test_connection_name");
    ImmutableList<SimpleModel.NameAndType> modelInputs =
        ImmutableList.of(
            new SimpleModel.NameAndType(
                "input", TypeFactory.createSimpleType(TypeKind.TYPE_STRING)));
    ImmutableList<SimpleModel.NameAndType> modelOutputs =
        ImmutableList.of(
            new SimpleModel.NameAndType(
                "output", TypeFactory.createSimpleType(TypeKind.TYPE_INT64)));
    SimpleModel model = new SimpleModel("test_model_name", modelInputs, modelOutputs);

    GoogleSQLBuiltinFunctionOptionsProto builtinFunctionOptionsproto =
        GoogleSQLBuiltinFunctionOptionsProto.newBuilder()
            .setLanguageOptions(new LanguageOptions().serialize())
            .addIncludeFunctionIds(FunctionSignatureId.FN_EQUAL)
            .addIncludeFunctionIds(FunctionSignatureId.FN_ANY_VALUE)
            .addExcludeFunctionIds(FunctionSignatureId.FN_ABS_DOUBLE)
            .addExcludeFunctionIds(FunctionSignatureId.FN_ANY_VALUE)
            .build();

    catalog.addGoogleSQLFunctions(new GoogleSQLBuiltinFunctionOptions(builtinFunctionOptionsproto));
    catalog.addSimpleTable(table);
    catalog.addType(type.typeName(), type);
    catalog.addSimpleCatalog(catalog2);
    catalog.addFunction(function);
    catalog.addTableValuedFunction(tvf);
    catalog.addProcedure(procedure);
    catalog.addConnection(connection);
    catalog.addModel(model);

    FileDescriptorSetsBuilder fileDescriptorSetsBuilder = new FileDescriptorSetsBuilder();
    SimpleCatalogProto catalogProto = catalog.serialize(fileDescriptorSetsBuilder);
    SimpleCatalogProto.Builder expected = SimpleCatalogProto.newBuilder();
    TextFormat.merge(
        "name: \"catalog1\"\n"
            + "table {\n"
            + "  name: \"t1\"\n"
            + "  is_value_table: false\n"
            + "  column {\n"
            + "    name: \"c1\"\n"
            + "    type {\n"
            + "      type_kind: TYPE_BOOL\n"
            + "    }\n"
            + "    is_pseudo_column: false\n"
            + "    is_writable_column: true\n"
            + "    can_update_unwritable_to_default: false\n"
            + "  }\n"
            + "}\n"
            + "named_type {\n"
            + "  name: \"bool\"\n"
            + "  type {\n"
            + "    type_kind: TYPE_BOOL\n"
            + "  }\n"
            + "}\n"
            + "catalog {\n"
            + "  name: \"catalog2\"\n"
            + "}\n"
            + "builtin_function_options {\n"
            + "  language_options {\n"
            + "    name_resolution_mode: NAME_RESOLUTION_DEFAULT\n"
            + "    product_mode: PRODUCT_INTERNAL\n"
            + "    error_on_deprecated_syntax: false\n"
            + "    supported_statement_kinds: RESOLVED_QUERY_STMT\n"
            + "  }\n"
            + "  include_function_ids: FN_EQUAL\n"
            + "  include_function_ids: FN_ANY_VALUE\n"
            + "  exclude_function_ids: FN_ABS_DOUBLE\n"
            + "  exclude_function_ids: FN_ANY_VALUE\n"
            + "}\n"
            + "custom_function {\n"
            + "  name_path: \"test_function_name\"\n"
            + "  group: \"GoogleSQL\"\n"
            + "  mode: SCALAR\n"
            + "  signature {\n"
            + "    return_type {\n"
            + "      kind: ARG_TYPE_FIXED\n"
            + "      type {\n"
            + "        type_kind: TYPE_INT64\n"
            + "      }\n"
            + "      options { cardinality: REQUIRED }\n"
            + "      num_occurrences: -1\n"
            + "    }\n"
            + "    context_id: -1\n"
            + "    options {\n"
            + "    }\n"
            + "  }\n"
            + "  options {\n"
            + "  }\n"
            + "}\n"
            + "procedure {\n"
            + "  name_path: \"test_procedure_name\"\n"
            + "  signature {\n"
            + "    return_type {\n"
            + "      kind: ARG_TYPE_FIXED\n"
            + "      type {\n"
            + "        type_kind: TYPE_INT64\n"
            + "      }\n"
            + "      options { cardinality: REQUIRED }\n"
            + "      num_occurrences: -1\n"
            + "    }\n"
            + "    argument {\n"
            + "      kind: ARG_TYPE_FIXED\n"
            + "      type {\n"
            + "        type_kind: TYPE_INT64\n"
            + "      }\n"
            + "      options { cardinality: REQUIRED }\n"
            + "      num_occurrences: -1\n"
            + "    }\n"
            + "    context_id: -1\n"
            + "    options {\n"
            + "    }\n"
            + "  }\n"
            + "}"
            + "custom_tvf {\n"
            + "  name_path: \"test_tvf_name\"\n"
            + "  signature {\n"
            + "    argument {\n"
            + "      kind: ARG_TYPE_RELATION\n"
            + "      options {\n"
            + "        cardinality: REQUIRED\n"
            + "      }\n"
            + "      num_occurrences: -1\n"
            + "    }\n"
            + "    return_type {\n"
            + "      kind: ARG_TYPE_RELATION\n"
            + "      options {\n"
            + "        cardinality: REQUIRED\n"
            + "      }\n"
            + "      num_occurrences: -1\n"
            + "    }\n"
            + "    context_id: -1\n"
            + "    options {\n"
            + "    }\n"
            + "  }\n"
            + "  type: FORWARD_INPUT_SCHEMA_TO_OUTPUT_SCHEMA_TVF\n"
            + "  options {"
            + "    uses_upper_case_sql_name: false"
            + "  }"
            + "  signatures {\n"
            + "    argument {\n"
            + "      kind: ARG_TYPE_RELATION\n"
            + "      options {\n"
            + "        cardinality: REQUIRED\n"
            + "      }\n"
            + "      num_occurrences: -1\n"
            + "    }\n"
            + "    return_type {\n"
            + "      kind: ARG_TYPE_RELATION\n"
            + "      options {\n"
            + "        cardinality: REQUIRED\n"
            + "      }\n"
            + "      num_occurrences: -1\n"
            + "    }\n"
            + "    context_id: -1\n"
            + "    options {\n"
            + "    }\n"
            + "  }\n"
            + "  group: \"\""
            + "}\n"
            + "connection {\n"
            + "  name: \"test_connection_name\"\n"
            + "}\n"
            + "model {\n"
            + "  name: \"test_model_name\"\n"
            + "  input {\n"
            + "    name: \"input\"\n"
            + "    type: {\n"
            + "      type_kind: TYPE_STRING\n"
            + "    }\n"
            + "  }\n"
            + "  output {\n"
            + "    name: \"output\"\n"
            + "    type: {\n"
            + "      type_kind: TYPE_INT64\n"
            + "    }\n"
            + "  }\n"
            + "}\n",
        expected);
    expected.getTableBuilder(0).setSerializationId(table.getId());
    expected.getModelBuilder(0).setId(model.getId());
    assertThat(catalogProto).isEqualTo(expected.build());
  }

  @Test
  public void testSerializeTableWithNameInCatalog() {
    SimpleType type = TypeFactory.createSimpleType(TypeKind.TYPE_BOOL);
    SimpleColumn column = new SimpleColumn("T1", "C1", type);
    SimpleTable table = new SimpleTable("T1", ImmutableList.of(column));
    SimpleCatalog catalog = new SimpleCatalog("foo");
    catalog.addSimpleTable("bar", table);
    catalog.addSimpleTable(table);
    FileDescriptorSetsBuilder fileDescriptorSetsBuilder = new FileDescriptorSetsBuilder();
    SimpleCatalogProto catalogProto = catalog.serialize(fileDescriptorSetsBuilder);
    assertThat(catalogProto.getTable(0).getName()).isEqualTo("T1");
    assertThat(catalogProto.getTable(1).getName()).isEqualTo("T1");
    // Map order undeterministic.
    assertThat(!catalogProto.getTable(0).hasNameInCatalog())
        .isEqualTo(catalogProto.getTable(1).hasNameInCatalog());
    assertThat(
            catalogProto.getTable(0).getNameInCatalog().equals("bar")
                || catalogProto.getTable(1).getNameInCatalog().equals("bar"))
        .isTrue();
    SimpleCatalog result =
        SimpleCatalog.deserialize(catalogProto, fileDescriptorSetsBuilder.getDescriptorPools());
    assertThat(result.getTable("T1").getName()).isEqualTo("T1");
    assertThat(result.getTable("bar").getName()).isEqualTo("T1");
  }

  @Test
  public void testRegisterAndUnregister() {
    SimpleType type = TypeFactory.createSimpleType(TypeKind.TYPE_BOOL);
    SimpleColumn column = new SimpleColumn("t1", "c1", type);
    SimpleTable table = new SimpleTable("t1", ImmutableList.of(column));
    SimpleCatalog catalog = new SimpleCatalog("catalog1");
    catalog.addSimpleTable(table);

    try {
      // Unregister before register.
      catalog.unregister();
      fail();
    } catch (IllegalStateException expected) {
    }

    // First register.
    catalog.register();

    try {
      // Mutating after register.
      catalog.addNewSimpleCatalog("foo");
      fail();
    } catch (IllegalStateException expected) {
    }

    try {
      // Mutating after register.
      catalog.addSimpleCatalog(new SimpleCatalog("catalog2"));
      fail();
    } catch (IllegalStateException expected) {
    }

    try {
      // Mutating after register.
      catalog.addSimpleTable(new SimpleTable("table2"));
      fail();
    } catch (IllegalStateException expected) {
    }

    try {
      // Mutating after register.
      catalog.addType("baz", TypeFactory.createSimpleType(TypeKind.TYPE_BOOL));
      fail();
    } catch (IllegalStateException expected) {
    }

    try {
      // Double register.
      catalog.register();
      fail();
    } catch (IllegalStateException expected) {
    }

    // Unregister after register.
    catalog.unregister();

    // Mutating after unregister.
    catalog.addNewSimpleCatalog("foo");

    // Mutating after unregister.
    catalog.addSimpleCatalog(new SimpleCatalog("catalog2"));

    // Mutating after unregister.
    catalog.addSimpleTable(new SimpleTable("bar"));

    // Mutating after unregister.
    catalog.addType("baz", TypeFactory.createSimpleType(TypeKind.TYPE_BOOL));

    try {
      // Double unregister.
      catalog.unregister();
      fail();
    } catch (IllegalStateException expected) {
    }
  }

  @Test
  public void testRegisterAndUnregisterWithTableContents() {
    SimpleType type = TypeFactory.createSimpleType(TypeKind.TYPE_BOOL);

    SimpleTable table1 = new SimpleTable("t1");
    table1.addSimpleColumn("c1", type);

    SimpleTable table2 = new SimpleTable("t2");
    table2.addSimpleColumn("c1", type);

    SimpleCatalog catalog = new SimpleCatalog("catalog1");
    catalog.addSimpleTable(table1);
    catalog.addSimpleTable(table2);

    ImmutableMap<String, TableContent> tablesContents =
        ImmutableMap.of(
            "t1",
            TableContent.create(
                ImmutableList.of(
                    ImmutableList.of(Value.createBoolValue(true)),
                    ImmutableList.of(Value.createBoolValue(false)))));

    // Register catalog with tables contents
    catalog.register(tablesContents);

    // Unregister catalog.
    catalog.unregister();
  }

  @Test
  public void testRegisterAutoClose() {
    SimpleCatalog catalog = new SimpleCatalog("catalog1");

    // Auto-unregister using try-with-resources.
    try (AutoUnregister autoUnregister = catalog.register()) {}

    try {
      // Unregister after auto close.
      catalog.unregister();
      fail();
    } catch (IllegalStateException expected) {
    }
  }

  @Test
  public void testClassAndProtoSize() {
    assertWithMessage(
            "The number of fields of SimpleCatalogProto has changed, "
                + "please also update the serialization code accordingly.")
        .that(SimpleCatalogProto.getDescriptor().getFields())
        .hasSize(14);
    assertWithMessage(
            "The number of fields in SimpleCatalog class has changed, "
                + "please also update the proto and serialization code accordingly.")
        .that(TestUtil.getNonStaticFieldCount(SimpleCatalog.class))
        .isEqualTo(24);
  }

  @Test
  public void testPoolSetOnDeserialize() {
    GoogleSQLDescriptorPool pool = new GoogleSQLDescriptorPool();
    pool.addFileDescriptor(SimpleCatalogProto.getDescriptor().getFile());
    SimpleCatalogProto proto =
        SimpleCatalogProto.newBuilder().setName("yams").setFileDescriptorSetIndex(0).build();
    SimpleCatalog catalog = SimpleCatalog.deserialize(proto, ImmutableList.of(pool));
    // There is no way to get at the catalog's descriptor pool directly, nor is there a way to get
    // at a ProtoType's descriptor pool. The best we can do here is note that the only way that this
    // catalog can produce this Type is by using the descriptor pool whose index was referred to in
    // the SimpleCatalogProto.
    assertThat(catalog.getType(SimpleCatalogProto.getDescriptor().getFullName(), null)).isNotNull();
  }

  @Test
  public void testBuiltInFunctionOptionsSetOnDeserialize() {
    GoogleSQLBuiltinFunctionOptionsProto optionsProto =
        new GoogleSQLBuiltinFunctionOptions().serialize();
    SimpleCatalogProto catalogProto =
        SimpleCatalogProto.newBuilder()
            .setName("yams")
            .setBuiltinFunctionOptions(optionsProto)
            .build();
    SimpleCatalog catalog = SimpleCatalog.deserialize(catalogProto, ImmutableList.of());
    assertThat(catalog.getFunctionByFullName("GoogleSQL:$add")).isNotNull();
  }

  @Test
  public void testGlobalNames() {
    SimpleCatalog catalog = new SimpleCatalog("catalog");
    catalog.addSimpleTable("table1", new SimpleTable("table1"));

    // insert a table with existing name
    assertThrows(
        IllegalArgumentException.class,
        () -> catalog.addSimpleTable("table1", new SimpleTable("table1")));

    // insert a property graph with existing name of a table
    assertThrows(
        IllegalArgumentException.class,
        () -> catalog.addSimplePropertyGraph(new SimplePropertyGraph(ImmutableList.of("table1"))));

    catalog.addSimplePropertyGraph(new SimplePropertyGraph(ImmutableList.of("table2")));
    // insert a property graph with existing name
    assertThrows(
        IllegalArgumentException.class,
        () -> catalog.addSimplePropertyGraph(new SimplePropertyGraph(ImmutableList.of("table2"))));
    // insert a table with existing name of a property graph
    assertThrows(
        IllegalArgumentException.class,
        () -> catalog.addSimpleTable("table2", new SimpleTable("table2")));
  }
}
