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
import static org.junit.Assert.assertEquals;
import static org.junit.Assert.assertThrows;

import com.google.common.collect.ImmutableList;
import com.google.common.collect.ImmutableSet;
import com.google.googlesql.FunctionProtos.FunctionOptionsProto;
import com.google.googlesql.GoogleSQLFunctions.FunctionEnums.Mode;
import com.google.googlesql.GoogleSQLFunctions.FunctionEnums.Volatility;
import com.google.googlesql.GoogleSQLFunctions.SignatureArgumentKind;
import com.google.googlesql.GoogleSQLOptions.LanguageFeature;
import com.google.googlesql.GoogleSQLResolvedNodeKind.ResolvedNodeKind;
import com.google.googlesql.GoogleSQLType.TypeKind;
import com.google.googlesql.TableValuedFunction.ForwardInputSchemaToOutputSchemaWithAppendedColumnTVF;
import com.google.googlesql.resolvedast.ResolvedNodes.ResolvedExpr;
import com.google.googlesql.resolvedast.ResolvedNodes.ResolvedQueryStmt;
import com.google.googlesql.resolvedast.ResolvedNodes.ResolvedStatement;
import com.google.googlesqltest.TestSchemaProto.KitchenSinkPB;
import com.google.googlesqltest.TestSchemaProto.NullableInt;
import com.google.googlesqltest.TestSchemaProto.TestEnum;


import java.util.Arrays;
import java.util.List;
import org.junit.Test;
import org.junit.runner.RunWith;
import org.junit.runners.JUnit4;

@RunWith(JUnit4.class)

public class AnalyzerTest {

  @Test
  public void testAnalyzeStatementEmptyCatalog() {
    SimpleCatalog catalog = new SimpleCatalog("foo");
    AnalyzerOptions options = new AnalyzerOptions();
    assertThat(new Analyzer(options, catalog).analyzeStatement("select 1;")).isNotNull();
  }

  @Test
  public void testAnalyzeNextStatementWithCatalog() {
    SimpleCatalog catalog = new SimpleCatalog("foo");
    catalog.addSimpleTable(
        SimpleTable.tableFromProto(catalog.getTypeFactory().createProtoType(KitchenSinkPB.class)));

    AnalyzerOptions options = new AnalyzerOptions();

    ParseResumeLocation aParseResumeLocation =
        new ParseResumeLocation(
            "select nullable_int from KitchenSinkPB;select key_value from KitchenSinkPB;");
    assertThat(Analyzer.analyzeNextStatement(aParseResumeLocation, options, catalog)).isNotNull();
    assertThat(aParseResumeLocation.getBytePosition()).isEqualTo(39);
    assertThat(Analyzer.analyzeNextStatement(aParseResumeLocation, options, catalog)).isNotNull();
    assertThat(aParseResumeLocation.getBytePosition()).isEqualTo(75);

    // Try registering the catalog.
    catalog.register();
    ParseResumeLocation parseResumeLocation0 =
        new ParseResumeLocation(
            "select nullable_int from KitchenSinkPB;select key_value from KitchenSinkPB;");
    assertThat(Analyzer.analyzeNextStatement(parseResumeLocation0, options, catalog)).isNotNull();
    assertThat(parseResumeLocation0.getBytePosition()).isEqualTo(39);
    assertThat(Analyzer.analyzeNextStatement(parseResumeLocation0, options, catalog)).isNotNull();
    assertThat(parseResumeLocation0.getBytePosition()).isEqualTo(75);

    ParseResumeLocation parseResumeLocation1 =
        new ParseResumeLocation(
            "select nullable_int from KitchenSinkPB;select key_value from KitchenSinkPB;");
    assertThat(Analyzer.analyzeNextStatement(parseResumeLocation1, options, catalog)).isNotNull();
    assertThat(parseResumeLocation1.getBytePosition()).isEqualTo(39);
    assertThat(Analyzer.analyzeNextStatement(parseResumeLocation1, options, catalog)).isNotNull();
    assertThat(parseResumeLocation1.getBytePosition()).isEqualTo(75);

    // Try unregistering the catalog.
    catalog.unregister();
    aParseResumeLocation =
        new ParseResumeLocation(
            "select nullable_int from KitchenSinkPB;select key_value from KitchenSinkPB;");
    assertThat(Analyzer.analyzeNextStatement(aParseResumeLocation, options, catalog)).isNotNull();
    assertThat(aParseResumeLocation.getBytePosition()).isEqualTo(39);
    assertThat(Analyzer.analyzeNextStatement(aParseResumeLocation, options, catalog)).isNotNull();
    assertThat(aParseResumeLocation.getBytePosition()).isEqualTo(75);
  }

