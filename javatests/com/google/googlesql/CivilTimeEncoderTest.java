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
import static com.google.googlesql.CivilTimeEncoder.decodePacked32TimeSeconds;
import static com.google.googlesql.CivilTimeEncoder.decodePacked32TimeSecondsAsJavaTime;
import static com.google.googlesql.CivilTimeEncoder.decodePacked64DatetimeMicros;
import static com.google.googlesql.CivilTimeEncoder.decodePacked64DatetimeMicrosAsJavaTime;
import static com.google.googlesql.CivilTimeEncoder.decodePacked64DatetimeSeconds;
import static com.google.googlesql.CivilTimeEncoder.decodePacked64DatetimeSecondsAsJavaTime;
import static com.google.googlesql.CivilTimeEncoder.decodePacked64TimeMicros;
import static com.google.googlesql.CivilTimeEncoder.decodePacked64TimeMicrosAsJavaTime;
import static com.google.googlesql.CivilTimeEncoder.decodePacked64TimeNanos;
import static com.google.googlesql.CivilTimeEncoder.decodePacked64TimeNanosAsJavaTime;
import static com.google.googlesql.CivilTimeEncoder.decodePacked96DatetimeNanos;
import static com.google.googlesql.CivilTimeEncoder.decodePacked96DatetimeNanosAsJavaTime;
import static com.google.googlesql.CivilTimeEncoder.encodePacked32TimeSeconds;
import static com.google.googlesql.CivilTimeEncoder.encodePacked64DatetimeMicros;
import static com.google.googlesql.CivilTimeEncoder.encodePacked64DatetimeSeconds;
import static com.google.googlesql.CivilTimeEncoder.encodePacked64TimeMicros;
import static com.google.googlesql.CivilTimeEncoder.encodePacked64TimeNanos;
import static com.google.googlesql.CivilTimeEncoder.encodePacked96DatetimeNanos;
import static org.junit.Assert.fail;

import com.google.googlesql.GoogleSQLValue.ValueProto;

import org.joda.time.LocalDateTime;
import org.joda.time.LocalTime;
import org.junit.Test;
import org.junit.runner.RunWith;
import org.junit.runners.JUnit4;

@RunWith(JUnit4.class)

public class CivilTimeEncoderTest {

  @Test
  public void encodePacked32TimeSeconds_validTime() {
    // 00:00:00 -> 0b000000000000000|00000|000000|000000 -> 0x0
    assertThat(encodePacked32TimeSeconds(new LocalTime(0, 0, 0))).isEqualTo(0x0);
    // 00:01:02 -> 0b000000000000000|00000|000001|000010 -> 0x42
    assertThat(encodePacked32TimeSeconds(new LocalTime(0, 1, 2))).isEqualTo(0x42);
    // 12:00:00 -> 0b000000000000000|01100|000000|000000 -> 0xC000
    assertThat(encodePacked32TimeSeconds(new LocalTime(12, 0, 0))).isEqualTo(0xC000);
    // 13:14:15 -> 0b000000000000000|01101|001110|001111 -> 0xD38F
    assertThat(encodePacked32TimeSeconds(new LocalTime(13, 14, 15))).isEqualTo(0xD38F);
    // 23:59:59 -> 0b000000000000000|10111|111011|111011 -> 0x17EFB
    assertThat(encodePacked32TimeSeconds(new LocalTime(23, 59, 59))).isEqualTo(0x17EFB);
  }

  @Test
  public void encodePacked32TimeSecondsAsJavaTime_validTime() {
    // 00:00:00 -> 0b000000000000000|00000|000000|000000 -> 0x0
    assertThat(encodePacked32TimeSeconds(java.time.LocalTime.of(0, 0, 0))).isEqualTo(0x0);
    // 00:01:02 -> 0b000000000000000|00000|000001|000010 -> 0x42
    assertThat(encodePacked32TimeSeconds(java.time.LocalTime.of(0, 1, 2))).isEqualTo(0x42);
    // 12:00:00 -> 0b000000000000000|01100|000000|000000 -> 0xC000
    assertThat(encodePacked32TimeSeconds(java.time.LocalTime.of(12, 0, 0))).isEqualTo(0xC000);
    // 13:14:15 -> 0b000000000000000|01101|001110|001111 -> 0xD38F
    assertThat(encodePacked32TimeSeconds(java.time.LocalTime.of(13, 14, 15))).isEqualTo(0xD38F);
    // 23:59:59 -> 0b000000000000000|10111|111011|111011 -> 0x17EFB
    assertThat(encodePacked32TimeSeconds(java.time.LocalTime.of(23, 59, 59))).isEqualTo(0x17EFB);
  }

  @Test
  public void decodePacked32TimeSeconds_validBitFieldTimeSeconds() {
    // 00:00:00 -> 0b000000000000000|00000|000000|000000 -> 0x0
    assertThat(decodePacked32TimeSeconds(0x0)).isEqualTo(new LocalTime(0, 0, 0));
    // 00:01:02 -> 0b000000000000000|00000|000001|000010 -> 0x42
    assertThat(decodePacked32TimeSeconds(0x42)).isEqualTo(new LocalTime(0, 1, 2));
    // 12:00:00 -> 0b000000000000000|01100|000000|000000 -> 0xC000
    assertThat(decodePacked32TimeSeconds(0xC000)).isEqualTo(new LocalTime(12, 0, 0));
    // 13:14:15 -> 0b000000000000000|01101|001110|001111 -> 0xD38F
    assertThat(decodePacked32TimeSeconds(0xD38F)).isEqualTo(new LocalTime(13, 14, 15));
    // 23:59:59 -> 0b000000000000000|10111|111011|111011 -> 0x17EFB
    assertThat(decodePacked32TimeSeconds(0x17EFB)).isEqualTo(new LocalTime(23, 59, 59));
  }

  @Test
  public void decodePacked32TimeSecondsAsJavaTime_validBitFieldTimeSeconds() {
    // 00:00:00 -> 0b000000000000000|00000|000000|000000 -> 0x0
    assertThat(decodePacked32TimeSecondsAsJavaTime(0x0)).isEqualTo(java.time.LocalTime.of(0, 0, 0));
    // 00:01:02 -> 0b000000000000000|00000|000001|000010 -> 0x42
    assertThat(decodePacked32TimeSecondsAsJavaTime(0x42))
        .isEqualTo(java.time.LocalTime.of(0, 1, 2));
    // 12:00:00 -> 0b000000000000000|01100|000000|000000 -> 0xC000
    assertThat(decodePacked32TimeSecondsAsJavaTime(0xC000))
        .isEqualTo(java.time.LocalTime.of(12, 0, 0));
    // 13:14:15 -> 0b000000000000000|01101|001110|001111 -> 0xD38F
    assertThat(decodePacked32TimeSecondsAsJavaTime(0xD38F))
        .isEqualTo(java.time.LocalTime.of(13, 14, 15));
    // 23:59:59 -> 0b000000000000000|10111|111011|111011 -> 0x17EFB
    assertThat(decodePacked32TimeSecondsAsJavaTime(0x17EFB))
        .isEqualTo(java.time.LocalTime.of(23, 59, 59));
  }

  @Test
  public void decodePacked32TimeSeconds_invalidBitField_throwsIllegalArgumentException() {
    try {
      // 00:00:00 -> 0b000000000000001|00000|000000|000000 -> 0x20000
      decodePacked32TimeSeconds(0x20000);
      fail();
    } catch (IllegalArgumentException expected) {
    }
  }

  @Test
  public void decodePacked32TimeSecondsAsJavaTime_invalidBitField_throwsIllegalArgumentException() {
    try {
      // 00:00:00 -> 0b000000000000001|00000|000000|000000 -> 0x20000
      decodePacked32TimeSecondsAsJavaTime(0x20000);
      fail();
    } catch (IllegalArgumentException expected) {
    }
  }

  @Test
  public void decodePacked32TimeSeconds_invalidSecondOfMinute_throwsIllegalArgumentException() {
    try {
      // 23:59:60 -> 0b000000000000000|10111|111011|111100 -> 0x17EFC
      decodePacked32TimeSeconds(0x17EFC);
      fail();
    } catch (IllegalArgumentException expected) {
    }
  }

  @Test
  public void
      decodePacked32TimeSecondsAsJavaTime_invalidSecondOfMinute_throwsIllegalArgumentException() {
    try {
      // 23:59:60 -> 0b000000000000000|10111|111011|111100 -> 0x17EFC
      decodePacked32TimeSecondsAsJavaTime(0x17EFC);
      fail();
    } catch (IllegalArgumentException expected) {
    }
  }

  @Test
  public void decodePacked32TimeSeconds_invalidMinuteOfHour_throwsIllegalArgumentException() {
    try {
      // 00:60:00 -> 0b000000000000000|00000|111100|000000 -> 0xF00
      decodePacked32TimeSeconds(0xF00);
      fail();
    } catch (IllegalArgumentException expected) {
    }
  }

  @Test
  public void
      decodePacked32TimeSecondsAsJavaTime_invalidMinuteOfHour_throwsIllegalArgumentException() {
    try {
      // 00:60:00 -> 0b000000000000000|00000|111100|000000 -> 0xF00
      decodePacked32TimeSecondsAsJavaTime(0xF00);
      fail();
    } catch (IllegalArgumentException expected) {
    }
  }

  @Test
  public void decodePacked32TimeSeconds_invalidHourOfDay_throwsIllegalArgumentException() {
    try {
      // 24:00:00 -> 0b000000000000000|11000|000000|000000 -> 0x18000
      decodePacked32TimeSeconds(0x18000);
      fail();
    } catch (IllegalArgumentException expected) {
    }
  }

  @Test
  public void
      decodePacked32TimeSecondsAsJavaTime_invalidHourOfDay_throwsIllegalArgumentException() {
    try {
      // 24:00:00 -> 0b000000000000000|11000|000000|000000 -> 0x18000
      decodePacked32TimeSecondsAsJavaTime(0x18000);
      fail();
    } catch (IllegalArgumentException expected) {
    }
  }

