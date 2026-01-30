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

import com.google.common.base.Preconditions;
import com.google.common.collect.ImmutableList;
import com.google.common.collect.Lists;
import com.google.googlesql.GoogleSQLType.TypeProto;
import com.google.googlesql.LocalService.TableFromProtoRequest;
import com.google.googlesql.SimpleTableProtos.SimpleAnonymizationInfoProto;
import com.google.googlesql.SimpleTableProtos.SimpleColumnProto;
import com.google.googlesql.SimpleTableProtos.SimpleTableProto;
import io.grpc.StatusRuntimeException;
import java.util.ArrayList;
import java.util.HashMap;
import java.util.HashSet;
import java.util.List;
import java.util.Map;
import java.util.Optional;
import java.util.Set;

/** SimpleTable is a concrete implementation of the Table interface. */
public final class SimpleTable implements Table {

  private static long nextTableId = 1;

  private static synchronized long getNextId() {
    return nextTableId++;
  }

  private static synchronized void updateNextIdIfNotGreaterThan(long id) {
    if (nextTableId <= id) {
      // We need to update nextTableId to avoid future conflicts.
      nextTableId = id + 1;
    }
  }

  private final long tableId;
  private final String name;
  // Uses name as fullName if empty. Never null.
  private String fullName = "";
  private boolean isValueTable = false;
  private List<SimpleColumn> columns = new ArrayList<>();
  private Map<String, SimpleColumn> columnsMap = new HashMap<>();
  private ImmutableList<Integer> primaryKey = null;
  private ImmutableList<Integer> rowIdentity = null;
  private Set<String> duplicateColumnNames = new HashSet<>();
  private boolean allowAnonymousColumnName = false;
  private boolean anonymousColumnSeen = false;
  private boolean allowDuplicateColumnNames = false;
  // TODO: Add support for value tables with userIdColumns, as this
  // implementation does not support them yet.
  private SimpleColumn userIdColumn = null;

  /** Make a table with the given Columns. Crashes if there are duplicate column names. */
  public SimpleTable(String name, List<SimpleColumn> columns) {
    this(name, getNextId());
    for (SimpleColumn column : columns) {
      addSimpleColumn(column);
    }
  }

  /**
   * Make a value table with row type {@code rowType}. The value column has no name visible in SQL
   * but will be called "value" in the resolved AST.
   */
  public SimpleTable(String name, Type rowType) {
    this(name, Lists.<SimpleColumn>newArrayList(new SimpleColumn(name, "value", rowType)));
  }

  /**
   * Make a table with no Columns.
   * @param name Name of table
   */
  public SimpleTable(String name) {
    this(name, getNextId());
  }

  SimpleTable(String name, long serializationId) {
    this.name = name;
    this.tableId = serializationId;
    // If serializationId is greater or equal to nextId, nextId may grow and
    // collide with it over time, we need to update nextId to avoid so.
    updateNextIdIfNotGreaterThan(serializationId);
  }

  /**
   * Serialize this table into protobuf, with
   * FileDescriptors emitted to the builder as needed.
   */
  public SimpleTableProto serialize(FileDescriptorSetsBuilder fileDescriptorSetsBuilder) {
    SimpleTableProto.Builder builder = SimpleTableProto.newBuilder();
    builder.setName(name);
    if (!fullName.isEmpty()) {
      builder.setFullName(fullName);
    }
    builder.setIsValueTable(isValueTable);
    builder.setSerializationId(tableId);
    if (allowAnonymousColumnName) {
      builder.setAllowAnonymousColumnName(true);
    }
    if (allowDuplicateColumnNames) {
      builder.setAllowDuplicateColumnNames(true);
    }
    for (SimpleColumn column : columns) {
      builder.addColumn(column.serialize(fileDescriptorSetsBuilder));
    }
    if (primaryKey != null) {
      for (Integer columnIndex : primaryKey) {
        builder.addPrimaryKeyColumnIndex(columnIndex);
      }
    }
    if (rowIdentity != null) {
      for (Integer columnIndex : rowIdentity) {
        builder.addRowIdentityColumnIndex(columnIndex);
      }
    }
    if (userIdColumn != null) {
      SimpleAnonymizationInfoProto.Builder anonymizationBuilder =
          SimpleAnonymizationInfoProto.newBuilder();
      anonymizationBuilder.addUseridColumnName(userIdColumn.getName());
      builder.setAnonymizationInfo(anonymizationBuilder.build());
    }
    return builder.build();
  }

