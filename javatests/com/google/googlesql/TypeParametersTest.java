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
import static org.junit.Assert.fail;

import com.google.googlesql.GoogleSQLTypeParameters.NumericTypeParametersProto;
import com.google.googlesql.GoogleSQLTypeParameters.StringTypeParametersProto;
import com.google.googlesql.GoogleSQLTypeParameters.TypeParametersProto;

import java.util.Arrays;
import java.util.List;
import org.junit.Test;
import org.junit.runner.RunWith;
import org.junit.runners.JUnit4;

@RunWith(JUnit4.class)

public final class TypeParametersTest {

  @Test
  public void createStringTypeParametersWithMaxLiteral() {
    StringTypeParametersProto proto =
        StringTypeParametersProto.newBuilder().setIsMaxLength(true).build();
    TypeParameters typeParameters = new TypeParameters(proto);
    assertThat(typeParameters.isStringTypeParameters()).isTrue();
    assertThat(typeParameters.getStringTypeParameters().getIsMaxLength()).isTrue();
    assertThat(typeParameters.debugString()).isEqualTo("(max_length=MAX)");
  }

  @Test
  public void createStringTypeParametersWithMaxLength() {
    StringTypeParametersProto proto =
        StringTypeParametersProto.newBuilder().setMaxLength(1000).build();
    TypeParameters typeParameters = new TypeParameters(proto);
    assertThat(typeParameters.isStringTypeParameters()).isTrue();
    assertThat(typeParameters.getStringTypeParameters().getMaxLength()).isEqualTo(1000);
    assertThat(typeParameters.debugString()).isEqualTo("(max_length=1000)");
  }

  @Test
  public void createNumericTypeParametersWithMaxLiteral() {
    NumericTypeParametersProto proto =
        NumericTypeParametersProto.newBuilder().setIsMaxPrecision(true).setScale(20).build();
    TypeParameters typeParameters = new TypeParameters(proto);
    assertThat(typeParameters.isNumericTypeParameters()).isTrue();
    assertThat(typeParameters.debugString()).isEqualTo("(precision=MAX,scale=20)");
  }

  @Test
  public void createNumericTypeParametersWithPrecisonAndScale() {
    NumericTypeParametersProto proto =
        NumericTypeParametersProto.newBuilder().setPrecision(20).setScale(7).build();
    TypeParameters typeParameters = new TypeParameters(proto);
    assertThat(typeParameters.isNumericTypeParameters()).isTrue();
    assertThat(typeParameters.debugString()).isEqualTo("(precision=20,scale=7)");
  }

  @Test
  public void createNumericTypeParametersWithPrecisionOnly() {
    NumericTypeParametersProto proto =
        NumericTypeParametersProto.newBuilder().setPrecision(20).build();
    TypeParameters typeParameters = new TypeParameters(proto);
    assertThat(typeParameters.isNumericTypeParameters()).isTrue();
    assertThat(typeParameters.debugString()).isEqualTo("(precision=20,scale=0)");
  }

  @Test
  public void createTypeParametersWithChildList() {
    StringTypeParametersProto stringParam =
        StringTypeParametersProto.newBuilder().setMaxLength(10).build();
    NumericTypeParametersProto numericParam =
        NumericTypeParametersProto.newBuilder().setPrecision(10).setScale(5).build();
    List<TypeParameters> childList =
        Arrays.asList(
            new TypeParameters(),
            new TypeParameters(stringParam),
            new TypeParameters(numericParam));
    TypeParameters structParam = new TypeParameters(childList);
    assertThat(structParam.getChildCount()).isEqualTo(3);
    assertThat(structParam.isStructOrArrayParameters()).isTrue();
    assertThat(structParam.isEmpty()).isFalse();
    assertThat(structParam.debugString())
        .isEqualTo("[null,(max_length=10),(precision=10,scale=5)]");
  }

  public static void checkSerializeAndDeserialize(TypeParameters typeParameters) {
    try {
      TypeParameters typeParameters2 = TypeParameters.deserialize(typeParameters.serialize());
      assertThat(typeParameters).isEqualTo(typeParameters2);
      assertThat(typeParameters2).isEqualTo(typeParameters);
      assertThat(typeParameters.hashCode()).isEqualTo(typeParameters2.hashCode());
      assertThat(typeParameters.serialize()).isEqualTo(typeParameters2.serialize());
    } catch (IllegalArgumentException e) {
      fail(e.toString());
    }
  }

  @Test
  public void serializeAndDeserializeStringTypeParameters() {
    checkSerializeAndDeserialize(
        new TypeParameters(StringTypeParametersProto.newBuilder().setIsMaxLength(true).build()));
    checkSerializeAndDeserialize(
        new TypeParameters(StringTypeParametersProto.newBuilder().setMaxLength(10).build()));
  }

  @Test
  public void serializeAndDeserializeNumericTypeParameters() {
    checkSerializeAndDeserialize(
        new TypeParameters(
            NumericTypeParametersProto.newBuilder().setPrecision(10).setScale(5).build()));
    checkSerializeAndDeserialize(
        new TypeParameters(
            NumericTypeParametersProto.newBuilder().setIsMaxPrecision(true).setScale(20).build()));
  }

