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

import static java.util.stream.Collectors.joining;

import com.google.common.base.Joiner;
import com.google.common.base.Preconditions;
import com.google.common.collect.ImmutableList;
import com.google.protobuf.Descriptors.FieldDescriptor;
import com.google.protobuf.Descriptors.OneofDescriptor;
import com.google.protobuf.ProtocolMessageEnum;
import com.google.googlesql.Column;
import com.google.googlesql.Connection;
import com.google.googlesql.Constant;
import com.google.googlesql.DebugPrintableNode.DebugStringField;
import com.google.googlesql.DescriptorPool.GoogleSQLFieldDescriptor;
import com.google.googlesql.DescriptorPool.GoogleSQLOneofDescriptor;
import com.google.googlesql.FunctionSignature;
import com.google.googlesql.GoogleSQLStrings;
import com.google.googlesql.GraphElementLabel;
import com.google.googlesql.GraphElementTable;
import com.google.googlesql.GraphPropertyDeclaration;
import com.google.googlesql.Model;
import com.google.googlesql.Procedure;
import com.google.googlesql.PropertyGraph;
import com.google.googlesql.ResolvedFunctionCallInfo;
import com.google.googlesql.Sequence;
import com.google.googlesql.TVFSignature;
import com.google.googlesql.Table;
import com.google.googlesql.TableValuedFunction;
import com.google.googlesql.Type;
import com.google.googlesql.TypeAnnotationProto.FieldFormat;
import com.google.googlesql.TypeParameters;
import com.google.googlesql.Value;
import com.google.googlesql.resolvedast.ResolvedFunctionCallBaseEnums.ErrorMode;
import com.google.googlesql.resolvedast.ResolvedInsertStmtEnums.InsertMode;
import com.google.googlesql.resolvedast.ResolvedNodes.ResolvedCast;
import com.google.googlesql.resolvedast.ResolvedNodes.ResolvedComputedColumn;
import com.google.googlesql.resolvedast.ResolvedNodes.ResolvedConstant;
import com.google.googlesql.resolvedast.ResolvedNodes.ResolvedDeferredComputedColumn;
import com.google.googlesql.resolvedast.ResolvedNodes.ResolvedExtendedCastElement;
import com.google.googlesql.resolvedast.ResolvedNodes.ResolvedFunctionCallBase;
import com.google.googlesql.resolvedast.ResolvedNodes.ResolvedMakeProtoField;
import com.google.googlesql.resolvedast.ResolvedNodes.ResolvedOption;
import com.google.googlesql.resolvedast.ResolvedNodes.ResolvedOutputColumn;
import com.google.googlesql.resolvedast.ResolvedNodes.ResolvedStaticDescribeScan;
import com.google.googlesql.resolvedast.ResolvedNodes.ResolvedSystemVariable;
import com.google.googlesql.resolvedast.ResolvedNodes.ResolvedWindowFrame;
import com.google.googlesql.resolvedast.ResolvedNodes.ResolvedWindowFrameExpr;
import com.google.googlesql.resolvedast.ResolvedOptionEnums.AssignmentOp;
import com.google.googlesql.resolvedast.ResolvedSetOperationScanEnums.SetOperationColumnMatchMode;
import com.google.googlesql.resolvedast.ResolvedSetOperationScanEnums.SetOperationColumnPropagationMode;
import com.google.googlesql.resolvedast.ResolvedStatementEnums.ObjectAccess;
import java.util.ArrayList;
import java.util.List;
import javax.annotation.Nullable;

/**
 * Helper functions for generating debug strings for {@link ResolvedNode}s.
 */
class DebugStrings {

  // isDefaultValue functions for different node field types, similar to the C++ implementations in
  // googlesql/resolved_ast/resolved_ast.cc.template

  static boolean isDefaultValue(ProtocolMessageEnum e) {
    return e.getNumber() == 0;
  }

  static boolean isDefaultValue(List<?> l) {
    return l.isEmpty();
  }

