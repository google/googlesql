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
import static com.google.googlesql.CivilTimeEncoder.decodePacked64TimeNanos;
import static com.google.googlesql.CivilTimeEncoder.decodePacked64TimeNanosAsJavaTime;
import static com.google.googlesql.CivilTimeEncoder.decodePacked96DatetimeNanos;
import static com.google.googlesql.CivilTimeEncoder.decodePacked96DatetimeNanosAsJavaTime;
import static com.google.googlesql.CivilTimeEncoder.encodePacked64TimeNanos;
import static com.google.googlesql.CivilTimeEncoder.encodePacked96DatetimeNanos;

import com.google.common.base.Ascii;
import com.google.common.base.Joiner;
import com.google.common.base.Preconditions;
import com.google.common.base.Strings;
import com.google.common.collect.ImmutableList;
import com.google.common.collect.ImmutableMap;
import com.google.common.hash.HashCode;
import com.google.common.hash.Hashing;
import com.google.common.primitives.Bytes;
import com.google.errorprone.annotations.CanIgnoreReturnValue;
import com.google.errorprone.annotations.Immutable;
import com.google.protobuf.ByteString;
import com.google.protobuf.DynamicMessage;
import com.google.protobuf.InvalidProtocolBufferException;
import com.google.protobuf.Message;
import com.google.protobuf.TextFormat;
import com.google.protobuf.Timestamp;
import com.google.protobuf.util.Timestamps;
import com.google.googlesql.GoogleSQLType.TypeKind;
import com.google.googlesql.GoogleSQLValue.ValueProto;
import com.google.googlesql.GoogleSQLValue.ValueProto.Array;
import com.google.googlesql.GoogleSQLValue.ValueProto.Datetime;
import com.google.googlesql.GoogleSQLValue.ValueProto.Range;
import com.google.googlesql.GoogleSQLValue.ValueProto.Struct;
import com.google.googlesql.GoogleSQLValue.ValueProto.TimestampPicos;
import com.google.googlesql.GoogleSQLValue.ValueProto.ValueCase;
import java.io.ByteArrayOutputStream;
import java.io.IOException;
import java.io.Serializable;
import java.math.BigDecimal;
import java.math.BigInteger;
import java.nio.ByteBuffer;
import java.nio.ByteOrder;
import java.time.Instant;
import java.time.LocalDate;
import java.time.LocalDateTime;
import java.time.LocalTime;
import java.util.ArrayList;
import java.util.Arrays;
import java.util.Collection;
import java.util.Comparator;
import java.util.HashMap;
import java.util.List;
import java.util.Map;
import java.util.Map.Entry;
import java.util.Objects;
import java.util.UUID;

/**
 * Represents a value in the GoogleSQL type system. Each valid value has a type. A value object has
 * type-specific methods that are applicable only when it is of certain types.
 *
 * <p>Value is an immutable type. It contains a ValueProto that is passed or built at construction
 * time. The serialize() method is just an alias of getProto().
 *
 * <p>To construct a Value object, use one of the static create methods. Note the create methods
 * does sanity check on the arguments and throws unchecked exceptions.
 *
 * <p>The deserialize() static method creates a Value from a ValueProto. An IllegalArgumentException
 * will be thrown if there is an error in the proto. Note that the ValueProto only contains the
 * literal value, but not the type, which means passing a Value through RPC requires the Type being
 * passed along separately.
 *
 * <p>To get the literal value, use one of the type specific getters. A getter does the type checks
 * and other internal state checks and may throw unchecked exceptions.
 *
 * <p>Since old TIMESTAMP_{PRECISION} types are being deprecated, we do not support creating and
 * using Value objects of these types.
 *
 * <p>TODO: add public getters/setters for timestamps that have nanoseconds precision.
 */
@Immutable
public class Value implements Serializable {
  private final Type type;
  private final boolean isNull;
  private final ValueProto proto;
  // Fields of a struct Value.
  private final ImmutableList<Value> fields;
  // Elements of an array Value.
  private final ImmutableList<Value> elements;
  // Start element of a range Value.
  private final Value start;
  // End element of a range Value.
  private final Value end;
  // Map of entries for a map Value
  private final ImmutableMap<Value, Value> mapEntries;
  // Deserialized NUMERIC/BIGNUMERIC value if the type is TYPE_NUMERIC or TYPE_BIGNUMERIC.
  private final BigDecimal numericValue;
  // Deserialized INTERVAL value if the type is TYPE_INTERVAL.
  private final IntervalValue intervalValue;
  // Deserialized UUID value if the type is TYPE_UUID.
  private final UUID uuidValue;

  // Number of digits after the decimal point supported by the NUMERIC data type.
  private static final int NUMERIC_SCALE = 9;
  // Maximum and minimum allowed values for the NUMERIC data type.
  private static final BigDecimal MAX_NUMERIC_VALUE =
      new BigDecimal("99999999999999999999999999999.999999999");
  private static final BigDecimal MIN_NUMERIC_VALUE =
      new BigDecimal("-99999999999999999999999999999.999999999");

  // Number of digits after the decimal point supported by the BIGNUMERIC data type.
  private static final int BIGNUMERIC_SCALE = 38;
  // Maximum and minimum allowed values for the BIGNUMERIC data type.
  private static final BigDecimal MAX_BIGNUMERIC_VALUE =
      new BigDecimal(
          "578960446186580977117854925043439539266.34992332820282019728792003956564819967");
  private static final BigDecimal MIN_BIGNUMERIC_VALUE =
      new BigDecimal(
          "-578960446186580977117854925043439539266.34992332820282019728792003956564819968");

  private static final int UUID_VALUE_SIZE_IN_BYTES = 16;

  /** Creates an invalid Value */
  public Value() {
    this.type = new SimpleType(); // Invalid type
    this.proto = ValueProto.getDefaultInstance();
    this.isNull = true;
    this.fields = null;
    this.elements = null;
    this.mapEntries = null;
    this.numericValue = null;
    this.intervalValue = null;
    this.start = null;
    this.end = null;
    this.uuidValue = null;
  }

  /** Creates a Value of given type and proto value. */
  private Value(Type type, ValueProto proto) {
    this.type = checkNotNull(type);
    this.proto = checkNotNull(proto);
    this.isNull = Value.isNullValue(proto);
    this.fields = null;
    this.elements = null;
    this.mapEntries = null;
    this.numericValue = null;
    this.intervalValue = null;
    this.start = null;
    this.end = null;
    this.uuidValue = null;
  }

  /**
   * Creates an array of given type, proto and elements. Assumes the proto contains the serialized
   * elements.
   */
  private Value(ArrayType type, ValueProto proto, Collection<Value> elements) {
    this.type = checkNotNull(type);
    this.proto = checkNotNull(proto);
    this.isNull = Value.isNullValue(proto);
    this.fields = null;
    this.elements = ImmutableList.copyOf(elements);
    this.mapEntries = null;
    this.numericValue = null;
    this.intervalValue = null;
    this.start = null;
    this.end = null;
    this.uuidValue = null;
  }

  /**
   * Creates a struct of given type, proto and fields. Assuming the proto contains the serialized
   * fields.
   */
  private Value(StructType type, ValueProto proto, Collection<Value> fields) {
    this.type = checkNotNull(type);
    this.proto = checkNotNull(proto);
    this.isNull = Value.isNullValue(proto);
    this.fields = ImmutableList.copyOf(fields);
    this.elements = null;
    this.mapEntries = null;
    this.numericValue = null;
    this.intervalValue = null;
    this.start = null;
    this.end = null;
    this.uuidValue = null;
  }

  /**
   * Creates a range of a given type, proto and fields. Assuming the proto contains the serialized
   * start and end values.
   */
  private Value(RangeType type, ValueProto proto, Value start, Value end) {
    this.type = checkNotNull(type);
    this.proto = checkNotNull(proto);
    this.isNull = Value.isNullValue(proto);
    this.fields = null;
    this.elements = null;
    this.mapEntries = null;
    this.numericValue = null;
    this.intervalValue = null;
    this.start = start;
    this.end = end;
    this.uuidValue = null;
  }

  /**
   * Creates a map of a given type, proto and fields. Assuming the proto contains the serialized map
   * entries.
   */
  private Value(MapType type, ValueProto proto, Map<Value, Value> mapEntries) {
    this.type = checkNotNull(type);
    this.proto = checkNotNull(proto);
    this.isNull = Value.isNullValue(proto);
    this.mapEntries = ImmutableMap.copyOf(mapEntries);
    this.fields = null;
    this.elements = null;
    this.numericValue = null;
    this.intervalValue = null;
    this.start = null;
    this.end = null;
    this.uuidValue = null;
  }

  /** Creates a value of type NUMERIC or BIGNUMERIC. */
  private Value(TypeKind typeKind, ValueProto proto, BigDecimal numericValue) {
    this.type = TypeFactory.createSimpleType(typeKind);
    this.proto = checkNotNull(proto);
    this.isNull = Value.isNullValue(proto);
    this.fields = null;
    this.elements = null;
    this.mapEntries = null;
    this.numericValue = numericValue;
    this.intervalValue = null;
    this.start = null;
    this.end = null;
    this.uuidValue = null;
  }

  /** Creates a value of type INTERVAL. */
  private Value(TypeKind typeKind, ValueProto proto, IntervalValue intervalValue) {
    this.type = TypeFactory.createSimpleType(typeKind);
    this.proto = checkNotNull(proto);
    this.isNull = Value.isNullValue(proto);
    this.fields = null;
    this.elements = null;
    this.mapEntries = null;
    this.numericValue = null;
    this.intervalValue = intervalValue;
    this.start = null;
    this.end = null;
    this.uuidValue = null;
  }

