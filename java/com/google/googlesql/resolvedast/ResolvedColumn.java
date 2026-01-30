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

import com.google.common.base.Preconditions;
import com.google.googlesql.Type;
import java.io.Serializable;
import java.util.List;
import java.util.Objects;

/**
 * A column produced by part of a query (e.g. a scan or subquery).
 *
 * <p>This is used in the column_list of a resolved AST node to represent a "slot" in the "tuple"
 * produced by that logical operator. This is also used in expressions in ResolvedColumnRef to point
 * at the column selected during name resolution.
 *
 * <p>The column_id is the definitive identifier for a ResolvedColumn, and the column_id should be
 * used to match a ResolvedColumnRef in an expression to the scan that produces that column.
 * column_ids are unique within a query. If the same table is scanned multiple times, distinct
 * column_ids will be chosen for each scan.
 *
 * <p>Joins and other combining nodes may propagate ResolvedColumns from their inputs, with the same
 * column_ids.
 */
public final class ResolvedColumn implements Serializable {

  private final long id;
  private final String tableName;
  private final String name;
  private final Type type;
  // TODO: add annotationMap field after adding full implementation of AnnotationMap.

  public ResolvedColumn(long id, String tableName, String name, Type type) {
    this.id = id;
    this.tableName = Preconditions.checkNotNull(tableName);
    this.name = Preconditions.checkNotNull(name);
    this.type = Preconditions.checkNotNull(type);
  }

  public long getId() {
    return id;
  }

  public String getTableName() {
    return tableName;
  }

  public String getName() {
    return name;
  }

  public Type getType() {
    return type;
  }

  public String debugString() {
    return tableName + "." + name + "#" + id;
  }

  public String shortDebugString() {
    return name + "#" + id;
  }

  @Override
  public String toString() {
    return debugString();
  }

  /**
   * Format a list of ResolvedColumns. Uses an abbreviated outputformat if all columns have the same
   * tableName.
   *
   * <p>The implementation is nearly identical to the C++ implementation in
   * ResolvedColumnListToString. Since the outputs must be the same, the implementations should be
   * similar to make changes to the output easier.
   */
  public static String toString(List<ResolvedColumn> columns) {
    if (columns.isEmpty()) {
      return "[]";
    }
    String commonTableName = columns.get(0).tableName;
    // Use the regular format if we have only one column.
    boolean useCommonTableName = columns.size() > 1;
    for (int i = 1; i < columns.size(); i++) {
      if (!columns.get(i).tableName.equals(commonTableName)) {
        useCommonTableName = false;
        break;
      }
    }

    StringBuilder sb = new StringBuilder();
    if (useCommonTableName) {
      sb.append(commonTableName).append(".[");
      for (ResolvedColumn column : columns) {
        if (column != columns.get(0)) {
          sb.append(", ");
        }
        sb.append(column.shortDebugString());
      }
    } else {
      sb.append("[");
      for (ResolvedColumn column : columns) {
        if (column != columns.get(0)) {
          sb.append(", ");
        }
        sb.append(column.debugString());
      }
    }
    sb.append("]");
    return sb.toString();
  }

  public boolean isDefaultValue() {
    return id == -1;
  }

  /** To provide parity with resolved_column.h, equality is defined using columnId only. */
  @Override
  public boolean equals(Object o) {
    if (this == o) {
      return true;
    }

    if (o instanceof ResolvedColumn) {
      ResolvedColumn other = (ResolvedColumn) o;
        return Objects.equals(this.id, other.id);
    }
    return false;
  }

  /** To provide parity with resolved_column.h, hashCode is defined using columnId only. */
  @Override
  public int hashCode() {
    return Objects.hashCode(id);
  }
}
