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

/** Represents how a GraphEdgeTable references a GraphNodeTable as one of the endpoints. */
public interface GraphNodeTableReference extends Serializable {
  /** Returns the referenced {@link GraphNodeTable}. */
  GraphNodeTable getReferencedNodeTable();

  /**
   * Returns ordinal indexes of referencing columns from the {@link GraphEdgeTable} that references
   * the {@link GraphNodeTable} columns returned by getGraphNodeTableColumns()
   */
  List<Integer> getEdgeTableColumns();

  /** Returns ordinal indexes of columns from the referenced {@link GraphNodeTable}. */
  List<Integer> getNodeTableColumns();
}
