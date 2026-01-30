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
import static org.junit.Assert.assertEquals;
import static org.junit.Assert.assertSame;
import static org.junit.Assert.assertTrue;
import static org.junit.Assert.fail;

import com.google.common.collect.ImmutableList;
import com.google.googlesql.GoogleSQLType.TypeKind;
import com.google.googlesql.SimpleTable;
import com.google.googlesql.Type;
import com.google.googlesql.TypeFactory;
import com.google.googlesql.Value;
import com.google.googlesql.resolvedast.ResolvedNodes.ResolvedColumnDefinition;
import com.google.googlesql.resolvedast.ResolvedNodes.ResolvedExpr;
import com.google.googlesql.resolvedast.ResolvedNodes.ResolvedLimitOffsetScan;
import com.google.googlesql.resolvedast.ResolvedNodes.ResolvedLiteral;
import com.google.googlesql.resolvedast.ResolvedNodes.ResolvedOption;
import com.google.googlesql.resolvedast.ResolvedNodes.ResolvedScan;
import com.google.googlesql.resolvedast.ResolvedNodes.ResolvedTableScan;
import org.junit.Test;
import org.junit.runner.RunWith;
import org.junit.runners.JUnit4;

@RunWith(JUnit4.class)
public class ResolvedNodesTest {
  private static final Type INT32_TYPE = TypeFactory.createSimpleType(TypeKind.TYPE_INT32);
  private static final Type INT64_TYPE = TypeFactory.createSimpleType(TypeKind.TYPE_INT64);
  private static final Type BOOL_TYPE = TypeFactory.createSimpleType(TypeKind.TYPE_BOOL);
  private static final ResolvedLiteral TRUE_LITERAL =
      ResolvedLiteral.builder()
          .setFloatLiteralId(-1)
          .setHasExplicitType(true)
          .setType(BOOL_TYPE)
          .setValue(Value.createBoolValue(true))
          .build();

  private static final ResolvedLiteral FIVE =
      ResolvedLiteral.builder()
          .setType(INT32_TYPE)
          .setFloatLiteralId(-1)
          .setHasExplicitType(true)
          .setValue(Value.createInt32Value(5))
          .build();
  private static final SimpleTable TABLE = new SimpleTable("SampleTable", ImmutableList.of());

  private static final ResolvedTableScan TABLE_SCAN =
      ResolvedTableScan.builder()
          .setColumnList(ImmutableList.of())
          .setColumnIndexList(ImmutableList.of(1L, 2L, 3L))
          .setForSystemTimeExpr(FIVE)
          .setTable(TABLE)
          .setAlias("no alias")
          .setIsOrdered(true)
          .build();

  @Test
  public void testBasicBuilder() {
    ResolvedLiteral literal =
        ResolvedLiteral.builder()
            .setType(INT32_TYPE)
            .setFloatLiteralId(-1)
            .setHasExplicitType(true)
            .setValue(Value.createInt32Value(5))
            .build();
    assertSame(INT32_TYPE, literal.getType());
    assertEquals(-1, literal.getFloatLiteralId());
    assertTrue(literal.getHasExplicitType());
    assertEquals(5, literal.getValue().getInt32Value());
  }

  @Test
  public void testBasicToBuilder() {
    ResolvedLiteral literal = FIVE.toBuilder().build();
    assertSame(FIVE.getType(), literal.getType());
    assertEquals(FIVE.getFloatLiteralId(), literal.getFloatLiteralId());
    assertEquals(FIVE.getHasExplicitType(), literal.getHasExplicitType());
    assertEquals(FIVE.getValue(), literal.getValue());
  }

  @Test
  public void testBasicToBuilderCanOverrideValue() {
    ResolvedLiteral literal = FIVE.toBuilder().setValue(Value.createInt32Value(4)).build();
    assertEquals(4, literal.getValue().getInt32Value());
  }

  @Test
  public void testBuilderFailsOnUnsetNotIgnorableField() {
    // FloatLiteralId and HasExplicitType are optional constructor arguments and can be omitted.
    ResolvedLiteral unused =
        ResolvedLiteral.builder().setType(INT32_TYPE).setValue(Value.createInt32Value(5)).build();
    try {
      // Value is required argument and must be set.
      ResolvedLiteral.builder()
          .setType(INT32_TYPE)
          .setFloatLiteralId(0)
          .setHasExplicitType(true)
          // .setValue(Value.createInt32Value(5))  // "forget" to set this.
          .build();
      fail("Expected IllegalArgumentException");
    } catch (IllegalArgumentException expected) {
    }
  }

  @Test
  public void testBuilderSucceedsOnUnsetIgnorableFields() {
    ResolvedColumnDefinition unused =
        ResolvedColumnDefinition.builder()
            .setName("testColumn")
            .setType(INT32_TYPE)
            .setIsHidden(false)
            // .setColumn() skip setting IGNORABLE field
            // .setGeneratedColumnInfo() skip setting IGNORABLE_DEFAULT field
            .build();
  }

  @Test
  public void testInheritedBasicToBuilder() {
    ResolvedExpr fiveAsExpr = FIVE;
    ResolvedExpr expr = fiveAsExpr.toBuilder().setType(INT64_TYPE).build();
    assertThat(expr).isInstanceOf(ResolvedLiteral.class);

    ResolvedLiteral literal = (ResolvedLiteral) expr;
    assertSame(INT64_TYPE, literal.getType());
    assertEquals(FIVE.getFloatLiteralId(), literal.getFloatLiteralId());
    assertEquals(FIVE.getHasExplicitType(), literal.getHasExplicitType());
    assertEquals(FIVE.getValue(), literal.getValue());
  }

  private static final ResolvedOption TEST_HINT =
      ResolvedOption.builder().setName("is_test").setValue(TRUE_LITERAL).setQualifier("").build();

  // Test that we can pass the Builder as its parent.
  private static void setHints(ResolvedScan.Builder builder) {
    builder.setHintList(ImmutableList.of(TEST_HINT));
  }

  @Test
  public void testComplexInheritedBuilder() {
    ResolvedLimitOffsetScan.Builder builder = ResolvedLimitOffsetScan.builder();
    setHints(builder);

    ResolvedLimitOffsetScan scan =
        builder
            .setIsOrdered(false) // Note, we override this on the next line
            .setInputScan(TABLE_SCAN)
            .setColumnList(ImmutableList.of())
            .setLimit(FIVE)
            .setOffset(TRUE_LITERAL) // notice: no semantic validation on build.
            .build();

    assertTrue(TABLE_SCAN.getIsOrdered());
    assertTrue(scan.getIsOrdered()); // propagated from TABLE_SCAN;
    assertThat(scan.getInputScan()).isSameInstanceAs(TABLE_SCAN);
    assertThat(scan.getColumnList()).isEmpty();
    assertThat(scan.getLimit()).isSameInstanceAs(FIVE);
    assertThat(scan.getOffset()).isSameInstanceAs(TRUE_LITERAL);
  }
}
