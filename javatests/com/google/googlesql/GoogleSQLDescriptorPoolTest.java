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
import static org.junit.Assert.fail;

import com.google.common.collect.ImmutableSet;
import com.google.common.testing.SerializableTester;
import com.google.protobuf.DescriptorProtos.FileDescriptorSet;
import com.google.protobuf.Descriptors.EnumDescriptor;
import com.google.protobuf.Descriptors.FileDescriptor;
import com.google.protobuf.TextFormat;
import com.google.protobuf.TextFormat.ParseException;
import com.google.googlesqltest.TestSchemaProto.TestEnum;

import java.util.LinkedHashSet;
import java.util.Set;
import java.util.stream.Collectors;
import org.junit.Before;
import org.junit.Test;
import org.junit.runner.RunWith;
import org.junit.runners.JUnit4;

@RunWith(JUnit4.class)

public class GoogleSQLDescriptorPoolTest {
  private GoogleSQLDescriptorPool pool;

  @Before
  public void setUp() {
    pool = new GoogleSQLDescriptorPool();
  }

  @Test
  public void testEmptyFileDescriptorSet() {
    try {
      pool.importFileDescriptorSet(fileDescriptorSetFromTextProto(""));
    } catch (IllegalArgumentException e) {
      fail(e.toString());
    }
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
  public void testOneSelfContainedFileDescriptorSet() {
    pool.importFileDescriptorSet(
        fileDescriptorSetFromTextProto(
            "file {"
                + "  name: 'test/file.proto'"
                + "  package: 'test'"
                + "  message_type {"
                + "    name: 'msg'"
                + "    field {"
                + "      name: 'simple'"
                + "      number: 1"
                + "      label: LABEL_OPTIONAL"
                + "      type: TYPE_INT32"
                + "    }"
                + "  }"
                + "}"));
    assertThat(pool.findFileByName("test/file.proto")).isNotNull();
    assertThat(pool.findFileByName("test/file2.proto")).isNull();
    assertThat(pool.findMessageTypeByName("test.msg")).isNotNull();
    assertFileDescriptorsAreDependencyOrdered(pool);
  }

  @Test
  public void testIncompleteFileDescriptorSet() {
    try {
      pool.importFileDescriptorSet(
          fileDescriptorSetFromTextProto(
              "file {"
                  + "  name: 'test/file.proto'"
                  + "  package: 'test'"
                  + "  dependency: 'test/file2.proto'"
                  + "  message_type {"
                  + "    name: 'msg'"
                  + "    field {"
                  + "      name: 'simple'"
                  + "      number: 1"
                  + "      label: LABEL_OPTIONAL"
                  + "      type: TYPE_INT32"
                  + "    }"
                  + "  }"
                  + "}"));
      fail("Should throw with incomplete FileDescriptorSet.");
    } catch (IllegalArgumentException expected) {
    }
  }

  @Test
  public void testFileDescriptorSetRecursiveDependency() {
    try {
      pool.importFileDescriptorSet(
          fileDescriptorSetFromTextProto(
              "file {"
                  + "  name: 'test/file.proto'"
                  + "  package: 'test'"
                  + "  dependency: 'test/file.proto'"
                  + "  message_type {"
                  + "    name: 'msg'"
                  + "    field {"
                  + "      name: 'simple'"
                  + "      number: 1"
                  + "      label: LABEL_OPTIONAL"
                  + "      type: TYPE_INT32"
                  + "    }"
                  + "  }"
                  + "}"));
      fail("Should throw with recursive FileDescriptorSet.");
    } catch (IllegalArgumentException expected) {
    }
  }

  @Test
  public void testFileDescriptorUnknownType() {
    try {
      pool.importFileDescriptorSet(
          fileDescriptorSetFromTextProto(
              "file {"
                  + "  name: 'test/file.proto'"
                  + "  package: 'test'"
                  + "  message_type {"
                  + "    name: 'msg'"
                  + "    field {"
                  + "      name: 'simple'"
                  + "      number: 1"
                  + "      label: LABEL_OPTIONAL"
                  + "      type: TYPE_MESSAGE"
                  + "      type_name: 'unknown'"
                  + "    }"
                  + "  }"
                  + "}"));
      fail("Should throw with unresolveable type in FileDescriptorSet.");
    } catch (IllegalArgumentException expected) {
    }
  }

  @Test
  public void testCompleteFileDescriptorSetInOrder() {
    pool.importFileDescriptorSet(
        fileDescriptorSetFromTextProto(
            "file {"
                + "  name: 'test/file.proto'"
                + "  package: 'test'"
                + "  message_type {"
                + "    name: 'msg'"
                + "    field {"
                + "      name: 'simple'"
                + "      number: 1"
                + "      label: LABEL_OPTIONAL"
                + "      type: TYPE_INT32"
                + "    }"
                + "  }"
                + "}"
                + "file {"
                + "  name: 'test/file2.proto'"
                + "  package: 'test'"
                + "  dependency: 'test/file.proto'"
                + "  message_type {"
                + "    name: 'msg2'"
                + "    field {"
                + "      name: 'simple'"
                + "      number: 1"
                + "      label: LABEL_OPTIONAL"
                + "      type: TYPE_INT32"
                + "    }"
                + "  }"
                + "}"));
    assertThat(pool.findFileByName("test/file.proto")).isNotNull();
    assertThat(pool.findFileByName("test/file2.proto")).isNotNull();
    assertThat(pool.findMessageTypeByName("test.msg")).isNotNull();
    assertThat(pool.findMessageTypeByName("test.msg2")).isNotNull();
    assertFileDescriptorsAreDependencyOrdered(pool);
  }

  @Test
  public void testCompleteFileDescriptorSetOutOfOrder() {
    pool.importFileDescriptorSet(
        fileDescriptorSetFromTextProto(
            "file {"
                + "  name: 'test/file.proto'"
                + "  package: 'test'"
                + "  dependency: 'test/file2.proto'"
                + "  message_type {"
                + "    name: 'msg'"
                + "    field {"
                + "      name: 'simple'"
                + "      number: 1"
                + "      label: LABEL_OPTIONAL"
                + "      type: TYPE_INT32"
                + "    }"
                + "  }"
                + "}"
                + "file {"
                + "  name: 'test/file2.proto'"
                + "  package: 'test'"
                + "  message_type {"
                + "    name: 'msg2'"
                + "    field {"
                + "      name: 'simple'"
                + "      number: 1"
                + "      label: LABEL_OPTIONAL"
                + "      type: TYPE_INT32"
                + "    }"
                + "  }"
                + "}"));
    assertThat(pool.findFileByName("test/file.proto")).isNotNull();
    assertThat(pool.findFileByName("test/file2.proto")).isNotNull();
    assertThat(pool.findMessageTypeByName("test.msg")).isNotNull();
    assertThat(pool.findMessageTypeByName("test.msg2")).isNotNull();
    assertFileDescriptorsAreDependencyOrdered(pool);
  }

  @Test
  public void testCompleteFileDescriptorSetNestedTypes() {
    pool.importFileDescriptorSet(
        fileDescriptorSetFromTextProto(
            "file {"
                + "  name: 'test/file.proto'"
                + "  package: 'test'"
                + "  dependency: 'test/file2.proto'"
                + "  message_type {"
                + "    name: 'msg'"
                + "    field {"
                + "      name: 'simple'"
                + "      number: 1"
                + "      label: LABEL_OPTIONAL"
                + "      type: TYPE_INT32"
                + "    }"
                + "    nested_type {"
                + "     name: 't'"
                + "      field {"
                + "        name: 'bar'"
                + "        number: 1"
                + "        label: LABEL_OPTIONAL"
                + "        type: TYPE_MESSAGE"
                + "        type_name: '.test.msg2'"
                + "      }"
                + "    }"
                + "  }"
                + "}"
                + "file {"
                + "  name: 'test/file2.proto'"
                + "  package: 'test'"
                + "  message_type {"
                + "    name: 'msg2'"
                + "    field {"
                + "      name: 'simple'"
                + "      number: 1"
                + "      label: LABEL_OPTIONAL"
                + "      type: TYPE_INT32"
                + "    }"
                + "    enum_type {"
                + "     name: 'e'"
                + "     value {"
                + "       name: 'foo'"
                + "       number: 1"
                + "     }"
                + "    }"
                + "  }"
                + "}"));
    assertThat(pool.findFileByName("test/file.proto")).isNotNull();
    assertThat(pool.findFileByName("test/file2.proto")).isNotNull();
    assertThat(pool.findMessageTypeByName("test.msg")).isNotNull();
    assertThat(pool.findMessageTypeByName("test.msg.t")).isNotNull();
    assertThat(pool.findMessageTypeByName("test.msg2")).isNotNull();
    assertThat(pool.findEnumTypeByName("test.msg2.e")).isNotNull();
    assertFileDescriptorsAreDependencyOrdered(pool);
  }

  @Test
  public void testCompleteFileDescriptorWithSimpleExtension() throws Exception {
    pool.importFileDescriptorSet(
        fileDescriptorSetFromTextProto(
            "file {"
                + "  name: 'test/file.proto'"
                + "  package: 'test'"
                + "  message_type {"
                + "    name: 'msg'"
                + "    field {"
                + "      name: 'simple'"
                + "      number: 1"
                + "      label: LABEL_OPTIONAL"
                + "      type: TYPE_INT32"
                + "    }"
                + "    extension_range {"
                + "      start: 100"
                + "      end: 200"
                + "    }"
                + "  }"
                + "}"
                + "file {"
                + "  name: 'test/file2.proto'"
                + "  package: 'test'"
                + "  dependency: 'test/file.proto'"
                + "  extension {"
                + "    name: 'simple'"
                + "    number: 100"
                + "    extendee: '.test.msg'"
                + "    label: LABEL_OPTIONAL"
                + "    type: TYPE_INT32"
                + "  }"
                + "}"));
    assertThat(pool.findFileByName("test/file.proto")).isNotNull();
    assertThat(pool.findFileByName("test/file2.proto")).isNotNull();
    assertThat(pool.findMessageTypeByName("test.msg")).isNotNull();
    assertThat(pool.findMessageTypeByName("test.msg").findFieldByNumber(100)).isNotNull();
    assertFileDescriptorsAreDependencyOrdered(pool);
  }

  @Test
  public void testCompleteFileDescriptorSetOverwrites() {
    pool.importFileDescriptorSet(
        fileDescriptorSetFromTextProto(
            "file {"
                + "  name: 'test/file.proto'"
                + "  package: 'test'"
                + "  dependency: 'test/file2.proto'"
                + "  message_type {"
                + "    name: 'msg'"
                + "    field {"
                + "      name: 'simple'"
                + "      number: 1"
                + "      label: LABEL_OPTIONAL"
                + "      type: TYPE_INT32"
                + "    }"
                + "  }"
                + "}"
                + "file {"
                + "  name: 'test/file2.proto'"
                + "  package: 'test'"
                + "  message_type {"
                + "    name: 'msg2'"
                + "    field {"
                + "      name: 'simple'"
                + "      number: 1"
                + "      label: LABEL_OPTIONAL"
                + "      type: TYPE_INT32"
                + "    }"
                + "  }"
                + "}"));
    assertThat(pool.findFileByName("test/file.proto")).isNotNull();
    assertThat(pool.findFileByName("test/file2.proto")).isNotNull();
    assertThat(pool.findMessageTypeByName("test.msg")).isNotNull();
    assertThat(pool.findMessageTypeByName("test.msg.t")).isNull();
    assertThat(pool.findMessageTypeByName("test.msg2")).isNotNull();
    assertThat(pool.findEnumTypeByName("test.msg2.e")).isNull();
    assertFileDescriptorsAreDependencyOrdered(pool);

    pool.importFileDescriptorSetWithOverwrite(
        fileDescriptorSetFromTextProto(
            "file {"
                + "  name: 'test/file.proto'"
                + "  package: 'test'"
                + "  dependency: 'test/file2.proto'"
                + "  message_type {"
                + "    name: 'msg'"
                + "    field {"
                + "      name: 'simple'"
                + "      number: 1"
                + "      label: LABEL_OPTIONAL"
                + "      type: TYPE_INT32"
                + "    }"
                + "    nested_type {"
                + "     name: 't'"
                + "      field {"
                + "        name: 'bar'"
                + "        number: 1"
                + "        label: LABEL_OPTIONAL"
                + "        type: TYPE_MESSAGE"
                + "        type_name: '.test.msg2'"
                + "      }"
                + "    }"
                + "  }"
                + "}"
                + "file {"
                + "  name: 'test/file2.proto'"
                + "  package: 'test'"
                + "  message_type {"
                + "    name: 'msg2'"
                + "    field {"
                + "      name: 'simple'"
                + "      number: 1"
                + "      label: LABEL_OPTIONAL"
                + "      type: TYPE_INT32"
                + "    }"
                + "    enum_type {"
                + "     name: 'e'"
                + "     value {"
                + "       name: 'foo'"
                + "       number: 1"
                + "     }"
                + "    }"
                + "  }"
                + "}"));
    assertThat(pool.findFileByName("test/file.proto")).isNotNull();
    assertThat(pool.findFileByName("test/file2.proto")).isNotNull();
    assertThat(pool.findMessageTypeByName("test.msg")).isNotNull();
    assertThat(pool.findMessageTypeByName("test.msg.t")).isNotNull();
    assertThat(pool.findMessageTypeByName("test.msg2")).isNotNull();
    assertThat(pool.findEnumTypeByName("test.msg2.e")).isNotNull();
    assertFileDescriptorsAreDependencyOrdered(pool);
  }

  @Test
  public void testCompleteFileDescriptorWithMessageExtension() throws Exception {
    pool.importFileDescriptorSet(
        fileDescriptorSetFromTextProto(
            "file {"
                + "  name: 'test/file.proto'"
                + "  package: 'test'"
                + "  message_type {"
                + "    name: 'msg'"
                + "    field {"
                + "      name: 'simple'"
                + "      number: 1"
                + "      label: LABEL_OPTIONAL"
                + "      type: TYPE_INT32"
                + "    }"
                + "    extension_range {"
                + "      start: 100"
                + "      end: 200"
                + "    }"
                + "  }"
                + "}"
                + "file {"
                + "  name: 'test/file2.proto'"
                + "  package: 'test'"
                + "  dependency: 'test/file.proto'"
                + "  extension {"
                + "    name: 'simple'"
                + "    number: 100"
                + "    extendee: '.test.msg'"
                + "    label: LABEL_OPTIONAL"
                + "    type: TYPE_MESSAGE"
                + "    type_name: '.test.msg2'"
                + "  }"
                + "  message_type {"
                + "    name: 'msg2'"
                + "    field {"
                + "      name: 'simple'"
                + "      number: 1"
                + "      label: LABEL_OPTIONAL"
                + "      type: TYPE_INT32"
                + "    }"
                + "  }"
                + "}"));
    assertThat(pool.findFileByName("test/file.proto")).isNotNull();
    assertThat(pool.findFileByName("test/file2.proto")).isNotNull();
    assertThat(pool.findMessageTypeByName("test.msg")).isNotNull();
    assertThat(pool.findMessageTypeByName("test.msg2")).isNotNull();
    assertThat(pool.findMessageTypeByName("test.msg").findFieldByNumber(100)).isNotNull();
    assertFileDescriptorsAreDependencyOrdered(pool);
  }

  @Test
  public void testCompleteFileDescriptorSetNestedTypesSerializable() {
    pool.importFileDescriptorSet(
        fileDescriptorSetFromTextProto(
            "file {"
                + "  name: 'test/file.proto'"
                + "  package: 'test'"
                + "  dependency: 'test/file2.proto'"
                + "  message_type {"
                + "    name: 'msg'"
                + "    field {"
                + "      name: 'simple'"
                + "      number: 1"
                + "      label: LABEL_OPTIONAL"
                + "      type: TYPE_INT32"
                + "    }"
                + "    nested_type {"
                + "     name: 't'"
                + "      field {"
                + "        name: 'bar'"
                + "        number: 1"
                + "        label: LABEL_OPTIONAL"
                + "        type: TYPE_MESSAGE"
                + "        type_name: '.test.msg2'"
                + "      }"
                + "    }"
                + "  }"
                + "}"
                + "file {"
                + "  name: 'test/file2.proto'"
                + "  package: 'test'"
                + "  message_type {"
                + "    name: 'msg2'"
                + "    field {"
                + "      name: 'simple'"
                + "      number: 1"
                + "      label: LABEL_OPTIONAL"
                + "      type: TYPE_INT32"
                + "    }"
                + "    enum_type {"
                + "     name: 'e'"
                + "     value {"
                + "       name: 'foo'"
                + "       number: 1"
                + "     }"
                + "    }"
                + "  }"
                + "}"));
    GoogleSQLDescriptorPool pool2 = SerializableTester.reserialize(pool);
    assertThat(pool2.findFileByName("test/file.proto")).isNotNull();
    assertThat(pool2.findFileByName("test/file2.proto")).isNotNull();
    assertThat(pool2.findMessageTypeByName("test.msg")).isNotNull();
    assertThat(pool2.findMessageTypeByName("test.msg.t")).isNotNull();
    assertThat(pool2.findMessageTypeByName("test.msg2")).isNotNull();
    assertThat(pool2.findEnumTypeByName("test.msg2.e")).isNotNull();
    assertFileDescriptorsAreDependencyOrdered(pool);
  }

  @Test
  public void testCompleteFileDescriptorWithMessageExtensionSerializable() {
    pool.importFileDescriptorSet(
        fileDescriptorSetFromTextProto(
            "file {"
                + "  name: 'test/file.proto'"
                + "  package: 'test'"
                + "  message_type {"
                + "    name: 'msg'"
                + "    field {"
                + "      name: 'simple'"
                + "      number: 1"
                + "      label: LABEL_OPTIONAL"
                + "      type: TYPE_INT32"
                + "    }"
                + "    extension_range {"
                + "      start: 100"
                + "      end: 200"
                + "    }"
                + "  }"
                + "}"
                + "file {"
                + "  name: 'test/file2.proto'"
                + "  package: 'test'"
                + "  dependency: 'test/file.proto'"
                + "  extension {"
                + "    name: 'simple'"
                + "    number: 100"
                + "    extendee: '.test.msg'"
                + "    label: LABEL_OPTIONAL"
                + "    type: TYPE_MESSAGE"
                + "    type_name: '.test.msg2'"
                + "  }"
                + "  message_type {"
                + "    name: 'msg2'"
                + "    field {"
                + "      name: 'simple'"
                + "      number: 1"
                + "      label: LABEL_OPTIONAL"
                + "      type: TYPE_INT32"
                + "    }"
                + "  }"
                + "}"));
    GoogleSQLDescriptorPool pool2 = SerializableTester.reserialize(pool);
    assertThat(pool2.findFileByName("test/file.proto")).isNotNull();
    assertThat(pool2.findFileByName("test/file2.proto")).isNotNull();
    assertThat(pool2.findMessageTypeByName("test.msg")).isNotNull();
    assertThat(pool2.findMessageTypeByName("test.msg2")).isNotNull();
    assertThat(pool2.findMessageTypeByName("test.msg").findFieldByNumber(100)).isNotNull();
    assertFileDescriptorsAreDependencyOrdered(pool);
  }

  @Test
  public void testGoogleSQLDescriptorPoolSerializableEmpty() {
    GoogleSQLDescriptorPool emptyPool = new GoogleSQLDescriptorPool();
    assertThat(emptyPool.getAllFileDescriptorsInDependencyOrder()).isEmpty();

    GoogleSQLDescriptorPool reserializedPool = SerializableTester.reserialize(emptyPool);
    assertThat(reserializedPool.getAllFileDescriptorsInDependencyOrder()).isEmpty();
  }

  private static Set<String> getFilenames(DescriptorPool pool) {
    return pool.getAllFileDescriptorsInDependencyOrder().stream()
        .map(FileDescriptor::getFullName)
        .collect(Collectors.toSet());
  }

  @Test
  public void testGoogleSQLDescriptorPoolSerializable() {
    GoogleSQLDescriptorPool pool = new GoogleSQLDescriptorPool();
    pool.addFileDescriptor(TestEnum.getDescriptor().getFile());
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

    GoogleSQLDescriptorPool reserializedPool = SerializableTester.reserialize(pool);
    assertThat(getFilenames(reserializedPool)).containsExactlyElementsIn(expectedDescriptorNames);

    EnumDescriptor reserializedTestEnumDescriptor =
        reserializedPool.findEnumTypeByName("googlesql_test.TestEnum").getDescriptor();

    // We expect that the descriptor will not be equal to the input.
    assertThat(reserializedTestEnumDescriptor).isNotEqualTo(testEnumDescriptor);
    // However, the proto should match
    assertThat(reserializedTestEnumDescriptor.toProto()).isEqualTo(testEnumDescriptor.toProto());
  }

  @Test
  public void testGeneratedDescriptorPoolSerializable() {
    GoogleSQLDescriptorPool pool = GoogleSQLDescriptorPool.getGeneratedPool();
    assertFileDescriptorsAreDependencyOrdered(GoogleSQLDescriptorPool.getGeneratedPool());
    GoogleSQLDescriptorPool.importIntoGeneratedPool(TestEnum.getDescriptor());
    EnumDescriptor testEnumDescriptor =
        pool.findEnumTypeByName("googlesql_test.TestEnum").getDescriptor();
    assertThat(testEnumDescriptor).isEqualTo(TestEnum.getDescriptor());

    ImmutableSet<String> expectedDescriptorNames =
        ImmutableSet.of(
            "googlesql/public/proto/wire_format_annotation.proto",
            "google/protobuf/descriptor.proto",
            "googlesql/public/proto/type_annotation.proto",
            "googlesql/testdata/test_schema.proto");
    // Because the generated pool is a singleton, and other tests might add stuff to it, the
    // best we can do is make sure it has at least what we are expecting.
    assertThat(getFilenames(pool)).containsAtLeastElementsIn(expectedDescriptorNames);

    GoogleSQLDescriptorPool reserializedPool = SerializableTester.reserialize(pool);
    assertThat(getFilenames(reserializedPool)).containsAtLeastElementsIn(expectedDescriptorNames);

    EnumDescriptor reserializedTestEnumDescriptor =
        reserializedPool.findEnumTypeByName("googlesql_test.TestEnum").getDescriptor();

    // We expect that the descriptor _will_ be equal to the input, because it is as singleton, and
    // we dedup preserving the existing copy.
    assertThat(reserializedTestEnumDescriptor).isEqualTo(testEnumDescriptor);
    assertFileDescriptorsAreDependencyOrdered(GoogleSQLDescriptorPool.getGeneratedPool());
  }

  private static FileDescriptorSet fileDescriptorSetFromTextProto(String textProto) {
    FileDescriptorSet.Builder builder = FileDescriptorSet.newBuilder();
    try {
      TextFormat.merge(textProto, builder);
    } catch (ParseException e) {
      fail(e.toString());
    }
    return builder.build();
  }
}
