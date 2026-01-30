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

import static com.google.common.base.Preconditions.checkArgument;
import static java.util.stream.Collectors.joining;

import com.google.common.collect.ImmutableList;
import com.google.googlesql.FunctionProtos.TVFRelationColumnProto;
import com.google.googlesql.FunctionProtos.TVFRelationProto;
import java.io.Serializable;
import java.util.List;
import java.util.Objects;

/**
 * This represents a relation passed as an input argument to a TVF, or returned from a TVF. It
 * either contains a list of columns, where each column contains a name and a type, or the relation
 * may be a value table. For the value table case, there should be exactly one column, with an empty
 * name.
 *
 * <p>TODO: Give this class a better name that suggests it is the schema of a table-valued
 * argument or return value. The word 'relation' implies that it might contain an entire table,
 * which is untrue.
 */
public class TVFRelation implements Serializable {
  private final ImmutableList<Column> columns;
  private final boolean isValueTable;

  private TVFRelation(Iterable<Column> columns, boolean isValueTable) {
    this.columns = ImmutableList.copyOf(columns);
    this.isValueTable = isValueTable;
  }

  /** Creates a new TVFRelation with a fixed list of column names and types. */
  public static TVFRelation createColumnBased(List<Column> columns) {
    return new TVFRelation(columns, /* isValueTable= */ false);
  }

  /** Creates a new value-table TVFRelation with a single column of 'type' with no name. */
  public static TVFRelation createValueTableBased(Type type) {
    return new TVFRelation(
        ImmutableList.of(Column.create(/* name= */ "", type, /* isPseudoColumn= */ false)),
        /* isValueTable= */ true);
  }

  public ImmutableList<Column> getColumns() {
    return columns;
  }

  public boolean isValueTable() {
    return isValueTable;
  }

  public TVFRelationProto serialize(FileDescriptorSetsBuilder fileDescriptorSetsBuilder) {
    TVFRelationProto.Builder protoBuilder = TVFRelationProto.newBuilder();
    for (Column col : columns) {
      protoBuilder.addColumn(
          TVFRelationColumnProto.newBuilder()
              .setName(col.getName())
              .setType(col.getType().serialize(fileDescriptorSetsBuilder))
              .setIsPseudoColumn(col.isPseudoColumn));
    }
    protoBuilder.setIsValueTable(isValueTable);
    return protoBuilder.build();
  }

  public static TVFRelation deserialize(
      TVFRelationProto proto,
      ImmutableList<? extends DescriptorPool> pools,
      TypeFactory typeFactory) {
    if (proto.getIsValueTable()) {
      ImmutableList.Builder<Column> columns = ImmutableList.builder();

      // The first column is the value column.
      TVFRelationColumnProto valueColumnProto = proto.getColumn(0);
      checkArgument(
          !valueColumnProto.getIsPseudoColumn(),
          "The first column in a value table relation cannot be a pseudo-column");
      Type valueType = typeFactory.deserialize(valueColumnProto.getType(), pools);
      columns.add(Column.create(/* name= */ "", valueType, /* isPseudoColumn= */ false));

      // Any additional columns must be pseudo-columns.
      for (int i = 1; i < proto.getColumnCount(); i++) {
        TVFRelationColumnProto columnProto = proto.getColumn(i);
        checkArgument(
            columnProto.getIsPseudoColumn(),
            "Additional columns in a value table relation must be pseudo-columns");
        Type type = typeFactory.deserialize(columnProto.getType(), pools);
        columns.add(Column.create(columnProto.getName(), type, /* isPseudoColumn= */ true));
      }
      return new TVFRelation(columns.build(), /* isValueTable= */ true);
    } else {
      ImmutableList.Builder<Column> columns = ImmutableList.builder();

      for (TVFRelationColumnProto columnProto : proto.getColumnList()) {
        Type type = typeFactory.deserialize(columnProto.getType(), pools);
        columns.add(
            Column.create(
                columnProto.getName(),
                type,
                columnProto.hasIsPseudoColumn() && columnProto.getIsPseudoColumn()));
      }
      return createColumnBased(columns.build());
    }
  }

  @Override
  public String toString() {
    return "TABLE<"
        + columns.stream()
            .map(c -> (!isValueTable || c.isPseudoColumn() ? c.getName() + " " : "") + c.getType())
            .collect(joining(", "))
        + ">";
  }

  @Override
  public boolean equals(Object o) {
    if (this == o) {
      return true;
    }
    if (!(o instanceof TVFRelation)) {
      return false;
    }
    TVFRelation that = (TVFRelation) o;
    return isValueTable == that.isValueTable && columns.equals(that.columns);
  }

  @Override
  public int hashCode() {
    return Objects.hash(columns, isValueTable);
  }

  /** A column of a {@link TVFRelation}. */
  public static class Column implements Serializable {
    private final String name;
    private final Type type;
    private final boolean isPseudoColumn;

    private Column(String name, Type type, boolean isPseudoColumn) {
      this.name = name;
      this.type = type;
      this.isPseudoColumn = isPseudoColumn;
    }

    public static Column create(String name, Type type, boolean isPseudoColumn) {
      return new Column(name, type, isPseudoColumn);
    }

    public static Column create(String name, Type type) {
      return new Column(name, type, false);
    }

    public String getName() {
      return name;
    }

    public Type getType() {
      return type;
    }

    public boolean isPseudoColumn() {
      return isPseudoColumn;
    }

    @Override
    public boolean equals(Object o) {
      if (this == o) {
        return true;
      }
      if (!(o instanceof Column)) {
        return false;
      }
      Column column = (Column) o;
      return name.equals(column.name)
          && type.equals(column.type)
          && isPseudoColumn == column.isPseudoColumn;
    }

    @Override
    public int hashCode() {
      return Objects.hash(name, type, isPseudoColumn);
    }
  }
}
