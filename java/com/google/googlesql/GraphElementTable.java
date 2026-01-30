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

import java.io.Serializable;
import java.util.List;
import java.util.Optional;
import java.util.Set;

/**
 * Represents an element (node or edge) table in a property graph. A GraphElementTable exposes a set
 * of {@link GraphElementLabel} and owns a set of {@link GraphPropertyDefinition}, which 1:1 define
 * the exposed {@link GraphPropertyDeclaration} by its {@link GraphElementLabel} set.
 */
public interface GraphElementTable extends Serializable {
  /** GraphElementTable can either be a {@link GraphNodeTable} or a {@link GraphEdgeTable} */
  enum Kind {
    NODE,
    EDGE
  }

  /** The cardinality of the dynamic label. */
  enum DynamicLabelCardinality {
    UNKNOWN,
    SINGLE,
    MULTIPLE,
  };

  /**
   * Returns the name which is a unique identifier of a GraphElementTable within the property graph.
   */
  String getName();

  /** The fully-qualified name, including the catalog name. */
  String getFullName();

  /** Returns the kind of this ElementTable. */
  Kind getKind();

  /**
   * Returns the {@link Table} identified by the <element table name> in an <element table
   * definition>.
   */
  Table getTable();

  /**
   * Returns the ordinal indexes of key columns. Key columns are specified from graph DDL statements
   * or implicitly the PK columns of the underlying table.
   */
  List<Integer> getKeyColumns();

  /**
   * Finds the {@link GraphPropertyDefinition} in this GraphElementTable, which maps to a {@link
   * GraphPropertyDeclaration} with the <name>. Returns null if GraphElementTable does not have any
   * GraphPropertyDefinition mapping to a GraphPropertyDeclaration by this <name>.
   */
  GraphPropertyDefinition findPropertyDefinitionByName(String name);

  /**
   * Finds the {@link GraphElementLabel} on this GraphElementTable with the <name>. Returns null if
   * there is no such GraphElementLabel.
   */
  GraphElementLabel findLabelByName(String name);

  /**
   * Returns all {@link GraphPropertyDefinition} exposed by GraphElementLabels on this
   * GraphElementTable
   */
  Set<GraphPropertyDefinition> getPropertyDefinitions();

  /** Returns all {@link GraphElementLabel} exposed by this GraphElementTable. */
  Set<GraphElementLabel> getLabels();

  /** Returns true if this GraphElementTable has {@link GraphDynamicLabel} defined. */
  boolean hasDynamicLabel();

  /** Returns the {@link GraphDynamicLabel} of this GraphElementTable. */
  GraphDynamicLabel getDynamicLabel();

  /** Returns the cardinality of the dynamic label. */
  DynamicLabelCardinality dynamicLabelCardinality();

  /** Returns true if this GraphElementTable has {@link GraphDynamicProperties} defined. */
  boolean hasDynamicProperties();

  /** Returns the {@link GraphDynamicProperties} of this GraphElementTable. */
  GraphDynamicProperties getDynamicProperties();
}