  static boolean isDefaultValue(boolean b) {
    return !b;
  }

  static boolean isDefaultValue(String s) {
    return s.isEmpty();
  }

  @SuppressWarnings("unused")
  static boolean isDefaultValue(Type t) {
    return false;
  }

  static boolean isDefaultValue(Value v) {
    return !v.isValid();
  }

  static boolean isDefaultValue(@Nullable FunctionSignature signature) {
    return signature == null || signature.isDefaultValue();
  }

  static boolean isDefaultValue(ResolvedFunctionCallInfo call) {
    return call.isDefaultValue();
  }

  static boolean isDefaultValue(TableValuedFunction tvf) {
    return tvf.isDefaultValue();
  }

  static boolean isDefaultValue(TVFSignature signature) {
    return signature.isDefaultValue();
  }

  static boolean isDefaultValue(ResolvedColumn column) {
    return column.isDefaultValue();
  }

  // Used for positional parameters.
  static boolean isDefaultValue(long position) {
    return position == 0;
  }

  static boolean isDefaultValue(AnnotationMap annotationMap) {
    return annotationMap == null;
  }

  static boolean isDefaultValue(ResolvedCollation resolvedCollation) {
    // TODO: implement ResolvedCollation in Java.
    return resolvedCollation == null || resolvedCollation.debugString().isEmpty();
  }

  static boolean isDefaultValue(TypeModifiers typeModifiers) {
    return typeModifiers == null || typeModifiers.isEmpty();
  }

  static boolean isDefaultValue(TypeParameters typeParameters) {
    // TODO: Modify once type parameters are made non-optional in the resolved AST and
    // the default value is an empty type parameters object.
    return (typeParameters == null || typeParameters.isEmpty());
  }

  static boolean isDefaultValue(Table table) {
    return table == null || table.getName().isEmpty();
  }

  static boolean isDefaultValue(SetOperationColumnMatchMode mode) {
    return mode == SetOperationColumnMatchMode.BY_POSITION;
  }

  static boolean isDefaultValue(SetOperationColumnPropagationMode mode) {
    return mode == SetOperationColumnPropagationMode.STRICT;
  }

  static boolean isDefaultValue(AssignmentOp assignmentOp) {
    return assignmentOp == AssignmentOp.DEFAULT_ASSIGN;
  }

  static boolean isDefaultValue(PropertyGraph propertyGraph) {
    return propertyGraph == null || propertyGraph.getFullName().isEmpty();
  }

  static boolean isDefaultValue(GraphPropertyDeclaration propertyDeclaration) {
    return propertyDeclaration == null || propertyDeclaration.getName().isEmpty();
  }

  static boolean isDefaultValue(GraphElementLabel elementLabel) {
    return elementLabel == null || elementLabel.getName().isEmpty();
  }

  // toStringImpl functions for different node field types, similar to the C++ implementation in
  // googlesql/resolved_ast/resolved_ast.cc.template

  static String toStringImpl(String s) {
    return GoogleSQLStrings.toStringLiteral(s);
  }

  static String toStringImpl(boolean b) {
    return b ? "TRUE" : "FALSE";
  }

  static String toStringImpl(long i) {
    return Long.toString(i);
  }

  static String toStringImpl(InsertMode mode) {
    return mode.name().replace('_', ' ');
  }

  static String toStringImpl(Enum<?> value) {
    return value.name();
  }

  static String toStringImpl(Model model) {
    return model.getFullName();
  }

  static String toStringImpl(Connection connection) {
    return connection.getFullName();
  }

  static String toStringImpl(Sequence sequence) {
    return sequence.getFullName();
  }

  static String toStringImpl(Table table) {
    return table.getFullName();
  }

  static String toStringImpl(Type type) {
    return type.debugString();
  }

  static String toStringImpl(GoogleSQLFieldDescriptor field) {
    FieldDescriptor descriptor = field.getDescriptor();
    return descriptor.isExtension() ? "[" + descriptor.getFullName() + "]" : descriptor.getName();
  }

