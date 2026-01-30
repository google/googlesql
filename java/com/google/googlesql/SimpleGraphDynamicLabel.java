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

import com.google.googlesql.SimplePropertyGraphProtos.SimpleGraphElementDynamicLabelProto;

/** A concrete implementation of the {@link GraphDynamicProperties} interface. */
public final class SimpleGraphDynamicLabel implements GraphDynamicLabel {
  private final String labelExpression;

  public SimpleGraphDynamicLabel(String labelExpression) {
    checkNotNull(labelExpression);
    this.labelExpression = labelExpression;
  }

  public SimpleGraphElementDynamicLabelProto serialize() {
    return SimpleGraphElementDynamicLabelProto.newBuilder()
        .setLabelExpression(labelExpression)
        .build();
  }

  public static SimpleGraphDynamicLabel deserialize(SimpleGraphElementDynamicLabelProto proto) {
    String labelExpression = proto.getLabelExpression();
    return new SimpleGraphDynamicLabel(labelExpression);
  }

  @Override
  public String getLabelExpression() {
    return labelExpression;
  }
}
