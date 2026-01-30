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
import com.google.protobuf.Descriptors.Descriptor;
import com.google.protobuf.Descriptors.EnumDescriptor;
import com.google.protobuf.Descriptors.FieldDescriptor;
import com.google.protobuf.Descriptors.FileDescriptor;
import com.google.protobuf.Descriptors.OneofDescriptor;
import java.io.Serializable;
import javax.annotation.Nullable;

/**
 * Class mimicking the C++ version of DescriptorPool. It defines a mapping from fullname (package +
 * name) to various "GoogleSQL*Descriptor". Each of these is a wrapper around a
 * com.google.protobuf.Descriptor object with a reference back to this object.
 *
 * <p>
 *
 * @see "(broken link)"
 *     <p>This class is not thread safe.
 */
public interface DescriptorPool extends Serializable {

  /**
   * Returns the {@code GoogleSQLEnumDescriptor} of the enum with given {@code name} or null if not
   * found.
   */
  @Nullable
  GoogleSQLEnumDescriptor findEnumTypeByName(String name);

  /**
   * Returns the {@code GoogleSQLDescriptor} of the message with given {@code name} or null if not
   * found.
   */
  @Nullable
  GoogleSQLDescriptor findMessageTypeByName(String name);

  /**
   * Returns the {@code GoogleSQLFileDescriptor} of the file with given {@code name} or null if not
   * found.
   */
  @Nullable
  GoogleSQLFileDescriptor findFileByName(String name);

  /**
   * Returns the {@code GoogleSQLFieldDescriptor} on the given {@code descriptor} or null if not
   * found. Note, this method should resolve any extensions, and should not call or make use of the
   * convenience method {@link GoogleSQLFieldDescriptor.findFieldByNumber} to resolve this.
   *
   * <p>That method may make use of this one, however.
   */
  @Nullable
  GoogleSQLFieldDescriptor findFieldByNumber(GoogleSQLDescriptor descriptor, int number);

  /**
   * Wrapped {@code FileDescriptor} with the {@code ImmutableGoogleSQLDescriptorPool} from which it
   * was created.
   */
  public interface GoogleSQLFileDescriptor {
    DescriptorPool getDescriptorPool();

    FileDescriptor getDescriptor();
  }

  /**
   * Wrapped {@code EnumDescriptor} with the {@code ImmutableGoogleSQLDescriptorPool} from which it
   * was created.
   */
  public interface GoogleSQLEnumDescriptor {
    DescriptorPool getDescriptorPool();

    EnumDescriptor getDescriptor();
  }

  /**
   * Wrapped {@code Descriptor} with the {@code ImmutableGoogleSQLDescriptorPool} from which it was
   * created.
   */
  public interface GoogleSQLDescriptor {
    DescriptorPool getDescriptorPool();

    Descriptor getDescriptor();

    default GoogleSQLFieldDescriptor findFieldByNumber(int number) {
      return getDescriptorPool().findFieldByNumber(this, number);
    }

    GoogleSQLOneofDescriptor findOneofByIndex(int index);
  }

  /**
   * Wrapped {@code FieldDescriptor} with the {@code GoogleSQLDescriptorPool} from which it was
   * created.
   */
  public interface GoogleSQLFieldDescriptor {
    DescriptorPool getDescriptorPool();

    FieldDescriptor getDescriptor();
  }

  /** Wrapped {@link OneofDescriptor} with the {@link DescriptorPool} from which it was created. */
  public interface GoogleSQLOneofDescriptor {
    DescriptorPool getDescriptorPool();

    OneofDescriptor getDescriptor();
  }

  /**
   * Return all the file descriptors in this descriptor pool in proto file dependency order.
   *
   * <p>Additionally, this should return all transitive dependencies explicitly.
   *
   * <p>This generally means descriptor.proto will be first, and application specific protos will be
   * last.
   */
  ImmutableList<FileDescriptor> getAllFileDescriptorsInDependencyOrder();
}
