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

package com.google.googlesql.parser;

import static com.google.common.truth.Truth.assertThat;

import com.google.googlesql.LanguageOptions;
import com.google.googlesql.Parser;
import com.google.googlesql.parser.ASTNodes.ASTColumnList;
import com.google.googlesql.parser.ASTNodes.ASTIdentifier;
import com.google.googlesql.parser.ASTNodes.ASTInsertStatement;
import com.google.googlesql.parser.ASTNodes.ASTSelectList;
import com.google.googlesql.parser.ASTNodes.ASTStatement;

import java.util.ArrayList;
import java.util.List;
import org.junit.Test;
import org.junit.runner.RunWith;
import org.junit.runners.JUnit4;

@RunWith(JUnit4.class)

public class ParseTreeVisitorTest {

  private final LanguageOptions languageOptions = new LanguageOptions();

  private static class VisitASTSelectList extends ParseTreeVisitor {

    private boolean visited = false;

    public boolean wasVisited() {
      return this.visited;
    }

    @Override
    public void visit(ASTSelectList selectList) {
      this.visited = true;
    }
  }

  @Test
  public void testChildrenAreVisited() {
    String queryText = "SELECT 1;";

    ASTStatement parsedStatement = Parser.parseStatement(queryText, languageOptions);

    VisitASTSelectList visitor = new VisitASTSelectList();

    parsedStatement.accept(visitor);

    assertThat(visitor.wasVisited()).isTrue();
  }

  private static class ExtractColumnsFromInsertStmt extends ParseTreeVisitor {

    public final List<String> insertColumns = new ArrayList<>();

    @Override
    public void visit(ASTInsertStatement insertStmt) {
      ASTColumnList columnList = insertStmt.getColumnList();

      if (columnList != null) {
        columnList.getIdentifiers().stream()
            .map(ASTIdentifier::getIdString)
            .forEach(this.insertColumns::add);
      }
    }
  }

  @Test
  public void extractColumnsFromInsertStmt() {
    String insertStmtText = "INSERT INTO table(columnA, columnB) VALUES (1, 2);";

    ASTStatement parsedStatement = Parser.parseStatement(insertStmtText, languageOptions);

    ExtractColumnsFromInsertStmt visitor = new ExtractColumnsFromInsertStmt();

    parsedStatement.accept(visitor);

    assertThat(visitor.insertColumns).containsExactly("columnA", "columnB");
  }
}