  @Test
  public void testAnalyzeStatementWithCatalog() {
    SimpleCatalog catalog = new SimpleCatalog("foo");
    catalog.addSimpleTable(
        SimpleTable.tableFromProto(catalog.getTypeFactory().createProtoType(KitchenSinkPB.class)));

    AnalyzerOptions options = new AnalyzerOptions();
    String sql = "select nullable_int from KitchenSinkPB;";
    assertThat(Analyzer.analyzeStatement(sql, options, catalog)).isNotNull();

    // Try registering the catalog.
    catalog.register();
    assertThat(Analyzer.analyzeStatement(sql, options, catalog)).isNotNull();
    assertThat(Analyzer.analyzeStatement(sql, options, catalog)).isNotNull();

    // Try unregistering the catalog.
    catalog.unregister();
    assertThat(Analyzer.analyzeStatement(sql, options, catalog)).isNotNull();
  }

  void assertOutputColumnType(ResolvedStatement stmt, Type expectedType) {
    assertThat(stmt).isInstanceOf(ResolvedQueryStmt.class);
    assertThat(((ResolvedQueryStmt) stmt).getOutputColumnList().get(0).getColumn().getType())
        .isEqualTo(expectedType);
  }

  @Test
  public void testAnalyzeStatementWithTableWithProtoType() {
    SimpleCatalog catalog = new SimpleCatalog("foo");

    ProtoType kitchenSinkType = catalog.getTypeFactory().createProtoType(KitchenSinkPB.class);
    String tableName = "TestTable";
    SimpleColumn column =
        new SimpleColumn(
            tableName,
            "kitchen",
            kitchenSinkType,
            /*isPseudoColumn=*/ false,
            /*isWritableColumn=*/ false);
    SimpleTable table = new SimpleTable(tableName, ImmutableList.of(column));

    catalog.addSimpleTable(table);

    AnalyzerOptions options = new AnalyzerOptions();
    String sql = "select kitchen.nullable_int from TestTable;";
    assertOutputColumnType(
        Analyzer.analyzeStatement(sql, options, catalog),
        catalog.getTypeFactory().createProtoType(NullableInt.class));

    // Try registering the catalog.
    catalog.register();
    assertThat(Analyzer.analyzeStatement(sql, options, catalog)).isNotNull();
    assertThat(Analyzer.analyzeStatement(sql, options, catalog)).isNotNull();

    // Try unregistering the catalog.
    catalog.unregister();
    assertThat(Analyzer.analyzeStatement(sql, options, catalog)).isNotNull();
  }

  @Test
  public void testAnalyzeStatementWithCatalogProtoType() {
    SimpleCatalog catalog = new SimpleCatalog("foo");
    ProtoType kitchenSinkType = catalog.getTypeFactory().createProtoType(KitchenSinkPB.class);
    catalog.addType("googlesql_test.KitchenSinkPB", kitchenSinkType);
    // TODO: Use ImmutableDescriptorPool
    catalog.setDescriptorPool(GoogleSQLDescriptorPool.getGeneratedPool());

    AnalyzerOptions options = new AnalyzerOptions();
    String sql = "select new googlesql_test.KitchenSinkPB(1 as int64_key_1, 2 as int64_key_2)";
    assertOutputColumnType(Analyzer.analyzeStatement(sql, options, catalog), kitchenSinkType);

    // Try registering the catalog.
    catalog.register();
    assertThat(Analyzer.analyzeStatement(sql, options, catalog)).isNotNull();
    assertThat(Analyzer.analyzeStatement(sql, options, catalog)).isNotNull();

    // Try unregistering the catalog.
    catalog.unregister();
    assertThat(Analyzer.analyzeStatement(sql, options, catalog)).isNotNull();
  }

