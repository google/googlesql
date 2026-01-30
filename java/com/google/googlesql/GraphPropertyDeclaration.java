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

/**
 * Represents a property declaration in a property graph. A GraphPropertyDeclaration could be
 * exposed by one or more {link GraphElementLabel}. Within a property graph, there is one
 * GraphPropertyDeclaration for each unique property name. It guarantees the consistency of property
 * declaration.
 */
public interface GraphPropertyDeclaration extends Serializable {
  String getName();

  /** The fully-qualified name, including the property graph name. */
  String getFullName();

  Type getType();
}