  @Test
  public void encodePacked64TimeMicros_validTime() {
    // 00:00:00.000000
    // 0b000000000000000000000000000|00000|000000|000000|00000000000000000000
    // 0x0
    assertThat(encodePacked64TimeMicros(new LocalTime(0, 0, 0, 0))).isEqualTo(0x0L);
    // 00:01:02.003000
    // 0b000000000000000000000000000|00000|000001|000010|00000000101110111000
    // 0x4200BB8
    assertThat(encodePacked64TimeMicros(new LocalTime(0, 1, 2, 3))).isEqualTo(0x4200BB8L);
    // 12:00:00.000000
    // 0b000000000000000000000000000|01100|000000|000000|00000000000000000000
    // 0xC00000000
    assertThat(encodePacked64TimeMicros(new LocalTime(12, 0, 0, 0))).isEqualTo(0xC00000000L);
    // 13:14:15.016000
    // 0b000000000000000000000000000|01101|001110|001111|00000011111010000000
    // 0xD38F03E80
    assertThat(encodePacked64TimeMicros(new LocalTime(13, 14, 15, 16))).isEqualTo(0xD38F03E80L);
    // 23:59:59.999000
    // 0b000000000000000000000000000|10111|111011|111011|11110011111001011000
    // 0x17EFBF3E58
    assertThat(encodePacked64TimeMicros(new LocalTime(23, 59, 59, 999))).isEqualTo(0x17EFBF3E58L);
  }

  @Test
  public void encodePacked64TimeMicrosAsJavaTime_validTime() {
    // 00:00:00.000000
    // 0b000000000000000000000000000|00000|000000|000000|00000000000000000000
    // 0x0
    assertThat(encodePacked64TimeMicros(java.time.LocalTime.of(0, 0, 0, 0))).isEqualTo(0x0L);
    // 00:01:02.003000
    // 0b000000000000000000000000000|00000|000001|000010|00000000101110111000
    // 0x4200BB8
    assertThat(encodePacked64TimeMicros(java.time.LocalTime.of(0, 1, 2, 3_000_000)))
        .isEqualTo(0x4200BB8L);
    // 12:00:00.000000
    // 0b000000000000000000000000000|01100|000000|000000|00000000000000000000
    // 0xC00000000
    assertThat(encodePacked64TimeMicros(java.time.LocalTime.of(12, 0, 0, 0)))
        .isEqualTo(0xC00000000L);
    // 13:14:15.016000
    // 0b000000000000000000000000000|01101|001110|001111|00000011111010000000
    // 0xD38F03E80
    assertThat(encodePacked64TimeMicros(java.time.LocalTime.of(13, 14, 15, 16_000_000)))
        .isEqualTo(0xD38F03E80L);
    // 23:59:59.999000
    // 0b000000000000000000000000000|10111|111011|111011|11110011111001011000
    // 0x17EFBF3E58
    assertThat(encodePacked64TimeMicros(java.time.LocalTime.of(23, 59, 59, 999_000_000)))
        .isEqualTo(0x17EFBF3E58L);
  }

  @Test
  public void decodePacked64TimeMicros_validBitFieldTimeMicros() {
    // 00:00:00.000000
    // 0b000000000000000000000000000|00000|000000|000000|00000000000000000000
    // 0x0
    assertThat(decodePacked64TimeMicros(0x0L)).isEqualTo(new LocalTime(0, 0, 0, 0));
    // 00:01:02.000003
    // 0b000000000000000000000000000|00000|000001|000010|00000000000000000011
    // 0x4200003
    assertThat(decodePacked64TimeMicros(0x4200003L)).isEqualTo(new LocalTime(0, 1, 2, 0));
    // 12:00:00.000000
    // 0b000000000000000000000000000|01100|000000|000000|00000000000000000000
    // 0xC00000000
    assertThat(decodePacked64TimeMicros(0xC00000000L)).isEqualTo(new LocalTime(12, 0, 0, 0));
    // 13:14:15.000016
    // 0b000000000000000000000000000|01101|001110|001111|00000000000000010000
    // 0xD38F00010
    assertThat(decodePacked64TimeMicros(0xD38F00010L)).isEqualTo(new LocalTime(13, 14, 15, 0));
    // 23:59:59.999999
    // 0b000000000000000000000000000|10111|111011|111011|11110100001000111111
    // 0x17EFBF423F
    assertThat(decodePacked64TimeMicros(0x17EFBF423FL)).isEqualTo(new LocalTime(23, 59, 59, 999));
  }

  @Test
  public void decodePacked64TimeMicrosAsJavaTime_validBitFieldTimeMicros() {
    // 00:00:00.000000
    // 0b000000000000000000000000000|00000|000000|000000|00000000000000000000
    // 0x0
    assertThat(decodePacked64TimeMicrosAsJavaTime(0x0L))
        .isEqualTo(java.time.LocalTime.of(0, 0, 0, 0));
    // 00:01:02.000003
    // 0b000000000000000000000000000|00000|000001|000010|00000000000000000011
    // 0x4200003
    assertThat(decodePacked64TimeMicrosAsJavaTime(0x4200003L))
        .isEqualTo(java.time.LocalTime.of(0, 1, 2, 3_000));
    // 12:00:00.000000
    // 0b000000000000000000000000000|01100|000000|000000|00000000000000000000
    // 0xC00000000
    assertThat(decodePacked64TimeMicrosAsJavaTime(0xC00000000L))
        .isEqualTo(java.time.LocalTime.of(12, 0, 0, 0));
    // 13:14:15.000016
    // 0b000000000000000000000000000|01101|001110|001111|00000000000000010000
    // 0xD38F00010
    assertThat(decodePacked64TimeMicrosAsJavaTime(0xD38F00010L))
        .isEqualTo(java.time.LocalTime.of(13, 14, 15, 16_000));
    // 23:59:59.999999
    // 0b000000000000000000000000000|10111|111011|111011|11110100001000111111
    // 0x17EFBF423F
    assertThat(decodePacked64TimeMicrosAsJavaTime(0x17EFBF423FL))
        .isEqualTo(java.time.LocalTime.of(23, 59, 59, 999_999_000));
  }

  @Test
  public void decodePacked64TimeMicros_invalidBitField_throwsIllegalArgumentException() {
    try {
      // 00:00:00.000000
      // 0b000000000000000000000000001|00000|000000|000000|00000000000000000000
      // 0x2000000000
      decodePacked64TimeMicros(0x2000000000L);
      fail();
    } catch (IllegalArgumentException expected) {
    }
  }

  @Test
  public void decodePacked64TimeMicrosAsJavaTime_invalidBitField_throwsIllegalArgumentException() {
    try {
      // 00:00:00.000000
      // 0b000000000000000000000000001|00000|000000|000000|00000000000000000000
      // 0x2000000000
      decodePacked64TimeMicrosAsJavaTime(0x2000000000L);
      fail();
    } catch (IllegalArgumentException expected) {
    }
  }

  @Test
  public void decodePacked64TimeMicros_invalidMicroOfSecond_throwsIllegalArgumentException() {
    try {
      // 00:00:00.1000000
      // 0b000000000000000000000000000|00000|000000|000000|11110100001001000000
      // 0xF4240
      decodePacked64TimeMicros(0xF4240L);
      fail();
    } catch (IllegalArgumentException expected) {
    }
  }

  @Test
  public void
      decodePacked64TimeMicrosAsJavaTime_invalidMicroOfSecond_throwsIllegalArgumentException() {
    try {
      // 00:00:00.1000000
      // 0b000000000000000000000000000|00000|000000|000000|11110100001001000000
      // 0xF4240
      decodePacked64TimeMicrosAsJavaTime(0xF4240L);
      fail();
    } catch (IllegalArgumentException expected) {
    }
  }

  @Test
  public void decodePacked64TimeMicros_invalidSecondOfMinute_throwsIllegalArgumentException() {
    try {
      // 00:00:60.000000
      // 0b000000000000000000000000000|00000|000000|111100|00000000000000000000
      // 0x3C00000
      decodePacked64TimeMicros(0x3C00000L);
      fail();
    } catch (IllegalArgumentException expected) {
    }
  }

  @Test
  public void
      decodePacked64TimeMicrosAsJavaTime_invalidSecondOfMinute_throwsIllegalArgumentException() {
    try {
      // 00:00:60.000000
      // 0b000000000000000000000000000|00000|000000|111100|00000000000000000000
      // 0x3C00000
      decodePacked64TimeMicrosAsJavaTime(0x3C00000L);
      fail();
    } catch (IllegalArgumentException expected) {
    }
  }

  @Test
  public void decodePacked64TimeMicros_invalidMinuteOfHour_throwsIllegalArgumentException() {
    try {
      // 00:60:00.000000
      // 0b000000000000000000000000000|00000|111100|000000|00000000000000000000
      // 0xF0000000
      decodePacked64TimeMicros(0xF0000000L);
      fail();
    } catch (IllegalArgumentException expected) {
    }
  }

  @Test
  public void
      decodePacked64TimeMicrosAsJavaTime_invalidMinuteOfHour_throwsIllegalArgumentException() {
    try {
      // 00:60:00.000000
      // 0b000000000000000000000000000|00000|111100|000000|00000000000000000000
      // 0xF0000000
      decodePacked64TimeMicrosAsJavaTime(0xF0000000L);
      fail();
    } catch (IllegalArgumentException expected) {
    }
  }

  @Test
  public void decodePacked64TimeMicros_invalidHourOfDay_throwsIllegalArgumentException() {
    try {
      // 24:00:00.000000
      // 0b000000000000000000000000000|11000|000000|000000|00000000000000000000
      // 0x1800000000
      decodePacked64TimeMicros(0x1800000000L);
      fail();
    } catch (IllegalArgumentException expected) {
    }
  }

  @Test
  public void decodePacked64TimeMicrosAsJavaTime_invalidHourOfDay_throwsIllegalArgumentException() {
    try {
      // 24:00:00.000000
      // 0b000000000000000000000000000|11000|000000|000000|00000000000000000000
      // 0x1800000000
      decodePacked64TimeMicrosAsJavaTime(0x1800000000L);
      fail();
    } catch (IllegalArgumentException expected) {
    }
  }

