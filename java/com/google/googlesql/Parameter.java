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

import com.google.common.base.Ascii;
import com.google.common.collect.ImmutableMap;
import java.util.Map;

/**
 * Class containing utility methods for processing the parameters used in prepared expressions,
 * queries or modify statements
 */
final class Parameter {

  private Parameter() {}

  /** Serialize a name:value pair as a proto Parameter */
  static com.google.googlesql.LocalService.Parameter serialize(String name, Value value) {
    return com.google.googlesql.LocalService.Parameter.newBuilder()
        .setName(name)
        .setValue(value.serialize())
        .build();
  }

  /**
   * Normalize the map of parameters by transforming the parameters' names (keys) into lower case
   */
  static ImmutableMap<String, Type> normalize(Map<String, Type> parameters) {
    ImmutableMap.Builder<String, Type> lowerMapBuilder = new ImmutableMap.Builder<>();
    for (Map.Entry<String, Type> entry : parameters.entrySet()) {
      lowerMapBuilder.put(Ascii.toLowerCase(entry.getKey()), entry.getValue());
    }
    return lowerMapBuilder.buildOrThrow();
  }

  /**
   * Normalize a map of parameters by transforming the parameters' names (keys) into lower case and
   * also validate that each parameter is expected (part of the expected map)
   */
  static ImmutableMap<String, Value> normalizeAndValidate(
      Map<String, Value> parameters, Map<String, Type> expected, String kind) {
    ImmutableMap.Builder<String, Value> lowerMapBuilder = ImmutableMap.builder();
    for (Map.Entry<String, Value> entry : parameters.entrySet()) {
      String name = Ascii.toLowerCase(entry.getKey());
      if (!expected.containsKey(name)) {
        throw new SqlException("Unexpected " + kind + " parameter '" + name + "'");
      }

      Value value = entry.getValue();
      Type type = expected.get(name);
      if (!type.equals(value.getType())) {
        throw new SqlException(
            "Expected " + kind + " parameter '" + name + "' to be of type " + type);
      }

      lowerMapBuilder.put(name, value);
    }
    return lowerMapBuilder.buildOrThrow();
  }
}