  /** Creates a value of type UUID. */
  private Value(TypeKind typeKind, ValueProto proto, UUID uuidValue) {
    this.type = TypeFactory.createSimpleType(typeKind);
    this.proto = checkNotNull(proto);
    this.isNull = Value.isNullValue(proto);
    this.fields = null;
    this.elements = null;
    this.mapEntries = null;
    this.numericValue = null;
    this.intervalValue = null;
    this.start = null;
    this.end = null;
    this.uuidValue = uuidValue;
  }

  private static IllegalArgumentException typeMismatchException(Type type, ValueProto proto) {
    return new IllegalArgumentException(
        String.format(
            "Type mismatch: provided type %s but "
                + "proto <%s> doesn't have field of that type and is not null.",
            type, proto));
  }

  /** Returns true if the given {@code proto} represents a null value. */
  private static boolean isNullValue(ValueProto proto) {
    return proto.getValueCase() == ValueCase.VALUE_NOT_SET;
  }

  /** Returns the Java int value if the type is int32. */
  public int getInt32Value() {
    checkValueHasKind(TypeKind.TYPE_INT32);
    checkValueNotNull();
    return proto.getInt32Value();
  }

  /** Returns the Java long value if the type is int64. */
  public long getInt64Value() {
    checkValueHasKind(TypeKind.TYPE_INT64);
    checkValueNotNull();
    return proto.getInt64Value();
  }

  /** Returns Java int which equals to the uint32 value at binary level, if the type is uint32. */
  public int getUint32Value() {
    checkValueHasKind(TypeKind.TYPE_UINT32);
    checkValueNotNull();
    return proto.getUint32Value();
  }

  /** Returns Java long which equals to the uint64 value at binary level, if the type is uint64. */
  public long getUint64Value() {
    checkValueHasKind(TypeKind.TYPE_UINT64);
    checkValueNotNull();
    return proto.getUint64Value();
  }

  /** Returns the boolean value if the type is bool. */
  public boolean getBoolValue() {
    checkValueHasKind(TypeKind.TYPE_BOOL);
    checkValueNotNull();
    return proto.getBoolValue();
  }

  /** Returns the float value if the type is float. */
  public float getFloatValue() {
    checkValueHasKind(TypeKind.TYPE_FLOAT);
    checkValueNotNull();
    return proto.getFloatValue();
  }

  /** Returns the double value if the type is double. */
  public double getDoubleValue() {
    checkValueHasKind(TypeKind.TYPE_DOUBLE);
    checkValueNotNull();
    return proto.getDoubleValue();
  }

  /** Returns the numeric value if the type is NUMERIC. */
  public BigDecimal getNumericValue() {
    checkValueHasKind(TypeKind.TYPE_NUMERIC);
    checkValueNotNull();
    return numericValue;
  }

  /** Returns the numeric value if the type is BIGNUMERIC. */
  public BigDecimal getBigNumericValue() {
    checkValueHasKind(TypeKind.TYPE_BIGNUMERIC);
    checkValueNotNull();
    return numericValue;
  }

  /** Returns the interval value if the type is INTERVAL. */
  public IntervalValue getIntervalValue() {
    checkValueHasKind(TypeKind.TYPE_INTERVAL);
    checkValueNotNull();
    return intervalValue;
  }

  /** Returns the uuid value if the type is UUID. */
  public UUID getUuidValue() {
    checkValueHasKind(TypeKind.TYPE_UUID);
    checkValueNotNull();
    return uuidValue;
  }

  /** Returns the String value if the type is string. */
  @CanIgnoreReturnValue // TODO: consider removing this?
  public String getStringValue() {
    checkValueHasKind(TypeKind.TYPE_STRING);
    checkValueNotNull();
    return proto.getStringValue();
  }

  /** Returns the String value's backing bytes if the type is a string. */
  public ByteString getStringValueBytes() {
    checkValueHasKind(TypeKind.TYPE_STRING);
    checkValueNotNull();
    return proto.getStringValueBytes();
  }

  /** Returns the ByteString value if the type is bytes. */
  public ByteString getBytesValue() {
    checkValueHasKind(TypeKind.TYPE_BYTES);
    checkValueNotNull();
    return proto.getBytesValue();
  }

  /** Returns the int value representing the date if the type is date. */
  @SuppressWarnings("GoodTime") // should return a java.time.LocalDate (?)
  public int getDateValue() {
    checkValueHasKind(TypeKind.TYPE_DATE);
    checkValueNotNull();
    return proto.getDateValue();
  }

  /** Returns the int value representing the date if the type is date. */
  public LocalDate getLocalDateValue() {
    checkValueHasKind(TypeKind.TYPE_DATE);
    checkValueNotNull();
    return LocalDate.ofEpochDay(proto.getDateValue());
  }

  /** Returns the long value encoding the time if the type is time. */
  @SuppressWarnings("GoodTime") // should return a java.time.LocalTime (?)
  public long getTimeValue() {
    checkValueHasKind(TypeKind.TYPE_TIME);
    checkValueNotNull();
    return proto.getTimeValue();
  }

  /** Returns the long value encoding the time if the type is time. */
  public LocalTime getLocalTimeValue() {
    checkValueHasKind(TypeKind.TYPE_TIME);
    checkValueNotNull();
    return decodePacked64TimeNanosAsJavaTime(proto.getTimeValue());
  }

  /** Returns the Datetime value encoding the datetime if the type is datetime. */
  public Datetime getDatetimeValue() {
    checkValueHasKind(TypeKind.TYPE_DATETIME);
    checkValueNotNull();
    return proto.getDatetimeValue();
  }

  /** Returns the Datetime value encoding the datetime if the type is datetime. */
  public LocalDateTime getLocalDateTimeValue() {
    checkValueHasKind(TypeKind.TYPE_DATETIME);
    checkValueNotNull();
    return decodePacked96DatetimeNanosAsJavaTime(proto.getDatetimeValue());
  }

  /** Returns the number value if the type is enum. */
  public int getEnumValue() {
    checkValueHasKind(TypeKind.TYPE_ENUM);
    checkValueNotNull();
    return proto.getEnumValue();
  }

  /** Returns the enum name or the number as string for open enums. */
  public String enumDisplayName() {
    checkValueHasKind(TypeKind.TYPE_ENUM);
    checkValueNotNull();
    EnumType enumType = type.asEnum();
    int value = getEnumValue();
    String name = enumType.findName(value);
    if (name != null) {
      return name;
    }
    if (enumType.getDescriptor().isClosed()) {
      throw new AssertionError(String.format("Value %s not in %s", value, enumType));
    }
    return String.valueOf(value);
  }

  /** Returns the enum name string if the type is enum. */
  public String getEnumName() {
    checkValueHasKind(TypeKind.TYPE_ENUM);
    checkValueNotNull();
    EnumType enumType = type.asEnum();
    int value = getEnumValue();
    String name = enumType.findName(value);
    Preconditions.checkNotNull(name, "Value %s not in %s", value, enumType);
    return name;
  }

  /**
   * Returns the long value of the unix timestamp at microsecond precision, if the type is
   * timestamp.
   */
  @SuppressWarnings("GoodTime") // should return a java.time.Instant
  public long getTimestampUnixMicros() {
    checkValueHasKind(TypeKind.TYPE_TIMESTAMP);
    checkValueNotNull();
    return Timestamps.toMicros(getNanosecondsTimestampValue());
  }

  /** Returns the encoded bytes value of the proto if the type is proto. */
  public ByteString getProtoValue() {
    checkValueHasKind(TypeKind.TYPE_PROTO);
    checkValueNotNull();
    return proto.getProtoValue();
  }

  /** Returns the JSON value as a string JSON document. */
  public String getJsonValue() {
    checkValueHasKind(TypeKind.TYPE_JSON);
    checkValueNotNull();
    return proto.getJsonValue();
  }

  /** Returns the number of fields, if the type is struct. */
  public int getFieldCount() {
    checkValueHasKind(TypeKind.TYPE_STRUCT);
    return fields.size();
  }

  /** Returns the field at given index {@code i}, if the type is struct. */
  public Value getField(int i) {
    checkValueHasKind(TypeKind.TYPE_STRUCT);
    return fields.get(i);
  }

  /** Returns the list of field values, if the type is struct. */
  public ImmutableList<Value> getFieldList() {
    checkValueHasKind(TypeKind.TYPE_STRUCT);
    return fields;
  }

  /** Returns the first field with given {@code name} (case sensitive), if the type is struct. */
  public Value findFieldByName(String name) {
    checkValueHasKind(TypeKind.TYPE_STRUCT);
    StructType structType = type.asStruct();
    if (!Strings.isNullOrEmpty(name)) {
      for (int i = 0; i < structType.getFieldCount(); ++i) {
        if (structType.getField(i).getName().equals(name)) {
          return fields.get(i);
        }
      }
    }
    return null;
  }

  /** Returns true if the array has no elements. */
  public boolean isEmptyArray() {
    Preconditions.checkState(getType().getKind() == TypeKind.TYPE_ARRAY);
    return elements.isEmpty();
  }

  /** Returns the number of elements, if the type is array. */
  public int getElementCount() {
    Preconditions.checkState(getType().getKind() == TypeKind.TYPE_ARRAY);
    return elements.size();
  }

  /** Returns the element at given index {@code i}, if the type is array. */
  public Value getElement(int i) {
    Preconditions.checkState(getType().getKind() == TypeKind.TYPE_ARRAY);
    return elements.get(i);
  }

  /** Returns The list of elements, if the type is array. */
  public ImmutableList<Value> getElementList() {
    return elements;
  }

