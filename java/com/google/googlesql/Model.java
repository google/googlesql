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

/** An ML Model in a GoogleSQL query. */
public interface Model extends Serializable {

  /** Get the model name. */
  public String getName();

  /**
   * Get a fully-qualified description of this Model. Suitable for log messages, but not necessarily
   * a valid SQL path expression.
   */
  public String getFullName();

  /**
   * Get this Model's serialization id, which is used when referencing models in serialized resolved
   * ASTs.
   */
  public long getId();

  /** Returns this Model's inputs, represented as Columns */
  public List<Column> getInputs();

  /** Returns this Model's outputs, represented as Columns */
  public List<Column> getOutputs();
}
