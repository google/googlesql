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

import com.google.common.collect.ImmutableList;
import java.io.Serializable;
import java.util.Set;

/**
 * A property graph object visible in a GoogleSQL query. It shares the same namespace with Tables.
 * The PropertyGraph owns unique instances of GraphElementTable(GraphNodeTable and GraphEdgeTable),
 * GraphElementLabel and GraphPropertyDeclarations. Objects owned by a PropertyGraph may refer to
 * other objects only within the same property graph. Eg, GraphElementLabel could refer to a set of
 * GraphPropertyDeclarations, GraphEdgeTable could refer to GraphNodeTable as its source/destination
 * GraphNodeReference etc.
 */
public interface PropertyGraph extends Serializable {
  // Gets the property graph name.
  String getName();

  /** The fully qualified name */
  ImmutableList<String> getNamePath();

  /** The user-friendly, fully-qualified name, including the catalog name. */
  String getFullName();

  /**
   * Finds the {@link GraphElementLabel} in this PropertyGraph with the <name>. Returns null if
   * there is no such {@link GraphElementLabel}.
   */
  GraphElementLabel findLabelByName(String name);

  /**
   * Finds the {@link GraphPropertyDeclaration} in this PropertyGraph with the <name>. Returns null
   * if there is no such {@link GraphPropertyDeclaration}.
   */
  GraphPropertyDeclaration findPropertyDeclarationByName(String name);

  /**
   * Finds the {@link GraphElementTable} in this PropertyGraph with the <name>. Returns null if
   * there is no such {@link GraphElementTable}.
   */
  GraphElementTable findElementTableByName(String name);

  /** Returns all {@link GraphNodeTable} owned by this PropertyGraph. */
  Set<GraphNodeTable> getNodeTables();

  // Returns all {@link GraphEdgeTable} owned by this PropertyGraph.
  Set<GraphEdgeTable> getEdgeTables();

  /** Returns all {@link GraphElementLabel} owned by this PropertyGraph. */
  Set<GraphElementLabel> getLabels();

  /** Returns all {@link GraphPropertyDeclaration} owned this PropertyGraph. */
  Set<GraphPropertyDeclaration> getPropertyDeclarations();
}
