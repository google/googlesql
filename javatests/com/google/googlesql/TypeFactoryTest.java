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
import static org.junit.Assert.assertThrows;

import com.google.common.collect.Lists;
import com.google.common.testing.SerializableTester;
import com.google.protobuf.Descriptors.EnumDescriptor;
import com.google.protobuf.Descriptors.EnumValueDescriptor;
import com.google.protobuf.GeneratedMessage;
import com.google.protobuf.Message;
import com.google.protobuf.ProtocolMessageEnum;
import com.google.googlesql.GoogleSQLOptions.ProductMode;
import com.google.googlesql.GoogleSQLType.EnumTypeProto;
import com.google.googlesql.GoogleSQLType.ProtoTypeProto.Builder;
import com.google.googlesql.GoogleSQLType.TypeKind;
import com.google.googlesql.GoogleSQLType.TypeProto;

import java.util.ArrayList;
import org.junit.Test;
import org.junit.runner.RunWith;
import org.junit.runners.JUnit4;

@RunWith(JUnit4.class)

public class TypeFactoryTest {

  @Test
  public void testIsSimpleType() {
    assertThat(TypeFactory.isSimpleType(TypeKind.TYPE_ARRAY)).isFalse();
    assertThat(TypeFactory.isSimpleType(TypeKind.TYPE_BOOL)).isTrue();
    assertThat(TypeFactory.isSimpleType(TypeKind.TYPE_BYTES)).isTrue();
    assertThat(TypeFactory.isSimpleType(TypeKind.TYPE_DATE)).isTrue();
    assertThat(TypeFactory.isSimpleType(TypeKind.TYPE_DOUBLE)).isTrue();
    assertThat(TypeFactory.isSimpleType(TypeKind.TYPE_ENUM)).isFalse();
    assertThat(TypeFactory.isSimpleType(TypeKind.TYPE_FLOAT)).isTrue();
    assertThat(TypeFactory.isSimpleType(TypeKind.TYPE_INT32)).isTrue();
    assertThat(TypeFactory.isSimpleType(TypeKind.TYPE_INT64)).isTrue();
    assertThat(TypeFactory.isSimpleType(TypeKind.TYPE_PROTO)).isFalse();
    assertThat(TypeFactory.isSimpleType(TypeKind.TYPE_STRING)).isTrue();
    assertThat(TypeFactory.isSimpleType(TypeKind.TYPE_STRUCT)).isFalse();
    assertThat(TypeFactory.isSimpleType(TypeKind.TYPE_TIMESTAMP)).isTrue();
    assertThat(TypeFactory.isSimpleType(TypeKind.TYPE_DATETIME)).isTrue();
    assertThat(TypeFactory.isSimpleType(TypeKind.TYPE_TIME)).isTrue();
    assertThat(TypeFactory.isSimpleType(TypeKind.TYPE_INTERVAL)).isTrue();
    assertThat(TypeFactory.isSimpleType(TypeKind.TYPE_UINT32)).isTrue();
    assertThat(TypeFactory.isSimpleType(TypeKind.TYPE_UINT64)).isTrue();
    assertThat(TypeFactory.isSimpleType(TypeKind.TYPE_UNKNOWN)).isFalse();
    assertThat(TypeFactory.isSimpleType(TypeKind.TYPE_RANGE)).isFalse();
    assertThat(TypeFactory.isSimpleType(TypeKind.TYPE_UUID)).isTrue();
  }