  /** Returns the range start element, if the type is range. */
  public Value start() {
    return start;
  }

  /** Returns the range end element, if the type is range. */
  public Value end() {
    return end;
  }

  /** Returns the map of entries, if the type is map */
  public ImmutableMap<Value, Value> getMapEntries() {
    Preconditions.checkState(getType().getKind() == TypeKind.TYPE_MAP);
    return mapEntries;
  }

  /** Returns the number of map entries, if the type is map. */
  public int getMapEntriesCount() {
    Preconditions.checkState(getType().getKind() == TypeKind.TYPE_MAP);
    return mapEntries.size();
  }

  /**
   * Returns false if 'this' and 'other' have different type kinds.
   *
   * <p>Returns true if 'this' equals 'other' or both are null. This is *not* SQL equality which
   * returns null when either value is null.
   *
   * <p>For floating point values, returns 'true' if both values are NaN of the same type.
   *
   * <p>For protos, returns true if the proto definitions are equivalent and the serialized data are
   * binary equal.
   */
  @Override
  public boolean equals(Object other) {
    if (other == this) {
      return true;
    }

    if (other == null) {
      return false;
    }

    return (other instanceof Value) && equalsImpl((Value) other);
  }

  private boolean equalsImpl(Value other) {
    if (other.getType().getKind() != type.getKind()) {
      return false;
    }

    if (isNull != other.isNull) {
      return false;
    }

    if (isNull) {
      return other.isNull;
    }

    switch (type.getKind()) {
      case TYPE_INT32:
        return other.getInt32Value() == getInt32Value();
      case TYPE_INT64:
        return other.getInt64Value() == getInt64Value();
      case TYPE_UINT32:
        return other.getUint32Value() == getUint32Value();
      case TYPE_UINT64:
        return other.getUint64Value() == getUint64Value();
      case TYPE_BOOL:
        return other.getBoolValue() == getBoolValue();
      case TYPE_FLOAT:
        {
          float v1 = getFloatValue();
          float v2 = other.getFloatValue();
          if (v1 == v2) {
            return true;
          }
          return Float.isNaN(v1) && Float.isNaN(v2);
        }
      case TYPE_DOUBLE:
        {
          double v1 = getDoubleValue();
          double v2 = other.getDoubleValue();
          if (v1 == v2) {
            return true;
          }
          return Double.isNaN(v1) && Double.isNaN(v2);
        }
      case TYPE_STRING:
        return other.getStringValue().equals(getStringValue());
      case TYPE_BYTES:
        return other.getBytesValue().equals(getBytesValue());
      case TYPE_DATE:
        return other.getDateValue() == getDateValue();
      case TYPE_TIMESTAMP:
        return Timestamps.compare(
                other.getNanosecondsTimestampValue(), getNanosecondsTimestampValue())
            == 0;
      case TYPE_TIME:
        return other.getTimeValue() == getTimeValue();
      case TYPE_DATETIME:
        {
          return (other.getDatetimeValue().getBitFieldDatetimeSeconds()
                  == getDatetimeValue().getBitFieldDatetimeSeconds())
              && (other.getDatetimeValue().getNanos() == getDatetimeValue().getNanos());
        }
      case TYPE_ENUM:
        return other.getType().equivalent(type) && other.getEnumValue() == getEnumValue();
      case TYPE_ARRAY:
        if (other.getType().equivalent(type) && other.getElementCount() == getElementCount()) {
          for (int i = 0; i < getElementCount(); ++i) {
            if (!other.getElement(i).equals(getElement(i))) {
              return false;
            }
          }
          return true;
        }
        return false;
      case TYPE_STRUCT:
        if (other.getFieldCount() == getFieldCount()) {
          for (int i = 0; i < getFieldCount(); ++i) {
            if (!other.getField(i).equals(getField(i))) {
              return false;
            }
          }
          return true;
        }
        return false;
      case TYPE_PROTO:
        if (other.getType().equivalent(type)) {
          return other.getProtoValue().equals(getProtoValue());
        }
        // TODO: The C++ version will return true if the parsed
        // messages are equal, but that breaks the contract that hashCode()
        // should be the same for objects that are equal(). We will leave
        // this as binary equality until the C++ version figures out how to
        // do hashCode() correctly.
        return false;
      case TYPE_NUMERIC:
      case TYPE_BIGNUMERIC:
        if (other.getType().equivalent(type)) {
          return other.numericValue.compareTo(numericValue) == 0;
        }
        return false;
      case TYPE_INTERVAL:
        if (other.getType().equivalent(type)) {
          return other.intervalValue.equals(intervalValue);
        }
        return false;
      case TYPE_JSON:
        if (other.getType().equivalent(type)) {
          return other.getJsonValue().equals(getJsonValue());
        }
        return false;
      case TYPE_RANGE:
        if (other.getType().equivalent(type)) {
          return other.start().equals(start()) && other.end().equals(end());
        }
        return false;
      case TYPE_UUID:
        if (other.getType().equivalent(type)) {
          return other.getUuidValue().compareTo(getUuidValue()) == 0;
        }
        return false;
      case TYPE_MAP:
        if (other.getType().equivalent(type)) {
          return other.getMapEntries().equals(getMapEntries());
        }
        return false;
      default:
        throw new IllegalStateException("Shouldn't happen: compare with unsupported type " + type);
    }
  }

  @Override
  @SuppressWarnings("SuperCallToObjectMethod") // For fall through to super.hashCode().
  public int hashCode() {
    if (isNull) {
      return ~type.hashCode();
    }

    switch (type.getKind()) {
      case TYPE_INT32:
        return HashCode.fromInt(getInt32Value()).asInt();
      case TYPE_INT64:
        return HashCode.fromLong(getInt64Value()).asInt();
      case TYPE_UINT32:
        return HashCode.fromInt(getUint32Value()).asInt();
      case TYPE_UINT64:
        return HashCode.fromLong(getUint64Value()).asInt();
      case TYPE_BOOL:
        return Boolean.valueOf(getBoolValue()).hashCode();
      case TYPE_FLOAT:
        return Float.valueOf(getFloatValue()).hashCode();
      case TYPE_DOUBLE:
        return Double.valueOf(getDoubleValue()).hashCode();
      case TYPE_STRING:
        return getStringValue().hashCode();
      case TYPE_BYTES:
        return getBytesValue().hashCode();
      case TYPE_PROTO:
        return getProtoValue().hashCode();
      case TYPE_DATE:
        return HashCode.fromInt(getDateValue()).asInt();
      case TYPE_TIMESTAMP:
        return HashCode.fromLong(getTimestampUnixMicros()).asInt();
      case TYPE_TIME:
        return HashCode.fromLong(getTimeValue()).asInt();
      case TYPE_DATETIME:
        {
          Datetime datetime = getDatetimeValue();
          return Objects.hash(datetime.getBitFieldDatetimeSeconds(), datetime.getNanos());
        }
      case TYPE_ENUM:
        return HashCode.fromInt(getEnumValue()).asInt();
      case TYPE_ARRAY:
        {
          int elementCount = getElementCount();
          if (elementCount == 0) {
            return type.hashCode() * 571;
          }
          List<HashCode> hashCodes = new ArrayList<>();
          for (int i = 0; i < elementCount; ++i) {
            hashCodes.add(HashCode.fromInt(getElement(i).hashCode()));
          }
          return Hashing.combineOrdered(hashCodes).asInt();
        }
      case TYPE_STRUCT:
        {
          int fieldCount = getFieldCount();
          if (fieldCount == 0) {
            return type.hashCode() * 617;
          }
          List<HashCode> hashCodes = new ArrayList<>();
          for (int i = 0; i < fieldCount; ++i) {
            hashCodes.add(HashCode.fromInt(getField(i).hashCode()));
          }
          return Hashing.combineOrdered(hashCodes).asInt();
        }
      case TYPE_NUMERIC:
      case TYPE_BIGNUMERIC:
        return numericValue.toBigInteger().hashCode();
      case TYPE_INTERVAL:
        return intervalValue.hashCode();
      case TYPE_JSON:
        return getJsonValue().hashCode();
      case TYPE_RANGE:
        {
          ImmutableList<HashCode> hashCodes =
              ImmutableList.of(
                  HashCode.fromInt(start().hashCode()), HashCode.fromInt(end().hashCode()));
          return Hashing.combineOrdered(hashCodes).asInt();
        }
      case TYPE_UUID:
        return uuidValue.hashCode();
      case TYPE_MAP:
        {
          if (getMapEntriesCount() == 0) {
            return type.hashCode() * 659;
          }
          List<HashCode> hashCodes = new ArrayList<>();
          for (Entry<Value, Value> entry : getMapEntries().entrySet()) {
            hashCodes.add(
                // Use combineOrdered on each entry since "a" => "b" not equivalent to "b" => "a".
                Hashing.combineOrdered(
                    ImmutableList.of(
                        HashCode.fromInt(entry.getKey().hashCode()),
                        HashCode.fromInt(entry.getValue().hashCode()))));
          }
          // Use combineUnordered to combine the entries, since MAP is unordered.
          return Hashing.combineUnordered(hashCodes).asInt();
        }
      default:
        // Shouldn't happen, but it's a bad idea to throw from hashCode().
        return super.hashCode();
    }
  }

  /** Returns printable string for this Value NOT including type name. */
  @Override
  public String toString() {
    return debugString(false);
  }

  /** Returns printable string for this Value NOT including type name. */
  public String debugString() {
    return debugString(false);
  }

