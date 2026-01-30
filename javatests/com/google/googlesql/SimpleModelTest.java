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

import com.google.googlesql.GoogleSQLType.TypeKind;

import org.junit.Test;
import org.junit.runner.RunWith;
import org.junit.runners.JUnit4;

@RunWith(JUnit4.class)

public class SimpleModelTest {

  @Test
  public void testInputsAndOutputs() {
    SimpleModel model = new SimpleModel("model");
    model.addInput(
        new SimpleModel.NameAndType("input", TypeFactory.createSimpleType(TypeKind.TYPE_STRING)));
    model.addOutput(
        new SimpleModel.NameAndType("output", TypeFactory.createSimpleType(TypeKind.TYPE_INT64)));

    assertThat(model.getInputs()).hasSize(1);
    assertThat(model.getInputs().get(0).getName()).isEqualTo("input");
    assertThat(model.getInputs().get(0).getType())
        .isEqualTo(TypeFactory.createSimpleType(TypeKind.TYPE_STRING));
    assertThat(model.getInputs().get(0).isPseudoColumn()).isFalse();
    assertThat(model.getInputs().get(0).isWritableColumn()).isFalse();
    assertThat(model.getInputs().get(0).canUpdateToDefault()).isFalse();
    assertThat(model.getOutputs()).hasSize(1);
    assertThat(model.getOutputs().get(0).getName()).isEqualTo("output");
    assertThat(model.getOutputs().get(0).getType())
        .isEqualTo(TypeFactory.createSimpleType(TypeKind.TYPE_INT64));
    assertThat(model.getOutputs().get(0).isPseudoColumn()).isFalse();
    assertThat(model.getOutputs().get(0).isWritableColumn()).isFalse();
    assertThat(model.getOutputs().get(0).canUpdateToDefault()).isFalse();
  }

  @Test
  public void testNextId() {
    SimpleModel m1 = new SimpleModel("m1");
    SimpleModel m2 = new SimpleModel("m2");
    SimpleModel m3 = new SimpleModel("m3", 100L);
    SimpleModel m4 = new SimpleModel("m4");
    assertThat(m2.getId()).isGreaterThan(m1.getId());
    assertThat(m3.getId()).isEqualTo(100);
    assertThat(m4.getId()).isEqualTo(101);
  }
}