  @Test
  public void serializeAndDeserializeTypeParametersWithChildList() {
    StringTypeParametersProto stringParam =
        StringTypeParametersProto.newBuilder().setMaxLength(50).build();
    NumericTypeParametersProto numericParam =
        NumericTypeParametersProto.newBuilder().setPrecision(70).setScale(30).build();
    List<TypeParameters> childList =
        Arrays.asList(new TypeParameters(stringParam), new TypeParameters(numericParam));
    TypeParameters structParam = new TypeParameters(childList);
    checkSerializeAndDeserialize(structParam);
  }

  @Test
  public void deserializeStringTypeParametersFailed() {
    // is_max_length is set to false.
    try {
      TypeParameters.deserialize(
          TypeParametersProto.newBuilder()
              .setStringTypeParameters(StringTypeParametersProto.newBuilder().setIsMaxLength(false))
              .build());
      fail();
    } catch (IllegalArgumentException expected) {
      assertThat(expected)
          .hasMessageThat()
          .contains("is_max_length should either be unset or true");
    }

    // max_length <= 0.
    try {
      TypeParameters.deserialize(
          TypeParametersProto.newBuilder()
              .setStringTypeParameters(StringTypeParametersProto.newBuilder().setMaxLength(-100))
              .build());
      fail();
    } catch (IllegalArgumentException expected) {
      assertThat(expected).hasMessageThat().contains("max_length must be larger than 0");
    }
  }

  @Test
  public void deserializeNumericTypeParametersFailed() {
    // is_max_precision is set to false.
    try {
      TypeParameters.deserialize(
          TypeParametersProto.newBuilder()
              .setNumericTypeParameters(
                  NumericTypeParametersProto.newBuilder().setIsMaxPrecision(false))
              .build());
      fail();
    } catch (IllegalArgumentException expected) {
      assertThat(expected)
          .hasMessageThat()
          .contains("is_max_precision should either be unset or true");
    }

    // precision > 76.
    try {
      TypeParameters.deserialize(
          TypeParametersProto.newBuilder()
              .setNumericTypeParameters(
                  NumericTypeParametersProto.newBuilder().setPrecision(100).setScale(30))
              .build());
      fail();
    } catch (IllegalArgumentException expected) {
      assertThat(expected)
          .hasMessageThat()
          .contains("precision must be within range [1, 76] or MAX");
    }

    // scale > 38.
    try {
      TypeParameters.deserialize(
          TypeParametersProto.newBuilder()
              .setNumericTypeParameters(
                  NumericTypeParametersProto.newBuilder().setPrecision(50).setScale(40))
              .build());
      fail();
    } catch (IllegalArgumentException expected) {
      assertThat(expected).hasMessageThat().contains("scale must be within range [0, 38]");
    }

    // precision < scale.
    try {
      TypeParameters.deserialize(
          TypeParametersProto.newBuilder()
              .setNumericTypeParameters(
                  NumericTypeParametersProto.newBuilder().setPrecision(30).setScale(35))
              .build());
      fail();
    } catch (IllegalArgumentException expected) {
      assertThat(expected)
          .hasMessageThat()
          .contains("precision must be equal to or larger than scale");
    }
  }

  @Test
  public void twoTypeParametersAreEqual() {
    TypeParameters numericParam1 =
        new TypeParameters(
            NumericTypeParametersProto.newBuilder().setIsMaxPrecision(true).setScale(20).build());
    TypeParameters numericParam2 =
        new TypeParameters(
            NumericTypeParametersProto.newBuilder().setIsMaxPrecision(true).setScale(20).build());
    TypeParameters numericParam3 =
        new TypeParameters(
            NumericTypeParametersProto.newBuilder().setPrecision(50).setScale(20).build());
    assertThat(numericParam1.equals(numericParam2)).isTrue();
    assertThat(numericParam1.equals(numericParam3)).isFalse();

    TypeParameters stringParam1 =
        new TypeParameters(StringTypeParametersProto.newBuilder().setMaxLength(100).build());
    TypeParameters stringParam2 =
        new TypeParameters(StringTypeParametersProto.newBuilder().setMaxLength(100).build());
    TypeParameters stringParam3 =
        new TypeParameters(StringTypeParametersProto.newBuilder().setMaxLength(1000).build());
    assertThat(stringParam1.equals(stringParam2)).isTrue();
    assertThat(stringParam1.equals(stringParam3)).isFalse();

    List<TypeParameters> childList1 =
        Arrays.asList(stringParam1, new TypeParameters(), numericParam1);
    List<TypeParameters> childList2 =
        Arrays.asList(stringParam2, new TypeParameters(), numericParam2);
    List<TypeParameters> childList3 = Arrays.asList(stringParam3, numericParam2);
    assertThat(new TypeParameters(childList1).equals(new TypeParameters(childList2))).isTrue();
    assertThat(new TypeParameters(childList1).equals(new TypeParameters(childList3))).isFalse();
  }
}