  /**
   * Returns printable string for this Value, including the type name if {@code verbose} is true.
   */
  public String debugString(boolean verbose) {
    if (type.getKind() == TypeKind.__TypeKind__switch_must_have_a_default__) {
      return "Uninitialized value";
    }
    if (!isValid()) {
      return String.format("Invalid value, typeKind: %s", type.getKind());
    }
    switch (type.getKind()) {
      case TYPE_INT32:
      case TYPE_INT64:
      case TYPE_UINT32:
      case TYPE_UINT64:
      case TYPE_BOOL:
      case TYPE_FLOAT:
      case TYPE_DOUBLE:
      case TYPE_STRING:
      case TYPE_BYTES:
      case TYPE_DATE:
      case TYPE_TIMESTAMP:
      case TYPE_TIME:
      case TYPE_DATETIME:
      case TYPE_NUMERIC:
      case TYPE_BIGNUMERIC:
      case TYPE_JSON:
      case TYPE_INTERVAL:
      case TYPE_UUID:
        return GoogleSQLStrings.convertSimpleValueToString(this, verbose);
      case TYPE_ENUM:
        {
          if (verbose) {
            String typeName =
                String.format("Enum<%s>", type.asEnum().getDescriptor().getFullName());
            String s =
                isNull() ? "NULL" : String.format("%s:%d", enumDisplayName(), getEnumValue());
            return String.format("%s(%s)", typeName, s);
          } else {
            return isNull() ? "NULL" : enumDisplayName();
          }
        }
      case TYPE_ARRAY:
        {
          if (isNull()) {
            return verbose
                ? String.format(
                    "Array<%s>(NULL)", getType().asArray().getElementType().debugString(false))
                : "NULL";
          }
          StringBuilder result = new StringBuilder();
          for (Value v : getElementList()) {
            result.append(result.length() == 0 ? "" : ", ").append(v.debugString(verbose));
          }
          return String.format("%s%s]", verbose ? "Array[" : "[", result);
        }
      case TYPE_STRUCT:
        {
          String name = verbose ? "Struct" : "";
          if (isNull()) {
            return verbose ? name + "(NULL)" : "NULL";
          }
          List<String> fieldStr = new ArrayList<>();
          StructType structType = getType().asStruct();
          for (int i = 0; i < structType.getFieldCount(); i++) {
            final String fieldValue = getField(i).debugString(verbose);
            if (structType.getField(i).getName().isEmpty()) {
              fieldStr.add(fieldValue);
            } else {
              fieldStr.add(String.format("%s:%s", structType.getField(i).getName(), fieldValue));
            }
          }
          return String.format("%s{%s}", name, Joiner.on(", ").join(fieldStr));
        }
      case TYPE_PROTO:
        {
          Preconditions.checkNotNull(type.asProto().getDescriptor());
          String name =
              String.format("Proto<%s>", getType().asProto().getDescriptor().getFullName());
          if (isNull()) {
            return verbose ? name + "(NULL)" : "NULL";
          }
          try {
            return verbose
                ? String.format("%s{%s}", name, toMessage())
                : String.format("{%s}", TextFormat.shortDebugString(toMessage()));
          } catch (InvalidProtocolBufferException e) {
            return (verbose ? name : "") + "{<unparseable>}";
          }
        }
      case TYPE_RANGE:
        {
          if (isNull()) {
            return verbose
                ? String.format(
                    "Range<%s>(NULL)", getType().asRange().getElementType().debugString(false))
                : "NULL";
          }
          String result = start().debugString(verbose) + ", " + end().debugString(verbose);
          return String.format("%s%s)", verbose ? "Range[" : "[", result);
        }
      case TYPE_GRAPH_ELEMENT:
        {
          String type =
              "GraphElement"
                  + (verbose
                      ? String.format("<%s>", getType().asGraphElement().debugString(false))
                      : "");
          if (isNull()) {
            return verbose ? String.format("%s(NULL)", type) : "NULL";
          }
          return String.format("%s(...)", type);
        }
      case TYPE_GRAPH_PATH:
        {
          String type =
              "GraphPath"
                  + (verbose
                      ? String.format(
                          "<node: %s, edge: %s>",
                          getType().asGraphPath().getNodeType().debugString(false),
                          getType().asGraphPath().getEdgeType().debugString(false))
                      : "");
          if (isNull()) {
            return verbose ? String.format("%s(NULL)", type) : "NULL";
          }
          return String.format("%s(...)", type);
        }
      case TYPE_MAP:
        {
          if (isNull()) {
            MapType mapType = getType().asMap();
            return verbose
                ? String.format(
                    "Map<%s, %s>(NULL)",
                    mapType.getKeyType().debugString(false),
                    mapType.getValueType().debugString(false))
                : "NULL";
          }
          // Ensure that debug strings will be printed in a deterministic way
          List<Entry<Value, Value>> entriesOrdered = new ArrayList<>(getMapEntries().entrySet());
          entriesOrdered.sort(comparingMapEntryByKey());

          StringBuilder result = new StringBuilder();
          entriesOrdered.forEach(
              entry ->
                  result
                      .append(result.length() == 0 ? "" : ", ")
                      .append(entry.getKey().debugString(verbose))
                      .append(": ")
                      .append(entry.getValue().debugString(verbose)));
          return String.format("%s%s}", verbose ? "Map{" : "{", result);
        }

      default:
        throw new IllegalStateException(
            "Unexpected type kind expected internally only: " + getType().getKind());
    }
  }

  public String shortDebugString() {
    return debugString(false);
  }

  public String fullDebugString() {
    return debugString(true);
  }

  /**
   * Returns the numeric value of bool, int, uint32, date, enum types, coerced to int64. REQUIRES:
   * !isNull().
   */
  public long toInt64() {
    checkValueNotNull();
    switch (type.getKind()) {
      case TYPE_INT64:
        return getInt64Value();
      case TYPE_INT32:
        return getInt32Value();
      case TYPE_UINT32:
        return getUint32Value();
      case TYPE_BOOL:
        return getBoolValue() ? 1 : 0;
      case TYPE_DATE:
        return getDateValue();
      case TYPE_TIME:
        return getTimeValue();
      case TYPE_ENUM:
        return getEnumValue();
      default:
        throw new IllegalStateException("Cannot coerce " + getType().getKind() + " to int64");
    }
  }

  /** Returns the numeric value of bool and uint types, coerced to uint64. REQUIRES: !isNull(). */
  public long toUint64() {
    checkValueNotNull();
    switch (type.getKind()) {
      case TYPE_UINT64:
        return getUint64Value();
      case TYPE_UINT32:
        return getUint32Value();
      case TYPE_BOOL:
        return getBoolValue() ? 1 : 0;
      default:
        throw new IllegalStateException("Cannot coerce " + getType().getKind() + " to uint64");
    }
  }

  /**
   * Returns the numeric value of bool, int, date, enum types, coerced to double. REQUIRES:
   * !isNull().
   */
  public double toDouble() {
    checkValueNotNull();
    switch (type.getKind()) {
      case TYPE_INT64:
        return getInt64Value();
      case TYPE_UINT64:
        return getUint64Value();
      case TYPE_INT32:
        return getInt32Value();
      case TYPE_UINT32:
        return getUint32Value();
      case TYPE_BOOL:
        return getBoolValue() ? 1 : 0;
      case TYPE_DATE:
        return getDateValue();
      case TYPE_DOUBLE:
        return getDoubleValue();
      case TYPE_FLOAT:
        return getFloatValue();
      case TYPE_ENUM:
        return getEnumValue();
      case TYPE_NUMERIC:
      case TYPE_BIGNUMERIC:
        return numericValue.doubleValue();
      default:
        throw new IllegalStateException("Cannot coerce " + getType().getKind() + " to double");
    }
  }

  /**
   * Convert this value to a dynamically allocated proto Message. REQUIRES: !isNull() &&
   * type.isProto()
   *
   * @return The dynamic proto Message.
   * @throws InvalidProtocolBufferException if the value is not parseable.
   */
  public Message toMessage() throws InvalidProtocolBufferException {
    Preconditions.checkState(type.isProto(), "Value is not a proto");
    checkValueNotNull();
    DynamicMessage m = DynamicMessage.getDefaultInstance(getType().asProto().getDescriptor());
    return m.getParserForType().parsePartialFrom(getProtoValue());
  }

