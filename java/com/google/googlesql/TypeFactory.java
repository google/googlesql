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
import static com.google.common.base.Preconditions.checkNotNull;

import com.google.common.base.Ascii;
import com.google.common.base.Function;
import com.google.common.collect.ImmutableList;
import com.google.common.collect.ImmutableMap;
import com.google.common.collect.ImmutableSet;
import com.google.common.collect.Maps;
import com.google.errorprone.annotations.CanIgnoreReturnValue;
import com.google.protobuf.DescriptorProtos.FileDescriptorSet;
import com.google.protobuf.Descriptors.Descriptor;
import com.google.protobuf.Descriptors.EnumDescriptor;
import com.google.protobuf.Message;
import com.google.protobuf.MessageOrBuilder;
import com.google.protobuf.ProtocolMessageEnum;
import com.google.googlesql.DescriptorPool.GoogleSQLDescriptor;
import com.google.googlesql.DescriptorPool.GoogleSQLEnumDescriptor;
import com.google.googlesql.GoogleSQLOptions.ProductMode;
import com.google.googlesql.GoogleSQLType.ArrayTypeProto;
import com.google.googlesql.GoogleSQLType.EnumTypeProto;
import com.google.googlesql.GoogleSQLType.MapTypeProto;
import com.google.googlesql.GoogleSQLType.MeasureTypeProto;
import com.google.googlesql.GoogleSQLType.ProtoTypeProto;
import com.google.googlesql.GoogleSQLType.RangeTypeProto;
import com.google.googlesql.GoogleSQLType.StructFieldProto;
import com.google.googlesql.GoogleSQLType.StructTypeProto;
import com.google.googlesql.GoogleSQLType.TypeKind;
import com.google.googlesql.GoogleSQLType.TypeProto;
import com.google.googlesql.GraphElementType.PropertyType;
import com.google.googlesql.StructType.StructField;
import java.io.Serializable;
import java.util.Collection;
import java.util.HashMap;
import java.util.List;
import java.util.Map;

/**
 * A factory for {@link Type} objects.
 *
 * <p>A {@code TypeFactory} can be obtained via {@link #uniqueNames()} or {@link #nonUniqueNames()}
 * static factory methods, depending if you want to guarantee uniqueness of descriptor full names
 * for non-simple types or not, respectively.
 */
public abstract class TypeFactory implements Serializable {

  private static final ImmutableMap<String, TypeKind> SIMPLE_TYPE_KIND_NAMES =
      new ImmutableMap.Builder<String, TypeKind>()
          .put("int32", TypeKind.TYPE_INT32)
          .put("int64", TypeKind.TYPE_INT64) // external
          .put("uint32", TypeKind.TYPE_UINT32)
          .put("uint64", TypeKind.TYPE_UINT64)
          .put("bool", TypeKind.TYPE_BOOL) // external
          .put("boolean", TypeKind.TYPE_BOOL) // external
          .put("float", TypeKind.TYPE_FLOAT)
          .put("float32", TypeKind.TYPE_FLOAT)
          .put("double", TypeKind.TYPE_DOUBLE)
          .put("float64", TypeKind.TYPE_DOUBLE) // external
          .put("string", TypeKind.TYPE_STRING) // external
          .put("bytes", TypeKind.TYPE_BYTES) // external
          .put("date", TypeKind.TYPE_DATE) // external
          .put("timestamp", TypeKind.TYPE_TIMESTAMP) // external
          .put("time", TypeKind.TYPE_TIME) // external
          .put("datetime", TypeKind.TYPE_DATETIME) // external
          .put("interval", TypeKind.TYPE_INTERVAL) // external
          .put("geography", TypeKind.TYPE_GEOGRAPHY) // external
          .put("numeric", TypeKind.TYPE_NUMERIC) // external
          .put("bignumeric", TypeKind.TYPE_BIGNUMERIC) // external
          .put("json", TypeKind.TYPE_JSON) // external
          .put("uuid", TypeKind.TYPE_UUID) // external
          .build();

