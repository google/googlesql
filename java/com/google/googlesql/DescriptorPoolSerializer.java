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

import com.google.common.collect.ImmutableMap;
import com.google.protobuf.DescriptorProtos.FileDescriptorSet;
import com.google.protobuf.Descriptors.FileDescriptor;
import com.google.googlesql.LocalService.DescriptorPoolListProto;
import java.util.Map;

/** Helper utilities to serialize {@link DescriptorPool}s for local service rpcs. */
class DescriptorPoolSerializer {
  private DescriptorPoolSerializer() {}

  static DescriptorPoolListProto createDescriptorPoolListWithRegisteredIds(
      FileDescriptorSetsBuilder fileDescriptorSetsBuilder,
      Map<DescriptorPool, Long> registeredDescriptorPoolIds) {
    DescriptorPoolListProto.Builder poolListProto = DescriptorPoolListProto.newBuilder();
    for (DescriptorPool pool : fileDescriptorSetsBuilder.getDescriptorPools()) {
      if (pool == BuiltinDescriptorPool.getInstance()) {
        poolListProto.addDefinitionsBuilder().getBuiltinBuilder();
      } else if (registeredDescriptorPoolIds.containsKey(pool)) {
        poolListProto
            .addDefinitionsBuilder()
            .setRegisteredId(registeredDescriptorPoolIds.get(pool));
      } else {
        FileDescriptorSet.Builder fileDescriptorSetBuilder =
            poolListProto.addDefinitionsBuilder().getFileDescriptorSetBuilder();
        for (FileDescriptor file : pool.getAllFileDescriptorsInDependencyOrder()) {
          fileDescriptorSetBuilder.addFile(file.toProto());
        }
      }
    }
    return poolListProto.build();
  }

  static DescriptorPoolListProto createDescriptorPoolList(
      FileDescriptorSetsBuilder fileDescriptorSetsBuilder) {
    return createDescriptorPoolListWithRegisteredIds(fileDescriptorSetsBuilder, ImmutableMap.of());
  }
}
