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

import static java.util.Map.entry;

import com.google.common.collect.ImmutableList;
import com.google.common.collect.ImmutableMap;
import com.google.errorprone.annotations.Immutable;
import com.google.protobuf.DescriptorProtos.FileDescriptorSet;
import com.google.protobuf.Timestamp;
import com.google.googlesql.GoogleSQLOptions.ProductMode;
import com.google.googlesql.GoogleSQLType.TypeKind;
import com.google.googlesql.GoogleSQLType.TypeProto;
import com.google.googlesql.GoogleSQLValue.ValueProto.TimestampPicos;
import java.io.Serializable;

/**
 * In-memory representation of a GoogleSQL type. See (broken link) for more information on the
 * type system.
 *
 * <p>Types can only created by a TypeFactory.
 */
@Immutable
public abstract class Type implements Serializable {
  // The valid date range is [ 0001-01-01, 9999-12-31 ].
  @SuppressWarnings("GoodTime") // should be a java.time.LocalDate (?)
  public static final int DATE_MIN = -719162;

  @SuppressWarnings("GoodTime") // should be a java.time.LocalDate (?)
  public static final int DATE_MAX = 2932896;

  public static final int MICROS_PER_SECOND = 1_000_000;
  public static final long PICOS_PER_SECOND = 1_000_000_000_000L;

  // The valid timestamp range for seconds is:
  //  [ 0001-01-01 00:00:00 UTC, 9999-12-31 23:59:59 UTC ]
  @SuppressWarnings("GoodTime") // should be a java.time.Instant
  public static final long TIMESTAMP_SECONDS_MIN = -62135596800L;

  @SuppressWarnings("GoodTime") // should be a java.time.Instant
  public static final long TIMESTAMP_SECONDS_MAX = 253402300799L;

  // The valid timestamp range for microseconds is:
  //  [ 0001-01-01 00:00:00 UTC, 9999-12-31 23:59:59.999999 UTC ]
  @SuppressWarnings("GoodTime") // should be a java.time.Instant
  public static final long TIMESTAMP_MICROS_MIN = TIMESTAMP_SECONDS_MIN * MICROS_PER_SECOND;

  @SuppressWarnings("GoodTime") // should be a java.time.Instant
  public static final long TIMESTAMP_MICROS_MAX =
      TIMESTAMP_SECONDS_MAX * MICROS_PER_SECOND + MICROS_PER_SECOND - 1;

  static final ImmutableMap<TypeKind, String> TYPE_KIND_NAMES =
      ImmutableMap.ofEntries(
          entry(TypeKind.TYPE_UNKNOWN, "UNKNOWN"), // Not a valid type.
          entry(TypeKind.TYPE_INT32, "INT32"),
          entry(TypeKind.TYPE_INT64, "INT64"),
          entry(TypeKind.TYPE_UINT32, "UINT32"),
          entry(TypeKind.TYPE_UINT64, "UINT64"),
          entry(TypeKind.TYPE_BOOL, "BOOL"),
          entry(TypeKind.TYPE_FLOAT, "FLOAT"),
          entry(TypeKind.TYPE_DOUBLE, "DOUBLE"),
          entry(TypeKind.TYPE_STRING, "STRING"),
          entry(TypeKind.TYPE_BYTES, "BYTES"),
          entry(TypeKind.TYPE_DATE, "DATE"),
          entry(TypeKind.TYPE_ENUM, "ENUM"),
          entry(TypeKind.TYPE_ARRAY, "ARRAY"),
          entry(TypeKind.TYPE_STRUCT, "STRUCT"),
          entry(TypeKind.TYPE_PROTO, "PROTO"),
          entry(TypeKind.TYPE_TIMESTAMP, "TIMESTAMP"),
          entry(TypeKind.TYPE_TIME, "TIME"),
          entry(TypeKind.TYPE_DATETIME, "DATETIME"),
          entry(TypeKind.TYPE_GEOGRAPHY, "GEOGRAPHY"),
          entry(TypeKind.TYPE_NUMERIC, "NUMERIC"),
          entry(TypeKind.TYPE_BIGNUMERIC, "BIGNUMERIC"),
          entry(TypeKind.TYPE_EXTENDED, "EXTENDED"),
          entry(TypeKind.TYPE_JSON, "JSON"),
          entry(TypeKind.TYPE_INTERVAL, "INTERVAL"),
          entry(TypeKind.TYPE_RANGE, "RANGE"),
          entry(TypeKind.TYPE_GRAPH_ELEMENT, "GRAPH_ELEMENT"),
          entry(TypeKind.TYPE_GRAPH_PATH, "GRAPH_PATH"),
          entry(TypeKind.TYPE_MAP, "MAP"),
          entry(TypeKind.TYPE_UUID, "UUID"),
          entry(TypeKind.TYPE_MEASURE, "MEASURE"));

