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
import com.google.errorprone.annotations.CheckReturnValue;
import com.google.googlesql.LocalService.EvaluateModifyResponse;
import java.util.List;

/**
 * It represents modifications to multiple rows in a single table and it is the result returned when
 * evaluating a modify SQL statement using {@link PreparedModify} or {@link EvaluatedModify}. Each
 * row can have a different type of DML operation.
 */
@CheckReturnValue
@AutoValue
public abstract class EvaluatorTableModifyResponse {

  private static EvaluatorTableModifyResponse create(String tableName, List<Row> content) {
    Preconditions.checkNotNull(tableName);
    Preconditions.checkNotNull(content);

    return new AutoValue_EvaluatorTableModifyResponse(tableName, ImmutableList.copyOf(content));
  }

  /** Get the name of the table being modified. */
  public abstract String getTableName();

  /** Get the modified values for all affected rows. */
  public abstract ImmutableList<Row> getContent();

  static EvaluatorTableModifyResponse deserialize(
      SimpleCatalog catalog, EvaluateModifyResponse response) {
    String tableName = response.getTableName();
    SimpleTable table = catalog.getTable(response.getTableName(), new Catalog.FindOptions());
    if (table == null) {
      throw new SqlException(
          String.format(
              "Returned table: '%s' not found inside the catalog: '%s'",
              response.getTableName(), catalog.getFullName()));
    }

    ImmutableList<Type> columnsTypes =
        table.getColumnList().stream().map(SimpleColumn::getType).collect(toImmutableList());
    ImmutableList<Row> content =
        response.getContentList().stream()
            .map(row -> Row.deserialize(columnsTypes, row))
            .collect(toImmutableList());

    return create(tableName, content);
  }

  /** Represents a single modified row in the table. */
  @AutoValue
  public abstract static class Row {

    /** Represents the type of DML operation performed. */
    public enum Operation {
      INSERT,
      DELETE,
      UPDATE
    };

    private static Row create(
        Operation operation, List<Value> content, List<Value> originalPrimaryKeysValues) {
      Preconditions.checkNotNull(operation);
      Preconditions.checkNotNull(content);
      Preconditions.checkNotNull(originalPrimaryKeysValues);

      return new AutoValue_EvaluatorTableModifyResponse_Row(
          operation,
          ImmutableList.copyOf(content),
          ImmutableList.copyOf(originalPrimaryKeysValues));
    }

    /** Get the type of DML operation on the current row. */
    public abstract Operation getOperation();

    /**
     * Get the modified values of the current row.
     *
     * <ol>
     *   <li>if getOperation() == INSERT, it returns the content of the new row to be inserted
     *   <li>if getOperation() == DELETE, it returns an empty list
     *   <li>if getOperation() == UPDATE, it returns the new content of the row to be updated
     * </ol>
     */
    public abstract ImmutableList<Value> getContent();

    /**
     * Returns the original values of the key columns of the current row. This can be used to
     * identify the modified row.
     */
    public abstract ImmutableList<Value> getOriginalPrimaryKeysValues();

    private static Row deserialize(
        ImmutableList<Type> columnsTypes, EvaluateModifyResponse.Row row) {
      Preconditions.checkNotNull(columnsTypes);
      Preconditions.checkNotNull(row);

      Operation operation = deserializeOperation(row.getOperation());

      ImmutableList.Builder<Value> contentBuilder = ImmutableList.builder();
      if (row.getCellCount() > 0) {
        Preconditions.checkArgument(
            row.getCellCount() == columnsTypes.size(),
            "Unexpected number of elements received. Expected: %s, but received: %s.",
            columnsTypes.size(),
            row.getCellCount());
        for (int i = 0; i < row.getCellCount(); i++) {
          contentBuilder.add(Value.deserialize(columnsTypes.get(i), row.getCell(i)));
        }
      }

      ImmutableList.Builder<Value> originalPrimaryKeysValuesBuilder = ImmutableList.builder();
      if (row.getOldPrimaryKeyCount() > 0) {
        Preconditions.checkArgument(
            row.getOldPrimaryKeyCount() <= columnsTypes.size(),
            "Unexpected number of primary keys received. Expected less than or equal to: %s, but"
                + " received: %s.",
            columnsTypes.size(),
            row.getOldPrimaryKeyCount());
        for (int i = 0; i < row.getOldPrimaryKeyCount(); i++) {
          originalPrimaryKeysValuesBuilder.add(
              Value.deserialize(columnsTypes.get(i), row.getOldPrimaryKey(i)));
        }
      }

      return create(operation, contentBuilder.build(), originalPrimaryKeysValuesBuilder.build());
    }

    private static Operation deserializeOperation(EvaluateModifyResponse.Row.Operation operation) {
      switch (operation) {
        case INSERT:
          return Operation.INSERT;
        case DELETE:
          return Operation.DELETE;
        case UPDATE:
          return Operation.UPDATE;
        default:
          throw new SqlException("Unknown Operation type received: " + operation);
      }
    }
  }
}