  @Test
  public void testIsSimpleTypeName() {
    assertThat(TypeFactory.isSimpleTypeName("Array", ProductMode.PRODUCT_EXTERNAL)).isFalse();
    assertThat(TypeFactory.isSimpleTypeName("BOOL", ProductMode.PRODUCT_EXTERNAL)).isTrue();
    assertThat(TypeFactory.isSimpleTypeName("Boolean", ProductMode.PRODUCT_EXTERNAL)).isTrue();
    assertThat(TypeFactory.isSimpleTypeName("BYTES", ProductMode.PRODUCT_EXTERNAL)).isTrue();
    assertThat(TypeFactory.isSimpleTypeName("DATE", ProductMode.PRODUCT_EXTERNAL)).isTrue();
    assertThat(TypeFactory.isSimpleTypeName("double", ProductMode.PRODUCT_EXTERNAL)).isFalse();
    assertThat(TypeFactory.isSimpleTypeName("float", ProductMode.PRODUCT_EXTERNAL)).isFalse();
    assertThat(TypeFactory.isSimpleTypeName("float32", ProductMode.PRODUCT_EXTERNAL)).isFalse();
    assertThat(TypeFactory.isSimpleTypeName("float64", ProductMode.PRODUCT_EXTERNAL)).isTrue();
    assertThat(TypeFactory.isSimpleTypeName("ENUM", ProductMode.PRODUCT_EXTERNAL)).isFalse();
    assertThat(TypeFactory.isSimpleTypeName("FLOAT", ProductMode.PRODUCT_EXTERNAL)).isFalse();
    assertThat(TypeFactory.isSimpleTypeName("INT32", ProductMode.PRODUCT_EXTERNAL)).isFalse();
    assertThat(TypeFactory.isSimpleTypeName("INT64", ProductMode.PRODUCT_EXTERNAL)).isTrue();
    assertThat(TypeFactory.isSimpleTypeName("PROTO", ProductMode.PRODUCT_EXTERNAL)).isFalse();
    assertThat(TypeFactory.isSimpleTypeName("STRING", ProductMode.PRODUCT_EXTERNAL)).isTrue();
    assertThat(TypeFactory.isSimpleTypeName("STRUCT", ProductMode.PRODUCT_EXTERNAL)).isFalse();
    assertThat(TypeFactory.isSimpleTypeName("TIMESTAMP", ProductMode.PRODUCT_EXTERNAL)).isTrue();
    assertThat(TypeFactory.isSimpleTypeName("DATETIME", ProductMode.PRODUCT_EXTERNAL)).isTrue();
    assertThat(TypeFactory.isSimpleTypeName("TIME", ProductMode.PRODUCT_EXTERNAL)).isTrue();
    assertThat(TypeFactory.isSimpleTypeName("INTERVAL", ProductMode.PRODUCT_EXTERNAL)).isTrue();
    assertThat(TypeFactory.isSimpleTypeName("UUID", ProductMode.PRODUCT_EXTERNAL)).isTrue();
    assertThat(TypeFactory.isSimpleTypeName("UINT32", ProductMode.PRODUCT_EXTERNAL)).isFalse();
    assertThat(TypeFactory.isSimpleTypeName("UINT64", ProductMode.PRODUCT_EXTERNAL)).isFalse();
    assertThat(TypeFactory.isSimpleTypeName("UNKNOWN", ProductMode.PRODUCT_EXTERNAL)).isFalse();
    assertThat(TypeFactory.isSimpleTypeName("RANGE", ProductMode.PRODUCT_EXTERNAL)).isFalse();

    assertThat(TypeFactory.isSimpleTypeName("Array", ProductMode.PRODUCT_INTERNAL)).isFalse();
    assertThat(TypeFactory.isSimpleTypeName("BOOL", ProductMode.PRODUCT_INTERNAL)).isTrue();
    assertThat(TypeFactory.isSimpleTypeName("Boolean", ProductMode.PRODUCT_INTERNAL)).isTrue();
    assertThat(TypeFactory.isSimpleTypeName("BYTES", ProductMode.PRODUCT_INTERNAL)).isTrue();
    assertThat(TypeFactory.isSimpleTypeName("DATE", ProductMode.PRODUCT_INTERNAL)).isTrue();
    assertThat(TypeFactory.isSimpleTypeName("double", ProductMode.PRODUCT_INTERNAL)).isTrue();
    assertThat(TypeFactory.isSimpleTypeName("ENUM", ProductMode.PRODUCT_INTERNAL)).isFalse();
    assertThat(TypeFactory.isSimpleTypeName("FLOAT", ProductMode.PRODUCT_INTERNAL)).isTrue();
    assertThat(TypeFactory.isSimpleTypeName("FLOAT32", ProductMode.PRODUCT_INTERNAL)).isTrue();
    assertThat(TypeFactory.isSimpleTypeName("FLOAT64", ProductMode.PRODUCT_INTERNAL)).isTrue();
    assertThat(TypeFactory.isSimpleTypeName("INT32", ProductMode.PRODUCT_INTERNAL)).isTrue();
    assertThat(TypeFactory.isSimpleTypeName("INT64", ProductMode.PRODUCT_INTERNAL)).isTrue();
    assertThat(TypeFactory.isSimpleTypeName("PROTO", ProductMode.PRODUCT_INTERNAL)).isFalse();
    assertThat(TypeFactory.isSimpleTypeName("STRING", ProductMode.PRODUCT_INTERNAL)).isTrue();
    assertThat(TypeFactory.isSimpleTypeName("STRUCT", ProductMode.PRODUCT_INTERNAL)).isFalse();
    assertThat(TypeFactory.isSimpleTypeName("TIMESTAMP", ProductMode.PRODUCT_INTERNAL)).isTrue();
    assertThat(TypeFactory.isSimpleTypeName("DATETIME", ProductMode.PRODUCT_INTERNAL)).isTrue();
    assertThat(TypeFactory.isSimpleTypeName("TIME", ProductMode.PRODUCT_INTERNAL)).isTrue();
    assertThat(TypeFactory.isSimpleTypeName("INTERVAL", ProductMode.PRODUCT_INTERNAL)).isTrue();
    assertThat(TypeFactory.isSimpleTypeName("UUID", ProductMode.PRODUCT_INTERNAL)).isTrue();
    assertThat(TypeFactory.isSimpleTypeName("UINT32", ProductMode.PRODUCT_INTERNAL)).isTrue();
    assertThat(TypeFactory.isSimpleTypeName("UINT64", ProductMode.PRODUCT_INTERNAL)).isTrue();
    assertThat(TypeFactory.isSimpleTypeName("UNKNOWN", ProductMode.PRODUCT_INTERNAL)).isFalse();
    assertThat(TypeFactory.isSimpleTypeName("RANGE", ProductMode.PRODUCT_INTERNAL)).isFalse();
  }

