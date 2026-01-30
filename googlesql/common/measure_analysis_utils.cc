//
// Copyright 2019 Google LLC
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//      http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.
//

#include "googlesql/common/measure_analysis_utils.h"

#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "googlesql/base/arena.h"
#include "googlesql/base/atomic_sequence_num.h"
#include "googlesql/common/internal_analyzer_options.h"
#include "googlesql/common/internal_value.h"
#include "googlesql/common/measure_utils.h"
#include "googlesql/public/analyzer.h"
#include "googlesql/public/analyzer_options.h"
#include "googlesql/public/catalog.h"
#include "googlesql/public/id_string.h"
#include "googlesql/public/language_options.h"
#include "googlesql/public/options.pb.h"
#include "googlesql/public/proto_util.h"
#include "googlesql/public/simple_catalog.h"
#include "googlesql/public/types/annotation.h"
#include "googlesql/public/types/struct_type.h"
#include "googlesql/public/types/type.h"
#include "googlesql/public/types/type_factory.h"
#include "googlesql/resolved_ast/column_factory.h"
#include "googlesql/resolved_ast/resolved_ast.h"
#include "googlesql/resolved_ast/resolved_ast_deep_copy_visitor.h"
#include "googlesql/resolved_ast/resolved_ast_visitor.h"
#include "googlesql/resolved_ast/resolved_node.h"
#include "googlesql/resolved_ast/resolved_node_kind.pb.h"
#include "googlesql/base/case.h"
#include "absl/container/flat_hash_set.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/match.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/string_view.h"
#include "absl/types/span.h"
#include "google/protobuf/descriptor.h"
#include "googlesql/base/ret_check.h"
#include "googlesql/base/status_builder.h"
#include "googlesql/base/status_macros.h"

