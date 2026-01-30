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
import static com.google.googlesql.TypeTestBase.getDescriptorPoolWithTypeProtoAndTypeKind;
import static org.junit.Assert.fail;

import com.google.common.testing.SerializableTester;
import com.google.protobuf.Descriptors.FileDescriptor;

import org.junit.Test;
import org.junit.runner.RunWith;
import org.junit.runners.JUnit4;

@RunWith(JUnit4.class)

public class FileDescriptorSetsBuilderTest {

  @Test
  public void testAddFileDesriptor() {
    GoogleSQLDescriptorPool pool1 = getDescriptorPoolWithTypeProtoAndTypeKind();
    GoogleSQLDescriptorPool pool2 = getDescriptorPoolWithTypeProtoAndTypeKind();
    FileDescriptorSetsBuilder builder = new FileDescriptorSetsBuilder();

    assertThat(builder.getFileDescriptorSetCount()).isEqualTo(0);

    FileDescriptor file1FromPool1 =
        pool1.findFileByName("googlesql/public/type.proto").getDescriptor();
    assertThat(builder.addFileDescriptor(file1FromPool1, pool1)).isEqualTo(0);
    assertThat(builder.getFileDescriptorSetCount()).isEqualTo(1);

    // add another file from the same pool
    FileDescriptor file2FromPool1 =
        pool1.findFileByName("google/protobuf/descriptor.proto").getDescriptor();
    assertThat(builder.addFileDescriptor(file2FromPool1, pool1)).isEqualTo(0);
    assertThat(builder.getFileDescriptorSetCount()).isEqualTo(1);

    // add the same file from the same pool again
    assertThat(builder.addFileDescriptor(file2FromPool1, pool1)).isEqualTo(0);
    assertThat(builder.getFileDescriptorSetCount()).isEqualTo(1);

    FileDescriptor file1FromPool2 =
        pool2.findFileByName("googlesql/public/type.proto").getDescriptor();
    // add file from a second pool
    assertThat(builder.addFileDescriptor(file1FromPool2, pool2)).isEqualTo(1);
    assertThat(builder.getFileDescriptorSetCount()).isEqualTo(2);

    // add another file from the second pool
    FileDescriptor file2FromPool2 =
        pool2.findFileByName("google/protobuf/descriptor.proto").getDescriptor();
    assertThat(builder.addFileDescriptor(file2FromPool2, pool2)).isEqualTo(1);
    assertThat(builder.getFileDescriptorSetCount()).isEqualTo(2);

    // add the first file again
    assertThat(builder.addFileDescriptor(file1FromPool1, pool1)).isEqualTo(0);
    assertThat(builder.getFileDescriptorSetCount()).isEqualTo(2);

    // add the last file again
    assertThat(builder.addFileDescriptor(file2FromPool2, pool2)).isEqualTo(1);
    assertThat(builder.getFileDescriptorSetCount()).isEqualTo(2);
  }

  @Test
  public void testNoChangeAfterBuild() {
    FileDescriptorSetsBuilder builder = new FileDescriptorSetsBuilder();
    GoogleSQLDescriptorPool pool1 = getDescriptorPoolWithTypeProtoAndTypeKind();
    FileDescriptor file1FromPool1 =
        pool1.findFileByName("googlesql/public/type.proto").getDescriptor();
    FileDescriptor file2FromPool1 =
        pool1.findFileByName("google/protobuf/descriptor.proto").getDescriptor();
    builder.addFileDescriptor(file1FromPool1, pool1);
    builder.build();

    // Exception will be thrown when adding descriptors after build().
    try {
      builder.addFileDescriptor(file2FromPool1, pool1);
      fail();
    } catch (IllegalStateException expected) {
    }

    try {
      builder.addAllFileDescriptors(pool1);
      fail();
    } catch (IllegalStateException expected) {
    }

    // Exception will be thrown when build() is called more than once.
    try {
      builder.build();
      fail();
    } catch (IllegalStateException expected) {
    }

    // OK to call getters.
    builder.getDescriptorPools();
    builder.getFileDescriptorSetCount();
  }

  @Test
  public void testAddFileDesriptorSerializable() {
    GoogleSQLDescriptorPool pool1 = getDescriptorPoolWithTypeProtoAndTypeKind();
    GoogleSQLDescriptorPool pool2 = getDescriptorPoolWithTypeProtoAndTypeKind();
    FileDescriptorSetsBuilder builder = new FileDescriptorSetsBuilder();

    // Add file from a pool.
    FileDescriptor file1FromPool1 =
        pool1.findFileByName("googlesql/public/type.proto").getDescriptor();
    builder.addFileDescriptor(file1FromPool1, pool1);
    assertThat(builder.getFileDescriptorSetCount()).isEqualTo(1);

    // Check that the serialized/deserialized builder contains one descriptor set.
    FileDescriptorSetsBuilder builder2 = SerializableTester.reserialize(builder);
    assertThat(builder2.getFileDescriptorSetCount()).isEqualTo(1);

    // Add file from a second pool.
    FileDescriptor file1FromPool2 =
        pool2.findFileByName("googlesql/public/type.proto").getDescriptor();

    builder.addFileDescriptor(file1FromPool2, pool2);
    assertThat(builder.getFileDescriptorSetCount()).isEqualTo(2);

    // Check that the serialized/deserialized builder contains both descriptor sets.
    builder2 = SerializableTester.reserialize(builder);
    assertThat(builder2.getFileDescriptorSetCount()).isEqualTo(2);
  }

  @Test
  public void testNoChangeAfterBuildSerializable() {
    FileDescriptorSetsBuilder builder = new FileDescriptorSetsBuilder();
    GoogleSQLDescriptorPool pool1 = getDescriptorPoolWithTypeProtoAndTypeKind();
    FileDescriptor file1FromPool1 =
        pool1.findFileByName("googlesql/public/type.proto").getDescriptor();
    FileDescriptor file2FromPool1 =
        pool1.findFileByName("google/protobuf/descriptor.proto").getDescriptor();
    builder.addFileDescriptor(file1FromPool1, pool1);
    builder.build();
    builder = SerializableTester.reserialize(builder);

    // Exception will be thrown when adding descriptors after build().
    try {
      builder.addFileDescriptor(file2FromPool1, pool1);
      fail();
    } catch (IllegalStateException expected) {
    }

    try {
      builder.addAllFileDescriptors(pool1);
      fail();
    } catch (IllegalStateException expected) {
    }

    // Exception will be thrown when build() is called more than once.
    try {
      builder.build();
      fail();
    } catch (IllegalStateException expected) {
    }

    // OK to call getters.
    builder.getDescriptorPools();
    builder.getFileDescriptorSetCount();
  }
}
