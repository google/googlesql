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

import com.google.common.collect.ImmutableList;

/** Class that gives tests access to package-private methods. */
public final class TestAccess {

  private TestAccess() {}

  /** Returns {@link FileDescriptorSetsBuilder#getDescriptorPools}. */
  public static ImmutableList<DescriptorPool> getDescriptorPools(
      FileDescriptorSetsBuilder builder) {
    return builder.getDescriptorPools();
  }
}
