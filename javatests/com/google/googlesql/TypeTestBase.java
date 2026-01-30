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

import static com.google.common.truth.Truth.assertThat;
import static com.google.common.truth.Truth.assertWithMessage;

import com.google.common.testing.SerializableTester;
import com.google.protobuf.DescriptorProtos.FileDescriptorSet;
import com.google.googlesql.GoogleSQLType.TypeProto;
import java.util.ArrayList;
import java.util.List;

public final class TypeTestBase {
  // This method takes the input Type, serializes it, deserializes it, and
  // then serializes it again.  It checks the input Type and deserialized Type
  // for equality, and checks the input Type proto and deserialized Type proto
  // for equality.
  public static void checkTypeSerializationAndDeserialization(Type type) {
    checkTypeSerializationAndDeserializationSelfContained(type);
    checkTypeSerializationAndDeserializationExistingPools(type, null);
    checkTypeSerializationAndDeserializationExistingFileDescriptorSets(type);

    // TODO: Add other checks, e.g. see if the serialized proto can be
    // deserialized by C++ code correctly using SWIG.
  }

  public static void checkTypeSerializationAndDeserializationSelfContained(Type type) {
    TypeFactory factory = TypeFactory.nonUniqueNames();
    TypeProto proto = type.serialize();
    Type type2 = factory.deserialize(proto);
    TypeProto proto2 = type2.serialize();
    checkSerializedAndDeserializedTypes(type, type2, proto, proto2);
  }

  public static void checkTypeSerializationAndDeserializationExistingPools(
      Type type, List<DescriptorPool> pools) {
    TypeFactory factory = TypeFactory.nonUniqueNames();
    FileDescriptorSetsBuilder fileDescriptorSetsBuilder = new FileDescriptorSetsBuilder();
    TypeProto.Builder builder = TypeProto.newBuilder();
    type.serialize(builder, fileDescriptorSetsBuilder);
    if (pools == null) {
      pools = fileDescriptorSetsBuilder.getDescriptorPools();
    }
    Type type2 = factory.deserialize(builder.build(), pools);
    TypeProto proto = type.serialize();
    TypeProto proto2 = type2.serialize();
    checkSerializedAndDeserializedTypes(type, type2, proto, proto2);
  }

  public static void checkTypeSerializationAndDeserializationExistingFileDescriptorSets(Type type) {
    TypeFactory factory = TypeFactory.nonUniqueNames();
    FileDescriptorSetsBuilder fileDescriptorSetsBuilder = new FileDescriptorSetsBuilder();
    TypeProto.Builder builder = TypeProto.newBuilder();
    type.serialize(builder, fileDescriptorSetsBuilder);
    List<FileDescriptorSet> sets = fileDescriptorSetsBuilder.build();
    List<DescriptorPool> pools = new ArrayList<>();
    for (FileDescriptorSet set : sets) {
      GoogleSQLDescriptorPool pool = new GoogleSQLDescriptorPool();
      pool.importFileDescriptorSet(set);
      pools.add(pool);
    }
    Type type2 = factory.deserialize(builder.build(), pools);
    TypeProto proto = type.serialize();
    TypeProto proto2 = type2.serialize();
    checkSerializedAndDeserializedTypes(type, type2, proto, proto2);
  }

  public static void checkSerializedAndDeserializedTypes(
      Type type, Type type2, TypeProto proto, TypeProto proto2) {
    if (type.isMeasure()) {
      // Measures are not equivalent because they are not interchangeable. Instead, compare their
      // result types, which would be expected to be equivalent.
      assertThat(type.asMeasure().getResultType().equivalent(type2.asMeasure().getResultType()))
          .isTrue();
    } else {
      assertThat(type.equivalent(type2)).isTrue();
    }
    assertThat(proto).isEqualTo(proto2);
  }

  public static void checkSerializable(Type type) {
    Type copy = SerializableTester.reserialize(type);
    assertWithMessage("Type %s not equivalent to its serialized copy, %s", type, copy)
        .that(type.equivalent(copy))
        .isTrue();
  }

  public static GoogleSQLDescriptorPool getDescriptorPoolWithTypeProtoAndTypeKind() {
    TypeFactory factory = TypeFactory.nonUniqueNames();
    TypeProto proto = factory.createProtoType(TypeProto.class).serialize();
    assertThat(proto.getFileDescriptorSetList()).hasSize(1);
    GoogleSQLDescriptorPool pool = new GoogleSQLDescriptorPool();
    pool.importFileDescriptorSet(proto.getFileDescriptorSet(0));
    assertThat(pool.findMessageTypeByName("googlesql.TypeProto")).isNotNull();
    assertThat(pool.findEnumTypeByName("googlesql.TypeKind")).isNotNull();
    return pool;
  }

  private TypeTestBase() {}
}