  @Test
  public void testCreateSimpleType() {
    for (TypeKind kind : TypeKind.values()) {
      if (TypeFactory.isSimpleType(kind)) {
        SimpleType type = TypeFactory.createSimpleType(kind);
        assertThat(type.getKind()).isEqualTo(kind);
        assertThat(type.isSimpleType()).isTrue();
      } else {
        assertThat(TypeFactory.createSimpleType(kind)).isNull();
      }
    }
  }

  @Test
  public void testCreateEnumType() {
    TypeFactory factory = TypeFactory.nonUniqueNames();
    EnumType type = factory.createEnumType(TypeKind.class);
    assertThat(type.getKind()).isEqualTo(TypeKind.TYPE_ENUM);
    assertThat(type.getDescriptor()).isEqualTo(TypeKind.getDescriptor());
  }

  @Test
  public void testCreateProtoType() {
    TypeFactory factory = TypeFactory.nonUniqueNames();
    ProtoType type = factory.createProtoType(TypeProto.class);
    assertThat(type.getKind()).isEqualTo(TypeKind.TYPE_PROTO);
    assertThat(type.getDescriptor()).isEqualTo(TypeProto.getDescriptor());
  }

  @Test
  public void testCreateArrayType() {
    TypeFactory factory = TypeFactory.nonUniqueNames();
    EnumType type = factory.createEnumType(TypeKind.class);
    ArrayType array = TypeFactory.createArrayType(type);
    assertThat(array.getKind()).isEqualTo(TypeKind.TYPE_ARRAY);
    assertThat(array.getElementType()).isEqualTo(type);
  }

  @Test
  public void testCreateStructType() {
    TypeFactory factory = TypeFactory.nonUniqueNames();
    EnumType enumType = factory.createEnumType(TypeKind.class);
    ProtoType protoType = factory.createProtoType(TypeProto.class);

    ArrayList<StructType.StructField> fields = new ArrayList<>();
    fields.add(new StructType.StructField("enum", enumType));
    fields.add(new StructType.StructField("proto", protoType));

    StructType struct = TypeFactory.createStructType(fields);
    assertThat(struct.getKind()).isEqualTo(TypeKind.TYPE_STRUCT);
    assertThat(struct.getFieldCount()).isEqualTo(2);
    assertThat(struct.getField(0).getName()).isEqualTo("enum");
    assertThat(struct.getField(1).getName()).isEqualTo("proto");
    assertThat(struct.getField(0).getType()).isEqualTo(enumType);
    assertThat(struct.getField(1).getType()).isEqualTo(protoType);
  }