  static String toStringImpl(GoogleSQLOneofDescriptor field) {
    OneofDescriptor descriptor = field.getDescriptor();
    return descriptor.getName();
  }

  static String toStringImpl(ResolvedColumn column) {
    return column.debugString();
  }

  static String toStringImpl(Constant constant) {
    return constant.toString();
  }

  static String toStringImpl(FunctionSignature signature) {
    return signature.toString();
  }

  static String toStringImpl(ResolvedFunctionCallInfo call) {
    return call.toString();
  }

  static String toStringImpl(TableValuedFunction tvf) {
    return tvf.toString();
  }

  static String toStringImpl(TVFSignature signature) {
    return signature.toString();
  }

  static String toStringImpl(Procedure procedure) {
    return procedure.toString();
  }

  static String toStringImpl(Column column) {
    return column.getFullName();
  }

  // To deal with type erasure, every toString function for repeated fields
  // must have a different signature or different name. For now, it's fine to
  // use ImmutableList<ResolvedColumn> here and List<String> below. This is
  // hacky but it works. A third function could use Iterable<T> or we could use
  // different names for each type.
  static String toStringImpl(ImmutableList<ResolvedColumn> columns) {
    return ResolvedColumn.toString(columns);
  }

  static String toStringImpl(Value value) {
    return value.shortDebugString();
  }

  static String toStringImpl(List<String> values, String separator) {
    StringBuilder sb = new StringBuilder();
    for (String value : values) {
      if (sb.length() > 0) {
        sb.append(separator);
      }
      sb.append(GoogleSQLStrings.toIdentifierLiteral(value));
    }
    return sb.toString();
  }

  // Most vector<string> fields are identifier paths so we format
  // the value that way by default.
  // For other vector<string> fields, we can override this with to_string_method.
  static String toStringImpl(List<String> values) {
    return toStringImpl(values, ".");
  }

  static String toStringImpl(AnnotationMap annotationMap) {
    return annotationMap.debugString();
  }

  static String toStringImpl(ResolvedCollation resolvedCollation) {
    return resolvedCollation.debugString();
  }

  static String toStringImpl(TypeModifiers typeModifiers) {
    return typeModifiers.debugString();
  }

  static String toStringImpl(TypeParameters typeParameters) {
    return typeParameters.debugString();
  }

  static String toStringImpl(PropertyGraph propertyGraph) {
    return propertyGraph.getFullName();
  }

  static String toStringImpl(GraphPropertyDeclaration graphPropertyDeclaration) {
    return String.format(
        "%s(%s)",
        graphPropertyDeclaration.getName(), graphPropertyDeclaration.getType().debugString());
  }

  static String toStringImpl(GraphElementLabel graphElementLabel) {
    return graphElementLabel.getFullName();
  }

  static String toStringImpl(GraphElementTable graphElementTable) {
    return graphElementTable.getName();
  }

  static String toStringCommaSeparatedForGraphElementLabel(
      ImmutableList<GraphElementLabel> labels) {
    return labels.stream().map(GraphElementLabel::getName).collect(joining(","));
  }

  static String toStringCommaSeparatedForGraphElementTable(
      ImmutableList<GraphElementTable> tables) {
    return String.format(
        "[%s]", tables.stream().map(GraphElementTable::getFullName).collect(joining(",")));
  }

  static String toStringPeriodSeparatedForFieldDescriptors(List<GoogleSQLFieldDescriptor> fields) {
    StringBuilder sb = new StringBuilder();
    for (GoogleSQLFieldDescriptor field : fields) {
      if (sb.length() > 0) {
        sb.append(".");
      }
      sb.append(toStringImpl(field));
    }
    return sb.toString();
  }

  static String toStringCommaSeparatedForInt(List<Long> values) {
    return "[" + Joiner.on(", ").join(values) + "]";
  }