  @Test
  public void encodePacked64TimeNanos_validTime() {
    // 00:00:00.000000000
    // 0b00000000000000000|00000|000000|000000|000000000000000000000000000000
    // 0x0
    assertThat(encodePacked64TimeNanos(new LocalTime(0, 0, 0, 0))).isEqualTo(0x0L);
    // 00:01:02.003000000
    // 0b00000000000000000|00000|000001|000010|000000001011011100011011000000
    // 0x10802DC6C0
    assertThat(encodePacked64TimeNanos(new LocalTime(0, 1, 2, 3))).isEqualTo(0x10802DC6C0L);
    // 12:00:00.000000000
    // 0b00000000000000000|01100|000000|000000|000000000000000000000000000000
    // 0x300000000000
    assertThat(encodePacked64TimeNanos(new LocalTime(12, 0, 0, 0))).isEqualTo(0x300000000000L);
    // 13:14:15.016000000
    // 0b00000000000000000|01101|001110|001111|000000111101000010010000000000
    // 0x34E3C0F42400
    assertThat(encodePacked64TimeNanos(new LocalTime(13, 14, 15, 16))).isEqualTo(0x34E3C0F42400L);
    // 23:59:59.999000000
    // 0b00000000000000000|10111|111011|111011|111011100010111000011111000000
    // 0x5FBEFB8B87C0
    assertThat(encodePacked64TimeNanos(new LocalTime(23, 59, 59, 999))).isEqualTo(0x5FBEFB8B87C0L);
  }

  @Test
  public void encodePacked64TimeNanosAsJavaTime_validTime() {
    // 00:00:00.000000000
    // 0b00000000000000000|00000|000000|000000|000000000000000000000000000000
    // 0x0
    assertThat(encodePacked64TimeNanos(java.time.LocalTime.of(0, 0, 0, 0))).isEqualTo(0x0L);
    // 00:01:02.003000000
    // 0b00000000000000000|00000|000001|000010|000000001011011100011011000000
    // 0x10802DC6C0
    assertThat(encodePacked64TimeNanos(java.time.LocalTime.of(0, 1, 2, 3_000_000)))
        .isEqualTo(0x10802DC6C0L);
    // 12:00:00.000000000
    // 0b00000000000000000|01100|000000|000000|000000000000000000000000000000
    // 0x300000000000
    assertThat(encodePacked64TimeNanos(java.time.LocalTime.of(12, 0, 0, 0)))
        .isEqualTo(0x300000000000L);
    // 13:14:15.016000000
    // 0b00000000000000000|01101|001110|001111|000000111101000010010000000000
    // 0x34E3C0F42400
    assertThat(encodePacked64TimeNanos(java.time.LocalTime.of(13, 14, 15, 16_000_000)))
        .isEqualTo(0x34E3C0F42400L);
    // 23:59:59.999000000
    // 0b00000000000000000|10111|111011|111011|111011100010111000011111000000
    // 0x5FBEFB8B87C0
    assertThat(encodePacked64TimeNanos(java.time.LocalTime.of(23, 59, 59, 999_000_000)))
        .isEqualTo(0x5FBEFB8B87C0L);
  }

  @Test
  public void decodePacked64TimeNanos_validBitFieldTimeNanos() {
    // 00:00:00.000000000
    // 0b00000000000000000|00000|000000|000000|000000000000000000000000000000
    // 0x0
    assertThat(decodePacked64TimeNanos(0x0L)).isEqualTo(new LocalTime(0, 0, 0, 0));
    // 00:01:02.000000003
    // 0b00000000000000000|00000|000001|000010|000000000000000000000000000011
    // 0x1080000003
    assertThat(decodePacked64TimeNanos(0x1080000003L)).isEqualTo(new LocalTime(0, 1, 2, 0));
    // 12:00:00.000000000
    // 0b00000000000000000|01100|000000|000000|000000000000000000000000000000
    // 0x300000000000
    assertThat(decodePacked64TimeNanos(0x300000000000L)).isEqualTo(new LocalTime(12, 0, 0, 0));
    // 13:14:15.000000016
    // 0b00000000000000000|01101|001110|001111|000000000000000000000000010000
    // 0x34E3C0000010
    assertThat(decodePacked64TimeNanos(0x34E3C0000010L)).isEqualTo(new LocalTime(13, 14, 15, 0));
    // 23:59:59.999999999
    // 0b00000000000000000|10111|111011|111011|111011100110101100100111111111
    // 0x5FBEFB9AC9FF
    assertThat(decodePacked64TimeNanos(0x5FBEFB9AC9FFL)).isEqualTo(new LocalTime(23, 59, 59, 999));
  }

  @Test
  public void decodePacked64TimeNanosAsJavaTime_validBitFieldTimeNanos() {
    // 00:00:00.000000000
    // 0b00000000000000000|00000|000000|000000|000000000000000000000000000000
    // 0x0
    assertThat(decodePacked64TimeNanosAsJavaTime(0x0L))
        .isEqualTo(java.time.LocalTime.of(0, 0, 0, 0));
    // 00:01:02.000000003
    // 0b00000000000000000|00000|000001|000010|000000000000000000000000000011
    // 0x1080000003
    assertThat(decodePacked64TimeNanosAsJavaTime(0x1080000003L))
        .isEqualTo(java.time.LocalTime.of(0, 1, 2, 3));
    // 12:00:00.000000000
    // 0b00000000000000000|01100|000000|000000|000000000000000000000000000000
    // 0x300000000000
    assertThat(decodePacked64TimeNanosAsJavaTime(0x300000000000L))
        .isEqualTo(java.time.LocalTime.of(12, 0, 0, 0));
    // 13:14:15.000000016
    // 0b00000000000000000|01101|001110|001111|000000000000000000000000010000
    // 0x34E3C0000010
    assertThat(decodePacked64TimeNanosAsJavaTime(0x34E3C0000010L))
        .isEqualTo(java.time.LocalTime.of(13, 14, 15, 16));
    // 23:59:59.999999999
    // 0b00000000000000000|10111|111011|111011|111011100110101100100111111111
    // 0x5FBEFB9AC9FF
    assertThat(decodePacked64TimeNanosAsJavaTime(0x5FBEFB9AC9FFL))
        .isEqualTo(java.time.LocalTime.of(23, 59, 59, 999_999_999));
  }

  @Test
  public void decodePacked64TimeNanos_invalidBitField() {
    try {
      // 00:00:00.000000000
      // 0b00000000000000001|00000|000000|000000|000000000000000000000000000000
      // 0x800000000000
      decodePacked64TimeNanos(0x800000000000L);
      fail();
    } catch (IllegalArgumentException expected) {
    }
  }

  @Test
  public void decodePacked64TimeNanosAsJavaTime_invalidBitField() {
    try {
      // 00:00:00.000000000
      // 0b00000000000000001|00000|000000|000000|000000000000000000000000000000
      // 0x800000000000
      decodePacked64TimeNanosAsJavaTime(0x800000000000L);
      fail();
    } catch (IllegalArgumentException expected) {
    }
  }

  @Test
  public void decodePacked64TimeNanos_invalidNanoOfSecond_throwsIllegalArgumentException() {
    try {
      // 00:00:00.1000000000
      // 0b00000000000000000|00000|000000|000000|111011100110101100101000000000
      // 0x3B9ACA00
      decodePacked64TimeNanos(0x3B9ACA00L);
      fail();
    } catch (IllegalArgumentException expected) {
    }
  }

  @Test
  public void
      decodePacked64TimeNanosAsJavaTime_invalidNanoOfSecond_throwsIllegalArgumentException() {
    try {
      // 00:00:00.1000000000
      // 0b00000000000000000|00000|000000|000000|111011100110101100101000000000
      // 0x3B9ACA00
      decodePacked64TimeNanosAsJavaTime(0x3B9ACA00L);
      fail();
    } catch (IllegalArgumentException expected) {
    }
  }

  @Test
  public void decodePacked64TimeNanos_invalidSecondOfMinute_throwsIllegalArgumentException() {
    try {
      // 00:00:00.000000000
      // 0b00000000000000000|00000|000000|111100|000000000000000000000000000000
      // 0xF00000000
      decodePacked64TimeNanos(0xF00000000L);
      fail();
    } catch (IllegalArgumentException expected) {
    }
  }

  @Test
  public void
      decodePacked64TimeNanosAsJavaTime_invalidSecondOfMinute_throwsIllegalArgumentException() {
    try {
      // 00:00:00.000000000
      // 0b00000000000000000|00000|000000|111100|000000000000000000000000000000
      // 0xF00000000
      decodePacked64TimeNanosAsJavaTime(0xF00000000L);
      fail();
    } catch (IllegalArgumentException expected) {
    }
  }

  @Test
  public void decodePacked64TimeNanos_invalidMinuteOfHour_throwsIllegalArgumentException() {
    try {
      // 00:00:00.000000000
      // 0b00000000000000000|00000|111100|000000|000000000000000000000000000000
      // 0x3C000000000
      decodePacked64TimeNanos(0x3C000000000L);
      fail();
    } catch (IllegalArgumentException expected) {
    }
  }

  @Test
  public void
      decodePacked64TimeNanosAsJavaTime_invalidMinuteOfHour_throwsIllegalArgumentException() {
    try {
      // 00:00:00.000000000
      // 0b00000000000000000|00000|111100|000000|000000000000000000000000000000
      // 0x3C000000000
      decodePacked64TimeNanosAsJavaTime(0x3C000000000L);
      fail();
    } catch (IllegalArgumentException expected) {
    }
  }

  @Test
  public void decodePacked64TimeNanos_invalidHourOfDay_throwsIllegalArgumentException() {
    try {
      // 00:00:00.000000000
      // 0b00000000000000000|11000|000000|000000|000000000000000000000000000000
      // 0x600000000000
      decodePacked64TimeNanos(0x600000000000L);
      fail();
    } catch (IllegalArgumentException expected) {
    }
  }

  @Test
  public void decodePacked64TimeNanosAsJavaTime_invalidHourOfDay_throwsIllegalArgumentException() {
    try {
      // 00:00:00.000000000
      // 0b00000000000000000|11000|000000|000000|000000000000000000000000000000
      // 0x600000000000
      decodePacked64TimeNanosAsJavaTime(0x600000000000L);
      fail();
    } catch (IllegalArgumentException expected) {
    }
  }