  /**
   * Deserialize a proto into a new table with existing Descriptor pools. Types will be deserialized
   * using the given TypeFactory and Descriptors from the given pools. The DescriptorPools should
   * have been created by type serialization, and all proto types are treated as references into
   * these pools.
   *
   * @param proto
   * @param pools contains all proto type
   * @param factory for type creating
   * @return a new SimpleTable
   * @throws IllegalArgumentException if the proto is inconsistent.
   */
  public static SimpleTable deserialize(
      SimpleTableProto proto, ImmutableList<? extends DescriptorPool> pools, TypeFactory factory) {
    SimpleTable table;
    if (proto.hasSerializationId()) {
      table = new SimpleTable(proto.getName(), proto.getSerializationId());
    } else {
      table = new SimpleTable(proto.getName());
    }
    if (proto.hasFullName() && !proto.getFullName().equals(proto.getName())) {
      table.setFullName(proto.getFullName());
    }
    table.setAllowAnonymousColumnName(proto.getAllowAnonymousColumnName());
    table.setAllowDuplicateColumnNames(proto.getAllowDuplicateColumnNames());
    for (SimpleColumnProto column : proto.getColumnList()) {
      table.addSimpleColumn(SimpleColumn.deserialize(column, table.getFullName(), pools, factory));
    }
    if (proto.getPrimaryKeyColumnIndexCount() > 0) {
      table.setPrimaryKey(proto.getPrimaryKeyColumnIndexList());
    }
    if (proto.getRowIdentityColumnIndexCount() > 0) {
      table.setRowIdentity(proto.getRowIdentityColumnIndexList());
    }
    table.setIsValueTable(proto.getIsValueTable());
    if (proto.hasAnonymizationInfo()) {
      // TODO: Support value tables with anonymization userid columns.
      // This implementation will ignore anonymization userid columns for value
      // tables (the findColumnByName() call will return nullptr), so such
      // tables will be treated as if they do not support anonymization.  We
      // cannot currently throw an exception in this case, since then all
      // analyzer tests will fail because the sample catalog that gets
      // serialized/deserialized includes such tables and the test setup for
      // every test will fail.
      if (!proto.getAnonymizationInfo().getUseridColumnNameList().isEmpty()) {
        table.userIdColumn =
            table.findColumnByName(proto.getAnonymizationInfo().getUseridColumnName(0));
      }
    }
    return table;
  }

  /**
   * This method creates a table by getting its schema from a proto type. This can be used for
   * tables that are stored as protos. Based on googlesql annotations in the proto, this class will
   * decide what column names and types are present in the table, and whether the table is a value
   * table.
   *
   * @param protoType
   * @return SimpleTable
   */
  public static SimpleTable tableFromProto(ProtoType protoType) {
    FileDescriptorSetsBuilder fileDescriptorSetsBuilder = new FileDescriptorSetsBuilder();
    TypeProto.Builder typeProtoBuilder = TypeProto.newBuilder();
    protoType.serialize(typeProtoBuilder, fileDescriptorSetsBuilder);
    TableFromProtoRequest request = TableFromProtoRequest.newBuilder()
        .setProto(typeProtoBuilder.getProtoType())
        .setFileDescriptorSet(fileDescriptorSetsBuilder.build().get(0))
        .build();

    SimpleTableProto response;
    try {
      response = Client.getStub().getTableFromProto(request);
    } catch (StatusRuntimeException e) {
      throw new SqlException(e);
    }

    SimpleTable output =
        SimpleTable.deserialize(
            response, fileDescriptorSetsBuilder.getDescriptorPools(), TypeFactory.nonUniqueNames());
    return output;
  }

  @Override
  public long getId() {
    return tableId;
  }

  @Override
  public String getName() {
    return name;
  }

  @Override
  public String getFullName() {
    return fullName.isEmpty() ? name : fullName;
  }

  @Override
  public int getColumnCount() {
    return columns.size();
  }

  @Override
  public SimpleColumn getColumn(int i) {
    return columns.get(i);
  }

  @Override
  public ImmutableList<SimpleColumn> getColumnList() {
    return ImmutableList.copyOf(columns);
  }

  @Override
  public Optional<ImmutableList<Integer>> getPrimaryKey() {
    return Optional.ofNullable(primaryKey);
  }

  @Override
  public Optional<ImmutableList<Integer>> getRowIdentityColumns() {
    return rowIdentity == null ? getPrimaryKey() : Optional.ofNullable(rowIdentity);
  }