  // See (broken link) for approved list of externally visible types.
  private static final ImmutableSet<String> EXTERNAL_MODE_SIMPLE_TYPE_KIND_NAMES =
      ImmutableSet.of(
          "int64",
          "bool",
          "boolean",
          "float64",
          "string",
          "bytes",
          "date",
          "timestamp",
          "time",
          "datetime",
          "interval",
          "geography",
          "numeric",
          "bignumeric",
          "json",
          "uuid");

  private static final ImmutableSet<TypeKind> SIMPLE_TYPE_KINDS =
      ImmutableSet.copyOf(SIMPLE_TYPE_KIND_NAMES.values());

  private static final ImmutableMap<TypeKind, SimpleType> SIMPLE_TYPES =
      Maps.toMap(
          SIMPLE_TYPE_KINDS,
          new Function<TypeKind, SimpleType>() {
            @Override
            public SimpleType apply(TypeKind typeKind) {
              return new SimpleType(typeKind);
            }
          });

  /**
   * Returns whether the given type kind is a simple type.
   *
   * <p>Simple types are those that can be represented with just a TypeKind, with no parameters.
   */
  public static boolean isSimpleType(TypeKind kind) {
    return SIMPLE_TYPE_KINDS.contains(kind);
  }

  /**
   * Returns whether {@code typeName} identifies a simple type.
   *
   * <p>Simple types are those that can be represented with just a TypeKind, with no parameters.
   */
  public static boolean isSimpleTypeName(String typeName, ProductMode prodMode) {
    if (prodMode == ProductMode.PRODUCT_EXTERNAL) {
      return EXTERNAL_MODE_SIMPLE_TYPE_KIND_NAMES.contains(Ascii.toLowerCase(typeName));
    }
    return SIMPLE_TYPE_KIND_NAMES.containsKey(Ascii.toLowerCase(typeName));
  }

  /**
   * Returns a TypeFactory which does *not* enforce uniquely named types.
   *
   * <p>The returned TypeFactory allows the creation of {@link EnumType EnumTypes} and {@link
   * ProtoType ProtoTypes} with different descriptors, even if they share the same full name.
   */
  public static TypeFactory nonUniqueNames() {
    return NonUniqueNamesTypeFactory.getInstance();
  }

  /**
   * Returns a TypeFactory which enforces uniquely named types.
   *
   * <p>For {@link EnumType EnumTypes} and {@link ProtoType ProtoTypes} created through the returned
   * factory, it is true that:
   *
   * <pre>
   * For all a, b,
   * if a.getDescriptor().getFullName().equals(b.getDescriptor().getFullName()),
   * then a == b.
   * </pre>
   */
  public static TypeFactory uniqueNames() {
    return new UniqueNamesTypeFactory();
  }

  /** Returns a SimpleType of given {@code kind}. */
  public static SimpleType createSimpleType(TypeKind kind) {
    return SIMPLE_TYPES.get(kind);
  }

  /** Returns a new ArrayType with the given {@code elementType}. */
  public static ArrayType createArrayType(Type elementType) {
    return new ArrayType(elementType);
  }

  /** Returns a StructType that contains the given {@code fields}. */
  public static StructType createStructType(Collection<StructType.StructField> fields) {
    return new StructType(fields);
  }

  public static RangeType createRangeType(Type elementType) {
    return new RangeType(elementType);
  }

  /** Returns a MapType that contains the given {@code keyType} and {@code valueType}. */
  public static MapType createMapType(Type keyType, Type valueType) {
    return new MapType(keyType, valueType);
  }

  /** Returns a MeasureType that produces the given {@code resultType} when aggregated. */
  public static MeasureType createMeasureType(Type resultType) {
    return new MeasureType(resultType);
  }