  @Test
  public void encodePacked64DatetimeSeconds_validDateTime() {
    // 0001/01/01 00:00:00
    // 0b0000000000000000000000|00000000000001|0001|00001|00000|000000|000000
    // 0x4420000
    assertThat(encodePacked64DatetimeSeconds(new LocalDateTime(1, 1, 1, 0, 0, 0)))
        .isEqualTo(0x4420000L);
    // 0001/02/03 00:01:02
    // 0b0000000000000000000000|00000000000001|0010|00011|00000|000001|000010
    // 0x4860042
    assertThat(encodePacked64DatetimeSeconds(new LocalDateTime(1, 2, 3, 0, 1, 2)))
        .isEqualTo(0x4860042L);
    // 0001/01/01 12:00:00
    // 0b0000000000000000000000|00000000000001|0001|00001|01100|000000|000000
    // 0x442C000
    assertThat(encodePacked64DatetimeSeconds(new LocalDateTime(1, 1, 1, 12, 0, 0)))
        .isEqualTo(0x442C000L);
    // 0001/01/01 13:14:15
    // 0b0000000000000000000000|00000000000001|0001|00001|01101|001110|001111
    // 0x442D38F
    assertThat(encodePacked64DatetimeSeconds(new LocalDateTime(1, 1, 1, 13, 14, 15)))
        .isEqualTo(0x442D38FL);
    // 9999/12/31 23:59:59
    // 0b0000000000000000000000|10011100001111|1100|11111|10111|111011|111011
    // 0x9C3F3F7EFB
    assertThat(encodePacked64DatetimeSeconds(new LocalDateTime(9999, 12, 31, 23, 59, 59)))
        .isEqualTo(0x9C3F3F7EFBL);
  }

  @Test
  public void encodePacked64DatetimeSecondsAsJavaTime_validDateTime() {
    // 0001/01/01 00:00:00
    // 0b0000000000000000000000|00000000000001|0001|00001|00000|000000|000000
    // 0x4420000
    assertThat(encodePacked64DatetimeSeconds(java.time.LocalDateTime.of(1, 1, 1, 0, 0, 0)))
        .isEqualTo(0x4420000L);
    // 0001/02/03 00:01:02
    // 0b0000000000000000000000|00000000000001|0010|00011|00000|000001|000010
    // 0x4860042
    assertThat(encodePacked64DatetimeSeconds(java.time.LocalDateTime.of(1, 2, 3, 0, 1, 2)))
        .isEqualTo(0x4860042L);
    // 0001/01/01 12:00:00
    // 0b0000000000000000000000|00000000000001|0001|00001|01100|000000|000000
    // 0x442C000
    assertThat(encodePacked64DatetimeSeconds(java.time.LocalDateTime.of(1, 1, 1, 12, 0, 0)))
        .isEqualTo(0x442C000L);
    // 0001/01/01 13:14:15
    // 0b0000000000000000000000|00000000000001|0001|00001|01101|001110|001111
    // 0x442D38F
    assertThat(encodePacked64DatetimeSeconds(java.time.LocalDateTime.of(1, 1, 1, 13, 14, 15)))
        .isEqualTo(0x442D38FL);
    // 9999/12/31 23:59:59
    // 0b0000000000000000000000|10011100001111|1100|11111|10111|111011|111011
    // 0x9C3F3F7EFB
    assertThat(encodePacked64DatetimeSeconds(java.time.LocalDateTime.of(9999, 12, 31, 23, 59, 59)))
        .isEqualTo(0x9C3F3F7EFBL);
  }

  @Test
  public void encodePacked64DatetimeSeconds_invalidYear_throwsIllegalArgumentException() {
    // 10000/01/01 00:00:00
    // 0b0000000000000000000000|10011100010000|0001|00001|00000|000000|000000
    // 0x9C40420000
    LocalDateTime dateTime = new LocalDateTime(10000, 1, 1, 0, 0, 0);
    try {
      encodePacked64DatetimeSeconds(dateTime);
      fail();
    } catch (IllegalArgumentException expected) {
    }
  }

  @Test
  public void encodePacked64DatetimeSecondsAsJavaTime_invalidYear_throwsIllegalArgumentException() {
    // 10000/01/01 00:00:00
    // 0b0000000000000000000000|10011100010000|0001|00001|00000|000000|000000
    // 0x9C40420000
    java.time.LocalDateTime dateTime = java.time.LocalDateTime.of(10000, 1, 1, 0, 0, 0);
    try {
      encodePacked64DatetimeSeconds(dateTime);
      fail();
    } catch (IllegalArgumentException expected) {
    }
  }

  @Test
  public void decodePacked64DatetimeSeconds_validBitFieldDatetimeSeconds() {
    // 0001/01/01 00:00:00
    // 0b0000000000000000000000|00000000000001|0001|00001|00000|000000|000000
    // 0x4420000
    assertThat(decodePacked64DatetimeSeconds(0x4420000L))
        .isEqualTo(new LocalDateTime(1, 1, 1, 0, 0, 0));
    // 0001/02/03 00:01:02
    // 0b0000000000000000000000|00000000000001|0010|00011|00000|000001|000010
    // 0x4860042
    assertThat(decodePacked64DatetimeSeconds(0x4860042L))
        .isEqualTo(new LocalDateTime(1, 2, 3, 0, 1, 2));
    // 0001/01/01 12:00:00
    // 0b0000000000000000000000|00000000000001|0001|00001|01100|000000|000000
    // 0x442C000
    assertThat(decodePacked64DatetimeSeconds(0x442C000L))
        .isEqualTo(new LocalDateTime(1, 1, 1, 12, 0, 0));
    // 0001/01/01 13:14:15
    // 0b0000000000000000000000|00000000000001|0001|00001|01101|001110|001111
    // 0x442D38F
    assertThat(decodePacked64DatetimeSeconds(0x442D38FL))
        .isEqualTo(new LocalDateTime(1, 1, 1, 13, 14, 15));
    // 9999/12/31 23:59:59
    // 0b0000000000000000000000|10011100001111|1100|11111|10111|111011|111011
    // 0x9C3F3F7EFB
    assertThat(decodePacked64DatetimeSeconds(0x9C3F3F7EFBL))
        .isEqualTo(new LocalDateTime(9999, 12, 31, 23, 59, 59));
  }

  @Test
  public void decodePacked64DatetimeSecondsAsJavaTime_validBitFieldDatetimeSeconds() {
    // 0001/01/01 00:00:00
    // 0b0000000000000000000000|00000000000001|0001|00001|00000|000000|000000
    // 0x4420000
    assertThat(decodePacked64DatetimeSecondsAsJavaTime(0x4420000L))
        .isEqualTo(java.time.LocalDateTime.of(1, 1, 1, 0, 0, 0));
    // 0001/02/03 00:01:02
    // 0b0000000000000000000000|00000000000001|0010|00011|00000|000001|000010
    // 0x4860042
    assertThat(decodePacked64DatetimeSecondsAsJavaTime(0x4860042L))
        .isEqualTo(java.time.LocalDateTime.of(1, 2, 3, 0, 1, 2));
    // 0001/01/01 12:00:00
    // 0b0000000000000000000000|00000000000001|0001|00001|01100|000000|000000
    // 0x442C000
    assertThat(decodePacked64DatetimeSecondsAsJavaTime(0x442C000L))
        .isEqualTo(java.time.LocalDateTime.of(1, 1, 1, 12, 0, 0));
    // 0001/01/01 13:14:15
    // 0b0000000000000000000000|00000000000001|0001|00001|01101|001110|001111
    // 0x442D38F
    assertThat(decodePacked64DatetimeSecondsAsJavaTime(0x442D38FL))
        .isEqualTo(java.time.LocalDateTime.of(1, 1, 1, 13, 14, 15));
    // 9999/12/31 23:59:59
    // 0b0000000000000000000000|10011100001111|1100|11111|10111|111011|111011
    // 0x9C3F3F7EFB
    assertThat(decodePacked64DatetimeSecondsAsJavaTime(0x9C3F3F7EFBL))
        .isEqualTo(java.time.LocalDateTime.of(9999, 12, 31, 23, 59, 59));
  }

  @Test
  public void decodePacked64DatetimeSeconds_invalidBitField() {
    try {
      // 0001/01/01 00:00:00
      // 0b0000000000000000000001|00000000000001|0001|00001|00000|000000|000000
      // 0x10004420000
      decodePacked64DatetimeSeconds(0x10004420000L);
      fail();
    } catch (IllegalArgumentException expected) {
    }
  }

  @Test
  public void decodePacked64DatetimeSecondsAsJavaTime_invalidBitField() {
    try {
      // 0001/01/01 00:00:00
      // 0b0000000000000000000001|00000000000001|0001|00001|00000|000000|000000
      // 0x10004420000
      decodePacked64DatetimeSecondsAsJavaTime(0x10004420000L);
      fail();
    } catch (IllegalArgumentException expected) {
    }
  }

  @Test
  public void decodePacked64DatetimeSeconds_invalidSecondOfMinute_throwsIllegalArgumentException() {
    try {
      // 0001/01/01 00:00:60
      // 0b0000000000000000000000|00000000000001|0001|00001|00000|000000|111100
      // 0x442003C
      decodePacked64DatetimeSeconds(0x442003CL);
      fail();
    } catch (IllegalArgumentException expected) {
    }
  }

  @Test
  public void
      decodePacked64DatetimeSecondsAsJavaTime_invalidSecondOfMinute_throwsIllegalArgumentException() {
    try {
      // 0001/01/01 00:00:60
      // 0b0000000000000000000000|00000000000001|0001|00001|00000|000000|111100
      // 0x442003C
      decodePacked64DatetimeSecondsAsJavaTime(0x442003CL);
      fail();
    } catch (IllegalArgumentException expected) {
    }
  }

  @Test
  public void decodePacked64DatetimeSeconds_invalidMinuteOfHour_throwsIllegalArgumentException() {
    try {
      // 0001/01/01 00:60:00
      // 0b0000000000000000000000|00000000000001|0001|00001|00000|111100|000000
      // 0x4420F00
      decodePacked64DatetimeSecondsAsJavaTime(0x4420F00L);
      fail();
    } catch (IllegalArgumentException expected) {
    }
  }

  @Test
  public void
      decodePacked64DatetimeSecondsAsJavaTime_invalidMinuteOfHour_throwsIllegalArgumentException() {
    try {
      // 0001/01/01 00:60:00
      // 0b0000000000000000000000|00000000000001|0001|00001|00000|111100|000000
      // 0x4420F00
      decodePacked64DatetimeSecondsAsJavaTime(0x4420F00L);
      fail();
    } catch (IllegalArgumentException expected) {
    }
  }

