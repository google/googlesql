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

import static com.google.common.base.Preconditions.checkNotNull;

import com.google.googlesql.SimplePropertyGraphProtos.SimpleGraphElementDynamicPropertiesProto;

/** A concrete implementation of the {@link GraphDynamicProperties} interface. */
public final class SimpleGraphDynamicProperties implements GraphDynamicProperties {
  private final String propertiesExpression;

  public SimpleGraphDynamicProperties(String propertiesExpression) {
    checkNotNull(propertiesExpression);
    this.propertiesExpression = propertiesExpression;
  }

  public SimpleGraphElementDynamicPropertiesProto serialize() {
    return SimpleGraphElementDynamicPropertiesProto.newBuilder()
        .setPropertiesExpression(propertiesExpression)
        .build();
  }

  public static SimpleGraphDynamicProperties deserialize(
      SimpleGraphElementDynamicPropertiesProto proto) {
    String propertiesExpression = proto.getPropertiesExpression();
    return new SimpleGraphDynamicProperties(propertiesExpression);
  }

  @Override
  public String getPropertiesExpression() {
    return propertiesExpression;
  }
}
