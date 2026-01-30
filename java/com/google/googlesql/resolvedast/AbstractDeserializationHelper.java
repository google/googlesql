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

package com.google.googlesql.resolvedast;

import static com.google.common.base.Preconditions.checkNotNull;

import com.google.common.collect.ImmutableList;
import com.google.googlesql.Column;
import com.google.googlesql.ColumnRefProto;
import com.google.googlesql.Connection;
import com.google.googlesql.ConnectionRefProto;
import com.google.googlesql.Constant;
import com.google.googlesql.ConstantRefProto;
import com.google.googlesql.DescriptorPool;
import com.google.googlesql.DescriptorPool.GoogleSQLDescriptor;
import com.google.googlesql.DescriptorPool.GoogleSQLFieldDescriptor;
import com.google.googlesql.DescriptorPool.GoogleSQLOneofDescriptor;
import com.google.googlesql.FieldDescriptorRefProto;
import com.google.googlesql.Function;
import com.google.googlesql.FunctionProtos.FunctionSignatureProto;
import com.google.googlesql.FunctionProtos.ResolvedFunctionCallInfoProto;
import com.google.googlesql.FunctionProtos.TVFSignatureProto;
import com.google.googlesql.FunctionRefProto;
import com.google.googlesql.FunctionSignature;
import com.google.googlesql.GoogleSQLAnnotation.AnnotationMapProto;
import com.google.googlesql.GoogleSQLType.ProtoTypeProto;
import com.google.googlesql.GoogleSQLType.TypeProto;
import com.google.googlesql.GoogleSQLTypeModifiers.TypeModifiersProto;
import com.google.googlesql.GoogleSQLTypeParameters.TypeParametersProto;
import com.google.googlesql.GraphElementLabel;
import com.google.googlesql.GraphElementLabelRefProto;
import com.google.googlesql.GraphElementTable;
import com.google.googlesql.GraphElementTableRefProto;
import com.google.googlesql.GraphPropertyDeclaration;
import com.google.googlesql.GraphPropertyDeclarationRefProto;
import com.google.googlesql.Model;
import com.google.googlesql.ModelRefProto;
import com.google.googlesql.OneofDescriptorRefProto;
import com.google.googlesql.Procedure;
import com.google.googlesql.ProcedureRefProto;
import com.google.googlesql.PropertyGraph;
import com.google.googlesql.PropertyGraphRefProto;
import com.google.googlesql.ResolvedCollationProto;
import com.google.googlesql.ResolvedColumnProto;
import com.google.googlesql.ResolvedFunctionCallInfo;
import com.google.googlesql.Sequence;
import com.google.googlesql.SequenceRefProto;
import com.google.googlesql.TVFSignature;
import com.google.googlesql.Table;
import com.google.googlesql.TableRefProto;
import com.google.googlesql.TableValuedFunction;
import com.google.googlesql.TableValuedFunctionRefProto;
import com.google.googlesql.Type;
import com.google.googlesql.TypeFactory;
import com.google.googlesql.TypeParameters;
import com.google.googlesql.Value;
import com.google.googlesql.ValueWithTypeProto;
import javax.annotation.Nullable;

/** Deserializes objects in the ResolvedAST which require some context. */
public abstract class AbstractDeserializationHelper {
  private final TypeFactory typeFactory;
  private final ImmutableList<? extends DescriptorPool> pools;

  public AbstractDeserializationHelper(
      TypeFactory typeFactory, ImmutableList<? extends DescriptorPool> pools) {
    this.typeFactory = checkNotNull(typeFactory);
    this.pools = checkNotNull(pools);
  }

  GoogleSQLFieldDescriptor deserialize(FieldDescriptorRefProto proto) {
    ProtoTypeProto containingMessageProto = proto.getContainingProto();
    GoogleSQLDescriptor containingMessage =
        checkNotNull(
            checkNotNull(pools.get(containingMessageProto.getFileDescriptorSetIndex()))
                .findMessageTypeByName(containingMessageProto.getProtoName()),
            "Couldn't find pool for descriptor %s",
            containingMessageProto.getProtoName());
    return checkNotNull(containingMessage.findFieldByNumber(proto.getNumber()));
  }

  GoogleSQLOneofDescriptor deserialize(OneofDescriptorRefProto proto) {
    ProtoTypeProto containingMessageProto = proto.getContainingProto();
    GoogleSQLDescriptor containingMessage =
        checkNotNull(
            checkNotNull(pools.get(containingMessageProto.getFileDescriptorSetIndex()))
                .findMessageTypeByName(containingMessageProto.getProtoName()),
            "Couldn't find pool for descriptor %s",
            containingMessageProto.getProtoName());
    return checkNotNull(containingMessage.findOneofByIndex(proto.getIndex()));
  }

  abstract Constant deserialize(ConstantRefProto proto);

  abstract Function deserialize(FunctionRefProto proto);

  FunctionSignature deserialize(FunctionSignatureProto proto) {
    return FunctionSignature.deserialize(proto, pools);
  }

  abstract TableValuedFunction deserialize(TableValuedFunctionRefProto proto);

  ResolvedFunctionCallInfo deserialize(ResolvedFunctionCallInfoProto proto) {
    return ResolvedFunctionCallInfo.deserialize(proto, pools);
  }

  TVFSignature deserialize(TVFSignatureProto proto) {
    return TVFSignature.deserialize(proto, pools);
  }

  abstract Procedure deserialize(ProcedureRefProto proto);

  abstract Column deserialize(ColumnRefProto proto);

  abstract PropertyGraph deserialize(PropertyGraphRefProto proto);

  abstract GraphPropertyDeclaration deserialize(GraphPropertyDeclarationRefProto proto);

  abstract GraphElementLabel deserialize(GraphElementLabelRefProto proto);

  abstract GraphElementTable deserialize(GraphElementTableRefProto proto);

  ResolvedColumn deserialize(ResolvedColumnProto proto) {
    return new ResolvedColumn(
        proto.getColumnId(),
        proto.getTableName(),
        proto.getName(),
        deserialize(proto.getType()));
  }

  abstract Model deserialize(ModelRefProto proto);

  abstract Connection deserialize(ConnectionRefProto proto);

  abstract Sequence deserialize(SequenceRefProto proto);

  @Nullable
  abstract Table deserialize(TableRefProto proto);

  Type deserialize(TypeProto proto) {
    return typeFactory.deserialize(proto, pools);
  }

  Value deserialize(ValueWithTypeProto proto) {
    if (!(proto.hasType() && proto.hasValue())) {
      return new Value(); // Invalid value.
    }
    return Value.deserialize(deserialize(proto.getType()), proto.getValue());
  }

  @Nullable
  AnnotationMap deserialize(AnnotationMapProto proto) {
    // TODO: use TypeFactory to create AnnotatedType.
    return null;
  }

  ResolvedCollation deserialize(ResolvedCollationProto proto) {
    return ResolvedCollation.deserialize(proto);
  }

  TypeModifiers deserialize(TypeModifiersProto proto) {
    return TypeModifiers.deserialize(proto);
  }

  TypeParameters deserialize(TypeParametersProto proto) {
    return TypeParameters.deserialize(proto);
  }
}