  @Test
  public void decodePacked64DatetimeSeconds_invalidHourOfDay_throwsIllegalArgumentException() {
    try {
      // 0001/01/01 24:00:00
      // 0b0000000000000000000000|00000000000001|0001|00001|11000|000000|000000
      // 0x4438000
      decodePacked64DatetimeSeconds(0x4438000L);
      fail();
    } catch (IllegalArgumentException expected) {
    }
  }

  @Test
  public void
      decodePacked64DatetimeSecondsAsJavaTime_invalidHourOfDay_throwsIllegalArgumentException() {
    try {
      // 0001/01/01 24:00:00
      // 0b0000000000000000000000|00000000000001|0001|00001|11000|000000|000000
      // 0x4438000
      decodePacked64DatetimeSecondsAsJavaTime(0x4438000L);
      fail();
    } catch (IllegalArgumentException expected) {
    }
  }

  @Test
  public void decodePacked64DatetimeSeconds_invalidDayOfMonth_throwsIllegalArgumentException() {
    try {
      // 0001/01/00 00:00:00
      // 0b0000000000000000000000|00000000000001|0001|00000|00000|000000|000000
      // 0x4400000
      decodePacked64DatetimeSeconds(0x4400000L);
      fail();
    } catch (IllegalArgumentException expected) {
    }
  }

  @Test
  public void
      decodePacked64DatetimeSecondsAsJavaTime_invalidDayOfMonth_throwsIllegalArgumentException() {
    try {
      // 0001/01/00 00:00:00
      // 0b0000000000000000000000|00000000000001|0001|00000|00000|000000|000000
      // 0x4400000
      decodePacked64DatetimeSecondsAsJavaTime(0x4400000L);
      fail();
    } catch (IllegalArgumentException expected) {
    }
  }

  @Test
  public void decodePacked64DatetimeSeconds_invalidMonthOfYear_throwsIllegalArgumentException() {
    try {
      // 0001/13/01 00:00:00
      // 0b0000000000000000000000|00000000000001|1101|00001|00000|000000|000000
      // 0x7420000
      decodePacked64DatetimeSeconds(0x7420000L);
      fail();
    } catch (IllegalArgumentException expected) {
    }
  }

  @Test
  public void
      decodePacked64DatetimeSecondsAsJavaTime_invalidMonthOfYear_throwsIllegalArgumentException() {
    try {
      // 0001/13/01 00:00:00
      // 0b0000000000000000000000|00000000000001|1101|00001|00000|000000|000000
      // 0x7420000
      decodePacked64DatetimeSecondsAsJavaTime(0x7420000L);
      fail();
    } catch (IllegalArgumentException expected) {
    }
  }

  @Test
  public void decodePacked64DatetimeSeconds_invalidYear_throwsIllegalArgumentException() {
    try {
      // 10000/01/01 00:00:00
      // 0b0000000000000000000000|10011100010000|0001|00001|00000|000000|000000
      // 0x9C40420000
      decodePacked64DatetimeSeconds(0x9C40420000L);
      fail();
    } catch (IllegalArgumentException expected) {
    }
  }

  @Test
  public void decodePacked64DatetimeSecondsAsJavaTime_invalidYear_throwsIllegalArgumentException() {
    try {
      // 10000/01/01 00:00:00
      // 0b0000000000000000000000|10011100010000|0001|00001|00000|000000|000000
      // 0x9C40420000
      decodePacked64DatetimeSecondsAsJavaTime(0x9C40420000L);
      fail();
    } catch (IllegalArgumentException expected) {
    }
  }

  @Test
  public void encodePacked64DatetimeMicros_validDateTime() {
    // 0001/01/01 00:00:00.000000
    // 0b00|00000000000001|0001|00001|00000|000000|000000|00000000000000000000
    // 0x442000000000
    assertThat(encodePacked64DatetimeMicros(new LocalDateTime(1, 1, 1, 0, 0, 0, 0)))
        .isEqualTo(0x442000000000L);
    // 0001/02/03 00:01:02.003000
    // 0b00|00000000000001|0010|00011|00000|000001|000010|00000000101110111000
    // 0x486004200BB8
    assertThat(encodePacked64DatetimeMicros(new LocalDateTime(1, 2, 3, 0, 1, 2, 3)))
        .isEqualTo(0x486004200BB8L);
    // 0001/01/01 12:00:00.000000
    // 0b00|00000000000001|0001|00001|01100|000000|000000|00000000000000000000
    // 0x442C00000000
    assertThat(encodePacked64DatetimeMicros(new LocalDateTime(1, 1, 1, 12, 0, 0, 0)))
        .isEqualTo(0x442C00000000L);
    // 0001/01/01 13:14:15.016000
    // 0b00|00000000000001|0001|00001|01101|001110|001111|00000011111010000000
    // 0x442D38F03E80
    assertThat(encodePacked64DatetimeMicros(new LocalDateTime(1, 1, 1, 13, 14, 15, 16)))
        .isEqualTo(0x442D38F03E80L);
    // 9999/12/31 23:59:59.999000
    // 0b00|10011100001111|1100|11111|10111|111011|111011|11110011111001011000
    // 0x9C3F3F7EFBF3E58
    assertThat(encodePacked64DatetimeMicros(new LocalDateTime(9999, 12, 31, 23, 59, 59, 999)))
        .isEqualTo(0x9C3F3F7EFBF3E58L);
  }

  @Test
  public void encodePacked64DatetimeMicrosAsJavaTime_validDateTime() {
    // 0001/01/01 00:00:00.000000
    // 0b00|00000000000001|0001|00001|00000|000000|000000|00000000000000000000
    // 0x442000000000
    assertThat(encodePacked64DatetimeMicros(java.time.LocalDateTime.of(1, 1, 1, 0, 0, 0, 0)))
        .isEqualTo(0x442000000000L);
    // 0001/02/03 00:01:02.003000
    // 0b00|00000000000001|0010|00011|00000|000001|000010|00000000101110111000
    // 0x486004200BB8
    assertThat(
            encodePacked64DatetimeMicros(java.time.LocalDateTime.of(1, 2, 3, 0, 1, 2, 3_000_000)))
        .isEqualTo(0x486004200BB8L);
    // 0001/01/01 12:00:00.000000
    // 0b00|00000000000001|0001|00001|01100|000000|000000|00000000000000000000
    // 0x442C00000000
    assertThat(encodePacked64DatetimeMicros(java.time.LocalDateTime.of(1, 1, 1, 12, 0, 0, 0)))
        .isEqualTo(0x442C00000000L);
    // 0001/01/01 13:14:15.016000
    // 0b00|00000000000001|0001|00001|01101|001110|001111|00000011111010000000
    // 0x442D38F03E80
    assertThat(
            encodePacked64DatetimeMicros(
                java.time.LocalDateTime.of(1, 1, 1, 13, 14, 15, 16_000_000)))
        .isEqualTo(0x442D38F03E80L);
    // 9999/12/31 23:59:59.999000
    // 0b00|10011100001111|1100|11111|10111|111011|111011|11110011111001011000
    // 0x9C3F3F7EFBF3E58
    assertThat(
            encodePacked64DatetimeMicros(
                java.time.LocalDateTime.of(9999, 12, 31, 23, 59, 59, 999_000_000)))
        .isEqualTo(0x9C3F3F7EFBF3E58L);
  }

  @Test
  public void encodePacked64DatetimeMicros_invalidYear_throwsIllegalArgumentException() {
    // 10000/01/01 00:00:00.000000
    // 0b00|10011100010000|0001|00001|00000|000000|000000|00000000000000000000
    // 0x9C4042000000000
    LocalDateTime dateTime = new LocalDateTime(10000, 1, 1, 0, 0, 0, 0);
    try {
      encodePacked64DatetimeMicros(dateTime);
      fail();
    } catch (IllegalArgumentException expected) {
    }
  }

  @Test
  public void encodePacked64DatetimeMicrosAsJavaTime_invalidYear_throwsIllegalArgumentException() {
    // 10000/01/01 00:00:00.000000
    // 0b00|10011100010000|0001|00001|00000|000000|000000|00000000000000000000
    // 0x9C4042000000000
    java.time.LocalDateTime dateTime = java.time.LocalDateTime.of(10000, 1, 1, 0, 0, 0, 0);
    try {
      encodePacked64DatetimeMicros(dateTime);
      fail();
    } catch (IllegalArgumentException expected) {
    }
  }

  @Test
  public void decodePacked64DatetimeMicros_validBitFieldDatetimeMicros() {
    // 0001/01/01 00:00:00.000000
    // 0b00|00000000000001|0001|00001|00000|000000|000000|00000000000000000000
    // 0x442000000000
    assertThat(decodePacked64DatetimeMicros(0x442000000000L))
        .isEqualTo(new LocalDateTime(1, 1, 1, 0, 0, 0, 0));
    // 0001/02/03 00:01:02.000003
    // 0b00|00000000000001|0010|00011|00000|000001|000010|00000000000000000011
    // 0x486004200003
    assertThat(decodePacked64DatetimeMicros(0x486004200003L))
        .isEqualTo(new LocalDateTime(1, 2, 3, 0, 1, 2, 0));
    // 0001/01/01 12:00:00.000000
    // 0b00|00000000000001|0001|00001|01100|000000|000000|00000000000000000000
    // 0x442C00000000
    assertThat(decodePacked64DatetimeMicros(0x442C00000000L))
        .isEqualTo(new LocalDateTime(1, 1, 1, 12, 0, 0, 0));
    // 0001/01/01 13:14:15.000016
    // 0b00|00000000000001|0001|00001|01101|001110|001111|00000000000000010000
    // 0x442D38F00010
    assertThat(decodePacked64DatetimeMicros(0x442D38F00010L))
        .isEqualTo(new LocalDateTime(1, 1, 1, 13, 14, 15, 0));
    // 9999/12/31 23:59:59.999999
    // 0b00|10011100001111|1100|11111|10111|111011|111011|11110100001000111111
    // 0x9C3F3F7EFBF423F
    assertThat(decodePacked64DatetimeMicros(0x9C3F3F7EFBF423FL))
        .isEqualTo(new LocalDateTime(9999, 12, 31, 23, 59, 59, 999));
  }

