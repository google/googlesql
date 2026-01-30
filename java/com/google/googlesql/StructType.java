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

import static com.google.common.collect.ImmutableList.toImmutableList;

import com.google.common.base.Joiner;
import com.google.common.collect.ImmutableList;
import com.google.common.hash.HashCode;
import com.google.common.hash.Hashing;
import com.google.errorprone.annotations.Immutable;
import com.google.googlesql.GoogleSQLOptions.ProductMode;
import com.google.googlesql.GoogleSQLType.StructFieldProto;
import com.google.googlesql.GoogleSQLType.StructTypeProto;
import com.google.googlesql.GoogleSQLType.TypeKind;
import com.google.googlesql.GoogleSQLType.TypeProto;
import java.io.Serializable;
import java.util.ArrayList;
import java.util.Collection;
import java.util.List;
import java.util.Objects;

/**
 * A struct type.
 * Structs are allowed to have zero fields, but this is not normal usage.
 * Field names do not have to be unique.
 * Empty field names are used to indicate anonymous fields - such fields are
 * unnamed and cannot be looked up by name.
 */
public class StructType extends Type {
  static boolean equalsImpl(StructType type1, StructType type2, boolean equivalent) {
    if (type1.fields.size() == type2.fields.size()) {
      for (int i = 0; i < type1.fields.size(); ++i) {
        if (!type1.fields.get(i).equalsImpl(type2.fields.get(i), equivalent)) {
          return false;
        }
      }
      return true;
    }
    return false;
  }

  private static final int EMPTY_STRUCT_HASH_CODE = 0x20130104;
  private final ImmutableList<StructField> fields;

  /** Private constructor, instances must be created with {@link TypeFactory} */
  StructType(Collection<StructField> fields) {
    super(TypeKind.TYPE_STRUCT);
    this.fields = ImmutableList.copyOf(fields);
  }

  public int getFieldCount() {
    return fields.size();
  }

  public StructField getField(int i) {
    return fields.get(i);
  }

  public ImmutableList<StructField> getFieldList() {
    return fields;
  }

  @Override
  public ImmutableList<Type> componentTypes() {
    return fields.stream().map(StructField::getType).collect(toImmutableList());
  }

  @Override
  public void serialize(
      TypeProto.Builder typeProtoBuilder, FileDescriptorSetsBuilder fileDescriptorSetsBuilder) {
    typeProtoBuilder.setTypeKind(getKind());
    StructTypeProto.Builder struct = typeProtoBuilder.getStructTypeBuilder();
    for (StructField field : fields) {
      StructFieldProto.Builder fieldBuilder = struct.addFieldBuilder();
      fieldBuilder.setFieldName(field.getName());
      field.getType().serialize(
          fieldBuilder.getFieldTypeBuilder(), fileDescriptorSetsBuilder);
    }
  }

  @Override
  public int hashCode() {
    if (fields.isEmpty()) {
      return EMPTY_STRUCT_HASH_CODE;
    }

    List<HashCode> hashCodes = new ArrayList<>();
    for (StructField field : fields) {
      hashCodes.add(HashCode.fromInt(field.hashCode()));
    }
    return Hashing.combineOrdered(hashCodes).asInt();
  }

  /** Field contained in a StructType, representing a name and a type. */
  @Immutable
  public static class StructField implements Serializable {
    private final String name;
    private final Type type;

    public StructField(String name, Type type) {
      this.name = name;
      this.type = type;
    }

    public String getName() {
      return name;
    }

    public Type getType() {
      return type;
    }

    public boolean equivalent(StructField other) {
      return equalsImpl(other, /* equivalent= */ true);
    }

    /**
     * Note: {@code equalsImpl} is used in {@code equals}, where this reference equality pattern is
     * allowed.
     */
    @SuppressWarnings("ReferenceEquality")
    boolean equalsImpl(StructField other, boolean equivalent) {
      if (this == other) {
        return true;
      }

      if (other == null) {
        return false;
      }

      return name.equalsIgnoreCase(other.name) && type.equalsInternal(other.type, equivalent);
    }

    @Override
    public boolean equals(Object other) {
      return (other instanceof StructField)
          && equalsImpl((StructField) other, /* equivalent= */ false);
    }

    @Override
    public int hashCode() {
      return Objects.hash(type, name);
    }
  }

  @Override
  public String typeName(ProductMode productMode) {
    List<String> strings = new ArrayList<String>();
    for (StructField field : fields) {
      if (!field.getName().isEmpty()) {
        strings.add(
            String.format(
                "%s %s",
                GoogleSQLStrings.toIdentifierLiteral(field.getName()),
                field.getType().typeName(productMode)));
      } else {
        strings.add(field.getType().typeName(productMode));
      }
    }
    return String.format("STRUCT<%s>", Joiner.on(", ").join(strings));
  }

  @Override
  public String debugString(boolean details) {
    List<String> strings = new ArrayList<String>();
    for (StructField field : fields) {
      if (!field.getName().isEmpty()) {
        strings.add(String.format("%s %s", GoogleSQLStrings.toIdentifierLiteral(field.getName()),
            field.getType().debugString(details)));
      } else {
      strings.add(field.getType().debugString(details));
      }
    }
    return String.format("STRUCT<%s>", Joiner.on(", ").join(strings));
  }

  @Override
  public StructType asStruct() {
    return this;
  }
}
