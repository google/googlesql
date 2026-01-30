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

import com.google.googlesql.Analyzer;
import com.google.googlesql.AnalyzerOptions;
import com.google.googlesql.GoogleSQLBuiltinFunctionOptions;
import com.google.googlesql.GoogleSQLOptions.LanguageFeature;
import com.google.googlesql.GoogleSQLType.TypeKind;
import com.google.googlesql.SimpleCatalog;
import com.google.googlesql.SimpleTable;
import com.google.googlesql.TypeFactory;
import com.google.googlesql.Value;
import com.google.googlesql.resolvedast.ResolvedJoinScanEnums.JoinType;
import com.google.googlesql.resolvedast.ResolvedNodes.ResolvedCast;
import com.google.googlesql.resolvedast.ResolvedNodes.ResolvedComputedColumn;
import com.google.googlesql.resolvedast.ResolvedNodes.ResolvedFilterScan;
import com.google.googlesql.resolvedast.ResolvedNodes.ResolvedFunctionCall;
import com.google.googlesql.resolvedast.ResolvedNodes.ResolvedJoinScan;
import com.google.googlesql.resolvedast.ResolvedNodes.ResolvedLiteral;
import com.google.googlesql.resolvedast.ResolvedNodes.ResolvedProjectScan;
import com.google.googlesql.resolvedast.ResolvedNodes.ResolvedQueryStmt;
import com.google.googlesql.resolvedast.ResolvedNodes.ResolvedStatement;


import org.junit.Before;
import org.junit.Test;
import org.junit.runner.RunWith;
import org.junit.runners.JUnit4;

@RunWith(JUnit4.class)

public class RewritingVisitorTest {

  private Analyzer analyzer;

  private static class ModifyJoinScan extends RewritingVisitor {
    @Override
    protected ResolvedJoinScan visit(ResolvedJoinScan node) {
      JoinType newJoinType = JoinType.forNumber((node.getJoinType().getNumber() + 1) % 4);
      return super.visit(node).toBuilder().setJoinType(newJoinType).build();
    }
  }

  private static class LiteralDoubler extends RewritingVisitor {
    @Override
    protected ResolvedLiteral visit(ResolvedLiteral node) {
      Value value = node.getValue();
      switch (value.getType().getKind()) {
        case TYPE_DOUBLE:
          return node.toBuilder()
              .setValue(Value.createDoubleValue(value.getDoubleValue() * 2.0))
              .build();

        case TYPE_INT64:
          return node.toBuilder()
              .setValue(Value.createInt64Value(value.getInt64Value() * 2))
              .build();

        case TYPE_STRING:
          return node.toBuilder()
              .setValue(Value.createStringValue(value.getStringValue() + value.getStringValue()))
              .build();

        default:
          return node;
      }
    }
  }

  @Before
  public void setUp() {
    SimpleCatalog catalog = new SimpleCatalog("Test catalog", TypeFactory.uniqueNames());
    catalog.addGoogleSQLFunctions(new GoogleSQLBuiltinFunctionOptions());
    AnalyzerOptions options = new AnalyzerOptions();
    options.getLanguageOptions().enableLanguageFeature(LanguageFeature.FEATURE_ANALYTIC_FUNCTIONS);
    SimpleTable temp = new SimpleTable("Temp");
    temp.addSimpleColumn("double_field", TypeFactory.createSimpleType(TypeKind.TYPE_DOUBLE));
    temp.addSimpleColumn("int32_field", TypeFactory.createSimpleType(TypeKind.TYPE_INT32));
    catalog.addSimpleTable(temp);
    analyzer = new Analyzer(options, catalog);
  }

  @Test
  public void modifyJoinScan() {
    String inputSql = "SELECT a.double_field FROM Temp a JOIN Temp b USING (int32_field)";
    ResolvedStatement resolvedStatement = analyzer.analyzeStatement(inputSql);
    ResolvedStatement updatedStatement = resolvedStatement.accept(new ModifyJoinScan());

    assertThat(updatedStatement).isInstanceOf(ResolvedQueryStmt.class);
    ResolvedQueryStmt query = (ResolvedQueryStmt) updatedStatement;
    assertThat(query.getQuery()).isInstanceOf(ResolvedProjectScan.class);
    ResolvedProjectScan projectScan = (ResolvedProjectScan) query.getQuery();
    assertThat(projectScan.getInputScan()).isInstanceOf(ResolvedJoinScan.class);
    ResolvedJoinScan joinScan = (ResolvedJoinScan) projectScan.getInputScan();
    assertThat(joinScan.getJoinType()).isEqualTo(JoinType.LEFT);
  }