  @Test
  public void decodePacked64DatetimeMicrosAsJavaTime_validBitFieldDatetimeMicros() {
    // 0001/01/01 00:00:00.000000
    // 0b00|00000000000001|0001|00001|00000|000000|000000|00000000000000000000
    // 0x442000000000
    assertThat(decodePacked64DatetimeMicrosAsJavaTime(0x442000000000L))
        .isEqualTo(java.time.LocalDateTime.of(1, 1, 1, 0, 0, 0, 0));
    // 0001/02/03 00:01:02.000003
    // 0b00|00000000000001|0010|00011|00000|000001|000010|00000000000000000011
    // 0x486004200003
    assertThat(decodePacked64DatetimeMicrosAsJavaTime(0x486004200003L))
        .isEqualTo(java.time.LocalDateTime.of(1, 2, 3, 0, 1, 2, 3_000));
    // 0001/01/01 12:00:00.000000
    // 0b00|00000000000001|0001|00001|01100|000000|000000|00000000000000000000
    // 0x442C00000000
    assertThat(decodePacked64DatetimeMicrosAsJavaTime(0x442C00000000L))
        .isEqualTo(java.time.LocalDateTime.of(1, 1, 1, 12, 0, 0, 0));
    // 0001/01/01 13:14:15.000016
    // 0b00|00000000000001|0001|00001|01101|001110|001111|00000000000000010000
    // 0x442D38F00010
    assertThat(decodePacked64DatetimeMicrosAsJavaTime(0x442D38F00010L))
        .isEqualTo(java.time.LocalDateTime.of(1, 1, 1, 13, 14, 15, 16_000));
    // 9999/12/31 23:59:59.999999
    // 0b00|10011100001111|1100|11111|10111|111011|111011|11110100001000111111
    // 0x9C3F3F7EFBF423F
    assertThat(decodePacked64DatetimeMicrosAsJavaTime(0x9C3F3F7EFBF423FL))
        .isEqualTo(java.time.LocalDateTime.of(9999, 12, 31, 23, 59, 59, 999_999_000));
  }

  @Test
  public void decodePacked64DatetimeMicros_invalidBitField() {
    try {
      // 0001/01/01 00:00:00.000000
      // 0b01|00000000000001|0001|00001|00000|000000|000000|00000000000000000000
      // 0x1000442000000000
      decodePacked64DatetimeMicros(0x1000442000000000L);
      fail();
    } catch (IllegalArgumentException expected) {
    }
  }

  @Test
  public void decodePacked64DatetimeMicrosAsJavaTime_invalidBitField() {
    try {
      // 0001/01/01 00:00:00.000000
      // 0b01|00000000000001|0001|00001|00000|000000|000000|00000000000000000000
      // 0x1000442000000000
      decodePacked64DatetimeMicrosAsJavaTime(0x1000442000000000L);
      fail();
    } catch (IllegalArgumentException expected) {
    }
  }

  @Test
  public void decodePacked64DatetimeMicros_invalidMicroOfSecond_throwsIllegalArgumentException() {
    try {
      // 0001/01/01 00:00:00.1000000
      // 0b00|00000000000001|0001|00001|00000|000000|000000|11110100001001000000
      // 0x4420000F4240
      decodePacked64DatetimeMicros(0x4420000F4240L);
      fail();
    } catch (IllegalArgumentException expected) {
    }
  }

  @Test
  public void
      decodePacked64DatetimeMicrosAsJavaTime_invalidMicroOfSecond_throwsIllegalArgumentException() {
    try {
      // 0001/01/01 00:00:00.1000000
      // 0b00|00000000000001|0001|00001|00000|000000|000000|11110100001001000000
      // 0x4420000F4240
      decodePacked64DatetimeMicrosAsJavaTime(0x4420000F4240L);
      fail();
    } catch (IllegalArgumentException expected) {
    }
  }

  @Test
  public void decodePacked64DatetimeMicros_invalidSecondOfMinute_throwsIllegalArgumentException() {
    try {
      // 0001/01/01 00:00:60.000000
      // 0b00|00000000000001|0001|00001|00000|000000|111100|00000000000000000000
      // 0x442003C00000
      decodePacked64DatetimeMicros(0x442003C00000L);
      fail();
    } catch (IllegalArgumentException expected) {
    }
  }

  @Test
  public void
      decodePacked64DatetimeMicrosAsJavaTime_invalidSecondOfMinute_throwsIllegalArgumentException() {
    try {
      // 0001/01/01 00:00:60.000000
      // 0b00|00000000000001|0001|00001|00000|000000|111100|00000000000000000000
      // 0x442003C00000
      decodePacked64DatetimeMicrosAsJavaTime(0x442003C00000L);
      fail();
    } catch (IllegalArgumentException expected) {
    }
  }

  @Test
  public void decodePacked64DatetimeMicros_invalidMinuteOfHour_throwsIllegalArgumentException() {
    try {
      // 0001/01/01 00:60:00.000000
      // 0b00|00000000000001|0001|00001|00000|111100|000000|00000000000000000000
      // 0x4420F0000000
      decodePacked64DatetimeMicros(0x4420F0000000L);
      fail();
    } catch (IllegalArgumentException expected) {
    }
  }

  @Test
  public void
      decodePacked64DatetimeMicrosAsJavaTime_invalidMinuteOfHour_throwsIllegalArgumentException() {
    try {
      // 0001/01/01 00:60:00.000000
      // 0b00|00000000000001|0001|00001|00000|111100|000000|00000000000000000000
      // 0x4420F0000000
      decodePacked64DatetimeMicrosAsJavaTime(0x4420F0000000L);
      fail();
    } catch (IllegalArgumentException expected) {
    }
  }

  @Test
  public void decodePacked64DatetimeMicros_invalidHourOfDay_throwsIllegalArgumentException() {
    try {
      // 0001/01/01 24:00:00.000000
      // 0b00|00000000000001|0001|00001|11000|000000|000000|00000000000000000000
      // 0x443800000000
      decodePacked64DatetimeMicros(0x443800000000L);
      fail();
    } catch (IllegalArgumentException expected) {
    }
  }

  @Test
  public void
      decodePacked64DatetimeMicrosAsJavaTime_invalidHourOfDay_throwsIllegalArgumentException() {
    try {
      // 0001/01/01 24:00:00.000000
      // 0b00|00000000000001|0001|00001|11000|000000|000000|00000000000000000000
      // 0x443800000000
      decodePacked64DatetimeMicrosAsJavaTime(0x443800000000L);
      fail();
    } catch (IllegalArgumentException expected) {
    }
  }

  @Test
  public void decodePacked64DatetimeMicros_invalidDayOfMonth_throwsIllegalArgumentException() {
    try {
      // 0001/01/00 00:00:00.000000
      // 0b00|00000000000001|0001|00000|00000|000000|000000|00000000000000000000
      // 0x440000000000
      decodePacked64DatetimeMicros(0x440000000000L);
      fail();
    } catch (IllegalArgumentException expected) {
    }
  }

  @Test
  public void
      decodePacked64DatetimeMicrosAsJavaTime_invalidDayOfMonth_throwsIllegalArgumentException() {
    try {
      // 0001/01/00 00:00:00.000000
      // 0b00|00000000000001|0001|00000|00000|000000|000000|00000000000000000000
      // 0x440000000000
      decodePacked64DatetimeMicrosAsJavaTime(0x440000000000L);
      fail();
    } catch (IllegalArgumentException expected) {
    }
  }

  @Test
  public void decodePacked64DatetimeMicros_invalidMonthOfYear_throwsIllegalArgumentException() {
    try {
      // 0001/13/01 00:00:00.000000
      // 0b00|00000000000001|1101|00001|00000|000000|000000|00000000000000000000
      // 0x742000000000
      decodePacked64DatetimeMicros(0x742000000000L);
      fail();
    } catch (IllegalArgumentException expected) {
    }
  }

  @Test
  public void
      decodePacked64DatetimeMicrosAsJavaTime_invalidMonthOfYear_throwsIllegalArgumentException() {
    try {
      // 0001/13/01 00:00:00.000000
      // 0b00|00000000000001|1101|00001|00000|000000|000000|00000000000000000000
      // 0x742000000000
      decodePacked64DatetimeMicrosAsJavaTime(0x742000000000L);
      fail();
    } catch (IllegalArgumentException expected) {
    }
  }

  @Test
  public void decodePacked64DatetimeMicros_invalidYear_throwsIllegalArgumentException() {
    try {
      // 10000/01/01 00:00:00.000000
      // 0b00|10011100010000|0001|00001|00000|000000|000000|00000000000000000000
      // 0x9C4042000000000
      decodePacked64DatetimeMicros(0x9C4042000000000L);
      fail();
    } catch (IllegalArgumentException expected) {
    }
  }

  @Test
  public void decodePacked64DatetimeMicrosAsJavaTime_invalidYear_throwsIllegalArgumentException() {
    try {
      // 10000/01/01 00:00:00.000000
      // 0b00|10011100010000|0001|00001|00000|000000|000000|00000000000000000000
      // 0x9C4042000000000
      decodePacked64DatetimeMicrosAsJavaTime(0x9C4042000000000L);
      fail();
    } catch (IllegalArgumentException expected) {
    }
  }

