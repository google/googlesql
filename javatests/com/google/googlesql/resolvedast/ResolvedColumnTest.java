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

import com.google.googlesql.GoogleSQLType.TypeKind;
import com.google.googlesql.Type;
import com.google.googlesql.TypeFactory;

import org.junit.Test;
import org.junit.runner.RunWith;
import org.junit.runners.JUnit4;

@RunWith(JUnit4.class)

public class ResolvedColumnTest {
  // Equality is defined based on column ID only.
  @Test
  public void equals_success() {
    final long columnId = 42L;
    final String tableAName = "table a";
    final String columnAName = "column_a";
    final String tableBName = "table b";
    final String columnBName = "column_b";
    final Type columnAType = TypeFactory.createSimpleType(TypeKind.TYPE_STRING);
    final Type columnBType = TypeFactory.createSimpleType(TypeKind.TYPE_INT64);
    ResolvedColumn colA = new ResolvedColumn(columnId, tableAName, columnAName, columnAType);
    ResolvedColumn colB = new ResolvedColumn(columnId, tableBName, columnBName, columnBType);

    assertThat(colA).isEqualTo(colB);
  }

  @Test
  public void equals_notEqual() {
    final long columnId = 42L;
    final String tableName = "table";
    final String columnName = "column_a";
    final Type columnType = TypeFactory.createSimpleType(TypeKind.TYPE_STRING);
    ResolvedColumn colA = new ResolvedColumn(columnId, tableName, columnName, columnType);
    ResolvedColumn colB = new ResolvedColumn(columnId + 1, tableName, columnName, columnType);

    assertThat(colA).isNotEqualTo(colB);
  }

  // Because equality is based only on column ID, so must be hash code.
  @Test
  public void hashCode_success() {
    final long columnId = 42L;
    final String tableAName = "table a";
    final String columnAName = "column_a";
    final String tableBName = "table b";
    final String columnBName = "column_b";
    final Type columnAType = TypeFactory.createSimpleType(TypeKind.TYPE_STRING);
    final Type columnBType = TypeFactory.createSimpleType(TypeKind.TYPE_INT64);
    ResolvedColumn colA = new ResolvedColumn(columnId, tableAName, columnAName, columnAType);
    ResolvedColumn colB = new ResolvedColumn(columnId, tableBName, columnBName, columnBType);

    assertThat(colA.hashCode()).isEqualTo(colB.hashCode());
  }

  @Test
  public void hashCode_different() {
    final long columnId = 42L;
    final String tableName = "table";
    final String columnName = "column_a";
    final Type columnType = TypeFactory.createSimpleType(TypeKind.TYPE_STRING);
    ResolvedColumn colA = new ResolvedColumn(columnId, tableName, columnName, columnType);
    ResolvedColumn colB = new ResolvedColumn(columnId + 1, tableName, columnName, columnType);

    assertThat(colA.hashCode()).isNotEqualTo(colB.hashCode());
  }
}