  @Test
  public void testCreateRangeType() {
    SimpleType dateType = TypeFactory.createSimpleType(TypeKind.TYPE_DATE);
    RangeType range = TypeFactory.createRangeType(dateType);
    assertThat(range.getKind()).isEqualTo(TypeKind.TYPE_RANGE);
    assertThat(range.getElementType()).isEqualTo(dateType);
  }

  @Test
  public void testDedupByName() throws Exception {
    TypeFactory factory = TypeFactory.uniqueNames();
    EnumType enumType = factory.createEnumType(TypeKind.class);
    ProtoType protoType = factory.createProtoType(TypeProto.class);
    EnumType enumType2 = factory.createEnumType(TypeKind.class);
    ProtoType protoType2 = factory.createProtoType(TypeProto.class);
    assertThat(enumType).isSameInstanceAs(enumType2);
    assertThat(protoType).isSameInstanceAs(protoType2);
    // Test that no exceptions are thrown.
    EnumType enumType3 = factory.deserialize(enumType2.serialize()).asEnum();
    ProtoType protoType3 = factory.deserialize(protoType2.serialize()).asProto();
    assertThat(enumType3).isSameInstanceAs(enumType2);
    assertThat(protoType3).isSameInstanceAs(protoType2);
  }

  @Test
  public void testNoDedupByName() throws Exception {
    TypeFactory factory = TypeFactory.nonUniqueNames();
    EnumType enumType = factory.createEnumType(TypeKind.class);
    ProtoType protoType = factory.createProtoType(TypeProto.class);
    EnumType enumType2 = factory.createEnumType(TypeKind.class);
    ProtoType protoType2 = factory.createProtoType(TypeProto.class);
    assertThat(enumType).isNotSameInstanceAs(enumType2);
    assertThat(protoType).isNotSameInstanceAs(protoType2);
    assertThat(enumType.equivalent(enumType2)).isTrue();
    assertThat(protoType.equivalent(protoType2)).isTrue();
    // Test that no exceptions are thrown.
    EnumType enumType3 = factory.deserialize(enumType2.serialize()).asEnum();
    ProtoType protoType3 = factory.deserialize(protoType2.serialize()).asProto();
    assertThat(enumType3).isNotSameInstanceAs(enumType2);
    assertThat(protoType3).isNotSameInstanceAs(protoType2);
    assertThat(enumType3.equivalent(enumType2)).isTrue();
    assertThat(protoType3.equivalent(protoType2)).isTrue();
  }

