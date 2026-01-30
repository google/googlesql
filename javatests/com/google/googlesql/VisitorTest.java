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

import com.google.googlesql.resolvedast.ResolvedColumn;
import com.google.googlesql.resolvedast.ResolvedNode;
import com.google.googlesql.resolvedast.ResolvedNodes;
import com.google.googlesql.resolvedast.ResolvedNodes.ResolvedAggregateFunctionCall;
import com.google.googlesql.resolvedast.ResolvedNodes.ResolvedComputedColumn;
import com.google.googlesql.resolvedast.ResolvedNodes.ResolvedFunctionCall;
import com.google.googlesql.resolvedast.ResolvedNodes.ResolvedProjectScan;
import com.google.googlesql.resolvedast.ResolvedNodes.ResolvedStatement;
import com.google.googlesql.resolvedast.ResolvedNodes.ResolvedTableScan;
import com.google.googlesqltest.TestSchemaProto.KitchenSinkPB;


import java.util.ArrayList;
import java.util.List;
import java.util.concurrent.atomic.AtomicInteger;
import org.junit.Test;
import org.junit.runner.RunWith;
import org.junit.runners.JUnit4;

@RunWith(JUnit4.class)

public final class VisitorTest {

  @Test
  public void testVisitSimpleQuery() throws Exception {
    SimpleCatalog catalog = new SimpleCatalog("foo");
    catalog.addSimpleTable(
        SimpleTable.tableFromProto(catalog.getTypeFactory().createProtoType(KitchenSinkPB.class)));

    AnalyzerOptions options = new AnalyzerOptions();
    String sql =
        "select nullable_int, has_confusing_name"
            + " from KitchenSinkPB"
            + " order by int_with_default;";

    ResolvedStatement statement = Analyzer.analyzeStatement(sql, options, catalog);
    List<String> tables = new ArrayList<>();
    List<String> columns = new ArrayList<>();
    AtomicInteger defaultVisitCount = new AtomicInteger();
    statement.accept(
        new ResolvedNodes.Visitor() {
          @Override
          protected void defaultVisit(ResolvedNode node) {
            defaultVisitCount.incrementAndGet();
            super.defaultVisit(node);
          }

          @Override
          public void visit(ResolvedProjectScan node) {
            node.getExprList().stream()
                .map(ResolvedComputedColumn::getColumn)
                .map(ResolvedColumn::getName)
                .forEach(columns::add);
            super.visit(node);
          }

          @Override
          public void visit(ResolvedTableScan node) {
            tables.add(node.getTable().getName());
            int previousDefaultVisitCount = defaultVisitCount.get();
            super.visit(node);
            // Assert that calling super.visit() calls defaultVisit()
            assertThat(defaultVisitCount.get()).isGreaterThan(previousDefaultVisitCount);
          }
        });
    assertThat(tables).containsExactly("KitchenSinkPB");
    assertThat(columns).containsExactly("$orderbycol1", "nullable_int", "has_confusing_name");
  }

  @Test
  public void testDescendIntoFunctionArguments() throws Exception {
    SimpleCatalog catalog = new SimpleCatalog("foo");
    catalog.addGoogleSQLFunctions(new GoogleSQLBuiltinFunctionOptions());
    AnalyzerOptions options = new AnalyzerOptions();
    String sql = "SELECT IF(1 = 2, TRUE AND FALSE, TRUE OR FALSE);";
    ResolvedStatement statement = Analyzer.analyzeStatement(sql, options, catalog);

    List<String> functions = new ArrayList<>();
    statement.accept(
        new ResolvedNodes.Visitor() {
          @Override
          public void visit(ResolvedFunctionCall node) {
            functions.add(node.getFunction().getSqlName());
            super.visit(node);
          }
        });

    assertThat(functions).containsExactly("IF", "=", "AND", "OR");
  }

  @Test
  public void testDescendIntoAggregateFunctionAndArguments() throws Exception {
    SimpleCatalog catalog = new SimpleCatalog("foo");
    catalog.addGoogleSQLFunctions(new GoogleSQLBuiltinFunctionOptions());
    catalog.addSimpleTable(
        SimpleTable.tableFromProto(catalog.getTypeFactory().createProtoType(KitchenSinkPB.class)));
    AnalyzerOptions options = new AnalyzerOptions();
    String sql =
        "SELECT 1 - COUNT(1+2), ARRAY_AGG(int64_val) FROM KitchenSinkPB GROUP BY int64_key_1;";
    ResolvedStatement statement = Analyzer.analyzeStatement(sql, options, catalog);

    List<String> aggregateFunctions = new ArrayList<>();
    List<String> functions = new ArrayList<>();
    statement.accept(
        new ResolvedNodes.Visitor() {
          @Override
          public void visit(ResolvedAggregateFunctionCall node) {
            aggregateFunctions.add(node.getFunction().getSqlName());
            super.visit(node);
          }

          @Override
          public void visit(ResolvedFunctionCall node) {
            functions.add(node.getFunction().getSqlName());
            super.visit(node);
          }
        });

    assertThat(aggregateFunctions).containsExactly("COUNT", "ARRAY_AGG");
    assertThat(functions).containsExactly("-", "+");
  }
}
