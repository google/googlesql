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

/**
 * A column in Table.
 */
public interface Column {

  /**
   * The column name.  Columns must have non-empty names.  Anonymous columns
   * are not supported.
   */
  public String getName();

  /**
   * The fully-qualified name, including the table name.
   */
  public String getFullName();

  public Type getType();

  /**
   * Pseudo-columns can be selected explicitly but do not show up in SELECT *. This can be used for
   * any hidden or virtual column or lazily computed value in a table.
   *
   * <p>Pseudo-columns can be used on value tables to provide additional named values outside the
   * content of the row value.
   *
   * <p>Pseudo-columns are normally not writable in INSERTs or UPDATEs, but this is up to the engine
   * and not checked by GoogleSQL.
   *
   * <p>Pseudo-columns are specified in more detail in the value tables spec:
   * (broken link)
   */
  public boolean isPseudoColumn();

  /** Returns true if the column is writable. */
  public boolean isWritableColumn();

  /**
   * Returns true if the column can be SET to DEFAULT in an UPDATE DML statement. Setting a
   * non-writable column to DEFAULT denotes that the generated column value must be re-evaluated.
   * This is currently only allowed for STORED VOLATILE generated columns, where
   * isWritableColumn()==false and canUpdateToDefault() is engine specific. See:
   * (broken link).
   */
  public boolean canUpdateToDefault();
}
