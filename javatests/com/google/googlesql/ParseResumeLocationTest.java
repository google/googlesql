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
import static com.google.common.truth.Truth.assertWithMessage;

import com.google.common.testing.EqualsTester;
import com.google.googlesql.GoogleSQLParser.ParseResumeLocationProto;


import org.junit.Test;
import org.junit.runner.RunWith;
import org.junit.runners.JUnit4;

@RunWith(JUnit4.class)

public class ParseResumeLocationTest {

  @Test
  public void testGettersAndSetters() {
    ParseResumeLocation aParseResumeLocation = new ParseResumeLocation("12345678900*0000");
    assertThat(aParseResumeLocation.getBytePosition()).isEqualTo(0);
    assertThat(aParseResumeLocation.getAllowResume()).isTrue();

    String filename = "filename";
    aParseResumeLocation.setFilename(filename);
    assertThat(aParseResumeLocation.getFilename()).isEqualTo(filename);

    int bytePosition = 12;
    aParseResumeLocation.setBytePosition(bytePosition);
    assertThat(aParseResumeLocation.getBytePosition()).isEqualTo(bytePosition);

    aParseResumeLocation.disallowResume();
    assertThat(aParseResumeLocation.getAllowResume()).isFalse();
  }

  @Test
  public void testSerializeAndDeserialize() {
    //test serialize
    String filename = "filename";
    String input = "12345678900*0000";
    ParseResumeLocation aParseResumeLocation = new ParseResumeLocation(filename, input);
    int bytePosition = 12;
    aParseResumeLocation.setBytePosition(bytePosition);
    aParseResumeLocation.disallowResume();

    ParseResumeLocationProto proto = aParseResumeLocation.serialize();
    assertThat(proto.getFilename()).isEqualTo(filename);
    assertThat(proto.getInput()).isEqualTo(input);
    assertThat(proto.getBytePosition()).isEqualTo(bytePosition);
    assertThat(proto.getAllowResume()).isFalse();

    ParseResumeLocation parseResumeLocation2 = new ParseResumeLocation(proto);
    new EqualsTester().addEqualityGroup(parseResumeLocation2, aParseResumeLocation).testEquals();

    ParseResumeLocationProto proto2 = parseResumeLocation2.serialize();
    new EqualsTester().addEqualityGroup(proto, proto2).testEquals();
  }

  @Test
  public void testClassAndProtoSize() {
    assertWithMessage(
            "The number of fields of ParseResumeLocationProto has changed, "
                + "please also update the serialization code accordingly.")
        .that(ParseResumeLocationProto.getDescriptor().getFields())
        .hasSize(4);
    assertWithMessage(
            "The number of fields in ParseResumeLocation class has changed, "
                + "please also update the proto and serialization code accordingly.")
        .that(TestUtil.getNonStaticFieldCount(ParseResumeLocation.class))
        .isEqualTo(4);
  }
}
