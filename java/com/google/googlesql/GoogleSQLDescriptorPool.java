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

import static com.google.common.base.Preconditions.checkArgument;

import com.google.common.collect.ImmutableList;
import com.google.common.collect.ImmutableSet;
import com.google.protobuf.DescriptorProtos.FileDescriptorProto;
import com.google.protobuf.DescriptorProtos.FileDescriptorSet;
import com.google.protobuf.Descriptors.Descriptor;
import com.google.protobuf.Descriptors.EnumDescriptor;
import com.google.protobuf.Descriptors.FieldDescriptor;
import com.google.protobuf.Descriptors.FileDescriptor;
import com.google.protobuf.Descriptors.GenericDescriptor;
import com.google.protobuf.ExtensionRegistry;
import com.google.protobuf.ExtensionRegistry.ExtensionInfo;
import com.google.googlesql.ImmutableDescriptorPool.ImmutableGoogleSQLDescriptor;
import com.google.googlesql.ImmutableDescriptorPool.ImmutableGoogleSQLEnumDescriptor;
import com.google.googlesql.ImmutableDescriptorPool.ImmutableGoogleSQLFieldDescriptor;
import com.google.googlesql.ImmutableDescriptorPool.ImmutableGoogleSQLFileDescriptor;
import java.io.Serializable;
import java.util.Collection;
import java.util.LinkedHashMap;
import java.util.Map;
import java.util.Set;
import javax.annotation.Nullable;

/**
 * A basic, mutable implementation of DescriptorPool.
 *
 * <p>This class is not thread safe.
 */
public class GoogleSQLDescriptorPool implements DescriptorPool, Serializable {
  // Global pool for generated descriptors i.e. those compiled into the Java
  // program, rather than loaded from protobuf. Generated descriptors will be
  // imported into this pool when they are used to create types.
  private static final GeneratedDescriptorPool generatedDescriptorPool =
      new GeneratedDescriptorPool();
  /**
   * Imports the given generated {@code descriptor} to the global {@code generatedDescriptorPool};
   */
  static void importIntoGeneratedPool(GenericDescriptor descriptor) {
    generatedDescriptorPool.importFileDescriptor(descriptor);
  }

  static GoogleSQLDescriptorPool getGeneratedPool() {
    return generatedDescriptorPool;
  }

  private final Map<String, FileDescriptor> fileDescriptorsByName = new LinkedHashMap<>();
  private final Map<String, EnumDescriptor> enumsByName = new LinkedHashMap<>();
  private final Map<String, Descriptor> messagesByName = new LinkedHashMap<>();
  private final ExtensionRegistry extensionRegistry = ExtensionRegistry.newInstance();

  /**
   * Imports the {@code fileDescriptorSet} into the descriptor pool.
   *
   * @throws IllegalArgumentException if {@code fileDescriptorSet} is not self-contained, i.e.
   *     contains a FileDescriptor whose dependencies are not within the set.
   */
  public void importFileDescriptorSet(FileDescriptorSet fileDescriptorSet) {
    importFileDescriptors(fileDescriptorSet.getFileList());
  }

  /**
   * Imports the {@code fileDescriptorSet} into the descriptor pool, overwriting existing {@link
   * FileDescriptor}s.
   *
   * <p>Generally prefer to use {@code importFileDescriptorSet}, unless importing multiple {@code
   * FileDescriptorSet}s which are known to be incomplete. This may be the case, for example, if the
   * {@code FileDescriptorSet} comes from a source which prunes unused dependencies, and thus
   * caching of these incomplete dependency trees may cause problems in subsequent imports.
   *
   * @throws IllegalArgumentException if {@code fileDescriptorSet} is not self-contained, i.e.
   *     contains a FileDescriptor whose dependencies are not within the set.
   */
  public void importFileDescriptorSetWithOverwrite(FileDescriptorSet fileDescriptorSet) {
    importFileDescriptorsWithOverwrite(fileDescriptorSet.getFileList());
  }

