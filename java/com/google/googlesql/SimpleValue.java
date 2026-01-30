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

import com.google.auto.value.AutoOneOf;
import com.google.errorprone.annotations.Immutable;
import com.google.protobuf.ByteString;
import com.google.googlesql.GoogleSQLSimpleValue.SimpleValueProto;
import java.io.Serializable;

/**
 * An immutable simple value class that represents a light weight version of value class.
 *
 * <p>The SimpleValue class is implemented as a <a href="(broken link)">tagged union</a> via
 * {@link AutoOneOf}. This is a java implementation of C++ version simple_value.h.
 */
@AutoOneOf(SimpleValue.ValueType.class)
@Immutable
public abstract class SimpleValue implements Serializable {
  /** An enum type that represents the value type in the simple value class. */
  static enum ValueType {
    INVALID,
    INT64,
    STRING,
    BOOL,
    FLOAT64,
    BYTES,
  }

  abstract ValueType type();

  public abstract long int64();

  public abstract String string();

  public abstract boolean bool();

  public abstract double float64();

  public abstract ByteString bytes();

  /**
   * A placeholder for the INVALID enum type.
   *
   * <p>{@link AutoOneOf} requires the instance has the same number of instance fields as the enum.
   * So we need to have this placeholder for the INVALID enum type. This will not occupy extra space
   * as the whole class is implemented as a variant.
   *
   * <p>This should be always true when the type is an invalid type.
   */
  abstract boolean invalid();

  /** Creates an instance of {@link SimpleValue} with a long value. */
  public static SimpleValue createInt64(long value) {
    return AutoOneOf_SimpleValue.int64(value);
  }

  /** Creates an instance of {@link SimpleValue} with a string value. */
  public static SimpleValue createString(String value) {
    return AutoOneOf_SimpleValue.string(value);
  }

  /** Creates an instance of {@link SimpleValue} with a boolean value. */
  public static SimpleValue createBool(boolean value) {
    return AutoOneOf_SimpleValue.bool(value);
  }

  /** Creates an instance of {@link SimpleValue} with a double value. */
  public static SimpleValue createFloat64(double value) {
    return AutoOneOf_SimpleValue.float64(value);
  }

  /** Creates an instance of SimpleValue with a {@link ByteString} value. */
  public static SimpleValue createBytes(ByteString value) {
    return AutoOneOf_SimpleValue.bytes(value);
  }

  /** Creates a default instance of SimpleValue that has an invalid type. */
  public static SimpleValue create() {
    return AutoOneOf_SimpleValue.invalid(true);
  }

  /** Returns whether this instance is a valid {@link SimpleValue}. */
  public final boolean isValid() {
    return type() != ValueType.INVALID;
  }

  public final boolean hasInt64() {
    return type() == ValueType.INT64;
  }

  public final boolean hasString() {
    return type() == ValueType.STRING;
  }

  public final boolean hasBool() {
    return type() == ValueType.BOOL;
  }

  public final boolean hasFloat64() {
    return type() == ValueType.FLOAT64;
  }

  public final boolean hasBytes() {
    return type() == ValueType.BYTES;
  }

  /** Deserializes and returns an instance from the {@link SimpleValueProto} proto. */
  public static SimpleValue deserialize(SimpleValueProto proto) {
    if (proto.hasInt64Value()) {
      return createInt64(proto.getInt64Value());
    } else if (proto.hasStringValue()) {
      return createString(proto.getStringValue());
    } else if (proto.hasBoolValue()) {
      return createBool(proto.getBoolValue());
    } else if (proto.hasDoubleValue()) {
      return createFloat64(proto.getDoubleValue());
    } else if (proto.hasBytesValue()) {
      return createBytes(proto.getBytesValue());
    }
    throw new AssertionError("Unexpected SimpleValueProto: " + proto);
  }

  /** Serializes the {@link SimpleValue} instance to a {@link SimpleValueProto}. */
  @SuppressWarnings(
      "UnnecessaryDefaultInEnumSwitch") // Adds the explicit check to align to other methods.
  public final SimpleValueProto serialize() {
    SimpleValueProto.Builder builder = SimpleValueProto.newBuilder();
    switch (type()) {
      case INVALID:
        throw new IllegalStateException("SimpleValue with INVALID type cannot be serialized");
      case INT64:
        builder.setInt64Value(int64());
        break;
      case STRING:
        builder.setStringValue(string());
        break;
      case BOOL:
        builder.setBoolValue(bool());
        break;
      case FLOAT64:
        builder.setDoubleValue(float64());
        break;
      case BYTES:
        builder.setBytesValue(bytes());
        break;
      default:
        throw new AssertionError("Unexpected SimpleValue type: " + type());
    }
    return builder.build();
  }

  /** Returns a debug string for this {@link SimpleValue} instance. */
  public final String debugString() {
    switch (type()) {
      case INVALID:
        return "<INVALID>";
      case INT64:
        return String.valueOf(int64());
      case STRING:
        return "\"" + string() + "\"";
      case BOOL:
        return String.valueOf(bool());
      case FLOAT64:
        return String.valueOf(float64());
      case BYTES:
        throw new UnsupportedOperationException();
    }
    throw new AssertionError("Unexpected SimpleValue type: " + type());
  }
}