  /** Returns {@code true} if the given {@code date} value is within valid range. */
  @SuppressWarnings("GoodTime") // should accept a java.time.LocalDate (?)
  public static boolean isValidDate(int date) {
    return date >= DATE_MIN && date <= DATE_MAX;
  }

  /** Returns {@code true} if the given {@code timestamp} value is within valid range. */
  @SuppressWarnings("GoodTime") // should accept a java.time.Instant
  public static boolean isValidTimestampUnixMicros(long timestamp) {
    return timestamp >= TIMESTAMP_MICROS_MIN && timestamp <= TIMESTAMP_MICROS_MAX;
  }

  /**
   * Returns {@code true} if the given {@code timestamp} value is within valid range.
   * TODO: Add validation on nanos part of timestamp.
   */
  @SuppressWarnings("NonApiType") // should accept a java.time.Instant
  public static boolean isValidTimestamp(Timestamp timestamp) {
    return isValidTimestampSeconds(timestamp.getSeconds());
  }

  /** Returns {@code true} if the given {@code timestampPicos} value is within valid range. */
  public static boolean isValidTimestamp(TimestampPicos timestampPicos) {
    return isValidTimestampSeconds(timestampPicos.getSeconds())
        && timestampPicos.getPicos() >= 0
        && timestampPicos.getPicos() < PICOS_PER_SECOND;
  }

  /* Validates a timestamp with seconds precision. */
  private static boolean isValidTimestampSeconds(long seconds) {
    return seconds >= TIMESTAMP_SECONDS_MIN && seconds <= TIMESTAMP_SECONDS_MAX;
  }

  private final TypeKind kind;

  Type(TypeKind kind) {
    this.kind = kind;
  }

  /** Returns TypeKind of this type. */
  public TypeKind getKind() {
    return kind;
  }

  // Returns the component types of this type in deterministic order.
  // Returns empty if the type is not composite.
  //
  public ImmutableList<Type> componentTypes() {
    return ImmutableList.of();
  }

  public boolean isInt32() {
    return kind == TypeKind.TYPE_INT32;
  }

  public boolean isInt64() {
    return kind == TypeKind.TYPE_INT64;
  }

  public boolean isUint32() {
    return kind == TypeKind.TYPE_UINT32;
  }

  public boolean isUint64() {
    return kind == TypeKind.TYPE_UINT64;
  }

  public boolean isBool() {
    return kind == TypeKind.TYPE_BOOL;
  }

  public boolean isFloat() {
    return kind == TypeKind.TYPE_FLOAT;
  }

  public boolean isDouble() {
    return kind == TypeKind.TYPE_DOUBLE;
  }

  public boolean isNumeric() {
    return kind == TypeKind.TYPE_NUMERIC;
  }

  public boolean isBigNumeric() {
    return kind == TypeKind.TYPE_BIGNUMERIC;
  }

  public boolean isJson() {
    return kind == TypeKind.TYPE_JSON;
  }

  public boolean isString() {
    return kind == TypeKind.TYPE_STRING;
  }

  public boolean isBytes() {
    return kind == TypeKind.TYPE_BYTES;
  }

  public boolean isDate() {
    return kind == TypeKind.TYPE_DATE;
  }

  public boolean isTimestamp() {
    return kind == TypeKind.TYPE_TIMESTAMP;
  }

  public boolean isDatetime() {
    return kind == TypeKind.TYPE_DATETIME;
  }

  public boolean isTime() {
    return kind == TypeKind.TYPE_TIME;
  }

  public boolean isInterval() {
    return kind == TypeKind.TYPE_INTERVAL;
  }

  public boolean isGeography() {
    return kind == TypeKind.TYPE_GEOGRAPHY;
  }

  public boolean isEnum() {
    return kind == TypeKind.TYPE_ENUM;
  }

  public boolean isArray() {
    return kind == TypeKind.TYPE_ARRAY;
  }

  public boolean isStruct() {
    return kind == TypeKind.TYPE_STRUCT;
  }

  public boolean isProto() {
    return kind == TypeKind.TYPE_PROTO;
  }

  public boolean isRange() {
    return kind == TypeKind.TYPE_RANGE;
  }