  @Test
  public void testAnalyzeStatementWithTableWithEnumType() {
    SimpleCatalog catalog = new SimpleCatalog("foo");

    EnumType testEnumType = catalog.getTypeFactory().createEnumType(TestEnum.class);
    String tableName = "TestTable";
    SimpleColumn column =
        new SimpleColumn(
            tableName, "te", testEnumType, /*isPseudoColumn=*/ false, /*isWritableColumn=*/ false);
    SimpleTable table = new SimpleTable(tableName, ImmutableList.of(column));

    catalog.addSimpleTable(table);

    AnalyzerOptions options = new AnalyzerOptions();
    String sql = "select te from TestTable";
    assertOutputColumnType(Analyzer.analyzeStatement(sql, options, catalog), testEnumType);

    // Try registering the catalog.
    catalog.register();
    assertThat(Analyzer.analyzeStatement(sql, options, catalog)).isNotNull();
    assertThat(Analyzer.analyzeStatement(sql, options, catalog)).isNotNull();

    // Try unregistering the catalog.
    catalog.unregister();
    assertThat(Analyzer.analyzeStatement(sql, options, catalog)).isNotNull();
  }

  @Test
  public void testAnalyzeStatementWithCatalogEnumType() {
    SimpleCatalog catalog = new SimpleCatalog("foo");

    EnumType testEnumType = catalog.getTypeFactory().createEnumType(TestEnum.class);
    catalog.addType("googlesql_test.TestEnum", testEnumType);
    // TODO: Use ImmutableDescriptorPool
    catalog.setDescriptorPool(GoogleSQLDescriptorPool.getGeneratedPool());

    AnalyzerOptions options = new AnalyzerOptions();
    String sql = "select cast('TESTENUM1' as googlesql_test.TestEnum)";
    assertOutputColumnType(Analyzer.analyzeStatement(sql, options, catalog), testEnumType);

    // Try registering the catalog.
    catalog.register();
    assertThat(Analyzer.analyzeStatement(sql, options, catalog)).isNotNull();
    assertThat(Analyzer.analyzeStatement(sql, options, catalog)).isNotNull();

    // Try unregistering the catalog.
    catalog.unregister();
    assertThat(Analyzer.analyzeStatement(sql, options, catalog)).isNotNull();
  }

  @Test
  public void testAnalyzeStatementWithTable() {

    SimpleColumn column =
        new SimpleColumn("t", "bar", TypeFactory.createSimpleType(TypeKind.TYPE_INT32));
    SimpleTable table = new SimpleTable("t", ImmutableList.of(column));
    SimpleCatalog catalog = new SimpleCatalog("");
    catalog.addSimpleTable(table);

    AnalyzerOptions options = new AnalyzerOptions();
    String sql = "select bar from t;";
    assertThat(Analyzer.analyzeStatement(sql, options, catalog)).isNotNull();
  }

  @Test
  public void testAnalyzeStatementWithSubCatalogTable() {

    SimpleColumn column =
        new SimpleColumn("t", "bar", TypeFactory.createSimpleType(TypeKind.TYPE_INT32));
    SimpleTable table = new SimpleTable("t", ImmutableList.of(column));
    SimpleCatalog subCatalog = new SimpleCatalog("sub");
    subCatalog.addSimpleTable(table);

    SimpleCatalog catalog = new SimpleCatalog("");
    catalog.addSimpleCatalog(subCatalog);

    AnalyzerOptions options = new AnalyzerOptions();
    String sql = "select bar from sub.t;";
    assertThat(Analyzer.analyzeStatement(sql, options, catalog)).isNotNull();
  }

