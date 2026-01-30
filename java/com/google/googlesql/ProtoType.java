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

import static com.google.common.base.Preconditions.checkNotNull;

import com.google.errorprone.annotations.Immutable;
import com.google.protobuf.Descriptors.Descriptor;
import com.google.protobuf.Descriptors.FieldDescriptor;
import com.google.googlesql.GoogleSQLOptions.ProductMode;
import com.google.googlesql.GoogleSQLType.TypeKind;
import com.google.googlesql.GoogleSQLType.TypeProto;
import com.google.googlesql.TypeAnnotationProto.DeprecatedEncoding;
import com.google.googlesql.TypeAnnotationProto.FieldFormat;
import java.io.IOException;
import java.util.Objects;

/** A type defined by a protobuf Descriptor. */
@Immutable
public class ProtoType extends Type {
  static boolean equalsImpl(ProtoType type1, ProtoType type2, boolean equivalent) {
    if (type1.descriptor == type2.descriptor) {
      return true;
    }
    return equivalent && type1.descriptor.getFullName().equals(type2.descriptor.getFullName());
  }

  @SuppressWarnings("Immutable") // only non-final for deserialization
  private transient Descriptor descriptor;

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

  public Descriptor getDescriptor() {
    return descriptor;
  }

  /** Private constructor, instances must be created with {@link TypeFactory} */
  ProtoType(Descriptor descriptor, DescriptorPool pool) {
    super(TypeKind.TYPE_PROTO);
    this.descriptor = checkNotNull(descriptor);
    this.descriptorPool = checkNotNull(pool);
  }

  @Override
  public void serialize(
      TypeProto.Builder typeProtoBuilder,
      FileDescriptorSetsBuilder fileDescriptorSetsBuilder) {
    int index =
        fileDescriptorSetsBuilder.getOrAddFileDescriptorIndex(descriptor.getFile(), descriptorPool);
    typeProtoBuilder.setTypeKind(getKind());
    typeProtoBuilder.getProtoTypeBuilder()
        .setProtoFileName(descriptor.getFile().getFullName())
        .setProtoName(descriptor.getFullName())
        .setFileDescriptorSetIndex(index);
  }

  @Override
  public int hashCode() {
    return Objects.hash(descriptor, getKind());
  }

  public static boolean hasFormatAnnotation(FieldDescriptor field) {
    return getFormatAnnotationImpl(field) != TypeAnnotationProto.FieldFormat.Format.DEFAULT_FORMAT;
  }

  private static FieldFormat.Format getFormatAnnotationImpl(FieldDescriptor field) {
    // Read the format encoding, or if it doesn't exist, the type encoding.
    if (field.getOptions().hasExtension(TypeAnnotationProto.format)) {
      return field.getOptions().getExtension(TypeAnnotationProto.format);
    } else if (field.getOptions().hasExtension(TypeAnnotationProto.type)) {
      return field.getOptions().getExtension(TypeAnnotationProto.type);
    } else {
      return TypeAnnotationProto.FieldFormat.Format.DEFAULT_FORMAT;
    }
  }

  public static FieldFormat.Format getFormatAnnotation(FieldDescriptor field) {
    // Read the format (or deprecated type) encoding.
    FieldFormat.Format format = getFormatAnnotationImpl(field);

    DeprecatedEncoding.Encoding encodingValue =
        field.getOptions().getExtension(TypeAnnotationProto.encoding);
    // If we also have a (valid) deprecated encoding annotation, merge that over
    // top of the type encoding.  Ignore any invalid encoding annotation.
    // This exists for backward compatability with existing .proto files only.
    if (encodingValue == TypeAnnotationProto.DeprecatedEncoding.Encoding.DATE_DECIMAL
        && format == TypeAnnotationProto.FieldFormat.Format.DATE) {
      return TypeAnnotationProto.FieldFormat.Format.DATE_DECIMAL;
    }
    return format;
  }

  @Override
  public String typeName(ProductMode productMode) {
    return GoogleSQLStrings.toIdentifierLiteral(descriptor.getFullName());
  }

  @Override
  public String debugString(boolean details) {
    if (details) {
      return String.format("PROTO<%s, file name: %s, <%s>>", descriptor.getFullName(),
          descriptor.getFile().getName(), descriptor.getName());
    }
    return String.format("PROTO<%s>", descriptor.getFullName());
  }

  @Override
  public ProtoType asProto() {
    return this;
  }

  private void writeObject(java.io.ObjectOutputStream out) throws IOException {
    out.defaultWriteObject();
    out.writeUTF(descriptor.getFullName());
  }

  private void readObject(java.io.ObjectInputStream in) throws IOException, ClassNotFoundException {
    in.defaultReadObject();
    this.descriptor = descriptorPool.findMessageTypeByName(in.readUTF()).getDescriptor();
  }
}