  public boolean isGraphElement() {
    return kind == TypeKind.TYPE_GRAPH_ELEMENT;
  }

  public boolean isGraphPath() {
    return kind == TypeKind.TYPE_GRAPH_PATH;
  }

  public boolean isMap() {
    return kind == TypeKind.TYPE_MAP;
  }

  public boolean isUuid() {
    return kind == TypeKind.TYPE_UUID;
  }

  public boolean isMeasure() {
    return kind == TypeKind.TYPE_MEASURE;
  }

  public boolean isStructOrProto() {
    return isStruct() || isProto();
  }

  public boolean isFloatingPoint() {
    return isFloat() || isDouble();
  }

  public boolean isNumerical() {
    switch (kind) {
      case TYPE_INT32:
      case TYPE_INT64:
      case TYPE_UINT32:
      case TYPE_UINT64:
      case TYPE_FLOAT:
      case TYPE_DOUBLE:
      case TYPE_NUMERIC:
      case TYPE_BIGNUMERIC:
        return true;
      default:
        return false;
    }
  }

  public boolean isInteger() {
    switch (kind) {
      case TYPE_INT32:
      case TYPE_INT64:
      case TYPE_UINT32:
      case TYPE_UINT64:
        return true;
      default:
        return false;
    }
  }

  public boolean isSignedInteger() {
    return isInt32() || isInt64();
  }

  public boolean isUnsignedInteger() {
    return isUint32() || isUint64();
  }

  /**
   * Simple types are those that can be represented with just a TypeKind,
   * with no parameters.
   *
   * @return Whether this type is a simple type.
   */
  public boolean isSimpleType() {
    return TypeFactory.isSimpleType(kind);
  }

  /**
   * Serialize this type into self-contained protobuf.
   *
   * <p>Note, self-contained protos should never be used by the googlesql library itself (they break
   * local-service state management and proto equality).
   *
   * @return The serialized protobuf.
   */
  // TODO: Implement RANGE.
  public final TypeProto serialize() {
    TypeProto.Builder typeProtoBuilder = TypeProto.newBuilder();
    FileDescriptorSetsBuilder fileDescriptorSetsBuilder = new FileDescriptorSetsBuilder();
    serialize(typeProtoBuilder, fileDescriptorSetsBuilder);
    for (FileDescriptorSet set : fileDescriptorSetsBuilder.build()) {
      typeProtoBuilder.addFileDescriptorSet(set);
    }
    return typeProtoBuilder.build();
  }

  /**
   * Serialize the this type to a non-self-contained TypeProto and an array of FileDescriptorSet
   * using the given builders. To deserialize the Type, the FileDescriptorSet array must be provided
   * together with TypeProto.
   *
   * <p>To efficiently serialize multiple Types, one can use this method with the same
   * FileDescriptorSetsBuilder, the built List&lt;FileDescriptorSet&rt; will then include all
   * FileDescriptors that are required to deserialize all the Types. This effectively avoids
   * duplicates in multiple self-contained TypeProtos.
   */
  public final TypeProto serialize(FileDescriptorSetsBuilder fileDescriptorSetsBuilder) {
    TypeProto.Builder typeProtoBuilder = TypeProto.newBuilder();
    this.serialize(typeProtoBuilder, fileDescriptorSetsBuilder);
    return typeProtoBuilder.build();
  }

  /**
   * Serialize the this type to a non-self-contained TypeProto and an array of FileDescriptorSet
   * using the given builders. To deserialize the Type, the FileDescriptorSet array must be provided
   * together with TypeProto.
   *
   * <p>To efficiently serialize multiple Types, one can use this method with the same
   * FileDescriptorSetsBuilder, the built List&lt;FileDescriptorSet&rt; will then include all
   * FileDescriptors that are required to deserialize all the Types. This effectively avoids
   * duplicates in multiple self-contained TypeProtos.
   *
   */
  public abstract void serialize(
      TypeProto.Builder typeProtoBuilder, FileDescriptorSetsBuilder fileDescriptorSetsBuilder);

  /**
   * Compare types for equivalence.  Equivalent types can be used
   * interchangeably in a query, but casts will always be added to convert
   * from one to the other.
   *
   * <p> This differs from Equals in that it treats Enums and Protos as
   * equivalent if their full_name() is equal.  Different versions of the
   * same proto or enum are equivalent in a query, but CASTs will be added
   * to do the conversion.
   *
   * <p> Structs with different field names are not considered Equivalent.
   *
   * @param other
   * @return Whether this type is equivalent to the given other type.
   */
  public boolean equivalent(Type other) {
    return equalsInternal(other, /* equivalent= */ true);
  }

