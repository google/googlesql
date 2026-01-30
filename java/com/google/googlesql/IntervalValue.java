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

import com.google.auto.value.AutoValue;
import com.google.errorprone.annotations.Immutable;
import com.google.protobuf.ByteString;
import java.nio.ByteBuffer;
import java.nio.ByteOrder;

/**
 * The object for holding interval value as combinations of months, days, and nanos represented as
 * micros and nano fractions. See (broken link) for more details.
 */
@AutoValue
@Immutable
@SuppressWarnings("GoodTime")
public abstract class IntervalValue implements Comparable<IntervalValue> {

  private static final int MONTHS_MASK = 0x7FFFE000;
  private static final int NANO_FRACTIONS_MASK = 0x3FF;
  private static final int MONTH_SHIFT = 13;
  private static final int MONTH_SIGN_SHIFT = 31;
  private static final int INTERVAL_VALUE_SIZE_IN_BYTES = 16;

  private static final int MAX_YEARS = 10000;
  private static final int MAX_MONTHS = 12 * MAX_YEARS;
  private static final int MAX_DAYS = 366 * MAX_YEARS;
  private static final long MAX_MICROS = MAX_DAYS * 24L * 3600 * 1000000;
  private static final int MAX_NANO_FRACTIONS = 999;

  private static final int MIN_MONTHS = -MAX_MONTHS;
  private static final int MIN_DAYS = -MAX_DAYS;
  private static final long MIN_MICROS = -MAX_MICROS;
  private static final int MIN_NANO_FRACTIONS = 0;

  private static final long DAYS_IN_MONTH = 30;
  private static final long HOURS_IN_DAY = 24;
  private static final long MINUTE_IN_HOUR = 60L;
  private static final long SECONDS_IN_MINUTE = 60L;
  private static final long MILLIS_IN_SECOND = 1000L;
  private static final long MICROS_IN_MILLI = 1000L;

  private static final long MICROS_IN_SECOND = MILLIS_IN_SECOND * MICROS_IN_MILLI;
  private static final long MICROS_IN_MINUTE  = SECONDS_IN_MINUTE * MICROS_IN_SECOND;
  private static final long MICROS_IN_HOUR = MINUTE_IN_HOUR * MICROS_IN_MINUTE;
  private static final long MICROS_IN_DAY = HOURS_IN_DAY * MICROS_IN_HOUR;
  private static final long MICROS_IN_MONTH = DAYS_IN_MONTH * MICROS_IN_DAY;

  public abstract int months();

  public abstract int days();

  public abstract long micros();

  public abstract short nanoFractions();

  public static Builder builder() {
    return new AutoValue_IntervalValue.Builder();
  }

  /** Creates an IntervalValue consisting of the given number of months. */
  public static IntervalValue ofMonths(int months) {
    return builder().setMonths(months).setDays(0).setMicros(0).setNanoFractions((short) 0).build();
  }

  /** Creates an IntervalValue consisting of the given number of days. */
  public static IntervalValue ofDays(int days) {
    return builder().setMonths(0).setDays(days).setMicros(0).setNanoFractions((short) 0).build();
  }

  /** Creates an IntervalValue consisting of the given number of seconds. */
  public static IntervalValue ofSeconds(int seconds) {
    return builder()
        .setMonths(0)
        .setDays(0)
        .setMicros(seconds * MICROS_IN_SECOND)
        .setNanoFractions((short) 0)
        .build();
  }

  /** Creates an IntervalValue consisting of the given number of microseconds. */
  public static IntervalValue ofMicros(long micros) {
    return builder().setMonths(0).setDays(0).setMicros(micros).setNanoFractions((short) 0).build();
  }

  /**
   * Serializes the {@link IntervalValue} into {@link ByteString}.
   *
   * <p>The output has 16 bytes. The first 8 bytes store the micros component. The next 4 bytes
   * store the days component. The last 4 bytes store the months component and nanoFractions
   * component.
   */
  public static ByteString serializeInterval(IntervalValue v) {
    validateMonths(v.months());
    validateDays(v.days());
    validateMicros(v.micros());
    validateNanoFractions(v.nanoFractions());

    ByteBuffer buffer = ByteBuffer.allocate(INTERVAL_VALUE_SIZE_IN_BYTES);
    buffer.order(ByteOrder.LITTLE_ENDIAN);
    buffer.putLong(0, v.micros());
    buffer.putInt(8, v.days());

    int monthNano = 0;
    if (v.months() < 0) {
      monthNano = 1 << MONTH_SIGN_SHIFT;
    }
    monthNano |= (Math.abs(v.months()) << MONTH_SHIFT);
    monthNano |= v.nanoFractions();
    buffer.putInt(12, monthNano);
    return ByteString.copyFrom(buffer);
  }