  @Test
  public void analyzeStatement_unsupportedStatement_throwsSqlException() {
    AnalyzerOptions analyzerOptions = new AnalyzerOptions();
    String sql = "create table foo (col string)";
    assertThrows(
        SqlException.class,
        () -> Analyzer.analyzeStatement(sql, analyzerOptions, new SimpleCatalog("")));
  }

  @Test
  public void analyzeStatement_supportsAllStatements() {
    String sql = "create table foo (col string)";
    AnalyzerOptions analyzerOptions = new AnalyzerOptions();
    analyzerOptions.getLanguageOptions().setSupportsAllStatementKinds();
    assertThat(Analyzer.analyzeStatement(sql, analyzerOptions, new SimpleCatalog(""))).isNotNull();
  }

  @Test
  public void analyzeStatement_supportsStatementKinds() {
    String sql = "create table foo (col string)";
    AnalyzerOptions analyzerOptions = new AnalyzerOptions();
    analyzerOptions
        .getLanguageOptions()
        .setSupportedStatementKinds(ImmutableSet.of(ResolvedNodeKind.RESOLVED_CREATE_TABLE_STMT));

    assertThat(Analyzer.analyzeStatement(sql, analyzerOptions, new SimpleCatalog(""))).isNotNull();
  }

  @Test
  public void testAnalyzeExpressionWithCatalog() {
    SimpleCatalog catalog = new SimpleCatalog("foo");
    catalog.addGoogleSQLFunctions(new GoogleSQLBuiltinFunctionOptions());

    AnalyzerOptions options = new AnalyzerOptions();
    options.addExpressionColumn("foo", TypeFactory.createSimpleType(TypeKind.TYPE_INT32));

    String expression = "foo < 123";
    assertThat(Analyzer.analyzeExpression(expression, options, catalog)).isNotNull();

    // Try registering the catalog.
    catalog.register();
    assertThat(Analyzer.analyzeExpression(expression, options, catalog)).isNotNull();

    // Try unregistering the catalog.
    catalog.unregister();
    assertThat(Analyzer.analyzeExpression(expression, options, catalog)).isNotNull();
  }

  @Test
  public void buildExpressionRoundTrip() {
    SimpleCatalog catalog = new SimpleCatalog("foo");
    catalog.addGoogleSQLFunctions(new GoogleSQLBuiltinFunctionOptions());

    AnalyzerOptions options = new AnalyzerOptions();
    options.addExpressionColumn("foo", TypeFactory.createSimpleType(TypeKind.TYPE_INT32));

    String expression = "foo < 123";
    ResolvedExpr resolvedExpr = Analyzer.analyzeExpression(expression, options, catalog);

    String expectedResponse = "foo < (CAST(123 AS INT32))"; // Sql builder normalizes expression.
    String response = Analyzer.buildExpression(resolvedExpr, catalog);
    assertEquals(expectedResponse, response);
  }

  @Test
  public void buildExpressionWithDatePartsRoundTrip() {
    SimpleCatalog catalog = new SimpleCatalog("foo");
    catalog.addGoogleSQLFunctions(new GoogleSQLBuiltinFunctionOptions());

    AnalyzerOptions options = new AnalyzerOptions();

    String expression = "DATE_TRUNC(DATE \"2008-12-25\", MONTH)";
    ResolvedExpr resolvedExpr = Analyzer.analyzeExpression(expression, options, catalog);

    String expectedResponse = "DATE_TRUNC(DATE \"2008-12-25\", MONTH)";
    String response = Analyzer.buildExpression(resolvedExpr, catalog);
    assertEquals(expectedResponse, response);
  }