  static String toStringVerbose(FunctionSignature signature) {
    return signature.debugString(/*functionName=*/"", /*verbose=*/true);
  }

  // Custom implementation for the list of enums. Named uniquely to avoid collisions with any other
  // method.
  static String toStringObjectAccess(ImmutableList<ObjectAccess> values) {
    StringBuilder sb = new StringBuilder();
    for (ObjectAccess value : values) {
      if (sb.length() > 0) {
        sb.append(",");
      }
      sb.append(value.name());
    }
    return sb.toString();
  }

  // This formats a list of identifiers (quoting if needed).
  static String toStringCommaSeparated(List<String> values) {
    return "[" + toStringImpl(values, ", ") + "]";
  }

  static String toStringCommaSeparated(ImmutableList<ResolvedCollation> resolvedCollations) {
    return resolvedCollations.stream().map(ResolvedCollation::debugString).collect(joining(","));
  }

  static String toStringCommaSeparatedForColumn(ImmutableList<Column> columns) {
    return columns.stream().map(DebugStrings::toStringImpl).collect(joining(", ", "[", "]"));
  }

  // Functions for classes in the generated code with customized debugStrings. These functions
  // should only be called from the generated code in ResolvedNodes.java. The implementations are
  // nearly identical to the C++ implementations in
  // ResolvedComputedColumn::CollectDebugStringFields

  /**
   * ResolvedComputedColumn gets formatted as "name := expr" with expr's children printed as its own
   * children.
   */
  static void collectDebugStringFields(ResolvedComputedColumn node, List<DebugStringField> fields) {
    fields.clear();
    node.getExpr().collectDebugStringFieldsWithNameFormat(fields);
  }

  /**
   * ResolvedDeferredComputedColumn gets formatted as "name := expr" with expr's children printed as
   * its own children.
   */
  static void collectDebugStringFields(
      ResolvedDeferredComputedColumn node, List<DebugStringField> fields) {
    fields.clear();
    node.getExpr().collectDebugStringFieldsWithNameFormat(fields);
  }

  /** ResolvedOutputColumn gets formatted as "column AS name [column.type]". */
  @SuppressWarnings("unused")
  static void collectDebugStringFields(ResolvedOutputColumn node, List<DebugStringField> fields) {
    Preconditions.checkState(fields.isEmpty());
  }

  /** ResolvedConstant gets formatted as Constant(name(constant), type[, value]). */
  static void collectDebugStringFields(ResolvedConstant node, List<DebugStringField> fields) {
    Preconditions.checkArgument(fields.size() <= 1);

    // The base class, ResolvedExpr, has its implementation called first, so when we get here,
    // <fields> is already populated with the type.  Insert the name first, then the value last,
    // to match the behavior of the C++ implementation, so that tests which rely on the debug string
    // output can pass.
    fields.add(0, new DebugStringField("", node.getConstant().getFullName()));
    fields.add(new DebugStringField("value", node.getConstant().getValue().debugString()));
  }

  /**
   * ResolvedFunctionCall gets formatted as "FunctionCall(name(arg_types) -> type)" with only
   * arguments printed as children.
   */
  static void collectDebugStringFields(
      ResolvedFunctionCallBase node, List<DebugStringField> fields) {
    Preconditions.checkArgument(fields.size() <= 2);

    fields.clear();
    Preconditions.checkArgument(
        node.getArgumentList().isEmpty() || node.getGenericArgumentList().isEmpty());
    if (!node.getArgumentList().isEmpty()) {
      // Use empty name to avoid printing "arguments=" with extra indentation.
      fields.add(new DebugStringField(/*name=*/ "", node.getArgumentList()));
    }
    if (!node.getGenericArgumentList().isEmpty()) {
      // Use empty name to avoid printing "generic_arguments=" with extra indentation.
      fields.add(new DebugStringField(/*name=*/ "", node.getGenericArgumentList()));
    }
    if (!node.getHintList().isEmpty()) {
      fields.add(new DebugStringField("hint_list", node.getHintList()));
    }
  }