  /**
   * Imports a collection of {@code fileDescriptors} into the descriptor pool.
   *
   * <p>This will ignore inputs that are already represented in this pool (by file name).
   *
   * @param fileDescriptorProtos The collection of {@link FileDescriptor}s to import.
   * @throws IllegalArgumentException if @{fileDescriptors} are not self-contained, i.e. have
   *     dependencies that are not within the collection.
   */
  public void importFileDescriptors(Collection<FileDescriptorProto> fileDescriptorProtos) {
    Map<String, FileDescriptorProto> fileDescriptorProtosByName = new LinkedHashMap<>();
    for (FileDescriptorProto proto : fileDescriptorProtos) {
      if (!fileDescriptorProtosByName.containsKey(proto.getName())) {
        fileDescriptorProtosByName.put(proto.getName(), proto);
      }
    }

    // Save a copy of the existing fileDescriptors, so we know which ones to skip below.
    Set<FileDescriptor> existingDescriptors = ImmutableSet.copyOf(fileDescriptorsByName.values());
    for (String filename : fileDescriptorProtosByName.keySet()) {
      ImmutableDescriptorPool.resolveFileDescriptor(
          filename, fileDescriptorProtosByName, fileDescriptorsByName);
    }
    for (FileDescriptor file : fileDescriptorsByName.values()) {
      if (!existingDescriptors.contains(file)) {
        // Add only the new ones.
        ImmutableDescriptorPool.updateDescriptorMapsFromFileDescriptor(
            file, enumsByName, messagesByName, extensionRegistry);
      }
    }
  }

  private void importFileDescriptorsWithOverwrite(
      Collection<FileDescriptorProto> fileDescriptorProtos) {
    LinkedHashMap<String, FileDescriptorProto> fileDescriptorProtosByName = new LinkedHashMap<>();
    for (FileDescriptorProto proto : fileDescriptorProtos) {
      fileDescriptorProtosByName.putIfAbsent(proto.getName(), proto);
    }

    LinkedHashMap<String, FileDescriptor> overwritingFileDescriptorsByName = new LinkedHashMap<>();

    for (String filename : fileDescriptorProtosByName.keySet()) {
      ImmutableDescriptorPool.resolveFileDescriptor(
          filename, fileDescriptorProtosByName, overwritingFileDescriptorsByName);
    }

    for (Map.Entry<String, FileDescriptor> entry : overwritingFileDescriptorsByName.entrySet()) {
      ImmutableDescriptorPool.updateDescriptorMapsFromFileDescriptor(
          entry.getValue(), enumsByName, messagesByName, extensionRegistry);
      fileDescriptorsByName.put(entry.getKey(), entry.getValue());
    }
  }

  /**
   * Adds a newly parsed {@code file} and its message/enum descriptors into the named maps for
   * future lookup.
   */
  protected void addFileDescriptor(FileDescriptor file) {
    FileDescriptor existing = fileDescriptorsByName.get(file.getFullName());
    checkArgument(
        existing == null || existing == file,
        "duplicate FileDescriptor with name %s",
        file.getFullName());
    if (existing == null) {
      addFileDescriptors(file.getDependencies());
      fileDescriptorsByName.put(file.getFullName(), file);
      ImmutableDescriptorPool.updateDescriptorMapsFromFileDescriptor(
          file, enumsByName, messagesByName, extensionRegistry);
    }
  }

  /** Adds {@code files} and its message/enum descriptors into the named maps for future lookup. */
  protected void addFileDescriptors(Collection<FileDescriptor> files) {
    files.forEach(this::addFileDescriptor);
  }

  /**
   * Returns the {@code GoogleSQLEnumDescriptor} of the enum with given {@code name} or null if not
   * found.
   */
  @Override
  @Nullable
  public GoogleSQLEnumDescriptor findEnumTypeByName(String name) {
    if (enumsByName.containsKey(name)) {
      return ImmutableGoogleSQLEnumDescriptor.create(this, enumsByName.get(name));
    }
    return null;
  }

  /**
   * Returns the {@code GoogleSQLDescriptor} of the message with given {@code name} or null if not
   * found.
   */
  @Override
  @Nullable
  public GoogleSQLDescriptor findMessageTypeByName(String name) {
    if (messagesByName.containsKey(name)) {
      return ImmutableGoogleSQLDescriptor.create(this, messagesByName.get(name));
    }
    return null;
  }