  @Test
  public void buildExpressionWithDatePartsAndRegisteredCatalogRoundTrip() {
    SimpleCatalog catalog = new SimpleCatalog("foo");
    catalog.addGoogleSQLFunctions(new GoogleSQLBuiltinFunctionOptions());
    catalog.register();

    AnalyzerOptions options = new AnalyzerOptions();

    String expression = "DATE_TRUNC(DATE \"2008-12-25\", MONTH)";
    ResolvedExpr resolvedExpr = Analyzer.analyzeExpression(expression, options, catalog);

    String expectedResponse = "DATE_TRUNC(DATE \"2008-12-25\", MONTH)";
    String response = Analyzer.buildExpression(resolvedExpr, catalog);
    assertEquals(expectedResponse, response);
  }

  @Test
  public void buildStatementRoundTrip() {
    SimpleCatalog catalog = new SimpleCatalog("foo");
    catalog.addGoogleSQLFunctions(new GoogleSQLBuiltinFunctionOptions());

    SimpleColumn column =
        new SimpleColumn("t", "bar", TypeFactory.createSimpleType(TypeKind.TYPE_INT32));
    SimpleTable table = new SimpleTable("t", ImmutableList.of(column));
    catalog.addSimpleTable(table);

    AnalyzerOptions options = new AnalyzerOptions();
    String sql = "select t.bar from t";

    ResolvedStatement resolvedStatement = Analyzer.analyzeStatement(sql, options, catalog);
    assertThat(resolvedStatement).isNotNull();

    // Sql builder normalizes expression.
    String expectedResponse = "SELECT t.bar AS bar FROM t";
    String response = Analyzer.buildStatement(resolvedStatement, catalog);
    assertEquals(expectedResponse, response);
  }

  @Test
  public void buildStatementWithDatePartsAndRegisteredCatalogRoundTrip() {
    SimpleCatalog catalog = new SimpleCatalog("foo");
    catalog.addGoogleSQLFunctions(new GoogleSQLBuiltinFunctionOptions());
    catalog.register();

    AnalyzerOptions options = new AnalyzerOptions();

    String expression = "SELECT DATE_TRUNC(DATE \"2008-12-25\", MONTH)";
    ResolvedStatement resolvedStmt = Analyzer.analyzeStatement(expression, options, catalog);

    String expectedResponse = "SELECT DATE_TRUNC(DATE \"2008-12-25\", MONTH) AS a_1";
    String response = Analyzer.buildStatement(resolvedStmt, catalog);
    assertEquals(expectedResponse, response);
  }

  @Test
  public void testExtractTableNamesFromStatement() {
    List<List<String>> tableNames =
        Analyzer.extractTableNamesFromStatement(
            "select count(1) from foo.bar", new AnalyzerOptions());

    assertThat(tableNames).containsExactly(Arrays.asList("foo", "bar"));
  }

  @Test
  public void testExtractTableNamesFromScriptGivenStatement() {
    List<List<String>> tableNames =
        Analyzer.extractTableNamesFromScript("select count(1) from foo.bar", new AnalyzerOptions());

    assertThat(tableNames).containsExactly(Arrays.asList("foo", "bar"));
  }

  @Test
  public void testExtractTableNamesFromScript() {
    List<List<String>> tableNames =
        Analyzer.extractTableNamesFromScript(
            "select count(1) from foo.bar; select count(1) from x.y.z", new AnalyzerOptions());

    assertThat(tableNames)
        .containsExactly(Arrays.asList("foo", "bar"), Arrays.asList("x", "y", "z"));
  }

  @Test
  public void testExtractTableNamesFromStatementWithAnalyzerOptions() {
    AnalyzerOptions analyzerOptions = new AnalyzerOptions();
    analyzerOptions
        .getLanguageOptions()
        .setSupportedStatementKinds(
            ImmutableSet.of(
                ResolvedNodeKind.RESOLVED_INSERT_STMT, ResolvedNodeKind.RESOLVED_QUERY_STMT));

    List<List<String>> tableNames =
        Analyzer.extractTableNamesFromStatement(
            "insert into baz select count(1) from foo.bar", analyzerOptions);

    assertThat(tableNames).containsExactly(Arrays.asList("baz"), Arrays.asList("foo", "bar"));
  }