  /**
   * Returns a SQL expression that produces this value. This is not necessarily a literal since we
   * don't have literal syntax for all values.
   */
  public String getSQL() {
    if (isNull) {
      return String.format("CAST(NULL AS %s)", type.typeName());
    }

    String s = shortDebugString();

    if (type.isDate()
        || type.isTime()
        || type.isDatetime()) {
      // Use literal syntax for DATE, DATETIME, TIME and TIMESTAMP.
      return String.format("%s %s", type.typeName(), GoogleSQLStrings.toStringLiteral(s));
    }
    if (type.isNumeric() || type.isBigNumeric() || type.isUuid()) {
      return getSQLLiteral();
    }
    if (type.isGeography()) {
      String wktString = GoogleSQLStrings.convertSimpleValueToString(this, /* verbose= */ false);
      return String.format("ST_GeogFromText(%s)", GoogleSQLStrings.toStringLiteral(wktString));
    }
    if (type.isJson()) {
      return String.format("JSON %s", GoogleSQLStrings.toStringLiteral(s));
    }

    if (type.isSimpleType()) {
      // Floats and doubles like "inf" and "nan" need to be quoted.
      if (type.isFloat() && (Float.isInfinite(getFloatValue()) || Float.isNaN(getFloatValue()))) {
        return String.format("CAST(%s AS FLOAT)", GoogleSQLStrings.toStringLiteral(s));
      }
      if (type.isDouble()) {
        if (Double.isInfinite(getDoubleValue()) || Double.isNaN(getDoubleValue())) {
          return String.format("CAST(%s AS DOUBLE)", GoogleSQLStrings.toStringLiteral(s));
        } else {
          // Make sure that doubles always print with a . or an 'e' so they
          // don't look like integers.
          if (s.matches("[-0123456789]+")) {
            s = s.concat(".0");
          }
          return s;
        }
      }

      // We need a cast for all numeric types except int64 and double.
      if (type.isNumerical() && !type.isInt64()) {
        return String.format("CAST(%s AS %s)", s, type.typeName());
      } else {
        return s;
      }
    }

    if (type.isEnum()) {
      return String.format("CAST(%s AS %s)", GoogleSQLStrings.toStringLiteral(s), type.typeName());
    }
    if (type.isProto()) {
      return String.format(
          "CAST(%s AS %s)",
          GoogleSQLStrings.toBytesLiteral(getProtoValue().toByteArray()), type.typeName());
    }
    if (type.isStruct()) {
      List<String> fieldsSql = new ArrayList<String>();
      for (Value fieldValue : getFieldList()) {
        fieldsSql.add(fieldValue.getSQL());
      }
      return String.format("%s(%s)", type.typeName(), Joiner.on(", ").join(fieldsSql));
    }
    if (type.isArray()) {
      List<String> elementsSql = new ArrayList<String>();
      for (Value element : getElementList()) {
        elementsSql.add(element.getSQL());
      }
      return String.format("%s[%s]", type.typeName(), Joiner.on(", ").join(elementsSql));
    }
    if (type.isRange()) {
      return String.format("RANGE(%s, %s)", start().getSQL(), end().getSQL());
    }
    if (type.isMap()) {
      ImmutableList<String> elementsSql =
          getMapEntries().entrySet().stream()
              .sorted(comparingMapEntryByKey())
              .map(
                  entry ->
                      String.format("(%s, %s)", entry.getKey().getSQL(), entry.getValue().getSQL()))
              .collect(ImmutableList.toImmutableList());
      return String.format("MAP_FROM_ARRAY([%s])", Joiner.on(", ").join(elementsSql));
    }

    return s;
  }

  /**
   * Returns a SQL expression that is compatible as a literal for this value. This won't include
   * CASTs and won't necessarily produce the exact same type when parsed on its own, but it should
   * be the closest SQL literal form for this value.
   */
  public String getSQLLiteral() {
    if (isNull) {
      return "NULL";
    }

    String s = shortDebugString();
    if (type.isDate()
        || type.isTime()
        || type.isDatetime()) {
      // Use literal syntax for DATE, DATETIME, TIME and TIMESTAMP.
      return String.format("%s %s", type.typeName(), GoogleSQLStrings.toStringLiteral(s));
    }
    if (type.isGeography()) {
      String wktString = GoogleSQLStrings.convertSimpleValueToString(this, /* verbose= */ false);
      return String.format("ST_GeogFromText(%s)", GoogleSQLStrings.toStringLiteral(wktString));
    }
    if (type.isNumeric()) {
      return String.format("NUMERIC %s", GoogleSQLStrings.toStringLiteral(s));
    }
    if (type.isBigNumeric()) {
      return String.format("BIGNUMERIC %s", GoogleSQLStrings.toStringLiteral(s));
    }
    if (type.isJson()) {
      return String.format("JSON %s", GoogleSQLStrings.toStringLiteral(s));
    }
    if (type.isUuid()) {
      return String.format("CAST(%s AS UUID)", GoogleSQLStrings.toSingleQuotedStringLiteral(s));
    }

    if (type.isSimpleType()) {
      // Floats and doubles like "inf" and "nan" need to be quoted.
      if (type.isFloat() && (Float.isInfinite(getFloatValue()) || Float.isNaN(getFloatValue()))) {
        return String.format("CAST(%s AS FLOAT)", GoogleSQLStrings.toStringLiteral(s));
      }
      if (type.isDouble()
          && (Double.isInfinite(getDoubleValue()) || Double.isNaN(getDoubleValue()))) {
        return String.format("CAST(%s AS DOUBLE)", GoogleSQLStrings.toStringLiteral(s));
      }

      if (type.isDouble() || type.isFloat()) {
        // Make sure that doubles always print with a . or an 'e' so they
        // don't look like integers.
        if (s.matches("[-0123456789]+")) {
          s = s.concat(".0");
        }
        return s;
      }
    }
    if (type.isEnum()) {
      return GoogleSQLStrings.toStringLiteral(s);
    }
    if (type.isProto()) {
      String ascii;

      try {
        ascii = TextFormat.printer().printToString(toMessage());
      } catch (InvalidProtocolBufferException e) {
        ascii = "<unparseable>";
      }
      return GoogleSQLStrings.toStringLiteral(ascii);
    }
    if (type.isStruct()) {
      if (type.asStruct().getFieldCount() == 0) {
        return "STRUCT()";
      }
      List<String> fieldsSql = new ArrayList<String>();
      for (Value fieldValue : getFieldList()) {
        fieldsSql.add(fieldValue.getSQLLiteral());
      }
      return String.format(
          "%s(%s)",
          (type.asStruct().getFieldCount() == 1 ? "STRUCT" : ""), Joiner.on(", ").join(fieldsSql));
    }
    if (type.isArray()) {
      List<String> elementsSql = new ArrayList<String>();
      for (Value element : getElementList()) {
        elementsSql.add(element.getSQLLiteral());
      }
      return String.format("[%s]", Joiner.on(", ").join(elementsSql));
    }
    if (type.isRange()) {
      return String.format(
          "%s \"[%s, %s)\"",
          type.typeName(),
          GoogleSQLStrings.convertSimpleValueToString(start(), /* verbose= */ false),
          GoogleSQLStrings.convertSimpleValueToString(end(), /* verbose= */ false));
    }
    if (type.isMap()) {
      ImmutableList<String> elementsSql =
          getMapEntries().entrySet().stream()
              .sorted(comparingMapEntryByKey())
              .map(
                  entry ->
                      String.format(
                          "(%s, %s)",
                          entry.getKey().getSQLLiteral(), entry.getValue().getSQLLiteral()))
              .collect(ImmutableList.toImmutableList());
      return String.format("MAP_FROM_ARRAY([%s])", Joiner.on(", ").join(elementsSql));
    }

    return s;
  }

  /** Returns the type of this value. */
  public Type getType() {
    return type;
  }

  /** Returns whether the value is null. */
  public boolean isNull() {
    return isNull;
  }

  /** Returns whether the type is valid and with necessary value. */
  public boolean isValid() {
    Preconditions.checkState(
        TypeKind.TYPE_UNKNOWN.getNumber() == 0
            && TypeKind.__TypeKind__switch_must_have_a_default__.getNumber() == -1);
    return type.getKind().getNumber() > 0;
  }

  /** Returns the ValueProto this value encodes to. */
  public ValueProto getProto() {
    return proto;
  }

  /** Returns the ValueProto this value encodes to. */
  public ValueProto serialize() {
    return proto;
  }

  private static BigDecimal deserializeBigDecimal(
      ByteString serializedValue,
      int scale,
      BigDecimal maxValue,
      BigDecimal minValue,
      String typeName) {
    byte[] bytes = serializedValue.toByteArray();
    // NUMERIC/BIGNUMERIC values are serialized as scaled integers in two's complement form in
    // little endian order. BigInteger requires the same encoding but in big endian order,
    // therefore we must reverse the bytes that come from the proto.
    Bytes.reverse(bytes);
    BigInteger scaledValue = new BigInteger(bytes);
    BigDecimal decimalValue = new BigDecimal(scaledValue, scale);
    if (decimalValue.compareTo(maxValue) > 0 || decimalValue.compareTo(minValue) < 0) {
      throw new IllegalArgumentException(typeName + " overflow: " + decimalValue.toPlainString());
    }
    return decimalValue;
  }

  private static UUID deserializeUuid(ByteString serializedValue) {
    byte[] bytes = serializedValue.toByteArray();
    if (bytes.length != UUID_VALUE_SIZE_IN_BYTES) {
      throw new IllegalArgumentException(
          String.format(
              "Unexpected UUID value length in bytes: %s. The value is %s.",
              bytes.length, serializedValue));
    }

    long msb = ByteBuffer.wrap(bytes, 0, 8).order(ByteOrder.BIG_ENDIAN).getLong();
    long lsb = ByteBuffer.wrap(bytes, 8, 8).order(ByteOrder.BIG_ENDIAN).getLong();
    return new UUID(msb, lsb);
  }