  /**
   * Returns the {@code GoogleSQLFileDescriptor} of the file with given {@code name} or null if not
   * found.
   */
  @Override
  @Nullable
  public GoogleSQLFileDescriptor findFileByName(String name) {
    if (fileDescriptorsByName.containsKey(name)) {
      return ImmutableGoogleSQLFileDescriptor.create(this, fileDescriptorsByName.get(name));
    }
    return null;
  }

  @Override
  @Nullable
  public GoogleSQLFieldDescriptor findFieldByNumber(
      DescriptorPool.GoogleSQLDescriptor descriptor, int number) {
    FieldDescriptor field = descriptor.getDescriptor().findFieldByNumber(number);
    if (field == null) {
      ExtensionInfo info =
          extensionRegistry.findImmutableExtensionByNumber(descriptor.getDescriptor(), number);
      if (info != null) {
        field = info.descriptor;
      }
    }
    return ImmutableGoogleSQLFieldDescriptor.create(this, field);
  }

  @Override
  public ImmutableList<FileDescriptor> getAllFileDescriptorsInDependencyOrder() {
    return ImmutableList.copyOf(fileDescriptorsByName.values());
  }

  private Object writeReplace() {
    return new SerializationProxy(this);
  }

  private static final class SerializationProxy implements Serializable {
    private final ImmutableDescriptorPool pool;

    SerializationProxy(GoogleSQLDescriptorPool concrete) {
      pool =
          ImmutableDescriptorPool.builder()
              .importFileDescriptors(concrete.getAllFileDescriptorsInDependencyOrder())
              .build();
    }

    private Object readResolve() {
      GoogleSQLDescriptorPool newPool = new GoogleSQLDescriptorPool();
      pool.getAllFileDescriptorsInDependencyOrder().forEach(newPool::addFileDescriptor);
      return newPool;
    }
  }

  /**
   * Pool for generated descriptors, i.e. those generated from .proto files and compiled into the
   * Java program. This is supposed to be shared by globally and is thread safe. Descriptors are
   * imported lazily because there's no efficient way to do it in a static intializer. Imported
   * descriptors are kept forever, which is fine since the number of generated descriptors is fixed
   * when the program is compiled.
   */
  static class GeneratedDescriptorPool extends GoogleSQLDescriptorPool {
    /**
     * Imports the given generated {@code descriptor} and its dependencies.
     */
    synchronized void importFileDescriptor(GenericDescriptor descriptor) {
      addFileDescriptor(descriptor.getFile());
    }

    @Override
    public void importFileDescriptors(Collection<FileDescriptorProto> fileDescriptors) {
      throw new IllegalStateException(
          "Importing FileDescriptor from proto to the generated pool not supported.");
    }

    @Override
    public synchronized GoogleSQLEnumDescriptor findEnumTypeByName(String name) {
      return super.findEnumTypeByName(name);
    }

    @Override
    public synchronized GoogleSQLDescriptor findMessageTypeByName(String name) {
      return super.findMessageTypeByName(name);
    }

    @Override
    public synchronized GoogleSQLFileDescriptor findFileByName(String name) {
      return super.findFileByName(name);
    }

    @Override
    public synchronized ImmutableList<FileDescriptor> getAllFileDescriptorsInDependencyOrder() {
      return super.getAllFileDescriptorsInDependencyOrder();
    }

    private Object writeReplace() {
      return new SerializationProxy(this);
    }

    /**
     * Serialization doesn't make sense for the generated pool. But we assume it is more important
     * to have the correct result then to _actually_ be generated - so we basically treat it the
     * same as a regular pool, except it's a singleton, so when we deserialize, we import everything
     * into the generated pool on 'this' side.
     */
    private static final class SerializationProxy implements Serializable {
      private final ImmutableDescriptorPool pool;

      SerializationProxy(GoogleSQLDescriptorPool concrete) {
        pool =
            ImmutableDescriptorPool.builder()
                .importFileDescriptors(concrete.getAllFileDescriptorsInDependencyOrder())
                .build();
      }

      private Object readResolve() {
        GoogleSQLDescriptorPool generatedPool = GoogleSQLDescriptorPool.getGeneratedPool();
        pool.getAllFileDescriptorsInDependencyOrder().stream()
            // Only import new descriptors.
            .filter(f -> generatedPool.findFileByName(f.getFullName()) == null)
            .forEach(generatedPool::addFileDescriptor);
        return generatedPool;
      }
    }
  }
}
