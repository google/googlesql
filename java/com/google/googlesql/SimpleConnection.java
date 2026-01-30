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

import com.google.googlesql.SimpleConnectionProtos.SimpleConnectionProto;

/** SimpleConnection is a concrete implementation of the Connection interface. */
public final class SimpleConnection implements Connection {

  private final String name;

  public SimpleConnection(String name) {
    this.name = name;
  }

  @Override
  public String getName() {
    return this.name;
  }

  @Override
  public String getFullName() {
    return this.name;
  }

  public SimpleConnectionProto serialize() {
    return SimpleConnectionProto.newBuilder().setName(this.name).build();
  }

  public static SimpleConnection deserialize(SimpleConnectionProto proto) {
    return new SimpleConnection(proto.getName());
  }

  @Override
  public boolean equals(Object other) {
    return other instanceof SimpleConnection
        && ((SimpleConnection) other).getName().equals(this.name);
  }

  @Override
  public int hashCode() {
    return this.name.hashCode();
  }
}
