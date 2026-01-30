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

import com.google.googlesql.SimpleSequenceProtos.SimpleSequenceProto;

/** SimpleSequence is a concrete implementation of the Sequence interface. */
public final class SimpleSequence implements Sequence {

  private final String name;
  private String fullName = "";

  public SimpleSequence(String name) {
    this.name = name;
  }

  @Override
  public String getName() {
    return name;
  }

  @Override
  public String getFullName() {
    return fullName.isEmpty() ? name : fullName;
  }

  public void setFullName(String fullName) {
    this.fullName = fullName;
  }

  /** Serialize this sequence into protobuf. */
  public SimpleSequenceProto serialize() {
    return SimpleSequenceProto.newBuilder().setName(name).setFullName(fullName).build();
  }

  /** Deserialize a proto into a new SimpleSequence. */
  public static SimpleSequence deserialize(SimpleSequenceProto proto) {
    SimpleSequence sequence = new SimpleSequence(proto.getName());
    sequence.setFullName(proto.getFullName());
    return sequence;
  }
}