  /**
   * Compares types for equality. Equal types can be used interchangeably
   * without any casting.
   *
   * <p>This compares structurally inside structs and arrays.
   * For protos and enums, this does proto descriptor pointer comparison only.
   * Two versions of identical descriptors (from different DescriptorPools)
   * will not be considered equal.
   *
   * @param other
   * @return Whether this type is equal to the given other type.
   */
  @Override
  public boolean equals(Object other) {
    return (other instanceof Type) && equalsInternal((Type) other, /* equivalent= */ false);
  }

  @Override
  public abstract int hashCode();

  /**
   * Returns the SQL name for this type, which will be reparseable as part
   * of a query.  For proto-based types, this just returns the type name, which
   * does not easily distinguish PROTOs from ENUMs.
   *
   * @param productMode
   * @return The SQL name for this type.
   */
  public abstract String typeName(ProductMode productMode);

  public final String typeName() {
    return typeName(ProductMode.PRODUCT_INTERNAL);
  }

  /**
   * Returns the full description of the type.  For proto-based types, this will
   * return {@code PROTO<name>} or {@code ENUM<name>}, which are not valid to parse as SQL.
   * If {@code details} is true, then the description includes full proto descriptors.
   *
   * @param details
   * @return Full description of the type.
   */
  public abstract String debugString(boolean details);

  /**
   * Returns the full description of the type.  For proto-based types, this will
   * return {@code PROTO<name>} or {@code ENUM<name>}, which are not valid to parse as SQL.
   * Tht description does NOT include full proto descriptors.
   * @return Full description of the type.
   */
  public String debugString() {
    return debugString(false);
  }

  @Override
  public String toString() {
    return debugString(false);
  }

  /** Returns {@code this} cast to ArrayType or null for other types. */
  public ArrayType asArray() {
    return null;
  }

  /** Returns {@code this} cast to EnumType or null for other types. */
  public EnumType asEnum() {
    return null;
  }

  /** Returns {@code this} cast to ProtoType or null for other types. */
  public ProtoType asProto() {
    return null;
  }

  /** Returns {@code this} cast to StructType or null for other types. */
  public StructType asStruct() {
    return null;
  }

  /** Returns {@code this} cast to RangeType or null for other types. */
  public RangeType asRange() {
    return null;
  }

  /** Returns {@code this} cast to GraphElementType or null for other types. */
  public GraphElementType asGraphElement() {
    return null;
  }

  /** Returns {@code this} cast to GraphPathType or null for other types. */
  public GraphPathType asGraphPath() {
    return null;
  }

  /** Returns {@code this} cast to MapType or null for other types. */
  public MapType asMap() {
    return null;
  }

  /** Returns {@code this} cast to MeasureType or null for other types. */
  public MeasureType asMeasure() {
    return null;
  }

  @SuppressWarnings("ReferenceEquality")
  protected boolean equalsInternal(Type other, boolean equivalent) {
    if (other == this) {
      return true;
    }

    if (other == null) {
      return false;
    }

    if (kind != other.kind) {
      return false;
    }

    if (isSimpleType()) {
      return true;
    }

    switch (kind) {
      case TYPE_ENUM:
        return EnumType.equalsImpl(this.asEnum(), other.asEnum(), equivalent);
      case TYPE_ARRAY:
        return ArrayType.equalsImpl(this.asArray(), other.asArray(), equivalent);
      case TYPE_STRUCT:
        return StructType.equalsImpl(this.asStruct(), other.asStruct(), equivalent);
      case TYPE_PROTO:
        return ProtoType.equalsImpl(this.asProto(), other.asProto(), equivalent);
      case TYPE_RANGE:
        return RangeType.equalsImpl(this.asRange(), other.asRange(), equivalent);
      case TYPE_GRAPH_ELEMENT:
        return GraphElementType.equalsImpl(
            this.asGraphElement(), other.asGraphElement(), equivalent);
      case TYPE_GRAPH_PATH:
        return GraphPathType.equalsImpl(this.asGraphPath(), other.asGraphPath(), equivalent);
      case TYPE_MAP:
        return MapType.equalsImpl(this.asMap(), other.asMap(), equivalent);
      case TYPE_MEASURE:
        // Measure types cannot be used interchangeably, and so are not equal or equivalent.
        return false;
      default:
        throw new IllegalArgumentException("Shouldn't happen: unsupported type " + other);
    }
  }
}
