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

import static com.google.common.collect.ImmutableList.toImmutableList;

import com.google.auto.value.AutoValue;
import com.google.common.base.Preconditions;
import com.google.common.collect.ImmutableList;
import com.google.googlesql.LocalService.TableData;
import java.util.List;

/**
 * Class that represents the content of a table. For now the content can only be provided as a
 * matrix of rows and columns where each row represents a row in the table.
 */
@AutoValue
public abstract class TableContent {

  public static TableContent create(List<List<Value>> tableData) {
    Preconditions.checkNotNull(tableData, "Setting table's content to Null is not allowed");

    return new AutoValue_TableContent(
        tableData.stream().map(ImmutableList::copyOf).collect(toImmutableList()));
  }

  public abstract ImmutableList<ImmutableList<Value>> getTableData();

  /**
   * Serialize this content into probuf.
   *
   * @return A TableContent representing the table's content if this was set, or Null otherwise.
   */
  public com.google.googlesql.LocalService.TableContent serialize() {
    return com.google.googlesql.LocalService.TableContent.newBuilder()
        .setTableData(serializeTableData())
        .build();
  }

  private TableData serializeTableData() {
    TableData.Builder builder = TableData.newBuilder();
    for (List<Value> row : getTableData()) {
      TableData.Row.Builder rowBuilder = TableData.Row.newBuilder();
      for (Value cell : row) {
        rowBuilder.addCell(cell.serialize());
      }
      builder.addRow(rowBuilder);
    }

    return builder.build();
  }

  public static TableContent deserialize(
      ImmutableList<Type> columnsTypes,
      com.google.googlesql.LocalService.TableContent tableContent) {
    Preconditions.checkNotNull(columnsTypes);
    Preconditions.checkNotNull(tableContent);

    ImmutableList.Builder<List<Value>> tableDataBuilder = ImmutableList.builder();
    for (int i = 0; i < tableContent.getTableData().getRowCount(); i++) {
      TableData.Row row = tableContent.getTableData().getRow(i);
      Preconditions.checkArgument(
          row.getCellCount() == columnsTypes.size(),
          "Unexpected number of elements for row content: %s. Expected: %s, but received: %s.",
          i,
          columnsTypes.size(),
          row.getCellCount());
      ImmutableList.Builder<Value> rowBuilder = ImmutableList.builder();
      for (int j = 0; j < row.getCellCount(); j++) {
        rowBuilder.add(Value.deserialize(columnsTypes.get(j), row.getCell(j)));
      }
      tableDataBuilder.add(rowBuilder.build());
    }

    return TableContent.create(tableDataBuilder.build());
  }
}
