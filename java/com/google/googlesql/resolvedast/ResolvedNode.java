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

import com.google.common.base.Preconditions;
import com.google.protobuf.Message;
import com.google.googlesql.DebugPrintableNode;
import com.google.googlesql.FileDescriptorSetsBuilder;
import com.google.googlesql.GoogleSQLResolvedNodeKind.ResolvedNodeKind;
import com.google.googlesql.ResolvedNodeProto;
import java.io.Serializable;
import java.util.ArrayList;
import java.util.List;

/**
 * This is the base class for the resolved AST. Subclasses are in the generated file
 * ResolvedNodes.java. ResolvedNodeKind enum is in the generated file resolved_node_kind.proto.
 * See gen_resolved_ast.py.
 *
 * <p> View code generated classes at (broken link).
 * HTML documentation for the class hierarchy is generated in resolved_ast.html.
 * A viewable copy is available at (broken link).
 *
 * <p> In this hierarchy, classes are either abstract or leaves.
 */
public abstract class ResolvedNode implements Serializable, DebugPrintableNode {
  ResolvedNode(ResolvedNodeProto proto, AbstractDeserializationHelper helper) {
    // Nothing to deserialize for now.
  }

  ResolvedNode() {}

  /**
   * Base case for recursive call in derived AST nodes.
   */
  public Message serialize(
      @SuppressWarnings("unused") FileDescriptorSetsBuilder fileDescriptorSetsBuilder) {
    return ResolvedNodeProto.getDefaultInstance();
  }

  public abstract ResolvedNodeKind nodeKind();

  @Override
  public abstract String nodeKindString();

  /** Accepts a visitor, which can process the node and optionally redispatch to children. */
  public abstract void accept(ResolvedNodes.Visitor visitor);

  /** Accepts a visitor, which can return a replacement node. */
  public abstract ResolvedNode accept(RewritingVisitor visitor);

  /**
   * Accepts a visitor, dispatching to the children of this node.
   *
   * <p>Note: this function is made non-abstract to allow all generated ResolvedNode implementations
   * to perform a {@code super.acceptChildren} call, which ensures all children nodes are sent to
   * {@code visitor}.
   */
  protected void acceptChildren(ResolvedNodes.Visitor visitor) {}

  /**
   * Creates a Builder object from this node.
   *
   * <p>The returned builder is a distinct object; modifications to the builder do not change this
   * object.
   */
  public abstract Builder toBuilder();

  /**
   * A builder class corresponding to ResolvedNode.
   *
   * <p>Note, there is a 'Builder' class hierarchy that matches the node class hierarchy found in {@
   * link ResolvedNodes}.
   *
   * <p>This is declared such that you can chain 'setX' calls freely between a class and those
   * classes it extends (transitively). Example: <code>
   *  // Note: ResolvedArgumentRef extends ResolvedExpr
   *  ResolvedArgumentRef ref =
   *      ResolvedArgumentRef.newBuilder()
   *        .setArgumentKind(...)  // A field on ResolvedArgumentRef
   *        .setType(...)  // A field on ResolvedExpr
   *        .setName("name")  // A field on ResolvedArgumentRef
   *        .build();
   * </code> Propagating 'is_ordered': Some setters will 'propagate' the 'is_ordered' field,
   * example: {@link ResolvedNodes.ResolvedLimitOffsetScan#setInputScan}. The setters will will be
   * documented as such.
   *
   * <p>Optional fields: Nearly all fields are required, and must have a non-null value. A small
   * number of fields are optional and have default values.
   */
  public abstract static class Builder {
    /**
     * Create this object.
     *
     * @throws IllegalArgumentException if any required field is not set.
     */
    public abstract ResolvedNode build();

    /**
     * Validates that each required field is set.
     *
     * @throws IllegalArgumentException if any required field is not set.
     */
    void validate() {}
  }

  @Override
  public final String toString() {
    return debugString();
  }

  public final String debugString() {
    StringBuilder sb = new StringBuilder();
    debugStringImpl("", "", sb);
    return sb.toString();
  }

  /**
   * Return true if the fields returned from collectDebugStringFields would contain any field with a
   * non-empty {@code nodes} list. These are the fields that would have child nodes in the
   * debugString tree.
   */
  private boolean hasDebugStringFieldsWithNodes() {
    List<DebugStringField> fields = new ArrayList<>();
    collectDebugStringFields(fields);
    for (DebugStringField field : fields) {
      if (field.hasNodes()) {
        return true;
      }
    }
    return false;
  }

  /**
   * collectDebugStringFieldsWithNameFormat and getNameForDebugStringWithNameFormat are helpers used
   * in custom format methods to format nodes as an assignment, "name := node", with node's children
   * as children of this node.
   */
  protected final void collectDebugStringFieldsWithNameFormat(List<DebugStringField> fields) {
    Preconditions.checkArgument(fields.isEmpty());
    if (hasDebugStringFieldsWithNodes()) {
      fields.add(new DebugStringField("", this));
    } else {
      collectDebugStringFields(fields);
    }
  }

  protected final String getNameForDebugStringWithNameFormat(String name) {
    if (hasDebugStringFieldsWithNodes()) {
      return name + " :=";
    } else {
      return name + " := " + getNameForDebugString();
    }
  }
}
