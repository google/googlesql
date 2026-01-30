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

import com.google.common.base.Preconditions;
import com.google.googlesql.GoogleSQLOptions.ProductMode;
import com.google.googlesql.GoogleSQLType.TypeKind;
import com.google.googlesql.GoogleSQLType.TypeProto;

/**
 * SimpleType includes all non-parameterized types (all scalar types
 * except enum).
 */
public class SimpleType extends Type {
  /** Private constructor, instances must be created with {@link TypeFactory} */
  SimpleType(TypeKind kind) {
    super(kind);
    Preconditions.checkArgument(TypeFactory.isSimpleType(kind));
  }

  /** Private constructor to create an invalid type */
  SimpleType() {
    super(TypeKind.TYPE_UNKNOWN);
  }

  @Override
  public void serialize(
      TypeProto.Builder typeProtoBuilder, FileDescriptorSetsBuilder fileDescriptorSetsBuilder) {
    typeProtoBuilder.setTypeKind(getKind());
  }

  @Override
  public int hashCode() {
    return getKind().getNumber();
  }

  @Override
  public String typeName(ProductMode productMode) {
    if (productMode == ProductMode.PRODUCT_EXTERNAL && getKind() == TypeKind.TYPE_DOUBLE) {
      return "FLOAT64";
    }
    return TYPE_KIND_NAMES.get(getKind());
  }

  @Override
  public String debugString(boolean details) {
    return TYPE_KIND_NAMES.get(getKind());
  }
}
