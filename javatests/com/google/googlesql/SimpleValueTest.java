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
import static org.junit.Assert.assertThrows;

import com.google.protobuf.ByteString;
import com.google.googlesql.GoogleSQLSimpleValue.SimpleValueProto;
import org.junit.Test;
import org.junit.runner.RunWith;
import org.junit.runners.JUnit4;

@RunWith(JUnit4.class)
public final class SimpleValueTest {
  @Test
  public void typeInt64() {
    SimpleValue int64Value = SimpleValue.createInt64(12345);
    assertThat(int64Value.isValid()).isTrue();
    assertThat(int64Value.hasInt64()).isTrue();
    assertThat(int64Value.int64()).isEqualTo(12345);
    assertThat(int64Value.hasString()).isFalse();
    assertThrows(UnsupportedOperationException.class, int64Value::string);

    // Equality checks
    SimpleValue anotherSameInt64Value = SimpleValue.createInt64(12345);
    assertThat(int64Value.equals(anotherSameInt64Value)).isTrue();

    SimpleValue anotherInt64Value = SimpleValue.createInt64(54321);
    assertThat(int64Value.equals(anotherInt64Value)).isFalse();

    SimpleValue invalidValue = SimpleValue.create();
    assertThat(int64Value.equals(invalidValue)).isFalse();

    // Debug String
    assertThat(int64Value.debugString()).isEqualTo("12345");

    // Serialization and Deserialization
    SimpleValueProto proto = int64Value.serialize();
    assertThat(int64Value.equals(SimpleValue.deserialize(proto))).isTrue();
  }

  @Test
  public void typeString() {
    SimpleValue stringValue = SimpleValue.createString("a string");
    assertThat(stringValue.isValid()).isTrue();
    assertThat(stringValue.hasString()).isTrue();
    assertThat(stringValue.string()).isEqualTo("a string");
    assertThat(stringValue.hasBool()).isFalse();
    assertThrows(UnsupportedOperationException.class, stringValue::bool);

    // Equality checks
    SimpleValue anotherSameStringValue = SimpleValue.createString("a string");
    assertThat(stringValue.equals(anotherSameStringValue)).isTrue();

    SimpleValue anotherStringValue = SimpleValue.createString("another string");
    assertThat(stringValue.equals(anotherStringValue)).isFalse();

    SimpleValue invalidValue = SimpleValue.create();
    assertThat(stringValue.equals(invalidValue)).isFalse();

    // Debug String
    assertThat(stringValue.debugString()).isEqualTo("\"a string\"");

    // Serialization and Deserialization
    SimpleValueProto proto = stringValue.serialize();
    assertThat(stringValue.equals(SimpleValue.deserialize(proto))).isTrue();
  }

  @Test
  public void typeBool() {
    SimpleValue boolValue = SimpleValue.createBool(true);
    assertThat(boolValue.isValid()).isTrue();
    assertThat(boolValue.hasBool()).isTrue();
    assertThat(boolValue.bool()).isTrue();
    assertThat(boolValue.hasFloat64()).isFalse();
    assertThrows(UnsupportedOperationException.class, boolValue::float64);

    // Equality checks
    SimpleValue anotherSameBoolValue = SimpleValue.createBool(true);
    assertThat(boolValue.equals(anotherSameBoolValue)).isTrue();

    SimpleValue anotherBoolValue = SimpleValue.createBool(false);
    assertThat(boolValue.equals(anotherBoolValue)).isFalse();

    SimpleValue invalidValue = SimpleValue.create();
    assertThat(boolValue.equals(invalidValue)).isFalse();

    // Debug String
    assertThat(boolValue.debugString()).isEqualTo("true");

    // Serialization and Deserialization
    SimpleValueProto proto = boolValue.serialize();
    assertThat(boolValue.equals(SimpleValue.deserialize(proto))).isTrue();
  }

  @Test
  public void typeFloat64() {
    SimpleValue float64Value = SimpleValue.createFloat64(1.2);
    assertThat(float64Value.isValid()).isTrue();
    assertThat(float64Value.hasFloat64()).isTrue();
    assertThat(float64Value.float64()).isEqualTo(1.2);
    assertThat(float64Value.hasBytes()).isFalse();
    assertThrows(UnsupportedOperationException.class, float64Value::bytes);

    // Equality checks
    SimpleValue anotherSameFloat64Value = SimpleValue.createFloat64(1.2);
    assertThat(float64Value.equals(anotherSameFloat64Value)).isTrue();

    SimpleValue anotherFloat64Value = SimpleValue.createFloat64(2.0);
    assertThat(float64Value.equals(anotherFloat64Value)).isFalse();

    SimpleValue invalidValue = SimpleValue.create();
    assertThat(float64Value.equals(invalidValue)).isFalse();

    // Debug String
    assertThat(float64Value.debugString()).isEqualTo("1.2");

    // Serialization and Deserialization
    SimpleValueProto proto = float64Value.serialize();
    assertThat(float64Value.equals(SimpleValue.deserialize(proto))).isTrue();
  }

  @Test
  public void typeBytes() {
    SimpleValue bytesValue = SimpleValue.createBytes(ByteString.copyFromUtf8("byte string"));
    assertThat(bytesValue.isValid()).isTrue();
    assertThat(bytesValue.hasBytes()).isTrue();
    assertThat(bytesValue.bytes()).isEqualTo(ByteString.copyFromUtf8("byte string"));
    assertThat(bytesValue.hasInt64()).isFalse();
    assertThrows(UnsupportedOperationException.class, bytesValue::int64);

    // Equality checks
    SimpleValue anotherSameBytesValue =
        SimpleValue.createBytes(ByteString.copyFromUtf8("byte string"));
    assertThat(bytesValue.equals(anotherSameBytesValue)).isTrue();

    SimpleValue anotherBytesValue =
        SimpleValue.createBytes(ByteString.copyFromUtf8("another byte string"));
    assertThat(bytesValue.equals(anotherBytesValue)).isFalse();

    SimpleValue invalidValue = SimpleValue.create();
    assertThat(bytesValue.equals(invalidValue)).isFalse();

    // Debug String
    assertThrows(UnsupportedOperationException.class, bytesValue::debugString);

    // Serialization and Deserialization
    SimpleValueProto proto = bytesValue.serialize();
    assertThat(bytesValue.equals(SimpleValue.deserialize(proto))).isTrue();
  }

  @Test
  public void typeInvalid() {
    SimpleValue invalidValue = SimpleValue.create();
    assertThat(invalidValue.isValid()).isFalse();
    assertThat(invalidValue.hasBytes()).isFalse();
    assertThrows(UnsupportedOperationException.class, invalidValue::bytes);

    // Equality checks
    SimpleValue invalidValue2 = SimpleValue.create();
    assertThat(invalidValue.equals(invalidValue2)).isTrue();

    // Debug String
    assertThat(invalidValue.debugString()).isEqualTo("<INVALID>");

    // Serialization and Deserialization
    assertThrows(IllegalStateException.class, invalidValue::serialize);
  }
}