  @Test
  public void encodePacked96DatetimeNanos_validDateTime() {
    // 0001/01/01 00:00:00.000000000
    // 0b0000000000000000000000|00000000000001|0001|00001|00000|000000|000000, 0
    // 0x4420000, 0
    assertThat(encodePacked96DatetimeNanos(new LocalDateTime(1, 1, 1, 0, 0, 0, 0)))
        .isEqualTo(
            ValueProto.Datetime.newBuilder()
                .setBitFieldDatetimeSeconds(0x4420000L)
                .setNanos(0)
                .build());
    // 0001/02/03 00:01:02.003000000
    // 0b0000000000000000000000|00000000000001|0010|00011|00000|000001|000010, 3000000
    // 0x4860042, 3000000
    assertThat(encodePacked96DatetimeNanos(new LocalDateTime(1, 2, 3, 0, 1, 2, 3)))
        .isEqualTo(
            ValueProto.Datetime.newBuilder()
                .setBitFieldDatetimeSeconds(0x4860042L)
                .setNanos(3000000)
                .build());
    // 0001/01/01 12:00:00.000000000
    // 0b0000000000000000000000|00000000000001|0001|00001|01100|000000|000000, 0
    // 0x442C000, 0
    assertThat(encodePacked96DatetimeNanos(new LocalDateTime(1, 1, 1, 12, 0, 0, 0)))
        .isEqualTo(
            ValueProto.Datetime.newBuilder()
                .setBitFieldDatetimeSeconds(0x442C000L)
                .setNanos(0)
                .build());
    // 0001/01/01 13:14:15.016000000
    // 0b0000000000000000000000|00000000000001|0001|00001|01101|001110|001111, 16000000
    // 0x442D38F, 16000000
    assertThat(encodePacked96DatetimeNanos(new LocalDateTime(1, 1, 1, 13, 14, 15, 16)))
        .isEqualTo(
            ValueProto.Datetime.newBuilder()
                .setBitFieldDatetimeSeconds(0x442D38FL)
                .setNanos(16000000)
                .build());
    // 9999/12/31 23:59:59.999000000
    // 0b0000000000000000000000|10011100001111|1100|11111|10111|111011|111011, 999000000
    // 0x9C3F3F7EFB, 999000000
    assertThat(encodePacked96DatetimeNanos(new LocalDateTime(9999, 12, 31, 23, 59, 59, 999)))
        .isEqualTo(
            ValueProto.Datetime.newBuilder()
                .setBitFieldDatetimeSeconds(0x9C3F3F7EFBL)
                .setNanos(999000000)
                .build());
  }

  @Test
  public void encodePacked96DatetimeNanosAsJavaTime_validDateTime() {
    // 0001/01/01 00:00:00.000000000
    // 0b0000000000000000000000|00000000000001|0001|00001|00000|000000|000000, 0
    // 0x4420000, 0
    assertThat(encodePacked96DatetimeNanos(java.time.LocalDateTime.of(1, 1, 1, 0, 0, 0, 0)))
        .isEqualTo(
            ValueProto.Datetime.newBuilder()
                .setBitFieldDatetimeSeconds(0x4420000L)
                .setNanos(0)
                .build());
    // 0001/02/03 00:01:02.003000000
    // 0b0000000000000000000000|00000000000001|0010|00011|00000|000001|000010, 3000000
    // 0x4860042, 3000000
    assertThat(encodePacked96DatetimeNanos(java.time.LocalDateTime.of(1, 2, 3, 0, 1, 2, 3_000_000)))
        .isEqualTo(
            ValueProto.Datetime.newBuilder()
                .setBitFieldDatetimeSeconds(0x4860042L)
                .setNanos(3000000)
                .build());
    // 0001/01/01 12:00:00.000000000
    // 0b0000000000000000000000|00000000000001|0001|00001|01100|000000|000000, 0
    // 0x442C000, 0
    assertThat(encodePacked96DatetimeNanos(java.time.LocalDateTime.of(1, 1, 1, 12, 0, 0, 0)))
        .isEqualTo(
            ValueProto.Datetime.newBuilder()
                .setBitFieldDatetimeSeconds(0x442C000L)
                .setNanos(0)
                .build());
    // 0001/01/01 13:14:15.016000000
    // 0b0000000000000000000000|00000000000001|0001|00001|01101|001110|001111, 16000000
    // 0x442D38F, 16000000
    assertThat(
            encodePacked96DatetimeNanos(
                java.time.LocalDateTime.of(1, 1, 1, 13, 14, 15, 16_000_000)))
        .isEqualTo(
            ValueProto.Datetime.newBuilder()
                .setBitFieldDatetimeSeconds(0x442D38FL)
                .setNanos(16000000)
                .build());
    // 9999/12/31 23:59:59.999000000
    // 0b0000000000000000000000|10011100001111|1100|11111|10111|111011|111011, 999000000
    // 0x9C3F3F7EFB, 999000000
    assertThat(
            encodePacked96DatetimeNanos(
                java.time.LocalDateTime.of(9999, 12, 31, 23, 59, 59, 999_000_000)))
        .isEqualTo(
            ValueProto.Datetime.newBuilder()
                .setBitFieldDatetimeSeconds(0x9C3F3F7EFBL)
                .setNanos(999000000)
                .build());
  }

  @Test
  public void encodePacked96DatetimeNanos_invalidYear_throwsIllegalArgumentException() {
    // 10000/01/01 00:00:00.000000000
    // 0b0000000000000000000000|10011100010000|0001|00001|00000|000000|000000, 0
    // 0x9C40420000, 0
    LocalDateTime dateTime = new LocalDateTime(10000, 1, 1, 0, 0, 0, 0);
    try {
      encodePacked96DatetimeNanos(dateTime);
      fail();
    } catch (IllegalArgumentException expected) {
    }
  }

  @Test
  public void encodePacked96DatetimeNanosAsJavaTime_invalidYear_throwsIllegalArgumentException() {
    // 10000/01/01 00:00:00.000000000
    // 0b0000000000000000000000|10011100010000|0001|00001|00000|000000|000000, 0
    // 0x9C40420000, 0
    java.time.LocalDateTime dateTime = java.time.LocalDateTime.of(10000, 1, 1, 0, 0, 0, 0);
    try {
      encodePacked96DatetimeNanos(dateTime);
      fail();
    } catch (IllegalArgumentException expected) {
    }
  }

  @Test
  public void decodePacked96DatetimeNanos_validBitFieldDatetimeNanos() {
    // 0001/01/01 00:00:00.000000000
    // 0b0000000000000000000000|00000000000001|0001|00001|00000|000000|000000, 0
    // 0x4420000, 0
    assertThat(
            decodePacked96DatetimeNanos(
                ValueProto.Datetime.newBuilder()
                    .setBitFieldDatetimeSeconds(0x4420000L)
                    .setNanos(0)
                    .build()))
        .isEqualTo(new LocalDateTime(1, 1, 1, 0, 0, 0, 0));
    // 0001/02/03 00:01:02.000000003
    // 0b0000000000000000000000|00000000000001|0010|00011|00000|000001|000010, 3
    // 0x4860042, 3
    assertThat(
            decodePacked96DatetimeNanos(
                ValueProto.Datetime.newBuilder()
                    .setBitFieldDatetimeSeconds(0x4860042L)
                    .setNanos(3)
                    .build()))
        .isEqualTo(new LocalDateTime(1, 2, 3, 0, 1, 2, 0));
    // 0001/01/01 12:00:00.000000000
    // 0b0000000000000000000000|00000000000001|0001|00001|01100|000000|000000, 0
    // 0x442C000, 0
    assertThat(
            decodePacked96DatetimeNanos(
                ValueProto.Datetime.newBuilder()
                    .setBitFieldDatetimeSeconds(0x442C000L)
                    .setNanos(0)
                    .build()))
        .isEqualTo(new LocalDateTime(1, 1, 1, 12, 0, 0, 0));
    // 0001/01/01 13:14:15.000000016
    // 0b0000000000000000000000|00000000000001|0001|00001|01101|001110|001111, 16
    // 0x442D38F, 16
    assertThat(
            decodePacked96DatetimeNanos(
                ValueProto.Datetime.newBuilder()
                    .setBitFieldDatetimeSeconds(0x442D38FL)
                    .setNanos(16)
                    .build()))
        .isEqualTo(new LocalDateTime(1, 1, 1, 13, 14, 15, 0));
    // 9999/12/31 23:59:59.999999999
    // 0b0000000000000000000000|10011100001111|1100|11111|10111|111011|111011, 999999999
    // 0x9C3F3F7EFB, 999999999
    assertThat(
            decodePacked96DatetimeNanos(
                ValueProto.Datetime.newBuilder()
                    .setBitFieldDatetimeSeconds(0x9C3F3F7EFBL)
                    .setNanos(999999999)
                    .build()))
        .isEqualTo(new LocalDateTime(9999, 12, 31, 23, 59, 59, 999));
  }

  @Test
  public void decodePacked96DatetimeNanosAsJavaTime_validBitFieldDatetimeNanos() {
    // 0001/01/01 00:00:00.000000000
    // 0b0000000000000000000000|00000000000001|0001|00001|00000|000000|000000, 0
    // 0x4420000, 0
    assertThat(
            decodePacked96DatetimeNanosAsJavaTime(
                ValueProto.Datetime.newBuilder()
                    .setBitFieldDatetimeSeconds(0x4420000L)
                    .setNanos(0)
                    .build()))
        .isEqualTo(java.time.LocalDateTime.of(1, 1, 1, 0, 0, 0, 0));
    // 0001/02/03 00:01:02.000000003
    // 0b0000000000000000000000|00000000000001|0010|00011|00000|000001|000010, 3
    // 0x4860042, 3
    assertThat(
            decodePacked96DatetimeNanosAsJavaTime(
                ValueProto.Datetime.newBuilder()
                    .setBitFieldDatetimeSeconds(0x4860042L)
                    .setNanos(3)
                    .build()))
        .isEqualTo(java.time.LocalDateTime.of(1, 2, 3, 0, 1, 2, 3));
    // 0001/01/01 12:00:00.000000000
    // 0b0000000000000000000000|00000000000001|0001|00001|01100|000000|000000, 0
    // 0x442C000, 0
    assertThat(
            decodePacked96DatetimeNanosAsJavaTime(
                ValueProto.Datetime.newBuilder()
                    .setBitFieldDatetimeSeconds(0x442C000L)
                    .setNanos(0)
                    .build()))
        .isEqualTo(java.time.LocalDateTime.of(1, 1, 1, 12, 0, 0, 0));
    // 0001/01/01 13:14:15.000000016
    // 0b0000000000000000000000|00000000000001|0001|00001|01101|001110|001111, 16
    // 0x442D38F, 16
    assertThat(
            decodePacked96DatetimeNanosAsJavaTime(
                ValueProto.Datetime.newBuilder()
                    .setBitFieldDatetimeSeconds(0x442D38FL)
                    .setNanos(16)
                    .build()))
        .isEqualTo(java.time.LocalDateTime.of(1, 1, 1, 13, 14, 15, 16));
    // 9999/12/31 23:59:59.999999999
    // 0b0000000000000000000000|10011100001111|1100|11111|10111|111011|111011, 999999999
    // 0x9C3F3F7EFB, 999999999
    assertThat(
            decodePacked96DatetimeNanosAsJavaTime(
                ValueProto.Datetime.newBuilder()
                    .setBitFieldDatetimeSeconds(0x9C3F3F7EFBL)
                    .setNanos(999999999)
                    .build()))
        .isEqualTo(java.time.LocalDateTime.of(9999, 12, 31, 23, 59, 59, 999_999_999));
  }