  @Test
  public void testAnalyzeNextStatementWithParseResumeLocation() {
    SimpleCatalog catalog = new SimpleCatalog("foo");
    catalog.addSimpleTable(
        SimpleTable.tableFromProto(catalog.getTypeFactory().createProtoType(KitchenSinkPB.class)));

    AnalyzerOptions options = new AnalyzerOptions();

    ParseResumeLocation aParseResumeLocation =
        new ParseResumeLocation(
            "select nullable_int from KitchenSinkPB;select key_value from KitchenSinkPB;");

    assertThat(Analyzer.analyzeNextStatement(aParseResumeLocation, options, catalog)).isNotNull();
    assertThat(aParseResumeLocation.getBytePosition()).isEqualTo(39);
    assertThat(Analyzer.analyzeNextStatement(aParseResumeLocation, options, catalog)).isNotNull();
    assertThat(aParseResumeLocation.getBytePosition()).isEqualTo(75);

    // After mutating location
    aParseResumeLocation.setBytePosition(39);
    assertThat(aParseResumeLocation.getBytePosition()).isEqualTo(39);
    assertThat(Analyzer.analyzeNextStatement(aParseResumeLocation, options, catalog)).isNotNull();
    assertThat(aParseResumeLocation.getBytePosition()).isEqualTo(75);
  }

  @Test
  public void testSelectColumnsFromForwardInputSchemaToOutputSchemaWithAppendedColumnTVF() {
    FunctionArgumentType tableType =
        new FunctionArgumentType(SignatureArgumentKind.ARG_TYPE_RELATION);
    TableValuedFunction tvf =
        new ForwardInputSchemaToOutputSchemaWithAppendedColumnTVF(
            ImmutableList.of("test_tvf"),
            new FunctionSignature(tableType, ImmutableList.of(tableType), /* contextId= */ -1),
            ImmutableList.of(
                TVFRelation.Column.create(
                    "append_col_1", TypeFactory.createSimpleType(TypeKind.TYPE_INT64)),
                TVFRelation.Column.create(
                    "append_col_2", TypeFactory.createSimpleType(TypeKind.TYPE_TIMESTAMP))),
            "customContext",
            Volatility.STABLE);

    SimpleCatalog catalog = new SimpleCatalog("catalog1");
    catalog.addTableValuedFunction(tvf);
    // Test if tvf is added to catalog.
    assertThat(catalog.getTvfByFullName("test_tvf")).isEqualTo(tvf);
    AnalyzerOptions analyzerOptions = new AnalyzerOptions();
    analyzerOptions
        .getLanguageOptions()
        .setEnabledLanguageFeatures(
            ImmutableSet.of(LanguageFeature.FEATURE_TABLE_VALUED_FUNCTIONS));
    Analyzer analyzer = new Analyzer(analyzerOptions, catalog);
    ResolvedStatement resolvedStatement =
        analyzer.analyzeStatement(
            "select append_col_1, append_col_2 from test_tvf((select cast (12 as int64)))");
    // Test if select extra columns query is analyzed correctly.
    assertThat(resolvedStatement).isNotNull();
  }