  @Test
  public void testDeserializeInvalidTypeProto() throws Exception {
    // Should throw when deserializing empty TypeProto.
    TypeFactory factory = TypeFactory.nonUniqueNames();
    TypeProto.Builder builder = TypeProto.newBuilder();
    assertThrows(IllegalArgumentException.class, () -> factory.deserialize(builder.build()));

    // Should throw when deserializing unknown type.
    builder.setTypeKind(TypeKind.TYPE_UNKNOWN);
    assertThrows(IllegalArgumentException.class, () -> factory.deserialize(builder.build()));

    // Should throw when deserializing array without element.
    builder.setTypeKind(TypeKind.TYPE_ARRAY);
    assertThrows(IllegalArgumentException.class, () -> factory.deserialize(builder.build()));

    // Should throw when deserializing array of array.
    TypeProto.Builder elementBuilder = builder.getArrayTypeBuilder().getElementTypeBuilder();
    elementBuilder.setTypeKind(TypeKind.TYPE_ARRAY);
    elementBuilder.getArrayTypeBuilder().getElementTypeBuilder().setTypeKind(TypeKind.TYPE_BOOL);
    assertThrows(IllegalArgumentException.class, () -> factory.deserialize(builder.build()));

    // Should throw when deserializing range with an invalid element type.
    builder.clearArrayType();
    TypeProto.Builder rangeElementBuilder = builder.getRangeTypeBuilder().getElementTypeBuilder();
    rangeElementBuilder.setTypeKind(TypeKind.TYPE_RANGE);
    rangeElementBuilder
        .getRangeTypeBuilder()
        .getElementTypeBuilder()
        .setTypeKind(TypeKind.TYPE_BOOL);
    assertThrows(IllegalArgumentException.class, () -> factory.deserialize(builder.build()));

    // Should throw when deserializing empty enum.
    builder.clearRangeType();
    builder.setTypeKind(TypeKind.TYPE_ENUM);
    assertThrows(IllegalArgumentException.class, () -> factory.deserialize(builder.build()));

    // Should throw when deserializing enum without name.
    EnumTypeProto.Builder enumBuilder = builder.getEnumTypeBuilder();
    assertThrows(IllegalArgumentException.class, () -> factory.deserialize(builder.build()));

    // Should throw when deserializing enum without DescriptorPools.
    enumBuilder.setEnumName("googlesql.TypeKind");
    assertThrows(IllegalArgumentException.class, () -> factory.deserialize(builder.build()));

    // Should not throw when deserializing enum with DescriptorPool.
    GoogleSQLDescriptorPool pool = getDescriptorPoolWithTypeProtoAndTypeKind();
    enumBuilder.setEnumFileName("googlesql/public/type.proto");
    factory.deserialize(builder.build(), Lists.newArrayList(pool));

    // Should throw when deserializing enum with wrong type name.
    enumBuilder.setEnumName("googlesql.UnknownMessage");
    assertThrows(
        NullPointerException.class,
        () -> factory.deserialize(builder.build(), Lists.newArrayList(pool)));

    // Should throw when deserializing enum with wrong FileDescriptorSetIndex.
    enumBuilder.setEnumName("googlesql.TypeKind");
    enumBuilder.setFileDescriptorSetIndex(-1);
    assertThrows(
        IllegalArgumentException.class,
        () -> factory.deserialize(builder.build(), Lists.newArrayList(pool)));

    // Should throw when deserializing enum with wrong FileDescriptorSetIndex.
    enumBuilder.setFileDescriptorSetIndex(1);
    assertThrows(
        IllegalArgumentException.class,
        () -> factory.deserialize(builder.build(), Lists.newArrayList(pool)));

    // Should throw when deserializing enum with wrong FileDescriptorSet.
    assertThrows(
        NullPointerException.class,
        () ->
            factory.deserialize(
                builder.build(), Lists.newArrayList(pool, new GoogleSQLDescriptorPool())));

    // Should throw when deserializing enum with wrong filename.
    enumBuilder.setFileDescriptorSetIndex(0);
    enumBuilder.setEnumFileName("googlesql/public/wrong.proto");
    assertThrows(
        IllegalArgumentException.class,
        () -> factory.deserialize(builder.build(), Lists.newArrayList(pool)));

    // Should throw when deserializing enum with a catalog name.
    enumBuilder.setEnumFileName("googlesql/public/type.proto");
    enumBuilder.addCatalogNamePath("catalog");
    assertThrows(
        IllegalArgumentException.class,
        () -> factory.deserialize(builder.build(), Lists.newArrayList(pool)));

    // Should throw when deserializing proto without name.
    builder.clearEnumType();
    builder.setTypeKind(TypeKind.TYPE_PROTO);
    Builder protoBuilder = builder.getProtoTypeBuilder();
    assertThrows(
        IllegalArgumentException.class,
        () -> factory.deserialize(builder.build(), Lists.newArrayList(pool)));

    // Should throw when deserializing proto without DescriptorPools.
    protoBuilder.setProtoName("googlesql.TypeProto");
    assertThrows(
        IllegalArgumentException.class,
        () -> factory.deserialize(builder.build(), Lists.newArrayList(pool)));

    // Should not throw when deserializing proto with DescriptorPool.
    protoBuilder.setProtoFileName("googlesql/public/type.proto");
    factory.deserialize(builder.build(), Lists.newArrayList(pool));

    // Should throw when deserializing proto with wrong type name.
    protoBuilder.setProtoName("googlesql.UnknownMessage");
    assertThrows(
        NullPointerException.class,
        () -> factory.deserialize(builder.build(), Lists.newArrayList(pool)));

    // Should throw when deserializing proto with wrong FileDescriptorSetIndex.
    protoBuilder.setProtoName("googlesql.TypeProto");
    protoBuilder.setFileDescriptorSetIndex(-1);
    assertThrows(
        IllegalArgumentException.class,
        () -> factory.deserialize(builder.build(), Lists.newArrayList(pool)));

    // Should throw when deserializing proto with wrong FileDescriptorSetIndex.
    protoBuilder.setFileDescriptorSetIndex(1);
    assertThrows(
        IllegalArgumentException.class,
        () -> factory.deserialize(builder.build(), Lists.newArrayList(pool)));

    // Should throw when deserializing proto with wrong FileDescriptorSet.
    assertThrows(
        NullPointerException.class,
        () ->
            factory.deserialize(
                builder.build(), Lists.newArrayList(pool, new GoogleSQLDescriptorPool())));

    // Should throw when deserializing proto with wrong filename.
    protoBuilder.setFileDescriptorSetIndex(0);
    protoBuilder.setProtoFileName("googlesql/public/wrong.proto");
    assertThrows(
        IllegalArgumentException.class,
        () -> factory.deserialize(builder.build(), Lists.newArrayList(pool)));

    // Should throw when deserializing proto with a catalog name.
    protoBuilder.setProtoFileName("googlesql/public/type.proto");
    protoBuilder.addCatalogNamePath("catalog");
    assertThrows(
        IllegalArgumentException.class,
        () -> factory.deserialize(builder.build(), Lists.newArrayList(pool)));
  }

