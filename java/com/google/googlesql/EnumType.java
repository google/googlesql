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

import com.google.common.base.Preconditions;
import com.google.errorprone.annotations.Immutable;
import com.google.protobuf.Descriptors.EnumDescriptor;
import com.google.protobuf.Descriptors.EnumValueDescriptor;
import com.google.googlesql.GoogleSQLOptions.ProductMode;
import com.google.googlesql.GoogleSQLType.OpaqueEnumTypeOptions;
import com.google.googlesql.GoogleSQLType.TypeKind;
import com.google.googlesql.GoogleSQLType.TypeProto;
import java.io.IOException;
import java.util.Objects;

/**
 * Each EnumType object defines a set of legal numeric (int32) values. Each value has one (or more)
 * string names. The first name is treated as the canonical name for that numeric value, and
 * additional names are treated as aliases.
 *
 * <p>We currently only support creating a GoogleSQL EnumType from protobuf EnumDescriptor.
 */
@Immutable
public class EnumType extends Type {
  static boolean equalsImpl(EnumType type1, EnumType type2, boolean equivalent) {
    if (type1.isOpaque != type2.isOpaque) {
      return false;
    }
    if (type1.enumDescriptor == type2.enumDescriptor) {
      return true;
    }
    return equivalent
        && type1.enumDescriptor.getFullName().equals(type2.enumDescriptor.getFullName());
  }

  @SuppressWarnings("Immutable") // only non-final for deserialization
  private transient EnumDescriptor enumDescriptor;
  
  /**
   * Must keep a reference to the descriptorPool so that the same descriptor in the same pool will
   * only be serialized once.
   *
   * <p>Note: This mutable type is only read during (de)serialization:
   *
   * <ul>
   *   <li>Deserialization: This field is created by the deserialization execution, so there is no
   *       risk of external mutation.
   *   <li>In {@link #serialize}: A reference to the pool is passed to {@link
   *       FileDescriptorSetsBuilder#getOrAddFileDescriptorIndex} and ultimately exposed in {@link
   *       FileDescriptorSetsBuilder#getDescriptorPools}, but that method is package-private so only
   *       GoogleSQL code would see any mutations.
   * </ul>
   *
   * This is not technically immutable, but only GoogleSQL code can observe any changes. Suppressing
   * for external users to treat this as immutable.
   */
  @SuppressWarnings("Immutable")
  private final DescriptorPool descriptorPool;

  private final boolean isOpaque;

  /** Package private constructor, instances must be created with {@link TypeFactory} */
  EnumType(EnumDescriptor enumDescriptor, DescriptorPool pool, boolean isOpaque) {
    super(TypeKind.TYPE_ENUM);
    Preconditions.checkNotNull(enumDescriptor);
    Preconditions.checkNotNull(pool);
    this.enumDescriptor = enumDescriptor;
    this.descriptorPool = pool;
    this.isOpaque = isOpaque;
  }

  public EnumDescriptor getDescriptor() {
    return enumDescriptor;
  }

  public boolean isOpaque() {
    return this.isOpaque;
  }

  @Override
  public void serialize(
      TypeProto.Builder typeProtoBuilder,
      FileDescriptorSetsBuilder fileDescriptorSetsBuilder) {
    int index =
        fileDescriptorSetsBuilder.getOrAddFileDescriptorIndex(
            enumDescriptor.getFile(), descriptorPool);
    typeProtoBuilder.setTypeKind(getKind());
    typeProtoBuilder
        .getEnumTypeBuilder()
        .setEnumFileName(enumDescriptor.getFile().getFullName())
        .setEnumName(enumDescriptor.getFullName())
        .setFileDescriptorSetIndex(index)
        .setIsOpaque(isOpaque);
  }

  @Override
  public String toString() {
    return debugString(/* details= */ false);
  }

  /**
   * Returns the enum value string corresponding to the given {@code number}.
   */
  public String findName(int number) {
    EnumValueDescriptor value = enumDescriptor.findValueByNumber(number);
    if (value == null) {
      return null;
    }
    return value.getName();
  }

  @Override
  public int hashCode() {
    return Objects.hash(enumDescriptor, getKind(), isOpaque);
  }

  private String rawTypeName() {
    if (this.isOpaque) {
      OpaqueEnumTypeOptions opaqueEnumOptions =
          enumDescriptor.getOptions().getExtension(GoogleSQLType.opaqueEnumTypeOptions);
      if (!opaqueEnumOptions.getSqlOpaqueEnumName().isEmpty()) {
        return opaqueEnumOptions.getSqlOpaqueEnumName();
      }
    }
    return enumDescriptor.getFullName();
  }

  @Override
  public String typeName(ProductMode productMode) {
    return GoogleSQLStrings.toIdentifierLiteral(rawTypeName());
  }

  @Override
  public String debugString(boolean details) {
    if (details) {
      return String.format(
          "ENUM<%s, file name: %s, <%s>%s>",
          rawTypeName(),
          enumDescriptor.getFile().getName(),
          enumDescriptor.getName(),
          this.isOpaque ? "is_opaque: true" : "");
    }
    return String.format("ENUM<%s>", rawTypeName());
  }

  @Override
  public EnumType asEnum() {
    return this;
  }

  private void writeObject(java.io.ObjectOutputStream out) throws IOException {
    out.defaultWriteObject();
    out.writeUTF(enumDescriptor.getFullName());
  }

  private void readObject(java.io.ObjectInputStream in) throws IOException, ClassNotFoundException {
    in.defaultReadObject();
    this.enumDescriptor = descriptorPool.findEnumTypeByName(in.readUTF()).getDescriptor();
  }
}
