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

import com.google.common.collect.ImmutableSet;
import com.google.common.testing.EqualsTester;
import com.google.common.testing.SerializableTester;
import com.google.protobuf.Descriptors.Descriptor;
import com.google.protobuf.Descriptors.EnumDescriptor;
import com.google.protobuf.Descriptors.FieldDescriptor;
import com.google.protobuf.Descriptors.FileDescriptor;
import com.google.protobuf.Descriptors.OneofDescriptor;
import com.google.protobuf.Timestamp;
import com.google.googlesql.ImmutableDescriptorPool.ImmutableGoogleSQLDescriptor;
import com.google.googlesql.ImmutableDescriptorPool.ImmutableGoogleSQLEnumDescriptor;
import com.google.googlesql.ImmutableDescriptorPool.ImmutableGoogleSQLFieldDescriptor;
import com.google.googlesql.ImmutableDescriptorPool.ImmutableGoogleSQLFileDescriptor;
import com.google.googlesql.ImmutableDescriptorPool.ImmutableGoogleSQLOneofDescriptor;
import com.google.googlesqltest.TestSchemaProto.AnotherTestEnum;
import com.google.googlesqltest.TestSchemaProto.KitchenSinkPB;
import com.google.googlesqltest.TestSchemaProto.MessageWithMapField;
import com.google.googlesqltest.TestSchemaProto.TestEnum;

import java.util.LinkedHashSet;
import java.util.Set;
import java.util.stream.Collectors;
import org.junit.Test;
import org.junit.runner.RunWith;
import org.junit.runners.JUnit4;

@RunWith(JUnit4.class)

public class ImmutableDescriptorPoolTest {
  @Test
  public void testImmutableDescriptorPoolSerializableEmpty() {
    ImmutableDescriptorPool emptyPool = ImmutableDescriptorPool.builder().build();
    assertThat(emptyPool.getAllFileDescriptorsInDependencyOrder()).isEmpty();

    ImmutableDescriptorPool reserializedPool = SerializableTester.reserialize(emptyPool);
    assertThat(reserializedPool.getAllFileDescriptorsInDependencyOrder()).isEmpty();
  }

  private static Set<String> getFilenames(DescriptorPool pool) {
    return pool.getAllFileDescriptorsInDependencyOrder().stream()
        .map(FileDescriptor::getFullName)
        .collect(Collectors.toSet());
  }

  static void assertFileDescriptorsAreDependencyOrdered(DescriptorPool pool) {
    Set<String> fileNames = new LinkedHashSet<>();
    for (FileDescriptor file : pool.getAllFileDescriptorsInDependencyOrder()) {
      fileNames.add(file.getFullName());
      for (FileDescriptor dependency : file.getDependencies()) {
        assertThat(fileNames).contains(dependency.getFullName());
      }
    }
  }

  @Test
  public void testImmutableDescriptorPoolSerializable() {
    ImmutableDescriptorPool pool =
        ImmutableDescriptorPool.builder()
            .importFileDescriptor(TestEnum.getDescriptor().getFile())
            .build();
    assertFileDescriptorsAreDependencyOrdered(pool);
    EnumDescriptor testEnumDescriptor =
        pool.findEnumTypeByName("googlesql_test.TestEnum").getDescriptor();
    assertThat(testEnumDescriptor).isEqualTo(TestEnum.getDescriptor());

    ImmutableSet<String> expectedDescriptorNames =
        ImmutableSet.of(
            "googlesql/public/proto/wire_format_annotation.proto",
            "google/protobuf/descriptor.proto",
            "googlesql/public/proto/type_annotation.proto",
            "googlesql/testdata/test_schema.proto");
    assertThat(getFilenames(pool)).containsExactlyElementsIn(expectedDescriptorNames);

    ImmutableDescriptorPool reserializedPool = SerializableTester.reserialize(pool);
    assertFileDescriptorsAreDependencyOrdered(reserializedPool);
    assertThat(getFilenames(reserializedPool)).containsExactlyElementsIn(expectedDescriptorNames);

    EnumDescriptor reserializedTestEnumDescriptor =
        reserializedPool.findEnumTypeByName("googlesql_test.TestEnum").getDescriptor();

    // We expect that the descriptor will not be equal to the input.
    assertThat(reserializedTestEnumDescriptor).isNotEqualTo(testEnumDescriptor);
    // However, the proto should match
    assertThat(reserializedTestEnumDescriptor.toProto()).isEqualTo(testEnumDescriptor.toProto());
  }