  @CanIgnoreReturnValue // TODO: consider removing this?
  public static Value deserialize(Type type, ValueProto proto) {
    Preconditions.checkNotNull(type);
    Preconditions.checkNotNull(proto);
    Preconditions.checkArgument(isSupportedTypeKind(type), "Type not supported %s", type);

    if (Value.isNullValue(proto)) {
      return new Value(type, proto);
    }

    switch (type.getKind()) {
      case TYPE_INT32:
        if (!proto.hasInt32Value()) {
          throw typeMismatchException(type, proto);
        }
        break;
      case TYPE_INT64:
        if (!proto.hasInt64Value()) {
          throw typeMismatchException(type, proto);
        }
        break;
      case TYPE_UINT32:
        if (!proto.hasUint32Value()) {
          throw typeMismatchException(type, proto);
        }
        break;
      case TYPE_UINT64:
        if (!proto.hasUint64Value()) {
          throw typeMismatchException(type, proto);
        }
        break;
      case TYPE_BOOL:
        if (!proto.hasBoolValue()) {
          throw typeMismatchException(type, proto);
        }
        break;
      case TYPE_FLOAT:
        if (!proto.hasFloatValue()) {
          throw typeMismatchException(type, proto);
        }
        break;
      case TYPE_DOUBLE:
        if (!proto.hasDoubleValue()) {
          throw typeMismatchException(type, proto);
        }
        break;
      case TYPE_STRING:
        if (!proto.hasStringValue()) {
          throw typeMismatchException(type, proto);
        }
        break;
      case TYPE_BYTES:
        if (!proto.hasBytesValue()) {
          throw typeMismatchException(type, proto);
        }
        break;
      case TYPE_DATE:
        {
          if (!proto.hasDateValue()) {
            throw typeMismatchException(type, proto);
          }
          int date = proto.getDateValue();
          if (!Type.isValidDate(proto.getDateValue())) {
            throw new IllegalArgumentException("Invalid value for DATE: " + date);
          }
          break;
        }
      case TYPE_TIMESTAMP:
        {
          if (!proto.hasTimestampValue() && !proto.hasTimestampPicosValue()) {
            throw typeMismatchException(type, proto);
          }
          if (proto.hasTimestampValue() && !Type.isValidTimestamp(proto.getTimestampValue())) {
            throw new IllegalArgumentException(
                "Invalid value for TIMESTAMP: " + proto.getTimestampValue());
          } else if (proto.hasTimestampPicosValue()
              && !Type.isValidTimestamp(proto.getTimestampPicosValue())) {
            throw new IllegalArgumentException(
                "Invalid value for TIMESTAMP: " + proto.getTimestampPicosValue());
          }
          break;
        }
      case TYPE_TIME:
        {
          if (!proto.hasTimeValue()) {
            throw typeMismatchException(type, proto);
          }
          decodePacked64TimeNanos(proto.getTimeValue());
          break;
        }
      case TYPE_DATETIME:
        {
          if (!proto.hasDatetimeValue()) {
            throw typeMismatchException(type, proto);
          }
          decodePacked96DatetimeNanos(proto.getDatetimeValue());
          break;
        }
      case TYPE_NUMERIC:
        if (!proto.hasNumericValue()) {
          throw typeMismatchException(type, proto);
        }
        return new Value(
            TypeKind.TYPE_NUMERIC,
            proto,
            deserializeBigDecimal(
                proto.getNumericValue(),
                NUMERIC_SCALE,
                MAX_NUMERIC_VALUE,
                MIN_NUMERIC_VALUE,
                "Numeric"));
      case TYPE_BIGNUMERIC:
        if (!proto.hasBignumericValue()) {
          throw typeMismatchException(type, proto);
        }
        return new Value(
            TypeKind.TYPE_BIGNUMERIC,
            proto,
            deserializeBigDecimal(
                proto.getBignumericValue(),
                BIGNUMERIC_SCALE,
                MAX_BIGNUMERIC_VALUE,
                MIN_BIGNUMERIC_VALUE,
                "BIGNUMERIC"));
      case TYPE_INTERVAL:
        if (!proto.hasIntervalValue()) {
          throw typeMismatchException(type, proto);
        }
        return new Value(
            TypeKind.TYPE_INTERVAL,
            proto,
            IntervalValue.deserializeInterval(proto.getIntervalValue()));
      case TYPE_JSON:
        if (!proto.hasJsonValue()) {
          throw typeMismatchException(type, proto);
        }
        break;
      case TYPE_ENUM:
        {
          if (!proto.hasEnumValue()) {
            throw typeMismatchException(type, proto);
          }
          EnumType enumType = type.asEnum();
          int n = proto.getEnumValue();
          if (enumType.getDescriptor().isClosed() && enumType.findName(n) == null) {
            throw new IllegalArgumentException("Invalid value for " + enumType + ": " + n);
          }
          break;
        }
      case TYPE_ARRAY:
        {
          if (!proto.hasArrayValue()) {
            throw typeMismatchException(type, proto);
          }
          Type elementType = type.asArray().getElementType();
          List<Value> elements = new ArrayList<>();
          for (ValueProto element : proto.getArrayValue().getElementList()) {
            elements.add(deserialize(elementType, element));
          }
          return new Value(type.asArray(), proto, elements);
        }
      case TYPE_STRUCT:
        {
          if (!proto.hasStructValue()) {
            throw typeMismatchException(type, proto);
          }
          StructType structType = type.asStruct();
          Struct structValue = proto.getStructValue();
          if (structType.getFieldCount() != structValue.getFieldCount()) {
            throw new IllegalArgumentException(
                "Type mismatch for struct. Type has "
                    + structType.getFieldCount()
                    + " fields, but proto has "
                    + structValue.getFieldCount()
                    + " fields.");
          }
          List<Value> fields = new ArrayList<>();
          for (int i = 0; i < structType.getFieldCount(); ++i) {
            fields.add(deserialize(structType.getField(i).getType(), structValue.getField(i)));
          }
          return new Value(structType, proto, fields);
        }
      case TYPE_PROTO:
        if (!proto.hasProtoValue()) {
          throw typeMismatchException(type, proto);
        }
        break;
      case TYPE_RANGE:
        if (!proto.hasRangeValue()) {
          throw typeMismatchException(type, proto);
        }
        Type elementType = type.asRange().getElementType();
        return new Value(
            type.asRange(),
            proto,
            deserialize(elementType, proto.getRangeValue().getStart()),
            deserialize(elementType, proto.getRangeValue().getEnd()));
      case TYPE_UUID:
        if (!proto.hasUuidValue()) {
          throw typeMismatchException(type, proto);
        }
        return new Value(TypeKind.TYPE_UUID, proto, deserializeUuid(proto.getUuidValue()));
      case TYPE_MAP:
        if (!proto.hasMapValue()) {
          throw typeMismatchException(type, proto);
        }
        Type keyType = type.asMap().getKeyType();
        Type valueType = type.asMap().getValueType();
        Map<Value, Value> mapEntries = new HashMap<>();
        for (GoogleSQLValue.ValueProto.MapEntry entry : proto.getMapValue().getEntryList()) {
          mapEntries.put(
              deserialize(keyType, entry.getKey()), deserialize(valueType, entry.getValue()));
        }
        return new Value(type.asMap(), proto, mapEntries);

      default:
        throw new IllegalArgumentException("Should not happen: unsupported type " + type);
    }
    return new Value(type, proto);
  }

  public static boolean isSupportedTypeKind(Type type) {
    switch (type.getKind()) {
      case TYPE_INT32:
      case TYPE_INT64:
      case TYPE_UINT32:
      case TYPE_UINT64:
      case TYPE_FLOAT:
      case TYPE_DOUBLE:
      case TYPE_DATE:
      case TYPE_TIMESTAMP:
      case TYPE_BOOL:
      case TYPE_STRING:
      case TYPE_BYTES:
      case TYPE_ENUM:
      case TYPE_PROTO:
      case TYPE_DATETIME:
      case TYPE_TIME:
      case TYPE_NUMERIC:
      case TYPE_BIGNUMERIC:
      case TYPE_INTERVAL:
      case TYPE_JSON:
      case TYPE_UUID:
        return true;
      case TYPE_ARRAY:
        return isSupportedTypeKind(type.asArray().getElementType());
      case TYPE_STRUCT:
        {
          StructType structType = type.asStruct();
          for (int i = 0; i < structType.getFieldCount(); ++i) {
            if (!isSupportedTypeKind(structType.getField(i).getType())) {
              return false;
            }
          }
          return true;
        }
      case TYPE_RANGE:
        Type elementType = type.asRange().getElementType();
        return isSupportedTypeKind(elementType)
            && RangeType.isValidElementType(elementType.getKind());
      case TYPE_GRAPH_ELEMENT:
        return type.asGraphElement().getPropertyTypeList().stream()
            .allMatch(propertyType -> isSupportedTypeKind(propertyType.getType()));
      case TYPE_GRAPH_PATH:
        {
          GraphPathType path = type.asGraphPath();
          return isSupportedTypeKind(path.getNodeType()) && isSupportedTypeKind(path.getEdgeType());
        }
      case TYPE_MAP:
        return isSupportedTypeKind(type.asMap().getKeyType())
            && isSupportedTypeKind(type.asMap().getValueType());
      default:
        return false;
    }
  }

  // Static creator methods below

  /**
   * Creates a null value of given type.
   *
   * @param type
   */
  public static Value createNullValue(Type type) {
    Preconditions.checkArgument(isSupportedTypeKind(type));
    return new Value(type, ValueProto.getDefaultInstance());
  }

  /** Returns a null Value of the given simple type {@code kind}. */
  public static Value createSimpleNullValue(TypeKind kind) {
    return createNullValue(TypeFactory.createSimpleType(kind));
  }

  /** Returns an int32 Value that equals to {@code v}. */
  public static Value createInt32Value(int v) {
    ValueProto proto = ValueProto.newBuilder().setInt32Value(v).build();
    return new Value(TypeFactory.createSimpleType(TypeKind.TYPE_INT32), proto);
  }

  /** Returns an int64 Value that equals to {@code v}. */
  public static Value createInt64Value(long v) {
    ValueProto proto = ValueProto.newBuilder().setInt64Value(v).build();
    return new Value(TypeFactory.createSimpleType(TypeKind.TYPE_INT64), proto);
  }

  /** Returns an uint32 Value that equals to {@code v}. */
  public static Value createUint32Value(int v) {
    ValueProto proto = ValueProto.newBuilder().setUint32Value(v).build();
    return new Value(TypeFactory.createSimpleType(TypeKind.TYPE_UINT32), proto);
  }

  /** Returns an uint64 Value that equals to {@code v}. */
  public static Value createUint64Value(long v) {
    ValueProto proto = ValueProto.newBuilder().setUint64Value(v).build();
    return new Value(TypeFactory.createSimpleType(TypeKind.TYPE_UINT64), proto);
  }