  /** Returns a GraphElementType that contains the given {@code properties}. */
  public static GraphElementType createGraphElementType(
      List<String> graphReference,
      GoogleSQLType.GraphElementTypeProto.ElementKind kind,
      boolean isDynamic,
      Collection<PropertyType> propertyTypes) {
    if (graphReference.isEmpty()) {
      throw new IllegalArgumentException("Graph reference cannot be empty");
    }
    if (kind != GoogleSQLType.GraphElementTypeProto.ElementKind.KIND_NODE
        && kind != GoogleSQLType.GraphElementTypeProto.ElementKind.KIND_EDGE) {
      throw new IllegalArgumentException(String.format("Invalid element kind: %s", kind));
    }
    Map<String, PropertyType> propertyTypeByName = new HashMap<>();
    for (PropertyType propertyType : propertyTypes) {
      String propertyTypeName = Ascii.toLowerCase(propertyType.getName());
      PropertyType existingPropertyType =
          propertyTypeByName.putIfAbsent(propertyTypeName, propertyType);
      if (existingPropertyType != null && !existingPropertyType.equals(propertyType)) {
        throw new IllegalArgumentException(
            String.format("Incompatible property type with name: %s", propertyTypeName));
      }
    }
    return new GraphElementType(
        graphReference, kind, isDynamic, ImmutableSet.copyOf(propertyTypes));
  }

  public static GraphPathType createGraphPathType(
      GraphElementType nodeType, GraphElementType edgeType) {
    if (nodeType.getElementKind() != GoogleSQLType.GraphElementTypeProto.ElementKind.KIND_NODE) {
      throw new IllegalArgumentException(
          String.format("Invalid node type: %s", nodeType.getElementKind()));
    }
    if (edgeType.getElementKind() != GoogleSQLType.GraphElementTypeProto.ElementKind.KIND_EDGE) {
      throw new IllegalArgumentException(
          String.format("Invalid edge type: %s", edgeType.getElementKind()));
    }
    boolean graphReferencesMatch = true;
    if (nodeType.getGraphReference().size() != edgeType.getGraphReference().size()) {
      graphReferencesMatch = false;
    } else {
      for (int i = 0; i < nodeType.getGraphReference().size(); ++i) {
        if (!Ascii.equalsIgnoreCase(
            nodeType.getGraphReference().get(i), edgeType.getGraphReference().get(i))) {
          graphReferencesMatch = false;
          break;
        }
      }
    }
    if (!graphReferencesMatch) {
      throw new IllegalArgumentException(
          String.format(
              "Graph references do not match between the node and edge type: %s and %s",
              nodeType.getGraphReference(), edgeType.getGraphReference()));
    }

    return new GraphPathType(nodeType, edgeType);
  }

  /**
   * Returns a ProtoType with a proto message descriptor that is loaded from FileDescriptorSet with
   * {@link DescriptorPool}.
   *
   * @param descriptor A GoogleSQLDescriptor that defines the ProtoType. A GoogleSQLDescriptor can
   *     be retrieved from a DescriptorPool, which in turn can be created by loading a
   *     FileDescriptorSet protobuf.
   */
  public abstract ProtoType createProtoType(GoogleSQLDescriptor descriptor);

  /**
   * Returns a ProtoType with a generated (i.e. one that is compiled into the Java program)
   * descriptor.
   *
   * @param generatedDescriptorClass Class of a generated protocol message.
   */
  public abstract ProtoType createProtoType(Class<? extends Message> descriptorClass);

  /**
   * Returns a EnumType with a enum descriptor that is loaded from FileDescriptorSet with {@link
   * DescriptorPool}.
   *
   * @param descriptor GoogleSQLEnumDescriptor that defines the EnumType. A GoogleSQLDescriptor can
   *     be retrieved from a DescriptorPool, which in turn can be created by loading a
   *     FileDescriptorSet protobuf.
   */
  public abstract EnumType createEnumType(GoogleSQLEnumDescriptor descriptor);

  /**
   * Returns a EnumType with a generated (i.e. one that is compiled into the Java program) enum
   * descriptor.
   *
   * @param generatedEnumClass Class that is generated from a protobuf enum.
   */
  public abstract EnumType createEnumType(Class<? extends ProtocolMessageEnum> generatedEnumClass);

  protected abstract EnumType createOpaqueEnumType(GoogleSQLEnumDescriptor descriptor);

  /** Deserialize a self-contained {@link TypeProto} into a {@link Type}. */
  @CanIgnoreReturnValue // TODO: consider removing this?
  public abstract Type deserialize(TypeProto proto);