  @Test
  public void testImmutableGoogleSQLFileDescriptorHashEquals() {
    ImmutableDescriptorPool pool = ImmutableDescriptorPool.builder().build();
    ImmutableDescriptorPool pool2 = ImmutableDescriptorPool.builder().build();

    FileDescriptor descriptor1 = TestEnum.getDescriptor().getFile();
    FileDescriptor descriptor2 = Timestamp.getDescriptor().getFile();
    new EqualsTester()
        .addEqualityGroup(
            ImmutableGoogleSQLFileDescriptor.create(pool, descriptor1),
            ImmutableGoogleSQLFileDescriptor.create(pool, descriptor1))
        .addEqualityGroup(ImmutableGoogleSQLFileDescriptor.create(pool, descriptor2))
        .addEqualityGroup(ImmutableGoogleSQLFileDescriptor.create(pool2, descriptor1))
        .testEquals();
  }

  @Test
  public void testImmutableGoogleSQLEnumDescriptorHashEquals() {
    ImmutableDescriptorPool pool = ImmutableDescriptorPool.builder().build();
    ImmutableDescriptorPool pool2 = ImmutableDescriptorPool.builder().build();

    EnumDescriptor descriptor1 = TestEnum.getDescriptor();
    EnumDescriptor descriptor2 = AnotherTestEnum.getDescriptor();
    new EqualsTester()
        .addEqualityGroup(
            ImmutableGoogleSQLEnumDescriptor.create(pool, descriptor1),
            ImmutableGoogleSQLEnumDescriptor.create(pool, descriptor1))
        .addEqualityGroup(ImmutableGoogleSQLEnumDescriptor.create(pool, descriptor2))
        .addEqualityGroup(ImmutableGoogleSQLEnumDescriptor.create(pool2, descriptor1))
        .testEquals();
  }

  @Test
  public void testImmutableGoogleSQLDescriptorHashEquals() {
    ImmutableDescriptorPool pool = ImmutableDescriptorPool.builder().build();
    ImmutableDescriptorPool pool2 = ImmutableDescriptorPool.builder().build();

    Descriptor descriptor1 = KitchenSinkPB.getDescriptor();
    Descriptor descriptor2 = MessageWithMapField.getDescriptor();
    new EqualsTester()
        .addEqualityGroup(
            ImmutableGoogleSQLDescriptor.create(pool, descriptor1),
            ImmutableGoogleSQLDescriptor.create(pool, descriptor1))
        .addEqualityGroup(ImmutableGoogleSQLDescriptor.create(pool, descriptor2))
        .addEqualityGroup(ImmutableGoogleSQLDescriptor.create(pool2, descriptor1))
        .testEquals();
  }

  @Test
  public void testImmutableGoogleSQLFieldDescriptorHashEquals() {
    ImmutableDescriptorPool pool = ImmutableDescriptorPool.builder().build();
    ImmutableDescriptorPool pool2 = ImmutableDescriptorPool.builder().build();

    FieldDescriptor descriptor1 = KitchenSinkPB.getDescriptor().findFieldByName("int64_key_1");
    FieldDescriptor descriptor2 = KitchenSinkPB.getDescriptor().findFieldByName("int64_key_2");
    new EqualsTester()
        .addEqualityGroup(
            ImmutableGoogleSQLFieldDescriptor.create(pool, descriptor1),
            ImmutableGoogleSQLFieldDescriptor.create(pool, descriptor1))
        .addEqualityGroup(ImmutableGoogleSQLFieldDescriptor.create(pool, descriptor2))
        .addEqualityGroup(ImmutableGoogleSQLFieldDescriptor.create(pool2, descriptor1))
        .testEquals();
  }

  @Test
  public void testImmutableGoogleSQLOneofDescriptorHashEquals() {
    ImmutableDescriptorPool pool = ImmutableDescriptorPool.builder().build();
    ImmutableDescriptorPool pool2 = ImmutableDescriptorPool.builder().build();

    // int32_one_of and string_one_of are both members of one_of_field
    OneofDescriptor descriptor1 =
        KitchenSinkPB.getDescriptor().findFieldByName("int32_one_of").getContainingOneof();
    OneofDescriptor descriptor1a =
        KitchenSinkPB.getDescriptor().findFieldByName("string_one_of").getContainingOneof();
    // int64_one_of is a member of one_of_field2
    OneofDescriptor descriptor2 =
        KitchenSinkPB.getDescriptor().findFieldByName("int64_one_of").getContainingOneof();
    new EqualsTester()
        .addEqualityGroup(
            ImmutableGoogleSQLOneofDescriptor.create(pool, descriptor1),
            ImmutableGoogleSQLOneofDescriptor.create(pool, descriptor1),
            ImmutableGoogleSQLOneofDescriptor.create(pool, descriptor1a))
        .addEqualityGroup(ImmutableGoogleSQLOneofDescriptor.create(pool, descriptor2))
        .addEqualityGroup(ImmutableGoogleSQLOneofDescriptor.create(pool2, descriptor1))
        .testEquals();
  }
}