  /** Returns an bool Value that equals to {@code v}. */
  public static Value createBoolValue(boolean v) {
    ValueProto proto = ValueProto.newBuilder().setBoolValue(v).build();
    return new Value(TypeFactory.createSimpleType(TypeKind.TYPE_BOOL), proto);
  }

  /** Returns an float Value that equals to {@code v}. */
  public static Value createFloatValue(float v) {
    ValueProto proto = ValueProto.newBuilder().setFloatValue(v).build();
    return new Value(TypeFactory.createSimpleType(TypeKind.TYPE_FLOAT), proto);
  }

  /** Returns an double Value that equals to {@code v}. */
  public static Value createDoubleValue(double v) {
    ValueProto proto = ValueProto.newBuilder().setDoubleValue(v).build();
    return new Value(TypeFactory.createSimpleType(TypeKind.TYPE_DOUBLE), proto);
  }

  /** Returns a numeric Value that equals to {@code v}. */
  private static ByteString serializeBigDecimal(
      BigDecimal v, int scale, BigDecimal maxValue, BigDecimal minValue, String typeName) {
    if (v.scale() > scale) {
      throw new IllegalArgumentException(
          typeName + " scale cannot exceed " + scale + ": " + v.toPlainString());
    }
    if (v.compareTo(maxValue) > 0 || v.compareTo(minValue) < 0) {
      throw new IllegalArgumentException(typeName + " overflow: " + v.toPlainString());
    }

    byte[] bytes = v.setScale(scale).unscaledValue().toByteArray();
    // NUMERIC/BIGNUMERIC values are serialized as scaled integers in two's complement form in
    // little endian
    // order. BigInteger requires the same encoding but in big endian order, therefore we must
    // reverse the bytes that come from the proto.
    Bytes.reverse(bytes);
    return ByteString.copyFrom(bytes);
  }

  /** Returns a numeric Value that equals to {@code v}. */
  public static Value createNumericValue(BigDecimal v) {
    ByteString serializedValue =
        serializeBigDecimal(v, NUMERIC_SCALE, MAX_NUMERIC_VALUE, MIN_NUMERIC_VALUE, "Numeric");
    ValueProto proto = ValueProto.newBuilder().setNumericValue(serializedValue).build();
    return new Value(TypeKind.TYPE_NUMERIC, proto, v);
  }

  /** Returns a BIGNUMERIC Value that equals to {@code v}. */
  public static Value createBigNumericValue(BigDecimal v) {
    ByteString serializedValue =
        serializeBigDecimal(
            v, BIGNUMERIC_SCALE, MAX_BIGNUMERIC_VALUE, MIN_BIGNUMERIC_VALUE, "BIGNUMERIC");
    ValueProto proto = ValueProto.newBuilder().setBignumericValue(serializedValue).build();
    return new Value(TypeKind.TYPE_BIGNUMERIC, proto, v);
  }

  /** Returns an INTERVAL Value that equals to {@code v}. */
  public static Value createIntervalValue(IntervalValue v) {
    ByteString serializedValue = IntervalValue.serializeInterval(v);
    ValueProto proto = ValueProto.newBuilder().setIntervalValue(serializedValue).build();
    return new Value(TypeKind.TYPE_INTERVAL, proto, v);
  }

  /** Returns an string Value that equals to {@code v}. */
  public static Value createStringValue(String v) {
    Preconditions.checkNotNull(v);
    ValueProto proto = ValueProto.newBuilder().setStringValue(v).build();
    return new Value(TypeFactory.createSimpleType(TypeKind.TYPE_STRING), proto);
  }

  /** Returns an bytes Value that equals to {@code v}. */
  public static Value createBytesValue(ByteString v) {
    Preconditions.checkNotNull(v);
    ValueProto proto = ValueProto.newBuilder().setBytesValue(v).build();
    return new Value(TypeFactory.createSimpleType(TypeKind.TYPE_BYTES), proto);
  }

  /** Returns a Uuid Value that equals to {@code v}. */
  private static ByteString serializeUuid(UUID v) {
    ByteBuffer buffer = ByteBuffer.allocate(UUID_VALUE_SIZE_IN_BYTES);
    buffer.order(ByteOrder.BIG_ENDIAN);
    buffer.putLong(0, v.getMostSignificantBits());
    buffer.putLong(8, v.getLeastSignificantBits());
    return ByteString.copyFrom(buffer);
  }

  /** Returns an UUID Value that equals to {@code v}. */
  public static Value createUuidValue(UUID v) {
    ByteString serializedValue = serializeUuid(v);
    ValueProto proto = ValueProto.newBuilder().setUuidValue(serializedValue).build();
    return new Value(TypeKind.TYPE_UUID, proto, v);
  }

  /**
   * Returns a date Value with given parameter.
   *
   * @param v Days from Jan 1, 1970
   */
  // TODO: Implement other versions that takes Java Date/Time types.
  @SuppressWarnings("GoodTime") // should accept a java.time.LocalDate (?)
  public static Value createDateValue(int v) {
    Preconditions.checkArgument(Type.isValidDate(v));
    ValueProto proto = ValueProto.newBuilder().setDateValue(v).build();
    return new Value(TypeFactory.createSimpleType(TypeKind.TYPE_DATE), proto);
  }

  /** Returns a date Value with given parameter. */
  public static Value createDateValue(LocalDate v) {
    Preconditions.checkArgument(Type.isValidDate(Math.toIntExact(v.toEpochDay())));
    ValueProto proto = ValueProto.newBuilder().setDateValue((int) v.toEpochDay()).build();
    return new Value(TypeFactory.createSimpleType(TypeKind.TYPE_DATE), proto);
  }

  /**
   * Returns a time Value with given parameter.
   *
   * @param bitFieldTimeNanos Bit field encoding of hour/minute/second/nanos. See
   *     googlesql/public/civil_time.h for the encoding.
   */
  @SuppressWarnings("GoodTime") // should accept a java.time.LocalTime (?)
  public static Value createTimeValue(long bitFieldTimeNanos) {
    decodePacked64TimeNanos(bitFieldTimeNanos);
    ValueProto proto = ValueProto.newBuilder().setTimeValue(bitFieldTimeNanos).build();
    return new Value(TypeFactory.createSimpleType(TypeKind.TYPE_TIME), proto);
  }

  /** Returns a time Value with given parameter. */
  public static Value createTimeValue(LocalTime t) {
    ValueProto proto = ValueProto.newBuilder().setTimeValue(encodePacked64TimeNanos(t)).build();
    return new Value(TypeFactory.createSimpleType(TypeKind.TYPE_TIME), proto);
  }

  /**
   * Returns a datetime Value with given parameter.
   *
   * @param bitFieldDatetimeSeconds Bit field encoding of year/month/day/hour/minute/second. See
   *     googlesql/public/civil_time.h for the encoding.
   * @param nanos Subsecond part at nano second precision.
   */
  @SuppressWarnings("GoodTime") // should accept a java.time.LocalDateTime (?)
  public static Value createDatetimeValue(long bitFieldDatetimeSeconds, int nanos) {
    Datetime datetime =
        Datetime.newBuilder()
            .setBitFieldDatetimeSeconds(bitFieldDatetimeSeconds)
            .setNanos(nanos)
            .build();
    decodePacked96DatetimeNanos(datetime);
    ValueProto proto = ValueProto.newBuilder().setDatetimeValue(datetime).build();
    return new Value(TypeFactory.createSimpleType(TypeKind.TYPE_DATETIME), proto);
  }

  /** Returns a datetime Value with given parameter. */
  public static Value createDatetimeValue(LocalDateTime v) {
    ValueProto proto =
        ValueProto.newBuilder().setDatetimeValue(encodePacked96DatetimeNanos(v)).build();
    return new Value(TypeFactory.createSimpleType(TypeKind.TYPE_DATETIME), proto);
  }

  /**
   * Returns date Value with given parameter.
   *
   * @param v Microseconds since epoch
   */
  @SuppressWarnings("GoodTime") // should accept a java.time.Instant
  public static Value createTimestampValueFromUnixMicros(long v) {
    Preconditions.checkArgument(Type.isValidTimestampUnixMicros(v));
    ValueProto proto = ValueProto.newBuilder().setTimestampValue(Timestamps.fromMicros(v)).build();
    return new Value(TypeFactory.createSimpleType(TypeKind.TYPE_TIMESTAMP), proto);
  }

  /** Returns an enum Value of given {@code type} with number value {@code v}. */
  public static Value createEnumValue(EnumType type, int v) {
    Preconditions.checkNotNull(type);
    Preconditions.checkArgument(!type.getDescriptor().isClosed() || type.findName(v) != null);
    ValueProto proto = ValueProto.newBuilder().setEnumValue(v).build();
    return new Value(type, proto);
  }

  /** Returns a proto Value of given {@code type} with encoded message {@code v}. */
  public static Value createProtoValue(ProtoType type, ByteString v) {
    Preconditions.checkNotNull(type);
    Preconditions.checkArgument(isSupportedTypeKind(type));
    Preconditions.checkNotNull(v);

    ValueProto proto = ValueProto.newBuilder().setProtoValue(v).build();
    return new Value(type, proto);
  }

  /** Returns a struct Value of given {@code type} and field {@code values}. */
  public static Value createStructValue(StructType type, Collection<Value> values) {
    Preconditions.checkNotNull(type);
    Preconditions.checkArgument(isSupportedTypeKind(type));
    Preconditions.checkNotNull(values);
    Preconditions.checkArgument(type.getFieldCount() == values.size());

    Struct.Builder builder = Struct.newBuilder();
    int i = 0;
    for (Value value : values) {
      Preconditions.checkArgument(type.getField(i++).getType().equals(value.type));
      builder.addField(value.proto);
    }

    ValueProto proto = ValueProto.newBuilder().setStructValue(builder).build();
    return new Value(type, proto, values);
  }