  @Test
  public void decodePacked96DatetimeNanos_invalidBitField() {
    // 0001/01/01 00:00:00.000000000
    // 0b0000000000000000000001|00000000000001|0001|00001|00000|000000|000000, 0
    // 0x10004420000, 0
    ValueProto.Datetime datetime =
        ValueProto.Datetime.newBuilder()
            .setBitFieldDatetimeSeconds(0x10004420000L)
            .setNanos(0)
            .build();
    try {
      decodePacked96DatetimeNanos(datetime);
      fail();
    } catch (IllegalArgumentException expected) {
    }
  }

  @Test
  public void decodePacked96DatetimeNanosAsJavaTime_invalidBitField() {
    // 0001/01/01 00:00:00.000000000
    // 0b0000000000000000000001|00000000000001|0001|00001|00000|000000|000000, 0
    // 0x10004420000, 0
    ValueProto.Datetime datetime =
        ValueProto.Datetime.newBuilder()
            .setBitFieldDatetimeSeconds(0x10004420000L)
            .setNanos(0)
            .build();
    try {
      decodePacked96DatetimeNanosAsJavaTime(datetime);
      fail();
    } catch (IllegalArgumentException expected) {
    }
  }

  @Test
  public void decodePacked96DatetimeNanos_invalidNanoOfSecond_throwsIllegalArgumentException() {
    // 0001/01/01 00:00:00.1000000000
    // 0b0000000000000000000000|00000000000001|0001|00001|00000|000000|000000, 1000000000
    // 0x10004420000, 1000000000
    ValueProto.Datetime datetime =
        ValueProto.Datetime.newBuilder()
            .setBitFieldDatetimeSeconds(0x10004420000L)
            .setNanos(1000000000)
            .build();
    try {
      decodePacked96DatetimeNanos(datetime);
      fail();
    } catch (IllegalArgumentException expected) {
    }
  }

  @Test
  public void
      decodePacked96DatetimeNanosAsJavaTime_invalidNanoOfSecond_throwsIllegalArgumentException() {
    // 0001/01/01 00:00:00.1000000000
    // 0b0000000000000000000000|00000000000001|0001|00001|00000|000000|000000, 1000000000
    // 0x10004420000, 1000000000
    ValueProto.Datetime datetime =
        ValueProto.Datetime.newBuilder()
            .setBitFieldDatetimeSeconds(0x10004420000L)
            .setNanos(1000000000)
            .build();
    try {
      decodePacked96DatetimeNanosAsJavaTime(datetime);
      fail();
    } catch (IllegalArgumentException expected) {
    }
  }

  @Test
  public void decodePacked96DatetimeNanos_invalidSecondOfMinute_throwsIllegalArgumentException() {
    // 0001/01/01 00:00:60.000000000
    // 0b0000000000000000000000|00000000000001|0001|00001|00000|000000|111100, 0
    // 0x442003C, 0
    ValueProto.Datetime datetime =
        ValueProto.Datetime.newBuilder().setBitFieldDatetimeSeconds(0x442003CL).setNanos(0).build();
    try {
      decodePacked96DatetimeNanos(datetime);
      fail();
    } catch (IllegalArgumentException expected) {
    }
  }

  @Test
  public void
      decodePacked96DatetimeNanosAsJavaTime_invalidSecondOfMinute_throwsIllegalArgumentException() {
    // 0001/01/01 00:00:60.000000000
    // 0b0000000000000000000000|00000000000001|0001|00001|00000|000000|111100, 0
    // 0x442003C, 0
    ValueProto.Datetime datetime =
        ValueProto.Datetime.newBuilder().setBitFieldDatetimeSeconds(0x442003CL).setNanos(0).build();
    try {
      decodePacked96DatetimeNanosAsJavaTime(datetime);
      fail();
    } catch (IllegalArgumentException expected) {
    }
  }

  @Test
  public void decodePacked96DatetimeNanos_invalidMinuteOfHour_throwsIllegalArgumentException() {
    // 0001/01/01 00:60:00.000000000
    // 0b0000000000000000000000|00000000000001|0001|00001|00000|111100|000000, 0
    // 0x4420F00, 0
    ValueProto.Datetime datetime =
        ValueProto.Datetime.newBuilder().setBitFieldDatetimeSeconds(0x4420F00L).setNanos(0).build();
    try {
      decodePacked96DatetimeNanos(datetime);
      fail();
    } catch (IllegalArgumentException expected) {
    }
  }

  @Test
  public void
      decodePacked96DatetimeNanosAsJavaTime_invalidMinuteOfHour_throwsIllegalArgumentException() {
    // 0001/01/01 00:60:00.000000000
    // 0b0000000000000000000000|00000000000001|0001|00001|00000|111100|000000, 0
    // 0x4420F00, 0
    ValueProto.Datetime datetime =
        ValueProto.Datetime.newBuilder().setBitFieldDatetimeSeconds(0x4420F00L).setNanos(0).build();
    try {
      decodePacked96DatetimeNanosAsJavaTime(datetime);
      fail();
    } catch (IllegalArgumentException expected) {
    }
  }

  @Test
  public void decodePacked96DatetimeNanos_invalidHourOfDay_throwsIllegalArgumentException() {
    // 0001/01/01 24:00:00.000000000
    // 0b0000000000000000000000|00000000000001|0001|00001|11000|000000|000000, 0
    // 0x4438000, 0
    ValueProto.Datetime datetime =
        ValueProto.Datetime.newBuilder().setBitFieldDatetimeSeconds(0x4438000L).setNanos(0).build();
    try {
      decodePacked96DatetimeNanos(datetime);
      fail();
    } catch (IllegalArgumentException expected) {
    }
  }

  @Test
  public void
      decodePacked96DatetimeNanosAsJavaTime_invalidHourOfDay_throwsIllegalArgumentException() {
    // 0001/01/01 24:00:00.000000000
    // 0b0000000000000000000000|00000000000001|0001|00001|11000|000000|000000, 0
    // 0x4438000, 0
    ValueProto.Datetime datetime =
        ValueProto.Datetime.newBuilder().setBitFieldDatetimeSeconds(0x4438000L).setNanos(0).build();
    try {
      decodePacked96DatetimeNanosAsJavaTime(datetime);
      fail();
    } catch (IllegalArgumentException expected) {
    }
  }

  @Test
  public void decodePacked96DatetimeNanos_invalidDayOfMonth_throwsIllegalArgumentException() {
    // 0001/01/00 00:00:00.000000000
    // 0b0000000000000000000000|00000000000001|0001|00000|00000|000000|000000, 0
    // 0x4400000, 0
    ValueProto.Datetime datetime =
        ValueProto.Datetime.newBuilder().setBitFieldDatetimeSeconds(0x4400000L).setNanos(0).build();
    try {
      decodePacked96DatetimeNanos(datetime);
      fail();
    } catch (IllegalArgumentException expected) {
    }
  }

  @Test
  public void
      decodePacked96DatetimeNanosAsJavaTime_invalidDayOfMonth_throwsIllegalArgumentException() {
    // 0001/01/00 00:00:00.000000000
    // 0b0000000000000000000000|00000000000001|0001|00000|00000|000000|000000, 0
    // 0x4400000, 0
    ValueProto.Datetime datetime =
        ValueProto.Datetime.newBuilder().setBitFieldDatetimeSeconds(0x4400000L).setNanos(0).build();
    try {
      decodePacked96DatetimeNanosAsJavaTime(datetime);
      fail();
    } catch (IllegalArgumentException expected) {
    }
  }

  @Test
  public void decodePacked96DatetimeNanos_invalidMonthOfYear_throwsIllegalArgumentException() {
    // 0001/13/01 00:00:00.000000000
    // 0b0000000000000000000000|00000000000001|1101|00001|00000|000000|000000, 0
    // 0x7420000, 0
    ValueProto.Datetime datetime =
        ValueProto.Datetime.newBuilder().setBitFieldDatetimeSeconds(0x7420000L).setNanos(0).build();
    try {
      decodePacked96DatetimeNanos(datetime);
      fail();
    } catch (IllegalArgumentException expected) {
    }
  }

  @Test
  public void
      decodePacked96DatetimeNanosAsJavaTime_invalidMonthOfYear_throwsIllegalArgumentException() {
    // 0001/13/01 00:00:00.000000000
    // 0b0000000000000000000000|00000000000001|1101|00001|00000|000000|000000, 0
    // 0x7420000, 0
    ValueProto.Datetime datetime =
        ValueProto.Datetime.newBuilder().setBitFieldDatetimeSeconds(0x7420000L).setNanos(0).build();
    try {
      decodePacked96DatetimeNanosAsJavaTime(datetime);
      fail();
    } catch (IllegalArgumentException expected) {
    }
  }

  @Test
  public void decodePacked96DatetimeNanos_invalidYear_throwsIllegalArgumentException() {
    // 10000/01/01 00:00:00.000000000
    // 0b0000000000000000000000|10011100010000|0001|00001|00000|000000|000000, 0
    // 0x9C40420000, 0
    ValueProto.Datetime datetime =
        ValueProto.Datetime.newBuilder()
            .setBitFieldDatetimeSeconds(0x9C40420000L)
            .setNanos(0)
            .build();
    try {
      decodePacked96DatetimeNanos(datetime);
      fail();
    } catch (IllegalArgumentException expected) {
    }
  }

  @Test
  public void decodePacked96DatetimeNanosAsJavaTime_invalidYear_throwsIllegalArgumentException() {
    // 10000/01/01 00:00:00.000000000
    // 0b0000000000000000000000|10011100010000|0001|00001|00000|000000|000000, 0
    // 0x9C40420000, 0
    ValueProto.Datetime datetime =
        ValueProto.Datetime.newBuilder()
            .setBitFieldDatetimeSeconds(0x9C40420000L)
            .setNanos(0)
            .build();
    try {
      decodePacked96DatetimeNanosAsJavaTime(datetime);
      fail();
    } catch (IllegalArgumentException expected) {
    }
  }
}