  /**
   * Deserializes the {@link ByteString} to {@link IntervalValue}.
   *
   * <p>The micros component is stored as a long. The days component and months component are stored
   * as integers. The nanoFractions are stored as a short.
   */
  public static IntervalValue deserializeInterval(ByteString serializedValue) {
    if (serializedValue.isEmpty()) {
      return IntervalValue.builder()
          .setMonths(0)
          .setDays(0)
          .setMicros(0)
          .setNanoFractions((short) 0)
          .build();
    }

    byte[] bytes = serializedValue.toByteArray();
    if (bytes.length != INTERVAL_VALUE_SIZE_IN_BYTES) {
      throw new IllegalArgumentException(
          String.format("Unexpected Interval value length in bytes: %s.", bytes.length));
    }

    int monthNano = ByteBuffer.wrap(bytes, 12, 4).order(ByteOrder.LITTLE_ENDIAN).getInt();
    int nanoFractions = monthNano & NANO_FRACTIONS_MASK;
    validateNanoFractions(nanoFractions);

    int months = (monthNano & MONTHS_MASK) >> MONTH_SHIFT;
    int monthSign = (monthNano >> MONTH_SIGN_SHIFT) & 1;
    if (monthSign > 0) {
      months = -months;
    }
    validateMonths(months);

    int days = ByteBuffer.wrap(bytes, 8, 4).order(ByteOrder.LITTLE_ENDIAN).getInt();
    validateDays(days);

    long micros = ByteBuffer.wrap(bytes, 0, 8).order(ByteOrder.LITTLE_ENDIAN).getLong();
    validateMicros(micros);

    return builder()
        .setMonths(months)
        .setDays(days)
        .setMicros(micros)
        .setNanoFractions((short) nanoFractions)
        .build();
  }

  public static void validateMonths(int months) {
    validateField(months, MIN_MONTHS, MAX_MONTHS, "months");
  }

  public static void validateDays(int days) {
    validateField(days, MIN_DAYS, MAX_DAYS, "days");
  }

  public static void validateMicros(long micros) {
    validateField(micros, MIN_MICROS, MAX_MICROS, "micros");
  }

  public static void validateNanoFractions(int nanoFractions) {
    validateField(nanoFractions, MIN_NANO_FRACTIONS, MAX_NANO_FRACTIONS, "nanoFractions");
  }

  // micros can be bigger (up to 3 times) than the maximum number of micros
  // allowed in interval.
  public long asMicros() {
    return months() * MICROS_IN_MONTH + days() * MICROS_IN_DAY + micros();
  }

  /** Compares given IntervalValue to 0. */
  public boolean isZero() {
    return months() == 0 && days() == 0 && micros() == 0 && nanoFractions() == 0;
  }

  @Override
  public int compareTo(IntervalValue other) {
    int microsComp = Long.compare(asMicros(), other.asMicros());
    if (microsComp != 0) {
      return microsComp;
    }
    return Short.compare(nanoFractions(), other.nanoFractions());
  }

  private static <T extends Comparable<T>> void validateField(
      T value, T minValue, T maxValue, String fieldName) {
    if (value.compareTo(minValue) < 0 || value.compareTo(maxValue) > 0) {
      throw new IllegalArgumentException(
          String.format(
              "Interval %s field value overflow, %s is out of range %s to %s.",
              fieldName, value, minValue, maxValue));
    }
  }

  /** The builder for {@link IntervalValue}. */
  @AutoValue.Builder
  public abstract static class Builder {
    public abstract Builder setMonths(int months);

    public abstract Builder setDays(int days);

    public abstract Builder setMicros(long micro);

    public abstract Builder setNanoFractions(short nanoFractions);

    public abstract IntervalValue build();
  }
}