  @Test
  /* This test method is a substitution of extended subscript tests defined at
   * `coercion.test` and `extended_sbuscript.test`.
   * With current setup, there is no good way to load extended signatures
   * defined in the SampleCatalog that are not defined in the googlesql built-in functions.
   * See more context at b/186869835.
   */
  public void testExtendedSubscriptFunction() {
    FunctionArgumentType stringType =
        new FunctionArgumentType(TypeFactory.createSimpleType(TypeKind.TYPE_STRING));
    FunctionArgumentType int64Type =
        new FunctionArgumentType(TypeFactory.createSimpleType(TypeKind.TYPE_INT64));
    Function subscriptWithOffsetFn =
        new Function(
            "$subscript_with_offset",
            "GoogleSQL",
            Mode.SCALAR,
            ImmutableList.of(
                new FunctionSignature(
                    stringType, ImmutableList.of(stringType, int64Type), /* contextId= */ -1),
                new FunctionSignature(
                    stringType, ImmutableList.of(stringType, stringType), /* contextId= */ -1)),
            FunctionOptionsProto.newBuilder().setSupportsSafeErrorMode(false).build());

    Function safeSubscriptWithOffsetFn =
        new Function(
            "$safe_subscript_with_offset",
            "GoogleSQL",
            Mode.SCALAR,
            ImmutableList.of(
                new FunctionSignature(
                    stringType, ImmutableList.of(stringType, int64Type), /* contextId= */ -1),
                new FunctionSignature(
                    stringType, ImmutableList.of(stringType, stringType), /* contextId= */ -1)),
            FunctionOptionsProto.newBuilder().setSupportsSafeErrorMode(false).build());

    SimpleCatalog catalog = new SimpleCatalog("foo");
    catalog.addFunction(subscriptWithOffsetFn);
    catalog.addFunction(safeSubscriptWithOffsetFn);

    LanguageOptions options = new LanguageOptions();
    options.enableLanguageFeature(LanguageFeature.FEATURE_SAFE_FUNCTION_CALL);
    AnalyzerOptions analyzerOptions = new AnalyzerOptions();
    analyzerOptions.setLanguageOptions(options);
    Analyzer analyzer = new Analyzer(analyzerOptions, catalog);

    ResolvedStatement resolvedStatement = analyzer.analyzeStatement("SELECT \"abc\"[OFFSET(1)];");
    assertThat(resolvedStatement).isNotNull();

    resolvedStatement = analyzer.analyzeStatement("SELECT \"abc\"[OFFSET(\"1\")];");
    assertThat(resolvedStatement).isNotNull();

    resolvedStatement = analyzer.analyzeStatement("SELECT \"abc\"[SAFE_OFFSET(1)];");
    assertThat(resolvedStatement).isNotNull();

    resolvedStatement = analyzer.analyzeStatement("SELECT \"abc\"[SAFE_OFFSET(\"1\")];");
    assertThat(resolvedStatement).isNotNull();

    resolvedStatement = analyzer.analyzeStatement("SELECT NULL[OFFSET(0)];");
    assertThat(resolvedStatement).isNotNull();

    // No signature for function signature `STRING[DOUBLE]` defined.
    SqlException thrown =
        assertThrows(
            SqlException.class, () -> analyzer.analyzeStatement("SELECT \"abc\"[OFFSET(1.0)];"));
    assertThat(thrown).hasMessageThat().contains("No matching signature");
  }

  @Test
  public void testExtractTableNamesFromNextStatement() {
    AnalyzerOptions analyzerOptions = new AnalyzerOptions();
    analyzerOptions
        .getLanguageOptions()
        .setSupportedStatementKinds(
            ImmutableSet.of(
                ResolvedNodeKind.RESOLVED_QUERY_STMT,
                ResolvedNodeKind.RESOLVED_CREATE_FUNCTION_STMT));

    ParseResumeLocation aParseResumeLocation =
        new ParseResumeLocation(
            "CREATE FUNCTION plusOne(x INT64) AS (x + 1);select bar from bar_dataset.bar_table"
                + " union all select * from baz_table;");

    assertThat(Analyzer.extractTableNamesFromNextStatement(aParseResumeLocation, analyzerOptions))
        .isEmpty();
    assertThat(aParseResumeLocation.getBytePosition()).isEqualTo(44);
    assertThat(Analyzer.extractTableNamesFromNextStatement(aParseResumeLocation, analyzerOptions))
        .containsExactly(
            ImmutableList.of("bar_dataset", "bar_table"), ImmutableList.of("baz_table"));
    assertThat(aParseResumeLocation.getBytePosition()).isEqualTo(116);
  }
}
