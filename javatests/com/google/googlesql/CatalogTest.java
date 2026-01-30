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

import com.google.common.collect.ImmutableList;
import com.google.googlesql.GoogleSQLFunctions.FunctionEnums.Mode;
import com.google.googlesql.GoogleSQLType.TypeKind;
import com.google.googlesql.GoogleSQLType.TypeProto;


import java.util.ArrayList;
import java.util.List;
import org.junit.Test;
import org.junit.runner.RunWith;
import org.junit.runners.JUnit4;

@RunWith(JUnit4.class)

public class CatalogTest {

  @Test
  public void testConvertPathToProtoName() {
    assertThat(Catalog.convertPathToProtoName(new ArrayList<String>())).isEmpty();
    assertThat(Catalog.convertPathToProtoName(null)).isEmpty();
    assertThat(Catalog.convertPathToProtoName(ImmutableList.of("A"))).isEqualTo("A");
    assertThat(Catalog.convertPathToProtoName(ImmutableList.of("A", "B"))).isEqualTo("A.B");
    assertThat(Catalog.convertPathToProtoName(ImmutableList.of("A", "B", "C"))).isEqualTo("A.B.C");
    assertThat(Catalog.convertPathToProtoName(ImmutableList.of("A", "B.C"))).isEmpty();
    assertThat(Catalog.convertPathToProtoName(ImmutableList.of("A", "", "C"))).isEmpty();
    assertThat(Catalog.convertPathToProtoName(ImmutableList.of("A1", "B_2", "z99")))
        .isEqualTo("A1.B_2.z99");
    assertThat(Catalog.convertPathToProtoName(ImmutableList.of("1a"))).isEmpty();
    assertThat(Catalog.convertPathToProtoName(ImmutableList.of("_a", "_"))).isEqualTo("_a._");
    assertThat(Catalog.convertPathToProtoName(ImmutableList.of("a-b"))).isEmpty();
  }
}
