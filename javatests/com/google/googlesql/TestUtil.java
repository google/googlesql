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

import java.lang.reflect.Field;
import java.lang.reflect.Modifier;

public final class TestUtil {

  public static int getNonStaticFieldCount(Class<? extends Object> c) {
    int count = 0;
    for (Field field : c.getDeclaredFields()) {
      if (Modifier.isStatic(field.getModifiers())) {
        continue;
      }
      if (field.isSynthetic()) {
        continue;
      }
      ++count;
    }
    return count;
  }

  private TestUtil() {}
}