  @Override
  public SimpleColumn findColumnByName(String name) {
    if (name != null && !name.isEmpty() && columnsMap.containsKey(name.toLowerCase())) {
      return columnsMap.get(name.toLowerCase());
    } else {
      return null;
    }
  }

  @Override
  public boolean isValueTable() {
    return isValueTable;
  }

  public void setFullName(String value) {
    if (value == null || value.equals(name)) {
      fullName = "";
    } else {
      fullName = value;
    }
  }

  public void setIsValueTable(boolean value) {
    isValueTable = value;
  }

  public boolean allowAnonymousColumnName() {
    return allowAnonymousColumnName;
  }

  public void setAllowAnonymousColumnName(boolean value) {
    Preconditions.checkState(value || !anonymousColumnSeen);
    allowAnonymousColumnName = value;
  }

  public boolean allowDuplicateColumnNames() {
    return allowDuplicateColumnNames;
  }

  public SimpleColumn userIdColumn() {
    return userIdColumn;
  }

  public void setUserIdColumn(SimpleColumn column) {
    Preconditions.checkArgument(columns.contains(column));
    userIdColumn = column;
  }

  public void setAllowDuplicateColumnNames(boolean value) {
    Preconditions.checkState(value || duplicateColumnNames.isEmpty());
    allowDuplicateColumnNames = value;
  }

  /**
   * Add a column. Returns an error if constraints allowAnonymousColumnName or
   * allowDuplicateColumnNames are violated.
   * The added column will not be pseudo column.
   */
  public void addSimpleColumn(String name, Type type) {
    addSimpleColumn(
        name,
        type,
        /* isPseudoColumn = */ false,
        /* isWritableColumn = */ true,
        /* canUpdateToDefault = */ false);
  }

  /**
   * Add a column. Returns an error if constraints allowAnonymousColumnName or
   * allowDuplicateColumnNames are violated.
   */
  public void addSimpleColumn(
      String name,
      Type type,
      boolean isPseudoColumn,
      boolean isWritableColumn,
      boolean canUpdateToDefault) {
    SimpleColumn column =
        new SimpleColumn(
            this.name, name, type, isPseudoColumn, isWritableColumn, canUpdateToDefault);
    addSimpleColumn(column);
  }

  /**
   * Add a column. Returns an error if constraints allowAnonymousColumnName or
   * allowDuplicateColumnNames are violated.
   */
  public void addSimpleColumn(
      String name, Type type, boolean isPseudoColumn, boolean isWritableColumn) {
    SimpleColumn column =
        new SimpleColumn(
            this.name,
            name,
            type,
            isPseudoColumn,
            isWritableColumn,
            /* canUpdateToDefault = */ false);
    addSimpleColumn(column);
  }

  /**
   * Add a column. Returns an error if constraints allowAnonymousColumnName or
   * allowDuplicateColumnNames are violated.
   */
  public void addSimpleColumn(SimpleColumn column) {
    insertColumnToColumnMap(column);
    columns.add(column);
  }

  private void insertColumnToColumnMap(SimpleColumn column) {
    if (!allowAnonymousColumnName) {
      Preconditions.checkArgument(!column.getName().isEmpty(), "Empty column names not allowed");
    }

    String columnName = column.getName().toLowerCase();
    if (columnsMap.containsKey(columnName)) {
      Preconditions.checkArgument(
          allowDuplicateColumnNames, "Duplicate column in %s: %s", getFullName(), column.getName());
      columnsMap.remove(columnName);
      duplicateColumnNames.add(columnName);
    } else if (!duplicateColumnNames.contains(columnName)) {
      columnsMap.put(columnName, column);
    }

    if (column.getName().isEmpty()) {
      anonymousColumnSeen = true;
    }
  }

  public void setPrimaryKey(List<Integer> columnIndexes) {
    for (Integer columnIndex : columnIndexes) {
      Preconditions.checkArgument(
          columnIndex < getColumnCount(), "Invalid column index %s in primary key", columnIndex);
    }
    primaryKey = ImmutableList.copyOf(columnIndexes);
  }

  public void setRowIdentity(List<Integer> columnIndexes) {
    for (Integer columnIndex : columnIndexes) {
      Preconditions.checkArgument(
          columnIndex < getColumnCount(), "Invalid column index %s in row identity", columnIndex);
    }
    rowIdentity = ImmutableList.copyOf(columnIndexes);
  }
}
