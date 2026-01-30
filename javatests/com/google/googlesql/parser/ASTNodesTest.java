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
import static com.google.common.io.Resources.getResource;
import static com.google.common.truth.Truth.assertThat;
import static java.nio.charset.StandardCharsets.UTF_8;

import com.google.common.io.Resources;
import com.google.protobuf.TextFormat;
import com.google.googlesql.parser.ASTNodes.ASTIntLiteral;
import com.google.googlesql.parser.ASTNodes.ASTQueryStatement;
import com.google.googlesql.parser.ASTNodes.ASTSelect;
import com.google.googlesql.parser.ASTNodes.ASTSelectColumn;
import com.google.googlesql.parser.ASTNodes.ASTStatement;
import java.io.IOException;
import org.junit.Test;
import org.junit.runner.RunWith;
import org.junit.runners.JUnit4;

@RunWith(JUnit4.class)
public class ASTNodesTest {

  @Test
  public void testQueryStatement1() throws IOException {
    // Serialized proto of simple query "select 123"
    String path = "protos/query_statement1.textproto";
    String textproto = Resources.toString(getResource(this.getClass(), path), UTF_8);
    AnyASTStatementProto proto = TextFormat.parse(textproto, AnyASTStatementProto.class);

    ASTQueryStatement stmt = (ASTQueryStatement) ASTStatement.deserialize(proto);

    ASTSelect select = (ASTSelect) stmt.getQuery().getQueryExpr();
    ASTSelectColumn col1 = select.getSelectList().getColumns().get(0);
    assertThat(((ASTIntLiteral) col1.getExpression()).getImage()).isEqualTo("123");
  }

  @Test
  public void testParseLocationRange() throws IOException {
    // Serialized proto of simple query "select 123", starts at index 0 and ends at 10
    String path = "protos/query_statement1.textproto";
    String textproto = Resources.toString(getResource(this.getClass(), path), UTF_8);
    AnyASTStatementProto proto = TextFormat.parse(textproto, AnyASTStatementProto.class);

    ASTQueryStatement stmt = (ASTQueryStatement) ASTStatement.deserialize(proto);

    assertThat(stmt.getParseLocationRange().start()).isEqualTo(0);
    assertThat(stmt.getParseLocationRange().end()).isEqualTo(10);
  }
  //TODO: Add test for more complex queries.
}
