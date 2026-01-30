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

import com.google.common.base.Joiner;
import com.google.common.collect.ImmutableList;
import com.google.googlesql.GoogleSQLOptions.ProductMode;
import java.io.Serializable;
import java.util.ArrayList;
import java.util.List;
import java.util.Optional;

/** A table or table-like object visible in a GoogleSQL query. */
public interface Table extends Serializable {

  /**
   * Get the table name.
   */
  public String getName();

  /**
   * Get a fully-qualified description of this Table.
   * Suitable for log messages, but not necessarily a valid SQL path expression.
   */
  public String getFullName();

  public int getColumnCount();

  public Column getColumn(int i);

  public ImmutableList<? extends Column> getColumnList();

  public Optional<ImmutableList<Integer>> getPrimaryKey();

  public Optional<ImmutableList<Integer>> getRowIdentityColumns();

  public Column findColumnByName(String name);

  /**
   * If true, this table is a value table, and should act like each row is a single unnamed value
   * with some type rather than acting like each row is a vector of named columns. This can be used
   * to represent inputs where each row is actually one protocol buffer value.
   *
   * <p>The table must have at least one column, and the first column (column 0) is treated as the
   * value of the row. Additional columns may be present but must be pseudo-columns.
   *
   * <p>For more information on value tables, refer to the value tables spec:
   * (broken link)
   */
  public boolean isValueTable();

  public long getId();

  /**
   * Generates the SQL name for this table type, which will be reparseable as part of a query.
   * NOTE: Pseudo-columns such as _PARTITION_DATE are not included.
   *
   * <p>e.g. {@code TABLE<x INT64, y STRING>} for tables with named columns
   *         {@code TABLE<INT64, STRING>} for tables with anonymous columns
   */
  public default String getTableTypeName(ProductMode productMode) {
    List<String> strings = new ArrayList<>();
    for (Column column : getColumnList()) {
      // Skip pseudo-columns such as _PARTITION_DATE
      if (column.isPseudoColumn()) {
        continue;
      }
      if (!column.getName().isEmpty()) {
        strings.add(
            String.format(
                "%s %s",
                GoogleSQLStrings.toIdentifierLiteral(column.getName()),
                column.getType().typeName(productMode)));
      } else {
        strings.add(column.getType().typeName(productMode));
      }
    }
    return String.format("TABLE<%s>", Joiner.on(", ").join(strings));
  };
}