  @Test
  public void testNonGeneratedClass() {
    TypeFactory factory = TypeFactory.nonUniqueNames();
    // Should throw when trying to create proto type from non-generated proto class.
    assertThrows(
        IllegalArgumentException.class, () -> factory.createProtoType(BadGeneratedProto.class));

    // Should throw when trying to create proto type from non-generated enum class.
    assertThrows(
        IllegalArgumentException.class, () -> factory.createEnumType(BadGeneratedEnum.class));
  }

  @Test
  public void testSerializable() {
    TypeFactory factory = TypeFactory.nonUniqueNames();
    ProtoType protoType = factory.createProtoType(TypeProto.class);
    EnumType enumType = factory.createEnumType(TypeKind.class);
    ArrayType arrayType = TypeFactory.createArrayType(protoType);
    ArrayList<StructType.StructField> fields = new ArrayList<>();
    fields.add(new StructType.StructField("enum", enumType));
    fields.add(new StructType.StructField("proto", protoType));
    StructType structType = TypeFactory.createStructType(fields);

    TypeFactory factory2 = SerializableTester.reserialize(factory);
    ProtoType protoType2 = factory2.createProtoType(TypeProto.class);
    EnumType enumType2 = factory2.createEnumType(TypeKind.class);
    ArrayType arrayType2 = TypeFactory.createArrayType(protoType2);
    fields = new ArrayList<>();
    fields.add(new StructType.StructField("enum", enumType));
    fields.add(new StructType.StructField("proto", protoType));
    StructType structType2 = TypeFactory.createStructType(fields);

    // The descriptors used in these types will be different references due to the fact that proto
    // descriptors aren't serializable, but the types should otherwise be equivalent.
    assertThat(protoType.equivalent(protoType2)).isTrue();
    assertThat(enumType.equivalent(enumType2)).isTrue();
    assertThat(arrayType.equivalent(arrayType2)).isTrue();
    assertThat(structType.equivalent(structType2)).isTrue();
  }

  private static final class BadGeneratedProto extends GeneratedMessage {

    @Override
    public com.google.protobuf.Message.Builder newBuilderForType() {
      return null;
    }

    @Override
    protected com.google.protobuf.Message.Builder newBuilderForType(BuilderParent parent) {
      return null;
    }

    @Override
    public com.google.protobuf.Message.Builder toBuilder() {
      return null;
    }

    @Override
    public Message getDefaultInstanceForType() {
      return null;
    }

    @Override
    protected FieldAccessorTable internalGetFieldAccessorTable() {
      return null;
    }
  }

  private static final class BadGeneratedEnum implements ProtocolMessageEnum {
    @Override
    public int getNumber() {
      return 0;
    }

    @Override
    public EnumValueDescriptor getValueDescriptor() {
      return null;
    }

    @Override
    public EnumDescriptor getDescriptorForType() {
      return null;
    }
  }
}