  /**
   * Deserialize a {@link TypeProto} into a {@link Type} using the given {@link DescriptorPool
   * DescriptorPools}.
   */
  @CanIgnoreReturnValue // TODO: consider removing this?
  public abstract Type deserialize(TypeProto proto, List<? extends DescriptorPool> pools);

  /**
   * A {@link TypeFactory} which implements methods based on {@link #createProtoType(Descriptor,
   * DescriptorPool)} and {@link #createEnumType(EnumDescriptor, DescriptorPool)}.
   */
  private abstract static class AbstractTypeFactory extends TypeFactory {

    /**
     * Creates a {@link ProtoType} using the given {@link Descriptor}.
     *
     * <p>The {@code Descriptor} together with the {@link DescriptorPool} fully describes the {@code
     * ProtoType}.
     */
    protected abstract ProtoType createProtoType(Descriptor descriptor, DescriptorPool pool);

    @Override
    public final ProtoType createProtoType(Class<? extends Message> messageClass) {
      Descriptor descriptor = getDescriptor(messageClass);
      GoogleSQLDescriptorPool.importIntoGeneratedPool(descriptor);
      GoogleSQLDescriptorPool pool = GoogleSQLDescriptorPool.getGeneratedPool();
      return createProtoType(descriptor, pool);
    }

    @Override
    public final ProtoType createProtoType(GoogleSQLDescriptor descriptor) {
      return createProtoType(descriptor.getDescriptor(), descriptor.getDescriptorPool());
    }

    /**
     * Creates a {@link EnumType} using the given {@link EnumDescriptor}.
     *
     * <p>The {@code EnumDescriptor} together with the {@link DescriptorPool} fully describes the
     * {@code EnumType}.
     */
    protected abstract EnumType createEnumType(
        EnumDescriptor descriptor, DescriptorPool pool, boolean isOpaque);

    @Override
    public final EnumType createEnumType(GoogleSQLEnumDescriptor descriptor) {
      return createEnumType(descriptor.getDescriptor(), descriptor.getDescriptorPool(), false);
    }

    @Override
    public final EnumType createEnumType(Class<? extends ProtocolMessageEnum> generatedEnumClass) {
      EnumDescriptor descriptor = getEnumDescriptor(generatedEnumClass);
      GoogleSQLDescriptorPool.importIntoGeneratedPool(descriptor);
      GoogleSQLDescriptorPool pool = GoogleSQLDescriptorPool.getGeneratedPool();
      return createEnumType(descriptor, pool, false);
    }

    @Override
    public final EnumType createOpaqueEnumType(GoogleSQLEnumDescriptor descriptor) {
      return createEnumType(descriptor.getDescriptor(), descriptor.getDescriptorPool(), true);
    }

    @Override
    public final Type deserialize(TypeProto proto) {
      ImmutableList.Builder<DescriptorPool> pools = ImmutableList.builder();
      for (FileDescriptorSet fileDescriptorSet : proto.getFileDescriptorSetList()) {
        GoogleSQLDescriptorPool pool = new GoogleSQLDescriptorPool();
        pool.importFileDescriptorSet(fileDescriptorSet);
        pools.add(pool);
      }
      return deserialize(proto, pools.build());
    }

    @Override
    public final Type deserialize(TypeProto proto, List<? extends DescriptorPool> pools) {
      if (TypeFactory.isSimpleType(proto.getTypeKind())) {
        return deserializeSimpleType(proto);
      }
      switch (proto.getTypeKind()) {
        case TYPE_ENUM:
          return deserializeEnumType(proto, pools);

        case TYPE_ARRAY:
          return deserializeArrayType(proto, pools);

        case TYPE_STRUCT:
          return deserializeStructType(proto, pools);

        case TYPE_PROTO:
          return deserializeProtoType(proto, pools);

        case TYPE_RANGE:
          return deserializeRangeType(proto, pools);

        case TYPE_GRAPH_ELEMENT:
          return deserializeGraphElementType(proto, pools);

        case TYPE_GRAPH_PATH:
          return deserializeGraphPathType(proto, pools);

        case TYPE_MAP:
          return deserializeMapType(proto, pools);

        case TYPE_MEASURE:
          return deserializeMeasureType(proto, pools);

        default:
          throw new IllegalArgumentException(
              String.format("proto.type_kind: %s", proto.getTypeKind()));
      }
    }