  @Test
  public void literalDoubler() {
    String inputSql =
        "SELECT 1, double_field + 3.0, 5 FROM Temp"
            + " WHERE int32_field > CAST(CONCAT(CAST(int32_field AS STRING), \"7\") AS INT32)";
    ResolvedStatement resolvedStatement = analyzer.analyzeStatement(inputSql);
    ResolvedStatement updatedStatement = resolvedStatement.accept(new LiteralDoubler());

    assertThat(updatedStatement).isInstanceOf(ResolvedQueryStmt.class);
    ResolvedQueryStmt query = (ResolvedQueryStmt) updatedStatement;
    assertThat(query.getQuery()).isInstanceOf(ResolvedProjectScan.class);
    ResolvedProjectScan projectScan = (ResolvedProjectScan) query.getQuery();
    assertThat(projectScan.getExprList()).hasSize(3);
    ResolvedComputedColumn column = projectScan.getExprList().get(0);
    assertThat(column.getExpr()).isInstanceOf(ResolvedLiteral.class);
    ResolvedLiteral literal = (ResolvedLiteral) column.getExpr();
    // Check that 1 was doubled to 2.
    assertThat(literal.getValue()).isEqualTo(Value.createInt64Value(2));

    column = projectScan.getExprList().get(1);
    assertThat(column.getExpr()).isInstanceOf(ResolvedFunctionCall.class);
    ResolvedFunctionCall call = (ResolvedFunctionCall) column.getExpr();
    assertThat(call.getArgumentList()).hasSize(2);
    assertThat(call.getArgumentList().get(1)).isInstanceOf(ResolvedLiteral.class);
    literal = (ResolvedLiteral) call.getArgumentList().get(1);
    // Check that 3.0 was doubled to 6.0
    assertThat(literal.getValue()).isEqualTo(Value.createDoubleValue(6.0));

    column = projectScan.getExprList().get(2);
    assertThat(column.getExpr()).isInstanceOf(ResolvedLiteral.class);
    literal = (ResolvedLiteral) column.getExpr();
    // Check that 5 was doubled to 10.
    assertThat(literal.getValue()).isEqualTo(Value.createInt64Value(10));

    assertThat(projectScan.getInputScan()).isInstanceOf(ResolvedFilterScan.class);
    ResolvedFilterScan filterScan = (ResolvedFilterScan) projectScan.getInputScan();
    assertThat(filterScan.getFilterExpr()).isInstanceOf(ResolvedFunctionCall.class);
    call = (ResolvedFunctionCall) filterScan.getFilterExpr();
    assertThat(call.getArgumentList()).hasSize(2);
    assertThat(call.getArgumentList().get(1)).isInstanceOf(ResolvedCast.class);
    ResolvedCast cast = (ResolvedCast) call.getArgumentList().get(1);
    assertThat(cast.getExpr()).isInstanceOf(ResolvedFunctionCall.class);
    call = (ResolvedFunctionCall) cast.getExpr();
    assertThat(call.getArgumentList()).hasSize(2);
    assertThat(call.getArgumentList().get(1)).isInstanceOf(ResolvedLiteral.class);
    literal = (ResolvedLiteral) call.getArgumentList().get(1);
    // Check that "7" was "doubled" to "77".
    assertThat(literal.getValue()).isEqualTo(Value.createStringValue("77"));
  }

  @Test
  public void noChanges() {
    String inputSql =
        "SELECT 1, double_field + 3.0, 5 FROM Temp"
            + " WHERE int32_field > CAST(CONCAT(CAST(int32_field AS STRING), \"7\") AS INT32)";
    ResolvedStatement resolvedStatement = analyzer.analyzeStatement(inputSql);
    ResolvedNode updatedStatement = resolvedStatement.accept(new RewritingVisitor() {});
    assertThat(updatedStatement).isSameInstanceAs(resolvedStatement);
  }
}