  /**
   * ResolvedCast gets formatted as "Cast(from_type -> to_type)" with only from_expr printed as a
   * child.
   */
  static void collectDebugStringFields(ResolvedCast node, List<DebugStringField> fields) {
    Preconditions.checkArgument(fields.size() <= 1);

    fields.clear();
    if (node.getExpr() != null) {
      // Use empty name to avoid printing "arguments=" with extra indentation.
      fields.add(new DebugStringField("", node.getExpr()));
    }
    if (node.getReturnNullOnError()) {
      fields.add(new DebugStringField("return_null_on_error", "TRUE"));
    }
    if (node.getExtendedCast() != null) {
      fields.add(new DebugStringField("extended_cast", node.getExtendedCast()));
    }
    if (node.getFormat() != null) {
      fields.add(new DebugStringField("format", node.getFormat()));
    }
    if (node.getTimeZone() != null) {
      fields.add(new DebugStringField("time_zone", node.getTimeZone()));
    }
    if (node.getTypeModifiers() != null && !node.getTypeModifiers().isEmpty()) {
      fields.add(new DebugStringField("type_modifiers", node.getTypeModifiers().debugString()));
    }
  }

  /**
   * ResolvedExtendedCastElement gets formatted as "ResolvedExtendedCastElement(from_type ->
   * to_type, function=name)".
   */
  static void collectDebugStringFields(
      ResolvedExtendedCastElement node, List<DebugStringField> fields) {
    Preconditions.checkArgument(fields.size() <= 1);
  }

  /**
   * ResolvedMakeProtoField gets formatted as "field[(format=TIMESTAMP_MILLIS)] := expr" with expr's
   * children printed as its own children. The required proto format is shown in parentheses when
   * present. expr is normally just a ResolvedColumnRef, but could be a cast expr.
   */
  static void collectDebugStringFields(ResolvedMakeProtoField node, List<DebugStringField> fields) {
    node.getExpr().collectDebugStringFieldsWithNameFormat(fields);
  }

  /**
   * ResolvedOption gets formatted as "[<qualifier>.]<name> := value" if the assignment operator is
   * "=". Otherwise, it is formatted as [<qualifier>.]<name><assignment_operator> +- <value>
   */
  static void collectDebugStringFields(ResolvedOption node, List<DebugStringField> fields) {
    if (isDefaultValue(node.getAssignmentOp())) {
      node.getValue().collectDebugStringFieldsWithNameFormat(fields);
    } else {
      fields.add(new DebugStringField("", node.getValue()));
    }
  }

  static void collectDebugStringFields(ResolvedWindowFrame node, List<DebugStringField> fields) {
    fields.add(new DebugStringField("start_expr", node.getStartExpr()));
    fields.add(new DebugStringField("end_expr", node.getEndExpr()));
  }

  static void collectDebugStringFields(
      ResolvedWindowFrameExpr node, List<DebugStringField> fields) {
    if (node.getExpression() != null) {
      // Use empty name to avoid printing "expression=" with extra indentation.
      fields.add(new DebugStringField("", node.getExpression()));
    }
  }

  static void collectDebugStringFields(ResolvedSystemVariable node, List<DebugStringField> fields) {
    fields.clear();
    ArrayList<String> pathParts = new ArrayList<>();
    for (String pathPart : node.getNamePath()) {
      pathParts.add(GoogleSQLStrings.toIdentifierLiteral(pathPart));
    }

    fields.add(new DebugStringField("", String.join(".", pathParts)));
    fields.add(new DebugStringField("type", node.getType().toString()));
  }

  static void collectDebugStringFields(
      ResolvedStaticDescribeScan node, List<DebugStringField> fields) {
    if (!node.getDescribeText().isEmpty()) {
      fields.add(new DebugStringField("describe_text", node.getDescribeText()));
    }
    if (node.getInputScan() != null) {
      fields.add(new DebugStringField("input_scan", node.getInputScan()));
    }
  }