    private static Type deserializeSimpleType(TypeProto proto) {
      return createSimpleType(proto.getTypeKind());
    }

    private EnumType deserializeEnumType(TypeProto proto, List<? extends DescriptorPool> pools) {
      EnumTypeProto enumType = proto.getEnumType();

      String name = enumType.getEnumName();
      checkArgument(!name.isEmpty(), "Names missing from EnumTypeProto %s", enumType);

      int index = enumType.getFileDescriptorSetIndex();
      checkArgument(
          index >= 0 && index < pools.size(), "FileDescriptorSetIndex out of bound: %s", enumType);

      checkArgument(
          enumType.getCatalogNamePathCount() == 0,
          "Catalog name support for EnumTypeProto is not implemented: %s",
          enumType);

      DescriptorPool pool = pools.get(index);

      GoogleSQLEnumDescriptor descriptor = pool.findEnumTypeByName(name);
      checkNotNull(descriptor, "Enum descriptor not found: %s", enumType);

      String filename = descriptor.getDescriptor().getFile().getName();
      checkArgument(
          filename.equals(enumType.getEnumFileName()),
          "Enum %s found in wrong file: %s",
          enumType,
          filename);

      return createEnumType(descriptor.getDescriptor(), pool, enumType.getIsOpaque());
    }

    private ArrayType deserializeArrayType(TypeProto proto, List<? extends DescriptorPool> pools) {
      ArrayTypeProto arrayType = proto.getArrayType();
      return createArrayType(deserialize(arrayType.getElementType(), pools));
    }

    private StructType deserializeStructType(
        TypeProto proto, List<? extends DescriptorPool> pools) {
      StructTypeProto structType = proto.getStructType();
      ImmutableList.Builder<StructField> fields = ImmutableList.builder();
      for (StructFieldProto field : structType.getFieldList()) {
        fields.add(new StructField(field.getFieldName(), deserialize(field.getFieldType(), pools)));
      }
      return createStructType(fields.build());
    }

    @SuppressWarnings("LenientFormatStringValidation")
    private ProtoType deserializeProtoType(TypeProto proto, List<? extends DescriptorPool> pools) {
      ProtoTypeProto protoType = proto.getProtoType();

      String name = protoType.getProtoName();
      checkArgument(!name.isEmpty(), "Name missing from ProtoTypeProto %s", protoType);

      int index = protoType.getFileDescriptorSetIndex();
      checkArgument(
          index >= 0 && index < pools.size(), "FileDescriptorSetIndex out of bound: %s", protoType);

      checkArgument(
          protoType.getCatalogNamePathCount() == 0,
          "Catalog name support for ProtoTypeProto is not implemented: %s",
          protoType);

      DescriptorPool pool = pools.get(index);

      GoogleSQLDescriptor descriptor = pool.findMessageTypeByName(name);
      // Expected 0 args, but got 1.
      checkNotNull(descriptor, "Proto descriptor not found: ", protoType);

      String filename = descriptor.getDescriptor().getFile().getName();
      checkArgument(
          filename.equals(protoType.getProtoFileName()),
          "Proto %s found in wrong file: %s",
          protoType,
          filename);

      return createProtoType(descriptor.getDescriptor(), pool);
    }

    private RangeType deserializeRangeType(TypeProto proto, List<? extends DescriptorPool> pools) {
      RangeTypeProto rangeType = proto.getRangeType();
      return createRangeType(deserialize(rangeType.getElementType(), pools));
    }

    private GraphElementType deserializeGraphElementType(
        TypeProto proto, List<? extends DescriptorPool> pools) {
      return deserializeGraphElementType(proto.getGraphElementType(), pools);
    }