namespace googlesql {

namespace {

// Creates a measure column from a measure expression.
// The `language_options` is used to validate the `resolved_measure_expr`.
absl::StatusOr<std::unique_ptr<SimpleColumn>> CreateMeasureColumn(
    absl::string_view table_name, absl::string_view measure_name,
    absl::string_view measure_expr, const ResolvedExpr& resolved_measure_expr,
    const LanguageOptions& language_options, TypeFactory& type_factory,
    bool is_pseudo_column,
    std::optional<std::vector<int>> row_identity_column_indices) {
  GOOGLESQL_ASSIGN_OR_RETURN(const Type* measure_type,
                   type_factory.MakeMeasureType(resolved_measure_expr.type()));
  GOOGLESQL_ASSIGN_OR_RETURN(
      Column::ExpressionAttributes expr_attributes,
      Column::ExpressionAttributes::Create(
          Column::ExpressionAttributes::ExpressionKind::MEASURE_EXPRESSION,
          std::string(measure_expr), &resolved_measure_expr,
          std::move(row_identity_column_indices)));
  return std::make_unique<SimpleColumn>(
      table_name, measure_name,
      AnnotatedType(measure_type, /*annotation_map=*/nullptr),
      /*attributes=*/
      SimpleColumn::Attributes{
          .is_pseudo_column = is_pseudo_column,
          .is_writable_column = false,
          .column_expression = std::move(expr_attributes)});
}

absl::Status EnsureNoDuplicateColumnNames(const Table& table) {
  absl::flat_hash_set<std::string, googlesql_base::StringViewCaseHash,
                      googlesql_base::StringViewCaseEqual>
      column_names;
  for (int i = 0; i < table.NumColumns(); i++) {
    const Column* column = table.GetColumn(i);
    if (!column_names.insert(column->Name()).second) {
      return googlesql_base::InvalidArgumentErrorBuilder()
             << "Measures cannot be defined on tables with duplicate column "
                "names. Table: "
             << table.Name() << ". Duplicate column name: " << column->Name();
    }
  }
  return absl::OkStatus();
}

// Wraps an `ExpressionColumn` with a `GetStructField` that accesses
// `field_name` from `struct_type`.
// Return an error if `field_name` is found, but ambiguous.
// Return false if `field_name` is not found.
// Return true if `field_name` is found and non-ambiguous.
absl::StatusOr<bool> WrapExpressionColumnWithStructFieldAccess(
    const StructType* struct_type, absl::string_view field_name,
    absl::string_view table_name,
    std::unique_ptr<ResolvedExpressionColumn> expression_column,
    std::unique_ptr<const ResolvedExpr>& resolved_expr_out) {
  bool is_ambiguous = false;
  int struct_field_index = -1;
  const StructField* struct_field =
      struct_type->FindField(field_name, &is_ambiguous, &struct_field_index);
  if (is_ambiguous) {
    return absl::InvalidArgumentError(absl::StrCat(
        "Field ", field_name, " is ambiguous in value table: ", table_name,
        " of type: ", struct_type->TypeName(googlesql::PRODUCT_INTERNAL)));
  }
  if (struct_field == nullptr) {
    return false;
  }
  GOOGLESQL_RET_CHECK(struct_field->type != nullptr);
  GOOGLESQL_RET_CHECK(!struct_field->type->IsMeasureType());
  GOOGLESQL_RET_CHECK(struct_field_index >= 0 &&
            struct_field_index < struct_type->num_fields());
  resolved_expr_out = MakeResolvedGetStructField(
      struct_field->type, std::move(expression_column), struct_field_index);
  return true;
}

// Wraps an `ExpressionColumn` with a `GetProtoField` that accesses
// `field_name` from `proto_type`.
// Return an error if `field_name` is found, but ambiguous.
// Return false if `field_name` is not found.
// Return true if `field_name` is found and non-ambiguous.
absl::StatusOr<bool> WrapExpressionColumnWithProtoFieldAccess(
    const ProtoType* proto_type, absl::string_view field_name,
    absl::string_view table_name, LanguageOptions language_options,
    TypeFactory* type_factory,
    std::unique_ptr<ResolvedExpressionColumn> expression_column,
    std::unique_ptr<const ResolvedExpr>& resolved_expr_out) {
  const google::protobuf::Descriptor* descriptor = proto_type->descriptor();
  const google::protobuf::FieldDescriptor* found_field_descriptor = nullptr;
  for (int i = 0; i < descriptor->field_count(); ++i) {
    if (googlesql_base::CaseEqual(descriptor->field(i)->name(), field_name)) {
      if (found_field_descriptor != nullptr) {
        return absl::InvalidArgumentError(absl::StrCat(
            "Field ", field_name, " is ambiguous in value table: ", table_name,
            " of type: ",
            proto_type->TypeName(language_options.product_mode())));
      }
      found_field_descriptor = descriptor->field(i);
    }
  }
  if (found_field_descriptor == nullptr) {
    return false;
  }

  const Type* field_type = nullptr;
  Value default_value;
  GOOGLESQL_RETURN_IF_ERROR(GetProtoFieldTypeAndDefault(
      ProtoFieldDefaultOptions::FromFieldAndLanguage(found_field_descriptor,
                                                     language_options),
      found_field_descriptor, proto_type->CatalogNamePath(), type_factory,
      &field_type, &default_value));
  resolved_expr_out = MakeResolvedGetProtoField(
      field_type, std::move(expression_column), found_field_descriptor,
      default_value, /*get_has_bit=*/false,
      ProtoType::GetFormatAnnotation(found_field_descriptor),
      /*return_default_value_when_unset=*/false);
  return true;
}

// Resolve `column_name` against the set of non-measure columns in `table`.
// If `table` is a value table, then `column_name` is interpreted as a field
// access within the value table column.
// If `column_name` is not found, then `resolved_expr_out` is not modified.
absl::Status ResolveColumnForMeasureExpression(
    const Table& table, std::string measure_expr, std::string column_name,
    LanguageOptions language_options, TypeFactory* type_factory,
    std::unique_ptr<const ResolvedExpr>& resolved_expr_out) {
  if (!table.IsValueTable()) {
    // Not a value table; just lookup the column and resolve it as an
    // `ExpressionColumn`.
    const Column* column = table.FindColumnByName(column_name);
    if (column == nullptr) {
      return absl::OkStatus();
    }
    GOOGLESQL_RET_CHECK(column->GetType() != nullptr);
    if (IsOrContainsMeasure(column->GetType())) {
      return absl::InvalidArgumentError(absl::StrCat(
          "Measure expression: ", measure_expr, " cannot reference column: ",
          column->Name(), " which contains a measure type"));
    }
    resolved_expr_out =
        MakeResolvedExpressionColumn(column->GetType(), column_name);
    return absl::OkStatus();
  }

  // Value table case. For value tables, the lookup can only reference:
  //   1) The names of fields in the value table column - assuming the value
  //      table column is a PROTO or STRUCT. The value table column name itself
  //      is not visible.
  //   2) Pseudo columns on the value table.
  GOOGLESQL_RET_CHECK(table.NumColumns() > 0);
  const Column* value_table_column = table.GetColumn(0);
  GOOGLESQL_RET_CHECK(value_table_column != nullptr);
  GOOGLESQL_RET_CHECK(!value_table_column->IsPseudoColumn());
  bool found_field = false;
  if (value_table_column->GetType()->IsStructOrProto()) {
    // Construct an expression column for the value table column. We wrap this
    // expression column with a GetStructField or GetProtoField to perform the
    // lookup of a field within the value table column.
    GOOGLESQL_RET_CHECK(!value_table_column->Name().empty());
    const Type* struct_or_proto_type = value_table_column->GetType();
    std::unique_ptr<ResolvedExpressionColumn> expression_column =
        MakeResolvedExpressionColumn(struct_or_proto_type,
                                     value_table_column->Name());
    if (struct_or_proto_type->IsStruct()) {
      GOOGLESQL_ASSIGN_OR_RETURN(
          found_field,
          WrapExpressionColumnWithStructFieldAccess(
              struct_or_proto_type->AsStruct(), column_name, table.Name(),
              std::move(expression_column), resolved_expr_out));
    } else {
      GOOGLESQL_ASSIGN_OR_RETURN(
          found_field,
          WrapExpressionColumnWithProtoFieldAccess(
              struct_or_proto_type->AsProto(), column_name, table.Name(),
              std::move(language_options), type_factory,
              std::move(expression_column), resolved_expr_out));
    }
  }
  // Regardless of whether `found_field` is true or false, we need
  // to check if `column_name` matches any pseudo columns on the value table.
  // There are 4 cases here:
  //   1) `column_name` is not a pseudo column on the value table and
  //      `found_field` is true. `resolved_expr_out` is already correctly set,
  //      so we can return OK.
  //   2) `column_name` is not a pseudo column on the value table, and
  //      `found_field` is false. This means that `column_name` was not found
  //      in the value table.
  //   3) `column_name` is a pseudo column on the value table, and
  //      `found_field` is true. This means that `column_name` is ambiguous
  //      because it matches both a field and a pseudo column.
  //   4) `column_name` is a pseudo column on the value table, and
  //      `found_field` is false. This means that `column_name` resolved as
  //      an expression column for the pseudo column.
  const Column* column = table.FindColumnByName(column_name);
  if (column == nullptr || !column->IsPseudoColumn()) {
    // Case 1 & 2. Return OK, since `resolved_expr_out` should be correctly
    // set for case 1, and not modified for case 2.
    return absl::OkStatus();
  } else {
    if (found_field) {
      // Case 3
      return absl::InvalidArgumentError(absl::StrCat(
          "Column `", column_name, "` is ambiguous in value table `",
          table.Name(), "` for measure expression: ", measure_expr));
    }
    // Case 4
    GOOGLESQL_RET_CHECK(column->GetType() != nullptr);
    if (column->GetType()->IsMeasureType()) {
      return absl::InvalidArgumentError(
          absl::StrCat("Measure expression: ", measure_expr,
                       " cannot reference measure column: ", column->Name()));
    }
    resolved_expr_out =
        MakeResolvedExpressionColumn(column->GetType(), column_name);
  }
  return absl::OkStatus();
}

}  // namespace

absl::StatusOr<const ResolvedExpr*> AnalyzeMeasureExpressionInternal(
    absl::string_view measure_expr, const Table& table, Catalog& catalog,
    TypeFactory& type_factory, AnalyzerOptions analyzer_options,
    std::unique_ptr<const AnalyzerOutput>& analyzer_output) {
  GOOGLESQL_RETURN_IF_ERROR(EnsureNoDuplicateColumnNames(table));
  GOOGLESQL_RET_CHECK(analyzer_options.expression_columns().empty());
  GOOGLESQL_RET_CHECK(
      !InternalAnalyzerOptions::GetLookupExpressionCallback(analyzer_options));
  GOOGLESQL_RET_CHECK(!analyzer_options.has_in_scope_expression_column());
  analyzer_options.mutable_language()->EnableLanguageFeature(
      FEATURE_ENABLE_MEASURES);
  analyzer_options.set_allow_aggregate_standalone_expression(true);

  // Mark the analyzer options that we are in a context specific to analyzing
  // a measure expression.
  analyzer_options
      .SetSuspendLookupExpressionCallbackWhenResolvingTemplatedFunction(true);

  // Disable all rewriters when analyzing measure expressions. These rewriters
  // may result in a measure expression query shapes that the measure expression
  // validator does not recognize as valid. Note that this only impacts the
  // measure expression itself, and not the final query tree the measure
  // expression gets stitched into.
  analyzer_options.set_enabled_rewrites({});

  // Use a callback to resolve expression columns in the measure expression.
  // A callback is necessary to handle the scenario where the measure expression
  // references fields from a value table column. Normal query resolution uses
  // a namescope to handle value table field accesses, but expression columns
  // cannot be NameTargets in a Namescope, so we need to use a callback to
  // resolve them.
  std::string measure_expr_str = std::string(measure_expr);
  LanguageOptions language_options = analyzer_options.language();
  AnalyzerOptions::LookupExpressionCallback callback =
      [&table, &type_factory, measure_expr_str, language_options](
          const std::string& column_name,
          std::unique_ptr<const ResolvedExpr>& resolved_expr_out)
      -> absl::Status {
    return ResolveColumnForMeasureExpression(
        table, measure_expr_str, column_name, std::move(language_options),
        &type_factory, resolved_expr_out);
  };
  InternalAnalyzerOptions::SetLookupExpressionCallback(analyzer_options,
                                                       callback);

  // Deliberately use `local_analyzer_output` instead of `analyzer_output` to
  // ensure that the caller cannot use the output unless the measure validation
  // logic succeeds.
  std::unique_ptr<const AnalyzerOutput> local_analyzer_output;
  GOOGLESQL_RETURN_IF_ERROR(AnalyzeExpression(measure_expr, analyzer_options, &catalog,
                                    &type_factory, &local_analyzer_output));

  // Validate the resolved measure expression.
  const ResolvedExpr* resolved_expr = local_analyzer_output->resolved_expr();
  GOOGLESQL_RET_CHECK(resolved_expr != nullptr);
  // TODO: b/350555383 - Modify the public API to accept a measure column name
  // and pass it to `ValidateMeasureExpression`.
  GOOGLESQL_RETURN_IF_ERROR(ValidateMeasureExpression(measure_expr, *resolved_expr,
                                            analyzer_options.language(),
                                            /*measure_column_name=*/""));
  analyzer_output = std::move(local_analyzer_output);
  return resolved_expr;
}

absl::StatusOr<std::vector<std::unique_ptr<const AnalyzerOutput>>>
AddMeasureColumnsToTable(SimpleTable& table,
                         std::vector<MeasureColumnDef> measures,
                         TypeFactory& type_factory, Catalog& catalog,
                         AnalyzerOptions analyzer_options) {
  std::vector<std::unique_ptr<const AnalyzerOutput>> analyzer_outputs;
  for (const MeasureColumnDef& measure_column : measures) {
    std::unique_ptr<const AnalyzerOutput> analyzer_output;
    GOOGLESQL_ASSIGN_OR_RETURN(const ResolvedExpr* resolved_measure_expr,
                     AnalyzeMeasureExpressionInternal(
                         measure_column.expression, table, catalog,
                         type_factory, analyzer_options, analyzer_output));
    GOOGLESQL_ASSIGN_OR_RETURN(
        std::unique_ptr<SimpleColumn> new_column,
        CreateMeasureColumn(table.Name(), measure_column.name,
                            measure_column.expression, *resolved_measure_expr,
                            analyzer_options.language(), type_factory,
                            measure_column.is_pseudo_column,
                            measure_column.row_identity_column_indices));
    GOOGLESQL_RETURN_IF_ERROR(table.AddColumn(new_column.release(), /*is_owned=*/true));
    analyzer_outputs.push_back(std::move(analyzer_output));
  }
  return analyzer_outputs;
}

absl::StatusOr<Value> UpdateTableRowsWithMeasureValues(
    const Value& array_value, const SimpleTable* simple_table,
    absl::Span<const MeasureColumnDef> measure_column_defs,
    std::vector<int> table_level_row_identity_columns,
    TypeFactory* type_factory, const LanguageOptions& language_options) {
  GOOGLESQL_RET_CHECK(array_value.type()->IsArray());
  GOOGLESQL_RET_CHECK(array_value.type()->AsArray()->element_type()->IsStruct());
  const StructType* row_as_struct_type =
      array_value.type()->AsArray()->element_type()->AsStruct();

  std::vector<StructField> new_row_fields = row_as_struct_type->fields();
  const int num_existing_fields = row_as_struct_type->num_fields();
  const int num_new_columns = simple_table->NumColumns();
  // The number of provided measure definitions must match the number of new
  // measure columns in the table.
  GOOGLESQL_RET_CHECK_EQ(measure_column_defs.size(),
               num_new_columns - num_existing_fields);

  for (int i = num_existing_fields; i < num_new_columns; ++i) {
    const Column* column = simple_table->GetColumn(i);
    GOOGLESQL_RET_CHECK(column->GetType()->IsMeasureType());
    new_row_fields.push_back({column->Name(), column->GetType()});
  }

  const StructType* new_row_as_struct_type = nullptr;
  GOOGLESQL_RET_CHECK_OK(
      type_factory->MakeStructType(new_row_fields, &new_row_as_struct_type));

  std::vector<Value> new_rows_as_struct_values;
  new_rows_as_struct_values.reserve(array_value.elements().size());

  for (const Value& row : array_value.elements()) {
    std::vector<Value> new_row_values;
    new_row_values.reserve(new_row_fields.size());

    for (const Value& column_in_row : row.fields()) {
      new_row_values.push_back(column_in_row);
    }

    // Add measure values.
    for (int i = num_existing_fields; i < new_row_fields.size(); ++i) {
      GOOGLESQL_RET_CHECK(new_row_fields[i].type->IsMeasureType());
      GOOGLESQL_RET_CHECK_LT(i - num_existing_fields, measure_column_defs.size());
      const auto& measure_def = measure_column_defs[i - num_existing_fields];
      // If the measure definition does not specify any row identity columns,
      // then use the table-level row identity columns.
      const std::vector<int>& row_identity_columns =
          measure_def.row_identity_column_indices.has_value()
              ? *measure_def.row_identity_column_indices
              : table_level_row_identity_columns;
      GOOGLESQL_RET_CHECK(!row_identity_columns.empty())
          << "row identity columns cannot be empty";
      GOOGLESQL_ASSIGN_OR_RETURN(
          Value measure_value,
          InternalValue::MakeMeasure(new_row_fields[i].type->AsMeasure(), row,
                                     row_identity_columns, language_options));
      new_row_values.push_back(measure_value);
    }

    auto new_row_as_struct_value =
        Value::MakeStruct(new_row_as_struct_type, new_row_values);
    GOOGLESQL_RET_CHECK_OK(new_row_as_struct_value.status());
    new_rows_as_struct_values.push_back(std::move(*new_row_as_struct_value));
  }

  const ArrayType* new_array_type = nullptr;
  GOOGLESQL_RET_CHECK_OK(
      type_factory->MakeArrayType(new_row_as_struct_type, &new_array_type));
  return Value::MakeArray(new_array_type, new_rows_as_struct_values);
}

}  // namespace googlesql
