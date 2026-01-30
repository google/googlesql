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
import java.util.Set;

/**
 * Represents a label in a property graph. Each GraphElementLabel could expose a set of {@link
 * GraphPropertyDeclaration}.
 */
public interface GraphElementLabel extends Serializable {
  /** Returns the name of this Label. */
  String getName();

  /** The fully-qualified name, including the property graph name. */
  String getFullName();

  /**
   * Returns all {@link GraphPropertyDeclaration} exposed by this GraphElementLabel. The {@link
   * PropertyGraph}PropertyGraph owns {@link GraphPropertyDeclaration}.
   */
  Set<GraphPropertyDeclaration> getPropertyDeclarations();
}