  static String getNameForDebugString(ResolvedStaticDescribeScan node) {
    return node.nodeKindString();
  }

  static String getNameForDebugString(ResolvedComputedColumn node) {
    String name = node.getColumn().shortDebugString();
    return node.getExpr().getNameForDebugStringWithNameFormat(name);
  }

  static String getNameForDebugString(ResolvedDeferredComputedColumn node) {
    String name =
        node.getColumn().shortDebugString()
            + " [side_effect_column="
            + ((ResolvedDeferredComputedColumn) node).getSideEffectColumn().shortDebugString()
            + "]";

    return node.getExpr().getNameForDebugStringWithNameFormat(name);
  }

  static String getNameForDebugString(ResolvedOutputColumn node) {
    return node.getColumn().debugString()
        + " AS "
        + GoogleSQLStrings.toIdentifierLiteral(node.getName())
        + " ["
        + node.getColumn().getType().debugString()
        + "]";
  }

  static String getNameForDebugString(ResolvedConstant node) {
    return "Constant";
  }

  static String getNameForDebugString(ResolvedFunctionCallBase node) {
    return node.nodeKindString()
        + "("
        + (node.getErrorMode() == ErrorMode.SAFE_ERROR_MODE ? "{SAFE_ERROR_MODE} " : "")
        + (node.getFunction() != null ? node.getFunction().toString() : "<unknown>")
        + node.getSignature().toString()
        + ")";
  }

  static String getNameForDebugString(ResolvedCast node) {
    return "Cast("
        + node.getExpr().getType().debugString()
        + " -> "
        + node.getType().debugString()
        + ")";
  }

  static String getNameForDebugString(ResolvedExtendedCastElement node) {
    return node.nodeKindString()
        + "("
        + node.getToType().debugString()
        + " -> "
        + node.getFromType().debugString()
        + ", (function="
        + (node.getFunction() != null ? node.getFunction().toString() : "<unknown>")
        + ")";
  }

  static String getNameForDebugString(ResolvedMakeProtoField node) {
    String name;

    FieldDescriptor descriptor = node.getFieldDescriptor().getDescriptor();
    if (descriptor.isExtension()) {
      name = "[" + descriptor.getFullName() + "]";
    } else {
      name = descriptor.getName();
    }

    // If the MakeProtoFieldNode has any modifiers present, add them
    // in parentheses on the field name.
    List<String> modifiers = new ArrayList<>();
    if (node.getFormat() != FieldFormat.Format.DEFAULT_FORMAT) {
      modifiers.add("format=" + node.getFormat().name());
    }

    if (!modifiers.isEmpty()) {
      name = name + "(" + Joiner.on(",").join(modifiers) + ")";
    }
    return node.getExpr().getNameForDebugStringWithNameFormat(name);
  }

  static String getNameForDebugString(ResolvedOption node) {
    String prefix =
        node.getQualifier().isEmpty()
            ? ""
            : GoogleSQLStrings.toIdentifierLiteral(node.getQualifier()) + ".";
    String nameWithPrefix = prefix + GoogleSQLStrings.toIdentifierLiteral(node.getName());
    if (isDefaultValue(node.getAssignmentOp())) {
      return node.getValue().getNameForDebugStringWithNameFormat(nameWithPrefix);
    }
    return nameWithPrefix + (node.getAssignmentOp() == AssignmentOp.ADD_ASSIGN ? "+=" : "-=");
  }

  static String getNameForDebugString(ResolvedWindowFrame node) {
    return node.nodeKindString() + "(frame_unit=" + node.getFrameUnit().name() + ")";
  }

  static String getNameForDebugString(ResolvedWindowFrameExpr node) {
    return node.nodeKindString()
        + "(boundary_type="
        + node.getBoundaryType().name().replace('_', ' ')
        + ")";
  }

  static String getNameForDebugString(ResolvedSystemVariable node) {
    return node.nodeKindString();
  }
}