    private GraphElementType deserializeGraphElementType(
        GoogleSQLType.GraphElementTypeProto graphElementType,
        List<? extends DescriptorPool> pools) {
      ImmutableList.Builder<PropertyType> propertyTypes = ImmutableList.builder();
      for (GoogleSQLType.GraphElementTypeProto.PropertyTypeProto propertyType :
          graphElementType.getPropertyTypeList()) {
        propertyTypes.add(
            new PropertyType(
                propertyType.getName(), deserialize(propertyType.getValueType(), pools)));
      }
      return createGraphElementType(
          graphElementType.getGraphReferenceList(),
          graphElementType.getKind(),
          graphElementType.getIsDynamic(),
          propertyTypes.build());
    }

    private GraphPathType deserializeGraphPathType(
        TypeProto proto, List<? extends DescriptorPool> pools) {
      GoogleSQLType.GraphPathTypeProto graphPath = proto.getGraphPathType();
      return createGraphPathType(
          deserializeGraphElementType(graphPath.getNodeType(), pools),
          deserializeGraphElementType(graphPath.getEdgeType(), pools));
    }

    private MapType deserializeMapType(TypeProto proto, List<? extends DescriptorPool> pools) {
      MapTypeProto mapType = proto.getMapType();
      return createMapType(
          deserialize(mapType.getKeyType(), pools), deserialize(mapType.getValueType(), pools));
    }

    private MeasureType deserializeMeasureType(
        TypeProto proto, List<? extends DescriptorPool> pools) {
      MeasureTypeProto measureType = proto.getMeasureType();
      return createMeasureType(deserialize(measureType.getResultType(), pools));
    }
  }

  private static Descriptor getDescriptor(Class<? extends MessageOrBuilder> type) {
    try {
      return (Descriptor) type.getMethod("getDescriptor").invoke(null);
    } catch (ReflectiveOperationException e) {
      throw new IllegalArgumentException(e);
    }
  }

  private static EnumDescriptor getEnumDescriptor(Class<? extends ProtocolMessageEnum> type) {
    try {
      return (EnumDescriptor) type.getMethod("getDescriptor").invoke(null);
    } catch (ReflectiveOperationException e) {
      throw new IllegalArgumentException(e);
    }
  }

  /** A {@link TypeFactory} which does *not* ensure uniquely named types. */
  private static final class NonUniqueNamesTypeFactory extends AbstractTypeFactory {
    private static final NonUniqueNamesTypeFactory INSTANCE = new NonUniqueNamesTypeFactory();

    static NonUniqueNamesTypeFactory getInstance() {
      return INSTANCE;
    }

    private NonUniqueNamesTypeFactory() {
      super();
    }

    @Override
    protected ProtoType createProtoType(Descriptor descriptor, DescriptorPool pool) {
      return new ProtoType(descriptor, pool);
    }

    @Override
    protected EnumType createEnumType(
        EnumDescriptor descriptor, DescriptorPool pool, boolean isOpaque) {
      return new EnumType(descriptor, pool, isOpaque);
    }
  }

  /** A {@link TypeFactory} which ensures uniquely named types. */
  private static final class UniqueNamesTypeFactory extends AbstractTypeFactory {
    private Map<String, EnumType> enumTypesByName;
    private Map<String, ProtoType> protoTypesByName;

    UniqueNamesTypeFactory() {
      super();
      enumTypesByName = new HashMap<>();
      protoTypesByName = new HashMap<>();
    }

    @Override
    public ProtoType createProtoType(Descriptor descriptor, DescriptorPool pool) {
      if (protoTypesByName.containsKey(descriptor.getFullName())) {
        return protoTypesByName.get(descriptor.getFullName());
      }
      ProtoType protoType = new ProtoType(descriptor, pool);
      protoTypesByName.put(descriptor.getFullName(), protoType);
      return protoType;
    }

    @Override
    public EnumType createEnumType(
        EnumDescriptor descriptor, DescriptorPool pool, boolean isOpaque) {
      if (enumTypesByName.containsKey(descriptor.getFullName())) {
        return enumTypesByName.get(descriptor.getFullName());
      }
      EnumType enumType = new EnumType(descriptor, pool, isOpaque);
      enumTypesByName.put(descriptor.getFullName(), enumType);
      return enumType;
    }
  }
}
