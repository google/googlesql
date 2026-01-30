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

import static com.google.common.base.Preconditions.checkNotNull;

import com.google.common.base.Splitter;
import com.google.common.collect.ImmutableList;
import com.google.googlesql.Column;
import com.google.googlesql.ColumnRefProto;
import com.google.googlesql.Connection;
import com.google.googlesql.ConnectionRefProto;
import com.google.googlesql.Constant;
import com.google.googlesql.ConstantRefProto;
import com.google.googlesql.DescriptorPool;
import com.google.googlesql.Function;
import com.google.googlesql.FunctionRefProto;
import com.google.googlesql.GraphElementLabel;
import com.google.googlesql.GraphElementLabelRefProto;
import com.google.googlesql.GraphElementTable;
import com.google.googlesql.GraphElementTableRefProto;
import com.google.googlesql.GraphPropertyDeclaration;
import com.google.googlesql.GraphPropertyDeclarationRefProto;
import com.google.googlesql.Model;
import com.google.googlesql.ModelRefProto;
import com.google.googlesql.NotFoundException;
import com.google.googlesql.Procedure;
import com.google.googlesql.ProcedureRefProto;
import com.google.googlesql.PropertyGraph;
import com.google.googlesql.PropertyGraphRefProto;
import com.google.googlesql.Sequence;
import com.google.googlesql.SequenceRefProto;
import com.google.googlesql.SimpleCatalog;
import com.google.googlesql.Table;
import com.google.googlesql.TableRefProto;
import com.google.googlesql.TableValuedFunction;
import com.google.googlesql.TableValuedFunctionRefProto;
import com.google.googlesql.TypeFactory;
import javax.annotation.Nullable;

/** Deserializes objects in the ResolvedAST which require catalog lookup */
public final class DeserializationHelper extends AbstractDeserializationHelper {

  // A reference to a SimpleCatalog is necessary to deserialize scalar functions and tables. These
  // should only be encountered in ASTs compiled from queries on catalogs.
  private final SimpleCatalog catalog;

  public DeserializationHelper(
      TypeFactory typeFactory,
      ImmutableList<? extends DescriptorPool> pools,
      SimpleCatalog catalog) {
    super(typeFactory, pools);
    this.catalog = checkNotNull(catalog);
  }

  @Override
  Constant deserialize(ConstantRefProto proto) {
    Constant constant;
    try {
      constant = catalog.findConstant(Splitter.on(".").splitToList(proto.getName()));
    } catch (NotFoundException e) {
      constant = null;
    }
    return checkNotNull(constant);
  }

  @Override
  Function deserialize(FunctionRefProto proto) {
    return checkNotNull(catalog.getFunctionByFullName(proto.getName()));
  }

  @Override
  TableValuedFunction deserialize(TableValuedFunctionRefProto proto) {
    return checkNotNull(catalog.getTvfByFullName(proto.getName()));
  }

  @Override
  Procedure deserialize(ProcedureRefProto proto) {
    ImmutableList.Builder<String> namePath = new ImmutableList.Builder<>();
    namePath.addAll(Splitter.on('.').split(proto.getName()));
    Procedure procedure;
    try {
      procedure = catalog.findProcedure(namePath.build());
    } catch (NotFoundException e) {
      procedure = null;
    }
    return checkNotNull(procedure);
  }

  @Override
  @Nullable
  Column deserialize(ColumnRefProto proto) {
    if (!proto.hasTableRef()) {
      return null;
    }
    ImmutableList.Builder<String> namePath = new ImmutableList.Builder<>();
    namePath.addAll(Splitter.on('.').split(proto.getTableRef().getFullName()));
    Table table;
    Column column;
    try {
      table = catalog.findTable(namePath.build());
      column = table.findColumnByName(proto.getName());
    } catch (NotFoundException e) {
      column = null;
    }
    return checkNotNull(column);
  }

  @Override
  Model deserialize(ModelRefProto proto) {
    return checkNotNull(
        catalog.getModelById(proto.getSerializationId()),
        "Could not find model '%s' in catalog.",
        proto.getName());
  }

  @Override
  Connection deserialize(ConnectionRefProto proto) {
    return checkNotNull(
        catalog.getConnectionByFullName(proto.getFullName()),
        "Could not find connection '%s' in catalog.",
        proto.getName());
  }

  @Override
  Sequence deserialize(SequenceRefProto proto) {
    return checkNotNull(
        catalog.getSequenceByFullName(proto.getFullName()),
        "Could not find sequence '%s' in catalog.",
        proto.getName());
  }

  @Override
  @Nullable
  Table deserialize(TableRefProto proto) {
    if (proto.hasSerializationId()) {
      return checkNotNull(
          catalog.getTableById(proto.getSerializationId()),
          "Could not find table '%s' in catalog.",
          proto.getName());
    }
    return null;
  }

  @Override
  PropertyGraph deserialize(PropertyGraphRefProto proto) {
    PropertyGraph propertyGraph;
    ImmutableList<String> path = ImmutableList.copyOf(Splitter.on('.').split(proto.getFullName()));
    try {
      propertyGraph = catalog.findPropertyGraph(path);
    } catch (NotFoundException e) {
      propertyGraph = null;
    }
    return checkNotNull(
        propertyGraph, "Could not find PropertyGraph '%s' in catalog.", proto.getFullName());
  }

  @Override
  GraphPropertyDeclaration deserialize(GraphPropertyDeclarationRefProto proto) {
    PropertyGraph propertyGraph = deserialize(proto.getPropertyGraph());
    GraphPropertyDeclaration propertyDeclaration =
        propertyGraph.findPropertyDeclarationByName(proto.getName());

    return checkNotNull(
        propertyDeclaration,
        "Could not find PropertyDeclaration '%s' in PropertyGraph '%s'.",
        proto.getName(),
        proto.getPropertyGraph().getFullName());
  }

  @Override
  GraphElementLabel deserialize(GraphElementLabelRefProto proto) {
    PropertyGraph propertyGraph = deserialize(proto.getPropertyGraph());
    GraphElementLabel graphElementLabel = propertyGraph.findLabelByName(proto.getName());

    return checkNotNull(
        graphElementLabel,
        "Could not find Graph Element Label '%s' in PropertyGraph '%s'.",
        proto.getName(),
        proto.getPropertyGraph().getFullName());
  }

  @Override
  GraphElementTable deserialize(GraphElementTableRefProto proto) {
    PropertyGraph propertyGraph = deserialize(proto.getPropertyGraph());
    GraphElementTable graphElementTable = propertyGraph.findElementTableByName(proto.getName());

    return checkNotNull(
        graphElementTable,
        "Could not find Graph Element Table'%s' in PropertyGraph '%s'.",
        proto.getName(),
        proto.getPropertyGraph().getFullName());
  }

}