  /** Returns an array Value of given {@code type} and elements {@code values}. */
  public static Value createArrayValue(ArrayType type, Collection<Value> values) {
    Preconditions.checkNotNull(type);
    Preconditions.checkArgument(isSupportedTypeKind(type));
    Preconditions.checkNotNull(values);

    Array.Builder builder = Array.newBuilder();
    for (Value value : values) {
      Preconditions.checkArgument(type.getElementType().equals(value.type));
      builder.addElement(value.proto);
    }

    ValueProto proto = ValueProto.newBuilder().setArrayValue(builder).build();
    return new Value(type, proto, values);
  }

  // A comparator used internally to ensure deterministic order for map entries when creating a MAP
  // string representation. Unlike the C++ implementation, this sorts the stringified representation
  // of the key, not the key value directly.
  // If this becomes an issue, we will need to implement an equivalent of Value::LessThan() in Java.
  private static Comparator<Entry<Value, Value>> comparingMapEntryByKey() {
    return Comparator.comparing(Entry::getKey, Comparator.comparing(Value::toString));
  }

  // This is a helper function only used by createRangeValue. Returns a negative integer, zero, or a
  // positive integer if the first value is less than, equal to, or greater than the second value.
  private static long compareRangeBoundValues(Value first, Value second) {
    Preconditions.checkArgument(first.getType().equals(second.getType()));
    Type type = first.getType();

    switch (type.getKind()) {
      case TYPE_DATE:
        return (long) first.proto.getDateValue() - second.proto.getDateValue();
      case TYPE_DATETIME:
        long compare =
            first.proto.getDatetimeValue().getBitFieldDatetimeSeconds()
                - second.proto.getDatetimeValue().getBitFieldDatetimeSeconds();
        if (compare == 0) {
          return (long) first.proto.getDatetimeValue().getNanos()
              - second.proto.getDatetimeValue().getNanos();
        } else {
          return compare;
        }
      case TYPE_TIMESTAMP:
        return Timestamps.compare(
            first.getNanosecondsTimestampValue(), second.getNanosecondsTimestampValue());
      default:
        throw new IllegalStateException("Compare with unsupported type " + type);
    }
  }

  /** Returns a range Value of given {@code type}, {@code start}, and {@code end}. */
  public static Value createRangeValue(RangeType type, Value start, Value end) {
    Preconditions.checkNotNull(type);
    Preconditions.checkArgument(isSupportedTypeKind(type));
    Preconditions.checkNotNull(start);
    Preconditions.checkNotNull(end);
    Preconditions.checkArgument(start.getType().equals(end.getType()));
    Preconditions.checkArgument(start.getType().equals(type.getElementType()));
    Preconditions.checkArgument(
        start.isNull() || end.isNull() || compareRangeBoundValues(start, end) < 0);

    ValueProto proto =
        ValueProto.newBuilder()
            .setRangeValue(Range.newBuilder().setStart(start.proto).setEnd(end.proto))
            .build();
    return new Value(type, proto, start, end);
  }

  /** Returns a map Value of a given {@code type} and {@code mapEntries} */
  public static Value createMapValue(MapType type, Map<Value, Value> mapEntries) {
    Preconditions.checkNotNull(type);
    Preconditions.checkArgument(isSupportedTypeKind(type));
    Preconditions.checkNotNull(mapEntries);

    ImmutableList<ValueProto.MapEntry> mapEntriesProto =
        mapEntries.entrySet().stream()
            .map(
                entry -> {
                  Preconditions.checkArgument(
                      type.getKeyType().equals(entry.getKey().getType()),
                      "Key type %s does not match expected %s for key %s",
                      entry.getKey().getType(),
                      type.getKeyType(),
                      entry.getKey());
                  Preconditions.checkArgument(
                      type.getValueType().equals(entry.getValue().getType()),
                      "Value type %s does not match expected %s for value %s",
                      entry.getValue().getType(),
                      type.getValueType(),
                      entry.getValue());

                  return ValueProto.MapEntry.newBuilder()
                      .setKey(entry.getKey().proto)
                      .setValue(entry.getValue().proto)
                      .build();
                })
            .collect(ImmutableList.toImmutableList());
    ValueProto proto =
        ValueProto.newBuilder()
            .setMapValue(ValueProto.Map.newBuilder().addAllEntry(mapEntriesProto))
            .build();
    return new Value(type, proto, mapEntries);
  }

  /** Returns an empty array Value of given {@code type}. */
  public static Value createEmptyArrayValue(ArrayType type) {
    return createArrayValue(type, new ArrayList<Value>());
  }

  /** Returns a JSON Value from the given JSON document {@code document}. */
  public static Value createJsonValue(String document) {
    SimpleType jsonType = TypeFactory.createSimpleType(TypeKind.TYPE_JSON);
    Preconditions.checkArgument(isSupportedTypeKind(jsonType));
    Preconditions.checkNotNull(document);

    ValueProto proto = ValueProto.newBuilder().setJsonValue(document).build();
    return new Value(jsonType, proto);
  }

  /**
   * Returns a TimestampPicos proto from the serialized TimestampPicosValue.
   *
   * @param serializedValue Serialized value of a TimestampPicosValue. See method
   *     TimestampPicosValue::SerializeAndAppendToProtoBytes() in
   *     googlesql/public/timestamp_picos_value.cc for the encoding.
   */
  public static TimestampPicos deserializeTimestampPicos(ByteString serializedValue) {
    // TIMESTAMP_PICOS values are serialized as integers in two's complement form in
    // little endian order. BigInteger requires the same encoding but in big endian order,
    // therefore we must reverse the bytes.
    byte[] bytes = serializedValue.toByteArray();
    Bytes.reverse(bytes);
    BigInteger[] result =
        new BigInteger(bytes).divideAndRemainder(BigInteger.valueOf(Type.PICOS_PER_SECOND));
    long seconds = result[0].longValue();
    long picos = result[1].longValue();
    return TimestampPicos.newBuilder().setSeconds(seconds).setPicos(picos).build();
  }

  // Convert the big integer to a byte array with the given length.
  //
  // The byte array returned by BigInteger.toByeArray() has the minimum length needed. On the other
  // hand, this function returns a byte array with the specified length, Examples:
  // - if BigInteger.toByteArray() is [0x01], and length is 4, the return value is [0x00, 0x00,
  //   0x00, 0x01].
  // - if BigInteger.toByteArray() is [0xF0], and length is 4, the return value is [0xFF, 0xFF,
  //   0xFF, 0xF0].
  private static byte[] toByteArray(BigInteger bigInt, int length) {
    byte[] base = bigInt.toByteArray();
    Preconditions.checkArgument(base.length <= length);
    byte[] returnArray = new byte[Math.max(base.length, length)];
    // If the big integer is negative, perform sign extension by filling the array with 0xFF.
    if (bigInt.signum() < 0) {
      Arrays.fill(returnArray, (byte) 0xFF);
    }
    System.arraycopy(base, 0, returnArray, returnArray.length - base.length, base.length);
    return returnArray;
  }

  /**
   * Serializes a TimestampPicos proto, in the same way as method
   * TimestampPicosValue::SerializeAndAppendToProtoBytes() in
   * googlesql/public/timestamp_picos_value.cc for the encoding.
   */
  public static ByteString serializeTimestampPicos(TimestampPicos picos) {
    Preconditions.checkArgument(Type.isValidTimestamp(picos));

    // TIMESTAMP_PICOS values are serialized as 128-bit integers in two's complement form in little
    // endian order, taking exactly 16 bytes. BigInteger requires the same encoding but in big
    // endian order, therefore we must reverse the bytes.
    BigInteger v =
        BigInteger.valueOf(picos.getSeconds())
            .multiply(BigInteger.valueOf(Type.PICOS_PER_SECOND))
            .add(BigInteger.valueOf(picos.getPicos()));
    byte[] bytes = toByteArray(v, /* length= */ 16);

    // The byte array from BigInteger is in big endian order, while encoded byte array of
    // TIMESTAMP_PICOS is in little endian order, therefore we must reverse the bytes.
    Bytes.reverse(bytes);
    return ByteString.copyFrom(bytes);
  }

  private void checkValueNotNull() {
    Preconditions.checkState(!isNull, "Can't retrieve null value");
  }

  private void checkValueHasKind(TypeKind kind) {
    // Inlining `checkState` so that the String replacements are done lazily.
    if (getType().getKind() != kind) {
      throw new IllegalStateException(
          Strings.lenientFormat(
              "Value is a %s, but was expecting %s",
              Ascii.toLowerCase(getType().getKind().toString()).replace("type_", ""),
              Ascii.toLowerCase(kind.toString()).replace("type_", "")));
    }
  }

  /**
   * Helper function to get the timestamp value from the proto. If the proto is storing timestamp
   * picos, we will convert it to timestamp, truncating the picos part.
   */
  private Timestamp getNanosecondsTimestampValue() {
    checkValueHasKind(TypeKind.TYPE_TIMESTAMP);
    checkValueNotNull();
    if (proto.hasTimestampPicosValue()) {
      return Timestamp.newBuilder()
          .setSeconds(proto.getTimestampPicosValue().getSeconds())
          .setNanos((int) (proto.getTimestampPicosValue().getPicos() / 1000))
          .build();
    } else {
      return proto.getTimestampValue();
    }
  }
}
