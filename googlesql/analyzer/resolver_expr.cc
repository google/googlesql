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

// This file contains the implementation of expression-related resolver methods
// from resolver.h.
#include <stddef.h>

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <cstring>
#include <deque>
#include <functional>
#include <iterator>
#include <limits>
#include <map>
#include <memory>
#include <optional>
#include <set>
#include <stack>
#include <string>
#include <utility>
#include <vector>

#include "googlesql/base/logging.h"
#include "googlesql/base/varsetter.h"

#include "google/protobuf/descriptor.pb.h"
#include "googlesql/analyzer/analytic_function_resolver.h"
#include "googlesql/analyzer/column_cycle_detector.h"
#include "googlesql/analyzer/constant_resolver_helper.h"
#include "googlesql/analyzer/expr_matching_helpers.h"
#include "googlesql/analyzer/expr_resolver_helper.h"
#include "googlesql/analyzer/filter_fields_path_validator.h"
#include "googlesql/analyzer/function_resolver.h"
#include "googlesql/analyzer/graph_expr_resolver_helper.h"
#include "googlesql/analyzer/input_argument_type_resolver_helper.h"
#include "googlesql/analyzer/lambda_util.h"
#include "googlesql/analyzer/name_scope.h"
#include "googlesql/analyzer/named_argument_info.h"
#include "googlesql/analyzer/path_expression_span.h"
#include "googlesql/analyzer/query_resolver_helper.h"
#include "googlesql/analyzer/resolver.h"
#include "googlesql/analyzer/resolver_common_inl.h"
#include "googlesql/analyzer/resolver_util.h"
#include "googlesql/common/constant_utils.h"
#include "googlesql/common/errors.h"
#include "googlesql/common/internal_analyzer_options.h"
#include "googlesql/common/internal_analyzer_output_properties.h"
#include "googlesql/common/measure_utils.h"
#include "googlesql/parser/ast_node.h"
#include "googlesql/parser/ast_node_kind.h"
#include "googlesql/parser/parse_tree.h"
#include "googlesql/parser/parse_tree_errors.h"
#include "googlesql/proto/internal_fix_suggestion.pb.h"
#include "googlesql/public/analyzer_options.h"
#include "googlesql/public/analyzer_output_properties.h"
#include "googlesql/public/annotation/collation.h"
#include "googlesql/public/anon_function.h"
#include "googlesql/public/builtin_function.pb.h"
#include "googlesql/public/cast.h"
#include "googlesql/public/catalog.h"
#include "googlesql/public/catalog_helper.h"
#include "googlesql/public/civil_time.h"
#include "googlesql/public/coercer.h"
#include "googlesql/public/constant.h"
#include "googlesql/public/cycle_detector.h"
#include "googlesql/public/deprecation_warning.pb.h"
#include "googlesql/public/function.h"
#include "googlesql/public/function.pb.h"
#include "googlesql/public/function_signature.h"
#include "googlesql/public/functions/convert_string.h"
#include "googlesql/public/functions/date_time_util.h"
#include "googlesql/public/functions/datetime.pb.h"
#include "googlesql/public/functions/normalize_mode.pb.h"
#include "googlesql/public/functions/range.h"
#include "googlesql/public/functions/regexp.h"
#include "googlesql/public/id_string.h"
#include "googlesql/public/input_argument_type.h"
#include "googlesql/public/interval_value.h"
#include "googlesql/public/json_value.h"
#include "googlesql/public/language_options.h"
#include "googlesql/public/numeric_value.h"
#include "googlesql/public/options.pb.h"
#include "googlesql/public/parse_location.h"
#include "googlesql/public/pico_time.h"
#include "googlesql/public/property_graph.h"
#include "googlesql/public/proto/type_annotation.pb.h"
#include "googlesql/public/proto_util.h"
#include "googlesql/public/signature_match_result.h"
#include "googlesql/public/simple_catalog.h"
#include "googlesql/public/sql_function.h"
#include "googlesql/public/strings.h"
#include "googlesql/public/table_valued_function.h"
#include "googlesql/public/templated_sql_function.h"
#include "googlesql/public/timestamp_picos_value.h"
#include "googlesql/public/type.h"
#include "googlesql/public/type.pb.h"
#include "googlesql/public/types/annotation.h"
#include "googlesql/public/types/array_type.h"
#include "googlesql/public/types/collation.h"
#include "googlesql/public/types/enum_type.h"
#include "googlesql/public/types/proto_type.h"
#include "googlesql/public/types/simple_value.h"
#include "googlesql/public/types/struct_type.h"
#include "googlesql/public/types/type_factory.h"
#include "googlesql/public/types/type_parameters.h"
#include "googlesql/public/value.h"
#include "googlesql/public/with_modifier_mode.h"
#include "googlesql/resolved_ast/resolved_ast.h"
#include "googlesql/resolved_ast/resolved_ast_builder.h"
#include "googlesql/resolved_ast/resolved_ast_deep_copy_visitor.h"
#include "googlesql/resolved_ast/resolved_column.h"
#include "googlesql/resolved_ast/resolved_node.h"
#include "googlesql/resolved_ast/resolved_node_kind.pb.h"
#include "googlesql/resolved_ast/target_syntax.h"
#include "googlesql/base/case.h"
#include "googlesql/base/string_numbers.h"  
#include "absl/algorithm/container.h"
#include "absl/base/attributes.h"
#include "absl/cleanup/cleanup.h"
#include "absl/container/flat_hash_map.h"
#include "absl/container/flat_hash_set.h"
#include "absl/flags/flag.h"
#include "absl/functional/any_invocable.h"
#include "googlesql/base/check.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/ascii.h"
#include "absl/strings/match.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/str_format.h"
#include "absl/strings/str_join.h"
#include "absl/strings/str_replace.h"
#include "absl/strings/string_view.h"
#include "absl/strings/strip.h"
#include "absl/strings/substitute.h"
#include "absl/time/time.h"
#include "absl/types/span.h"
#include "google/protobuf/descriptor.h"
#include "googlesql/base/general_trie.h"
#include "googlesql/base/map_util.h"
#include "googlesql/base/ret_check.h"
#include "googlesql/base/status_macros.h"

// We are making a change to behavior that is already potentially live --
// the behavior of bare array access on proto maps. We think it's unlikely
// that anyone is using this, so we're changing the behavior instantly. This
// flag will be removed in 2023Q2 assuming we hear no reports of it needing
// to be used.
ABSL_FLAG(bool, googlesql_suppress_proto_map_bare_array_subscript_error, false,
          "If true, the error for accessing a protocol buffer map using a bare "
          "array subscript operation is suppressed.");

namespace googlesql {

// These are constant identifiers used mostly for generated column or table
// names.  We use a single IdString for each so we never have to allocate
// or copy these strings again.
STATIC_IDSTRING(kAggregateId, "$aggregate");
STATIC_IDSTRING(kGroupingId, "$grouping_call");
STATIC_IDSTRING(kExprSubqueryId, "$expr_subquery");
STATIC_IDSTRING(kOrderById, "$orderby");
STATIC_IDSTRING(kInSubqueryCastId, "$in_subquery_cast");
STATIC_IDSTRING(kKey, "KEY");
STATIC_IDSTRING(kOffset, "OFFSET");
STATIC_IDSTRING(kOrdinal, "ORDINAL");
STATIC_IDSTRING(kSafeKey, "SAFE_KEY");
STATIC_IDSTRING(kSafeOffset, "SAFE_OFFSET");
STATIC_IDSTRING(kSafeOrdinal, "SAFE_ORDINAL");
STATIC_IDSTRING(kGraphTableId, "$graph_table");
STATIC_IDSTRING(kHorizontalAggregateId, "$horizontal_aggregate");

namespace {

// Verifies that 'field_descriptor' is an extension corresponding to the same
// message as descriptor, and then returns 'field_descriptor'.
absl::StatusOr<const google::protobuf::FieldDescriptor*> VerifyFieldExtendsMessage(
    const ASTNode* ast_node, const google::protobuf::FieldDescriptor* field_descriptor,
    const google::protobuf::Descriptor* descriptor) {
  const google::protobuf::Descriptor* containing_type_descriptor =
      field_descriptor->containing_type();
  // Verify by full_name rather than by pointer equality to allow for extensions
  // that come from different DescriptorPools. This is tested in the
  // ExternalExtension test in analyzer_test.cc.
  if (descriptor->full_name() != containing_type_descriptor->full_name()) {
    return MakeSqlErrorAt(ast_node)
           << "Proto extension " << field_descriptor->full_name()
           << " extends message " << containing_type_descriptor->full_name()
           << " so cannot be used on an expression with message type "
           << descriptor->full_name();
  }

  return field_descriptor;
}

// If the input type is an array, returns the element annotated type. Otherwise,
// returns `annotated_type`.
static AnnotatedType ElementAnnotatedTypeWithFallbackToSelf(
    AnnotatedType annotated_type) {
  const auto& [type, annotation_map] = annotated_type;
  if (!type->IsArray()) {
    return annotated_type;
  }

  const AnnotationMap* element_annotation_map =
      annotation_map == nullptr ? nullptr
                                : annotation_map->AsStructMap()->field(0);
  return {type->AsArray()->element_type(), element_annotation_map};
}

static std::unique_ptr<ResolvedFlattenedArg> MakeResolvedFlattenedArg(
    AnnotatedType annotated_type) {
  auto arg = MakeResolvedFlattenedArg(annotated_type.type);
  arg->set_type_annotation_map(annotated_type.annotation_map);
  return arg;
}

// Adds 'expr' to the get_field_list for the passed in flatten node.
// Updates the flatten result type accordingly.
absl::Status AddGetFieldToFlatten(std::unique_ptr<const ResolvedExpr> expr,
                                  TypeFactory* type_factory,
                                  ResolvedFlatten* flatten) {
  const Type* type = expr->type();
  // For join-column ROW types, leave the output as a ROW rather than
  // wrapping it in an ARRAY.
  const AnnotationMap* annotation_map = expr->type_annotation_map();
  if (!type->IsArrayLike()) {
    GOOGLESQL_RETURN_IF_ERROR(type_factory->MakeArrayType(expr->type(), &type));
    if (annotation_map != nullptr) {
      auto array_annotation_map = AnnotationMap::Create(type);
      GOOGLESQL_RETURN_IF_ERROR(array_annotation_map->AsStructMap()->CloneIntoField(
          0, annotation_map));
      GOOGLESQL_ASSIGN_OR_RETURN(annotation_map, type_factory->TakeOwnership(
                                           std::move(array_annotation_map)));
    }
  }
  flatten->set_type(type);
  flatten->set_type_annotation_map(annotation_map);
  flatten->add_get_field_list(std::move(expr));
  return absl::OkStatus();
}

static std::string GetTypeNameForPrefixedLiteral(absl::string_view sql,
                                                 const ASTLeaf& leaf_node) {
  int start_offset = leaf_node.location().start().GetByteOffset();
  int end_offset = start_offset;
  while (end_offset < sql.size() && std::isalpha(sql[end_offset])) {
    end_offset++;
  }
  return absl::AsciiStrToUpper(
      absl::ClippedSubstr(sql, start_offset, end_offset - start_offset));
}

inline std::unique_ptr<ResolvedCast> MakeResolvedCast(
    const Type* type, std::unique_ptr<const ResolvedExpr> expr,
    std::unique_ptr<const ResolvedExpr> format,
    std::unique_ptr<const ResolvedExpr> time_zone,
    const TypeModifiers& type_modifiers, bool return_null_on_error,
    const ExtendedCompositeCastEvaluator& extended_conversion_evaluator) {
  auto result = MakeResolvedCast(type, std::move(expr), return_null_on_error,
                                 /*extended_cast=*/nullptr, std::move(format),
                                 std::move(time_zone), type_modifiers);

  if (extended_conversion_evaluator.is_valid()) {
    std::vector<std::unique_ptr<const ResolvedExtendedCastElement>>
        conversion_list;
    for (const ConversionEvaluator& evaluator :
         extended_conversion_evaluator.evaluators()) {
      conversion_list.push_back(MakeResolvedExtendedCastElement(
          evaluator.from_type(), evaluator.to_type(), evaluator.function()));
    }

    result->set_extended_cast(
        MakeResolvedExtendedCast(std::move(conversion_list)));
  }

  return result;
}

bool IsFilterFields(absl::string_view function_sql_name) {
  return googlesql_base::CaseEqual(function_sql_name, "FILTER_FIELDS");
}

absl::Span<const std::string> GetTypeCatalogNamePath(const Type* type) {
  if (type->IsProto()) {
    return type->AsProto()->CatalogNamePath();
  }
  if (type->IsEnum()) {
    return type->AsEnum()->CatalogNamePath();
  }
  return {};
}

// Graph element reference is only allowed in a limited set of expressions.
absl::Status CheckGraphElementExpr(const LanguageOptions& language_options,
                                   const ASTExpression* ast_expr) {
  GOOGLESQL_RET_CHECK(ast_expr->parent() != nullptr);
  const ASTNode* parent = ast_expr->parent();
  switch (parent->node_kind()) {
    case AST_DOT_STAR:
    case AST_DOT_IDENTIFIER:
      // `n.*` and `n.prop` is allowed.
    case AST_FUNCTION_CALL:
      // All built-in functions are allowed. Function signature matcher already
      // does the validation.
      return absl::OkStatus();

    case AST_BINARY_EXPRESSION: {
      switch (parent->GetAsOrDie<ASTBinaryExpression>()->op()) {
        case ASTBinaryExpression::IS_SOURCE_NODE:
        case ASTBinaryExpression::IS_DEST_NODE:
        case ASTBinaryExpression::EQ:
        case ASTBinaryExpression::NE:
        case ASTBinaryExpression::NE2:
          return absl::OkStatus();
        default:
          break;
      }
      break;
    }
    case AST_GRAPH_IS_LABELED_PREDICATE:
      return absl::OkStatus();
    case AST_SELECT_COLUMN:
    case AST_GROUPING_ITEM:
    case AST_PARTITION_BY: {
      if (language_options.LanguageFeatureEnabled(
              FEATURE_SQL_GRAPH_ADVANCED_QUERY)) {
        // When GQL syntax in enabled don't check for graph type in GQL return
        // items. The final return / the shape expr will be checked externally.
        return absl::OkStatus();
      }
      break;
    }
    default:
      break;
  }
  return MakeSqlErrorAt(ast_expr)
         << "Graph element typed expression is not allowed here";
}

absl::Status MakeUnsupportedGroupingFunctionError(
    const ASTFunctionCall* func_call, WithModifierMode mode) {
  ABSL_DCHECK(func_call != nullptr);
  std::string query_kind;
  switch (mode) {
    case WithModifierMode::ANONYMIZATION:
      query_kind = "anonymization";
      break;
    case WithModifierMode::DIFFERENTIAL_PRIVACY:
      query_kind = "differential privacy";
      break;
    case WithModifierMode::AGGREGATION_THRESHOLD:
      query_kind = "aggregation threshold";
      break;
    case WithModifierMode::NONE:
    default:
      return absl::OkStatus();
  }
  return MakeSqlErrorAt(func_call) << absl::StrFormat(
             "GROUPING function is not supported in %s queries", query_kind);
}

}  // namespace

absl::Status Resolver::ResolveBuildProto(
    const ASTNode* ast_type_location, const ProtoType* proto_type,
    const ResolvedScan* input_scan, absl::string_view argument_description,
    absl::string_view query_description,
    std::vector<ResolvedBuildProtoArg>* arguments,
    std::unique_ptr<const ResolvedExpr>* output) {
  const google::protobuf::Descriptor* descriptor = proto_type->descriptor();

  // Required fields we haven't found so far.
  absl::flat_hash_set<const google::protobuf::FieldDescriptor*> missing_required_fields;

  for (int i = 0; i < descriptor->field_count(); ++i) {
    const google::protobuf::FieldDescriptor* field = descriptor->field(i);
    if (field->is_required()) {
      missing_required_fields.insert(field);
    }
  }

  std::map<int, const google::protobuf::FieldDescriptor*> added_tag_number_to_field_map;

  std::vector<std::unique_ptr<const ResolvedMakeProtoField>>
      resolved_make_fields;

  for (int i = 0; i < arguments->size(); ++i) {
    ResolvedBuildProtoArg& argument = (*arguments)[i];
    const google::protobuf::FieldDescriptor* field =
        argument.field_descriptor_path.back();
    const Type* proto_field_type = argument.leaf_field_type;
    if (!field->is_extension()) {
      if (field->is_required()) {
        // Note that required fields may be listed twice, so this erase can
        // be a no-op.  This condition will eventually trigger a duplicate
        // column error below.
        missing_required_fields.erase(field);
      }
    }
    auto insert_result = added_tag_number_to_field_map.insert(
        std::make_pair(field->number(), field));
    if (!insert_result.second) {
      // We can't set the same tag number twice, so this is an error. We make an
      // effort to print simple error messages for common cases.
      const google::protobuf::FieldDescriptor* other_field = insert_result.first->second;
      if (other_field->full_name() == field->full_name()) {
        if (!field->is_extension()) {
          // Very common case (regular field accessed twice) or a very weird
          // case (regular field accessed sometime after accessing an extension
          // field with the same tag number and name).
          return MakeSqlErrorAt(argument.ast_location)
                 << query_description << " has duplicate column name "
                 << ToIdentifierLiteral(field->name())
                 << " so constructing proto field " << field->full_name()
                 << " is ambiguous";
        } else {
          // Less common case (field accessed twice, the second time as an
          // extension field).
          return MakeSqlErrorAt(argument.ast_location)
                 << query_description << " has duplicate extension field "
                 << field->full_name();
        }
      } else {
        // Very strange case (tag number accessed twice, somehow with different
        // names).
        return MakeSqlErrorAt(argument.ast_location)
               << query_description << " refers to duplicate tag number "
               << field->number() << " with proto field names "
               << other_field->full_name() << " and " << field->full_name();
      }
    }

    std::unique_ptr<const ResolvedExpr> expr = std::move(argument.expr);

    // Add coercion if necessary.
    // TODO: Remove input_scan arg and convert to CoerceExprToType
    //     This will be a separate change because it churns test files.
    // TODO: Check coercer->AssignableTo
    if (const absl::Status cast_status =
            function_resolver_->AddCastOrConvertLiteral(
                argument.ast_location, proto_field_type, /*format=*/nullptr,
                /*time_zone=*/nullptr, TypeParameters(), input_scan,
                /*set_has_explicit_type=*/false,
                /*return_null_on_error=*/false, &expr);
        !cast_status.ok()) {
      // Propagate "Out of stack space" errors.
      // TODO
      if (cast_status.code() == absl::StatusCode::kResourceExhausted) {
        return cast_status;
      }
      GOOGLESQL_ASSIGN_OR_RETURN(InputArgumentType input_argument_type,
                       GetInputArgumentTypeForExpr(
                           expr.get(),
                           /*pick_default_type_for_untyped_expr=*/false,
                           analyzer_options()));
      return MakeSqlErrorAt(argument.ast_location)
             << "Could not store value with type "
             << input_argument_type.UserFacingName(product_mode())
             << " into proto field " << field->full_name()
             << " which has SQL type "
             << proto_field_type->ShortTypeName(product_mode());
    }

    resolved_make_fields.emplace_back(MakeResolvedMakeProtoField(
        field, ProtoType::GetFormatAnnotation(field), std::move(expr)));
  }

  if (!missing_required_fields.empty()) {
    std::set<absl::string_view> field_names;  // Sorted list of fields.
    for (const google::protobuf::FieldDescriptor* field : missing_required_fields) {
      field_names.insert(field->name());
    }
    return MakeSqlErrorAt(ast_type_location)
           << "Cannot construct proto " << descriptor->full_name()
           << " because required field"
           << (missing_required_fields.size() > 1 ? "s" : "") << " "
           << absl::StrJoin(field_names, ",") << " "
           << (missing_required_fields.size() > 1 ? "are" : "is") << " missing";
  }

  auto resolved_make_proto =
      MakeResolvedMakeProto(proto_type, std::move(resolved_make_fields));
  MaybeRecordParseLocation(ast_type_location, resolved_make_proto.get());
  *output = std::move(resolved_make_proto);
  return absl::OkStatus();
}

absl::StatusOr<const google::protobuf::FieldDescriptor*> Resolver::FindFieldDescriptor(
    const google::protobuf::Descriptor* descriptor,
    const AliasOrASTPathExpression& alias_or_ast_path_expr,
    const ASTNode* ast_location, int field_index,
    absl::string_view argument_description) {
  const google::protobuf::FieldDescriptor* field_descriptor = nullptr;
  switch (alias_or_ast_path_expr.kind()) {
    case AliasOrASTPathExpression::ALIAS: {
      IdString field_alias = alias_or_ast_path_expr.alias();
      GOOGLESQL_RET_CHECK(!field_alias.empty());
      GOOGLESQL_RET_CHECK(!IsInternalAlias(field_alias));

      field_descriptor = descriptor->FindFieldByLowercaseName(
          absl::AsciiStrToLower(field_alias.ToStringView()));
      break;
    }
    case AliasOrASTPathExpression::AST_PATH_EXPRESSION: {
      const ASTPathExpression* ast_path_expr =
          alias_or_ast_path_expr.ast_path_expr();
      GOOGLESQL_ASSIGN_OR_RETURN(field_descriptor,
                       FindExtensionFieldDescriptor(ast_path_expr, descriptor));
      break;
    }
  }
  if (field_descriptor == nullptr) {
    return MakeSqlErrorAt(ast_location)
           << argument_description << " " << (field_index + 1) << " has name "
           << ToIdentifierLiteral(alias_or_ast_path_expr.alias())
           << " which is not a field in proto " << descriptor->full_name();
  }

  return field_descriptor;
}

absl::StatusOr<const Type*> Resolver::FindProtoFieldType(
    const google::protobuf::FieldDescriptor* field_descriptor,
    const ASTNode* ast_location,
    absl::Span<const std::string> catalog_name_path) {
  // Although the default value is unused, we need to pass it otherwise
  // default validation does not take place. Ideally this should be refactored
  // so that the validation is done separately.
  Value unused_default_value;
  const Type* type = nullptr;
  GOOGLESQL_RETURN_IF_ERROR(GetProtoFieldTypeAndDefault(
                      ProtoFieldDefaultOptions::FromFieldAndLanguage(
                          field_descriptor, language()),
                      field_descriptor, catalog_name_path, type_factory_, &type,
                      &unused_default_value))
      .With(LocationOverride(ast_location));
  if (!type->IsSupportedType(language())) {
    return MakeSqlErrorAt(ast_location)
           << "Proto field " << field_descriptor->full_name()
           << " has unsupported type "
           << type->TypeName(language().product_mode());
  }
  return type;
}

// The extension name can be written in any of these forms.
//   (1) package.ProtoName.field_name
//   (2) catalog.package.ProtoName.field_name
//   (3) `package.ProtoName`.field_name
//   (4) catalog.`package.ProtoName`.field_name
//   (5) package.field_name
//
// The package and catalog names are optional, and could also be multi-part.
// The field_name is always written as the last identifier and cannot be part of
// a quoted name. (The Catalog lookup interface can only find message names, not
// extension field names.)
//
// We'll first try to resolve the full path as a fully qualified extension name
// by looking up the extension in the DescriptorPool of the relevant proto. This
// will resolve scoped extensions that are written in form (1) as well as
// top-level extensions written in form (5).
//
// If a resolution of extension with form (5) fails and weak field fallback
// lookup (FEATURE_WEAK_FIELD_FALLBACK_LOOKUP) is enabled then we attempt to
// find a weak field by field_name such that if a weak field was converted to
// extension it would have matched form (5) exactly.
//
// If we can't find the extension this way, we'll resolve the first N-1 names as
// a type name, which must come out to a ProtoType, and then look for the last
// name as an extension field name. This will find extensions written in form
// (2), (3) or (4).
//
// We look for the message name first using the DescriptorPool attached to the
// proto we are reading from. This lets some cases work where the proto does not
// exist or has an unexpected name in the catalog, like a global_proto_db
// qualified name.
absl::StatusOr<const google::protobuf::FieldDescriptor*>
Resolver::FindExtensionFieldDescriptor(const ASTPathExpression* ast_path_expr,
                                       const google::protobuf::Descriptor* descriptor) {
  // First try to find the extension in the DescriptorPool of the relevant proto
  // using the fully qualified name specified in the path.
  // TODO Ideally we should do this lookup using the Catalog, which will
  // enable finding top-level extensions that are inside a Catalog.
  const std::vector<std::string> extension_path =
      ast_path_expr->ToIdentifierVector();

  const std::string extension_name =
      Catalog::ConvertPathToProtoName(extension_path);

  const google::protobuf::DescriptorPool* descriptor_pool = descriptor->file()->pool();
  if (!extension_name.empty()) {
    const google::protobuf::FieldDescriptor* field_descriptor =
        descriptor_pool->FindExtensionByName(extension_name);
    if (field_descriptor != nullptr) {
      return VerifyFieldExtendsMessage(ast_path_expr, field_descriptor,
                                       descriptor);
    }
  }

  // If we couldn't find the extension in the pool using the specified path, we
  // try to look for the extension inside a message (and point
  // 'extension_scope_descriptor' to that message). But that only works if there
  // are at least two identifiers in the path (e.g., path.to.message.extension).
  const google::protobuf::Descriptor* extension_scope_descriptor = nullptr;
  if (extension_path.size() >= 2) {
    // The type name path is the full extension path without the last element
    // (which is the field name).
    std::vector<std::string> type_name_path = extension_path;
    type_name_path.pop_back();

    GOOGLESQL_ASSIGN_OR_RETURN(extension_scope_descriptor,
                     FindMessageTypeForExtension(
                         ast_path_expr, type_name_path, descriptor_pool,
                         /*return_error_for_non_message=*/true));

    if (extension_scope_descriptor == nullptr) {
      // We didn't find the scope message for the extension. If the extension
      // was written as just (package.ProtoName), without specifying a field,
      // then we can fail with a more helpful error.
      GOOGLESQL_ASSIGN_OR_RETURN(extension_scope_descriptor,
                       FindMessageTypeForExtension(
                           ast_path_expr, extension_path, descriptor_pool,
                           /*return_error_for_non_message=*/false));
      if (extension_scope_descriptor != nullptr) {
        return MakeSqlErrorAt(ast_path_expr)
               << "Expected extension name of the form "
                  "(MessageName.extension_field_name), but "
               << ast_path_expr->ToIdentifierPathString()
               << " is a full message name.  Add the extension field name.";
      }
    } else {
      const google::protobuf::FieldDescriptor* field_descriptor =
          extension_scope_descriptor->FindExtensionByName(
              ast_path_expr->last_name()->GetAsString());
      if (field_descriptor != nullptr) {
        return VerifyFieldExtendsMessage(ast_path_expr, field_descriptor,
                                         descriptor);
      }
    }
  }

  // If the extension was written as a single quoted identifier containing the
  // fully qualified extension name, we try to resolve the extension this way,
  // and if we can, we issue a more specific error message. Otherwise we'll
  // issue the generic extension not found error.
  if (extension_path.size() == 1 && extension_name.empty() &&
      descriptor_pool->FindExtensionByName(
          ast_path_expr->last_name()->GetAsString()) != nullptr) {
    return MakeSqlErrorAt(ast_path_expr)
           << "Specifying the fully qualified extension name as a quoted "
              "identifier is disallowed: "
           << ast_path_expr->ToIdentifierPathString();
  }

  // We couldn't find the extension. If we found the scope message for the
  // extension we can issue a more specific error mentioning the scope message.
  // Otherwise we don't know if the user was looking for an extension scoped
  // within a message or a top-level extension, so we'll issue a generic message
  // that the extension was not found.
  if (extension_scope_descriptor != nullptr) {
    return MakeSqlErrorAt(ast_path_expr->last_name())
           << "Extension "
           << ToIdentifierLiteral(ast_path_expr->last_name()->GetAsIdString())
           << " not found in proto message "
           << extension_scope_descriptor->full_name();
  }
  return MakeSqlErrorAt(ast_path_expr)
         << "Extension " << ast_path_expr->ToIdentifierPathString()
         << " not found";
}

absl::StatusOr<const google::protobuf::Descriptor*> Resolver::FindMessageTypeForExtension(
    const ASTPathExpression* ast_path_expr,
    absl::Span<const std::string> type_name_path,
    const google::protobuf::DescriptorPool* descriptor_pool,
    bool return_error_for_non_message) {
  const std::string message_name =
      Catalog::ConvertPathToProtoName(type_name_path);
  if (!message_name.empty()) {
    const google::protobuf::Descriptor* found_descriptor =
        descriptor_pool->FindMessageTypeByName(message_name);
    if (found_descriptor != nullptr) {
      GOOGLESQL_VLOG(2) << "Found message in proto's DescriptorPool: "
              << found_descriptor->DebugString();
      return found_descriptor;
    }
  }

  const Type* found_type = nullptr;
  const absl::Status find_type_status = catalog_->FindType(
      type_name_path, &found_type, analyzer_options_.find_options());
  if (find_type_status.code() == absl::StatusCode::kNotFound) {
    // We don't give an error if it wasn't found.  That will happen in
    // the caller so it has a chance to try generating a better error.
    return nullptr;
  }
  GOOGLESQL_RETURN_IF_ERROR(find_type_status);
  GOOGLESQL_RET_CHECK(found_type != nullptr);

  if (!found_type->IsProto()) {
    if (return_error_for_non_message) {
      return MakeSqlErrorAt(ast_path_expr)
             << "Path "
             << ast_path_expr->ToIdentifierPathString(
                    /*max_prefix_size=*/type_name_path.size())
             << " resolves to type "
             << found_type->ShortTypeName(product_mode())
             << " but a PROTO type was expected for reading an extension field";
    } else {
      return nullptr;
    }
  }

  return found_type->AsProto()->descriptor();
}

functions::TimestampScale GetTimestampScale(
    const LanguageOptions& language_options) {
  if (language_options.LanguageFeatureEnabled(FEATURE_TIMESTAMP_PICOS)) {
    return functions::kPicoseconds;
  } else if (language_options.LanguageFeatureEnabled(FEATURE_TIMESTAMP_NANOS)) {
    return functions::kNanoseconds;
  } else {
    return functions::kMicroseconds;
  }
}

absl::Status Resolver::MakeResolvedDateOrTimeLiteral(
    const ASTExpression* ast_expr, const TypeKind type_kind,
    absl::string_view literal_string_value,
    std::unique_ptr<const ResolvedExpr>* resolved_expr_out) {
  if (Type::IsSimpleType(type_kind)) {
    const Type* type = types::TypeFromSimpleTypeKind(type_kind);
    GOOGLESQL_RET_CHECK(type != nullptr);
    if (!type->IsSupportedType(language())) {
      return MakeSqlErrorAt(ast_expr)
             << "Type not found: "
             << Type::TypeKindToString(type_kind, language().product_mode());
    }
  }

  std::string string_value;
  switch (type_kind) {
    case TYPE_DATE: {
      int32_t date;
      if (functions::ConvertStringToDate(literal_string_value, &date).ok()) {
        *resolved_expr_out =
            MakeResolvedLiteral(ast_expr, Value::Date(date),
                                /*set_has_explicit_type=*/true);
        return absl::OkStatus();
      }
      break;
    }
    case TYPE_TIMESTAMP: {
      functions::TimestampScale scale = GetTimestampScale(language());

      // All GoogleSQL Timestamps are internally stored with picosecond
      // precision (with 0 padding if needed). We use scale when parsing the
      // timestamp string to enforce the precision limit set by language feature
      // flags.
      PicoTime pico_time;
      int precision = 0;
      if (!functions::ConvertStringToTimestamp(
               literal_string_value, default_time_zone(), scale,
               /*allow_tz_in_str=*/true, &pico_time, &precision)
               .ok()) {
        break;
      }

      // Create Timestamp value.
      auto literal = MakeResolvedLiteral(
          ast_expr, Value::Timestamp(TimestampPicosValue(pico_time)),
          /*set_has_explicit_type=*/true);
      *resolved_expr_out = std::move(literal);

      return absl::OkStatus();
    }
    case TYPE_TIME: {
      TimeValue time;
      functions::TimestampScale scale =
          language().LanguageFeatureEnabled(FEATURE_TIMESTAMP_NANOS)
              ? functions::kNanoseconds
              : functions::kMicroseconds;
      if (functions::ConvertStringToTime(literal_string_value, scale, &time)
              .ok() &&
          time.IsValid()) {
        *resolved_expr_out =
            MakeResolvedLiteral(ast_expr, Value::Time(time),
                                /*set_has_explicit_type=*/true);
        return absl::OkStatus();
      }
      break;
    }
    case TYPE_DATETIME: {
      DatetimeValue datetime;
      functions::TimestampScale scale =
          language().LanguageFeatureEnabled(FEATURE_TIMESTAMP_NANOS)
              ? functions::kNanoseconds
              : functions::kMicroseconds;
      if (functions::ConvertStringToDatetime(literal_string_value, scale,
                                             &datetime)
              .ok() &&
          datetime.IsValid()) {
        *resolved_expr_out =
            MakeResolvedLiteral(ast_expr, Value::Datetime(datetime),
                                /*set_has_explicit_type=*/true);
        return absl::OkStatus();
      }
      break;
    }
    default:
      break;
  }
  return MakeSqlErrorAt(ast_expr)
         << "Invalid " << Type::TypeKindToString(type_kind, product_mode())
         << " literal";
}

absl::Status Resolver::ResolveScalarExpr(
    const ASTExpression* ast_expr, const NameScope* name_scope,
    const char* clause_name, QueryResolutionInfo* query_resolution_info,
    std::unique_ptr<const ResolvedExpr>* resolved_expr_out,
    const Type* inferred_type) {
  auto expr_resolution_info =
      std::make_unique<ExprResolutionInfo>(query_resolution_info, name_scope,
                                           ExprResolutionInfoOptions{
                                               .clause_name = clause_name,
                                           });
  expr_resolution_info->grouping_context = ExprGroupingContext::kPreGrouping;
  return ResolveExpr(ast_expr, expr_resolution_info.get(), resolved_expr_out,
                     inferred_type);
}

absl::StatusOr<std::unique_ptr<const ResolvedLiteral>>
Resolver::ResolveJsonLiteral(const ASTJSONLiteral* json_literal) {
  auto status_or_value = JSONValue::ParseJSONString(
      json_literal->string_literal()->string_value(),
      JSONParsingOptions{
          .wide_number_mode =
              (language().LanguageFeatureEnabled(
                   FEATURE_JSON_STRICT_NUMBER_PARSING)
                   ? JSONParsingOptions::WideNumberMode::kExact
                   : JSONParsingOptions::WideNumberMode::kRound)});
  if (!status_or_value.ok()) {
    return MakeSqlErrorAt(json_literal)
           << "Invalid JSON literal: " << status_or_value.status().message();
  }
  return MakeResolvedLiteral(json_literal,
                             {types::JsonType(), /*annotation_map=*/nullptr},
                             Value::Json(std::move(status_or_value.value())),
                             /*has_explicit_type=*/true);
}

absl::StatusOr<std::unique_ptr<const ResolvedLiteral>>
Resolver::ParseRangeBoundary(const TypeKind& type_kind,
                             std::optional<absl::string_view> boundary_value,
                             const LanguageOptions& language,
                             const absl::TimeZone default_time_zone,
                             TypeFactory* type_factory) {
  bool unbounded = !boundary_value.has_value();
  switch (type_kind) {
    case TYPE_DATE: {
      if (unbounded) {
        return googlesql::MakeResolvedLiteral(
            types::DateType(), Value::NullDate(), /*has_explicit_type=*/true);
      }
      int32_t date;
      GOOGLESQL_RETURN_IF_ERROR(functions::ConvertStringToDate(*boundary_value, &date));
      return googlesql::MakeResolvedLiteral(
          types::DateType(), Value::Date(date), /*has_explicit_type=*/true);
    }
    case TYPE_DATETIME: {
      if (unbounded) {
        return googlesql::MakeResolvedLiteral(types::DatetimeType(),
                                              Value::NullDatetime(),
                                              /*has_explicit_type=*/true);
      }
      DatetimeValue datetime;
      functions::TimestampScale scale =
          language.LanguageFeatureEnabled(FEATURE_TIMESTAMP_NANOS)
              ? functions::kNanoseconds
              : functions::kMicroseconds;
      GOOGLESQL_RETURN_IF_ERROR(functions::ConvertStringToDatetime(*boundary_value, scale,
                                                         &datetime));
      if (!datetime.IsValid()) {
        return absl::InvalidArgumentError("Datetime is invalid");
      }
      return googlesql::MakeResolvedLiteral(types::DatetimeType(),
                                            Value::Datetime(datetime),
                                            /*has_explicit_type=*/true);
    }
    case TYPE_TIMESTAMP: {
      Value timestamp_value = Value::NullTimestamp();
      if (!unbounded) {
        functions::TimestampScale scale = GetTimestampScale(language);
        PicoTime pico_time;
        GOOGLESQL_RETURN_IF_ERROR(functions::ConvertStringToTimestamp(
            *boundary_value, default_time_zone, scale,
            /*allow_tz_in_str=*/true, &pico_time));
        timestamp_value = Value::Timestamp(TimestampPicosValue(pico_time));
      }
      const AnnotationMap* annotation_map = nullptr;
      return ResolvedLiteralBuilder()
          .set_value(timestamp_value)
          .set_type(type_factory->get_timestamp())
          .set_type_annotation_map(annotation_map)
          .set_has_explicit_type(true)
          .Build();
    }
    default: {
      return absl::InvalidArgumentError(absl::StrCat(
          "Parsing of RANGE literal of type ",
          Type::TypeKindToString(type_kind, language.product_mode()),
          " is not supported"));
    }
  }
}

absl::StatusOr<std::unique_ptr<const ResolvedLiteral>> Resolver::ParseRange(
    const ASTRangeLiteral* range_literal, const RangeType* range_type,
    const LanguageOptions& language, const absl::TimeZone default_time_zone,
    TypeFactory* type_factory) {
  GOOGLESQL_ASSIGN_OR_RETURN(
      const auto boundaries,
      ParseRangeBoundaries(range_literal->range_value()->string_value()));
  GOOGLESQL_ASSIGN_OR_RETURN(
      std::unique_ptr<const ResolvedLiteral> start,
      ParseRangeBoundary(range_type->element_type()->kind(), boundaries.start,
                         language, default_time_zone, type_factory));
  GOOGLESQL_ASSIGN_OR_RETURN(
      std::unique_ptr<const ResolvedLiteral> end,
      ParseRangeBoundary(range_type->element_type()->kind(), boundaries.end,
                         language, default_time_zone, type_factory));
  GOOGLESQL_ASSIGN_OR_RETURN(auto range, Value::MakeRange(start->value(), end->value()));

  // Resolve range as function constructor to calculate annotations.
  std::vector<std::unique_ptr<const ResolvedExpr>> resolved_arguments;
  resolved_arguments.push_back(std::move(start));
  resolved_arguments.push_back(std::move(end));
  std::unique_ptr<ResolvedFunctionCall> resolved_function_call;
  GOOGLESQL_RETURN_IF_ERROR(function_resolver_->ResolveGeneralFunctionCall(
      range_literal, {range_literal, range_literal},
      /*match_internal_signatures=*/false, "range",
      /*is_analytic=*/false, std::move(resolved_arguments),
      /*named_arguments=*/{}, /*expected_result_type=*/range_type,
      &resolved_function_call));

  return MakeResolvedLiteral(
      range_literal,
      {range_type, resolved_function_call->type_annotation_map()}, range,
      /*has_explicit_type=*/true);
}

absl::StatusOr<std::unique_ptr<const ResolvedLiteral>>
Resolver::ResolveRangeLiteral(const ASTRangeLiteral* range_literal) {
  RETURN_ERROR_IF_OUT_OF_STACK_SPACE();
  const RangeType* range_type = nullptr;
  GOOGLESQL_RETURN_IF_ERROR(ResolveRangeType(range_literal->type(),
                                   {.context = "literal value construction"},
                                   &range_type,
                                   /*resolved_type_modifiers=*/nullptr));
  if (range_literal->range_value() == nullptr) {
    return MakeSqlErrorAt(range_literal)
           << "Invalid range literal. Expected RANGE keyword to be followed by "
              "a STRING literal";
  }

  // Parse range, wrapping any error with a custom message.
  absl::StatusOr<std::unique_ptr<const ResolvedLiteral>> range =
      ParseRange(range_literal, range_type, language(), default_time_zone(),
                 type_factory_);
  if (!range.ok()) {
    return MakeSqlErrorAt(range_literal)
           << "Invalid RANGE literal value: " << range.status().message();
  }
  return range;
}

absl::Status Resolver::ResolveExpr(
    const ASTExpression* ast_expr,
    ExprResolutionInfo* parent_expr_resolution_info,
    std::unique_ptr<const ResolvedExpr>* resolved_expr_out,
    const Type* inferred_type) {
  RETURN_ERROR_IF_OUT_OF_STACK_SPACE();
  ABSL_DCHECK(parent_expr_resolution_info != nullptr);

  // Use a separate ExprAggregationInfo for the child because we don't
  // want it to observe <has_aggregation>, <has_analytic>, or <can_flatten> from
  // a sibling. <has_aggregation> and <has_analytic> need to flow up the tree
  // only, and not across. <can_flatten> needs to be selectively propagated.
  std::unique_ptr<ExprResolutionInfo> expr_resolution_info =
      std::make_unique<ExprResolutionInfo>(  // Save stack for
          parent_expr_resolution_info);      // nested exprs.

  switch (ast_expr->node_kind()) {
    // These cases are extracted into a separate method to reduce stack usage.
    case AST_INT_LITERAL:
    case AST_STRING_LITERAL:
    case AST_BYTES_LITERAL:
    case AST_BOOLEAN_LITERAL:
    case AST_FLOAT_LITERAL:
    case AST_NULL_LITERAL:
    case AST_DATE_OR_TIME_LITERAL:
    case AST_NUMERIC_LITERAL:
    case AST_BIGNUMERIC_LITERAL:
    case AST_JSON_LITERAL:
    case AST_RANGE_LITERAL:
      GOOGLESQL_RETURN_IF_ERROR(ResolveLiteralExpr(ast_expr, resolved_expr_out));
      break;

    case AST_STAR:
      return MakeSqlErrorAt(ast_expr)
             << "Argument * can only be used in COUNT(*)"
             << (language().LanguageFeatureEnabled(FEATURE_ANONYMIZATION)
                     ? " or ANON_COUNT(*)"
                     : "");

    case AST_DOT_STAR:
    case AST_DOT_STAR_WITH_MODIFIERS:
      // This is expected to be unreachable as parser allows creation of
      // dot star nodes only inside SELECT expression.
      return MakeSqlErrorAt(ast_expr)
             << "Dot-star is only supported in SELECT expression";

    case AST_PATH_EXPRESSION:
      expr_resolution_info->flatten_state.SetParent(
          &parent_expr_resolution_info->flatten_state);
      GOOGLESQL_RETURN_IF_ERROR(ResolvePathExpressionAsExpression(
          PathExpressionSpan(*ast_expr->GetAsOrDie<ASTPathExpression>()),
          expr_resolution_info.get(), ResolvedStatement::READ,
          resolved_expr_out));
      break;

    case AST_PARAMETER_EXPR:
      GOOGLESQL_RETURN_IF_ERROR(ResolveParameterExpr(
          ast_expr->GetAsOrDie<ASTParameterExpr>(), resolved_expr_out));
      break;

    case AST_DOT_IDENTIFIER:
      expr_resolution_info->flatten_state.SetParent(
          &parent_expr_resolution_info->flatten_state);
      GOOGLESQL_RETURN_IF_ERROR(
          ResolveDotIdentifier(ast_expr->GetAsOrDie<ASTDotIdentifier>(),
                               expr_resolution_info.get(), resolved_expr_out));
      break;

    case AST_DOT_GENERALIZED_FIELD:
      expr_resolution_info->flatten_state.SetParent(
          &parent_expr_resolution_info->flatten_state);
      GOOGLESQL_RETURN_IF_ERROR(ResolveDotGeneralizedField(
          ast_expr->GetAsOrDie<ASTDotGeneralizedField>(),
          expr_resolution_info.get(), resolved_expr_out));
      break;

    case AST_UNARY_EXPRESSION:
      GOOGLESQL_RETURN_IF_ERROR(
          ResolveUnaryExpr(ast_expr->GetAsOrDie<ASTUnaryExpression>(),
                           expr_resolution_info.get(), resolved_expr_out));
      break;

    case AST_BINARY_EXPRESSION:
      GOOGLESQL_RETURN_IF_ERROR(
          ResolveBinaryExpr(ast_expr->GetAsOrDie<ASTBinaryExpression>(),
                            expr_resolution_info.get(), resolved_expr_out));
      break;

    case AST_BITWISE_SHIFT_EXPRESSION:
      GOOGLESQL_RETURN_IF_ERROR(ResolveBitwiseShiftExpr(
          ast_expr->GetAsOrDie<ASTBitwiseShiftExpression>(),
          expr_resolution_info.get(), resolved_expr_out));
      break;

    case AST_IN_EXPRESSION:
      GOOGLESQL_RETURN_IF_ERROR(ResolveInExpr(ast_expr->GetAsOrDie<ASTInExpression>(),
                                    expr_resolution_info.get(),
                                    resolved_expr_out));
      break;

    case AST_LIKE_EXPRESSION:
      GOOGLESQL_RETURN_IF_ERROR(ResolveLikeExpr(ast_expr->GetAsOrDie<ASTLikeExpression>(),
                                      expr_resolution_info.get(),
                                      resolved_expr_out));
      break;

    case AST_QUANTIFIED_COMPARISON_EXPRESSION:
      GOOGLESQL_RETURN_IF_ERROR(ResolveQuantifiedComparisonExpr(
          ast_expr->GetAsOrDie<ASTQuantifiedComparisonExpression>(),
          expr_resolution_info.get(), resolved_expr_out));
      break;

    case AST_BETWEEN_EXPRESSION:
      GOOGLESQL_RETURN_IF_ERROR(
          ResolveBetweenExpr(ast_expr->GetAsOrDie<ASTBetweenExpression>(),
                             expr_resolution_info.get(), resolved_expr_out));
      break;

    case AST_AND_EXPR:
      GOOGLESQL_RETURN_IF_ERROR(ResolveAndExpr(ast_expr->GetAsOrDie<ASTAndExpr>(),
                                     expr_resolution_info.get(),
                                     resolved_expr_out));
      break;

    case AST_OR_EXPR:
      GOOGLESQL_RETURN_IF_ERROR(ResolveOrExpr(ast_expr->GetAsOrDie<ASTOrExpr>(),
                                    expr_resolution_info.get(),
                                    resolved_expr_out));
      break;

    case AST_FUNCTION_CALL:
      GOOGLESQL_RETURN_IF_ERROR(
          ResolveFunctionCall(ast_expr->GetAsOrDie<ASTFunctionCall>(),
                              expr_resolution_info.get(), resolved_expr_out));
      break;

    case AST_CAST_EXPRESSION:
      GOOGLESQL_RETURN_IF_ERROR(
          ResolveExplicitCast(ast_expr->GetAsOrDie<ASTCastExpression>(),
                              expr_resolution_info.get(), resolved_expr_out));
      break;

    case AST_ARRAY_ELEMENT:
      expr_resolution_info->flatten_state.SetParent(
          &parent_expr_resolution_info->flatten_state);
      GOOGLESQL_RETURN_IF_ERROR(
          ResolveArrayElement(ast_expr->GetAsOrDie<ASTArrayElement>(),
                              expr_resolution_info.get(), resolved_expr_out));
      break;

    case AST_CASE_VALUE_EXPRESSION:
      GOOGLESQL_RETURN_IF_ERROR(ResolveCaseValueExpression(
          ast_expr->GetAsOrDie<ASTCaseValueExpression>(),
          expr_resolution_info.get(), resolved_expr_out));
      break;

    case AST_CASE_NO_VALUE_EXPRESSION:
      GOOGLESQL_RETURN_IF_ERROR(ResolveCaseNoValueExpression(
          ast_expr->GetAsOrDie<ASTCaseNoValueExpression>(),
          expr_resolution_info.get(), resolved_expr_out));
      break;

    case AST_EXTRACT_EXPRESSION:
      GOOGLESQL_RETURN_IF_ERROR(ResolveExtractExpression(
          ast_expr->GetAsOrDie<ASTExtractExpression>(),
          expr_resolution_info.get(), resolved_expr_out));
      break;

    case AST_EXPRESSION_SUBQUERY:
      GOOGLESQL_RETURN_IF_ERROR(ResolveExprSubquery(
          ast_expr->GetAsOrDie<ASTExpressionSubquery>(),
          expr_resolution_info.get(), inferred_type, resolved_expr_out));
      break;

    case AST_NEW_CONSTRUCTOR:
      GOOGLESQL_RETURN_IF_ERROR(
          ResolveNewConstructor(ast_expr->GetAsOrDie<ASTNewConstructor>(),
                                expr_resolution_info.get(), resolved_expr_out));
      break;

    case AST_BRACED_NEW_CONSTRUCTOR:
      GOOGLESQL_RETURN_IF_ERROR(ResolveBracedNewConstructor(
          ast_expr->GetAsOrDie<ASTBracedNewConstructor>(),
          expr_resolution_info.get(), resolved_expr_out));
      break;

    case AST_STRUCT_BRACED_CONSTRUCTOR:
      GOOGLESQL_RETURN_IF_ERROR(ResolveStructBracedConstructor(
          ast_expr->GetAsOrDie<ASTStructBracedConstructor>(), inferred_type,
          expr_resolution_info.get(), resolved_expr_out));
      break;

    case AST_BRACED_CONSTRUCTOR:
      GOOGLESQL_RETURN_IF_ERROR(ResolveBracedConstructor(
          ast_expr->GetAsOrDie<ASTBracedConstructor>(), inferred_type,
          expr_resolution_info.get(), resolved_expr_out));
      break;

    case AST_UPDATE_CONSTRUCTOR:
      GOOGLESQL_RETURN_IF_ERROR(ResolveUpdateConstructor(
          *ast_expr->GetAsOrDie<ASTUpdateConstructor>(),
          expr_resolution_info.get(), resolved_expr_out));
      break;

    case AST_ARRAY_CONSTRUCTOR:
      GOOGLESQL_RETURN_IF_ERROR(ResolveArrayConstructor(
          ast_expr->GetAsOrDie<ASTArrayConstructor>(), inferred_type,
          expr_resolution_info.get(), resolved_expr_out));
      break;

    case AST_STRUCT_CONSTRUCTOR_WITH_PARENS:
      GOOGLESQL_RETURN_IF_ERROR(ResolveStructConstructorWithParens(
          ast_expr->GetAsOrDie<ASTStructConstructorWithParens>(), inferred_type,
          expr_resolution_info.get(), resolved_expr_out));
      break;

    case AST_STRUCT_CONSTRUCTOR_WITH_KEYWORD:
      GOOGLESQL_RETURN_IF_ERROR(ResolveStructConstructorWithKeyword(
          ast_expr->GetAsOrDie<ASTStructConstructorWithKeyword>(),
          inferred_type, expr_resolution_info.get(), resolved_expr_out));
      break;

    case AST_ANALYTIC_FUNCTION_CALL:
      if (!language().LanguageFeatureEnabled(FEATURE_ANALYTIC_FUNCTIONS)) {
        return MakeSqlErrorAtLocalNode(ast_expr)
               << "Analytic functions not supported";
      }
      if (generated_column_cycle_detector_ != nullptr) {
        return MakeSqlErrorAtLocalNode(ast_expr)
               << "Analytic functions cannot be used inside generated columns";
      }
      if (default_expr_access_error_name_scope_.has_value()) {
        return MakeSqlErrorAtLocalNode(ast_expr)
               << "Analytic functions cannot be used "
                  "inside a column default expression";
      }
      GOOGLESQL_RETURN_IF_ERROR(ResolveAnalyticFunctionCall(
          ast_expr->GetAsOrDie<ASTAnalyticFunctionCall>(),
          expr_resolution_info.get(), resolved_expr_out));
      break;

    case AST_INTERVAL_EXPR:
      // The functions that expect an INTERVAL expression handle them specially
      // while resolving the argument list, and only in the locations where they
      // are expected. All other cases end up here.
      if (!language().LanguageFeatureEnabled(FEATURE_INTERVAL_TYPE)) {
        return MakeSqlErrorAt(ast_expr) << "Unexpected INTERVAL expression";
      }
      GOOGLESQL_RETURN_IF_ERROR(
          ResolveIntervalExpr(ast_expr->GetAsOrDie<ASTIntervalExpr>(),
                              expr_resolution_info.get(), resolved_expr_out));
      break;

    case AST_REPLACE_FIELDS_EXPRESSION:
      GOOGLESQL_RETURN_IF_ERROR(ResolveReplaceFieldsExpression(
          ast_expr->GetAsOrDie<ASTReplaceFieldsExpression>(),
          expr_resolution_info.get(), resolved_expr_out));
      break;

    case AST_SYSTEM_VARIABLE_EXPR:
      GOOGLESQL_RETURN_IF_ERROR(ResolveSystemVariableExpression(
          ast_expr->GetAsOrDie<ASTSystemVariableExpr>(),
          expr_resolution_info.get(), resolved_expr_out));
      break;

    case AST_NAMED_ARGUMENT:
      // Resolve named arguments for function calls by simply resolving the
      // expression part of the argument. The function resolver will apply
      // special handling to inspect the name part and integrate into function
      // signature matching appropriately.
      //
      // Named lambdas should be filtered out already. They will be resolved
      // in the function resolver instead.
      GOOGLESQL_RET_CHECK(
          !ast_expr->GetAsOrDie<ASTNamedArgument>()->expr()->Is<ASTLambda>());
      GOOGLESQL_RETURN_IF_ERROR(
          ResolveExpr(ast_expr->GetAsOrDie<ASTNamedArgument>()->expr(),
                      expr_resolution_info.get(), resolved_expr_out));
      break;

    case AST_WITH_EXPRESSION:
      GOOGLESQL_RETURN_IF_ERROR(ResolveWithExpr(ast_expr->GetAsOrDie<ASTWithExpression>(),
                                      expr_resolution_info.get(),
                                      resolved_expr_out));
      break;
    case AST_EXPRESSION_WITH_ALIAS:
      // Alias is not needed to resolve the expression.
      GOOGLESQL_RETURN_IF_ERROR(ResolveExpr(
          ast_expr->GetAsOrDie<ASTExpressionWithAlias>()->expression(),
          expr_resolution_info.get(), resolved_expr_out));
      break;
    case AST_GRAPH_IS_LABELED_PREDICATE:
      GOOGLESQL_RETURN_IF_ERROR(ResolveGraphIsLabeledPredicate(
          ast_expr->GetAsOrDie<ASTGraphIsLabeledPredicate>(),
          expr_resolution_info.get(), resolved_expr_out));
      break;

    default:
      return MakeSqlErrorAt(ast_expr)
             << "Unhandled select-list expression for node kind "
             << ast_expr->GetNodeKindString() << ":\n"
             << ast_expr->DebugString();
  }

  GOOGLESQL_RET_CHECK(resolved_expr_out->get() != nullptr);
  if (resolved_expr_out->get()->GetParseLocationOrNULL() == nullptr) {
    MaybeRecordParseLocation(
        ast_expr, const_cast<ResolvedExpr*>(resolved_expr_out->get()));
  }

  // Note: This call checks that graph element types cannot be returned from
  // most expressions.  This cannot currently detect when the expression
  // occurred as the base argument in a chained function call, which is a hole.
  // For graph element functions, it is acceptable, particularly since the
  // feature flag below disables these error checks.
  // If any other checks are added here, watch out for chained calls.
  if (resolved_expr_out->get()->type()->IsGraphElement() &&
      !language().LanguageFeatureEnabled(
          FEATURE_SQL_GRAPH_EXPOSE_GRAPH_ELEMENT)) {
    return CheckGraphElementExpr(language(), ast_expr);
  }
  return absl::OkStatus();
}

absl::Status Resolver::ResolveWhere(
    const ASTWhereClause* ast_where, const NameScope* name_scope,
    const char* clause_name, QueryResolutionInfo* query_resolution_info,
    std::unique_ptr<const ResolvedExpr>* resolved_expr) {
  if (ast_where == nullptr || ast_where->expression() == nullptr) {
    return absl::OkStatus();
  }
  GOOGLESQL_RETURN_IF_ERROR(ResolveScalarExpr(ast_where->expression(), name_scope,
                                    clause_name, query_resolution_info,
                                    resolved_expr));
  GOOGLESQL_RETURN_IF_ERROR(
      CoerceExprToBool(ast_where->expression(), clause_name, resolved_expr));
  return absl::OkStatus();
}

absl::Status Resolver::ResolveLiteralExpr(
    const ASTExpression* ast_expr,
    std::unique_ptr<const ResolvedExpr>* resolved_expr_out) {
  switch (ast_expr->node_kind()) {
    case AST_INT_LITERAL: {
      // Note: Negative integer literals are handled by a special case for MINUS
      // in ResolveUnaryExpr().
      const ASTIntLiteral* literal = ast_expr->GetAsOrDie<ASTIntLiteral>();
      Value value;
      if (!Value::ParseInteger(literal->image(), &value)) {
        if (literal->is_hex()) {
          return MakeSqlErrorAt(literal)
                 << "Invalid hex integer literal: " << literal->image();
        } else {
          return MakeSqlErrorAt(literal)
                 << "Invalid integer literal: " << literal->image();
        }
      }
      GOOGLESQL_RET_CHECK(!value.is_null());
      GOOGLESQL_RET_CHECK(value.type_kind() == TYPE_INT64 ||
                value.type_kind() == TYPE_UINT64)
          << value.DebugString();

      if (product_mode() == PRODUCT_EXTERNAL &&
          value.type_kind() == TYPE_UINT64) {
        if (literal->is_hex()) {
          return MakeSqlErrorAt(literal)
                 << "Invalid hex integer literal: " << literal->image();
        } else {
          return MakeSqlErrorAt(literal)
                 << "Invalid integer literal: " << literal->image();
        }
      }
      *resolved_expr_out = MakeResolvedLiteral(ast_expr, value);
      return absl::OkStatus();
    }

    case AST_STRING_LITERAL: {
      const ASTStringLiteral* literal =
          ast_expr->GetAsOrDie<ASTStringLiteral>();
      if (literal->components().size() > 1 &&
          !language().LanguageFeatureEnabled(FEATURE_LITERAL_CONCATENATION)) {
        return MakeSqlErrorAt(literal->components()[1])
               << "Concatenation of subsequent string literals is not "
                  "supported. Did you mean to use the || operator?";
      }
      *resolved_expr_out =
          MakeResolvedLiteral(ast_expr, Value::String(literal->string_value()));
      return absl::OkStatus();
    }

    case AST_BYTES_LITERAL: {
      const ASTBytesLiteral* literal = ast_expr->GetAsOrDie<ASTBytesLiteral>();
      if (literal->components().size() > 1 &&
          !language().LanguageFeatureEnabled(FEATURE_LITERAL_CONCATENATION)) {
        return MakeSqlErrorAt(literal->components()[1])
               << "Concatenation of subsequent bytes literals is not "
                  "supported. Did you mean to use the || operator?";
      }
      *resolved_expr_out =
          MakeResolvedLiteral(ast_expr, Value::Bytes(literal->bytes_value()));
      return absl::OkStatus();
    }

    case AST_BOOLEAN_LITERAL: {
      const ASTBooleanLiteral* literal =
          ast_expr->GetAsOrDie<ASTBooleanLiteral>();
      *resolved_expr_out =
          MakeResolvedLiteral(ast_expr, Value::Bool(literal->value()));
      return absl::OkStatus();
    }

    case AST_FLOAT_LITERAL: {
      const ASTFloatLiteral* literal = ast_expr->GetAsOrDie<ASTFloatLiteral>();
      double double_value;
      if (!functions::StringToNumeric(literal->image(), &double_value,
                                      nullptr)) {
        return MakeSqlErrorAt(literal)
               << "Invalid floating point literal: " << literal->image();
      }
      std::unique_ptr<const ResolvedLiteral> resolved_literal =
          MakeResolvedFloatLiteral(
              ast_expr, types::DoubleType(), Value::Double(double_value),
              /*has_explicit_type=*/false, literal->image());
      *resolved_expr_out = std::move(resolved_literal);
      return absl::OkStatus();
    }

    case AST_NUMERIC_LITERAL: {
      const ASTNumericLiteral* literal =
          ast_expr->GetAsOrDie<ASTNumericLiteral>();
      if (!language().LanguageFeatureEnabled(FEATURE_NUMERIC_TYPE)) {
        std::string error_type_token =
            GetTypeNameForPrefixedLiteral(sql_, *literal);
        return MakeSqlErrorAt(literal)
               << error_type_token << " literals are not supported";
      }

      absl::string_view string_value =
          literal->string_literal()->string_value();
      auto value_or_status = NumericValue::FromStringStrict(string_value);
      if (!value_or_status.status().ok()) {
        std::string error_type_token =
            GetTypeNameForPrefixedLiteral(sql_, *literal);
        return MakeSqlErrorAt(literal)
               << "Invalid " << error_type_token
               << " literal: " << ToStringLiteral(string_value);
      }
      *resolved_expr_out = MakeResolvedLiteral(
          ast_expr, {types::NumericType(), /*annotation_map=*/nullptr},
          Value::Numeric(value_or_status.value()), /*has_explicit_type=*/true);
      return absl::OkStatus();
    }

    case AST_BIGNUMERIC_LITERAL: {
      const ASTBigNumericLiteral* literal =
          ast_expr->GetAsOrDie<ASTBigNumericLiteral>();
      if (!language().LanguageFeatureEnabled(FEATURE_BIGNUMERIC_TYPE)) {
        std::string error_type_token =
            GetTypeNameForPrefixedLiteral(sql_, *literal);
        return MakeSqlErrorAt(literal)
               << error_type_token << " literals are not supported";
      }
      absl::string_view string_value =
          literal->string_literal()->string_value();
      auto value_or_status = BigNumericValue::FromStringStrict(string_value);
      if (!value_or_status.ok()) {
        std::string error_type_token =
            GetTypeNameForPrefixedLiteral(sql_, *literal);
        return MakeSqlErrorAt(literal)
               << "Invalid " << error_type_token
               << " literal: " << ToStringLiteral(string_value);
      }
      *resolved_expr_out = MakeResolvedLiteral(
          ast_expr, {types::BigNumericType(), /*annotation_map=*/nullptr},
          Value::BigNumeric(*value_or_status), /*has_explicit_type=*/true);
      return absl::OkStatus();
    }

    case AST_JSON_LITERAL: {
      const ASTJSONLiteral* literal = ast_expr->GetAsOrDie<ASTJSONLiteral>();
      if (!language().LanguageFeatureEnabled(FEATURE_JSON_TYPE)) {
        return MakeSqlErrorAt(literal) << "JSON literals are not supported";
      }
      GOOGLESQL_ASSIGN_OR_RETURN(*resolved_expr_out, ResolveJsonLiteral(literal));
      return absl::OkStatus();
    }

    case AST_RANGE_LITERAL: {
      const ASTRangeLiteral* literal = ast_expr->GetAsOrDie<ASTRangeLiteral>();
      if (!language().LanguageFeatureEnabled(FEATURE_RANGE_TYPE)) {
        return MakeSqlErrorAt(literal) << "RANGE literals are not supported";
      }
      GOOGLESQL_ASSIGN_OR_RETURN(*resolved_expr_out, ResolveRangeLiteral(literal));
      return absl::OkStatus();
    }

    case AST_NULL_LITERAL: {
      // NULL literals are always treated as int64.  Literal coercion rules
      // may make the NULL change type.
      *resolved_expr_out = MakeResolvedLiteral(
          ast_expr, Value::Null(type_factory_->get_int64()));
      return absl::OkStatus();
    }

    case AST_DATE_OR_TIME_LITERAL: {
      const ASTDateOrTimeLiteral* literal =
          ast_expr->GetAsOrDie<ASTDateOrTimeLiteral>();
      return MakeResolvedDateOrTimeLiteral(
          ast_expr, literal->type_kind(),
          literal->string_literal()->string_value(), resolved_expr_out);
    }
    default:
      return MakeSqlErrorAt(ast_expr)
             << "Unhandled select-list literal expression for node kind "
             << ast_expr->GetNodeKindString() << ":\n"
             << ast_expr->DebugString();
  }
}

// Used to map a ResolvedColumnRef to a version of it that is
// available after GROUP BY.  Updates the ResolvedColumnRef with an
// available version if necessary, and returns an error if the input
// column is not available after GROUP BY.
//
// This is currently only used for resolving SELECT STAR columns
// after GROUP BY, but could potentially be used for other contexts.
//
// Arguments:
//   path_expr - input, only used for error messaging
//   clause_name - input, the clause that references the ResolvedColumn
//   expr_resolution_info - input, contains map from pre-grouping columns
//       to post-grouping columns
//   resolved_column_ref_expr - input/output
//
// On input, <resolved_column_ref_expr> indicates a ResolvedColumnRef that
// we want to resolve to a version of the column that is available after
// GROUP BY.
//
// On output, <resolved_column_ref_expr> indicates a ResolvedColumnRef that
// is available after GROUP BY.
//
// The function logic is as follows:
//
// 1) If the input ResolvedColumn is correlated then it is visible, so
//    <resolved_column_ref_expr> is left unchanged and returns OK.
// 2) Otherwise, we look through the QueryResolutionInfo's GroupByExprInfo
//    vector to see if we map this ResolvedColumn to another ResolvedColumn,
//    with no additional name path.
// 3) If we found a matching GroupByExprInfo, then <resolved_column_ref_expr>
//    is updated to a ResolvedColumnRef for the post-grouping version of the
//    column and returns OK.
// 4) Otherwise an error is returned.
absl::Status Resolver::ResolveColumnRefExprToPostGroupingColumn(
    const ASTExpression* path_expr, absl::string_view clause_name,
    QueryResolutionInfo* query_resolution_info,
    std::unique_ptr<const ResolvedExpr>* resolved_column_ref_expr) {
  GOOGLESQL_RET_CHECK_EQ(RESOLVED_COLUMN_REF, (*resolved_column_ref_expr)->node_kind());
  ABSL_DCHECK(query_resolution_info != nullptr);
  ABSL_DCHECK(query_resolution_info->HasGroupByOrAggregation());
  // This function only runs for SELECT *.
  // For AGGREGATE x.*, an error is raised in ResolveSelectDotStar.
  // For AGGREGATE ANY_VALUE(x).*, which is allowed, this method isn't called.
  GOOGLESQL_RET_CHECK(!query_resolution_info->IsPipeAggregate());

  const ResolvedColumnRef* resolved_column_ref =
      (*resolved_column_ref_expr)->GetAs<ResolvedColumnRef>();

  if (resolved_column_ref->is_correlated()) {
    return absl::OkStatus();
  }

  ResolvedColumn resolved_column = resolved_column_ref->column();
  const ValidNamePathList* name_path_list;
  if (query_resolution_info->group_by_valid_field_info_map().LookupNamePathList(
          resolved_column, &name_path_list)) {
    for (const ValidNamePath& valid_name_path : *name_path_list) {
      if (valid_name_path.name_path().empty()) {
        *resolved_column_ref_expr =
            MakeColumnRef(valid_name_path.target_column());
        return absl::OkStatus();
      }
    }
  }
  return MakeSqlErrorAt(path_expr)
         << clause_name << " expression references column "
         << resolved_column.name()
         << " which is neither grouped nor aggregated";
}

namespace {
std::string GetUnrecognizedNameErrorWithCatalogSuggestion(
    absl::Span<const std::string> name_parts, Catalog* catalog,
    bool name_is_system_variable, bool suggesting_system_variable) {
  std::string name_suggestion = catalog->SuggestConstant(name_parts);
  if (!name_suggestion.empty()) {
    std::string path_prefix = name_is_system_variable ? "@@" : "";
    std::string suggestion_path_prefix = suggesting_system_variable ? "@@" : "";

    std::string error_message;
    absl::StrAppend(
        &error_message, "Unrecognized name: ", path_prefix,
        absl::StrJoin(name_parts, ".",
                      [](std::string* out, absl::string_view part) {
                        absl::StrAppend(out, ToIdentifierLiteral(part));
                      }),
        "; Did you mean ", suggestion_path_prefix, name_suggestion, "?");
    return error_message;
  }
  return "";
}
}  // namespace

absl::Status Resolver::GetUnrecognizedNameError(
    const ParseLocationPoint& path_location_point,
    absl::Span<const std::string> identifiers, const NameScope* name_scope,
    bool is_system_variable) {
  IdStringPool id_string_pool;
  const IdString first_name = id_string_pool.Make(identifiers[0]);
  std::string error_message;

  // If the expression is system variable, use the position of the '@@' to
  // construct the error message, rather than that of the name following it.
  absl::StrAppend(&error_message,
                  "Unrecognized name: ", is_system_variable ? "@@" : "",
                  ToIdentifierLiteral(first_name));

  // See if we can come up with a name suggestion.  First, look for aliases in
  // the current scope.
  std::string name_suggestion;
  if (name_scope != nullptr) {
    name_suggestion = name_scope->SuggestName(first_name);
    if (!name_suggestion.empty()) {
      absl::StrAppend(&error_message, "; Did you mean ", name_suggestion, "?");
    }
  }

  // Check the catalog for either a named constant or a system variable.  We'll
  // check for both in all cases, but give priority to system variables if the
  // original expression was a system variable, or a named constant, otherwise.
  if (name_suggestion.empty()) {
    std::string suggested_error_message =
        GetUnrecognizedNameErrorWithCatalogSuggestion(
            absl::MakeConstSpan(identifiers),
            is_system_variable ? GetSystemVariablesCatalog() : catalog_,
            /*name_is_system_variable=*/is_system_variable,
            /*suggesting_system_variable=*/is_system_variable);
    if (suggested_error_message.empty()) {
      suggested_error_message = GetUnrecognizedNameErrorWithCatalogSuggestion(
          absl::MakeConstSpan(identifiers),
          is_system_variable ? catalog_ : GetSystemVariablesCatalog(),
          /*name_is_system_variable=*/is_system_variable,
          /*suggesting_system_variable=*/!is_system_variable);
    }
    if (!suggested_error_message.empty()) {
      error_message = suggested_error_message;
    }
  }
  return MakeSqlErrorAtPoint(path_location_point) << error_message;
}

absl::Status Resolver::ValidateColumnForAggregateOrAnalyticSupport(
    const ResolvedColumn& resolved_column, IdString first_name,
    const ASTIdentifier* first_identifier,
    ExprResolutionInfo* expr_resolution_info) const {
  SelectColumnStateList* select_column_state_list =
      expr_resolution_info->query_resolution_info->select_column_state_list();
  // If this ResolvedColumn is a SELECT list column, then validate
  // that any included aggregate or analytic function is allowed in
  // this expression.
  if (select_column_state_list != nullptr) {
    for (const std::unique_ptr<SelectColumnState>& select_column_state :
         select_column_state_list->select_column_state_list()) {
      if (resolved_column == select_column_state->resolved_select_column) {
        if (select_column_state->expr_findings.has_aggregation) {
          expr_resolution_info->findings.has_aggregation = true;
        }
        if (select_column_state->expr_findings.has_analytic) {
          expr_resolution_info->findings.has_analytic = true;
        }
        GOOGLESQL_RETURN_IF_ERROR(expr_resolution_info->query_resolution_info
                            ->select_column_state_list()
                            ->ValidateAggregateAndAnalyticSupport(
                                first_name.ToStringView(), first_identifier,
                                select_column_state.get(),
                                expr_resolution_info));
        break;
      }
    }
  }
  return absl::OkStatus();
}

absl::Status Resolver::MaybeResolvePathExpressionAsFunctionArgumentRef(
    IdString first_name, const ParseLocationRange& parse_location,
    std::unique_ptr<const ResolvedExpr>* resolved_expr_out,
    int* num_parts_consumed) {
  if (function_argument_info_ == nullptr) {
    return absl::OkStatus();
  }
  const FunctionArgumentInfo::ArgumentDetails* arg_details =
      function_argument_info_->FindScalarArg(first_name);
  if (arg_details == nullptr) {
    return absl::OkStatus();
  }
  // We do not expect to analyze a template function body until the function is
  // invoked and we have concrete argument types. If a template type is found
  // that could mean engine code is using an API incorrectly.
  GOOGLESQL_RET_CHECK(!arg_details->arg_type.IsTemplated())
      << "Function bodies cannot be resolved with templated argument types";
  auto resolved_argument_ref = MakeResolvedArgumentRef(
      arg_details->arg_type.type(), arg_details->name.ToString(),
      arg_details->arg_kind.value());
  resolved_argument_ref->set_type_annotation_map(arg_details->annotation_map);
  MaybeRecordParseLocation(parse_location, resolved_argument_ref.get());
  if (arg_details->arg_kind.value() == ResolvedArgumentDef::AGGREGATE) {
    // Save the location for aggregate arguments because we generate
    // some errors referencing them in a post-pass after resolving the full
    // function body, when we no longer have the ASTNode.
    resolved_argument_ref->SetParseLocationRange(parse_location);
  }
  *resolved_expr_out = std::move(resolved_argument_ref);
  (*num_parts_consumed)++;
  return absl::OkStatus();
}

static std::string GetRangeNameForError(
    const std::optional<IdString>& range_name) {
  return range_name.has_value()
             ? absl::StrCat("pattern variable ",
                            ToIdentifierLiteral(range_name->ToStringView()))
             : "all input rows";
}

// TODO: The noinline attribute is to prevent the stack usage
// being added to its caller "Resolver::ResolveExpr" which is a recursive
// function. Now the attribute has to be added for all callees. Hopefully
// the fix allows replacing these with one attribute on ResolveExpr.
ABSL_ATTRIBUTE_NOINLINE
absl::Status Resolver::ResolvePathExpressionAsExpression(
    PathExpressionSpan path_expr, ExprResolutionInfo* expr_resolution_info,
    ResolvedStatement::ObjectAccess access_flags,
    std::unique_ptr<const ResolvedExpr>* resolved_expr_out) {
  RETURN_ERROR_IF_OUT_OF_STACK_SPACE();

  ParseLocationRange path_parse_location = path_expr.GetParseLocationRange();
  IdString first_name = path_expr.GetFirstIdString();
  const std::string lowercase_name =
      absl::AsciiStrToLower(first_name.ToStringView());
  // The length of the longest prefix of <path_expr> that has been resolved
  // successfully. Often 1 after successful resolution and 0 if resolution
  // fails, but can be longer for name paths and named constants. 0 while
  // <path_expr> has not be resolved yet.
  int num_names_consumed = 0;

  // The catalog object that <path_expr> resolves to, if any. Can be any of the
  // following, which will be tried in this order below:
  // (1) Name target;
  // (2) Expression column (for standalone expression evaluation only);
  // (3) Function argument
  // (4) Named constant.
  // (5) Graph property (for graph measure expression only);
  std::unique_ptr<const ResolvedExpr> resolved_expr;

  // (1) Try to find a name target that matches <path_expr>.
  CorrelatedColumnsSetList correlated_columns_sets;
  const std::vector<ExprResolutionInfo*>* lateral_column_reference_observers;

  NameTarget target;
  bool resolved_to_target = false;
  if (num_names_consumed == 0 && expr_resolution_info->name_scope != nullptr) {
    const char* problem_string = NameScope::kDefaultProblemString;
    if (expr_resolution_info->is_post_distinct()) {
      problem_string = "not visible after SELECT DISTINCT";
    }
    if (expr_resolution_info->query_resolution_info != nullptr &&
        expr_resolution_info->query_resolution_info->IsPipeAggregate()) {
      problem_string = "not aggregated";
    }

    std::optional<IdString> referenced_pattern_variable;
    GOOGLESQL_RETURN_IF_ERROR(expr_resolution_info->name_scope->LookupNamePath(
        path_expr, expr_resolution_info->clause_name, problem_string,
        in_strict_mode(), correlated_columns_sets, &num_names_consumed,
        referenced_pattern_variable, lateral_column_reference_observers,
        &target));
    resolved_to_target = num_names_consumed > 0;

    const bool is_correlated = !correlated_columns_sets.empty();
    if (resolved_to_target && !is_correlated &&
        expr_resolution_info->query_resolution_info != nullptr) {
      MatchRecognizeAggregationState* scoped_agg_state =
          expr_resolution_info->query_resolution_info
              ->scoped_aggregation_state();
      if (scoped_agg_state->row_range_determined) {
        // The range has already been pinned. Make sure we're not referring to
        // a conflicting range.
        if (scoped_agg_state->target_pattern_variable_ref !=
            referenced_pattern_variable) {
          return MakeSqlErrorAtPoint(path_parse_location.start())
                 << "Column access ranges over "
                 << GetRangeNameForError(referenced_pattern_variable)
                 << " in an expression that already ranges over a "
                 << GetRangeNameForError(
                        scoped_agg_state->target_pattern_variable_ref);
        }
      } else {
        // Mark the range as pinned.
        // If any computed columns (e.g. intermediate aggregations in a
        // multi-level aggregate function) are registered, move them to the
        // appropriate list. We couldn't determine their scope before because
        // they are constant, e.g. Agg(....GROUP BY @p ORDER BY min('a')), where
        // the intermediate aggregation min('a') is constant and that's why the
        // range is still not determined.
        GOOGLESQL_RETURN_IF_ERROR(
            expr_resolution_info->query_resolution_info->PinToRowRange(
                referenced_pattern_variable));
      }
    }
  }

  if (resolved_to_target) {
    // We resolved (at least part of) the prefix path to a NameTarget.  Create
    // a ResolvedExpr for the resolved part of the name path.  We will
    // resolve any remaining names as field accesses just before returning.
    switch (target.kind()) {
      case NameTarget::RANGE_VARIABLE:
        if (target.is_pattern_variable()) {
          return MakeSqlErrorAtPoint(path_parse_location.start())
                 << "Pattern variable " << path_expr.ToIdentifierPathString()
                 << " cannot be used as a value table";
        }
        if (target.scan_columns()->is_value_table()) {
          GOOGLESQL_RET_CHECK_EQ(target.scan_columns()->num_columns(), 1);
          ResolvedColumn resolved_column =
              target.scan_columns()->column(0).column();
          resolved_expr = MakeColumnRefWithCorrelation(
              resolved_column, correlated_columns_sets, access_flags);
        } else {
          // Creating a STRUCT is relatively expensive, so we want to
          // avoid it whenever possible.  For example, if 'path_expr'
          // references a field of a struct then we can avoid creating the
          // STRUCT.  The current implementation only creates a STRUCT
          // here if there is exactly one name in the path (in which
          // case there will be an error later if there is a second component,
          // because it would have to be a generalized field access, and those
          // only apply to PROTO fields).  It is the responsibility of
          // LookupNamePath() to facilitate this optimization by only choosing
          // 'target' to be a STRUCT RANGE_VARIABLE if the prefix path has no
          // subsequent identifiers to resolve.  We GOOGLESQL_RET_CHECK that condition
          // here to avoid unintentional performance regression in the future.
          GOOGLESQL_RET_CHECK_EQ(1, path_expr.num_names());
          std::unique_ptr<ResolvedComputedColumn> make_struct_computed_column;
          GOOGLESQL_RETURN_IF_ERROR(CreateStructFromNameList(
              target.scan_columns().get(), correlated_columns_sets,
              &make_struct_computed_column));
          resolved_expr = make_struct_computed_column->release_expr();
        }
        break;
      case NameTarget::EXPLICIT_COLUMN:
      case NameTarget::IMPLICIT_COLUMN: {
        const bool is_correlated = !correlated_columns_sets.empty();
        // If this is not a delayed column (which is currently used only for
        // lateral column references), there may not be an ExprResolutionInfo
        // to observe, e.g. if the column is coming directly from the FROM
        // clause. In this case, SetupPrecomputedColumnIfNeeded() will be a NOOP
        // so it doesn't matter which ExprResolutionInfo we pass. However, we
        // set it up if available (always the case for lateral column
        // references) so that the pre- and post-grouping context tracking in
        // the outer query remains accurate.
        ExprResolutionInfo* uncorrelated_expr_info = expr_resolution_info;
        if (lateral_column_reference_observers != nullptr &&
            !lateral_column_reference_observers->empty()) {
          uncorrelated_expr_info = lateral_column_reference_observers->back();
          GOOGLESQL_RET_CHECK(is_correlated ||
                    uncorrelated_expr_info == expr_resolution_info)
              << "Is correlated? " << is_correlated
              << ", expr_resolution_info is observing? "
              << (uncorrelated_expr_info == expr_resolution_info)
              << " for target: " << target.DebugString();

          // The first name is a column, so return the target.
          // find the context from the appropriate ExprResolutionInfo, in case
          // this is a correlated reference to a lateral column.
          GOOGLESQL_RETURN_IF_ERROR(
              target.SetupPrecomputedColumnIfNeeded(ColumnReferenceContext{
                  .location = path_parse_location.start(),
                  .grouping_context =
                      uncorrelated_expr_info->grouping_context}));
        } else {
          GOOGLESQL_RET_CHECK(!target.delayed_column_info().has_value());
        }

        ResolvedColumn resolved_column = target.column();
        auto resolved_column_ref = MakeColumnRefWithCorrelation(
            resolved_column, correlated_columns_sets, access_flags);
        MaybeRecordParseLocation(path_expr.GetParseLocationRange(),
                                 resolved_column_ref.get());

        if (target.delayed_column_info().has_value()) {
          GOOGLESQL_RET_CHECK(target.delayed_column_info()->precompute_callback ==
                    nullptr);

          if (uncorrelated_expr_info->query_resolution_info != nullptr &&
              uncorrelated_expr_info->query_resolution_info
                  ->HasGroupByOrAggregation() &&
              uncorrelated_expr_info->grouping_context ==
                  ExprGroupingContext::kPreGrouping) {
            return MakeSqlErrorAtPoint(path_parse_location.start())
                   << "Column " << path_expr.ToIdentifierPathString()
                   << " is referenced laterally from WHERE or argument to an "
                      "aggregation when there's grouping or aggregation in the "
                      "query";
          }

          if (target.delayed_column_info()
                  ->select_column_state->expr_findings.has_aggregation) {
            if (!expr_resolution_info->allows_aggregation) {
              return MakeSqlErrorAtPoint(path_parse_location.start())
                     << "Column " << path_expr.ToIdentifierPathString()
                     << " contains an aggregate function, which is not "
                        "allowed in "
                     << expr_resolution_info->clause_name;
            }
            uncorrelated_expr_info->findings.has_aggregation = true;
          }
          if (target.delayed_column_info()
                  ->select_column_state->expr_findings.has_analytic) {
            if (!expr_resolution_info->allows_analytic) {
              return MakeSqlErrorAtPoint(path_parse_location.start())
                     << "Column " << path_expr.ToIdentifierPathString()
                     << " contains an analytic function, which is not "
                        "allowed in "
                     << expr_resolution_info->clause_name;
            }
            uncorrelated_expr_info->findings.has_analytic = true;
          }

          if (target.delayed_column_info()
                  ->select_column_state->expr_findings.has_volatile) {
            uncorrelated_expr_info->findings.has_volatile = true;
          }
          uncorrelated_expr_info->columns_referenced_laterally.push_back(
              target.delayed_column_info()->select_column_state);
        }

        if (expr_resolution_info->in_horizontal_aggregation &&
            resolved_column_ref->type()->IsArray()) {
          GOOGLESQL_RET_CHECK(!target.delayed_column_info().has_value());
          GOOGLESQL_RET_CHECK(expr_resolution_info->allows_horizontal_aggregation);
          auto& info = expr_resolution_info->horizontal_aggregation_info;
          ResolvedColumn output_element_column;
          if (info.has_value()) {
            const auto& [array, array_is_correlated, element] = *info;
            if (array != resolved_column_ref->column()) {
              return MakeSqlErrorAtPoint(path_parse_location.start())
                     << "Horizontal aggregation on more than one array-typed "
                        "variable is not allowed. First variable: "
                     << array.name();
            }
            output_element_column = element;
          } else {
            output_element_column =
                ResolvedColumn(AllocateColumnId(), kHorizontalAggregateId,
                               resolved_column_ref->column().name_id(),
                               resolved_column_ref->column()
                                   .type()
                                   ->AsArray()
                                   ->element_type());
            info = {resolved_column_ref->column(),
                    resolved_column_ref->is_correlated(),
                    output_element_column};
          }
          // This variable's scope is just the array aggregation so it is never
          // correlated.
          resolved_column_ref = MakeColumnRef(
              output_element_column, /*is_correlated=*/false, access_flags);
        }

        if (expr_resolution_info->is_graph_measure_expression) {
          // For graph measure property expressions, columns are resolved to
          // ResolvedCatalogColumnRef.
          std::unique_ptr<ResolvedCatalogColumnRef>
              resolved_catalog_column_ref =
                  MakeResolvedCatalogColumnRef(resolved_column_ref->type(),
                                               /*column=*/nullptr);
          resolved_catalog_column_ref->set_name(
              resolved_column_ref->column().name());
          resolved_expr = std::move(resolved_catalog_column_ref);
        } else {
          resolved_expr = std::move(resolved_column_ref);
        }
        if (expr_resolution_info->query_resolution_info != nullptr &&
            !expr_resolution_info->is_post_distinct()) {
          // We resolved this to a column, which might be a SELECT list
          // column.  If so, we need to validate that any aggregate or
          // analytic functions are valid in this context.  For instance,
          // consider:
          //
          //   SELECT sum(a) as b FROM table HAVING sum(b) > 5;
          //
          // When resolving the HAVING clause expression 'sum(b) > 5', as
          // we resolve the subexpression 'sum(b)' the arguments to it must
          // not contain aggregation or analytic functions.  Since 'b' is a
          // SELECT list alias and resolved to a SELECT list ResolvedColumn,
          // we must check it for the presence of aggregate or analytic
          // functions there.
          //
          // We don't do this for post-DISTINCT column references, since SELECT
          // list references are always ok (neither aggregate nor analytic
          // functions are currently allowed in ORDER BY if DISTINCT is
          // present).  TODO: We have an open item for allowing
          // analytic functions in ORDER BY after DISTINCT.  If/when we
          // add support for that, we will need to re-enable this validation
          // when is_post_distinct() is true.
          GOOGLESQL_RETURN_IF_ERROR(ValidateColumnForAggregateOrAnalyticSupport(
              resolved_column, first_name, path_expr.first_name(),
              expr_resolution_info));
        }
        break;
      }
      case NameTarget::FIELD_OF:
        GOOGLESQL_RETURN_IF_ERROR(ResolveFieldAccess(
            MakeColumnRefWithCorrelation(target.column_containing_field(),
                                         correlated_columns_sets, access_flags),
            path_parse_location, path_expr.first_name(),
            &expr_resolution_info->flatten_state, &resolved_expr));
        break;
      case NameTarget::ACCESS_ERROR:
      case NameTarget::AMBIGUOUS:
        // As per the LookupNamePath() contract, if it returns OK and at
        // least one name was consumed then the returned target must
        // be a valid target (range variable, column, or field reference).
        GOOGLESQL_RET_CHECK_FAIL()
            << "LookupNamePath returned OK with unexpected NameTarget: "
            << target.DebugString();
    }
  }

  if (num_names_consumed == 0 && analyzing_expression_) {
    // (2) We still haven't found a matching name. See if we can find it in an
    // expression column (for standalone expression evaluation only).
    // TODO: Move this to a separate function to handle this
    // case out of line.
    const Type* column_type = googlesql_base::FindPtrOrNull(
        analyzer_options_.expression_columns(), lowercase_name);

    if (column_type != nullptr) {
      std::unique_ptr<ResolvedExpressionColumn> resolved_expr_column =
          MakeResolvedExpressionColumn(column_type, lowercase_name);
      MaybeRecordParseLocation(path_expr.GetParseLocationRange(),
                               resolved_expr_column.get());
      resolved_expr = std::move(resolved_expr_column);
      num_names_consumed = 1;
    } else if (analyzer_options_.has_in_scope_expression_column()) {
      // We have an automatically in-scope expression column.
      // See if we can resolve this name as a field on it.
      column_type = analyzer_options_.in_scope_expression_column_type();

      // Make a ResolvedExpr for the in-scope expression column.
      // We'll only use this if we are successful resolving a field access.
      std::unique_ptr<const ResolvedExpr> value_column =
          MakeResolvedExpressionColumn(
              column_type, analyzer_options_.in_scope_expression_column_name());

      // These MaybeResolve methods with <error_if_not_found> false will
      // return OK with a NULL <*resolved_expr_out> if the field isn't found.
      if (column_type->IsProto()) {
        MaybeResolveProtoFieldOptions options;
        options.error_if_not_found = false;
        GOOGLESQL_RETURN_IF_ERROR(MaybeResolveProtoFieldAccess(
            path_parse_location, path_expr.first_name(), options,
            std::move(value_column), &resolved_expr));
      } else if (column_type->IsStruct()) {
        GOOGLESQL_RETURN_IF_ERROR(MaybeResolveStructFieldAccess(
            path_parse_location, path_expr.first_name(),
            /*error_if_not_found=*/false, std::move(value_column),
            &resolved_expr));
      }

      if (resolved_expr != nullptr) {
        num_names_consumed = 1;
      }
    }
  }

  if (num_names_consumed == 0 &&
      InternalAnalyzerOptions::GetLookupExpressionCallback(analyzer_options_) !=
          nullptr) {
    // Lookup the column using the callback function if set.
    std::unique_ptr<const ResolvedExpr> expr;
    GOOGLESQL_RETURN_IF_ERROR(InternalAnalyzerOptions::GetLookupExpressionCallback(
        analyzer_options_)(lowercase_name, expr));
    if (expr != nullptr) {
      MaybeRecordParseLocation(path_expr.GetParseLocationRange(),
                               const_cast<ResolvedExpr*>(expr.get()));
      resolved_expr = std::move(expr);
      num_names_consumed = 1;
    }
  }

  if (num_names_consumed == 0) {
    // (3) We still haven't found a matching name. See if we can find it in
    // function arguments (for CREATE FUNCTION statements only).
    GOOGLESQL_RETURN_IF_ERROR(MaybeResolvePathExpressionAsFunctionArgumentRef(
        first_name, path_expr.first_name()->location(), &resolved_expr,
        &num_names_consumed));
  }

  if (num_names_consumed == 0) {
    // (4) We still haven't found a matching name. Try to resolve the longest
    // possible prefix of <path_expr> to a named constant.
    const Constant* constant = nullptr;
    absl::Status find_constant_with_path_prefix_status =
        catalog_->FindConstantWithPathPrefix(path_expr.ToIdentifierVector(),
                                             &num_names_consumed, &constant,
                                             analyzer_options_.find_options());

    // Handle the case where a constant was found or some internal error
    // occurred. If no constant was found, <num_names_consumed> is set to 0.
    if (find_constant_with_path_prefix_status.code() !=
        absl::StatusCode::kNotFound) {
      // If an error occurred, return immediately.
      GOOGLESQL_RETURN_IF_ERROR(find_constant_with_path_prefix_status);

      // A constant was found. Wrap it in an AST node and skip ahead to
      // resolving any field access in a path suffix if present.
      GOOGLESQL_RET_CHECK(constant != nullptr);
      GOOGLESQL_RET_CHECK_GT(num_names_consumed, 0);
      auto resolved_constant = MakeResolvedConstant(constant->type(), constant);
      MaybeRecordParseLocation(path_expr.GetParseLocationRange(),
                               resolved_constant.get());
      resolved_expr = std::move(resolved_constant);
    }
  }

  if (num_names_consumed == 0 &&
      expr_resolution_info->is_graph_measure_expression) {
    // (5) We still haven't found a matching name. See if we can find it in an
    // graph property (for graph measure expression only).
    const Type* property_type = googlesql_base::FindPtrOrNull(
        analyzer_options_.graph_properties(), lowercase_name);
    if (property_type != nullptr) {
      std::unique_ptr<ResolvedCatalogColumnRef> resolved_catalog_column_ref =
          MakeResolvedCatalogColumnRef(property_type,
                                       /*column=*/nullptr);
      resolved_catalog_column_ref->set_name(lowercase_name);
      resolved_expr = std::move(resolved_catalog_column_ref);
      num_names_consumed = 1;
    }
  }

  if (generated_column_cycle_detector_ != nullptr) {
    // If we are analyzing generated columns, we need to record the dependency
    // here so that we are not introducing cycles.
    // E.g. in
    // CREATE TABLE T (
    //  a as b,
    //  b INT64
    // );
    // If we are processing 'a', 'first_name' will be b in this example.
    // In this case this dependency does not introduce a cycle so it's a valid
    // reference.
    unresolved_column_name_in_generated_column_.clear();
    GOOGLESQL_RETURN_IF_ERROR(
        generated_column_cycle_detector_->AddDependencyOn(first_name));
  }
  if (num_names_consumed == 0) {
    // No matching name could be found, so throw an error.
    if (generated_column_cycle_detector_ != nullptr) {
      // When we are analyzing generated columns and there was an error, we
      // record the name of the column here so that the caller can try resolving
      // the column that was not found. E.g. in
      // CREATE TABLE T (
      //  a as b,
      //  b as c,
      //  c INT64
      // );
      // 'b' won't be found the first time 'a' is being resolved, so
      // 'generated_column_cycle_detector_' will be set to 'b' in this case so
      // that the resolver tries to resolve 'b' next.
      unresolved_column_name_in_generated_column_ = first_name;
    }
    return GetUnrecognizedNameError(path_expr.GetParseLocationRange().start(),
                                    path_expr.ToIdentifierVector(),
                                    expr_resolution_info != nullptr
                                        ? expr_resolution_info->name_scope
                                        : nullptr,
                                    /*is_system_variable=*/false);
  }
  GOOGLESQL_RET_CHECK(resolved_expr != nullptr);

  // Resolve any further identifiers in <path_expr> as field accesses.
  for (; num_names_consumed < path_expr.num_names(); ++num_names_consumed) {
    GOOGLESQL_RETURN_IF_ERROR(ResolveFieldAccess(
        std::move(resolved_expr), path_parse_location,
        path_expr.name(num_names_consumed),
        &expr_resolution_info->flatten_state, &resolved_expr));
  }
  *resolved_expr_out = std::move(resolved_expr);
  return absl::OkStatus();
}

// TODO: The noinline attribute is to prevent the stack usage
// being added to its caller "Resolver::ResolveExpr" which is a recursive
// function. Now the attribute has to be added for all callees. Hopefully
// the fix allows replacing these with one attribute on ResolveExpr.
ABSL_ATTRIBUTE_NOINLINE
absl::Status Resolver::ResolveParameterExpr(
    const ASTParameterExpr* param_expr,
    std::unique_ptr<const ResolvedExpr>* resolved_expr_out) {
  if (analyzer_options().statement_context() == CONTEXT_MODULE) {
    return MakeSqlErrorAt(param_expr)
           << "Query parameters cannot be used inside modules";
  }
  if (!disallowing_query_parameters_with_error_.empty()) {
    return MakeSqlErrorAt(param_expr)
           << disallowing_query_parameters_with_error_;
  }
  if (generated_column_cycle_detector_ != nullptr) {
    return MakeSqlErrorAt(param_expr)
           << "Query parameters cannot be used inside generated columns";
  }
  if (analyzing_check_constraint_expression_) {
    return MakeSqlErrorAt(param_expr)
           << "Query parameters cannot be used inside "
           << "CHECK constraint "
           << "expression";
  }
  if (default_expr_access_error_name_scope_.has_value()) {
    return MakeSqlErrorAt(param_expr) << "Query parameters cannot be used "
                                         "inside a column default expression";
  }

  const Type* param_type = nullptr;
  std::string lowercase_param_name;
  int position = 0;

  switch (analyzer_options_.parameter_mode()) {
    case PARAMETER_NAMED: {
      if (param_expr->name() == nullptr) {
        return MakeSqlErrorAt(param_expr)
               << "Positional parameters are not supported";
      }
      const std::string& param_name = param_expr->name()->GetAsString();
      GOOGLESQL_RET_CHECK(!param_name.empty());
      lowercase_param_name = absl::AsciiStrToLower(param_name);
      param_type = googlesql_base::FindPtrOrNull(analyzer_options_.query_parameters(),
                                      lowercase_param_name);

      if (param_type == nullptr) {
        if (analyzer_options_.allow_undeclared_parameters()) {
          auto param =
              MakeResolvedParameter(types::Int64Type(), lowercase_param_name,
                                    /*position=*/0, /*is_untyped=*/true);
          // Note: We always attach a parse location for a parameter
          // (regardless of whether or not the AnalyzerOptions is set to
          // record_parse_locations), because we use that parse location as
          // a key to look up the parameter when doing Type analysis for
          // untyped parameters.  See MaybeAssignTypeToUndeclaredParameter()
          // for details.
          param->SetParseLocationRange(param_expr->location());
          *resolved_expr_out = std::move(param);
          // Record the new location, no collisions are possible.
          untyped_undeclared_parameters_.emplace(param_expr->location().start(),
                                                 lowercase_param_name);
          return absl::OkStatus();
        }
        return MakeSqlErrorAt(param_expr)
               << "Query parameter '" << param_name << "' not found";
      }
    } break;
    case PARAMETER_POSITIONAL: {
      if (param_expr->name() != nullptr) {
        return MakeSqlErrorAt(param_expr)
               << "Named parameters are not supported";
      }

      position = param_expr->position();
      if (position - 1 >=
          analyzer_options_.positional_query_parameters().size()) {
        if (analyzer_options_.allow_undeclared_parameters()) {
          auto param =
              MakeResolvedParameter(types::Int64Type(), lowercase_param_name,
                                    position, /*is_untyped=*/true);
          param->SetParseLocationRange(param_expr->location());
          *resolved_expr_out = std::move(param);
          // Record the new location, no collisions are possible.
          untyped_undeclared_parameters_.emplace(param_expr->location().start(),
                                                 position);
          return absl::OkStatus();
        }
        return MakeSqlErrorAt(param_expr)
               << "Query parameter number " << position << " is not defined ("
               << analyzer_options_.positional_query_parameters().size()
               << " provided)";
      }

      param_type =
          analyzer_options_.positional_query_parameters()[position - 1];
    } break;
    case PARAMETER_NONE:
      return MakeSqlErrorAt(param_expr) << "Parameters are not supported";
  }

  auto param = MakeResolvedParameter(param_type, lowercase_param_name, position,
                                     /*is_untyped=*/false);
  MaybeRecordParseLocation(param_expr, param.get());
  *resolved_expr_out = std::move(param);
  return absl::OkStatus();
}

// TODO: The noinline attribute is to prevent the stack usage
// being added to its caller "Resolver::ResolveExpr" which is a recursive
// function. Now the attribute has to be added for all callees. Hopefully
// the fix allows replacing these with one attribute on ResolveExpr.
ABSL_ATTRIBUTE_NOINLINE
absl::Status Resolver::ResolveDotIdentifier(
    const ASTDotIdentifier* dot_identifier,
    ExprResolutionInfo* expr_resolution_info,
    std::unique_ptr<const ResolvedExpr>* resolved_expr_out) {
  std::unique_ptr<const ResolvedExpr> resolved_expr;
  GOOGLESQL_RETURN_IF_ERROR(ResolveExpr(dot_identifier->expr(), expr_resolution_info,
                              &resolved_expr));

  return ResolveFieldAccess(std::move(resolved_expr),
                            dot_identifier->location(), dot_identifier->name(),
                            &expr_resolution_info->flatten_state,
                            resolved_expr_out);
}

absl::Status Resolver::MaybeResolveProtoFieldAccess(
    const ParseLocationRange& parse_location, const ASTIdentifier* identifier,
    const MaybeResolveProtoFieldOptions& options,
    std::unique_ptr<const ResolvedExpr> resolved_lhs,
    std::unique_ptr<const ResolvedExpr>* resolved_expr_out) {
  resolved_expr_out->reset();

  const std::string dot_name = identifier->GetAsString();

  GOOGLESQL_RET_CHECK(resolved_lhs->type()->IsProto());
  const ProtoType* lhs_proto_type = resolved_lhs->type()->AsProto();
  const google::protobuf::Descriptor* lhs_proto = lhs_proto_type->descriptor();
  GOOGLESQL_ASSIGN_OR_RETURN(const google::protobuf::FieldDescriptor* field,
                   FindFieldDescriptor(identifier, lhs_proto, dot_name));
  bool get_has_bit = false;
  if (options.get_has_bit_override) {
    get_has_bit = *options.get_has_bit_override;
  } else if (absl::StartsWithIgnoreCase(dot_name, "has_")) {
    if (lhs_proto->options().map_entry()) {
      return MakeSqlErrorAt(identifier)
             << lhs_proto->full_name() << " is a synthetic map entry proto. "
             << dot_name
             << " is not allowed for map entries because all map entry fields "
                "are considered to be present by definition";
    }

    const std::string dot_name_without_has = dot_name.substr(4);
    GOOGLESQL_ASSIGN_OR_RETURN(
        const google::protobuf::FieldDescriptor* has_field,
        FindFieldDescriptor(identifier, lhs_proto, dot_name_without_has));
    if (has_field != nullptr) {
      if (field != nullptr) {
        // Give an error if we asked for has_X and the proto has fields
        // called both X and has_X.  Such protos won't actually make it
        // through the c++ proto compiler, but might be hackable other ways.
        return MakeSqlErrorAt(identifier)
               << "Protocol buffer " << lhs_proto->full_name()
               << " has fields called both " << dot_name << " and "
               << dot_name_without_has << ", so " << dot_name
               << " is ambiguous";
      }

      // Get the has bit rather than the field value.
      field = has_field;
      get_has_bit = true;
    }
  }

  if (field == nullptr) {
    if (options.error_if_not_found) {
      std::string error_message;
        if (false) {
      } else {
        absl::StrAppend(&error_message, "Protocol buffer ",
                        lhs_proto->full_name(),
                        " does not have a field called ", dot_name);
        if (lhs_proto->field_count() > 0) {
          std::vector<std::string> possible_names;
          possible_names.reserve(lhs_proto->field_count());
          for (int i = 0; i < lhs_proto->field_count(); ++i) {
            possible_names.emplace_back(lhs_proto->field(i)->name());
          }
          const std::string name_suggestion =
              ClosestName(dot_name, possible_names);
          if (!name_suggestion.empty()) {
            absl::StrAppend(&error_message, "; Did you mean ", name_suggestion,
                            "?");
            absl::Status status = MakeSqlErrorAt(identifier) << error_message;
            return AddFixSuggestionToStatus(
                status, absl::StrFormat("Replace with `%s`", name_suggestion),
                identifier->start_location(), identifier->end_location(),
                name_suggestion);
          }
        }
      }
      return MakeSqlErrorAt(identifier) << error_message;
    } else {
      // We return success with a NULL resolved_expr_out.
      GOOGLESQL_RET_CHECK(*resolved_expr_out == nullptr);
      return absl::OkStatus();
    }
  }

  const Type* field_type;
  Value default_value;
  if (get_has_bit) {
    if (field->is_repeated()) {
      return MakeSqlErrorAt(identifier)
             << "Protocol buffer " << lhs_proto->full_name() << " field "
             << field->name() << " is repeated, so "
             << (options.get_has_bit_override
                     ? absl::StrCat("HAS(", field->name(), ")")
                     : dot_name)
             << " is not allowed";
    }

    // Note that proto3 does not allow TYPE_GROUP.
    if (!field->has_presence() &&
        language().LanguageFeatureEnabled(
            FEATURE_DEPRECATED_DISALLOW_PROTO3_HAS_SCALAR_FIELD)) {
      return MakeSqlErrorAt(identifier)
             << "Checking the presence of scalar field " << field->full_name()
             << " is not supported";
    }

    // When reading has_<field>, none of the modifiers are relevant because
    // we are checking for existence of the proto field, not checking if
    // we get a NULL after unwrapping.
    field_type = type_factory_->get_bool();
  } else {
    auto default_options =
        ProtoFieldDefaultOptions::FromFieldAndLanguage(field, language());
    if (options.ignore_format_annotations) {
      // ignore_format_annotations in the options implies a RAW() fetch.
      // RAW() fetches ignore any annotations on the fields and give you back
      // whatever you would have gotten without those annotations. This applies
      // to both type annotations and default annotations.
      default_options.ignore_format_annotations = true;
      default_options.ignore_use_default_annotations = true;
    }
    GOOGLESQL_RETURN_IF_ERROR(
        GetProtoFieldTypeAndDefault(default_options, field,
                                    lhs_proto_type->CatalogNamePath(),
                                    type_factory_, &field_type, &default_value))
        .With(LocationOverride(identifier));

    if (options.ignore_format_annotations && field_type->IsBytes()) {
      const Type* type_with_annotations;
      GOOGLESQL_RETURN_IF_ERROR(type_factory_->GetProtoFieldType(
          field, lhs_proto_type->CatalogNamePath(), &type_with_annotations));
      if (type_with_annotations->IsGeography()) {
        return MakeSqlErrorAt(identifier)
               << "RAW() extractions of Geography fields are unsupported";
      }
    }
  }

  // GoogleSQL supports has_X() for fields that have unsupported type
  // annotations, but accessing the field value would produce an error.
  if (!get_has_bit &&
      ((field_type->UsingFeatureV12CivilTimeType() &&
        !language().LanguageFeatureEnabled(FEATURE_CIVIL_TIME)) ||
       (field_type->IsNumericType() &&
        !language().LanguageFeatureEnabled(FEATURE_NUMERIC_TYPE)))) {
    return MakeSqlErrorAt(identifier)
           << "Field " << dot_name << " in protocol buffer "
           << lhs_proto->full_name() << " has unsupported type "
           << field_type->ShortTypeName(language().product_mode());
  }

  auto resolved_get_proto_field = MakeResolvedGetProtoField(
      field_type, std::move(resolved_lhs), field, default_value, get_has_bit,
      ((get_has_bit || options.ignore_format_annotations)
           ? FieldFormat::DEFAULT_FORMAT
           : ProtoType::GetFormatAnnotation(field)),
      /*return_default_value_when_unset=*/false);
  MaybeRecordFieldAccessParseLocation(parse_location, identifier,
                                      resolved_get_proto_field.get());
  *resolved_expr_out = std::move(resolved_get_proto_field);
  return absl::OkStatus();
}

absl::Status Resolver::MaybeResolveStructFieldAccess(
    const ParseLocationRange& parse_location, const ASTIdentifier* identifier,
    bool error_if_not_found, std::unique_ptr<const ResolvedExpr> resolved_lhs,
    std::unique_ptr<const ResolvedExpr>* resolved_expr_out) {
  resolved_expr_out->reset();

  const std::string dot_name = identifier->GetAsString();

  GOOGLESQL_RET_CHECK(resolved_lhs->type()->IsStruct());
  const StructType* struct_type = resolved_lhs->type()->AsStruct();
  bool is_ambiguous;
  int found_idx;
  const StructType::StructField* field =
      struct_type->FindField(dot_name, &is_ambiguous, &found_idx);
  if (is_ambiguous) {
    return MakeSqlErrorAt(identifier)
           << "Struct field name " << ToIdentifierLiteral(dot_name)
           << " is ambiguous";
  }
  if (field == nullptr) {
    if (error_if_not_found) {
      std::string error_message;
      absl::StrAppend(&error_message, "Field name ",
                      ToIdentifierLiteral(dot_name), " does not exist in ",
                      struct_type->ShortTypeName(product_mode()));

      std::vector<std::string> possible_names;
      for (const StructType::StructField& field : struct_type->fields()) {
        possible_names.push_back(field.name);
      }
      const std::string name_suggestion = ClosestName(dot_name, possible_names);
      if (!name_suggestion.empty()) {
        absl::StrAppend(&error_message, "; Did you mean ", name_suggestion,
                        "?");
      }
      return MakeSqlErrorAt(identifier) << error_message;
    } else {
      // We return success with a NULL resolved_expr_out.
      GOOGLESQL_RET_CHECK(*resolved_expr_out == nullptr);
      return absl::OkStatus();
    }
  }
  ABSL_DCHECK_EQ(field, &struct_type->field(found_idx));

  std::unique_ptr<ResolvedExpr> resolved_node = MakeResolvedGetStructField(
      field->type, std::move(resolved_lhs), found_idx);
  GOOGLESQL_RETURN_IF_ERROR(CheckAndPropagateAnnotations(/*error_node=*/nullptr,
                                               resolved_node.get()));
  MaybeRecordFieldAccessParseLocation(parse_location, identifier,
                                      resolved_node.get());
  *resolved_expr_out = std::move(resolved_node);
  return absl::OkStatus();
}

absl::Status Resolver::ResolveJsonFieldAccess(
    const ASTIdentifier* identifier,
    std::unique_ptr<const ResolvedExpr> resolved_lhs,
    std::unique_ptr<const ResolvedExpr>* resolved_expr_out) {
  *resolved_expr_out = nullptr;

  GOOGLESQL_RET_CHECK(resolved_lhs->type()->IsJson());

  *resolved_expr_out = MakeResolvedGetJsonField(
      types::JsonType(), std::move(resolved_lhs), identifier->GetAsString());
  return absl::OkStatus();
}

absl::Status Resolver::ResolveGetRowField(
    const ASTIdentifier* identifier, FlattenState* flatten_state,
    std::unique_ptr<const ResolvedExpr> resolved_lhs,
    std::unique_ptr<const ResolvedExpr>* resolved_expr_out) {
  *resolved_expr_out = nullptr;

  GOOGLESQL_RET_CHECK(resolved_lhs->type()->IsRow());
  const RowType* row_type = resolved_lhs->type()->AsRow();
  const Table* table = row_type->table();

  std::string multi_row_error;
  if (row_type->IsMultiRow() &&
      (flatten_state == nullptr || !flatten_state->can_flatten())) {
    return MakeSqlErrorAt(identifier)
           << "Cannot access fields on an expression with type "
           << row_type->ShortTypeName(product_mode())
           << "; Fields of MULTIROW columns can be read in JOIN, UNNEST, "
              "and FLATTEN";
  }

  CycleDetector cycle_detector;
  Catalog::FindOptions find_options(&cycle_detector);

  // The context object is shared across all column lookups for this ROW type,
  // which corresponds uniquely to one table scan.
  // This allows the Table to add implementation-defined behavior at table
  // level that has effects across multiple columns.
  // TODO Add test cases that make use of this.
  Table::LazyColumnsTableScanContext* context = row_type->GetTableScanContext();

  absl::StatusOr<const Column*> result =
      table->FindLazyColumn(identifier->GetAsString(), context, find_options);

  // TODO Include test coverage for all error message cases in
  // later CLs.
  if (!result.ok()) {
    if (absl::IsNotFound(result.status())) {
      // Lookup returned NotFound.  If it gave a message, use that as the
      // error message.  Otherwise, use the generic not-found message below.
      if (!result.status().message().empty()) {
        return MakeSqlErrorAt(identifier) << result.status().message();
      }
    } else {
      // Lookup failed.  Return the failure.
      return result.status();
    }
  }
  const Column* column = result.value_or(nullptr);
  if (column == nullptr) {
    return MakeSqlErrorAt(identifier)
           << "Table " << table->FullName() << " does not have a column "
           << ToIdentifierLiteral(identifier->GetAsIdString());
  }

  *resolved_expr_out = MakeResolvedGetRowField(column->GetType(),
                                               std::move(resolved_lhs), column);
  return absl::OkStatus();
}

namespace {

absl::StatusOr<const PropertyGraph*> FindPropertyGraph(
    Catalog* catalog, absl::Span<const std::string> graph_reference,
    const Catalog::FindOptions& find_options) {
  // Lookup the PropertyGraph this element belongs to from the current catalog.
  // PropertyGraph is identified by `graph_reference` in GraphElementType, which
  // is the same `graph_reference` used to lookup the PropertyGraph that
  // produced the GraphElementType.
  const PropertyGraph* graph;
  GOOGLESQL_RETURN_IF_ERROR(
      catalog->FindPropertyGraph(graph_reference, graph, find_options));
  return graph;
}

}  // namespace

absl::Status Resolver::ResolveGraphElementPropertyAccess(
    const ASTIdentifier* property_name_identifier,
    std::unique_ptr<const ResolvedExpr> resolved_lhs,
    std::unique_ptr<const ResolvedExpr>* resolved_expr_out) {
  resolved_expr_out->reset();
  const auto* lhs_type = resolved_lhs->type();
  GOOGLESQL_RET_CHECK(lhs_type->IsGraphElement());
  const auto* graph_element_type = lhs_type->AsGraphElement();

  absl::string_view property_name = property_name_identifier->GetAsStringView();
  GOOGLESQL_ASSIGN_OR_RETURN(
      const PropertyGraph* graph,
      FindPropertyGraph(catalog_, graph_element_type->graph_reference(),
                        analyzer_options_.find_options()));
  GOOGLESQL_ASSIGN_OR_RETURN(
      *resolved_expr_out,
      ResolveGraphGetElementProperty(
          property_name_identifier, graph, graph_element_type, property_name,
          /*supports_dynamic_properties=*/
          language().LanguageFeatureEnabled(
              FEATURE_SQL_GRAPH_DYNAMIC_ELEMENT_TYPE),
          std::move(resolved_lhs)));

  return absl::OkStatus();
}

absl::Status Resolver::ResolvePropertyNameArgument(
    const ASTExpression* property_name_identifier,
    std::vector<std::unique_ptr<const ResolvedExpr>>& resolved_arguments_out,
    std::vector<const ASTNode*>& ast_arguments_out) {
  GOOGLESQL_RET_CHECK(!resolved_arguments_out.empty());
  GOOGLESQL_RET_CHECK_EQ(resolved_arguments_out.size(), ast_arguments_out.size());

  if (resolved_arguments_out.back() == nullptr ||
      !resolved_arguments_out.back()->type()->IsGraphElement()) {
    return MakeSqlErrorAt(ast_arguments_out.back())
           << "First argument of PROPERTY_EXISTS must be GRAPH_ELEMENT";
  }
  if (property_name_identifier->node_kind() != AST_PATH_EXPRESSION ||
      property_name_identifier->GetAsOrDie<ASTPathExpression>()->num_names() !=
          1) {
    return MakeSqlErrorAt(property_name_identifier)
           << "Second argument of PROPERTY_EXISTS must be identifier of the "
              "property";
  }
  const GraphElementType* graph_element_type =
      resolved_arguments_out.back()->type()->AsGraphElement();
  GOOGLESQL_ASSIGN_OR_RETURN(
      const PropertyGraph* graph,
      FindPropertyGraph(catalog_, graph_element_type->graph_reference(),
                        analyzer_options_.find_options()));
  absl::string_view property_name =
      property_name_identifier->GetAsOrDie<ASTPathExpression>()
          ->last_name()
          ->GetAsStringView();
  const GraphPropertyDeclaration* unused_prop_dcl;
  const absl::Status status =
      graph->FindPropertyDeclarationByName(property_name, unused_prop_dcl);
  if (absl::IsNotFound(status)
      // Undeclared property allowed for dynamic element type.
      && !graph_element_type->is_dynamic()) {
    return MakeSqlErrorAt(property_name_identifier)
           << "Property " << ToIdentifierLiteral(property_name)
           << " is not defined in PropertyGraph " << graph->FullName();
  }
  if (!status.ok() && !absl::IsNotFound(status)) {
    return status;
  }
  // Make this string literal without AST location so it won't be used as
  // candidate for literal replacement.
  resolved_arguments_out.push_back(MakeResolvedLiteralWithoutLocation(
      Value::String(property_name_identifier->GetAsOrDie<ASTPathExpression>()
                        ->last_name()
                        ->GetAsStringView())));
  ast_arguments_out.push_back(property_name_identifier);
  return absl::OkStatus();
}

absl::Status Resolver::ResolveGraphIsLabeledPredicate(
    const ASTGraphIsLabeledPredicate* predicate,
    ExprResolutionInfo* expr_resolution_info,
    std::unique_ptr<const ResolvedExpr>* resolved_expr_out) {
  std::unique_ptr<const ResolvedExpr> operand;
  GOOGLESQL_RETURN_IF_ERROR(
      ResolveExpr(predicate->operand(), expr_resolution_info, &operand));
  if (!operand->type()->IsGraphElement()) {
    return MakeSqlErrorAt(predicate->operand())
           << "Operand to IS LABELED must be a graph element";
  }
  if (operand->node_kind() != RESOLVED_COLUMN_REF) {
    return MakeSqlErrorAt(predicate->operand())
           << "Operand to IS LABELED must be a column reference";
  }

  const PropertyGraph* graph;
  GOOGLESQL_RETURN_IF_ERROR(catalog_->FindPropertyGraph(
      operand->type()->AsGraphElement()->graph_reference(), graph));
  GraphElementTable::Kind element_kind =
      operand->type()->AsGraphElement()->IsNode()
          ? GraphElementTable::Kind::kNode
          : GraphElementTable::Kind::kEdge;

  GOOGLESQL_RETURN_IF_ERROR(ValidateGraphElementTablesDynamicLabelAndProperties(
      language(), predicate, *graph));
  absl::flat_hash_map<const GraphElementTable*, const GraphDynamicLabel*>
      dynamic_labels;
  absl::flat_hash_set<const GraphElementLabel*> static_labels;
  GOOGLESQL_RETURN_IF_ERROR(FindAllLabelsApplicableToElementKind(
      *graph, element_kind, static_labels, dynamic_labels));
  GOOGLESQL_ASSIGN_OR_RETURN(
      std::unique_ptr<const ResolvedGraphLabelExpr> label_expr,
      ResolveGraphLabelExpr(
          predicate->label_expression(), element_kind, static_labels, graph,
          /*supports_dynamic_labels=*/
          language().LanguageFeatureEnabled(
              FEATURE_SQL_GRAPH_DYNAMIC_ELEMENT_TYPE),
          /*element_table_contains_dynamic_label=*/!dynamic_labels.empty()));
  GOOGLESQL_ASSIGN_OR_RETURN(
      std::unique_ptr<const ResolvedGraphIsLabeledPredicate> output,
      ResolvedGraphIsLabeledPredicateBuilder()
          .set_type(type_factory_->get_bool())
          .set_is_not(predicate->is_not())
          .set_expr(std::move(operand))
          .set_label_expr(std::move(label_expr))
          .Build());
  *resolved_expr_out = std::move(output);
  return absl::OkStatus();
}

absl::Status Resolver::ResolveFieldAccess(
    std::unique_ptr<const ResolvedExpr> resolved_lhs,
    const ParseLocationRange& parse_location, const ASTIdentifier* identifier,
    FlattenState* flatten_state,
    std::unique_ptr<const ResolvedExpr>* resolved_expr_out) {
  AnnotatedType lhs_type = resolved_lhs->annotated_type();
  std::unique_ptr<ResolvedFlatten> resolved_flatten;
  if (lhs_type.type->IsArrayLike() && flatten_state != nullptr &&
      flatten_state->can_flatten()) {
    if (lhs_type.type->IsRow()) {
      GOOGLESQL_ASSIGN_OR_RETURN(const Type* element_type,
                       lhs_type.type->GetElementType());
      const AnnotationMap* annotation_map = nullptr;
      if (lhs_type.annotation_map != nullptr) {
        GOOGLESQL_RET_CHECK(lhs_type.annotation_map->IsStructMap());
        GOOGLESQL_RET_CHECK_EQ(lhs_type.annotation_map->AsStructMap()->num_fields(), 1);
        annotation_map = lhs_type.annotation_map->AsStructMap()->field(0);
      }
      lhs_type = AnnotatedType(element_type, annotation_map);
    } else {
      lhs_type = ElementAnnotatedTypeWithFallbackToSelf(
          resolved_lhs->annotated_type());
    }
    if (resolved_lhs->Is<ResolvedFlatten>() &&
        flatten_state->active_flatten() != nullptr) {
      resolved_flatten.reset(const_cast<ResolvedFlatten*>(
          resolved_lhs.release()->GetAs<ResolvedFlatten>()));
      GOOGLESQL_RET_CHECK_EQ(flatten_state->active_flatten(), resolved_flatten.get());
    } else {
      resolved_flatten =
          MakeResolvedFlatten(/*type=*/nullptr, std::move(resolved_lhs), {});
      GOOGLESQL_RET_CHECK_EQ(nullptr, flatten_state->active_flatten());
      flatten_state->set_active_flatten(resolved_flatten.get());
    }
    resolved_lhs = MakeResolvedFlattenedArg(lhs_type);
  }

  if (lhs_type.type->IsProto()) {
    GOOGLESQL_RETURN_IF_ERROR(MaybeResolveProtoFieldAccess(
        parse_location, identifier, MaybeResolveProtoFieldOptions(),
        std::move(resolved_lhs), resolved_expr_out));
  } else if (lhs_type.type->IsStruct()) {
    GOOGLESQL_RETURN_IF_ERROR(MaybeResolveStructFieldAccess(parse_location, identifier,
                                                  /*error_if_not_found=*/true,
                                                  std::move(resolved_lhs),
                                                  resolved_expr_out));
  } else if (lhs_type.type->IsJson()) {
    GOOGLESQL_RETURN_IF_ERROR(ResolveJsonFieldAccess(identifier, std::move(resolved_lhs),
                                           resolved_expr_out));
  } else if (lhs_type.type->IsRow()) {
    GOOGLESQL_RETURN_IF_ERROR(ResolveGetRowField(
        identifier, flatten_state, std::move(resolved_lhs), resolved_expr_out));
  } else if (lhs_type.type->IsGraphElement()) {
    GOOGLESQL_RETURN_IF_ERROR(ResolveGraphElementPropertyAccess(
        identifier, std::move(resolved_lhs), resolved_expr_out));
  } else if (lhs_type.type->IsArray() &&
             language().LanguageFeatureEnabled(
                 FEATURE_UNNEST_AND_FLATTEN_ARRAYS)) {
    return MakeSqlErrorAt(identifier)
           << "Cannot access field " << identifier->GetAsIdString()
           << " on a value with type "
           << lhs_type.type->ShortTypeName(product_mode()) << ". "
           << "You may need an explicit call to FLATTEN, and the flattened "
              "argument may only contain 'dot' after the first array";
  } else if (resolved_lhs->Is<ResolvedParameter>() &&
             resolved_lhs->GetAs<ResolvedParameter>()->is_untyped()) {
    const ResolvedParameter* param = resolved_lhs->GetAs<ResolvedParameter>();
    return MakeSqlErrorAt(identifier)
           << "Cannot access field " << identifier->GetAsIdString()
           << " on parameter "
           << (param->position() == 0 ? param->name()
                                      : absl::StrCat("#", param->position()))
           << " whose type is unknown";
  } else {
    return MakeSqlErrorAt(identifier)
           << "Cannot access field " << identifier->GetAsIdString()
           << " on a value with type "
           << lhs_type.type->ShortTypeName(product_mode());
  }

  GOOGLESQL_RET_CHECK(*resolved_expr_out != nullptr);
  if (resolved_flatten != nullptr) {
    GOOGLESQL_RETURN_IF_ERROR(AddGetFieldToFlatten(
        std::move(*resolved_expr_out), type_factory_, resolved_flatten.get()));
    *resolved_expr_out = std::move(resolved_flatten);
  }
  return absl::OkStatus();
}

absl::Status Resolver::ResolveExtensionFieldAccess(
    std::unique_ptr<const ResolvedExpr> resolved_lhs,
    const ResolveExtensionFieldOptions& options,
    const ASTPathExpression* ast_path_expr, FlattenState* flatten_state,
    std::unique_ptr<const ResolvedExpr>* resolved_expr_out) {
  std::unique_ptr<ResolvedFlatten> resolved_flatten;
  if (resolved_lhs->type()->IsArray() && flatten_state != nullptr &&
      flatten_state->can_flatten()) {
    AnnotatedType lhs_type =
        ElementAnnotatedTypeWithFallbackToSelf(resolved_lhs->annotated_type());
    if (resolved_lhs->Is<ResolvedFlatten>() &&
        flatten_state->active_flatten() != nullptr) {
      resolved_flatten.reset(const_cast<ResolvedFlatten*>(
          resolved_lhs.release()->GetAs<ResolvedFlatten>()));
      GOOGLESQL_RET_CHECK_EQ(flatten_state->active_flatten(), resolved_flatten.get());
    } else {
      resolved_flatten =
          MakeResolvedFlatten(/*type=*/nullptr, std::move(resolved_lhs), {});
      GOOGLESQL_RET_CHECK_EQ(nullptr, flatten_state->active_flatten());
      flatten_state->set_active_flatten(resolved_flatten.get());
    }
    resolved_lhs = MakeResolvedFlattenedArg(lhs_type);
  }

  if (!resolved_lhs->type()->IsProto()) {
    return MakeSqlErrorAt(ast_path_expr)
           << "Generalized field access is not supported on expressions of "
              "type "
           << resolved_lhs->type()->ShortTypeName(product_mode());
  }

  const ProtoType* lhs_proto_type = resolved_lhs->type()->AsProto();
  const google::protobuf::Descriptor* lhs_proto = lhs_proto_type->descriptor();
  GOOGLESQL_ASSIGN_OR_RETURN(const google::protobuf::FieldDescriptor* extension_field,
                   FindExtensionFieldDescriptor(ast_path_expr, lhs_proto));

  const Type* field_type;
  Value default_value;
  if (options.get_has_bit) {
    if (extension_field->is_repeated() || resolved_flatten != nullptr) {
      return MakeSqlErrorAt(ast_path_expr)
             << "Protocol buffer " << lhs_proto->full_name()
             << " extension field " << extension_field->name()
             << " is repeated, so "
             << absl::StrCat("HAS((", extension_field->name(), "))")
             << " is not allowed";
    }

    field_type = type_factory_->get_bool();
  } else {
    auto default_options = ProtoFieldDefaultOptions::FromFieldAndLanguage(
        extension_field, language());
    if (options.ignore_format_annotations) {
      // ignore_format_annotations in the options implies a RAW() fetch.
      // RAW() fetches ignore any annotations on the fields and give you back
      // whatever you would have gotten without those annotations. This applies
      // to both type annotations and default annotations.
      default_options.ignore_format_annotations = true;
      default_options.ignore_use_default_annotations = true;
    }
    GOOGLESQL_RETURN_IF_ERROR(
        GetProtoFieldTypeAndDefault(default_options, extension_field,
                                    lhs_proto_type->CatalogNamePath(),
                                    type_factory_, &field_type, &default_value))
        .With(LocationOverride(ast_path_expr));

    if (options.ignore_format_annotations && field_type->IsBytes()) {
      const Type* type_with_annotations;
      GOOGLESQL_RETURN_IF_ERROR(type_factory_->GetProtoFieldType(
          extension_field, lhs_proto_type->CatalogNamePath(),
          &type_with_annotations));
      if (type_with_annotations->IsGeography()) {
        return MakeSqlErrorAt(ast_path_expr)
               << "RAW() extractions of Geography fields are unsupported";
      }
    }
  }

  if ((field_type->UsingFeatureV12CivilTimeType() &&
       !language().LanguageFeatureEnabled(FEATURE_CIVIL_TIME)) ||
      (field_type->IsNumericType() &&
       !language().LanguageFeatureEnabled(FEATURE_NUMERIC_TYPE))) {
    return MakeSqlErrorAt(ast_path_expr)
           << "Protocol buffer extension " << extension_field->full_name()
           << " has unsupported type "
           << field_type->ShortTypeName(language().product_mode());
  }

  auto resolved_get_proto_field = MakeResolvedGetProtoField(
      field_type, std::move(resolved_lhs), extension_field, default_value,
      options.get_has_bit,
      ((options.get_has_bit || options.ignore_format_annotations)
           ? FieldFormat::DEFAULT_FORMAT
           : ProtoType::GetFormatAnnotation(extension_field)),
      /*return_default_value_when_unset=*/false);

  MaybeRecordParseLocation(ast_path_expr, resolved_get_proto_field.get());
  *resolved_expr_out = std::move(resolved_get_proto_field);
  if (resolved_flatten != nullptr) {
    GOOGLESQL_RETURN_IF_ERROR(AddGetFieldToFlatten(
        std::move(*resolved_expr_out), type_factory_, resolved_flatten.get()));
    *resolved_expr_out = std::move(resolved_flatten);
  }
  return absl::OkStatus();
}

absl::Status Resolver::ResolveOneofCase(
    const ASTIdentifier* oneof_identifier,
    std::unique_ptr<const ResolvedExpr> resolved_lhs,
    std::unique_ptr<const ResolvedExpr>* resolved_expr_out) {
  GOOGLESQL_RET_CHECK(resolved_lhs->type()->IsProto());
  const google::protobuf::Descriptor* message_desc =
      resolved_lhs->type()->AsProto()->descriptor();

  const std::string oneof_name = oneof_identifier->GetAsString();
  const google::protobuf::OneofDescriptor* oneof_desc =
      message_desc->FindOneofByName(oneof_name);
  if (oneof_desc == nullptr) {
    std::string error_message =
        absl::StrCat("Protocol buffer ", message_desc->full_name(),
                     " does not have a Oneof named '", oneof_name, "'");
    if (const google::protobuf::FieldDescriptor* oneof_field =
            message_desc->FindFieldByName(oneof_name);
        oneof_field != nullptr && oneof_field->containing_oneof() != nullptr) {
      absl::StrAppend(&error_message, ". Did you mean '",
                      oneof_field->containing_oneof()->name(),
                      "' which contains '", oneof_name, "'?");
    }
    return MakeSqlErrorAt(oneof_identifier) << error_message;
  }

  *resolved_expr_out = MakeResolvedGetProtoOneof(
      type_factory_->get_string(), std::move(resolved_lhs), oneof_desc);
  return absl::OkStatus();
}

// TODO: The noinline attribute is to prevent the stack usage
// being added to its caller "Resolver::ResolveExpr" which is a recursive
// function. Now the attribute has to be added for all callees. Hopefully
// the fix allows replacing these with one attribute on ResolveExpr.
ABSL_ATTRIBUTE_NOINLINE
absl::Status Resolver::ResolveDotGeneralizedField(
    const ASTDotGeneralizedField* dot_generalized_field,
    ExprResolutionInfo* expr_resolution_info,
    std::unique_ptr<const ResolvedExpr>* resolved_expr_out) {
  std::unique_ptr<const ResolvedExpr> resolved_lhs;
  GOOGLESQL_RETURN_IF_ERROR(ResolveExpr(dot_generalized_field->expr(),
                              expr_resolution_info, &resolved_lhs));

  // The only supported operation using generalized field access currently
  // is reading proto extensions.
  return ResolveExtensionFieldAccess(
      std::move(resolved_lhs), ResolveExtensionFieldOptions(),
      dot_generalized_field->path(), &expr_resolution_info->flatten_state,
      resolved_expr_out);
}

absl::StatusOr<const google::protobuf::FieldDescriptor*> Resolver::FindFieldDescriptor(
    const ASTNode* ast_name_location, const google::protobuf::Descriptor* descriptor,
    absl::string_view name) {
  const google::protobuf::FieldDescriptor* field =
      ProtoType::FindFieldByNameIgnoreCase(descriptor, name);
  return field;
}

absl::Status Resolver::FindFieldDescriptors(
    absl::Span<const ASTIdentifier* const> path_vector,
    const ProtoType* root_type,
    std::vector<const google::protobuf::FieldDescriptor*>* field_descriptors) {
  GOOGLESQL_RET_CHECK(root_type != nullptr);
  GOOGLESQL_RET_CHECK(root_type->descriptor() != nullptr);
  GOOGLESQL_RET_CHECK(field_descriptors != nullptr);
  // Inside the loop, 'current_descriptor' will be NULL if
  // field_descriptors->back() is not of message type.
  const google::protobuf::Descriptor* current_descriptor = root_type->descriptor();
  for (int path_index = 0; path_index < path_vector.size(); ++path_index) {
    if (current_descriptor == nullptr) {
      // There was an attempt to modify a field of a non-message type field.
      const Type* last_type;
      GOOGLESQL_RETURN_IF_ERROR(type_factory_->GetProtoFieldType(
          field_descriptors->back(), root_type->CatalogNamePath(), &last_type));
      return MakeSqlErrorAt(path_vector[path_index])
             << "Cannot access field " << path_vector[path_index]->GetAsString()
             << " on a value with type "
             << last_type->ShortTypeName(product_mode());
    }
    GOOGLESQL_ASSIGN_OR_RETURN(
        const google::protobuf::FieldDescriptor* field_descriptor,
        FindFieldDescriptor(path_vector[path_index], current_descriptor,
                            path_vector[path_index]->GetAsString()));
    if (field_descriptor == nullptr) {
      std::string error_message_prefix = "Protocol buffer ";
      if (path_index > 0) {
        error_message_prefix = absl::StrCat(
            "Field ", path_vector[path_index - 1]->GetAsString(), " of type ");
      }
      return MakeSqlErrorAt(path_vector[path_index])
             << error_message_prefix << current_descriptor->full_name()
             << " does not have a field named "
             << path_vector[path_index]->GetAsString();
    }
    field_descriptors->push_back(field_descriptor);
    // 'current_descriptor' will be NULL if 'field_descriptor' is not a message.
    current_descriptor = field_descriptor->message_type();
  }

  return absl::OkStatus();
}

static absl::Status MakeCannotAccessFieldError(
    const ASTNode* field_to_extract_location,
    absl::string_view field_to_extract, absl::string_view invalid_type_name,
    bool is_extension) {
  return MakeSqlErrorAt(field_to_extract_location)
         << "Cannot access " << (is_extension ? "extension (" : "field ")
         << field_to_extract << (is_extension ? ")" : "")
         << " on a value with type " << invalid_type_name;
}

absl::Status Resolver::GetLastSeenFieldType(
    const Resolver::FindFieldsOutput& output,
    absl::Span<const std::string> catalog_name_path, TypeFactory* type_factory,
    const Type** last_field_type) {
  if (!output.field_descriptor_path.empty()) {
    return type_factory->GetProtoFieldType(output.field_descriptor_path.back(),
                                           catalog_name_path, last_field_type);
  }
  // No proto fields have been extracted, therefore return the type of the
  // last struct field.
  GOOGLESQL_RET_CHECK(!output.struct_path.empty());
  *last_field_type = output.struct_path.back().field->type;
  return absl::OkStatus();
}

absl::StatusOr<Resolver::FindFieldsOutput>
Resolver::FindFieldsFromPathExpression(
    absl::string_view function_name,
    const ASTGeneralizedPathExpression* generalized_path, const Type* root_type,
    bool can_traverse_array_fields) {
  RETURN_ERROR_IF_OUT_OF_STACK_SPACE();
  GOOGLESQL_RET_CHECK(generalized_path != nullptr);
  GOOGLESQL_RET_CHECK(root_type->IsStructOrProto());
  Resolver::FindFieldsOutput output;
  switch (generalized_path->node_kind()) {
    case AST_PATH_EXPRESSION: {
      auto path_expression = generalized_path->GetAsOrDie<ASTPathExpression>();
      if (root_type->IsProto()) {
        if (path_expression->parenthesized()) {
          GOOGLESQL_ASSIGN_OR_RETURN(
              const google::protobuf::FieldDescriptor* field_descriptor,
              FindExtensionFieldDescriptor(path_expression,
                                           root_type->AsProto()->descriptor()));
          output.field_descriptor_path.push_back(field_descriptor);
        } else {
          GOOGLESQL_RETURN_IF_ERROR(FindFieldDescriptors(path_expression->names(),
                                               root_type->AsProto(),
                                               &output.field_descriptor_path));
        }
      } else {
        GOOGLESQL_RETURN_IF_ERROR(FindStructFieldPrefix(path_expression->names(),
                                              root_type->AsStruct(),
                                              &output.struct_path));
        const Type* last_struct_field_type =
            output.struct_path.back().field->type;
        if (last_struct_field_type->IsProto() &&
            path_expression->num_names() != output.struct_path.size()) {
          // There are proto extractions in this path expression.
          GOOGLESQL_RETURN_IF_ERROR(FindFieldDescriptors(
              path_expression->names().last(path_expression->num_names() -
                                            output.struct_path.size()),
              last_struct_field_type->AsProto(),
              &output.field_descriptor_path));
        }
      }
      break;
    }
    case AST_DOT_GENERALIZED_FIELD: {
      // This has to be a proto extension access since that is the only path
      // expression that gets parsed to an ASTDotGeneralizedField.
      auto dot_generalized_ast =
          generalized_path->GetAsOrDie<ASTDotGeneralizedField>();
      auto generalized_path_expr =
          dot_generalized_ast->expr()
              ->GetAsOrNull<ASTGeneralizedPathExpression>();
      GOOGLESQL_ASSIGN_OR_RETURN(output, FindFieldsFromPathExpression(
                                   function_name, generalized_path_expr,
                                   root_type, can_traverse_array_fields));

      // The extension should be extracted from the last seen field in the path,
      // which must be of proto type.
      const Type* last_seen_type;
      GOOGLESQL_RETURN_IF_ERROR(GetLastSeenFieldType(output,
                                           GetTypeCatalogNamePath(root_type),
                                           type_factory_, &last_seen_type));
      if (can_traverse_array_fields && last_seen_type->IsArray()) {
        last_seen_type = last_seen_type->AsArray()->element_type();
      }
      if (!last_seen_type->IsProto()) {
        return MakeCannotAccessFieldError(
            dot_generalized_ast->path(),
            dot_generalized_ast->path()->ToIdentifierPathString(),
            last_seen_type->ShortTypeName(product_mode()),
            /*is_extension=*/true);
      }
      GOOGLESQL_ASSIGN_OR_RETURN(const google::protobuf::FieldDescriptor* field_descriptor,
                       FindExtensionFieldDescriptor(
                           dot_generalized_ast->path(),
                           last_seen_type->AsProto()->descriptor()));
      output.field_descriptor_path.push_back(field_descriptor);
      break;
    }
    case AST_DOT_IDENTIFIER: {
      // This has to be a proto field access since path expressions that parse
      // to ASTDotIdentifier must have at least one parenthesized section of the
      // path somewhere on the left side.
      auto dot_identifier_ast =
          generalized_path->GetAsOrDie<ASTDotIdentifier>();
      auto generalized_path_expr =
          dot_identifier_ast->expr()
              ->GetAsOrNull<ASTGeneralizedPathExpression>();
      GOOGLESQL_ASSIGN_OR_RETURN(output, FindFieldsFromPathExpression(
                                   function_name, generalized_path_expr,
                                   root_type, can_traverse_array_fields));

      // The field should be extracted from the last seen field in the path,
      // which must be of proto type.
      const Type* last_seen_type;
      GOOGLESQL_RETURN_IF_ERROR(GetLastSeenFieldType(output,
                                           GetTypeCatalogNamePath(root_type),
                                           type_factory_, &last_seen_type));
      if (last_seen_type->IsArray() && IsFilterFields(function_name)) {
        last_seen_type = last_seen_type->AsArray()->element_type();
      }
      if (!last_seen_type->IsProto()) {
        return MakeCannotAccessFieldError(
            dot_identifier_ast->name(),
            dot_identifier_ast->name()->GetAsString(),
            last_seen_type->ShortTypeName(product_mode()),
            /*is_extension=*/false);
      }
      GOOGLESQL_RETURN_IF_ERROR(FindFieldDescriptors({dot_identifier_ast->name()},
                                           last_seen_type->AsProto(),
                                           &output.field_descriptor_path));
      break;
    }
    case AST_EXTENDED_PATH_EXPRESSION: {
      auto extended_path_ast =
          generalized_path->GetAsOrDie<ASTExtendedPathExpression>();
      auto parenthesized_path = extended_path_ast->parenthesized_path();
      GOOGLESQL_ASSIGN_OR_RETURN(output, FindFieldsFromPathExpression(
                                   function_name, parenthesized_path, root_type,
                                   can_traverse_array_fields));

      // The extension should be extracted from the last seen field in the path,
      // which must be of proto type.
      const Type* last_seen_type;
      GOOGLESQL_RETURN_IF_ERROR(GetLastSeenFieldType(output,
                                           GetTypeCatalogNamePath(root_type),
                                           type_factory_, &last_seen_type));
      if (can_traverse_array_fields && last_seen_type->IsArray()) {
        last_seen_type = last_seen_type->AsArray()->element_type();
      }
      if (!last_seen_type->IsProto()) {
        return MakeCannotAccessFieldError(
            extended_path_ast->generalized_path_expression(),
            extended_path_ast->generalized_path_expression()->DebugString(),
            last_seen_type->ShortTypeName(product_mode()),
            /*is_extension=*/true);
      }
      Resolver::FindFieldsOutput rhs_output;
      GOOGLESQL_ASSIGN_OR_RETURN(
          rhs_output,
          FindFieldsFromPathExpression(
              function_name, extended_path_ast->generalized_path_expression(),
              last_seen_type, can_traverse_array_fields));
      AppendFindFieldsOutput(rhs_output, &output);
      break;
    }
    case AST_ARRAY_ELEMENT: {
      return MakeSqlErrorAt(generalized_path) << absl::Substitute(
                 "Path expressions in $0() cannot index array fields",
                 function_name);
    }
    default: {
      GOOGLESQL_RET_CHECK_FAIL() << "Invalid generalized path expression input "
                       << generalized_path->DebugString();
    }
  }

  return output;
}

absl::Status Resolver::AddToFieldPathTrie(
    const LanguageOptions& language_options, const ASTNode* path_location,
    absl::Span<const FindFieldsOutput::StructFieldInfo> struct_path_prefix,
    const std::vector<const google::protobuf::FieldDescriptor*>& proto_field_path_suffix,
    absl::flat_hash_map<std::string, std::string>* oneof_path_to_full_path,
    googlesql_base::GeneralTrie<const ASTNode*, nullptr>* field_path_trie) {
  std::string path_string;
  bool overlapping_oneof = false;
  std::string shortest_oneof_path;
  for (const FindFieldsOutput::StructFieldInfo& struct_field :
       struct_path_prefix) {
    if (!path_string.empty()) {
      absl::StrAppend(&path_string, ".");
    }
    absl::StrAppend(&path_string, struct_field.field->name);
  }
  for (const google::protobuf::FieldDescriptor* field : proto_field_path_suffix) {
    if (field->real_containing_oneof() != nullptr &&
        shortest_oneof_path.empty()) {
      shortest_oneof_path =
          absl::StrCat(path_string, field->real_containing_oneof()->name());
      if (oneof_path_to_full_path->contains(shortest_oneof_path)) {
        overlapping_oneof = true;
      }
    }
    if (!path_string.empty()) {
      absl::StrAppend(&path_string, ".");
    }
    if (field->is_extension()) {
      absl::StrAppend(&path_string, "(");
    }
    absl::StrAppend(&path_string,
                    field->is_extension() ? field->full_name() : field->name());
    if (field->is_extension()) {
      absl::StrAppend(&path_string, ")");
    }
  }
  if (overlapping_oneof && !language_options.LanguageFeatureEnabled(
                               FEATURE_REPLACE_FIELDS_ALLOW_MULTI_ONEOF)) {
    return MakeSqlErrorAt(path_location) << absl::StrCat(
               "Modifying multiple fields from the same OneOf is unsupported "
               "by REPLACE_FIELDS(). Field path ",
               path_string, " overlaps with field path ",
               googlesql_base::FindOrDie(*oneof_path_to_full_path, shortest_oneof_path));
  }
  if (!overlapping_oneof && !shortest_oneof_path.empty()) {
    GOOGLESQL_RET_CHECK(googlesql_base::InsertIfNotPresent(oneof_path_to_full_path,
                                      shortest_oneof_path, path_string));
  }

  // Determine if a prefix of 'path_string' is already present in the trie.
  int match_length = 0;
  const ASTNode* prefix_location = field_path_trie->GetDataForMaximalPrefix(
      path_string, match_length, /*is_terminator=*/{});
  std::vector<std::pair<std::string, const ASTNode*>> matching_paths;
  bool prefix_exists = false;
  if (prefix_location != nullptr) {
    // If the max prefix is equal to 'path_string' or if the next character of
    // 'path_string' after the max prefix is a "." then an overlapping path is
    // already present in the trie.
    prefix_exists =
        path_string.size() == match_length
            ? true
            : (std::strncmp(&path_string.at(match_length), ".", 1) == 0);
  } else {
    // Determine if 'path_string' is the prefix of a path already in the trie.
    field_path_trie->GetAllMatchingStrings(absl::StrCat(path_string, "."),
                                           &matching_paths);
  }
  if (prefix_exists || !matching_paths.empty()) {
    return MakeSqlErrorAt(path_location)
           << absl::StrCat("REPLACE_FIELDS() field path ",
                           prefix_exists ? path_string.substr(0, match_length)
                                         : matching_paths.at(0).first,
                           " overlaps with field path ", path_string);
  }
  field_path_trie->Insert(path_string, path_location);

  return absl::OkStatus();
}

absl::Status Resolver::FindStructFieldPrefix(
    absl::Span<const ASTIdentifier* const> path_vector,
    const StructType* root_struct,
    std::vector<Resolver::FindFieldsOutput::StructFieldInfo>* struct_path) {
  GOOGLESQL_RET_CHECK(root_struct != nullptr);
  const StructType* current_struct = root_struct;
  for (const ASTIdentifier* const current_field : path_vector) {
    if (current_struct == nullptr) {
      // There was an attempt to modify a field of a non-message type field.
      return MakeCannotAccessFieldError(
          current_field, current_field->GetAsString(),
          struct_path->back().field->type->ShortTypeName(product_mode()),
          /*is_extension=*/false);
    }
    bool is_ambiguous = false;
    int found_index;
    const StructType::StructField* field = current_struct->FindField(
        current_field->GetAsString(), &is_ambiguous, &found_index);
    if (field == nullptr) {
      if (is_ambiguous) {
        return MakeSqlErrorAt(current_field)
               << "Field name " << current_field->GetAsString()
               << " is ambiguous";
      }
      return MakeSqlErrorAt(current_field)
             << "Struct " << current_struct->ShortTypeName(product_mode())
             << " does not have field named " << current_field->GetAsString();
    }
    struct_path->emplace_back(
        Resolver::FindFieldsOutput::StructFieldInfo(found_index, field));
    if (field->type->IsProto()) {
      return absl::OkStatus();
    }
    current_struct = field->type->AsStruct();
  }

  return absl::OkStatus();
}

class SystemVariableConstant final : public Constant {
 public:
  SystemVariableConstant(const std::vector<std::string>& name_path,
                         const Type* type)
      : Constant(name_path), type_(type) {}

  const Type* type() const override { return type_; }
  std::string DebugString() const override { return FullName(); }
  std::string ConstantValueDebugString() const override { return "<N/A>"; }

 private:
  const Type* const type_;
};

Catalog* Resolver::GetSystemVariablesCatalog() {
  if (system_variables_catalog_ != nullptr) {
    return system_variables_catalog_.get();
  }

  auto catalog = std::make_unique<SimpleCatalog>("<system_variables>");
  for (const auto& entry : analyzer_options_.system_variables()) {
    std::vector<std::string> name_path = entry.first;
    const googlesql::Type* type = entry.second;

    // Traverse the name path, adding nested catalogs as necessary so that
    // the entry has a place to go.
    SimpleCatalog* target_catalog = catalog.get();
    for (size_t i = 0; i < name_path.size() - 1; ++i) {
      const std::string& path_elem = name_path[i];
      Catalog* nested_catalog = nullptr;
      GOOGLESQL_CHECK_OK(target_catalog->GetCatalog(path_elem, &nested_catalog));
      if (nested_catalog == nullptr) {
        auto new_catalog = std::make_unique<SimpleCatalog>(path_elem);
        nested_catalog = new_catalog.get();
        target_catalog->AddOwnedCatalog(std::move(new_catalog));
      }
      target_catalog = static_cast<SimpleCatalog*>(nested_catalog);
    }

    target_catalog->AddOwnedConstant(
        name_path.back(),
        std::make_unique<SystemVariableConstant>(name_path, type));
  }
  system_variables_catalog_ = std::move(catalog);
  return system_variables_catalog_.get();
}

// TODO: The noinline attribute is to prevent the stack usage
// being added to its caller "Resolver::ResolveExpr" which is a recursive
// function. Now the attribute has to be added for all callees. Hopefully
// the fix allows replacing these with one attribute on ResolveExpr.
ABSL_ATTRIBUTE_NOINLINE
absl::Status Resolver::ResolveSystemVariableExpression(
    const ASTSystemVariableExpr* ast_system_variable_expr,
    ExprResolutionInfo* expr_resolution_info,
    std::unique_ptr<const ResolvedExpr>* resolved_expr_out) {
  Catalog* system_variables_catalog = GetSystemVariablesCatalog();

  std::vector<std::string> path_parts =
      ast_system_variable_expr->path()->ToIdentifierVector();

  int num_names_consumed = 0;
  const Constant* constant = nullptr;
  absl::Status find_constant_with_path_prefix_status =
      system_variables_catalog->FindConstantWithPathPrefix(
          path_parts, &num_names_consumed, &constant,
          analyzer_options_.find_options());

  if (find_constant_with_path_prefix_status.code() ==
      absl::StatusCode::kNotFound) {
    return GetUnrecognizedNameError(
        ast_system_variable_expr->location().start(), path_parts,
        expr_resolution_info != nullptr ? expr_resolution_info->name_scope
                                        : nullptr,
        /*is_system_variable=*/true);
  }
  GOOGLESQL_RETURN_IF_ERROR(find_constant_with_path_prefix_status);

  // A constant was found.  Wrap it in an ResolvedSystemVariable node.
  std::vector<std::string> name_path(num_names_consumed);
  for (int i = 0; i < num_names_consumed; ++i) {
    name_path[i] = ast_system_variable_expr->path()->name(i)->GetAsString();
  }

  auto resolved_system_variable =
      MakeResolvedSystemVariable(constant->type(), name_path);
  MaybeRecordParseLocation(ast_system_variable_expr,
                           resolved_system_variable.get());
  *resolved_expr_out = std::move(resolved_system_variable);

  for (; num_names_consumed < ast_system_variable_expr->path()->num_names();
       ++num_names_consumed) {
    GOOGLESQL_RETURN_IF_ERROR(ResolveFieldAccess(
        std::move(*resolved_expr_out),
        ast_system_variable_expr->path()->location(),
        ast_system_variable_expr->path()->name(num_names_consumed),
        &expr_resolution_info->flatten_state, resolved_expr_out));
  }
  return absl::OkStatus();
}

absl::Status Resolver::ResolveFilterFieldsFunctionCall(
    const ASTFunctionCall* ast_function,
    const std::vector<const ASTExpression*>& function_arguments,
    ExprResolutionInfo* expr_resolution_info,
    std::unique_ptr<const ResolvedExpr>* resolved_expr_out) {
  if (function_arguments.empty()) {
    return MakeSqlErrorAt(ast_function)
           << "FILTER_FIELDS() should have arguments";
  }
  std::unique_ptr<const ResolvedExpr> proto_to_modify;
  const ASTExpression* proto_ast = function_arguments.front();
  GOOGLESQL_RETURN_IF_ERROR(
      ResolveExpr(proto_ast, expr_resolution_info, &proto_to_modify));
  if (!proto_to_modify->type()->IsProto()) {
    return MakeSqlErrorAt(proto_ast)
           << "FILTER_FIELDS() expected an input proto type for first "
              "argument, but found type "
           << proto_to_modify->type()->ShortTypeName(product_mode());
  }

  const Type* field_type = proto_to_modify->type();
  std::vector<std::unique_ptr<const ResolvedFilterFieldArg>> filter_field_args;

  FilterFieldsPathValidator validator(field_type->AsProto()->descriptor());
  std::optional<bool> reset_cleared_required_fields;
  constexpr absl::string_view kResetClearedRequiredFields =
      "RESET_CLEARED_REQUIRED_FIELDS";
  for (int i = 1; i < function_arguments.size(); ++i) {
    const ASTExpression* argument = function_arguments[i];
    if (argument->Is<ASTNamedArgument>()) {
      const ASTNamedArgument* named_argument =
          argument->GetAs<ASTNamedArgument>();
      const absl::string_view name = named_argument->name()->GetAsStringView();
      if (!googlesql_base::CaseEqual(name, kResetClearedRequiredFields)) {
        return MakeSqlErrorAt(argument) << absl::Substitute(
                   "Unsupported named argument in FILTER_FIELDS(): $0", name);
      }
      if (reset_cleared_required_fields.has_value()) {
        return MakeSqlErrorAt(argument) << "Duplicated named option";
      }
      const ASTExpression* expr = named_argument->expr();
      if (!expr->Is<ASTBooleanLiteral>()) {
        return MakeSqlErrorAt(argument) << absl::Substitute(
                   "FILTER_FIELDS()'s named argument only supports literal "
                   "bool, but got $0",
                   expr->DebugString());
      }
      reset_cleared_required_fields = expr->GetAs<ASTBooleanLiteral>()->value();
      continue;
    }
    if (!argument->Is<ASTUnaryExpression>() ||
        (argument->GetAs<ASTUnaryExpression>()->op() !=
             ASTUnaryExpression::PLUS &&
         argument->GetAs<ASTUnaryExpression>()->op() !=
             ASTUnaryExpression::MINUS)) {
      return MakeSqlErrorAt(argument) << "FILTER_FIELDS() expected each field "
                                         "path to start with \"+\" or \"-\"";
    }
    const ASTUnaryExpression* unary = argument->GetAs<ASTUnaryExpression>();
    const bool include = unary->op() == ASTUnaryExpression::PLUS;
    // Ignores error message from this function to give an appropriate error
    // message with more context.
    std::string node_kind;
    if (!ASTGeneralizedPathExpression::VerifyIsPureGeneralizedPathExpression(
             unary->operand(), /*node_kind_out=*/&node_kind)
             .ok()) {
      return MakeSqlErrorAt(unary->operand())
             << "FILTER_FIELDS() expected a field path after \"+\" or \"-\", "
                "but got "
             << node_kind;
    }
    const ASTGeneralizedPathExpression* generalized_path_expression =
        unary->operand()->GetAs<ASTGeneralizedPathExpression>();

    GOOGLESQL_ASSIGN_OR_RETURN(Resolver::FindFieldsOutput output,
                     FindFieldsFromPathExpression(
                         "FILTER_FIELDS", generalized_path_expression,
                         proto_to_modify->type(),
                         /*can_traverse_array_fields=*/true));
    if (absl::Status status =
            validator.ValidateFieldPath(include, output.field_descriptor_path);
        !status.ok()) {
      return MakeSqlErrorAt(unary) << status.message();
    }

    filter_field_args.push_back(
        MakeResolvedFilterFieldArg(include, output.field_descriptor_path));
  }
  // Validator also ensures that there is at least one field path.
  if (absl::Status status = validator.FinalValidation(
          reset_cleared_required_fields.value_or(false));
      !status.ok()) {
    return MakeSqlErrorAt(ast_function) << status.message();
  }

  *resolved_expr_out = MakeResolvedFilterField(
      field_type, std::move(proto_to_modify), std::move(filter_field_args),
      reset_cleared_required_fields.value_or(false));
  return absl::OkStatus();
}

// TODO: The noinline attribute is to prevent the stack usage
// being added to its caller "Resolver::ResolveExpr" which is a recursive
// function. Now the attribute has to be added for all callees. Hopefully
// the fix allows replacing these with one attribute on ResolveExpr.
ABSL_ATTRIBUTE_NOINLINE
absl::Status Resolver::ResolveReplaceFieldsExpression(
    const ASTReplaceFieldsExpression* ast_replace_fields,
    ExprResolutionInfo* expr_resolution_info,
    std::unique_ptr<const ResolvedExpr>* resolved_expr_out) {
  RETURN_ERROR_IF_OUT_OF_STACK_SPACE();

  if (!language().LanguageFeatureEnabled(FEATURE_REPLACE_FIELDS)) {
    return MakeSqlErrorAtLocalNode(ast_replace_fields)
           << "REPLACE_FIELDS() is not supported";
  }

  std::unique_ptr<const ResolvedExpr> expr_to_modify;
  GOOGLESQL_RETURN_IF_ERROR(ResolveExpr(ast_replace_fields->expr(), expr_resolution_info,
                              &expr_to_modify));
  if (!expr_to_modify->type()->IsStructOrProto()) {
    return MakeSqlErrorAt(ast_replace_fields->expr())
           << "REPLACE_FIELDS() expected an input type with fields for first "
              "argument, but found type "
           << expr_to_modify->type()->ShortTypeName(product_mode());
  }

  std::vector<std::unique_ptr<const ResolvedReplaceFieldItem>>
      resolved_modify_items;

  // This trie keeps track of the path expressions that are modified by
  // this REPLACE_FIELDS expression.
  googlesql_base::GeneralTrie<const ASTNode*, nullptr> field_path_trie;
  // This map keeps track of the OneOf fields that are modified. Modifying
  // multiple fields from the same OneOf is unsupported when the language
  // feature FEATURE_REPLACE_FIELDS_ALLOW_MULTI_ONEOF is not enabled. This
  // is not tracked in 'field_path_trie' because field path expressions do not
  // contain the OneOf name of modified OneOf fields.
  absl::flat_hash_map<std::string, std::string> oneof_path_to_full_path;
  for (const ASTReplaceFieldsArg* replace_arg :
       ast_replace_fields->arguments()) {
    // Get the field descriptors for the field to be modified.
    GOOGLESQL_ASSIGN_OR_RETURN(
        Resolver::FindFieldsOutput output,
        FindFieldsFromPathExpression(
            "REPLACE_FIELDS", replace_arg->path_expression(),
            expr_to_modify->type(), /*can_traverse_array_fields=*/false));
    GOOGLESQL_RETURN_IF_ERROR(
        AddToFieldPathTrie(language(), replace_arg->path_expression(),
                           output.struct_path, output.field_descriptor_path,
                           &oneof_path_to_full_path, &field_path_trie));

    // Add a cast to the modified value if it needs to be coerced to the type
    // of the field.
    const Type* field_type;
    if (output.field_descriptor_path.empty()) {
      field_type = output.struct_path.back().field->type;
    } else {
      GOOGLESQL_RETURN_IF_ERROR(type_factory_->GetProtoFieldType(
          output.field_descriptor_path.back(),
          GetTypeCatalogNamePath(expr_to_modify->type()), &field_type));
    }
    // Resolve the new value passing down the inferred type.
    std::unique_ptr<const ResolvedExpr> replaced_field_expr;
    GOOGLESQL_RETURN_IF_ERROR(ResolveExpr(replace_arg->expression(), expr_resolution_info,
                                &replaced_field_expr, field_type));

    GOOGLESQL_RETURN_IF_ERROR(CoerceExprToType(
        replace_arg->expression(), field_type, kImplicitAssignment,
        "Cannot replace field of type $0 with value of type $1",
        &replaced_field_expr));
    std::vector<int> struct_index_path;
    struct_index_path.reserve(output.struct_path.size());
    for (const FindFieldsOutput::StructFieldInfo& struct_field :
         output.struct_path) {
      struct_index_path.push_back(struct_field.field_index);
    }
    resolved_modify_items.push_back(MakeResolvedReplaceFieldItem(
        std::move(replaced_field_expr), struct_index_path,
        output.field_descriptor_path));
  }

  const Type* field_type = expr_to_modify->type();
  *resolved_expr_out = MakeResolvedReplaceField(
      field_type, std::move(expr_to_modify), std::move(resolved_modify_items));
  return absl::OkStatus();
}

// Return an error if <expr> is a ResolvedFunctionCall and is getting an un-CAST
// literal NULL as an argument.  <parser_op_sql> is used to make the error
// message.
static absl::Status ReturnErrorOnLiteralNullArg(
    absl::string_view parser_op_sql,
    const std::vector<const ASTNode*>& arg_locations,
    const ResolvedExpr* expr) {
  const ResolvedFunctionCall* function_call;
  if (expr->node_kind() == RESOLVED_FUNCTION_CALL) {
    function_call = expr->GetAs<ResolvedFunctionCall>();
    GOOGLESQL_RET_CHECK_EQ(arg_locations.size(), function_call->argument_list().size());
    for (int i = 0; i < function_call->argument_list().size(); ++i) {
      if (arg_locations[i]->node_kind() == AST_NULL_LITERAL) {
        return MakeSqlErrorAt(arg_locations[i])
               << "Operands of " << parser_op_sql << " cannot be literal NULL";
      }
    }
  }
  return absl::OkStatus();
}

// TODO: The noinline attribute is to prevent the stack usage
// being added to its caller "Resolver::ResolveExpr" which is a recursive
// function. Now the attribute has to be added for all callees. Hopefully
// the fix allows replacing these with one attribute on ResolveExpr.
ABSL_ATTRIBUTE_NOINLINE
absl::Status Resolver::ResolveUnaryExpr(
    const ASTUnaryExpression* unary_expr,
    ExprResolutionInfo* expr_resolution_info,
    std::unique_ptr<const ResolvedExpr>* resolved_expr_out) {
  RETURN_ERROR_IF_OUT_OF_STACK_SPACE();
  absl::string_view function_name =
      FunctionResolver::UnaryOperatorToFunctionName(unary_expr->op());

  if (unary_expr->op() == ASTUnaryExpression::MINUS &&
      !unary_expr->child(0)->GetAsOrDie<ASTExpression>()->parenthesized()) {
    if (unary_expr->child(0)->node_kind() == AST_INT_LITERAL) {
      // Try to parse this as a negative int64.
      const ASTIntLiteral* literal =
          unary_expr->operand()->GetAsOrDie<ASTIntLiteral>();
      int64_t int64_value;
      if (literal->is_hex()) {
        if (googlesql_base::safe_strto64_base(absl::StrCat("-", literal->image()),
                                       &int64_value, 16)) {
          *resolved_expr_out =
              MakeResolvedLiteral(unary_expr, Value::Int64(int64_value));
          return absl::OkStatus();
        } else {
          return MakeSqlErrorAt(unary_expr)
                 << "Invalid hex integer literal: -" << literal->image();
        }
      }
      if (functions::StringToNumeric(absl::StrCat("-", literal->image()),
                                     &int64_value, nullptr)) {
        *resolved_expr_out =
            MakeResolvedLiteral(unary_expr, Value::Int64(int64_value));
        return absl::OkStatus();
      } else {
        return MakeSqlErrorAt(unary_expr)
               << "Invalid integer literal: -" << literal->image();
      }
    } else if (unary_expr->child(0)->node_kind() == AST_FLOAT_LITERAL) {
      // Try to parse this as a negative double.
      const ASTFloatLiteral* literal =
          unary_expr->operand()->GetAsOrDie<ASTFloatLiteral>();
      std::string negative_image = absl::StrCat("-", literal->image());
      double double_value;
      if (functions::StringToNumeric(negative_image, &double_value, nullptr)) {
        std::unique_ptr<const ResolvedLiteral> resolved_literal =
            MakeResolvedFloatLiteral(
                unary_expr, types::DoubleType(), Value::Double(double_value),
                /*has_explicit_type=*/false, negative_image);
        *resolved_expr_out = std::move(resolved_literal);
        return absl::OkStatus();
      } else {
        return MakeSqlErrorAt(unary_expr)
               << "Invalid floating point literal: -" << literal->image();
      }
    }
  } else if (unary_expr->op() == ASTUnaryExpression::PLUS) {
    // Unary plus on NULL is an error.
    if (unary_expr->operand()->node_kind() == AST_NULL_LITERAL) {
      return MakeSqlErrorAt(unary_expr->operand())
             << "Operands of " << unary_expr->GetSQLForOperator()
             << " cannot be literal NULL";
    }

    GOOGLESQL_RETURN_IF_ERROR(ResolveExpr(unary_expr->operand(), expr_resolution_info,
                                resolved_expr_out));

    // Unary plus on non-numeric type is an error.
    if (!(*resolved_expr_out)->type()->IsNumerical()) {
      return MakeSqlErrorAt(unary_expr->operand())
             << "Operands of " << unary_expr->GetSQLForOperator()
             << " must be numeric type but was "
             << (*resolved_expr_out)->type()->ShortTypeName(product_mode());
    }
    return absl::OkStatus();
  } else if (unary_expr->op() == ASTUnaryExpression::IS_UNKNOWN ||
             unary_expr->op() == ASTUnaryExpression::IS_NOT_UNKNOWN) {
    // IS [NOT] UNKNOWN reuses the "is_null" function, and skips the null
    // literal validation. IS NOT UNKNOWN also handles NOT in subsequent step.
    std::unique_ptr<const ResolvedExpr> resolved_operand;
    GOOGLESQL_RETURN_IF_ERROR(ResolveExpr(unary_expr->operand(), expr_resolution_info,
                                &resolved_operand));

    absl::string_view coerce_msg =
        unary_expr->op() == ASTUnaryExpression::IS_UNKNOWN
            ? "Operand of IS UNKNOWN must be coercible to $0, but has type $1"
            : "Operand of IS NOT UNKNOWN must be coercible to $0, but has type "
              "$1";

    GOOGLESQL_RETURN_IF_ERROR(
        CoerceExprToType(unary_expr->operand(), type_factory_->get_bool(),
                         kImplicitCoercion, coerce_msg, &resolved_operand));

    std::vector<std::unique_ptr<const ResolvedExpr>> resolved_arguments;
    resolved_arguments.push_back(std::move(resolved_operand));

    // Explicitly casting or converting resolved literal to target type.
    // It prevents ResolveFunctionCallWithResolvedArguments from undoing
    // the coercion to boolean.
    GOOGLESQL_RETURN_IF_ERROR(
        UpdateLiteralsToExplicit({unary_expr->operand()}, &resolved_arguments));
    GOOGLESQL_RETURN_IF_ERROR(ResolveFunctionCallWithResolvedArguments(
        unary_expr, {unary_expr->operand()},
        /*match_internal_signatures=*/false, "$is_null",
        std::move(resolved_arguments), /*named_arguments=*/{},
        expr_resolution_info, resolved_expr_out));

    if (unary_expr->op() == ASTUnaryExpression::IS_NOT_UNKNOWN) {
      return MakeNotExpr(unary_expr, std::move(*resolved_expr_out),
                         expr_resolution_info, resolved_expr_out);
    }
    return absl::OkStatus();
  }

  GOOGLESQL_RETURN_IF_ERROR(ResolveFunctionCallByNameWithoutAggregatePropertyCheck(
      unary_expr, function_name, {unary_expr->operand()},
      *kEmptyArgumentOptionMap, expr_resolution_info, resolved_expr_out));

  GOOGLESQL_RETURN_IF_ERROR(ReturnErrorOnLiteralNullArg(unary_expr->GetSQLForOperator(),
                                              {unary_expr->operand()},
                                              resolved_expr_out->get()));
  return absl::OkStatus();
}

static const std::string* const kInvalidOperatorTypeStr =
    new std::string("$invalid_is_operator_type");
static const std::string* const kIsFalseFnName = new std::string("$is_false");
static const std::string* const kIsNullFnName = new std::string("$is_null");
static const std::string* const kIsTrueFnName = new std::string("$is_true");

static const std::string& IsOperatorToFunctionName(const ASTExpression* expr) {
  switch (expr->node_kind()) {
    case AST_NULL_LITERAL:
      return *kIsNullFnName;
    case AST_BOOLEAN_LITERAL:
      if (expr->GetAsOrDie<ASTBooleanLiteral>()->value()) {
        return *kIsTrueFnName;
      } else {
        return *kIsFalseFnName;
      }
    default:
      break;
  }

  return *kInvalidOperatorTypeStr;
}

// TODO: The noinline attribute is to prevent the stack usage
// being added to its caller "Resolver::ResolveExpr" which is a recursive
// function. Now the attribute has to be added for all callees. Hopefully
// the fix allows replacing these with one attribute on ResolveExpr.
ABSL_ATTRIBUTE_NOINLINE
absl::Status Resolver::ResolveBinaryExpr(
    const ASTBinaryExpression* binary_expr,
    ExprResolutionInfo* expr_resolution_info,
    std::unique_ptr<const ResolvedExpr>* resolved_expr_out) {
  RETURN_ERROR_IF_OUT_OF_STACK_SPACE();

  std::unique_ptr<const ResolvedExpr> resolved_binary_expr;
  // Special case to handle IS operator. Based on the rhs_ resolved type (i.e.
  // true/false/NULL), the general function name is resolved to
  // $is_true/$is_false/$is_null respectively with lhs_ as an argument.
  bool not_handled = false;
  if (binary_expr->op() == ASTBinaryExpression::IS) {
    const std::string& function_name =
        IsOperatorToFunctionName(binary_expr->rhs());
    GOOGLESQL_RETURN_IF_ERROR(ResolveFunctionCallByNameWithoutAggregatePropertyCheck(
        binary_expr, function_name, {binary_expr->lhs()},
        *kEmptyArgumentOptionMap, expr_resolution_info, &resolved_binary_expr));
  } else {
    absl::string_view function_name =
        FunctionResolver::BinaryOperatorToFunctionName(
            binary_expr->op(), binary_expr->is_not(), &not_handled);
    GOOGLESQL_RETURN_IF_ERROR(ResolveFunctionCallByNameWithoutAggregatePropertyCheck(
        binary_expr, function_name, {binary_expr->lhs(), binary_expr->rhs()},
        *kEmptyArgumentOptionMap, expr_resolution_info, &resolved_binary_expr));

    // Give an error on literal NULL arguments to any binary expression
    // except IS or IS DISTINCT FROM
    if (binary_expr->op() != ASTBinaryExpression::DISTINCT) {
      GOOGLESQL_RETURN_IF_ERROR(
          ReturnErrorOnLiteralNullArg(binary_expr->GetSQLForOperator(),
                                      {binary_expr->lhs(), binary_expr->rhs()},
                                      resolved_binary_expr.get()));
    }
  }

  if (binary_expr->is_not() && !not_handled) {
    return MakeNotExpr(binary_expr, std::move(resolved_binary_expr),
                       expr_resolution_info, resolved_expr_out);
  }

  *resolved_expr_out = std::move(resolved_binary_expr);
  return absl::OkStatus();
}

// TODO: The noinline attribute is to prevent the stack usage
// being added to its caller "Resolver::ResolveExpr" which is a recursive
// function. Now the attribute has to be added for all callees. Hopefully
// the fix allows replacing these with one attribute on ResolveExpr.
ABSL_ATTRIBUTE_NOINLINE
absl::Status Resolver::ResolveBitwiseShiftExpr(
    const ASTBitwiseShiftExpression* bitwise_shift_expr,
    ExprResolutionInfo* expr_resolution_info,
    std::unique_ptr<const ResolvedExpr>* resolved_expr_out) {
  RETURN_ERROR_IF_OUT_OF_STACK_SPACE();
  const std::string& function_name = bitwise_shift_expr->is_left_shift()
                                         ? "$bitwise_left_shift"
                                         : "$bitwise_right_shift";
  GOOGLESQL_RETURN_IF_ERROR(ResolveFunctionCallByNameWithoutAggregatePropertyCheck(
      bitwise_shift_expr->operator_location(), function_name,
      {bitwise_shift_expr->lhs(), bitwise_shift_expr->rhs()},
      *kEmptyArgumentOptionMap, expr_resolution_info, resolved_expr_out));
  return absl::OkStatus();
}

// TODO: The noinline attribute is to prevent the stack usage
// being added to its caller "Resolver::ResolveExpr" which is a recursive
// function. Now the attribute has to be added for all callees. Hopefully
// the fix allows replacing these with one attribute on ResolveExpr.
ABSL_ATTRIBUTE_NOINLINE
absl::Status Resolver::ResolveInExpr(
    const ASTInExpression* in_expr, ExprResolutionInfo* expr_resolution_info,
    std::unique_ptr<const ResolvedExpr>* resolved_expr_out) {
  RETURN_ERROR_IF_OUT_OF_STACK_SPACE();

  std::unique_ptr<const ResolvedExpr> resolved_in_expr;
  if (in_expr->query() != nullptr) {
    GOOGLESQL_RETURN_IF_ERROR(
        ResolveInSubquery(in_expr, expr_resolution_info, &resolved_in_expr));
  } else if (in_expr->in_list() != nullptr) {
    std::vector<const ASTExpression*> in_arguments;
    in_arguments.reserve(1 + in_expr->in_list()->list().size());
    in_arguments.push_back(in_expr->lhs());
    for (const ASTExpression* expr : in_expr->in_list()->list()) {
      in_arguments.push_back(expr);
    }
    GOOGLESQL_RETURN_IF_ERROR(ResolveFunctionCallByNameWithoutAggregatePropertyCheck(
        in_expr->in_location(), "$in", in_arguments, *kEmptyArgumentOptionMap,
        expr_resolution_info, &resolved_in_expr));
  } else {
    const ASTUnnestExpression* unnest_expr = in_expr->unnest_expr();
    GOOGLESQL_RET_CHECK(unnest_expr != nullptr);
    GOOGLESQL_RETURN_IF_ERROR(ValidateUnnestSingleExpression(unnest_expr, "IN operator"));

    std::vector<std::unique_ptr<const ResolvedExpr>> args;
    GOOGLESQL_RETURN_IF_ERROR(
        ResolveExpressionArgument(in_expr->lhs(), expr_resolution_info, &args));

    {
      // The unnest expression is allowed to flatten.
      FlattenState::Restorer restorer;
      expr_resolution_info->flatten_state.set_can_flatten(true, &restorer);
      GOOGLESQL_RETURN_IF_ERROR(ResolveExpressionArgument(
          in_expr->unnest_expr()->expressions()[0]->expression(),
          expr_resolution_info, &args));
    }

    GOOGLESQL_RETURN_IF_ERROR(ResolveFunctionCallWithResolvedArguments(
        in_expr->in_location(),
        {in_expr->lhs(),
         in_expr->unnest_expr()->expressions()[0]->expression()},
        /*match_internal_signatures=*/false, "$in_array", std::move(args),
        /*named_arguments=*/{}, expr_resolution_info, &resolved_in_expr));
  }

  if (analyzer_options_.parse_location_record_type() ==
      PARSE_LOCATION_RECORD_FULL_NODE_SCOPE) {
    MaybeRecordParseLocation(in_expr,
                             const_cast<ResolvedExpr*>(resolved_in_expr.get()));
  }

  if (in_expr->is_not()) {
    return MakeNotExpr(in_expr, std::move(resolved_in_expr),
                       expr_resolution_info, resolved_expr_out);
  }
  *resolved_expr_out = std::move(resolved_in_expr);
  return absl::OkStatus();
}

absl::Status Resolver::ResolveInSubquery(
    const ASTInExpression* in_subquery_expr,
    ExprResolutionInfo* expr_resolution_info,
    std::unique_ptr<const ResolvedExpr>* resolved_expr_out) {
  std::unique_ptr<const ResolvedExpr> resolved_in_expr;

  const ASTExpression* in_expr = in_subquery_expr->lhs();
  const ASTQuery* in_subquery = in_subquery_expr->query();

  GOOGLESQL_RET_CHECK(in_expr != nullptr);
  GOOGLESQL_RETURN_IF_ERROR(
      ResolveExpr(in_expr, expr_resolution_info, &resolved_in_expr));

  CorrelatedColumnsSet correlated_columns_set;
  std::unique_ptr<const NameScope> subquery_scope(
      new NameScope(expr_resolution_info->name_scope, &correlated_columns_set));
  std::unique_ptr<const ResolvedScan> resolved_in_subquery;
  std::shared_ptr<const NameList> resolved_name_list;
  GOOGLESQL_RET_CHECK(in_subquery != nullptr);
  GOOGLESQL_RETURN_IF_ERROR(
      ResolveQuery(in_subquery, subquery_scope.get(), kExprSubqueryId,
                   &resolved_in_subquery, &resolved_name_list,
                   {.inferred_type_for_query = resolved_in_expr->type(),
                    .is_expr_subquery = true}));

  // Order preservation is not relevant for IN subqueries.
  const_cast<ResolvedScan*>(resolved_in_subquery.get())->set_is_ordered(false);

  if (resolved_name_list->num_columns() > 1) {
    return MakeSqlErrorAt(in_subquery)
           << "Subquery of type IN must have only one output column";
  }

  // If the subquery output included pseudo-columns, prune them, because
  // the Resolved AST requires the query to produce exactly one column.
  GOOGLESQL_RETURN_IF_ERROR(MaybeAddProjectForColumnPruning(*resolved_name_list,
                                                  &resolved_in_subquery));
  GOOGLESQL_RET_CHECK_EQ(resolved_in_subquery->column_list().size(), 1);
  // Don't let the subquery output column get pruned.
  RecordColumnAccess(resolved_in_subquery->column_list());

  const Type* in_expr_type = resolved_in_expr->type();
  const Type* in_subquery_type = resolved_name_list->column(0).column().type();

  // TODO: Non-equivalent STRUCTs should still be comparable
  // as long as their related field types are comparable.  Add support for
  // this.
  if (!in_expr_type->SupportsEquality(language()) ||
      !in_subquery_type->SupportsEquality(language()) ||
      (!in_expr_type->Equivalent(in_subquery_type) &&
       (!in_expr_type->IsNumerical() || !in_subquery_type->IsNumerical()))) {
    return MakeSqlErrorAt(in_expr)
           << "Cannot execute IN subquery with uncomparable types "
           << in_expr_type->ShortTypeName(product_mode()) << " and "
           << in_subquery_type->ShortTypeName(product_mode());
  }

  // The check above ensures that the two types support equality and they
  // can be compared.  If the types are Equals then they can be compared
  // directly without any coercion.  Otherwise we try to find a common
  // supertype between the two types and coerce the two expressions to the
  // common supertype before the comparison.
  //
  // TODO:  For non-Equals but Equivalent STRUCTs we are always
  // adding coercion to a common supertype.  However, sometimes the
  // supertyping is unnecessary.  In particular, if the only difference
  // between the STRUCT types is the field names, and all the field types
  // are Equals, then we do not need to do the coercion.  Optimize this.
  if (!in_expr_type->Equals(in_subquery_type)) {
    // We must find the common supertype and add cast(s) where necessary.
    // This check is basically an Equals() test, and is not an equivalence
    // test since if we have two equivalent but different field types
    // (such as two enums with the same name) we must coerce one to the other.
    InputArgumentTypeSet type_set;
    GOOGLESQL_ASSIGN_OR_RETURN(
        InputArgumentType in_expr_arg,
        GetInputArgumentTypeForExpr(
            resolved_in_expr.get(),
            /*pick_default_type_for_untyped_expr=*/false, analyzer_options()));
    type_set.Insert(in_expr_arg);
    // The output column from the subquery column is non-literal, non-parameter.
    type_set.Insert(InputArgumentType(in_subquery_type));
    const Type* supertype = nullptr;
    GOOGLESQL_RETURN_IF_ERROR(coercer_.GetCommonSuperType(type_set, &supertype));
    const Type* in_expr_cast_type = nullptr;
    const Type* in_subquery_cast_type = nullptr;
    if (supertype != nullptr) {
      // We use Equals(), not Equivalent(), because we want to add the cast
      // in that case.
      if (!in_expr_type->Equals(supertype)) {
        in_expr_cast_type = supertype;
      }
      if (!in_subquery_type->Equals(supertype)) {
        in_subquery_cast_type = supertype;
      }
    } else if ((in_expr_type->IsUint64() &&
                in_subquery_type->IsSignedInteger()) ||
               (in_expr_type->IsSignedInteger() &&
                in_subquery_type->IsUint64())) {
      // We need to handle the case where one operand is signed integer and
      // the other is UINT64.
      //
      // The argument types coming out of here should match the '$equals'
      // function signatures, so they must either both be the same type
      // or one must be INT64 and the other must be UINT64.  We know here
      // that one argument is UINT64, so the other is INT64 or INT32.
      // If the signed integer is INT64 then we do not need to do anything.
      // If the signed integer is INT32 then we need to coerce it to INT64.
      if (in_expr_type->IsInt32()) {
        in_expr_cast_type = type_factory_->get_int64();
      }
      if (in_subquery_type->IsInt32()) {
        in_subquery_cast_type = type_factory_->get_int64();
      }
    } else {
      // We did not find a common supertype for the two types, so they
      // are not comparable.
      return MakeSqlErrorAt(in_expr)
             << "Cannot execute IN subquery with uncomparable types "
             << in_expr_type->DebugString() << " and "
             << in_subquery_type->DebugString();
    }
    if (in_expr_cast_type != nullptr) {
      // Add a cast to <in_expr>. Preserve the collation of the original
      // expression.
      GOOGLESQL_RETURN_IF_ERROR(CoerceExprToType(
          in_expr,
          AnnotatedType(in_expr_cast_type,
                        resolved_in_expr->type_annotation_map()),
          kExplicitCoercion, &resolved_in_expr));
    }
    if (in_subquery_cast_type != nullptr) {
      // Add a project on top of the subquery scan that casts its (only)
      // column to the supertype.
      ResolvedColumnList target_columns;
      GOOGLESQL_RET_CHECK_EQ(1, resolved_name_list->num_columns());

      // If the feature is enabled, we keep the original <type_annotation_map>
      // when coercing types of the columns since we may need to process the
      // annotations (e.g. collation) of the original columns at a later
      // stage.
      const AnnotationMap* subquery_target_annotation_map = nullptr;
      if (language().LanguageFeatureEnabled(
              FEATURE_PRESERVE_ANNOTATION_IN_IMPLICIT_CAST_IN_SCAN)) {
        subquery_target_annotation_map =
            resolved_name_list->column(0).column().type_annotation_map();
      }

      target_columns.push_back(
          ResolvedColumn(AllocateColumnId(), kInSubqueryCastId,
                         resolved_name_list->column(0).column().name_id(),
                         AnnotatedType(in_subquery_cast_type,
                                       subquery_target_annotation_map)));

      ResolvedColumnList current_columns =
          resolved_name_list->GetResolvedColumns();

      GOOGLESQL_RETURN_IF_ERROR(CreateWrapperScanWithCasts(
          in_subquery, target_columns, kInSubqueryCastId, &resolved_in_subquery,
          &current_columns));
    }
  }

  std::vector<std::unique_ptr<const ResolvedColumnRef>> parameters;
  FetchCorrelatedSubqueryParameters(correlated_columns_set, &parameters);
  std::unique_ptr<ResolvedSubqueryExpr> resolved_expr =
      MakeResolvedSubqueryExpr(/*type=*/type_factory_->get_bool(),
                               ResolvedSubqueryExpr::IN, std::move(parameters),
                               std::move(resolved_in_expr),
                               std::move(resolved_in_subquery));
  GOOGLESQL_RETURN_IF_ERROR(CheckAndPropagateAnnotations(
      /*error_node=*/in_subquery_expr->query(), resolved_expr.get()));
  MaybeRecordParseLocation(in_subquery_expr->query(), resolved_expr.get());
  GOOGLESQL_RETURN_IF_ERROR(
      ResolveHintsForNode(in_subquery_expr->hint(), resolved_expr.get()));
  GOOGLESQL_RETURN_IF_ERROR(MaybeResolveCollationForSubqueryExpr(
      /*error_location=*/in_subquery_expr->query(), resolved_expr.get()));
  *resolved_expr_out = std::move(resolved_expr);
  return absl::OkStatus();
}

static absl::StatusOr<std::string> GetLikeAnySomeAllOpTypeString(
    const ASTLikeExpression* like_expr) {
  GOOGLESQL_RET_CHECK(like_expr->op() != nullptr);
  switch (like_expr->op()->op()) {
    case ASTAnySomeAllOp::kAny:
      return "ANY";
    case ASTAnySomeAllOp::kSome:
      return "SOME";
    case ASTAnySomeAllOp::kAll:
      return "ALL";
    case ASTAnySomeAllOp::kUninitialized:
      break;
  }
  GOOGLESQL_RET_CHECK_FAIL() << "Operation type for LIKE must be either ANY, SOME or ALL";
}

static absl::StatusOr<std::string> GetLikeAnyAllFunctionName(
    const ASTLikeExpression* like_expr, bool is_not) {
  const bool is_list = like_expr->in_list() != nullptr;
  const bool is_array = like_expr->unnest_expr() != nullptr;
  switch (like_expr->op()->op()) {
    // ANY and SOME are synonyms.
    case ASTAnySomeAllOp::kAny:
    case ASTAnySomeAllOp::kSome:
      if (is_list) {
        return is_not ? "$not_like_any" : "$like_any";
      } else if (is_array) {
        return is_not ? "$not_like_any_array" : "$like_any_array";
      }
      break;
    case ASTAnySomeAllOp::kAll:
      if (is_list) {
        return is_not ? "$not_like_all" : "$like_all";
      } else if (is_array) {
        return is_not ? "$not_like_all_array" : "$like_all_array";
      }
      break;
    default:
      break;
  }

  GOOGLESQL_RET_CHECK_FAIL() << "Unsupported LIKE expression operation. Operation must "
                      "be [NOT] ANY|SOME|ALL.";
}

absl::Status Resolver::ResolveLikeAnyAllExpressionHelper(
    const ASTLikeExpression* like_expr,
    const absl::Span<const ASTExpression* const> arguments,
    ExprResolutionInfo* expr_resolution_info,
    std::unique_ptr<const ResolvedExpr>* resolved_expr_out) {
  GOOGLESQL_ASSIGN_OR_RETURN(std::string function_name,
                   GetLikeAnyAllFunctionName(like_expr, like_expr->is_not()));
  std::unique_ptr<const ResolvedExpr> resolved_like_expr;
  GOOGLESQL_RETURN_IF_ERROR(ResolveFunctionCallByNameWithoutAggregatePropertyCheck(
      like_expr->like_location(), function_name, arguments,
      *kEmptyArgumentOptionMap, expr_resolution_info, &resolved_like_expr));

  *resolved_expr_out = std::move(resolved_like_expr);
  return absl::OkStatus();
}

absl::Status Resolver::ResolveLikeExprArray(
    const ASTLikeExpression* like_expr,
    ExprResolutionInfo* expr_resolution_info,
    std::unique_ptr<const ResolvedExpr>* resolved_expr_out) {
  if (!language().LanguageFeatureEnabled(FEATURE_LIKE_ANY_SOME_ALL_ARRAY)) {
    GOOGLESQL_ASSIGN_OR_RETURN(std::string op_type,
                     GetLikeAnySomeAllOpTypeString(like_expr));
    return MakeSqlErrorAt(like_expr->like_location()) << absl::StrCat(
               "The LIKE ANY|SOME|ALL operator does not support an array of "
               "patterns; did you mean LIKE ",
               op_type, " (pattern1, pattern2, ...)?");
  }

  FlattenState::Restorer restorer;
  expr_resolution_info->flatten_state.set_can_flatten(true, &restorer);
  return ResolveLikeAnyAllExpressionHelper(
      like_expr,
      {like_expr->lhs(),
       like_expr->unnest_expr()->expressions()[0]->expression()},
      expr_resolution_info, resolved_expr_out);
}

absl::Status Resolver::ResolveLikeExprList(
    const ASTLikeExpression* like_expr,
    ExprResolutionInfo* expr_resolution_info,
    std::unique_ptr<const ResolvedExpr>* resolved_expr_out) {
  std::vector<const ASTExpression*> like_arguments;
  like_arguments.reserve(1 + like_expr->in_list()->list().size());
  like_arguments.push_back(like_expr->lhs());
  for (const ASTExpression* expr : like_expr->in_list()->list()) {
    like_arguments.push_back(expr);
  }
  return ResolveLikeAnyAllExpressionHelper(
      like_expr, like_arguments, expr_resolution_info, resolved_expr_out);
}

// TODO: The noinline attribute is to prevent the stack usage
// being added to its caller "Resolver::ResolveExpr" which is a recursive
// function. Now the attribute has to be added for all callees. Hopefully
// the fix allows replacing these with one attribute on ResolveExpr.
ABSL_ATTRIBUTE_NOINLINE
absl::Status Resolver::ResolveLikeExpr(
    const ASTLikeExpression* like_expr,
    ExprResolutionInfo* expr_resolution_info,
    std::unique_ptr<const ResolvedExpr>* resolved_expr_out) {
  RETURN_ERROR_IF_OUT_OF_STACK_SPACE();
  // Regular LIKE expressions (ex. X LIKE Y) are parsed as an
  // ASTBinaryExpression, not an ASTLikeExpression.
  if (!language().LanguageFeatureEnabled(FEATURE_LIKE_ANY_SOME_ALL)) {
    GOOGLESQL_ASSIGN_OR_RETURN(std::string op_type,
                     GetLikeAnySomeAllOpTypeString(like_expr));
    return MakeSqlErrorAt(like_expr->like_location())
           << absl::StrCat("LIKE ", op_type, " is not supported");
  }
  std::unique_ptr<const ResolvedExpr> resolved_like_expr;
  if (like_expr->query() != nullptr) {
    GOOGLESQL_RETURN_IF_ERROR(ResolveLikeExprSubquery(like_expr, expr_resolution_info,
                                            &resolved_like_expr));
  } else if (like_expr->in_list() != nullptr) {
    GOOGLESQL_RETURN_IF_ERROR(ResolveLikeExprList(like_expr, expr_resolution_info,
                                        &resolved_like_expr));
  } else if (like_expr->unnest_expr() != nullptr) {
    GOOGLESQL_ASSIGN_OR_RETURN(std::string op_type,
                     GetLikeAnySomeAllOpTypeString(like_expr));
    GOOGLESQL_RETURN_IF_ERROR(ValidateUnnestSingleExpression(
        like_expr->unnest_expr(), absl::StrCat("LIKE ", op_type)));
    GOOGLESQL_RETURN_IF_ERROR(ResolveLikeExprArray(like_expr, expr_resolution_info,
                                         &resolved_like_expr));
  } else {
    GOOGLESQL_RET_CHECK_FAIL() << "Unsupported LIKE expression. LIKE must have patterns "
                        "to compare to";
  }

  *resolved_expr_out = std::move(resolved_like_expr);
  return absl::OkStatus();
}

absl::Status Resolver::ResolveLikeExprSubquery(
    const ASTLikeExpression* like_subquery_expr,
    ExprResolutionInfo* expr_resolution_info,
    std::unique_ptr<const ResolvedExpr>* resolved_expr_out) {
  if (!language().LanguageFeatureEnabled(FEATURE_LIKE_ANY_SOME_ALL_SUBQUERY)) {
    GOOGLESQL_ASSIGN_OR_RETURN(std::string op_type,
                     GetLikeAnySomeAllOpTypeString(like_subquery_expr));
    return MakeSqlErrorAt(like_subquery_expr->query()) << absl::StrCat(
               "The LIKE ANY|SOME|ALL operator does not support subquery "
               "expression as patterns. Patterns must be string or bytes; "
               "did you mean LIKE ",
               op_type, " (pattern1, pattern2, ...)?");
  }

  std::unique_ptr<const ResolvedExpr> resolved_like_expr;

  const ASTExpression* like_lhs = like_subquery_expr->lhs();
  const ASTQuery* like_subquery = like_subquery_expr->query();
  GOOGLESQL_RETURN_IF_ERROR(
      ResolveExpr(like_lhs, expr_resolution_info, &resolved_like_expr));

  CorrelatedColumnsSet correlated_columns_set;
  auto subquery_scope = std::make_unique<const NameScope>(
      expr_resolution_info->name_scope, &correlated_columns_set);
  std::unique_ptr<const ResolvedScan> resolved_like_subquery;
  std::shared_ptr<const NameList> resolved_name_list;
  GOOGLESQL_RET_CHECK_NE(like_subquery, nullptr);
  GOOGLESQL_RETURN_IF_ERROR(ResolveQuery(
      like_subquery, subquery_scope.get(), kExprSubqueryId,
      &resolved_like_subquery, &resolved_name_list,
      {.inferred_type_for_query = nullptr, .is_expr_subquery = true}));
  GOOGLESQL_RET_CHECK(resolved_name_list->num_columns() != 0);
  if (resolved_name_list->num_columns() > 1) {
    return MakeSqlErrorAt(like_subquery)
           << "Subquery of a LIKE expression must have only one output column";
  }
  // If the subquery output included pseudo-columns, prune them, because
  // the Resolved AST requires the query to produce exactly one column.
  GOOGLESQL_RETURN_IF_ERROR(MaybeAddProjectForColumnPruning(*resolved_name_list,
                                                  &resolved_like_subquery));
  GOOGLESQL_RET_CHECK_EQ(resolved_like_subquery->column_list().size(), 1);
  // Order preservation is not relevant for LIKE subqueries.
  const_cast<ResolvedScan*>(resolved_like_subquery.get())
      ->set_is_ordered(false);

  const Type* like_expr_type = resolved_like_expr->type();
  const Type* like_subquery_type =
      resolved_name_list->column(0).column().type();
  if (!like_expr_type->Equivalent(like_subquery_type) ||
      (!like_expr_type->IsString() && !like_expr_type->IsBytes())) {
    return MakeSqlErrorAt(like_lhs)
           << "Cannot execute a LIKE expression subquery with types "
           << like_expr_type->ShortTypeName(product_mode()) << " and "
           << like_subquery_type->ShortTypeName(product_mode());
  }

  ResolvedSubqueryExpr::SubqueryType subquery_type;
  if (like_subquery_expr->is_not()) {
    switch (like_subquery_expr->op()->op()) {
      case ASTAnySomeAllOp::kAny:
      case ASTAnySomeAllOp::kSome:
        subquery_type = ResolvedSubqueryExpr::NOT_LIKE_ANY;
        break;
      case ASTAnySomeAllOp::kAll:
        subquery_type = ResolvedSubqueryExpr::NOT_LIKE_ALL;
        break;
      default:
        GOOGLESQL_RET_CHECK_FAIL() << "Unsupported NOT LIKE expression operation. "
                            "Operation must be of type ANY|SOME|ALL.";
    }
  } else {
    switch (like_subquery_expr->op()->op()) {
      case ASTAnySomeAllOp::kAny:
      case ASTAnySomeAllOp::kSome:
        subquery_type = ResolvedSubqueryExpr::LIKE_ANY;
        break;
      case ASTAnySomeAllOp::kAll:
        subquery_type = ResolvedSubqueryExpr::LIKE_ALL;
        break;
      default:
        GOOGLESQL_RET_CHECK_FAIL() << "Unsupported LIKE expression operation. Operation "
                            "must be of type ANY|SOME|ALL.";
    }
  }

  std::vector<std::unique_ptr<const ResolvedColumnRef>> parameters;
  FetchCorrelatedSubqueryParameters(correlated_columns_set, &parameters);
  std::unique_ptr<ResolvedSubqueryExpr> resolved_expr =
      MakeResolvedSubqueryExpr(/*type=*/type_factory_->get_bool(),
                               subquery_type, std::move(parameters),
                               std::move(resolved_like_expr),
                               std::move(resolved_like_subquery));
  GOOGLESQL_RETURN_IF_ERROR(CheckAndPropagateAnnotations(
      /*error_node=*/like_subquery_expr->query(), resolved_expr.get()));
  GOOGLESQL_RETURN_IF_ERROR(
      ResolveHintsForNode(like_subquery_expr->hint(), resolved_expr.get()));
  *resolved_expr_out = std::move(resolved_expr);
  return absl::OkStatus();
}

// Figure out the function name for a quantified comparison.
//
// The form of the name is:
//
//   $<op>_<quantifier>[_array]
//
// For example:
//
//   $gt_any
//   $le_all_array
//
static absl::StatusOr<std::string> GetQuantifiedComparisonFunctionName(
    const ASTQuantifiedComparisonExpression* cmp_expr) {
  std::string name = "$";
  switch (cmp_expr->op()) {
    case ASTBinaryExpression::EQ:
      name.append("eq");
      break;
    case ASTBinaryExpression::NE:  // Treat != and <> the same.
    case ASTBinaryExpression::NE2:
      name.append("ne");
      break;
    case ASTBinaryExpression::GT:
      name.append("gt");
      break;
    case ASTBinaryExpression::GE:
      name.append("ge");
      break;
    case ASTBinaryExpression::LT:
      name.append("lt");
      break;
    case ASTBinaryExpression::LE:
      name.append("le");
      break;
    default:
      GOOGLESQL_RET_CHECK_FAIL() << "Unsupported operator in quantified comparison";
      break;
  }
  switch (cmp_expr->quantifier()->op()) {
    case ASTAnySomeAllOp::kAny:  // ANY and SOME are synonyms.
    case ASTAnySomeAllOp::kSome:
      name.append("_any");
      break;
    case ASTAnySomeAllOp::kAll:
      name.append("_all");
      break;
    default:
      GOOGLESQL_RET_CHECK_FAIL() << "Unsupported quantifier in quantified comparison:"
                       << " Must be ANY, SOME, or ALL";
      break;
  }
  if (cmp_expr->unnest_expr() != nullptr) {
    name.append("_array");
  }
  return name;
}

absl::Status Resolver::ResolveQuantifiedComparisonExprArray(
    const ASTNode* op_location, absl::string_view function_name,
    const ASTExpression* lhs_expr, const ASTUnnestExpression* unnest_expr,
    absl::string_view comparison_description,
    ExprResolutionInfo* expr_resolution_info,
    std::unique_ptr<const ResolvedExpr>* resolved_expr_out) {
  // Validate the UNNEST structure.
  GOOGLESQL_RETURN_IF_ERROR(
      ValidateUnnestSingleExpression(unnest_expr, comparison_description));
  const ASTExpression* unnest_arg_expr =
      unnest_expr->expressions()[0]->expression();

  // Resolve LHS expression.
  std::vector<std::unique_ptr<const ResolvedExpr>> args;
  args.reserve(2);
  GOOGLESQL_RETURN_IF_ERROR(
      ResolveExpressionArgument(lhs_expr, expr_resolution_info, &args));

  // Resolve the UNNEST argument expression with flattening enabled.
  {
    FlattenState::Restorer restorer;
    expr_resolution_info->flatten_state.set_can_flatten(true, &restorer);
    GOOGLESQL_RETURN_IF_ERROR(ResolveExpressionArgument(unnest_arg_expr,
                                              expr_resolution_info, &args));
  }

  // Resolve the function call with the now-resolved arguments
  std::vector<const ASTNode*> arg_locations = {lhs_expr, unnest_arg_expr};
  return ResolveFunctionCallWithResolvedArguments(
      op_location, arg_locations,
      /*match_internal_signatures=*/false, function_name, std::move(args),
      /*named_arguments=*/{}, expr_resolution_info, resolved_expr_out);
}

ABSL_ATTRIBUTE_NOINLINE
absl::Status Resolver::ResolveQuantifiedComparisonExpr(
    const ASTQuantifiedComparisonExpression* cmp_expr,
    ExprResolutionInfo* expr_resolution_info,
    std::unique_ptr<const ResolvedExpr>* resolved_expr_out) {
  RETURN_ERROR_IF_OUT_OF_STACK_SPACE();

  // The description is like "= ANY" or ">= SOME", etc.
  const std::string comparison_description =
      absl::StrCat(cmp_expr->GetSQLForOperator(), " ",
                   cmp_expr->quantifier()->GetSQLForOperator());

  if (!language().LanguageFeatureEnabled(
          FEATURE_GENERAL_QUANTIFIED_COMPARISONS)) {
    return MakeSqlErrorAt(cmp_expr->op_location())
           << "Quantified comparisons (" << comparison_description << ")"
           << " are not supported";
  }

  std::unique_ptr<const ResolvedExpr> resolved_cmp_expr;
  if (cmp_expr->query() != nullptr) {
    // Subquery form.
    // TODO: b/465509210
    return MakeSqlErrorAt(cmp_expr->op_location())
           << "Quantified comparisons (" << comparison_description << ")"
           << " with subqueries are not supported";
  } else if (cmp_expr->in_list() != nullptr) {
    // Expression list form.
    // TODO: b/465513375
    return MakeSqlErrorAt(cmp_expr->op_location())
           << "Quantified comparisons (" << comparison_description << ")"
           << " with expression lists are not supported";
  } else if (cmp_expr->unnest_expr() != nullptr) {
    // Array form.
    GOOGLESQL_ASSIGN_OR_RETURN(std::string function_name,
                     GetQuantifiedComparisonFunctionName(cmp_expr));

    GOOGLESQL_RETURN_IF_ERROR(ResolveQuantifiedComparisonExprArray(
        cmp_expr->op_location(), function_name, cmp_expr->lhs(),
        cmp_expr->unnest_expr(), comparison_description, expr_resolution_info,
        &resolved_cmp_expr));
  } else {
    GOOGLESQL_RET_CHECK_FAIL() << "Unsupported quantified comparison expression";
  }

  if (analyzer_options_.parse_location_record_type() ==
      PARSE_LOCATION_RECORD_FULL_NODE_SCOPE) {
    MaybeRecordParseLocation(
        cmp_expr, const_cast<ResolvedExpr*>(resolved_cmp_expr.get()));
  }

  *resolved_expr_out = std::move(resolved_cmp_expr);
  return absl::OkStatus();
}

// TODO: The noinline attribute is to prevent the stack usage
// being added to its caller "Resolver::ResolveExpr" which is a recursive
// function. Now the attribute has to be added for all callees. Hopefully
// the fix allows replacing these with one attribute on ResolveExpr.
ABSL_ATTRIBUTE_NOINLINE
absl::Status Resolver::ResolveBetweenExpr(
    const ASTBetweenExpression* between_expr,
    ExprResolutionInfo* expr_resolution_info,
    std::unique_ptr<const ResolvedExpr>* resolved_expr_out) {
  RETURN_ERROR_IF_OUT_OF_STACK_SPACE();

  std::vector<const ASTExpression*> between_arguments;
  between_arguments.push_back(between_expr->lhs());
  between_arguments.push_back(between_expr->low());
  between_arguments.push_back(between_expr->high());
  std::unique_ptr<const ResolvedExpr> resolved_between_expr;
  GOOGLESQL_RETURN_IF_ERROR(ResolveFunctionCallWithLiteralRetry(
      between_expr->between_location(), "$between", between_arguments,
      *kEmptyArgumentOptionMap, expr_resolution_info, &resolved_between_expr));

  if (analyzer_options_.parse_location_record_type() ==
      PARSE_LOCATION_RECORD_FULL_NODE_SCOPE) {
    MaybeRecordParseLocation(
        between_expr, const_cast<ResolvedExpr*>(resolved_between_expr.get()));
  }

  if (between_expr->is_not()) {
    return MakeNotExpr(between_expr, std::move(resolved_between_expr),
                       expr_resolution_info, resolved_expr_out);
  }

  *resolved_expr_out = std::move(resolved_between_expr);
  return absl::OkStatus();
}

// TODO: The noinline attribute is to prevent the stack usage
// being added to its caller "Resolver::ResolveExpr" which is a recursive
// function. Now the attribute has to be added for all callees. Hopefully
// the fix allows replacing these with one attribute on ResolveExpr.
ABSL_ATTRIBUTE_NOINLINE
absl::Status Resolver::ResolveAndExpr(
    const ASTAndExpr* and_expr, ExprResolutionInfo* expr_resolution_info,
    std::unique_ptr<const ResolvedExpr>* resolved_expr_out) {
  RETURN_ERROR_IF_OUT_OF_STACK_SPACE();
  return ResolveFunctionCallByNameWithoutAggregatePropertyCheck(
      and_expr, "$and", and_expr->conjuncts(), *kEmptyArgumentOptionMap,
      expr_resolution_info, resolved_expr_out);
}

// TODO: The noinline attribute is to prevent the stack usage
// being added to its caller "Resolver::ResolveExpr" which is a recursive
// function. Now the attribute has to be added for all callees. Hopefully
// the fix allows replacing these with one attribute on ResolveExpr.
ABSL_ATTRIBUTE_NOINLINE
absl::Status Resolver::ResolveOrExpr(
    const ASTOrExpr* or_expr, ExprResolutionInfo* expr_resolution_info,
    std::unique_ptr<const ResolvedExpr>* resolved_expr_out) {
  RETURN_ERROR_IF_OUT_OF_STACK_SPACE();
  return ResolveFunctionCallByNameWithoutAggregatePropertyCheck(
      or_expr, "$or", or_expr->disjuncts(), *kEmptyArgumentOptionMap,
      expr_resolution_info, resolved_expr_out);
}

void Resolver::FetchCorrelatedSubqueryParameters(
    const CorrelatedColumnsSet& correlated_columns_set,
    std::vector<std::unique_ptr<const ResolvedColumnRef>>* parameters) {
  for (const auto& item : correlated_columns_set) {
    const ResolvedColumn& column = item.first;
    const bool is_already_correlated = item.second;
    parameters->push_back(MakeColumnRef(column, is_already_correlated));
  }
}

// TODO: The noinline attribute is to prevent the stack usage
// being added to its caller "Resolver::ResolveExpr" which is a recursive
// function. Now the attribute has to be added for all callees. Hopefully
// the fix allows replacing these with one attribute on ResolveExpr.
ABSL_ATTRIBUTE_NOINLINE
absl::Status Resolver::ResolveExprSubquery(
    const ASTExpressionSubquery* expr_subquery,
    ExprResolutionInfo* expr_resolution_info, const Type* inferred_type,
    std::unique_ptr<const ResolvedExpr>* resolved_expr_out) {
  if (generated_column_cycle_detector_ != nullptr) {
    return MakeSqlErrorAt(expr_subquery)
           << "Generated column expression must not include a subquery";
  }
  if (analyzing_check_constraint_expression_) {
    return MakeSqlErrorAt(expr_subquery)
           << "CHECK constraint "
              "expression must not include a subquery";
  }
  if (default_expr_access_error_name_scope_.has_value()) {
    return MakeSqlErrorAt(expr_subquery)
           << "A column default expression must not include a subquery";
  }
  if (expr_resolution_info->in_horizontal_aggregation) {
    return MakeSqlErrorAt(expr_subquery)
           << "Horizontal aggregation expression must not include a subquery";
  }
  std::unique_ptr<CorrelatedColumnsSet> correlated_columns_set =
      std::make_unique<CorrelatedColumnsSet>();
  // TODO: If the subquery appears in the HAVING, then we probably
  // need to allow the select list aliases to resolve also.  Test subqueries
  // in the HAVING.
  std::unique_ptr<const NameScope> subquery_scope(new NameScope(
      expr_resolution_info->name_scope, correlated_columns_set.get()));

  std::unique_ptr<const ResolvedScan> resolved_query;
  std::shared_ptr<const NameList> resolved_name_list;
  const Type* inferred_subquery_type = nullptr;
  switch (expr_subquery->modifier()) {
    case ASTExpressionSubquery::NONE:
    case ASTExpressionSubquery::VALUE:
      inferred_subquery_type = inferred_type;
      break;
    case ASTExpressionSubquery::ARRAY:
      if (inferred_type != nullptr && inferred_type->IsArray()) {
        inferred_subquery_type = inferred_type->AsArray()->element_type();
      }
      break;
    case ASTExpressionSubquery::EXISTS:
      inferred_subquery_type = nullptr;
      break;
    default:
      GOOGLESQL_RET_CHECK_FAIL() << "Invalid subquery modifier: "
                       << expr_subquery->modifier();
  }
  GOOGLESQL_RETURN_IF_ERROR(
      ResolveQuery(expr_subquery->query(), subquery_scope.get(),
                   kExprSubqueryId, &resolved_query, &resolved_name_list,
                   {.inferred_type_for_query = inferred_subquery_type,
                    .is_expr_subquery = true}));

  ResolvedSubqueryExpr::SubqueryType subquery_type;
  switch (expr_subquery->modifier()) {
    case ASTExpressionSubquery::ARRAY:
      subquery_type = ResolvedSubqueryExpr::ARRAY;
      break;
    case ASTExpressionSubquery::NONE:
    case ASTExpressionSubquery::VALUE:
      subquery_type = ResolvedSubqueryExpr::SCALAR;
      break;
    case ASTExpressionSubquery::EXISTS:
      subquery_type = ResolvedSubqueryExpr::EXISTS;
      break;
    default:
      GOOGLESQL_RET_CHECK_FAIL() << "Invalid subquery modifier: "
                       << expr_subquery->modifier();
  }

  if (subquery_type != ResolvedSubqueryExpr::ARRAY) {
    // Order preservation is not relevant for non-ARRAY expression subqueries.
    const_cast<ResolvedScan*>(resolved_query.get())->set_is_ordered(false);
  }

  const Type* output_type;
  if (subquery_type == ResolvedSubqueryExpr::EXISTS) {
    output_type = type_factory_->get_bool();
  } else if (resolved_name_list->num_columns() == 1) {
    output_type = resolved_name_list->column(0).column().type();

    // If the subquery output included pseudo-columns, prune them, because
    // the Resolved AST requires the query to produce exactly one column.
    GOOGLESQL_RETURN_IF_ERROR(
        MaybeAddProjectForColumnPruning(*resolved_name_list, &resolved_query));
    GOOGLESQL_RET_CHECK_EQ(resolved_query->column_list().size(), 1);
    // Don't let the subquery output column get pruned.
    RecordColumnAccess(resolved_query->column_list());
  } else {
    // The subquery has more than one column, which is not allowed without
    // SELECT AS STRUCT.
    GOOGLESQL_RET_CHECK_GE(resolved_name_list->num_columns(), 1);
    return MakeSqlErrorAt(expr_subquery)
           << (subquery_type == ResolvedSubqueryExpr::ARRAY ? "ARRAY"
                                                            : "Scalar")
           << " subquery cannot have more than one column unless using"
              " SELECT AS STRUCT to build STRUCT values";
  }

  if (subquery_type == ResolvedSubqueryExpr::ARRAY) {
    if (output_type->IsArray()) {
      return MakeSqlErrorAt(expr_subquery)
             << "Cannot use array subquery with column of type "
             << output_type->ShortTypeName(product_mode())
             << " because nested arrays are not supported";
    }
    if (IsOrContainsMeasure(output_type) || output_type->IsRow()) {
      return MakeSqlErrorAt(expr_subquery)
             << "Cannot use array subquery with column of type "
             << output_type->ShortTypeName(product_mode());
    }
    GOOGLESQL_RETURN_IF_ERROR(type_factory_->MakeArrayType(output_type, &output_type));
  }

  std::vector<std::unique_ptr<const ResolvedColumnRef>> parameters;
  FetchCorrelatedSubqueryParameters(*correlated_columns_set, &parameters);
  std::unique_ptr<ResolvedSubqueryExpr> resolved_expr =
      MakeResolvedSubqueryExpr(output_type, subquery_type,
                               std::move(parameters), /*in_expr=*/nullptr,
                               std::move(resolved_query));
  GOOGLESQL_RETURN_IF_ERROR(CheckAndPropagateAnnotations(
      /*error_node=*/expr_subquery, resolved_expr.get()));
  MaybeRecordExpressionSubqueryParseLocation(expr_subquery,
                                             resolved_expr.get());
  GOOGLESQL_RETURN_IF_ERROR(
      ResolveHintsForNode(expr_subquery->hint(), resolved_expr.get()));
  switch (expr_subquery->query()->query_expr()->node_kind()) {
    case AST_GQL_QUERY:
      InternalAnalyzerOutputProperties::MarkTargetSyntax(
          analyzer_output_properties_, resolved_expr.get(),
          SQLBuildTargetSyntax::kGqlSubquery);
      break;
    case AST_GQL_GRAPH_PATTERN_QUERY:
      InternalAnalyzerOutputProperties::MarkTargetSyntax(
          analyzer_output_properties_, resolved_expr.get(),
          SQLBuildTargetSyntax::kGqlExistsSubqueryGraphPattern);
      break;
    case AST_GQL_LINEAR_OPS_QUERY:
      InternalAnalyzerOutputProperties::MarkTargetSyntax(
          analyzer_output_properties_, resolved_expr.get(),
          SQLBuildTargetSyntax::kGqlExistsSubqueryLinearOps);
      break;
    default:
      // not GQL subquery
      break;
  }
  *resolved_expr_out = std::move(resolved_expr);
  return absl::OkStatus();
}

absl::Status Resolver::MakeDatePartEnumResolvedLiteral(
    functions::DateTimestampPart date_part,
    std::unique_ptr<const ResolvedExpr>* resolved_date_part) {
  const EnumType* date_part_type;
  const google::protobuf::EnumDescriptor* date_part_descr =
      functions::DateTimestampPart_descriptor();
  GOOGLESQL_RET_CHECK(type_factory_->MakeEnumType(date_part_descr, &date_part_type).ok());
  // Parameter substitution cannot be used for dateparts; don't record parse
  // location.
  *resolved_date_part = MakeResolvedLiteralWithoutLocation(
      Value::Enum(date_part_type, date_part));
  return absl::OkStatus();
}

absl::Status Resolver::MakeDatePartEnumResolvedLiteralFromNames(
    IdString date_part_name, IdString date_part_arg_name,
    const ASTExpression* date_part_ast_location,
    const ASTExpression* date_part_arg_ast_location,
    std::unique_ptr<const ResolvedExpr>* resolved_date_part,
    functions::DateTimestampPart* date_part) {
  GOOGLESQL_RET_CHECK_EQ(date_part_arg_name.empty(),
               date_part_arg_ast_location == nullptr);
  using functions::DateTimestampPart;

  DateTimestampPart local_date_part;
  if (!functions::DateTimestampPart_Parse(
          absl::AsciiStrToUpper(date_part_name.ToStringView()),
          &local_date_part)) {
    return MakeSqlErrorAt(date_part_ast_location)
           << "A valid date part name is required but found " << date_part_name;
  }
  // Users must use WEEK(<WEEKDAY>) instead of WEEK_<WEEKDAY>.
  switch (local_date_part) {
    case functions::WEEK_MONDAY:
    case functions::WEEK_TUESDAY:
    case functions::WEEK_WEDNESDAY:
    case functions::WEEK_THURSDAY:
    case functions::WEEK_FRIDAY:
    case functions::WEEK_SATURDAY:
      return MakeSqlErrorAt(date_part_ast_location)
             << "A valid date part name is required but found "
             << date_part_name;
    default:
      break;
  }

  if (!date_part_arg_name.empty()) {
    if (!language().LanguageFeatureEnabled(FEATURE_WEEK_WITH_WEEKDAY)) {
      return MakeSqlErrorAt(date_part_arg_ast_location)
             << "Date part arguments are not supported";
    }

    if (local_date_part != functions::WEEK) {
      return MakeSqlErrorAt(date_part_arg_ast_location)
             << "Date part arguments are not supported for "
             << functions::DateTimestampPart_Name(local_date_part)
             << ", but found " << date_part_arg_name;
    }

    // WEEK(SUNDAY) is the same as WEEK.
    static const auto* arg_name_to_date_part =
        new IdStringHashMapCase<DateTimestampPart>(
            {{IdString::MakeGlobal("SUNDAY"), DateTimestampPart::WEEK},
             {IdString::MakeGlobal("MONDAY"), DateTimestampPart::WEEK_MONDAY},
             {IdString::MakeGlobal("TUESDAY"), DateTimestampPart::WEEK_TUESDAY},
             {IdString::MakeGlobal("WEDNESDAY"),
              DateTimestampPart::WEEK_WEDNESDAY},
             {IdString::MakeGlobal("THURSDAY"),
              DateTimestampPart::WEEK_THURSDAY},
             {IdString::MakeGlobal("FRIDAY"), DateTimestampPart::WEEK_FRIDAY},
             {IdString::MakeGlobal("SATURDAY"),
              DateTimestampPart::WEEK_SATURDAY}});

    const DateTimestampPart* final_date_part =
        googlesql_base::FindOrNull(*arg_name_to_date_part, date_part_arg_name);
    if (final_date_part == nullptr) {
      return MakeSqlErrorAt(date_part_arg_ast_location)
             << "A valid date part argument for "
             << functions::DateTimestampPart_Name(local_date_part)
             << " is required, but found " << date_part_arg_name;
    }
    local_date_part = *final_date_part;
  }

  if (date_part != nullptr) {
    *date_part = local_date_part;
  }

  return MakeDatePartEnumResolvedLiteral(local_date_part, resolved_date_part);
}

// For a given ASTFunctionCall with non-empty argument list, check if any of the
// arguments contain alias. If it does, throw a SQL error in the first failing
// node.
static absl::Status ValidateASTFunctionCallWithoutArgumentAlias(
    const ASTFunctionCall* ast_function) {
  for (const ASTExpression* arg : ast_function->arguments()) {
    if (arg->node_kind() == AST_EXPRESSION_WITH_ALIAS) {
      return MakeSqlErrorAt(arg->GetAsOrDie<ASTExpressionWithAlias>()->alias())
             << "Unexpected function call argument alias found at "
             << ast_function->function()->ToIdentifierPathString();
    }
  }
  return absl::OkStatus();
}

absl::Status Resolver::ResolveDatePartArgument(
    const ASTExpression* date_part_ast_location,
    std::unique_ptr<const ResolvedExpr>* resolved_date_part,
    functions::DateTimestampPart* date_part) {
  IdString date_part_name;
  IdString date_part_arg_name;
  const ASTExpression* date_part_arg_ast_location = nullptr;
  switch (date_part_ast_location->node_kind()) {
    case AST_IDENTIFIER:
      date_part_name =
          date_part_ast_location->GetAsOrDie<ASTIdentifier>()->GetAsIdString();
      break;
    case AST_PATH_EXPRESSION: {
      const auto ast_path =
          date_part_ast_location->GetAsOrDie<ASTPathExpression>();
      if (ast_path->num_names() != 1) {
        return MakeSqlErrorAt(ast_path)
               << "A valid date part name is required but found "
               << ast_path->ToIdentifierPathString();
      }
      date_part_name = ast_path->first_name()->GetAsIdString();
      break;
    }
    case AST_FUNCTION_CALL: {
      const ASTFunctionCall* ast_function_call =
          date_part_ast_location->GetAsOrDie<ASTFunctionCall>();

      if (ast_function_call->function()->num_names() != 1) {
        return MakeSqlErrorAt(ast_function_call->function())
               << "A valid date part name is required, but found "
               << ast_function_call->function()->ToIdentifierPathString();
      }
      date_part_name =
          ast_function_call->function()->first_name()->GetAsIdString();

      if (ast_function_call->arguments().size() != 1 ||
          ast_function_call->HasModifiers()) {
        return MakeSqlErrorAt(ast_function_call)
               << "Found invalid date part argument function call syntax for "
               << ast_function_call->function()->ToIdentifierPathString()
               << "()";
      }

      GOOGLESQL_RETURN_IF_ERROR(
          ValidateASTFunctionCallWithoutArgumentAlias(ast_function_call));
      date_part_arg_ast_location = ast_function_call->arguments()[0];
      if (date_part_arg_ast_location->node_kind() != AST_PATH_EXPRESSION) {
        // We don't allow AST_IDENTIFIER because the grammar won't allow it to
        // be used as a function argument, despite ASTIdentifier inheriting from
        // ASTExpression.
        return MakeSqlErrorAt(date_part_arg_ast_location)
               << "Found invalid date part argument syntax in argument of "
               << ast_function_call->function()->ToIdentifierPathString();
      }
      const auto ast_path =
          date_part_arg_ast_location->GetAsOrDie<ASTPathExpression>();
      if (ast_path->num_names() != 1) {
        return MakeSqlErrorAt(ast_path)
               << "A valid date part argument is required, but found "
               << ast_path->ToIdentifierPathString();
      }
      date_part_arg_name = ast_path->first_name()->GetAsIdString();
      break;
    }
    default:
      return MakeSqlErrorAt(date_part_ast_location)
             << "A valid date part name is required";
  }

  return MakeDatePartEnumResolvedLiteralFromNames(
      date_part_name, date_part_arg_name, date_part_ast_location,
      date_part_arg_name.empty() ? nullptr : date_part_arg_ast_location,
      resolved_date_part, date_part);
}

// static
absl::StatusOr<Resolver::ProtoExtractionType>
Resolver::ProtoExtractionTypeFromName(absl::string_view extraction_type_name) {
  std::string upper_name = absl::AsciiStrToUpper(extraction_type_name);
  if (upper_name == "HAS") {
    return ProtoExtractionType::kHas;
  } else if (upper_name == "FIELD") {
    return ProtoExtractionType::kField;
  } else if (upper_name == "RAW") {
    return ProtoExtractionType::kRaw;
  } else if (upper_name == "ONEOF_CASE") {
    return ProtoExtractionType::kOneofCase;
  } else {
    return MakeSqlError() << "Unable to parse " << extraction_type_name
                          << " to a valid ProtoExtractionType";
  }
}

// static
std::string Resolver::ProtoExtractionTypeName(
    ProtoExtractionType extraction_type) {
  switch (extraction_type) {
    case ProtoExtractionType::kHas:
      return "HAS";
    case ProtoExtractionType::kField:
      return "FIELD";
    case ProtoExtractionType::kRaw:
      return "RAW";
    case ProtoExtractionType::kOneofCase:
      return "ONEOF_CASE";
  }
}

absl::Status Resolver::ResolveProtoExtractExpression(
    const ASTExpression* field_extraction_type_ast_location,
    std::unique_ptr<const ResolvedExpr> resolved_proto_input,
    std::unique_ptr<const ResolvedExpr>* resolved_expr_out) {
  GOOGLESQL_RET_CHECK(language().LanguageFeatureEnabled(FEATURE_EXTRACT_FROM_PROTO));
  if (field_extraction_type_ast_location->node_kind() != AST_FUNCTION_CALL) {
    return MakeSqlErrorAt(field_extraction_type_ast_location)
           << "Invalid proto extraction function call syntax found. Extraction "
              "type and field should follow the pattern EXTRACTION_TYPE(field)";
  }

  // Determine the field and extraction type from the ACCESSOR(field) input
  // expression.
  const auto ast_function_call =
      field_extraction_type_ast_location->GetAsOrDie<ASTFunctionCall>();
  if (ast_function_call->function()->num_names() != 1) {
    return MakeSqlErrorAt(ast_function_call->function())
           << "A valid proto extraction type is required (e.g., HAS or "
              "FIELD), but found "
           << ast_function_call->function()->ToIdentifierPathString();
  }

  std::string extraction_type_name =
      ast_function_call->function()->first_name()->GetAsString();
  absl::StatusOr<const Resolver::ProtoExtractionType> field_extraction_type_or =
      ProtoExtractionTypeFromName(extraction_type_name);
  if (!field_extraction_type_or.ok()) {
    return MakeSqlErrorAt(field_extraction_type_ast_location)
           << "A valid proto extraction type is required (e.g., HAS or FIELD), "
              "but found "
           << extraction_type_name;
  }
  bool extraction_type_supported = false;
  switch (field_extraction_type_or.value()) {
    case ProtoExtractionType::kHas:
    case ProtoExtractionType::kField:
    case ProtoExtractionType::kRaw: {
      // These EXTRACT types are supported by the base
      // FEATURE_EXTRACT_FROM_PROTO.
      extraction_type_supported = true;
      break;
    }
    case ProtoExtractionType::kOneofCase: {
      extraction_type_supported =
          language().LanguageFeatureEnabled(FEATURE_EXTRACT_ONEOF_CASE);
      break;
    }
  }
  if (!extraction_type_supported) {
    return MakeSqlErrorAt(field_extraction_type_ast_location)
           << "Extraction type "
           << ProtoExtractionTypeName(field_extraction_type_or.value())
           << "() is not supported";
  }

  if (ast_function_call->arguments().size() != 1 ||
      ast_function_call->HasModifiers()) {
    return MakeSqlErrorAt(ast_function_call)
           << "Found invalid argument function call syntax for "
           << ast_function_call->function()->ToIdentifierPathString() << "()";
  }

  GOOGLESQL_RETURN_IF_ERROR(
      ValidateASTFunctionCallWithoutArgumentAlias(ast_function_call));
  const ASTExpression* field_ast_location = ast_function_call->arguments()[0];
  // We don't allow AST_IDENTIFIER because the grammar won't allow it to
  // be used as a function argument, despite ASTIdentifier inheriting from
  // ASTExpression.
  if (field_ast_location->node_kind() != AST_PATH_EXPRESSION) {
    return MakeSqlErrorAt(field_ast_location)
           << "Found invalid argument for "
           << ast_function_call->function()->ToIdentifierPathString()
           << "() accessor. Input must be an identifier naming a valid "
              "field";
  }

  const ASTPathExpression* field_path =
      field_ast_location->GetAsOrDie<ASTPathExpression>();
  if (field_path->names().empty() ||
      (field_path->num_names() > 1 && !field_path->parenthesized())) {
    const absl::string_view error_message =
        field_extraction_type_or.value() == ProtoExtractionType::kOneofCase
            ? "A single non-parenthesized Oneof name is required as input to "
              "the ONEOF_CASE accessor"
            : "A valid top level field or parenthesized extension path is "
              "required";
    return MakeSqlErrorAt(field_path)
           << error_message << ", but found '"
           << field_path->ToIdentifierPathString() << "'";
  }

  return ResolveProtoExtractWithExtractTypeAndField(
      field_extraction_type_or.value(), field_path,
      std::move(resolved_proto_input), resolved_expr_out);
}

absl::Status Resolver::ResolveProtoExtractWithExtractTypeAndField(
    ProtoExtractionType field_extraction_type,
    const ASTPathExpression* field_path,
    std::unique_ptr<const ResolvedExpr> resolved_proto_input,
    std::unique_ptr<const ResolvedExpr>* resolved_expr_out) {
  MaybeResolveProtoFieldOptions top_level_field_options;
  ResolveExtensionFieldOptions extension_options;
  switch (field_extraction_type) {
    case ProtoExtractionType::kHas: {
      top_level_field_options.get_has_bit_override = true;
      extension_options.get_has_bit = true;
      top_level_field_options.ignore_format_annotations = false;
      extension_options.ignore_format_annotations = false;
      break;
    }
    case ProtoExtractionType::kField: {
      top_level_field_options.get_has_bit_override = false;
      extension_options.get_has_bit = false;
      top_level_field_options.ignore_format_annotations = false;
      extension_options.ignore_format_annotations = false;
      break;
    }
    case ProtoExtractionType::kRaw: {
      top_level_field_options.get_has_bit_override = false;
      top_level_field_options.ignore_format_annotations = true;
      extension_options.get_has_bit = false;
      extension_options.ignore_format_annotations = true;
      break;
    }
    case ProtoExtractionType::kOneofCase: {
      if (field_path->parenthesized()) {
        return MakeSqlErrorAt(field_path)
               << ProtoExtractionTypeName(field_extraction_type)
               << " requires input to be a non-parenthesized Oneof name, but "
                  "found '("
               << field_path->ToIdentifierPathString() << ")'";
      }
      return ResolveOneofCase(field_path->first_name(),
                              std::move(resolved_proto_input),
                              resolved_expr_out);
    }
    default:
      GOOGLESQL_RET_CHECK_FAIL() << "Invalid proto extraction type: "
                       << ProtoExtractionTypeName(field_extraction_type);
  }
  if (field_path->parenthesized()) {
    return ResolveExtensionFieldAccess(
        std::move(resolved_proto_input), extension_options, field_path,
        /*flatten_state=*/nullptr, resolved_expr_out);
  } else {
    GOOGLESQL_RET_CHECK_EQ(field_path->num_names(), 1)
        << "Non-parenthesized input to "
        << ProtoExtractionTypeName(field_extraction_type)
        << " must be a top level field, but found "
        << field_path->ToIdentifierPathString();
    return MaybeResolveProtoFieldAccess(
        field_path->location(), field_path->first_name(),
        top_level_field_options, std::move(resolved_proto_input),
        resolved_expr_out);
  }
}

absl::Status Resolver::ResolveNormalizeModeArgument(
    const ASTExpression* arg,
    std::unique_ptr<const ResolvedExpr>* resolved_expr_out) {
  // Resolve the normalize mode argument.  If it is a valid normalize mode,
  // creates a NormalizeMode enum ResolvedLiteral for it.
  if (arg->node_kind() != AST_PATH_EXPRESSION) {
    return MakeSqlErrorAt(arg) << "Argument is not a valid NORMALIZE mode";
  }
  const absl::Span<const ASTIdentifier* const>& names =
      arg->GetAsOrDie<ASTPathExpression>()->names();
  if (names.size() != 1) {
    return MakeSqlErrorAt(arg) << "Argument is not a valid NORMALIZE mode";
  }
  const std::string normalize_mode_name = names[0]->GetAsString();
  functions::NormalizeMode normalize_mode;
  if (!functions::NormalizeMode_Parse(
          absl::AsciiStrToUpper(normalize_mode_name), &normalize_mode)) {
    return MakeSqlErrorAt(arg) << "Argument is not a valid NORMALIZE mode: "
                               << ToIdentifierLiteral(normalize_mode_name);
  }

  const EnumType* normalize_mode_type;
  const google::protobuf::EnumDescriptor* normalize_mode_descr =
      functions::NormalizeMode_descriptor();
  GOOGLESQL_RET_CHECK_OK(
      type_factory_->MakeEnumType(normalize_mode_descr, &normalize_mode_type));
  *resolved_expr_out = MakeResolvedLiteralWithoutLocation(
      Value::Enum(normalize_mode_type, normalize_mode));
  return absl::OkStatus();
}

absl::Status Resolver::ResolveIntervalArgument(
    const ASTExpression* arg, ExprResolutionInfo* expr_resolution_info,
    std::vector<std::unique_ptr<const ResolvedExpr>>* resolved_arguments_out,
    std::vector<const ASTNode*>* ast_arguments_out) {
  if (arg->node_kind() != AST_INTERVAL_EXPR) {
    return MakeSqlErrorAt(arg) << "Expected INTERVAL expression";
  }
  const ASTIntervalExpr* interval_expr = arg->GetAsOrDie<ASTIntervalExpr>();

  // Resolve the INTERVAL value expression.
  const ASTExpression* interval_value_expr = interval_expr->interval_value();
  GOOGLESQL_RETURN_IF_ERROR(ResolveExpressionArgument(
      interval_value_expr, expr_resolution_info, resolved_arguments_out));
  ast_arguments_out->push_back(interval_value_expr);

  // Resolve the date part identifier and verify that it is a valid date part.
  const ASTIdentifier* interval_date_part_identifier =
      interval_expr->date_part_name();
  std::unique_ptr<const ResolvedExpr> resolved_date_part;
  GOOGLESQL_RETURN_IF_ERROR(ResolveDatePartArgument(interval_date_part_identifier,
                                          &resolved_date_part));

  // Optionally resolve the second date part identifier.
  if (interval_expr->date_part_name_to() != nullptr) {
    const ASTIdentifier* interval_date_part_identifier_to =
        interval_expr->date_part_name_to();
    // We verify that it is a valid date part to have good error message.
    std::unique_ptr<const ResolvedExpr> resolved_date_part_to;
    GOOGLESQL_RETURN_IF_ERROR(ResolveDatePartArgument(interval_date_part_identifier_to,
                                            &resolved_date_part_to));
    // But for backward compatibility with existing code, INTERVAL used as
    // argument to few date/time functions has to be single part.
    return MakeSqlErrorAt(arg)
           << "INTERVAL argument only support single date part field.";
  }

  GOOGLESQL_RET_CHECK(resolved_arguments_out->back() != nullptr);

  // Coerce the interval value argument to INT64 if necessary.
  if (!resolved_arguments_out->back()->type()->IsInt64()) {
    std::unique_ptr<const ResolvedExpr> resolved_interval_value_arg =
        std::move(resolved_arguments_out->back());
    resolved_arguments_out->pop_back();

    SignatureMatchResult result;
    GOOGLESQL_ASSIGN_OR_RETURN(
        InputArgumentType input_argument_type,
        GetInputArgumentTypeForExpr(
            resolved_interval_value_arg.get(),
            /*pick_default_type_for_untyped_expr=*/false, analyzer_options()));
    // Interval value must be either coercible to INT64 type, or be string
    // literal coercible to INT64 value.
    if (((resolved_interval_value_arg->node_kind() == RESOLVED_LITERAL ||
          resolved_interval_value_arg->node_kind() == RESOLVED_PARAMETER) &&
         resolved_interval_value_arg->type()->IsString()) ||
        coercer_.CoercesTo(input_argument_type, type_factory_->get_int64(),
                           /*is_explicit=*/false, &result)) {
      GOOGLESQL_RETURN_IF_ERROR(CoerceExprToType(
          interval_expr->interval_value(), type_factory_->get_int64(),
          kExplicitCoercion, &resolved_interval_value_arg));
      resolved_arguments_out->push_back(std::move(resolved_interval_value_arg));
    } else {
      return MakeSqlErrorAt(interval_expr->interval_value())
             << "Interval value must be coercible to INT64 type";
    }
  }

  resolved_arguments_out->push_back(std::move(resolved_date_part));

  ast_arguments_out->push_back(arg);
  return absl::OkStatus();
}

namespace {
// This is used to make a map from function name to a SpecialFunctionFamily
// enum that is used inside GetFunctionNameAndArguments to pick out functions
// that need special-case handling based on name.  Using the map avoids
// having several sequential calls to googlesql_base::StringCaseEqual, which is slow.
enum SpecialFunctionFamily {
  FAMILY_NONE,
  FAMILY_COUNT,
  FAMILY_ANON,
  FAMILY_ANON_BINARY,
  FAMILY_DATE_ADD,
  FAMILY_DATE_DIFF,
  FAMILY_DATE_TRUNC,
  FAMILY_STRING_NORMALIZE,
  FAMILY_GENERATE_CHRONO_ARRAY,
  FAMILY_BINARY_STATS,
  FAMILY_DIFFERENTIAL_PRIVACY,
  FAMILY_PROPERTY_EXISTS,
};

static constexpr absl::string_view kDifferentialPrivacyFunctionNames[] = {
    "count",
    "sum",
    "avg",
    "var_pop",
    "stddev_pop",
    "percentile_cont",
    "approx_quantiles",
    "approx_count_distinct"};
static constexpr absl::string_view kDifferentialPrivacyFunctionPrefix =
    "$differential_privacy_";

static IdStringHashMapCase<SpecialFunctionFamily>*
InitSpecialFunctionFamilyMap() {
  auto* out = new IdStringHashMapCase<SpecialFunctionFamily>;

  // COUNT(*) has special handling to erase the * argument.
  googlesql_base::InsertOrDie(out, IdString::MakeGlobal("count"), FAMILY_COUNT);

  // These expect argument 1 to be an INTERVAL.
  googlesql_base::InsertOrDie(out, IdString::MakeGlobal("date_add"), FAMILY_DATE_ADD);
  googlesql_base::InsertOrDie(out, IdString::MakeGlobal("date_sub"), FAMILY_DATE_ADD);
  googlesql_base::InsertOrDie(out, IdString::MakeGlobal("adddate"), FAMILY_DATE_ADD);
  googlesql_base::InsertOrDie(out, IdString::MakeGlobal("subdate"), FAMILY_DATE_ADD);
  googlesql_base::InsertOrDie(out, IdString::MakeGlobal("datetime_add"), FAMILY_DATE_ADD);
  googlesql_base::InsertOrDie(out, IdString::MakeGlobal("datetime_sub"), FAMILY_DATE_ADD);
  googlesql_base::InsertOrDie(out, IdString::MakeGlobal("time_add"), FAMILY_DATE_ADD);
  googlesql_base::InsertOrDie(out, IdString::MakeGlobal("time_sub"), FAMILY_DATE_ADD);
  googlesql_base::InsertOrDie(out, IdString::MakeGlobal("timestamp_add"), FAMILY_DATE_ADD);
  googlesql_base::InsertOrDie(out, IdString::MakeGlobal("timestamp_sub"), FAMILY_DATE_ADD);

  // These expect argument 2 to be a DATEPART.
  googlesql_base::InsertOrDie(out, IdString::MakeGlobal("date_diff"), FAMILY_DATE_DIFF);
  googlesql_base::InsertOrDie(out, IdString::MakeGlobal("datetime_diff"),
                   FAMILY_DATE_DIFF);
  googlesql_base::InsertOrDie(out, IdString::MakeGlobal("time_diff"), FAMILY_DATE_DIFF);
  googlesql_base::InsertOrDie(out, IdString::MakeGlobal("timestamp_diff"),
                   FAMILY_DATE_DIFF);

  // These expect argument 1 to be a DATEPART.
  googlesql_base::InsertOrDie(out, IdString::MakeGlobal("date_trunc"), FAMILY_DATE_TRUNC);
  googlesql_base::InsertOrDie(out, IdString::MakeGlobal("datetime_trunc"),
                   FAMILY_DATE_TRUNC);
  googlesql_base::InsertOrDie(out, IdString::MakeGlobal("time_trunc"), FAMILY_DATE_TRUNC);
  googlesql_base::InsertOrDie(out, IdString::MakeGlobal("timestamp_trunc"),
                   FAMILY_DATE_TRUNC);
  googlesql_base::InsertOrDie(out, IdString::MakeGlobal("last_day"), FAMILY_DATE_TRUNC);

  // These expect argument 1 to be a NORMALIZE_MODE.
  googlesql_base::InsertOrDie(out, IdString::MakeGlobal("normalize"),
                   FAMILY_STRING_NORMALIZE);
  googlesql_base::InsertOrDie(out, IdString::MakeGlobal("normalize_and_casefold"),
                   FAMILY_STRING_NORMALIZE);

  // These expect argument 2 to be an INTERVAL.
  googlesql_base::InsertOrDie(out, IdString::MakeGlobal("generate_date_array"),
                   FAMILY_GENERATE_CHRONO_ARRAY);
  googlesql_base::InsertOrDie(out, IdString::MakeGlobal("generate_timestamp_array"),
                   FAMILY_GENERATE_CHRONO_ARRAY);

  googlesql_base::InsertOrDie(out, IdString::MakeGlobal("anon_count"), FAMILY_ANON);
  googlesql_base::InsertOrDie(out, IdString::MakeGlobal("anon_sum"), FAMILY_ANON);
  googlesql_base::InsertOrDie(out, IdString::MakeGlobal("anon_avg"), FAMILY_ANON);
  googlesql_base::InsertOrDie(out, IdString::MakeGlobal("anon_var_pop"), FAMILY_ANON);
  googlesql_base::InsertOrDie(out, IdString::MakeGlobal("anon_stddev_pop"), FAMILY_ANON);
  googlesql_base::InsertOrDie(out, IdString::MakeGlobal("anon_percentile_cont"),
                   FAMILY_ANON_BINARY);
  googlesql_base::InsertOrDie(out, IdString::MakeGlobal("anon_quantiles"),
                   FAMILY_ANON_BINARY);

  // These don't support DISTINCT.
  googlesql_base::InsertOrDie(out, IdString::MakeGlobal("corr"), FAMILY_BINARY_STATS);
  googlesql_base::InsertOrDie(out, IdString::MakeGlobal("covar_pop"), FAMILY_BINARY_STATS);
  googlesql_base::InsertOrDie(out, IdString::MakeGlobal("covar_samp"),
                   FAMILY_BINARY_STATS);

  for (absl::string_view function : kDifferentialPrivacyFunctionNames) {
    googlesql_base::InsertOrDie(out,
                     IdString::MakeGlobal(absl::StrCat(
                         kDifferentialPrivacyFunctionPrefix, function)),
                     FAMILY_DIFFERENTIAL_PRIVACY);
  }

  googlesql_base::InsertOrDie(out, IdString::MakeGlobal("property_exists"),
                   FAMILY_PROPERTY_EXISTS);

  return out;
}

static IdStringHashMapCase<SpecialFunctionFamily>*
InitSpecialFunctionFamilyMapForDifferentialPrivacy() {
  IdStringHashMapCase<SpecialFunctionFamily>* out =
      InitSpecialFunctionFamilyMap();
  for (absl::string_view function : kDifferentialPrivacyFunctionNames) {
    out->erase(IdString::MakeGlobal(function));
    googlesql_base::InsertOrDie(out, IdString::MakeGlobal(function),
                     FAMILY_DIFFERENTIAL_PRIVACY);
  }
  return out;
}

static SpecialFunctionFamily GetSpecialFunctionFamily(
    const IdString& function_name, WithModifierMode with_modifier_mode) {
  static IdStringHashMapCase<SpecialFunctionFamily>* kSpecialFunctionFamilyMap =
      InitSpecialFunctionFamilyMap();
  // For differential privacy we replace count, sum etc. with DP versions of
  // these functions.
  static IdStringHashMapCase<SpecialFunctionFamily>*
      kSpecialFunctionFamilyMapForDifferentialPrivacy =
          InitSpecialFunctionFamilyMapForDifferentialPrivacy();

  if (with_modifier_mode == WithModifierMode::DIFFERENTIAL_PRIVACY) {
    return googlesql_base::FindWithDefault(
        *kSpecialFunctionFamilyMapForDifferentialPrivacy, function_name,
        FAMILY_NONE);
  }
  return googlesql_base::FindWithDefault(*kSpecialFunctionFamilyMap, function_name,
                              FAMILY_NONE);
}

}  // namespace

absl::Status Resolver::GetFunctionNameAndArgumentsForAnonFunctions(
    const ASTFunctionCall* function_call, bool is_binary_anon_function,
    std::vector<std::string>* function_name_path,
    std::vector<const ASTExpression*>* function_arguments,
    QueryResolutionInfo* query_resolution_info) {
  if (!language().LanguageFeatureEnabled(FEATURE_ANONYMIZATION)) {
    return absl::OkStatus();
  }

  if (query_resolution_info != nullptr) {
    switch (query_resolution_info->with_modifier_mode()) {
      case WithModifierMode::ANONYMIZATION:
        break;

      case WithModifierMode::NONE:
        query_resolution_info->set_with_modifier_mode(
            WithModifierMode::ANONYMIZATION);
        break;

      case WithModifierMode::AGGREGATION_THRESHOLD:
        return MakeSqlErrorAt(function_call)
               << "ANON functions are not allowed in SELECT WITH "
                  "AGGREGATION_THRESHOLD queries.";
        break;

      case WithModifierMode::DIFFERENTIAL_PRIVACY:
        return MakeSqlErrorAt(function_call)
               << "ANON functions are not allowed in SELECT WITH "
                  "DIFFERENTIAL_PRIVACY queries. Please use non ANON_ versions "
                  "of the functions";
        break;
    }
  }

  const ASTPathExpression* function = function_call->function();
  // The currently supported anonymous aggregate functions each require
  // exactly one 'normal argument' (two for ANON_PERCENTILE_CONT or
  // ANON_QUANTILES), along with an optional CLAMPED clause.  We
  // transform the CLAMPED clause into normal function call arguments
  // here. Note that after this transformation we cannot tell if the
  // function call arguments were originally derived from the CLAMPED
  // clause or not, so we won't be able to tell the following apart:
  //   ANON_SUM(x CLAMPED BETWEEN 1 AND 10)
  //   ANON_SUM(x, 1, 10)
  // Since the latter is invalid, we must detect that case here and
  // provide an error.
  const std::string upper_case_function_name =
      absl::AsciiStrToUpper(function->last_name()->GetAsString());
  if (is_binary_anon_function) {
    if (function_arguments->size() != 2) {
      size_t arg_size = function_arguments->size();
      return MakeSqlErrorAt(function_call)
             << "Anonymized aggregate function " << upper_case_function_name
             << " expects exactly 2 arguments but found " << arg_size
             << (arg_size == 1 ? " argument" : " arguments");
    }
  } else if (function_arguments->size() != 1) {
    return MakeSqlErrorAt(function_call)
           << "Anonymized aggregate function " << upper_case_function_name
           << " expects exactly 1 argument but found "
           << function_arguments->size() << " arguments";
  }
  // Convert the CLAMPED BETWEEN lower and upper bound to
  // normal function call arguments, appended after the first
  // argument.
  if (function_call->clamped_between_modifier() != nullptr) {
    function_arguments->emplace_back(
        function_call->clamped_between_modifier()->low());
    function_arguments->emplace_back(
        function_call->clamped_between_modifier()->high());
  }
  std::string function_name_lower = absl::AsciiStrToLower(
      function->last_name()->GetAsIdString().ToStringView());
  if (function_call->with_report_modifier() != nullptr) {
    if (function_name_lower == "anon_sum" ||
        function_name_lower == "anon_avg" ||
        function_name_lower == "anon_count" ||
        function_name_lower == "anon_quantiles") {
      // The catalog functions with report in different formats have the
      // names as follows:
      //   ANON_AVG, JSON        - "$anon_avg_with_report_json"
      //   ANON_AVG, PROTO       - "$anon_avg_with_report_proto"
      //   ANON_SUM, JSON        - "$anon_sum_with_report_json"
      //   ANON_SUM, PROTO       - "$anon_sum_with_report_proto"
      //   ANON_COUNT, JSON      - "$anon_count_with_report_json"
      //   ANON_COUNT, PROTO     - "$anon_count_with_report_proto"
      //   ANON_COUNT(*), JSON   - "$anon_count_star_with_report_json"
      //   ANON_COUNT(*), PROTO  - "$anon_count_star_with_report_proto"
      //   ANON_QUANTILES, JSON  - "$anon_quantiles_with_report_json"
      //   ANON_QUANTILES, PROTO - "$anon_quantiles_with_report_proto"
      std::vector<std::unique_ptr<const ResolvedOption>> resolved_options;
      std::string anon_function_report_format;
      GOOGLESQL_RETURN_IF_ERROR(ResolveAnonWithReportOptionsList(
          function_call->with_report_modifier()->options_list(),
          default_anon_function_report_format(), &resolved_options,
          &anon_function_report_format));

      if (anon_function_report_format.empty()) {
        return MakeSqlErrorAt(function_call)
               << "The anon function report format must be specified in the "
                  "function call in the WITH REPORT clause";
      }

      if (!function_call->arguments().empty() &&
          function_call->arguments()[0]->node_kind() == AST_STAR &&
          function_name_lower == "anon_count") {
        function_name_lower = "anon_count_star";
        function_arguments->erase(function_arguments->begin());
      }

      if (anon_function_report_format == "json") {
        function_name_path->back() =
            absl::StrCat("$", function_name_lower, "_with_report_json");
      } else if (anon_function_report_format == "proto") {
        function_name_path->back() =
            absl::StrCat("$", function_name_lower, "_with_report_proto");
      } else {
        // Verification of report format happens in
        // ResolveAnonWithReportOptionsList, so invalid formats are
        // unexpected here
        GOOGLESQL_RET_CHECK_FAIL() << "Invalid anon function report format "
                         << anon_function_report_format;
      }
    } else {
      return MakeUnimplementedErrorAtPoint(GetErrorLocationPoint(
                 function_call->with_report_modifier(), true))
             << "WITH REPORT is not supported yet for function "
             << absl::AsciiStrToUpper(function->name(0)->GetAsString());
    }
  } else if (function_name_lower == "anon_count" &&
             !function_call->arguments().empty() &&
             function_call->arguments()[0]->node_kind() == AST_STAR) {
    // Special case to look for ANON_COUNT(*).
    // The catalog function for ANON_COUNT(*) has the name
    // "$anon_count_star", and the signature is without the *
    // argument.
    function_name_path->back() = "$anon_count_star";
    function_arguments->erase(function_arguments->begin());
  }

  // We normally record that anonymization is present when an
  // AnonymizedAggregateScan is created, which is appropriate when
  // resolving queries.  However, when resolving expressions, we
  // also need to record that anonymization is present here since
  // the expression might include an anonymized aggregate function
  // call but no related resolved scan is created for it.
  analyzer_output_properties_.has_anonymization = true;
  return absl::OkStatus();
}

absl::Status Resolver::GetFunctionNameAndArgumentsForDPFunctions(
    const ASTFunctionCall* function_call,
    std::vector<std::string>* function_name_path,
    std::vector<const ASTExpression*>* function_arguments,
    QueryResolutionInfo* query_resolution_info) {
  if (!language().LanguageFeatureEnabled(FEATURE_DIFFERENTIAL_PRIVACY)) {
    return absl::OkStatus();
  }
  const ASTPathExpression* function = function_call->function();
  const std::string upper_case_function_name =
      absl::AsciiStrToUpper(function->last_name()->GetAsString());

  if (query_resolution_info == nullptr ||
      query_resolution_info->with_modifier_mode() !=
          WithModifierMode::DIFFERENTIAL_PRIVACY) {
    return MakeSqlErrorAt(function_call)
           << "Function not found: "
           << IdentifierPathToString(function->ToIdentifierVector());
  }
  const std::string function_name_lower = absl::AsciiStrToLower(
      function->last_name()->GetAsIdString().ToStringView());
  function_name_path->back() =
      absl::StrCat(kDifferentialPrivacyFunctionPrefix, function_name_lower);
  if (function_name_lower == "count" && !function_call->arguments().empty() &&
      function_call->arguments()[0]->node_kind() == AST_STAR) {
    absl::StrAppend(&function_name_path->back(), "_star");
    function_arguments->erase(function_arguments->begin());
  }
  return absl::OkStatus();
}

absl::Status Resolver::CheckChainedFunctionCall(
    const ASTFunctionCall* function_call) {
  if (function_call->is_chained_call()) {
    if (!language().LanguageFeatureEnabled(FEATURE_CHAINED_FUNCTION_CALLS)) {
      return MakeSqlErrorAt(function_call)
             << "Chained function calls are not supported";
    }
    analyzer_output_properties_.AddFeatureLabel(
        "SugarSyntax:CHAINED_FUNCTION_CALLS");

    // Check if we have any modifiers not allowed on chained calls.
    // Minus-one is for `is_chained_call`.
    int modifiers_left = function_call->NumModifiers() - 1;

    // These modifiers are not allowed, and have error messages.
    // The tests show that for each of these, if the relevant feature is not
    // enabled, we get the error for that missing feature first, so we don't
    // need to worry about mentioning unsupported features in these.
    if (function_call->group_by()) {
      // This is disallowed because it doesn't act like it evaluates
      // the function's base expression first.
      return MakeSqlErrorAt(function_call)
             << "Chained function call cannot use multi-level aggregation";
    }
    if (function_call->clamped_between_modifier()) {
      // This is disallowed because the parse only allows it on calls with
      // at least one argument. It would seem weird if
      // `(input).f(x CLAMPED BETWEEN y AND z)` worked but
      // `(input).g(CLAMPED BETWEEN y AND z)` did not.
      return MakeSqlErrorAt(function_call)
             << "Chained function call cannot use CLAMPED BETWEEN";
    }

    // These modifiers are allowed for chained calls.  Later code will check if
    // they are allowed on this specific function call (regardless of chained
    // syntax).
    // These modifiers are allowed because left-to-right evaluation (base
    // expression first) logically makes sense for these calls.  Modifiers that
    // involve unusual evaluation orders are disallowed in chained calls.
    if (function_call->distinct()) --modifiers_left;
    if (function_call->order_by()) --modifiers_left;
    if (function_call->limit_offset()) --modifiers_left;
    if (function_call->having_modifier()) --modifiers_left;
    if (function_call->with_report_modifier()) --modifiers_left;
    if (function_call->where_expr()) --modifiers_left;
    if (function_call->null_handling_modifier() !=
        ASTFunctionCall::DEFAULT_NULL_HANDLING) {
      --modifiers_left;
    }

    // If any other modifiers were present, give a generic error.
    if (modifiers_left != 0) {
      return MakeSqlErrorAt(function_call)
             << "Chained function call uses unsupported syntax feature";
    }
  }
  return absl::OkStatus();
}

absl::Status Resolver::GetFunctionNameAndArguments(
    const ASTFunctionCall* function_call,
    std::vector<std::string>* function_name_path,
    std::vector<const ASTExpression*>* function_arguments,
    std::map<int, SpecialArgumentType>* argument_option_map,
    QueryResolutionInfo* query_resolution_info) {
  const ASTPathExpression* function = function_call->function();
  *function_name_path = function->ToIdentifierVector();

  // Chained function calls like
  //    (base_expression).function_name(args)
  // work as usual since the base argument is already in `arguments[0]`.
  GOOGLESQL_RETURN_IF_ERROR(CheckChainedFunctionCall(function_call));

  function_arguments->insert(function_arguments->end(),
                             function_call->arguments().begin(),
                             function_call->arguments().end());

  if (function->num_names() != 1 &&
      !(function_name_path->size() == 2 &&
        googlesql_base::CaseEqual((*function_name_path)[0], "SAFE"))) {
    return absl::OkStatus();
  }

  const auto& function_family = GetSpecialFunctionFamily(
      function->last_name()->GetAsIdString(),
      (query_resolution_info == nullptr)
          ? WithModifierMode::NONE
          : query_resolution_info->with_modifier_mode());

  switch (function_family) {
    // A normal function with no special handling.
    case FAMILY_NONE:
      break;

    // Special case to look for COUNT(*).
    case FAMILY_COUNT:
      if (function_call->arguments().size() == 1 &&
          function_call->arguments()[0]->node_kind() == AST_STAR) {
        if (function_call->distinct()) {
          return MakeSqlErrorAt(function_call)
                 << "COUNT(*) cannot be used with DISTINCT";
        }

        // The catalog function for COUNT(*) has the name "$count_star" with
        // no arguments.
        function_name_path->back() = "$count_star";
        function_arguments->pop_back();
      }
      break;

    // Special case handling for differential privacy functions due to
    // CLAMPED BETWEEN syntax.
    // TODO: refactor so that the function name is first looked up
    // from the catalog, and then perform the special handling for special
    // functions. then change this code to also check
    // function->Is<AnonFunction>(), instead of checking for an 'anon_' prefix
    // in the function name.
    case FAMILY_ANON:
    case FAMILY_ANON_BINARY:
      return GetFunctionNameAndArgumentsForAnonFunctions(
          function_call, function_family == FAMILY_ANON_BINARY,
          function_name_path, function_arguments, query_resolution_info);
    case FAMILY_DIFFERENTIAL_PRIVACY:
      return GetFunctionNameAndArgumentsForDPFunctions(
          function_call, function_name_path, function_arguments,
          query_resolution_info);

    // Special case handling for DATE_ADD, DATE_SUB, DATE_TRUNC, and
    // DATE_DIFF due to the INTERVAL and AT TIME ZONE syntax, and date parts
    // represented as identifiers.  In these cases, we massage the element
    // list to conform to the defined function signatures.
    case FAMILY_DATE_ADD:
      googlesql_base::InsertOrDie(argument_option_map, 1, SpecialArgumentType::INTERVAL);
      break;
    case FAMILY_DATE_DIFF:
      googlesql_base::InsertOrDie(argument_option_map, 2, SpecialArgumentType::DATEPART);
      break;
    case FAMILY_DATE_TRUNC:
      googlesql_base::InsertOrDie(argument_option_map, 1, SpecialArgumentType::DATEPART);
      break;
    case FAMILY_STRING_NORMALIZE:
      googlesql_base::InsertOrDie(argument_option_map, 1,
                       SpecialArgumentType::NORMALIZE_MODE);
      break;
    case FAMILY_GENERATE_CHRONO_ARRAY:
      googlesql_base::InsertOrDie(argument_option_map, 2, SpecialArgumentType::INTERVAL);
      break;

    // Special case to disallow DISTINCT with binary stats.
    case FAMILY_BINARY_STATS:
      if (function_call->distinct()) {
        // TODO: function->name(0) probably does not work right
        // if this is a SAFE function call.  This should probably be
        // function->last_name() or function_name_path->back() instead.
        return MakeSqlErrorAt(function_call)
               << "DISTINCT is not allowed for function "
               << absl::AsciiStrToUpper(function->name(0)->GetAsString());
      }

      break;
    case FAMILY_PROPERTY_EXISTS:
      if (function_arguments->size() != 2) {
        return MakeSqlErrorAt(function_call)
               << "PROPERTY_EXISTS should have exactly two arguments";
      }
      googlesql_base::InsertOrDie(argument_option_map, 1,
                       SpecialArgumentType::PROPERTY_NAME);
      break;
  }

  return absl::OkStatus();
}

static bool IsLetterO(char c) { return c == 'o' || c == 'O'; }

// Returns function_name without a leading "SAFE_" prefix. If no safe prefix
// is present, returns function_name.
absl::string_view StripSafeCaseInsensitive(absl::string_view function_name) {
  if (!function_name.empty() &&
      (function_name[0] == 's' || function_name[0] == 'S') &&
      googlesql_base::CaseCompare(function_name.substr(0, 5),
                                           "SAFE_") == 0) {
    return function_name.substr(5);
  }
  return function_name;
}

static bool IsSpecialArrayContextFunction(absl::string_view function_name) {
  // We try to avoid doing the googlesql_base::StringCaseEqual calls as much as possible.
  if (!function_name.empty() && IsLetterO(function_name[0]) &&
      (googlesql_base::CaseCompare(function_name, "OFFSET") == 0 ||
       googlesql_base::CaseCompare(function_name, "ORDINAL") == 0)) {
    return true;
  }
  return false;
}

static bool IsSpecialMapContextFunction(absl::string_view function_name) {
  if (function_name.empty()) {
    return false;
  }
  // We try to avoid doing the googlesql_base::StringCaseEqual calls as much as possible.
  char first_letter = function_name[0];
  return (first_letter == 'k' || first_letter == 'K') &&
         googlesql_base::CaseCompare(function_name, "KEY") == 0;
}

absl::Status Resolver::ResolveSequence(
    const ASTPathExpression* path_expr,
    std::unique_ptr<const ResolvedSequence>* resolved_sequence) {
  const Sequence* sequence = nullptr;
  const absl::Status find_status =
      catalog_->FindSequence(path_expr->ToIdentifierVector(), &sequence,
                             analyzer_options_.find_options());

  if (find_status.code() == absl::StatusCode::kNotFound) {
    std::string error_message;
    absl::StrAppend(&error_message, "Sequence not found: ",
                    path_expr->ToIdentifierPathString());
    const std::string sequence_suggestion =
        catalog_->SuggestSequence(path_expr->ToIdentifierVector());
    if (!sequence_suggestion.empty()) {
      absl::StrAppend(&error_message, "; Did you mean ", sequence_suggestion,
                      "?");
    }
    return MakeSqlErrorAt(path_expr) << error_message;
  }
  GOOGLESQL_RETURN_IF_ERROR(find_status);
  std::unique_ptr<ResolvedSequence> mutable_resolved_sequence =
      MakeResolvedSequence(sequence);
  MaybeRecordParseLocation(path_expr, mutable_resolved_sequence.get());
  *resolved_sequence = std::move(mutable_resolved_sequence);
  return absl::OkStatus();
}

absl::Status Resolver::ResolveLambda(
    const ASTLambda* ast_lambda, absl::Span<const IdString> arg_names,
    absl::Span<const Type* const> arg_types, const Type* body_result_type,
    bool allow_argument_coercion, const NameScope* name_scope,
    std::unique_ptr<const ResolvedInlineLambda>* resolved_expr_out) {
  std::vector<AnnotatedType> annotated_arg_types;
  annotated_arg_types.reserve(arg_types.size());
  for (const Type* arg_type : arg_types) {
    annotated_arg_types.push_back(
        AnnotatedType(arg_type, /*annotation_map=*/nullptr));
  }
  return ResolveLambdaWithAnnotations(
      ast_lambda, arg_names, annotated_arg_types, body_result_type,
      allow_argument_coercion, name_scope, resolved_expr_out);
}

absl::Status Resolver::ResolveLambdaWithAnnotations(
    const ASTLambda* ast_lambda, absl::Span<const IdString> arg_names,
    absl::Span<const AnnotatedType> arg_types, const Type* body_result_type,
    bool allow_argument_coercion, const NameScope* name_scope,
    std::unique_ptr<const ResolvedInlineLambda>* resolved_expr_out) {
  static constexpr char kLambda[] = "Lambda";
  // Every argument should have a corresponding type.
  GOOGLESQL_RET_CHECK_EQ(arg_names.size(), arg_types.size());

  // Build a NameList from lambda arguments.
  std::vector<ResolvedColumn> arg_columns;
  arg_columns.reserve(arg_names.size());
  std::shared_ptr<NameList> args_name_list = std::make_shared<NameList>();
  for (int i = 0; i < arg_names.size(); i++) {
    GOOGLESQL_RET_CHECK(arg_types[i].annotation_map == nullptr ||
              !allow_argument_coercion);
    IdString arg_name = arg_names[i];
    const ResolvedColumn arg_column(AllocateColumnId(),
                                    /*table_name=*/kLambdaArgId, arg_name,
                                    arg_types[i]);
    GOOGLESQL_RETURN_IF_ERROR(
        args_name_list->AddColumn(arg_name, arg_column, /*is_explicit=*/true));
    arg_columns.push_back(arg_column);

    // We need to record access to the column representing lambda argument,
    // otherwise analyzer will further remove it for the list of used columns as
    // unreferenced.
    RecordColumnAccess(arg_column);
  }

  // Combine argument names and function call site name scope to create a name
  // scope for lambda body. This enables the lambda body to access columns in
  // addition to lambda arguments.
  CorrelatedColumnsSet correlated_columns_set;
  auto body_name_scope = std::make_unique<NameScope>(name_scope, args_name_list,
                                                     &correlated_columns_set);

  // Resolve the body.
  std::unique_ptr<const ResolvedExpr> resolved_body;
  GOOGLESQL_RETURN_IF_ERROR(ResolveScalarExpr(ast_lambda->body(), body_name_scope.get(),
                                    kLambda, &resolved_body));

  // If <body_result_type> is set, the body expr is expected to have
  // specific type.
  if (body_result_type != nullptr && allow_argument_coercion) {
    GOOGLESQL_RETURN_IF_ERROR(CoerceExprToType(
        ast_lambda->body(), body_result_type, kImplicitCoercion,
        "Lambda should return type $0, but returns $1", &resolved_body));
  }

  // Gather the correlated column references: columns other than the lambda
  // arguments.
  std::vector<std::unique_ptr<const ResolvedColumnRef>> parameter_list;
  FetchCorrelatedSubqueryParameters(correlated_columns_set, &parameter_list);

  *resolved_expr_out = MakeResolvedInlineLambda(
      arg_columns, std::move(parameter_list), std::move(resolved_body));

  return absl::OkStatus();
}

namespace {

bool IsGroupingFunction(const Function* function) {
  return function->NumSignatures() == 1 &&
         function->signatures()[0].context_id() == FN_GROUPING &&
         function->IsGoogleSQLBuiltin();
}

bool IsMatchRecognizeMeasuresFunction(const Function* function) {
  if (function->NumSignatures() != 1 || !function->IsGoogleSQLBuiltin()) {
    return false;
  }
  switch (function->signatures()[0].context_id()) {
    case FN_FIRST_AGG:
    case FN_LAST_AGG:
    case FN_MATCH_NUMBER:
    case FN_MATCH_ROW_NUMBER:
    case FN_CLASSIFIER:
      return true;
    default:
      return false;
  }
}

bool IsMatchRecognizePhysicalNavigationFunction(const Function* function) {
  if (function->NumSignatures() != 1 || !function->IsGoogleSQLBuiltin()) {
    return false;
  }
  switch (function->signatures()[0].context_id()) {
    case FN_NEXT:
    case FN_PREV:
      return true;
    default:
      return false;
  }
}

absl::Status CheckNodeIsNull(const ASTNode* node,
                             absl::string_view error_message) {
  if (node != nullptr) {
    return MakeSqlErrorAt(node) << error_message;
  }
  return absl::OkStatus();
}

// Return an error if `ast_function` contains a clause that is not supported
// in horizontal aggregation. Return OK otherwise.
absl::Status ValidateAggregateFunctionClausesForHorizontalAggregation(
    const ASTFunctionCall* ast_function,
    const ExprResolutionInfo* expr_resolution_info) {
  if (!expr_resolution_info->allows_horizontal_aggregation) {
    return absl::OkStatus();
  }
  if (ast_function->group_by() != nullptr) {
    return MakeSqlErrorAt(ast_function->group_by())
           << "Horizontal aggregates do not support multi-level aggregation";
  }
  if (ast_function->where_expr() != nullptr) {
    return MakeSqlErrorAt(ast_function->where_expr())
           << "Horizontal aggregates do not support WHERE clause aggregate "
              "filtering";
  }
  if (ast_function->having_expr() != nullptr) {
    return MakeSqlErrorAt(ast_function->where_expr())
           << "Horizontal aggregates do not support HAVING clause aggregate "
              "filtering";
  }
  return absl::OkStatus();
}

absl::Status ValidatePotentialMultiLevelAggregate(
    const ASTFunctionCall* ast_function,
    const ExprResolutionInfo* expr_resolution_info,
    const LanguageOptions& language_options) {
  const ASTGroupBy* group_by = ast_function->group_by();
  if (group_by == nullptr) {
    return absl::OkStatus();
  }
  if (ast_function->having_modifier() != nullptr) {
    return MakeSqlErrorAt(group_by)
           << "GROUP BY cannot be used with a HAVING MAX/MIN modifier.";
  }
  if (group_by->grouping_items().empty()) {
    return MakeSqlErrorAt(group_by)
           << "GROUP BY modifiers list cannot be empty.";
  }
  for (const ASTGroupingItem* group_by_modifier : group_by->grouping_items()) {
    if (group_by_modifier == nullptr) {
      return MakeSqlErrorAt(group_by_modifier)
             << "GROUP BY modifier cannot be null.";
    }
    GOOGLESQL_RETURN_IF_ERROR(CheckNodeIsNull(
        group_by_modifier->rollup(),
        "GROUP BY ROLLUP is not supported inside an aggregate function."));
    GOOGLESQL_RETURN_IF_ERROR(CheckNodeIsNull(
        group_by_modifier->cube(),
        "GROUP BY CUBE is not supported inside an aggregate function."));
    GOOGLESQL_RETURN_IF_ERROR(CheckNodeIsNull(group_by_modifier->grouping_set_list(),
                                    "GROUP BY GROUPING SETS is not supported "
                                    "inside an aggregate function."));
    GOOGLESQL_RETURN_IF_ERROR(CheckNodeIsNull(group_by_modifier->grouping_item_order(),
                                    "GROUP BY does not support order "
                                    "specification outside pipe AGGREGATE"));
    GOOGLESQL_RETURN_IF_ERROR(CheckNodeIsNull(
        group_by_modifier->alias(),
        "GROUP BY expressions in an aggregate function cannot have aliases."));
    if (group_by_modifier->expression() == nullptr) {
      return MakeSqlErrorAt(group_by_modifier)
             << "GROUP BY () is not supported inside an aggregate function.";
    }
  }
  GOOGLESQL_RETURN_IF_ERROR(CheckNodeIsNull(
      group_by->all(),
      "GROUP BY ALL is not supported inside an aggregate function."));
  if (group_by->and_order_by()) {
    return MakeSqlErrorAt(group_by)
           << "GROUP AND ORDER BY is not supported outside pipe AGGREGATE.";
  }
  return absl::OkStatus();
}

absl::Status ValidateMeasureTypeAggregateFunction(
    const ASTFunctionCall* ast_function, const Function* function,
    const ExprResolutionInfo* expr_resolution_info) {
  if (!IsMeasureAggFunction(function)) {
    return absl::OkStatus();
  }
  const QueryResolutionInfo* query_resolution_info =
      expr_resolution_info->query_resolution_info;
  if (query_resolution_info != nullptr &&
      query_resolution_info->is_nested_aggregation()) {
    return MakeSqlErrorAt(ast_function)
           << function->Name()
           << " cannot be nested inside another aggregate function.";
  }
  return absl::OkStatus();
}

}  // namespace

absl::Status Resolver::ResolveGroupingItem(
    const ASTGroupingItem* group_by_modifier,
    ExprResolutionInfo* expr_resolution_info,
    std::unique_ptr<const ResolvedExpr>* resolved_group_by) {
  GOOGLESQL_RET_CHECK_NE(group_by_modifier, nullptr);
  GOOGLESQL_RET_CHECK_NE(group_by_modifier->expression(), nullptr);
  GOOGLESQL_RET_CHECK_EQ(group_by_modifier->rollup(), nullptr);
  GOOGLESQL_RET_CHECK_EQ(group_by_modifier->cube(), nullptr);
  GOOGLESQL_RET_CHECK_EQ(group_by_modifier->grouping_set_list(), nullptr);
  GOOGLESQL_RET_CHECK_EQ(group_by_modifier->grouping_item_order(), nullptr);
  GOOGLESQL_RET_CHECK_EQ(group_by_modifier->alias(), nullptr);
  std::unique_ptr<const ResolvedExpr> resolved_group_by_expr;
  GOOGLESQL_RETURN_IF_ERROR(ResolveExpr(group_by_modifier->expression(),
                              expr_resolution_info, &resolved_group_by_expr));

  std::string type_description;
  if (!resolved_group_by_expr->type()->SupportsGrouping(language(),
                                                        &type_description)) {
    return MakeSqlErrorAt(group_by_modifier)
           << "GROUP BY modifier has type " << type_description
           << ", which is not groupable.";
  }

  // TODO: Refactor ordinal checking logic into a shared method.
  if (resolved_group_by_expr->node_kind() == RESOLVED_LITERAL &&
      !resolved_group_by_expr->GetAs<ResolvedLiteral>()->has_explicit_type()) {
    const Value& value =
        resolved_group_by_expr->GetAs<ResolvedLiteral>()->value();
    if (value.type_kind() == TYPE_INT64 && !value.is_null()) {
      return MakeSqlErrorAt(group_by_modifier)
             << "GROUP BY modifiers cannot specify ordinals.";
    } else {
      return MakeSqlErrorAt(group_by_modifier)
             << "GROUP BY modifiers cannot be literal values.";
    }
  }

  *resolved_group_by = std::move(resolved_group_by_expr);
  return absl::OkStatus();
}

absl::Status Resolver::ResolveAggregateFunctionCallFirstPass(
    const ASTFunctionCall* ast_function, const Function* function,
    ResolvedFunctionCallBase::ErrorMode error_mode,
    const std::vector<const ASTExpression*>& function_arguments,
    const std::map<int, SpecialArgumentType>& argument_option_map,
    ExprResolutionInfo* expr_resolution_info,
    std::unique_ptr<const ResolvedExpr>* resolved_expr_out) {
  GOOGLESQL_RETURN_IF_ERROR(ValidateAggregateFunctionClausesForHorizontalAggregation(
      ast_function, expr_resolution_info));
  GOOGLESQL_RETURN_IF_ERROR(ValidatePotentialMultiLevelAggregate(
      ast_function, expr_resolution_info, language()));
  GOOGLESQL_RETURN_IF_ERROR(ValidateMeasureTypeAggregateFunction(ast_function, function,
                                                       expr_resolution_info));
  std::unique_ptr<ExprResolutionInfo> local_expr_resolution_info;
  std::vector<std::unique_ptr<const ResolvedColumnRef>> correlated_columns;

  // Normal case, do initial function call resolution.
  local_expr_resolution_info = std::make_unique<ExprResolutionInfo>(
      expr_resolution_info,
      ExprResolutionInfoOptions{
          .name_scope = expr_resolution_info->aggregate_name_scope,
          // When resolving arguments of aggregation functions, we resolve
          // against pre-grouped versions of columns only.
          .use_post_grouping_columns = false});
  local_expr_resolution_info->grouping_context =
      ExprGroupingContext::kPreGrouping;

  if (expr_resolution_info->allows_horizontal_aggregation) {
    if (local_expr_resolution_info->in_horizontal_aggregation) {
      return MakeSqlErrorAt(ast_function)
             << "Horizontal aggregations of horizontal aggregations are not "
                "allowed";
    }
    local_expr_resolution_info->in_horizontal_aggregation = true;
    GOOGLESQL_RET_CHECK(!expr_resolution_info->horizontal_aggregation_info.has_value());
  }

  return ResolveFunctionCallImpl(
      ast_function, function, error_mode, function_arguments,
      argument_option_map, local_expr_resolution_info.get(),
      resolved_expr_out);
}

// Add the resolved_agg_function_call to the aggregate expression map.
// The aggregate_expr_map is used in the ResolveSelectColumnSecondPass
// when determining if we need to re-resolve the select column or not.
// This is custom aggregation function logic for the GROUPING aggregate
// function.
absl::Status Resolver::AddColumnToGroupingListFirstPass(
    const ASTFunctionCall* ast_function,
    std::unique_ptr<const ResolvedAggregateFunctionCall> agg_function_call,
    ExprResolutionInfo* expr_resolution_info,
    std::unique_ptr<ResolvedColumn>* resolved_column_out) {
  if (agg_function_call->argument_list().size() != 1) {
    return MakeSqlErrorAt(ast_function)
           << "GROUPING can only have a single expression argument.";
  }

  ResolvedColumn grouping_column = MakeGroupingOutputColumn(
      expr_resolution_info, kGroupingId, agg_function_call->annotated_type());
  *resolved_column_out = std::make_unique<ResolvedColumn>(grouping_column);

  // Check if we have group by columns resolved already, if we do, we are
  // resolving the grouping function call for QUALIFY, ORDER BY, or HAVING,
  // which means we can auto call the secondPass function and add the GROUPING
  // function call to the grouping call list.
  if (!expr_resolution_info->query_resolution_info->group_by_column_state_list()
           .empty()) {
    return AddColumnToGroupingListSecondPass(
        ast_function, agg_function_call.get(), expr_resolution_info,
        resolved_column_out);
  }

  GOOGLESQL_RETURN_IF_ERROR(
      expr_resolution_info->query_resolution_info->AddGroupingColumnToExprMap(
          ast_function, MakeResolvedComputedColumn(
                            grouping_column, std::move(agg_function_call))));
  return absl::OkStatus();
}

// Scan the group_by_columns_to_compute to find the expression that matches
// the GROUPING argument expression. With the matching expression, create
// a ResolvedGroupingCall with a tuple of the group_by_column referenced
// as the GROUPING argument, and the output column representing the
// output of the GROUPING function call and add it to the grouping_call_list.
// If there is no matching group_by_columns_to_compute for the GROUPING
// argument, throw an error. This is required to be valid.
absl::Status Resolver::AddColumnToGroupingListSecondPass(
    const ASTFunctionCall* ast_function,
    const ResolvedAggregateFunctionCall* agg_function_call,
    ExprResolutionInfo* expr_resolution_info,
    std::unique_ptr<ResolvedColumn>* resolved_column_out) {
  QueryResolutionInfo* query_resolution_info =
      expr_resolution_info->query_resolution_info;
  if (agg_function_call->argument_list().size() != 1) {
    return MakeSqlErrorAt(ast_function)
           << "GROUPING can only have a single expression argument.";
  }
  if (query_resolution_info->with_modifier_mode() != WithModifierMode::NONE) {
    return MakeUnsupportedGroupingFunctionError(
        ast_function, query_resolution_info->with_modifier_mode());
  }
  const ResolvedExpr* argument =
      agg_function_call->argument_list().front().get();
  for (const GroupByColumnState& group_by_column_state :
       query_resolution_info->group_by_column_state_list()) {
    // When the select item has an alias, the post-group-by expression is a
    // a column reference of the pre-group-by computed column. For example:
    //
    // SELECT key+1 AS a, GROUPING(key+1)
    // FROM KeyValue
    // GROUP BY 1
    // ORDER BY 1
    //
    // The post-group-by expression is a column reference of `a`, while the
    // original pre-group-by expression is `key+1`.
    //
    // When the select item doesn't have an alias, both pre-group-by and
    // post-group-by expression are same. For example:
    //
    // SELECT key+1, GROUPING(key+1)
    // FROM KeyValue
    // GROUP BY 1
    // ORDER BY 1
    //
    // Both pre-group-by and post-group-by expressions are `key+1`.
    //
    // We need to check both expressions to match the GROUPING argument, as the
    // argument can be either the pre-group-by expression or the post-group-by
    // expression. For example, two grouping functions in the following query
    // have the same argument in different format, they are both valid.
    //
    // SELECT key+1 AS a, GROUPING(key+1)
    // FROM KeyValue
    // GROUP BY 1
    // ORDER BY GROUPING(a)
    GOOGLESQL_ASSIGN_OR_RETURN(
        bool expression_match,
        IsSameExpressionForGroupBy(
            argument, group_by_column_state.computed_column->expr(),
            language()));
    if (!expression_match &&
        group_by_column_state.pre_group_by_expr != nullptr) {
      GOOGLESQL_ASSIGN_OR_RETURN(
          expression_match,
          IsSameExpressionForGroupBy(
              argument, group_by_column_state.pre_group_by_expr, language()));
    }

    if (expression_match) {
      // Output column should always have INT64 type for GROUPING function.
      ResolvedColumn grouping_output_column =
          MakeGroupingOutputColumn(expr_resolution_info, kGroupingId,
                                   AnnotatedType(googlesql::types::Int64Type(),
                                                 /*annotation_map=*/nullptr));
      std::unique_ptr<ResolvedColumnRef> grouping_argument =
          MakeColumnRef(group_by_column_state.computed_column->column());

      std::unique_ptr<ResolvedGroupingCall> grouping_call =
          MakeResolvedGroupingCall(std::move(grouping_argument),
                                   grouping_output_column);
      *resolved_column_out =
          std::make_unique<ResolvedColumn>(grouping_output_column);

      query_resolution_info->AddGroupingColumn(std::move(grouping_call));
      return absl::OkStatus();
    }
  }

  return MakeSqlErrorAt(ast_function)
         << "GROUPING must have an argument that exists within the group-by "
            "expression list.";
}

static bool MultiLevelAggregationPresent(const ASTFunctionCall* ast_function) {
  return ast_function != nullptr && ast_function->group_by() != nullptr;
}

static absl::Status CheckForMultiLevelAggregationSupport(
    const ASTFunctionCall* ast_function,
    const LanguageOptions& language_options) {
  if (MultiLevelAggregationPresent(ast_function) &&
      !language_options.LanguageFeatureEnabled(
          FEATURE_MULTILEVEL_AGGREGATION)) {
    return MakeSqlErrorAt(ast_function->group_by())
           << "Multi-level aggregation is not yet supported.";
  }
  return absl::OkStatus();
}

ABSL_ATTRIBUTE_NOINLINE
absl::Status Resolver::TryMakeErrorSuggestionForChainedCallImpl(
    const absl::Status& orig_status, const ASTFunctionCall* ast_function_call,
    absl::Span<const std::string> alt_function_name,
    absl::Span<const std::string> alt_prefix_path) {
  // Suppress suggestions for paths ending with SAFE, since those
  // probably only make sense as part of a function name, and we don't want
  // to given suggestions about using SAFE on functions that don't support SAFE.
  if (!alt_prefix_path.empty() &&
      googlesql_base::CaseEqual(alt_prefix_path.back(), "SAFE")) {
    return absl::OkStatus();
  }

  // Strip SAFE off the path to look up so we can find SAFE functions.
  absl::Span<const std::string> alt_lookup_name = alt_function_name;
  if (alt_lookup_name.size() > 1 &&
      googlesql_base::CaseEqual(alt_lookup_name[0], "SAFE")) {
    alt_lookup_name = alt_lookup_name.subspan(1);
  }

  const Function* function;
  ResolvedFunctionCallBase::ErrorMode error_mode;
  const absl::Status alt_lookup_status = LookupFunctionFromCatalog(
      ast_function_call, alt_lookup_name,
      FunctionNotFoundHandleMode::kReturnError, &function, &error_mode);

  // The alternate name isn't a function, so we have no error suggestion to
  // return.
  if (!alt_lookup_status.ok()) {
    return absl::OkStatus();
  }

  // The alternate name would be a valid function.
  // Return the original error, but with a suggestion to add parentheses.

  std::string alt_prefix;
  if (!alt_prefix_path.empty()) {
    // Parentheses go on the input path if not added on the function name.
    bool parens = alt_function_name.size() == 1;
    alt_prefix =
        absl::StrCat(parens ? "(" : "", IdentifierPathToString(alt_prefix_path),
                     parens ? ")" : "");
  }

  if (alt_function_name.size() == 1) {
    return googlesql_base::StatusBuilder(orig_status)
           << "Chained function calls require parentheses on input paths; "
              "Did you mean "
           << alt_prefix << "." << IdentifierPathToString(alt_function_name)
           << "(...)?";
  } else {
    return googlesql_base::StatusBuilder(orig_status)
           << "Calling multi-part function names in chained call syntax "
              "requires parentheses; Did you mean "
           << alt_prefix << ".(" << IdentifierPathToString(alt_function_name)
           << ")(...)?";
  }
}

ABSL_ATTRIBUTE_NOINLINE
absl::Status Resolver::TryMakeErrorSuggestionForChainedCall(
    const absl::Status& orig_status, const ASTFunctionCall* ast_function_call) {
  RETURN_ERROR_IF_OUT_OF_STACK_SPACE();

  if (!orig_status.ok() && ast_function_call->is_chained_call()) {
    GOOGLESQL_RET_CHECK(!ast_function_call->arguments().empty());
    const ASTExpression* base_expr = ast_function_call->arguments().front();
    // If we have an ASTDotIdentifier base expression for a chained function
    // call, like `(expr).identifier.function()`, see if `identifier.function`
    // is a valid function.  If it is, suggest `.(identifier.function)()`.
    if (base_expr->node_kind() == AST_DOT_IDENTIFIER &&
        !base_expr->parenthesized() &&
        ast_function_call->function()->num_names() == 1) {
      const std::vector<std::string> alt_function_name = {
          base_expr->GetAsOrDie<ASTDotIdentifier>()->name()->GetAsString(),
          ast_function_call->function()->first_name()->GetAsString()};

      GOOGLESQL_RETURN_IF_ERROR(TryMakeErrorSuggestionForChainedCallImpl(
          orig_status, ast_function_call, alt_function_name,
          /*alt_prefix_path=*/{}));
    }
  }

  // Note: This returns OK if it didn't modify the error message.
  // The caller must still handle the original error.
  return absl::OkStatus();
}

absl::Status Resolver::LookupFunctionFromCatalogWithChainedCallErrors(
    const ASTFunctionCall* ast_function_call,
    absl::Span<const std::string> function_name_path,
    FunctionNotFoundHandleMode handle_mode,
    ExprResolutionInfo* expr_resolution_info, const Function** function,
    ResolvedFunctionCallBase::ErrorMode* error_mode) {
  RETURN_ERROR_IF_OUT_OF_STACK_SPACE();

  const absl::Status lookup_status = LookupFunctionFromCatalog(
      ast_function_call, function_name_path,
      FunctionNotFoundHandleMode::kReturnError, function, error_mode);

  // If we have a chained call and we're going to give a function-not-found
  // error, see if we can give a better error.

  // For non-chained calls on a path, maybe the path was supposed to be
  // partly a function name: `path.function()` could be `(path).function()`.
  if (!lookup_status.ok() && !ast_function_call->is_chained_call() &&
      function_name_path.size() >= 2 &&
      language().LanguageFeatureEnabled(FEATURE_CHAINED_FUNCTION_CALLS)) {
    // Try all suffixes of the path as function names, in increasing size order.
    // This handles functions with >2 names, but the other cases below don't.
    for (size_t name_start = function_name_path.size() - 1; name_start >= 1;
         --name_start) {
      GOOGLESQL_RETURN_IF_ERROR(TryMakeErrorSuggestionForChainedCallImpl(
          lookup_status, ast_function_call,
          function_name_path.subspan(name_start),
          function_name_path.subspan(0, name_start)));
    }
  }

  // For chained calls, consider interpretations of the base expression.
  if (!lookup_status.ok() && ast_function_call->is_chained_call()) {
    // If the base expression was an ASTDotIdentifier, see if we can give
    // an error suggesting replacing `(expr).catalog.func()` with
    // `(expr).(catalog.func)()`.
    GOOGLESQL_RETURN_IF_ERROR(
        TryMakeErrorSuggestionForChainedCall(lookup_status, ast_function_call));

    // Try resolving the input expression, and if we find an error, return it.
    // The input expression came first so we prefer giving its error.
    //
    // If there's no error there, discard the result and just return the
    // `lookup_status` error below.
    GOOGLESQL_RET_CHECK(!ast_function_call->arguments().empty());
    std::unique_ptr<const ResolvedExpr> resolved_expr;
    GOOGLESQL_RETURN_IF_ERROR(ResolveExpr(ast_function_call->arguments().front(),
                                expr_resolution_info, &resolved_expr));
  }

  return lookup_status;
}

// TODO: The noinline attribute is to prevent the stack usage
// being added to its caller "Resolver::ResolveExpr" which is a recursive
// function. Now the attribute has to be added for all callees. Hopefully
// the fix allows replacing these with one attribute on ResolveExpr.
ABSL_ATTRIBUTE_NOINLINE
absl::Status Resolver::ResolveFunctionCall(
    const ASTFunctionCall* ast_function,
    ExprResolutionInfo* expr_resolution_info,
    std::unique_ptr<const ResolvedExpr>* resolved_expr_out) {
  RETURN_ERROR_IF_OUT_OF_STACK_SPACE();
  GOOGLESQL_RETURN_IF_ERROR(
      CheckForMultiLevelAggregationSupport(ast_function, language()));

  std::vector<std::string> function_name_path;
  std::vector<const ASTExpression*> function_arguments;
  std::map<int, SpecialArgumentType> argument_option_map;
  if (expr_resolution_info->use_post_grouping_columns) {
    // If `use_post_grouping_columns` is true, we have already resolved
    // aggregate function calls for SELECT list aggregate expressions. Second
    // pass aggregate function resolution should look up these resolved
    // aggregate function calls in the map.
    GOOGLESQL_RET_CHECK(expr_resolution_info->query_resolution_info != nullptr);
    const ResolvedComputedColumnBase* computed_aggregate_column =
        googlesql_base::FindPtrOrNull(
            expr_resolution_info->query_resolution_info->aggregate_expr_map(),
            ast_function);
    if (computed_aggregate_column != nullptr) {
      // Check the edge case for GROUPING function, which requires us to add
      // a post group_by ResolvedColumn to the grouping_call_list within the
      // aggregate scan.
      if (computed_aggregate_column->expr()
              ->Is<ResolvedAggregateFunctionCall>()) {
        const ResolvedAggregateFunctionCall* agg_function_call =
            computed_aggregate_column->expr()
                ->GetAs<ResolvedAggregateFunctionCall>();
        if (IsGroupingFunction(agg_function_call->function())) {
          std::unique_ptr<ResolvedColumn> resolved_grouping_column;
          GOOGLESQL_RETURN_IF_ERROR(AddColumnToGroupingListSecondPass(
              ast_function, agg_function_call, expr_resolution_info,
              &resolved_grouping_column));
          *resolved_expr_out = MakeColumnRef(*resolved_grouping_column);
          return absl::OkStatus();
        }
      }

      *resolved_expr_out = MakeColumnRef(computed_aggregate_column->column());
      if (computed_aggregate_column->Is<ResolvedDeferredComputedColumn>()) {
        GOOGLESQL_ASSIGN_OR_RETURN(
            *resolved_expr_out,
            WrapInASideEffectCall(ast_function, std::move(*resolved_expr_out),
                                  computed_aggregate_column
                                      ->GetAs<ResolvedDeferredComputedColumn>()
                                      ->side_effect_column(),
                                  *expr_resolution_info));
      }

      return absl::OkStatus();
    }
    // If we do not find them in the map, then fall through
    // and resolve the aggregate function call normally.  This is required
    // when the aggregate function appears in the HAVING or ORDER BY.
  }

  // Get the catalog function name and arguments that may be different from
  // what are specified in the input query.
  GOOGLESQL_RETURN_IF_ERROR(GetFunctionNameAndArguments(
      ast_function, &function_name_path, &function_arguments,
      &argument_option_map, expr_resolution_info->query_resolution_info));

  // Special case for "OFFSET", "ORDINAL", and "KEY" functions, which are really
  // just special wrappers allowed inside array element access syntax only.
  if (function_name_path.size() == 1) {
    const absl::string_view function_name = function_name_path[0];
    const absl::string_view function_name_without_safe =
        StripSafeCaseInsensitive(function_name);
    const bool is_array_context_fn =
        IsSpecialArrayContextFunction(function_name_without_safe);
    const bool is_map_context_fn =
        IsSpecialMapContextFunction(function_name_without_safe);
    if (is_array_context_fn || is_map_context_fn) {
      absl::string_view type = is_map_context_fn ? "map" : "array";
      absl::string_view arg_name = is_map_context_fn ? "key" : "position";
      return MakeSqlErrorAt(ast_function)
             << absl::AsciiStrToUpper(function_name)
             << " is not a function. It can only be used for " << type
             << " element access using " << type << "["
             << absl::AsciiStrToUpper(function_name) << "(" << arg_name << ")]";
    }
  }

  const Function* function;
  ResolvedFunctionCallBase::ErrorMode error_mode;
  GOOGLESQL_RETURN_IF_ERROR(LookupFunctionFromCatalogWithChainedCallErrors(
      ast_function, function_name_path,
      FunctionNotFoundHandleMode::kReturnError, expr_resolution_info, &function,
      &error_mode));

  if (IsMatchRecognizeMeasuresFunction(function) &&
      !match_recognize_state_.has_value()) {
    return MakeSqlErrorAt(ast_function)
           << function->QualifiedSQLName()
           << " can only be used inside a MATCH_RECOGNIZE's MEASURES clause";
  }
  const ASTFunctionCall* nearest_enclosing_physical_nav_op = nullptr;
  if (IsMatchRecognizePhysicalNavigationFunction(function)) {
    if (!expr_resolution_info->in_match_recognize_define) {
      return MakeSqlErrorAt(ast_function)
             << function->QualifiedSQLName()
             << " must be directly inside a MATCH_RECOGNIZE's DEFINE clause, "
                "without any enclosing subquery";
    }
    if (expr_resolution_info->nearest_enclosing_physical_nav_op != nullptr) {
      // In the future, we can attach the location of the enclosing PREV() or
      // NEXT() call to the error message metadata, for easier navigation.
      return MakeSqlErrorAt(ast_function)
             << function->QualifiedSQLName()
             << " cannot be nested in another PREV() or NEXT() call";
    }
    nearest_enclosing_physical_nav_op = ast_function;
  }
  // Ensure we restore the original value when returning from this function.
  googlesql_base::VarSetter setter(&expr_resolution_info->nearest_enclosing_physical_nav_op,
                   nearest_enclosing_physical_nav_op);

  if (IsFilterFields(function->SQLName())) {
    return ResolveFilterFieldsFunctionCall(ast_function, function_arguments,
                                           expr_resolution_info,
                                           resolved_expr_out);
  }
  const auto& function_family = GetSpecialFunctionFamily(
      ast_function->function()->last_name()->GetAsIdString(),
      expr_resolution_info->GetWithModifierMode());
  if (function_family != FAMILY_ANON && function_family != FAMILY_ANON_BINARY) {
    if (ast_function->with_report_modifier() != nullptr) {
      return MakeSqlErrorAt(ast_function)
             << "WITH REPORT is not allowed for function "
             << absl::AsciiStrToUpper(
                    ast_function->function()->name(0)->GetAsString());
    } else if (ast_function->clamped_between_modifier() != nullptr) {
      return MakeSqlErrorAt(ast_function)
             << "The CLAMPED BETWEEN clause is not allowed in the function "
                "call arguments for function "
             << absl::AsciiStrToUpper(
                    ast_function->function()->name(0)->GetAsString());
    }
  }
  if (function->IsAggregate()) {
    return ResolveAggregateFunctionCallFirstPass(
        ast_function, function, error_mode, function_arguments,
        argument_option_map, expr_resolution_info, resolved_expr_out);
  }
  if (ast_function->distinct()) {
    return MakeSqlErrorAt(ast_function)
           << "Non-aggregate " << function->QualifiedSQLName()
           << " cannot be called with DISTINCT";
  }
  if (ast_function->null_handling_modifier() !=
      ASTFunctionCall::DEFAULT_NULL_HANDLING) {
    return MakeSqlErrorAt(ast_function)
           << "IGNORE NULLS and RESPECT NULLS are not supported on scalar "
              "functions";
  }
  GOOGLESQL_RETURN_IF_ERROR(MakeSqlErrorIfPresent(ast_function->having_modifier()))
      << "HAVING MAX and HAVING MIN are not supported on scalar functions";
  GOOGLESQL_RETURN_IF_ERROR(
      MakeSqlErrorIfPresent(ast_function->clamped_between_modifier()))
      << "CLAMPED BETWEEN is not supported on scalar functions";
  GOOGLESQL_RETURN_IF_ERROR(MakeSqlErrorIfPresent(ast_function->order_by()))
      << "ORDER BY in arguments is not supported on scalar functions";
  GOOGLESQL_RETURN_IF_ERROR(MakeSqlErrorIfPresent(ast_function->limit_offset()))
      << "LIMIT in arguments is not supported on scalar functions";
  GOOGLESQL_RETURN_IF_ERROR(MakeSqlErrorIfPresent(ast_function->group_by()))
      << "GROUP BY in arguments is not supported on scalar functions";
  GOOGLESQL_RETURN_IF_ERROR(MakeSqlErrorIfPresent(ast_function->where_expr()))
      << "WHERE in arguments is not supported on scalar functions";
  GOOGLESQL_RETURN_IF_ERROR(MakeSqlErrorIfPresent(ast_function->having_expr()))
      << "HAVING in arguments is not supported on scalar functions";

  return ResolveFunctionCallImpl(ast_function, function, error_mode,
                                 function_arguments, argument_option_map,
                                 expr_resolution_info, resolved_expr_out);
}

// Validates that no named lambda arguments are provided for aggregate and
// window function calls.
static absl::Status ValidateNamedLambdas(
    const Function* function,
    const absl::Span<const ASTExpression* const> function_arguments) {
  if (!function->IsAggregate() && !function->IsAnalytic()) {
    // Only scalar functions are allowed to have named lambda arguments.
    return absl::OkStatus();
  }
  auto first_named_lambda =
      std::find_if(function_arguments.begin(), function_arguments.end(),
                   [](const ASTExpression* arg) { return IsNamedLambda(arg); });
  if (first_named_lambda == function_arguments.end()) {
    return absl::OkStatus();
  }
  GOOGLESQL_RET_CHECK((*first_named_lambda)->Is<ASTNamedArgument>());
  // Lambdas arguments are not implemented for window or aggregate functions, so
  // we should report the error at the lambda instead of at the name.
  const ASTNode* error_location =
      (*first_named_lambda)->GetAsOrDie<ASTNamedArgument>()->expr();
  if (function->IsAggregate()) {
    return MakeSqlErrorAt(error_location)
           << "Lambda arguments are not implemented for aggregate functions";
  } else {
    // Window functions.
    return MakeSqlErrorAt(error_location)
           << "Lambda arguments are not implemented for window functions";
  }
}

// TODO: The noinline attribute is to prevent the stack usage
// being added to its caller "Resolver::ResolveExpr" which is a recursive
// function. Now the attribute has to be added for all callees. Hopefully
// the fix allows replacing these with one attribute on ResolveExpr.
ABSL_ATTRIBUTE_NOINLINE
absl::Status Resolver::ResolveAnalyticFunctionCall(
    const ASTAnalyticFunctionCall* analytic_function_call,
    ExprResolutionInfo* expr_resolution_info,
    std::unique_ptr<const ResolvedExpr>* resolved_expr_out) {
  RETURN_ERROR_IF_OUT_OF_STACK_SPACE();

  if (!expr_resolution_info->allows_analytic) {
    return MakeSqlErrorAt(analytic_function_call)
           << "Analytic function not allowed in "
           << expr_resolution_info->clause_name
           << (expr_resolution_info->is_post_distinct()
                   ? " after SELECT DISTINCT"
                   : "");
  }

  if (MultiLevelAggregationPresent(analytic_function_call->function())) {
    return MakeSqlErrorAt(analytic_function_call)
           << "GROUP BY modifiers not supported on analytic function calls.";
  }
  // HAVING filter modifier is not supported on analytic functions, since
  // GROUP BY modifiers are not supported on analytic functions.
  GOOGLESQL_RET_CHECK(analytic_function_call->function()->having_expr() == nullptr);

  GOOGLESQL_RETURN_IF_ERROR(
      MakeSqlErrorIfPresent(analytic_function_call->function()->order_by()))
      << "ORDER BY in arguments is not supported on analytic functions";

  GOOGLESQL_RETURN_IF_ERROR(
      MakeSqlErrorIfPresent(analytic_function_call->function()->limit_offset()))
      << "LIMIT in arguments is not supported on analytic functions";

  GOOGLESQL_RETURN_IF_ERROR(MakeSqlErrorIfPresent(
      analytic_function_call->function()->having_modifier()))
      << "HAVING modifier is not supported on analytic functions";

  GOOGLESQL_RETURN_IF_ERROR(MakeSqlErrorIfPresent(
      analytic_function_call->function()->clamped_between_modifier()))
      << "CLAMPED BETWEEN is not supported on analytic functions";

  std::vector<std::string> function_name_path;
  std::vector<const ASTExpression*> function_arguments;
  std::map<int, SpecialArgumentType> argument_option_map;

  // Get the catalog function name and arguments that may be different from
  // what are specified in the input query.
  GOOGLESQL_RETURN_IF_ERROR(GetFunctionNameAndArguments(
      analytic_function_call->function(), &function_name_path,
      &function_arguments, &argument_option_map,
      expr_resolution_info->query_resolution_info));

  // We want to report errors on invalid function names before errors on invalid
  // arguments, so pre-emptively lookup the function in the catalog.
  const Function* function;
  ResolvedFunctionCallBase::ErrorMode error_mode;
  GOOGLESQL_RET_CHECK(analytic_function_call->function() != nullptr);
  GOOGLESQL_RETURN_IF_ERROR(LookupFunctionFromCatalogWithChainedCallErrors(
      analytic_function_call->function(), function_name_path,
      FunctionNotFoundHandleMode::kReturnError, expr_resolution_info, &function,
      &error_mode));

  if (IsMeasureAggFunction(function)) {
    return MakeSqlErrorAt(analytic_function_call)
           << function->Name()
           << " function cannot be used as an analytic function";
  }

  std::vector<std::unique_ptr<const ResolvedExpr>> resolved_arguments;

  std::vector<NamedArgumentInfo> named_arguments;
  for (int i = 0; i < function_arguments.size(); ++i) {
    const ASTExpression* arg = function_arguments[i];
    if (arg->node_kind() == AST_NAMED_ARGUMENT) {
      const ASTNamedArgument* named_arg = arg->GetAs<ASTNamedArgument>();
      named_arguments.emplace_back(named_arg->name()->GetAsIdString(), i,
                                   named_arg);
    }
  }
  GOOGLESQL_RETURN_IF_ERROR(ValidateNamedLambdas(function, function_arguments));

  std::vector<const ASTNode*> ast_arguments;
  {
    auto analytic_arg_resolution_info = std::make_unique<ExprResolutionInfo>(
        expr_resolution_info,
        ExprResolutionInfoOptions{
            .name_scope = expr_resolution_info->analytic_name_scope,
            .allows_analytic = expr_resolution_info->allows_analytic,
            .clause_name = expr_resolution_info->clause_name});
    GOOGLESQL_RETURN_IF_ERROR(ResolveExpressionArguments(
        analytic_arg_resolution_info.get(), function_arguments,
        argument_option_map, &resolved_arguments, &ast_arguments,
        analytic_function_call->function()));
  }

  if (expr_resolution_info->findings.has_analytic) {
    return MakeSqlErrorAt(analytic_function_call)
           << "Analytic function cannot be an argument of another analytic "
              "function";
  }

  expr_resolution_info->findings.has_analytic = true;

  std::unique_ptr<ResolvedFunctionCall> resolved_function_call;
  GOOGLESQL_RETURN_IF_ERROR(function_resolver_->ResolveGeneralFunctionCall(
      analytic_function_call, ast_arguments,
      /*match_internal_signatures=*/false, function_name_path,
      /*is_analytic=*/true, std::move(resolved_arguments),
      std::move(named_arguments), /*expected_result_type=*/nullptr,
      &resolved_function_call));
  ABSL_DCHECK(expr_resolution_info->query_resolution_info != nullptr);
  if (side_effect_scope_depth_ > 0 &&
      language().LanguageFeatureEnabled(
          FEATURE_ENFORCE_CONDITIONAL_EVALUATION)) {
    return MakeSqlErrorAt(analytic_function_call)
           << "Analytic functions in side effect scopes are not yet "
              "implemented";
  }
  return expr_resolution_info->query_resolution_info->analytic_resolver()
      ->ResolveOverClauseAndCreateAnalyticColumn(
          analytic_function_call, std::move(resolved_function_call),
          expr_resolution_info, resolved_expr_out);
}

absl::StatusOr<bool> Resolver::CheckExplicitCast(
    const ResolvedExpr* resolved_argument, const Type* to_type,
    ExtendedCompositeCastEvaluator* extended_conversion_evaluator) {
  SignatureMatchResult result;
  GOOGLESQL_ASSIGN_OR_RETURN(
      InputArgumentType input_argument_type,
      GetInputArgumentTypeForExpr(resolved_argument,
                                  /*pick_default_type_for_untyped_expr=*/false,
                                  analyzer_options()));
  return coercer_.CoercesTo(input_argument_type, to_type, /*is_explicit=*/true,
                            &result, extended_conversion_evaluator);
}

static absl::Status CastResolutionError(const ASTNode* ast_location,
                                        const Type* from_type,
                                        const Type* to_type, ProductMode mode) {
  std::string error_prefix;
  if (from_type->IsArray() && to_type->IsArray()) {
    // Special intro message for array casts.
    error_prefix =
        "Casting between arrays with incompatible element types "
        "is not supported: ";
  }
  return MakeSqlErrorAt(ast_location) << error_prefix << "Invalid cast from "
                                      << from_type->ShortTypeName(mode)
                                      << " to " << to_type->ShortTypeName(mode);
}

absl::Status Resolver::ResolveFormatOrTimeZoneExpr(
    const ASTExpression* expr, ExprResolutionInfo* expr_resolution_info,
    const char* clause_name,
    std::unique_ptr<const ResolvedExpr>* resolved_expr) {
  GOOGLESQL_RETURN_IF_ERROR(ResolveExpr(expr, expr_resolution_info, resolved_expr));

  auto make_error_msg = [clause_name](absl::string_view target_type_name,
                                      absl::string_view actual_type_name) {
    return absl::Substitute("$2 should return type $0, but returns $1",
                            target_type_name, actual_type_name, clause_name);
  };
  auto status =
      CoerceExprToType(expr,
                       {type_factory_->get_string(),
                        resolved_expr->get()->type_annotation_map()},
                       kImplicitCoercion, make_error_msg, resolved_expr);
  // TODO
  if (!status.ok()) {
    return MakeSqlErrorAt(expr) << status.message();
  }

  return absl::OkStatus();
}

absl::Status Resolver::ResolveFormatClause(
    const ASTCastExpression* cast, ExprResolutionInfo* expr_resolution_info,
    const std::unique_ptr<const ResolvedExpr>& resolved_argument,
    const Type* resolved_cast_type,
    std::unique_ptr<const ResolvedExpr>* resolved_format,
    std::unique_ptr<const ResolvedExpr>* resolved_time_zone,
    bool* resolve_cast_to_null) {
  if (cast->format() == nullptr) {
    return absl::OkStatus();
  }

  if (!language().LanguageFeatureEnabled(FEATURE_FORMAT_IN_CAST)) {
    return MakeSqlErrorAt(cast->format())
           << "CAST with FORMAT is not supported";
  }

  *resolve_cast_to_null = false;
  auto iter = internal::GetCastFormatMap().find(
      {resolved_argument->type()->kind(), resolved_cast_type->kind()});
  if (iter == internal::GetCastFormatMap().end()) {
    return MakeSqlErrorAt(cast->format())
           << "FORMAT is not allowed for cast from "
           << resolved_argument->type()->ShortTypeName(product_mode()) << " to "
           << resolved_cast_type->ShortTypeName(product_mode());
  }

  GOOGLESQL_RETURN_IF_ERROR(ResolveFormatOrTimeZoneExpr(
      cast->format()->format(), expr_resolution_info, "FORMAT expression",
      resolved_format));

  if (cast->format()->time_zone_expr() != nullptr) {
    // time zone is allowed only for cast from/to timestamps.
    if (resolved_argument->type()->kind() != TYPE_TIMESTAMP &&
        resolved_cast_type->kind() != TYPE_TIMESTAMP) {
      return MakeSqlErrorAt(cast->format()->time_zone_expr())
             << "AT TIME ZONE is not allowed for cast from "
             << resolved_argument->type()->ShortTypeName(product_mode())
             << " to " << resolved_cast_type->ShortTypeName(product_mode());
    }

    GOOGLESQL_RETURN_IF_ERROR(ResolveFormatOrTimeZoneExpr(
        cast->format()->time_zone_expr(), expr_resolution_info,
        "AT TIME ZONE expression", resolved_time_zone));
  }

  return absl::OkStatus();
}

absl::StatusOr<bool> Resolver::ShouldTryCastConstantFold(
    const ResolvedExpr* resolved_argument, const Type* resolved_cast_type) {
  // Don't attempt to coerce string/bytes literals to protos in the
  // analyzer. Instead, put a ResolvedCast node in the resolved AST and let the
  // engines do it. There are at least two reasons for this:
  // 1) We might not have the appropriate proto descriptor here (particularly
  //    for extensions).
  // 2) Since semantics of proto casting are not fully specified, applying these
  //    casts at analysis time may have inconsistent behavior with applying them
  //    at run-time in the engine. (One example is with garbage values, where
  //    engines may not be prepared for garbage proto Values in the resolved
  //    AST.)
  // TODO: should we also prevent casting/coercion into proto
  //                    enums?
  SignatureMatchResult result;

  // Note: If we have something other than a literal, such as a function call,
  // then we may still want to attempt to fold and to guess the types. Example:
  // CAST(Error('abc') AS MyProto1)
  // We need to infer that Error() returns MyProto1 in this case.
  if (resolved_argument->node_kind() == RESOLVED_LITERAL &&
      resolved_cast_type->IsProto()) {
    return false;
  }

  if (resolved_cast_type->IsEnum() &&
      resolved_cast_type->AsEnum()->IsOpaque()) {
    // Constant folding is probably a bad idea generally, but opaque enums
    // present a unique challenging because PRODUCT_EXTERNAL engines don't
    // (currently) recognize the explicitly named opaque type names.
    // SqlBuilder needs to respect this, and it does so by printing opaque
    // values by name - as long as the explicit cast is preserved, then
    // both implicit and explicit coercion of literals can just print
    // the literal, and it all works out.
    return false;
  }
  ExtendedCompositeCastEvaluator unused_extended_conversion_evaluator =
      ExtendedCompositeCastEvaluator::Invalid();
  absl::StatusOr<bool> check_status =
      CheckExplicitCast(resolved_argument, resolved_cast_type,
                        &unused_extended_conversion_evaluator);
  return check_status.value_or(false);
}

absl::StatusOr<const AnnotationMap*>
Resolver::CreateAnnotationMapFromTypeWithModifiers(
    const Type* type, const TypeModifiers& type_modifiers) const {
  std::unique_ptr<AnnotationMap> annotation_map = AnnotationMap::Create(type);
  if (!type_modifiers.collation().Empty()) {
    GOOGLESQL_RETURN_IF_ERROR(
        type_modifiers.collation().PopulateAnnotationMap(*annotation_map));
  }
  annotation_map->Normalize();

  if (annotation_map->Empty()) {
    return nullptr;
  }
  return type_factory_->TakeOwnership(std::move(annotation_map));
}

// TODO: The noinline attribute is to prevent the stack usage
// being added to its caller "Resolver::ResolveExpr" which is a recursive
// function. Now the attribute has to be added for all callees. Hopefully
// the fix allows replacing these with one attribute on ResolveExpr.
ABSL_ATTRIBUTE_NOINLINE
absl::Status Resolver::ResolveExplicitCast(
    const ASTCastExpression* cast, ExprResolutionInfo* expr_resolution_info,
    std::unique_ptr<const ResolvedExpr>* resolved_expr_out) {
  const Type* resolved_cast_type;
  TypeModifiers resolved_type_modifiers;
  std::unique_ptr<const ResolvedExpr> resolved_argument;
  ResolveTypeModifiersOptions options_for_cast = {.allow_type_parameters = true,
                                                  .allow_collation = true};
  if (!language().LanguageFeatureEnabled(FEATURE_COLLATION_IN_EXPLICIT_CAST)) {
    options_for_cast.allow_collation = false;
    options_for_cast.context = "cast";
  }
  GOOGLESQL_RETURN_IF_ERROR(ResolveType(cast->type(), options_for_cast,
                              &resolved_cast_type, &resolved_type_modifiers));

  // Create annotation map and populate it with collation & timestamp precision
  // annotations.
  GOOGLESQL_ASSIGN_OR_RETURN(const AnnotationMap* cast_type_annotation_map,
                   CreateAnnotationMapFromTypeWithModifiers(
                       resolved_cast_type, resolved_type_modifiers));
  AnnotatedType annotated_cast_type =
      AnnotatedType(resolved_cast_type, cast_type_annotation_map);

  GOOGLESQL_RETURN_IF_ERROR(ResolveExpr(cast->expr(), expr_resolution_info,
                              &resolved_argument, resolved_cast_type));
  const bool return_null_on_error = cast->is_safe_cast();

  std::unique_ptr<const ResolvedExpr> resolved_format;
  std::unique_ptr<const ResolvedExpr> resolved_time_zone;
  bool resolve_cast_to_null = false;
  GOOGLESQL_RETURN_IF_ERROR(ResolveFormatClause(
      cast, expr_resolution_info, resolved_argument, resolved_cast_type,
      &resolved_format, &resolved_time_zone, &resolve_cast_to_null));

  if (resolve_cast_to_null) {
    *resolved_expr_out = MakeResolvedLiteral(cast, annotated_cast_type,
                                             Value::Null(resolved_cast_type),
                                             /*has_explicit_type=*/true);
    return absl::OkStatus();
  }

  // Handles casting literals that do not have an explicit type:
  // - For untyped NULL, converts the NULL to the target type.
  // - For untyped empty array, converts it to the target type if the target
  //   type is an array type.
  if (resolved_argument->node_kind() == RESOLVED_LITERAL &&
      !resolved_argument->GetAs<ResolvedLiteral>()->has_explicit_type()) {
    if (resolved_argument->GetAs<ResolvedLiteral>()->value().is_null()) {
      *resolved_expr_out = MakeResolvedLiteral(cast, annotated_cast_type,
                                               Value::Null(resolved_cast_type),
                                               /*has_explicit_type=*/true);
      return absl::OkStatus();
    }

    if (resolved_argument->GetAs<ResolvedLiteral>()->value().is_empty_array()) {
      if (resolved_cast_type->IsArray()) {
        *resolved_expr_out = MakeResolvedLiteral(
            cast, annotated_cast_type,
            Value::Array(resolved_cast_type->AsArray(), /*values=*/{}),
            /*has_explicit_type=*/true);
        return absl::OkStatus();
      } else {
        return CastResolutionError(cast->expr(), resolved_argument->type(),
                                   resolved_cast_type, product_mode());
      }
    }
  }

  GOOGLESQL_ASSIGN_OR_RETURN(
      bool should_try_to_fold,
      ShouldTryCastConstantFold(resolved_argument.get(), resolved_cast_type));
  if (should_try_to_fold) {
    // For an explicit cast, if we are casting a literal then we will try to
    // convert it to the target type and mark it as already cast.
    absl::Status folding_result = function_resolver_->AddCastOrConvertLiteral(
        cast->expr(), annotated_cast_type, std::move(resolved_format),
        std::move(resolved_time_zone), resolved_type_modifiers,
        /*scan=*/nullptr, /*set_has_explicit_type=*/true, return_null_on_error,
        &resolved_argument);

    // Ignore errors only if they are safe-convertible.
    // If we cannot fold it, we'll just leave the cast unfolded.
    if (!folding_result.ok()) {
      if (folding_result.code() != absl::StatusCode::kInvalidArgument &&
          folding_result.code() != absl::StatusCode::kOutOfRange) {
        // Only kInvalidArgument and kOutOfRange indicate a bad value when
        // folding. Anything else is a bigger problem that we need to bubble
        // up. This logic is not to be used elsewhere as there are exceptional
        // circumstances here:
        // 1. kOutOfRange generally is a runtime error, but it may arise during
        //    folding, which we are absorbing and deferring to runtime.
        // 2. kInvalidArgument is usually a fatal analysis error. However, in
        //    this particular check, we catch it when casting/coercion fails
        //    during a user-specified cast. It should have been a kOutOfRange
        //    but changing all such places is non-trivial.
        // TODO : change all such places to return kOutOfRange,
        //                     to reflect their nature as evaluation errors.
        return folding_result;
      }

      // In that case, we abandon the attempt the constant fold and produce a
      // regular cast operator to be evaluated at runtime.
    } else {
      // Make the location of the resolved literal to enclose the entire CAST
      // expression.
      if (resolved_argument->node_kind() == RESOLVED_LITERAL) {
        const ResolvedLiteral* argument_literal =
            resolved_argument->GetAs<ResolvedLiteral>();
        resolved_argument = MakeResolvedLiteral(
            cast,
            {argument_literal->type(), annotated_cast_type.annotation_map},
            argument_literal->value(), argument_literal->has_explicit_type());
      }
      if (resolved_argument->node_kind() == RESOLVED_CAST) {
        MaybeRecordParseLocation(
            analyzer_options_.parse_location_record_type() ==
                    PARSE_LOCATION_RECORD_FULL_NODE_SCOPE
                ? cast
                : static_cast<const ASTNode*>(cast->type()),
            const_cast<ResolvedExpr*>(resolved_argument.get()));
      }
      *resolved_expr_out = std::move(resolved_argument);
      return absl::OkStatus();
    }
  }

  // If we reach this point, either we decided not to attempt constant folding
  // the cast or we attempted and decided to defer the cast until runtime.
  GOOGLESQL_RETURN_IF_ERROR(ResolveCastWithResolvedArgument(
      cast->expr(), annotated_cast_type, std::move(resolved_format),
      std::move(resolved_time_zone), std::move(resolved_type_modifiers),
      return_null_on_error, &resolved_argument));
  // The result is not always a RESOLVED_CAST: if it turns out to be a NOOP
  // (e.d. input and output types and collations are the same), we end up
  // removing that cast and just return the argument, which itself could be a
  // literal, for example.
  if (resolved_argument->node_kind() == RESOLVED_CAST) {
    MaybeRecordParseLocation(
        analyzer_options_.parse_location_record_type() ==
                PARSE_LOCATION_RECORD_FULL_NODE_SCOPE
            ? cast
            : static_cast<const ASTNode*>(cast->type()),
        const_cast<ResolvedExpr*>(resolved_argument.get()));
  }

  *resolved_expr_out = std::move(resolved_argument);
  return absl::OkStatus();
}

absl::StatusOr<TypeParameters> Resolver::ResolveTypeParameters(
    const ASTTypeParameterList* type_parameters, const Type& resolved_type,
    const std::vector<TypeParameters>& child_parameter_list) {
  if (type_parameters != nullptr &&
      !language().LanguageFeatureEnabled(FEATURE_PARAMETERIZED_TYPES)) {
    return MakeSqlErrorAt(type_parameters)
           << "Parameterized types are not supported";
  }

  if (type_parameters == nullptr) {
    // A subfield of the struct or the element of an array has type parameters.
    if (!child_parameter_list.empty()) {
      return TypeParameters::MakeTypeParametersWithChildList(
          child_parameter_list);
    }
    // No type parameters in the AST.
    return TypeParameters();
  }

  // Resolve each type parameter value retrieved from the parser.
  GOOGLESQL_ASSIGN_OR_RETURN(std::vector<TypeParameterValue> resolved_type_parameter_list,
                   ResolveParameterLiterals(*type_parameters));

  // Validate type parameters and get the resolved TypeParameters class.
  absl::StatusOr<TypeParameters> type_params_or_status =
      resolved_type.ValidateAndResolveTypeParameters(
          resolved_type_parameter_list, product_mode());
  // TODO: Modify ValidateAndResolveTypeParameters to
  // handle the attachment of the error location. This can be done by taking an
  // argument that is a function: std::function<StatusBuilder(int)>.
  if (!type_params_or_status.ok()) {
    // We assume INVALID_ARGUMENT is never returned by
    // ValidateAndResolveTypeParameters for reasons other than MakeSqlError().
    if (absl::IsInvalidArgument(type_params_or_status.status())) {
      return MakeSqlErrorAt(type_parameters)
             << type_params_or_status.status().message();
    }
    return type_params_or_status.status();
  }
  TypeParameters type_params = type_params_or_status.value();

  if (!child_parameter_list.empty()) {
    type_params.set_child_list(child_parameter_list);
  }

  if (type_params.IsTimestampTypeParameters()) {
    if (!analyzer_options_.language().LanguageFeatureEnabled(
            FEATURE_TIMESTAMP_PRECISION)) {
      return MakeSqlErrorAt(type_parameters)
             << "Timestamp precision type parameter is not supported";
    }
    if (type_params.timestamp_type_parameters().has_precision()) {
      auto precision = type_params.timestamp_type_parameters().precision();
      if (precision == 12 &&
          !analyzer_options_.language().LanguageFeatureEnabled(
              FEATURE_TIMESTAMP_PICOS)) {
        return MakeSqlErrorAt(type_parameters)
               << "Timestamp precision " << precision << " is not supported";
      }
      if (precision == 9 &&
          !analyzer_options_.language().LanguageFeatureEnabled(
              FEATURE_TIMESTAMP_NANOS)) {
        return MakeSqlErrorAt(type_parameters)
               << "Timestamp precision " << precision << " is not supported";
      }
    }
  }

  return type_params;
}

absl::StatusOr<Collation> Resolver::ResolveTypeCollation(
    const ASTCollate* collate, const Type& resolved_type,
    std::vector<Collation> child_collation_list) {
  // We only check <collate> here. For a collated array type or struct type
  // (in which case <collate> is null but <child_collation_list> is not empty),
  // this should have been checked when resolving collation for its element or
  // field type.
  if (collate != nullptr &&
      !language().LanguageFeatureEnabled(FEATURE_COLLATION_SUPPORT)) {
    return MakeSqlErrorAt(collate)
           << "Type with collation name is not supported";
  }

  if (resolved_type.kind() == TYPE_ARRAY ||
      resolved_type.kind() == TYPE_STRUCT) {
    std::string child_type_name =
        resolved_type.kind() == TYPE_ARRAY ? "element type" : "field type";
    if (collate != nullptr) {
      return MakeSqlErrorAt(collate)
             << resolved_type.ShortTypeName(product_mode())
             << " type cannot have collation by itself, it can only have "
                "collation on its "
             << child_type_name;
    }
    return Collation::MakeCollationWithChildList(
        std::move(child_collation_list));
  } else {
    GOOGLESQL_RET_CHECK(child_collation_list.empty());
    if (collate == nullptr) {
      return Collation();
    }
    if (resolved_type.kind() != TYPE_STRING) {
      return MakeSqlErrorAt(collate)
             << "Type " << resolved_type.ShortTypeName(product_mode())
             << " does not support collation name";
    }
    // Deal with STRING type.
    std::unique_ptr<const ResolvedExpr> resolved_collation_expr;
    GOOGLESQL_RETURN_IF_ERROR(ResolveCollate(collate, &resolved_collation_expr));
    GOOGLESQL_RET_CHECK(
        resolved_collation_expr->node_kind() == RESOLVED_LITERAL &&
        resolved_collation_expr->GetAs<ResolvedLiteral>()->type()->IsString());
    return Collation::MakeScalar(
        resolved_collation_expr->GetAs<ResolvedLiteral>()
            ->value()
            .string_value());
  }
}

absl::Status Resolver::ResolveCastWithResolvedArgument(
    const ASTNode* ast_location, const Type* to_type, bool return_null_on_error,
    std::unique_ptr<const ResolvedExpr>* resolved_argument) {
  return ResolveCastWithResolvedArgument(
      ast_location, {to_type, /*annotation_map=*/nullptr}, /*format=*/nullptr,
      /*time_zone=*/nullptr, TypeModifiers(), return_null_on_error,
      resolved_argument);
}

static bool IsCastNoop(AnnotatedType source, AnnotatedType target,
                       const TypeModifiers& type_modifiers) {
  return source.type->Equals(target.type) &&
         type_modifiers.type_parameters().IsEmpty() &&
         AnnotationMap::Equals(source.annotation_map, target.annotation_map);
}

absl::Status Resolver::ResolveCastWithResolvedArgument(
    const ASTNode* ast_location, AnnotatedType to_annotated_type,
    std::unique_ptr<const ResolvedExpr> format,
    std::unique_ptr<const ResolvedExpr> time_zone, TypeModifiers type_modifiers,
    bool return_null_on_error,
    std::unique_ptr<const ResolvedExpr>* resolved_argument) {
  RETURN_ERROR_IF_OUT_OF_STACK_SPACE();
  const Type* to_type = to_annotated_type.type;
  const AnnotationMap* to_type_annotation_map =
      to_annotated_type.annotation_map;
  GOOGLESQL_ASSIGN_OR_RETURN(bool equals_collation_annotation,
                   type_modifiers.collation().EqualsCollationAnnotation(
                       to_type_annotation_map));
  GOOGLESQL_RET_CHECK(equals_collation_annotation);

  AnnotatedType source_annotated_type =
      resolved_argument->get()->annotated_type();

  // We can return without creating a ResolvedCast if the original type and
  // target type & annotations are the same, unless explicitly requested.
  if (!analyzer_options_.preserve_unnecessary_cast() &&
      IsCastNoop(source_annotated_type, to_annotated_type, type_modifiers)) {
    return absl::OkStatus();
  }

  ExtendedCompositeCastEvaluator extended_conversion_evaluator =
      ExtendedCompositeCastEvaluator::Invalid();
  GOOGLESQL_ASSIGN_OR_RETURN(bool cast_is_valid,
                   CheckExplicitCast(resolved_argument->get(), to_type,
                                     &extended_conversion_evaluator));
  if (!cast_is_valid) {
    return CastResolutionError(ast_location, (*resolved_argument)->type(),
                               to_type, product_mode());
  }

  // Add EXPLICIT cast.
  auto resolved_cast = MakeResolvedCast(
      to_type, std::move(*resolved_argument), std::move(format),
      std::move(time_zone), std::move(type_modifiers), return_null_on_error,
      extended_conversion_evaluator);
  if (to_type_annotation_map != nullptr) {
    resolved_cast->set_type_annotation_map(to_type_annotation_map);
  }

  GOOGLESQL_RETURN_IF_ERROR(annotation_propagator_->CheckAndPropagateAnnotations(
      ast_location, resolved_cast.get()));

  // Annotation propagation may have added defaults, so check again whether the
  // final cast is a NOOP. If so, just return the input argument directly.
  if (!analyzer_options_.preserve_unnecessary_cast() &&
      IsCastNoop(source_annotated_type, resolved_cast->annotated_type(),
                 resolved_cast->type_modifiers())) {
    *resolved_argument = ToBuilder(std::move(resolved_cast)).release_expr();
  } else {
    *resolved_argument = std::move(resolved_cast);
  }
  return absl::OkStatus();
}

const char Resolver::kArrayAtOffset[] = "$array_at_offset";
const char Resolver::kArrayAtOrdinal[] = "$array_at_ordinal";
const char Resolver::kProtoMapAtKey[] = "$proto_map_at_key";
const char Resolver::kSafeArrayAtOffset[] = "$safe_array_at_offset";
const char Resolver::kSafeArrayAtOrdinal[] = "$safe_array_at_ordinal";
const char Resolver::kSafeProtoMapAtKey[] = "$safe_proto_map_at_key";
const char Resolver::kSubscript[] = "$subscript";
const char Resolver::kSubscriptWithKey[] = "$subscript_with_key";
const char Resolver::kSafeSubscriptWithKey[] = "$safe_subscript_with_key";
const char Resolver::kSubscriptWithOffset[] = "$subscript_with_offset";
const char Resolver::kSafeSubscriptWithOffset[] = "$safe_subscript_with_offset";
const char Resolver::kSubscriptWithOrdinal[] = "$subscript_with_ordinal";
const char Resolver::kSafeSubscriptWithOrdinal[] =
    "$safe_subscript_with_ordinal";

// TODO: Rename ResolveArrayElement to ResolveSubscriptElement.
// Rename ASTArrayElement to ASTSubscriptElement in all references.
//
// TODO: The noinline attribute is to prevent the stack usage
// being added to its caller "Resolver::ResolveExpr" which is a recursive
// function. Now the attribute has to be added for all callees. Hopefully
// the fix allows replacing these with one attribute on ResolveExpr.
ABSL_ATTRIBUTE_NOINLINE
absl::Status Resolver::ResolveArrayElement(
    const ASTArrayElement* array_element,
    ExprResolutionInfo* expr_resolution_info,
    std::unique_ptr<const ResolvedExpr>* resolved_expr_out) {
  RETURN_ERROR_IF_OUT_OF_STACK_SPACE();

  std::vector<std::unique_ptr<const ResolvedExpr>> args;
  // Resolve the lhs first before failing the subscript lookup.
  // This will produce errors in the input query from left to right.
  GOOGLESQL_RETURN_IF_ERROR(ResolveExpressionArgument(array_element->array(),
                                            expr_resolution_info, &args));
  const ResolvedExpr* resolved_lhs = args.back().get();

  if (resolved_lhs->type()->IsStruct() &&
      language().LanguageFeatureEnabled(
          LanguageFeature::FEATURE_STRUCT_POSITIONAL_ACCESSOR)) {
    return ResolveStructSubscriptElementAccess(
        std::move(args.back()), array_element->position(), expr_resolution_info,
        resolved_expr_out);
  }

  // An array element access during a flattened path refers to the preceding
  // element, not to the output of flatten. As such, if we're in the middle of
  // a flatten, we have to apply the array access to the Get*Field.
  std::unique_ptr<ResolvedFlatten> resolved_flatten;
  if (resolved_lhs->Is<ResolvedFlatten>() &&
      expr_resolution_info->flatten_state.active_flatten() != nullptr &&
      resolved_lhs == expr_resolution_info->flatten_state.active_flatten()) {
    resolved_flatten.reset(const_cast<ResolvedFlatten*>(
        args.back().release()->GetAs<ResolvedFlatten>()));
    auto get_field_list = resolved_flatten->release_get_field_list();
    GOOGLESQL_RET_CHECK(!get_field_list.empty());
    args.back() = std::move(get_field_list.back());
    get_field_list.pop_back();
    resolved_flatten->set_get_field_list(std::move(get_field_list));
  }

  const bool is_array_subscript = resolved_lhs->type()->IsArray();
  std::vector<std::string> function_name_path;
  const ASTExpression* unwrapped_ast_position_expr;
  std::unique_ptr<const ResolvedExpr> resolved_position;
  std::string original_wrapper_name("");

  if (!is_array_subscript) {
    GOOGLESQL_RETURN_IF_ERROR(ResolveNonArraySubscriptElementAccess(
        resolved_lhs, array_element->position(), expr_resolution_info,
        &function_name_path, &unwrapped_ast_position_expr, &resolved_position,
        &original_wrapper_name));
  } else {
    absl::string_view function_name;
    GOOGLESQL_RETURN_IF_ERROR(ResolveArrayElementAccess(
        resolved_lhs, array_element->position(), expr_resolution_info,
        &function_name, &unwrapped_ast_position_expr, &resolved_position,
        &original_wrapper_name));
    function_name_path.push_back(std::string(function_name));
  }
  const std::string subscript_lhs_type =
      resolved_lhs->type()->ShortTypeName(product_mode());
  const std::string argumentType =
      resolved_position->type()->ShortTypeName(product_mode());
  args.push_back(std::move(resolved_position));

  // Engines may or may not have loaded the relevant $subscript function (or
  // friends) into their Catalog.  If they have not, then we want to provide
  // a user friendly error saying that the operator is not supported.  So we
  // first try to look up the function name in the Catalog to see if it exists.
  const Function* function;
  ResolvedFunctionCallBase::ErrorMode error_mode;
  absl::Status status = LookupFunctionFromCatalog(
      array_element, function_name_path,
      FunctionNotFoundHandleMode::kReturnNotFound, &function, &error_mode);

  // If the Catalog lookup fails then we will return an error.
  if (!status.ok()) {
    if (!absl::IsNotFound(status)) {
      return status;
    }
    // Otherwise, then the engine has not added the appropriate subscript
    // function into their Catalog.  We detect that case so we can provide
    // a good user-facing error.
    const std::string element_wrapper_string =
        original_wrapper_name.empty()
            ? ""
            : absl::StrCat(original_wrapper_name, "()");
    const std::string element_or_subscript =
        (is_array_subscript ? "Element" : "Subscript");
    return MakeSqlErrorAt(array_element->open_bracket_location())
           << element_or_subscript << " access using ["
           << element_wrapper_string << "] is not supported on values of type "
           << subscript_lhs_type;
  }

  // This may fail due to no function signature match.  If so, then we get
  // a no signature match error (which can/should be customized as appropriate).
  GOOGLESQL_RETURN_IF_ERROR(ResolveFunctionCallWithResolvedArguments(
      /*ast_location=*/(analyzer_options_.parse_location_record_type() ==
                                PARSE_LOCATION_RECORD_FULL_NODE_SCOPE
                            ? array_element
                            : array_element->position()),
      /*arg_locations=*/{array_element, unwrapped_ast_position_expr},
      /*match_internal_signatures=*/false, function, error_mode,
      std::move(args), /*named_arguments=*/{}, expr_resolution_info,
      /*multi_level_aggregate_info=*/nullptr, resolved_expr_out));

  if (resolved_flatten != nullptr) {
    resolved_flatten->add_get_field_list(std::move(*resolved_expr_out));
    *resolved_expr_out = std::move(resolved_flatten);
  }
  return absl::OkStatus();
}

absl::Status Resolver::ResolveStructSubscriptElementAccess(
    std::unique_ptr<const ResolvedExpr> resolved_struct,
    const ASTExpression* field_position,
    ExprResolutionInfo* expr_resolution_info,
    std::unique_ptr<const ResolvedExpr>* resolved_expr_out) {
  const ASTFunctionCall* ast_function_call =
      field_position->GetAsOrNull<ASTFunctionCall>();
  IdString field_expr_wrapper = kOffset;
  int starting_position = 0;
  absl::string_view position_name = "offset";
  std::unique_ptr<const ResolvedExpr> resolved_position;

  if (!ast_function_call && language().LanguageFeatureEnabled(
                                LanguageFeature::FEATURE_BARE_ARRAY_ACCESS)) {
    GOOGLESQL_RETURN_IF_ERROR(
        ResolveExpr(field_position, expr_resolution_info, &resolved_position));
  } else if (!ast_function_call ||
             ast_function_call->function()->num_names() != 1 ||
             ast_function_call->HasModifiers()) {
    return MakeSqlErrorAt(field_position)
           << "Field access must be OFFSET, SAFE_OFFSET, ORDINAL or "
              "SAFE_ORDINAL.";
  }
  if (ast_function_call) {
    field_expr_wrapper =
        ast_function_call->function()->first_name()->GetAsIdString();

    if (ast_function_call->arguments().size() != 1) {
      return MakeSqlErrorAt(field_position)
             << "Subscript access using [" << field_expr_wrapper.ToStringView()
             << "()] on structs only supports one argument";
    }
    GOOGLESQL_RETURN_IF_ERROR(
        ValidateASTFunctionCallWithoutArgumentAlias(ast_function_call));
    GOOGLESQL_RETURN_IF_ERROR(ResolveExpr(ast_function_call->arguments()[0],
                                expr_resolution_info, &resolved_position));
  }

  if (field_expr_wrapper.CaseEquals(kOrdinal) ||
      field_expr_wrapper.CaseEquals(kSafeOrdinal)) {
    starting_position = 1;
    position_name = "ordinal";
  } else if (!field_expr_wrapper.CaseEquals(kOffset) &&
             !field_expr_wrapper.CaseEquals(kSafeOffset)) {
    return MakeSqlErrorAt(field_position)
           << "Field access must be OFFSET, SAFE_OFFSET, ORDINAL or "
              "SAFE_ORDINAL.";
  }

  int64_t position;
  bool is_nonnull_int_literal =
      resolved_position->Is<ResolvedLiteral>() &&
      resolved_position->type()->IsInteger() &&
      !resolved_position->GetAs<ResolvedLiteral>()->value().is_null();
  bool analysis_const_enabled = language().LanguageFeatureEnabled(
      FEATURE_ANALYSIS_CONSTANT_STRUCT_POSITIONAL_ACCESSOR);
  if (is_nonnull_int_literal) {
    auto* literal = resolved_position->GetAs<ResolvedLiteral>();
    const_cast<ResolvedLiteral*>(literal)->set_preserve_in_literal_remover(
        true);
    if (literal->type()->kind() == TYPE_UINT64) {
      uint64_t position_uint64 = literal->value().ToUint64();
      if (position_uint64 > std::numeric_limits<int64_t>::max()) {
        return MakeSqlErrorAt(field_position)
               << "Field index is larger than INT64 max size";
      }
      position = static_cast<int64_t>(position_uint64);
    } else {
      // Verified IsInteger() above, so ToInt64() is valid for remaining types.
      position = literal->value().ToInt64();
    }
  } else if (!analysis_const_enabled) {
    return MakeSqlErrorAt(field_position)
           << "Field element access is only supported for literal integer "
              "positions";
  } else {
    // Otherwise, evaluate the analysis time constant.
    if (!IsAnalysisConstant(resolved_position.get())) {
      return MakeSqlErrorAt(field_position)
             << "Field element access must be a non-null integer knowable at "
                "compile time";
    }

    // TODO: b/277365877 - Refactor this to use a more robust GetConstantValue
    // API, instead of doing special case.
    if (!resolved_position->Is<ResolvedConstant>()) {
      return absl::UnimplementedError(
          "Unimplemented compile time constant type. Consider using a SQL "
          "constant instead");
    }

    if (!resolved_position->type()->IsInteger()) {
      return MakeSqlErrorAt(field_position)
             << "Field element access only supports integer positions, but "
                "got a constant of type "
             << resolved_position->type()->DebugString();
    }

    auto* resolved_constant = resolved_position->GetAs<ResolvedConstant>();
    const Constant* constant = resolved_constant->constant();
    if (analyzer_options_.constant_evaluator() != nullptr) {
      absl::StatusOr<Value> constant_value = GetResolvedConstantValue(
          *resolved_position->GetAs<ResolvedConstant>(), analyzer_options());

      if (ShouldPropagateError(constant_value.status())) {
        return constant_value.status();
      }

      // Handle runtime error. It indicates something is malfunctioning in
      // constant evaluator.
      GOOGLESQL_RET_CHECK(!absl::IsOutOfRange(constant_value.status()))
          << "Evaluation of constant expressions during semantic analysis "
          << "produced a runtime error: " << constant_value.status().message();
    }

    if (!constant->HasValue()) {
      return MakeSqlErrorAt(field_position)
             << "Field access cannot be computed because the value of constant "
             << constant->Name() << " is not available";
    }
    GOOGLESQL_ASSIGN_OR_RETURN(Value constant_value, constant->GetValue());
    if (constant_value.type()->kind() == TYPE_UINT64) {
      uint64_t position_uint64 = constant_value.ToUint64();
      if (position_uint64 > std::numeric_limits<int64_t>::max()) {
        return MakeSqlErrorAt(field_position)
               << "Field index is larger than INT64 max size";
      }
      position = static_cast<int64_t>(position_uint64);
    } else {
      // Verified IsInteger() above, so ToInt64() is valid for remaining types.
      position = constant_value.ToInt64();
    }
  }

  GOOGLESQL_RET_CHECK(IsAnalysisConstant(resolved_position.get()))
      << resolved_position->DebugString();

  int64_t field_idx = position - starting_position;

  if (field_idx < 0 ||
      field_idx >= resolved_struct->type()->AsStruct()->num_fields()) {
    return MakeSqlErrorAt(field_position)
           << "Field " << position_name << " " << position
           << " is out of bounds in " << resolved_struct->type()->DebugString();
  }
  GOOGLESQL_RET_CHECK_LE(field_idx, std::numeric_limits<int>::max());
  int field_idx_int = static_cast<int>(field_idx);
  auto* field_type =
      resolved_struct->type()->AsStruct()->field(field_idx_int).type;
  auto resolved_get_field = MakeResolvedGetStructField(
      field_type, std::move(resolved_struct), field_idx_int,
      /*field_expr_is_positional=*/true);
  MaybeRecordParseLocation(field_position, resolved_get_field.get());
  *resolved_expr_out = std::move(resolved_get_field);
  return absl::OkStatus();
}

absl::Status Resolver::ResolveNonArraySubscriptElementAccess(
    const ResolvedExpr* resolved_lhs, const ASTExpression* ast_position,
    ExprResolutionInfo* expr_resolution_info,
    std::vector<std::string>* function_name_path,
    const ASTExpression** unwrapped_ast_position_expr,
    std::unique_ptr<const ResolvedExpr>* resolved_expr_out,
    std::string* original_wrapper_name) {
  original_wrapper_name->clear();
  *unwrapped_ast_position_expr = nullptr;
  if (ast_position->node_kind() == AST_FUNCTION_CALL) {
    const ASTFunctionCall* ast_function_call =
        ast_position->GetAsOrDie<ASTFunctionCall>();
    if (ast_function_call->function()->num_names() == 1 &&
        !ast_function_call->HasModifiers()) {
      const IdString name =
          ast_function_call->function()->first_name()->GetAsIdString();
      *original_wrapper_name = name.ToString();
      static const IdStringHashMapCase<std::vector<std::string>>*
          kNameToSubscript = new IdStringHashMapCase<std::vector<std::string>>{
              {kKey, {kSubscriptWithKey}},
              {kOffset, {kSubscriptWithOffset}},
              {kOrdinal, {kSubscriptWithOrdinal}},
              {kSafeKey, {kSafeSubscriptWithKey}},
              {kSafeOffset, {kSafeSubscriptWithOffset}},
              {kSafeOrdinal, {kSafeSubscriptWithOrdinal}}};

      // The function name is correctly resolved, then proceed to resolve the
      // argument.
      if (auto iter = kNameToSubscript->find(name);
          iter != kNameToSubscript->end()) {
        *function_name_path = iter->second;
        if (ast_function_call->arguments().size() != 1) {
          return MakeSqlErrorAt(ast_position)
                 << "Subscript access using [" << *original_wrapper_name
                 << "()] on value of type "
                 << resolved_lhs->type()->ShortTypeName(product_mode())
                 << " only support one argument";
        }
        GOOGLESQL_RETURN_IF_ERROR(
            ValidateASTFunctionCallWithoutArgumentAlias(ast_function_call));
        *unwrapped_ast_position_expr = ast_function_call->arguments()[0];
        GOOGLESQL_RETURN_IF_ERROR(ResolveExpr(*unwrapped_ast_position_expr,
                                    expr_resolution_info, resolved_expr_out));
        return absl::OkStatus();
      }
    }
  }

  // If we get here then the subscript operator argument is a normal expression,
  // not one of the special cases above.
  function_name_path->push_back(std::string(kSubscript));
  *unwrapped_ast_position_expr = ast_position;
  GOOGLESQL_RETURN_IF_ERROR(ResolveExpr(*unwrapped_ast_position_expr,
                              expr_resolution_info, resolved_expr_out));
  return absl::OkStatus();
}

absl::Status Resolver::ResolveArrayElementAccess(
    const ResolvedExpr* resolved_array, const ASTExpression* ast_position,
    ExprResolutionInfo* expr_resolution_info, absl::string_view* function_name,
    const ASTExpression** unwrapped_ast_position_expr,
    std::unique_ptr<const ResolvedExpr>* resolved_expr_out,
    std::string* original_wrapper_name) {
  GOOGLESQL_RET_CHECK(resolved_array->type()->IsArray());

  // We expect that array element positions are wrapped like array[offset(x)] or
  // array[ordinal(y)], and we give an error if neither of those wrapper
  // functions is used.  <unwrapped_ast_position_expr> will be set to non-NULL
  // if we find a valid wrapper.
  *unwrapped_ast_position_expr = nullptr;
  if (ast_position->node_kind() == AST_FUNCTION_CALL) {
    const ASTFunctionCall* ast_function_call =
        ast_position->GetAsOrDie<ASTFunctionCall>();
    if (ast_function_call->function()->num_names() == 1 &&
        ast_function_call->arguments().size() == 1 &&
        !ast_function_call->HasModifiers()) {
      GOOGLESQL_RETURN_IF_ERROR(
          ValidateASTFunctionCallWithoutArgumentAlias(ast_function_call));
      const IdString name =
          ast_function_call->function()->first_name()->GetAsIdString();
      *original_wrapper_name = name.ToString();
      static const IdStringHashMapCase<std::string>* name_to_function =
          new IdStringHashMapCase<std::string>{
              {kKey, kProtoMapAtKey},
              {kOffset, kArrayAtOffset},
              {kOrdinal, kArrayAtOrdinal},
              {kSafeKey, kSafeProtoMapAtKey},

              {kSafeOffset, kSafeArrayAtOffset},
              {kSafeOrdinal, kSafeArrayAtOrdinal}};
      const std::string* function_name_str =
          googlesql_base::FindOrNull(*name_to_function, name);
      if (function_name_str != nullptr) {
        *function_name = *function_name_str;
        *unwrapped_ast_position_expr = ast_function_call->arguments()[0];
      }
    }
  }

  if (*unwrapped_ast_position_expr == nullptr) {
    if (!language().LanguageFeatureEnabled(FEATURE_BARE_ARRAY_ACCESS)) {
      return MakeSqlErrorAt(ast_position)
             << "Array element access with array[position] is not supported. "
                "Use array[OFFSET(zero_based_offset)] or "
                "array[ORDINAL(one_based_ordinal)]";
    }
    if (IsProtoMap(resolved_array->type())) {
      if (language().LanguageFeatureEnabled(FEATURE_PROTO_MAPS)) {
        *function_name = kProtoMapAtKey;
      } else if (
          absl::GetFlag(
              FLAGS_googlesql_suppress_proto_map_bare_array_subscript_error)) {
        GOOGLESQL_RETURN_IF_ERROR(AddDeprecationWarning(
            ast_position, DeprecationWarning::DEPRECATED_FUNCTION_SIGNATURE,
            "Array element access with array[offset] will soon not be "
            "supported on arrays of protocol buffer map entries and this query "
            "will break. Use array[OFFSET(offset)] instead"));
        *function_name = kArrayAtOffset;
      } else {
        return MakeSqlErrorAt(ast_position)
               << "Array element access with array[offset] is not supported "
                  "on arrays of protocol buffer map entries. Use "
                  "array[OFFSET(offset)].";
      }
    } else {
      *function_name = kArrayAtOffset;
    }
    *unwrapped_ast_position_expr = ast_position;
  }

  // The offset/ordinal expression itself is like a function arg. It is not
  // included in the current flattening.
  FlattenState::Restorer restorer;
  expr_resolution_info->flatten_state.set_can_flatten(false, &restorer);
  GOOGLESQL_RETURN_IF_ERROR(ResolveExpr(*unwrapped_ast_position_expr,
                              expr_resolution_info, resolved_expr_out));

  const bool is_map_at =
      *function_name == kProtoMapAtKey || *function_name == kSafeProtoMapAtKey;

  // Coerce to INT64 if necessary.
  if (!is_map_at) {
    GOOGLESQL_RETURN_IF_ERROR(CoerceExprToType(
        *unwrapped_ast_position_expr, types::Int64Type(), kImplicitCoercion,
        "Array position in [] must be coercible to $0 type, but has type $1",
        resolved_expr_out));
  }

  if (is_map_at) {
    if (!IsProtoMap(resolved_array->type())) {
      return MakeSqlErrorAt(ast_position)
             << "Only proto maps can be accessed using KEY or SAFE_KEY; tried "
                "to use map accessor on "
             << resolved_array->type()->ShortTypeName(product_mode());
    }

    const ProtoType* proto_type =
        resolved_array->type()->AsArray()->element_type()->AsProto();

    const Type* key_type;
    GOOGLESQL_RETURN_IF_ERROR(type_factory_->GetProtoFieldType(
        proto_type->map_key(), proto_type->CatalogNamePath(), &key_type));
    GOOGLESQL_RETURN_IF_ERROR(CoerceExprToType(
        *unwrapped_ast_position_expr, key_type, kImplicitCoercion,
        "Map key in [] must be coercible to type $0, but has type $1",
        resolved_expr_out));
  }

  return absl::OkStatus();
}

// TODO: The noinline attribute is to prevent the stack usage
// being added to its caller "Resolver::ResolveExpr" which is a recursive
// function. Now the attribute has to be added for all callees. Hopefully
// the fix allows replacing these with one attribute on ResolveExpr.
ABSL_ATTRIBUTE_NOINLINE
absl::Status Resolver::ResolveCaseNoValueExpression(
    const ASTCaseNoValueExpression* case_no_value,
    ExprResolutionInfo* expr_resolution_info,
    std::unique_ptr<const ResolvedExpr>* resolved_expr_out) {
  RETURN_ERROR_IF_OUT_OF_STACK_SPACE();

  // CASE has a side effects scope.
  side_effect_scope_depth_++;
  absl::Cleanup cleanup = [this] { side_effect_scope_depth_--; };

  std::vector<std::unique_ptr<const ResolvedExpr>> resolved_arguments;
  std::vector<const ASTNode*> ast_arguments;
  GOOGLESQL_RETURN_IF_ERROR(ResolveExpressionArguments(
      expr_resolution_info, case_no_value->arguments(), {}, &resolved_arguments,
      &ast_arguments));

  if (case_no_value->arguments().size() % 2 == 0) {
    // Missing an ELSE expression.  Add a NULL literal to the arguments
    // for resolution. The literal has no parse location.
    resolved_arguments.push_back(
        MakeResolvedLiteralWithoutLocation(Value::NullInt64()));
    // Need a location for the added NULL, but it probably won't ever be used.
    ast_arguments.push_back(case_no_value);
  }

  return ResolveFunctionCallWithResolvedArguments(
      case_no_value, ast_arguments, /*match_internal_signatures=*/false,
      "$case_no_value", std::move(resolved_arguments), /*named_arguments=*/{},
      expr_resolution_info, resolved_expr_out);
}

// TODO: The noinline attribute is to prevent the stack usage
// being added to its caller "Resolver::ResolveExpr" which is a recursive
// function. Now the attribute has to be added for all callees. Hopefully
// the fix allows replacing these with one attribute on ResolveExpr.
ABSL_ATTRIBUTE_NOINLINE
absl::Status Resolver::ResolveCaseValueExpression(
    const ASTCaseValueExpression* case_value,
    ExprResolutionInfo* expr_resolution_info,
    std::unique_ptr<const ResolvedExpr>* resolved_expr_out) {
  RETURN_ERROR_IF_OUT_OF_STACK_SPACE();

  // CASE has a side effects scope
  side_effect_scope_depth_++;
  absl::Cleanup cleanup = [this] { side_effect_scope_depth_--; };

  std::vector<std::unique_ptr<const ResolvedExpr>> resolved_arguments;
  std::vector<const ASTNode*> ast_arguments;
  GOOGLESQL_RETURN_IF_ERROR(
      ResolveExpressionArguments(expr_resolution_info, case_value->arguments(),
                                 {}, &resolved_arguments, &ast_arguments));

  if (case_value->arguments().size() % 2 == 1) {
    // Missing an ELSE expression.  Add a NULL literal to the arguments
    // for resolution. No parse location.
    resolved_arguments.push_back(
        MakeResolvedLiteralWithoutLocation(Value::NullInt64()));
    // Need a location for the added NULL, but it probably won't ever be used.
    ast_arguments.push_back(case_value);
  }

  return ResolveFunctionCallWithResolvedArguments(
      case_value, ast_arguments, /*match_internal_signatures=*/false,
      "$case_with_value", std::move(resolved_arguments), /*named_arguments=*/{},
      expr_resolution_info, resolved_expr_out);
}

// TODO: The noinline attribute is to prevent the stack usage
// being added to its caller "Resolver::ResolveExpr" which is a recursive
// function. Now the attribute has to be added for all callees. Hopefully
// the fix allows replacing these with one attribute on ResolveExpr.
ABSL_ATTRIBUTE_NOINLINE
absl::Status Resolver::ResolveExtractExpression(
    const ASTExtractExpression* extract_expression,
    ExprResolutionInfo* expr_resolution_info,
    std::unique_ptr<const ResolvedExpr>* resolved_expr_out) {
  RETURN_ERROR_IF_OUT_OF_STACK_SPACE();
  std::vector<std::unique_ptr<const ResolvedExpr>> resolved_arguments;
  std::vector<const ASTNode*> arg_locations;

  GOOGLESQL_RETURN_IF_ERROR(ResolveExpressionArgument(extract_expression->rhs_expr(),
                                            expr_resolution_info,
                                            &resolved_arguments));
  arg_locations.push_back(extract_expression->rhs_expr());

  if (resolved_arguments[0]->type()->IsProto()) {
    if (!language().LanguageFeatureEnabled(FEATURE_EXTRACT_FROM_PROTO)) {
      return MakeSqlErrorAt(extract_expression)
             << "EXTRACT from PROTO is not supported";
    }
    if (extract_expression->time_zone_expr() != nullptr) {
      return MakeSqlErrorAt(extract_expression)
             << "EXTRACT from PROTO does not support AT TIME ZONE";
    }
    return ResolveProtoExtractExpression(extract_expression->lhs_expr(),
                                         std::move(resolved_arguments[0]),
                                         resolved_expr_out);
  }

  functions::DateTimestampPart date_part;
  std::unique_ptr<const ResolvedExpr> resolved_date_part_argument;
  absl::Status resolved_datepart_status = ResolveDatePartArgument(
      extract_expression->lhs_expr(), &resolved_date_part_argument, &date_part);
  if (!resolved_datepart_status.ok()) {
    // We prioritize invalid argument type error over invalid date part
    // name error for the EXTRACT function.
    if (!resolved_arguments[0]->type()->IsTimestamp() &&
        !resolved_arguments[0]->type()->IsCivilDateOrTimeType() &&
        !resolved_arguments[0]->type()->IsProto()) {
      return MakeSqlErrorAt(extract_expression->rhs_expr())
             << "EXTRACT does not support arguments of type: "
             << resolved_arguments[0]->type()->ShortTypeName(product_mode());
    }
    return resolved_datepart_status;
  }

  // If extracting the DATE date part, the TIME date part, or the
  // DATETIME date part, check the date part is compatible, and they will be
  // eventually resolved as '$extract_date', '$extract_time' and
  // '$extract_datetime', respectively; Otherwise, add the date part as an
  // argument for '$extract'.
  if (date_part == functions::DATE || date_part == functions::TIME) {
    if (date_part == functions::TIME &&
        !language().LanguageFeatureEnabled(FEATURE_CIVIL_TIME)) {
      return MakeSqlErrorAt(extract_expression->lhs_expr())
             << "Unknown date part 'TIME'";
    }
    if (resolved_arguments[0]->type()->IsDate() ||
        resolved_arguments[0]->type()->IsTime()) {
      // We cannot extract a DATE or TIME from a DATE or TIME, provide a custom
      // error message consistent with CheckExtractPostResolutionArguments().
      return MakeSqlErrorAt(extract_expression)
             << ExtractingNotSupportedDatePart(
                    resolved_arguments[0]->type()->ShortTypeName(
                        product_mode()),
                    functions::DateTimestampPartToSQL(date_part));
    }
  } else if (date_part == functions::DATETIME) {
    if (!language().LanguageFeatureEnabled(FEATURE_CIVIL_TIME)) {
      return MakeSqlErrorAt(extract_expression->lhs_expr())
             << "Unknown date part 'DATETIME'";
    }
    if (!resolved_arguments[0]->type()->IsTimestamp()) {
      // We can only extract a DATETIME from a TIMESTAMP, provide a custom
      // error message consistent with CheckExtractPostResolutionArguments().
      return MakeSqlErrorAt(extract_expression)
             << ExtractingNotSupportedDatePart(
                    resolved_arguments[0]->type()->ShortTypeName(
                        product_mode()),
                    "DATETIME");
    }
  } else {
    resolved_arguments.push_back(std::move(resolved_date_part_argument));
    arg_locations.push_back(extract_expression->lhs_expr());
  }

  if (extract_expression->time_zone_expr() != nullptr) {
    // TODO: Consider always adding the 'name' of the default time
    // zone to the arguments if time_zone_expr() == nullptr and make the
    // time zone argument required, rather than let it be optional as it is
    // now (so engines must use their default time zone if not explicitly in
    // the query).  The default absl::TimeZone in the analyzer options has a
    // Name() that can be used if necessary.  Current implementations do not
    // support the time zone argument yet, so requiring it will break them
    // until the new argument is supported in all the implementations.
    if (resolved_arguments[0]->type()->IsCivilDateOrTimeType()) {
      return MakeSqlErrorAt(extract_expression)
             << "EXTRACT from "
             << resolved_arguments[0]->type()->ShortTypeName(product_mode())
             << " does not support AT TIME ZONE";
    }
    GOOGLESQL_RETURN_IF_ERROR(
        ResolveExpressionArgument(extract_expression->time_zone_expr(),
                                  expr_resolution_info, &resolved_arguments));
    arg_locations.push_back(extract_expression->time_zone_expr());
  }

  std::string function_name;
  switch (date_part) {
    case functions::DATE:
      function_name = "$extract_date";
      break;
    case functions::TIME:
      function_name = "$extract_time";
      break;
    case functions::DATETIME:
      function_name = "$extract_datetime";
      break;
    case functions::YEAR:
    case functions::MONTH:
    case functions::DAY:
    case functions::DAYOFWEEK:
    case functions::DAYOFYEAR:
    case functions::QUARTER:
    case functions::HOUR:
    case functions::MINUTE:
    case functions::SECOND:
    case functions::MILLISECOND:
    case functions::MICROSECOND:
    case functions::NANOSECOND:
    case functions::PICOSECOND:
    case functions::WEEK:
    case functions::WEEK_MONDAY:
    case functions::WEEK_TUESDAY:
    case functions::WEEK_WEDNESDAY:
    case functions::WEEK_THURSDAY:
    case functions::WEEK_FRIDAY:
    case functions::WEEK_SATURDAY:
    case functions::ISOWEEK:
    case functions::ISOYEAR:
      function_name = "$extract";
      break;
    default:
      break;
  }
  // ABSL_CHECK should never fail because the date_part should always be valid now.
  GOOGLESQL_RET_CHECK(!function_name.empty());

  return ResolveFunctionCallWithResolvedArguments(
      extract_expression, arg_locations, /*match_internal_signatures=*/false,
      function_name, std::move(resolved_arguments), /*named_arguments=*/{},
      expr_resolution_info, resolved_expr_out);
}

// TODO: The noinline attribute is to prevent the stack usage
// being added to its caller "Resolver::ResolveExpr" which is a recursive
// function. Now the attribute has to be added for all callees. Hopefully
// the fix allows replacing these with one attribute on ResolveExpr.
ABSL_ATTRIBUTE_NOINLINE
absl::Status Resolver::ResolveNewConstructor(
    const ASTNewConstructor* ast_new_constructor,
    ExprResolutionInfo* expr_resolution_info,
    std::unique_ptr<const ResolvedExpr>* resolved_expr_out) {
  RETURN_ERROR_IF_OUT_OF_STACK_SPACE();

  const Type* resolved_type;
  GOOGLESQL_RET_CHECK(ast_new_constructor->type_name()->type_parameters() == nullptr)
      << "The parser does not support type parameters in new constructor "
         "syntax";
  GOOGLESQL_RET_CHECK(ast_new_constructor->type_name()->collate() == nullptr)
      << "The parser does not support type with collation name in new "
         "constructor syntax";
  GOOGLESQL_RETURN_IF_ERROR(ResolveSimpleType(ast_new_constructor->type_name(),
                                    {.context = "new constructors"},
                                    &resolved_type,
                                    /*resolved_type_modifiers=*/nullptr));

  if (!resolved_type->IsProto()) {
    return MakeSqlErrorAt(ast_new_constructor->type_name())
           << "NEW constructors are not allowed for type "
           << resolved_type->ShortTypeName(product_mode());
  }

  std::vector<ResolvedBuildProtoArg> arguments;
  for (int i = 0; i < ast_new_constructor->arguments().size(); ++i) {
    const ASTNewConstructorArg* ast_arg = ast_new_constructor->argument(i);

    std::unique_ptr<AliasOrASTPathExpression> alias_or_ast_path_expr;
    if (ast_arg->optional_identifier() != nullptr) {
      alias_or_ast_path_expr = std::make_unique<AliasOrASTPathExpression>(
          ast_arg->optional_identifier()->GetAsIdString());
    } else if (ast_arg->optional_path_expression() != nullptr) {
      if (!language().LanguageFeatureEnabled(
              FEATURE_PROTO_EXTENSIONS_WITH_NEW)) {
        return MakeSqlErrorAt(ast_arg->optional_path_expression())
               << "NEW constructor does not support proto extensions";
      }
      alias_or_ast_path_expr = std::make_unique<AliasOrASTPathExpression>(
          ast_arg->optional_path_expression());
    } else {
      alias_or_ast_path_expr = std::make_unique<AliasOrASTPathExpression>(
          GetAliasForExpression(ast_arg->expression()));
    }
    if (alias_or_ast_path_expr->kind() == AliasOrASTPathExpression::ALIAS) {
      const IdString alias = alias_or_ast_path_expr->alias();
      if (alias.empty() || IsInternalAlias(alias)) {
        return MakeSqlErrorAt(ast_arg)
               << "Cannot construct proto because argument " << (i + 1)
               << " does not have an alias to match to a field name";
      }
    }

    GOOGLESQL_ASSIGN_OR_RETURN(
        const google::protobuf::FieldDescriptor* field_descriptor,
        FindFieldDescriptor(resolved_type->AsProto()->descriptor(),
                            *alias_or_ast_path_expr, ast_arg, i, "Argument"));
    GOOGLESQL_ASSIGN_OR_RETURN(
        const Type* type,
        FindProtoFieldType(field_descriptor, ast_arg,
                           resolved_type->AsProto()->CatalogNamePath()));

    std::unique_ptr<const ResolvedExpr> expr;
    GOOGLESQL_RETURN_IF_ERROR(
        ResolveExpr(ast_arg->expression(), expr_resolution_info, &expr, type));

    arguments.emplace_back(
        ast_arg->expression(), std::move(expr), type,
        std::vector<const google::protobuf::FieldDescriptor*>{field_descriptor});
  }

  GOOGLESQL_RETURN_IF_ERROR(ResolveBuildProto(
      ast_new_constructor->type_name(), resolved_type->AsProto(),
      /*input_scan=*/nullptr, "Argument", "Constructor", &arguments,
      resolved_expr_out));
  return absl::OkStatus();
}

absl::StatusOr<Resolver::BracedConstructorField>
Resolver::ResolveBracedConstructorLhs(
    const ASTBracedConstructorLhs* ast_braced_constructor_lhs,
    const ProtoType* parent_type) {
  RETURN_ERROR_IF_OUT_OF_STACK_SPACE();

  BracedConstructorField field;
  GOOGLESQL_ASSIGN_OR_RETURN(
      Resolver::FindFieldsOutput output,
      FindFieldsFromPathExpression(
          "BracedConstructor", ast_braced_constructor_lhs->extended_path_expr(),
          parent_type, /*can_traverse_array_fields=*/true));
  field.location = ast_braced_constructor_lhs->extended_path_expr();
  field.field_info = output;

  return field;
}

absl::StatusOr<Resolver::ResolvedBuildProtoArg>
Resolver::ResolveBracedConstructorField(
    const ASTBracedConstructorField* ast_braced_constructor_field,
    const ProtoType* parent_type, int field_index, bool allow_field_paths,
    const ResolvedExpr* update_constructor_expr_to_modify,
    ExprResolutionInfo* expr_resolution_info) {
  RETURN_ERROR_IF_OUT_OF_STACK_SPACE();

  const ASTBracedConstructorLhs* braced_constructor_lhs =
      ast_braced_constructor_field->braced_constructor_lhs();
  GOOGLESQL_ASSIGN_OR_RETURN(
      BracedConstructorField field,
      ResolveBracedConstructorLhs(braced_constructor_lhs, parent_type));
  const std::vector<const google::protobuf::FieldDescriptor*>& field_descriptor_path =
      field.field_info.field_descriptor_path;
  if (field_descriptor_path.empty()) {
    GOOGLESQL_RET_CHECK_FAIL() << "Cannot construct proto because field "
                     << (field_index + 1)
                     << " does not specify field name/extension path. "
                     << "This should be a parser error.";
  }
  if (!allow_field_paths && field_descriptor_path.size() > 1) {
    return MakeSqlErrorAt(field.location)
           << "Braced constructor supports only singular field paths, "
           << "nested paths should use additional sets of braces";
  }

  const google::protobuf::FieldDescriptor* leaf_field_descriptor =
      field_descriptor_path.back();
  GOOGLESQL_ASSIGN_OR_RETURN(const Type* lhs_type,
                   FindProtoFieldType(leaf_field_descriptor, field.location,
                                      parent_type->CatalogNamePath()));
  // Resolve the field value.
  GOOGLESQL_RET_CHECK(ast_braced_constructor_field->value());
  GOOGLESQL_RET_CHECK(ast_braced_constructor_field->value()->expression());
  const ASTExpression* field_value =
      ast_braced_constructor_field->value()->expression();
  // If we are in an update and we have a braced constructor as the value we
  // interpret it as an update constructor i.e.
  //
  // UPDATE (x) {
  //   a: 3
  //   b { c: 4 }
  // }
  // is interpreted as:
  // UPDATE (x) {
  //   a: 3
  //   b: UPDATE(x.b) { c: 4 }
  // }
  //
  // We do this check here because we want only immediate children to be
  // converted to UPDATE. For example in UPDATE (x) { a: [{c: 4}] }, the inner
  // braced constructor should not be converted to UPDATE.
  std::unique_ptr<const ResolvedExpr> expr;
  if (update_constructor_expr_to_modify != nullptr &&
      field_value->node_kind() == AST_BRACED_CONSTRUCTOR) {
    GOOGLESQL_ASSIGN_OR_RETURN(
        std::unique_ptr<const ResolvedExpr> proto_expr,
        ResolveProtoFieldAccess(*update_constructor_expr_to_modify,
                                field_value->location(),
                                *braced_constructor_lhs->extended_path_expr(),
                                &expr_resolution_info->flatten_state));
    GOOGLESQL_ASSIGN_OR_RETURN(expr, ResolveBracedConstructorInUpdateContext(
                               *field_value, std::move(proto_expr),
                               /*alias=*/"",
                               *field_value->GetAsOrDie<ASTBracedConstructor>(),
                               expr_resolution_info));
  } else {
    GOOGLESQL_RETURN_IF_ERROR(
        ResolveExpr(field_value, expr_resolution_info, &expr, lhs_type));
  }

  return ResolvedBuildProtoArg(field.location, std::move(expr), lhs_type,
                               field_descriptor_path);
}

// TODO: The noinline attribute is to prevent the stack usage
// being added to its caller "Resolver::ResolveExpr" which is a recursive
// function. Now the attribute has to be added for all callees. Hopefully
// the fix allows replacing these with one attribute on ResolveExpr.
ABSL_ATTRIBUTE_NOINLINE
absl::StatusOr<std::unique_ptr<const ResolvedExpr>>
Resolver::ResolveProtoFieldAccess(
    const ResolvedExpr& resolved_base, const ParseLocationRange& parse_location,
    const ASTGeneralizedPathExpression& generalized_path,
    FlattenState* flatten_state) {
  RETURN_ERROR_IF_OUT_OF_STACK_SPACE();

  switch (generalized_path.node_kind()) {
    case AST_PATH_EXPRESSION: {
      const ASTPathExpression* path_expression =
          generalized_path.GetAs<ASTPathExpression>();
      ResolvedASTDeepCopyVisitor deep_copy_visitor;
      GOOGLESQL_RETURN_IF_ERROR(resolved_base.Accept(&deep_copy_visitor));
      std::unique_ptr<const ResolvedExpr> resolved_expr_out;
      GOOGLESQL_ASSIGN_OR_RETURN(resolved_expr_out,
                       deep_copy_visitor.ConsumeRootNode<ResolvedExpr>());
      if (generalized_path.parenthesized()) {
        GOOGLESQL_RETURN_IF_ERROR(ResolveExtensionFieldAccess(
            std::move(resolved_expr_out), ResolveExtensionFieldOptions(),
            path_expression, flatten_state, &resolved_expr_out));
        return resolved_expr_out;
      } else {
        for (const ASTIdentifier* identifier : path_expression->names()) {
          GOOGLESQL_RETURN_IF_ERROR(ResolveFieldAccess(
              std::move(resolved_expr_out), parse_location, identifier,
              flatten_state, &resolved_expr_out));
        }
      }
      return resolved_expr_out;
    }
    case AST_DOT_IDENTIFIER: {
      const ASTDotIdentifier* dot_identifier =
          generalized_path.GetAs<ASTDotIdentifier>();
      const ASTGeneralizedPathExpression* lhs =
          dot_identifier->expr()->GetAsOrNull<ASTGeneralizedPathExpression>();
      if (lhs == nullptr) {
        return MakeSqlErrorAt(dot_identifier->expr())
               << "Path expression expected, instead found: "
               << dot_identifier->expr()->DebugString();
      }

      GOOGLESQL_ASSIGN_OR_RETURN(std::unique_ptr<const ResolvedExpr> resolved_expr_out,
                       ResolveProtoFieldAccess(resolved_base, parse_location,
                                               *lhs, flatten_state));
      GOOGLESQL_RETURN_IF_ERROR(ResolveFieldAccess(std::move(resolved_expr_out),
                                         parse_location, dot_identifier->name(),
                                         flatten_state, &resolved_expr_out));
      return resolved_expr_out;
    }
    case AST_DOT_GENERALIZED_FIELD: {
      const ASTDotGeneralizedField* dot_generalized =
          generalized_path.GetAs<ASTDotGeneralizedField>();
      const ASTGeneralizedPathExpression* lhs =
          dot_generalized->expr()->GetAsOrNull<ASTGeneralizedPathExpression>();
      if (lhs == nullptr) {
        return MakeSqlErrorAt(dot_generalized->expr())
               << "Path expression expected, instead found: "
               << dot_generalized->expr()->DebugString();
      }

      GOOGLESQL_ASSIGN_OR_RETURN(std::unique_ptr<const ResolvedExpr> resolved_expr_out,
                       ResolveProtoFieldAccess(resolved_base, parse_location,
                                               *lhs, flatten_state));
      GOOGLESQL_RETURN_IF_ERROR(ResolveExtensionFieldAccess(
          std::move(resolved_expr_out), ResolveExtensionFieldOptions(),
          dot_generalized->path(), flatten_state, &resolved_expr_out));
      return resolved_expr_out;
    }
    case AST_EXTENDED_PATH_EXPRESSION: {
      const ASTExtendedPathExpression* extended_path_expression =
          generalized_path.GetAs<ASTExtendedPathExpression>();
      const ASTGeneralizedPathExpression* lhs =
          extended_path_expression->parenthesized_path()
              ->GetAsOrNull<ASTGeneralizedPathExpression>();
      if (lhs == nullptr) {
        return MakeSqlErrorAt(extended_path_expression->parenthesized_path())
               << "Path expression expected, instead found: "
               << extended_path_expression->parenthesized_path()->DebugString();
      }
      GOOGLESQL_ASSIGN_OR_RETURN(std::unique_ptr<const ResolvedExpr> resolved_expr_out,
                       ResolveProtoFieldAccess(resolved_base, parse_location,
                                               *lhs, flatten_state));
      GOOGLESQL_RET_CHECK(extended_path_expression->generalized_path_expression());
      return ResolveProtoFieldAccess(
          *resolved_expr_out, parse_location,
          *extended_path_expression->generalized_path_expression(),
          flatten_state);
    }
    default:
      return MakeSqlErrorAt(&generalized_path)
             << "Unimplemented node type while resolving proto field path: "
             << generalized_path.DebugString();
  }
}

// TODO: The noinline attribute is to prevent the stack usage
// being added to its caller "Resolver::ResolveExpr" which is a recursive
// function. Now the attribute has to be added for all callees. Hopefully
// the fix allows replacing these with one attribute on ResolveExpr.
ABSL_ATTRIBUTE_NOINLINE
absl::Status Resolver::ResolveBracedConstructor(
    const ASTBracedConstructor* ast_braced_constructor,
    const Type* inferred_type, ExprResolutionInfo* expr_resolution_info,
    std::unique_ptr<const ResolvedExpr>* resolved_expr_out) {
  RETURN_ERROR_IF_OUT_OF_STACK_SPACE();
  // This function is called for bare brace, we don't know whether the brace is
  // struct or proto, so we need to derive from the infer_type.
  if (inferred_type == nullptr) {
    return MakeSqlErrorAt(ast_braced_constructor)
           << "Unable to infer a type for braced constructor";
  }

  if (inferred_type->IsStruct()) {  // bare braced
    return ResolveBracedConstructorForStruct(
        ast_braced_constructor, /*is_bare_struct*/ false,
        /*expression_location_node*/ ast_braced_constructor,
        /*ast_struct_type*/ nullptr, inferred_type, expr_resolution_info,
        resolved_expr_out);
  } else if (inferred_type->IsProto()) {
    return ResolveBracedConstructorForProto(ast_braced_constructor,
                                            inferred_type, expr_resolution_info,
                                            resolved_expr_out);
  }
  // Neither Proto or Struct
  return MakeSqlErrorAt(ast_braced_constructor)
         << "Braced constructors are not allowed for type "
         << inferred_type->ShortTypeName(product_mode());
}

absl::Status Resolver::ResolveBracedConstructorForStruct(
    const ASTBracedConstructor* ast_braced_constructor, bool is_bare_struct,
    const ASTNode* expression_location_node,
    const ASTStructType* ast_struct_type, const Type* inferred_type,
    ExprResolutionInfo* expr_resolution_info,
    std::unique_ptr<const ResolvedExpr>* resolved_expr_out) {
  std::vector<const ASTIdentifier*> identifiers;
  identifiers.reserve(ast_braced_constructor->fields().size());
  std::vector<const ASTExpression*> field_expressions;
  field_expressions.reserve(ast_braced_constructor->fields().size());
  bool is_first_field = true;
  for (const ASTBracedConstructorField* ast_field :
       ast_braced_constructor->fields()) {
    // Pure whiteSpace separation for fields is not allowed in STRUCT.
    // comma_separated() records whether this record uses comma or pure
    // whitespace to separate with the previous records, for the 1st record,
    // leading comma is not allowed, so comma_separated() is always false, and
    // it's allowed.
    if (!is_first_field && !ast_field->comma_separated()) {
      return MakeSqlErrorAt(ast_field)
             << "STRUCT Braced constructor is not allowed to use pure "
                "whitespace separation, please use comma instead";
    }
    if (is_first_field) {
      is_first_field = false;
    }
    const ASTGeneralizedPathExpression* generalized_path =
        ast_field->braced_constructor_lhs()->extended_path_expr();
    if (generalized_path->node_kind() != AST_PATH_EXPRESSION ||
        generalized_path->GetAsOrDie<ASTPathExpression>()->num_names() != 1) {
      return MakeSqlErrorAt(ast_field)
             << "Fields in STRUCT Braced constructor should always have a "
                "single identifier specified, not a path expression";
    }
    identifiers.push_back(
        generalized_path->GetAsOrDie<ASTPathExpression>()->first_name());
    field_expressions.push_back(ast_field->value()->expression());
  }
  // Skip name matching for all bare struct.
  // Discussion: http://shortn/_XFezKJYuR7
  GOOGLESQL_RETURN_IF_ERROR(ResolveStructConstructorImpl(
      expression_location_node, ast_struct_type, field_expressions, identifiers,
      inferred_type, /*require_name_match=*/!is_bare_struct,
      expr_resolution_info, resolved_expr_out));
  return absl::OkStatus();
}

absl::Status Resolver::ResolveBracedConstructorForProto(
    const ASTBracedConstructor* ast_braced_constructor,
    const Type* inferred_type, ExprResolutionInfo* expr_resolution_info,
    std::unique_ptr<const ResolvedExpr>* resolved_expr_out) {
  // This is a proto braced constructor.
  std::vector<ResolvedBuildProtoArg> resolved_build_proto_args;
  for (int i = 0; i < ast_braced_constructor->fields().size(); ++i) {
    const ASTBracedConstructorField* ast_field =
        ast_braced_constructor->fields(i);

    GOOGLESQL_ASSIGN_OR_RETURN(ResolvedBuildProtoArg arg,
                     ResolveBracedConstructorField(
                         ast_field, inferred_type->AsProto(), i,
                         /*allow_field_paths=*/false,
                         /*update_constructor_expr_to_modify=*/nullptr,
                         expr_resolution_info));
    resolved_build_proto_args.emplace_back(std::move(arg));
  }

  GOOGLESQL_RETURN_IF_ERROR(
      ResolveBuildProto(ast_braced_constructor, inferred_type->AsProto(),
                        /*input_scan=*/nullptr, "Argument", "Constructor",
                        &resolved_build_proto_args, resolved_expr_out));
  return absl::OkStatus();
}

// TODO: The noinline attribute is to prevent the stack usage
// being added to its caller "Resolver::ResolveExpr" which is a recursive
// function. Now the attribute has to be added for all callees. Hopefully
// the fix allows replacing these with one attribute on ResolveExpr.
ABSL_ATTRIBUTE_NOINLINE
absl::Status Resolver::ResolveBracedNewConstructor(
    const ASTBracedNewConstructor* ast_braced_new_constructor,
    ExprResolutionInfo* expr_resolution_info,
    std::unique_ptr<const ResolvedExpr>* resolved_expr_out) {
  RETURN_ERROR_IF_OUT_OF_STACK_SPACE();

  const Type* resolved_type;
  GOOGLESQL_RET_CHECK(ast_braced_new_constructor->type_name()->type_parameters() ==
            nullptr)
      << "The parser does not support type parameters in new constructor "
         "syntax";
  GOOGLESQL_RET_CHECK(ast_braced_new_constructor->type_name()->collate() == nullptr)
      << "The parser does not support type with collation name in new "
         "constructor syntax";
  GOOGLESQL_RETURN_IF_ERROR(ResolveSimpleType(ast_braced_new_constructor->type_name(),
                                    {.context = "new braced constructor"},
                                    &resolved_type,
                                    /*resolved_type_modifiers=*/nullptr));

  if (!resolved_type->IsProto()) {
    return MakeSqlErrorAt(ast_braced_new_constructor->type_name())
           << "Braced NEW constructors are not allowed for type "
           << resolved_type->ShortTypeName(product_mode());
  }

  GOOGLESQL_RETURN_IF_ERROR(ResolveBracedConstructorForProto(
      ast_braced_new_constructor->braced_constructor(), resolved_type,
      expr_resolution_info, resolved_expr_out));

  return absl::OkStatus();
}

absl::Status Resolver::ResolveStructBracedConstructor(
    const ASTStructBracedConstructor* ast_struct_braced_constructor,
    const Type* inferred_type, ExprResolutionInfo* expr_resolution_info,
    std::unique_ptr<const ResolvedExpr>* resolved_expr_out) {
  RETURN_ERROR_IF_OUT_OF_STACK_SPACE();

  const ASTType* type_name = ast_struct_braced_constructor->type_name();
  const ASTStructType* ast_struct_type = nullptr;
  bool is_bare_struct = true;
  if (type_name) {
    is_bare_struct = false;
    // STRUCT<...> { ... } with explicit type, use the explicit type.
    ast_struct_type = type_name->GetAsOrNull<ASTStructType>();
    GOOGLESQL_RET_CHECK_NE(ast_struct_type, nullptr)
        << " STRUCT Braced constructors are not allowed for type "
        << type_name->DebugString() << ", which should be a struct";
    GOOGLESQL_RET_CHECK(type_name->type_parameters() == nullptr)
        << "The parser does not support type parameters in braced STRUCT "
           "constructor syntax";
    GOOGLESQL_RET_CHECK(type_name->collate() == nullptr)
        << "The parser does not support type with collation name in braced "
           "STRUCT constructor syntax";
  }

  GOOGLESQL_RETURN_IF_ERROR(ResolveBracedConstructorForStruct(
      ast_struct_braced_constructor->braced_constructor(), is_bare_struct,
      ast_struct_braced_constructor, ast_struct_type, inferred_type,
      expr_resolution_info, resolved_expr_out));

  return absl::OkStatus();
}

// TODO: The noinline attribute is to prevent the stack usage
// being added to its caller "Resolver::ResolveExpr" which is a recursive
// function. Now the attribute has to be added for all callees. Hopefully
// the fix allows replacing these with one attribute on ResolveExpr.
ABSL_ATTRIBUTE_NOINLINE
absl::Status Resolver::ResolveUpdateConstructorProtoExpression(
    const ASTUpdateConstructor& ast_update_constructor,
    ExprResolutionInfo* expr_resolution_info, std::string& alias,
    std::unique_ptr<const ResolvedExpr>& expr_to_modify) {
  const ASTFunctionCall* function = ast_update_constructor.function();
  GOOGLESQL_RET_CHECK(function);
  const ASTPathExpression* function_name_path = function->function();
  GOOGLESQL_RET_CHECK(function_name_path);
  bool is_function_name_update =
      function_name_path->names().size() == 1 &&
      absl::AsciiStrToUpper(
          function_name_path->first_name()->GetAsStringView()) == "UPDATE";
  bool is_child_of_braced_constructor =
      ast_update_constructor.parent() != nullptr &&
      ast_update_constructor.parent()->Is<ASTBracedConstructorFieldValue>();
  if (!language().LanguageFeatureEnabled(FEATURE_UPDATE_CONSTRUCTOR)) {
    if (is_child_of_braced_constructor) {
      return MakeSqlErrorAt(ast_update_constructor.braced_constructor())
             << "Unexpected braced syntax after call to function '"
             << function_name_path->first_name()->GetAsStringView()
             << "'. If the intent is to set an extension field, the preceding "
                "field must include a trailing comma.";
    } else {
      return MakeSqlErrorAt(&ast_update_constructor)
             << "UPDATE constructor is not supported";
    }
  }

  if (!is_function_name_update) {
    if (is_child_of_braced_constructor) {
      return MakeSqlErrorAt(ast_update_constructor.braced_constructor())
             << "Unexpected braced syntax after call to function '"
             << function_name_path->first_name()->GetAsStringView()
             << "'. If the intent is to set an extension field, the preceding "
                "field must include a trailing comma. If the intent is to "
                "invoke "
                "the UPDATE constructor for a proto, the function name is "
                "misspelled.";
    } else {
      return MakeSqlErrorAt(ast_update_constructor.braced_constructor())
             << "Unexpected braced syntax after call to function '"
             << function_name_path->first_name()->GetAsStringView()
             << "'. If the "
                "intent is to invoke the UPDATE constructor for a proto, it is "
                "misspelled.";
    }
  }

  if (function->arguments().size() != 1) {
    return MakeSqlErrorAt(function)
           << "UPDATE constructor should have a singular proto expression "
              "argument";
  }
  if (function->HasModifiers(/*ignore_is_chained_call=*/true)) {
    return MakeSqlErrorAt(function)
           << "Found invalid UPDATE constructor syntax";
  }
  const ASTExpression* proto_expr = function->arguments(0);
  GOOGLESQL_RET_CHECK(proto_expr);

  if (proto_expr->Is<ASTExpressionWithAlias>()) {
    const ASTExpressionWithAlias* proto_expr_with_alias =
        proto_expr->GetAsOrDie<ASTExpressionWithAlias>();
    alias = proto_expr_with_alias->alias()->GetAsString();
    proto_expr = proto_expr_with_alias->expression();
  }
  return ResolveExpr(proto_expr, expr_resolution_info, &expr_to_modify);
}

// TODO: The noinline attribute is to prevent the stack usage
// being added to its caller "Resolver::ResolveExpr" which is a recursive
// function. Now the attribute has to be added for all callees. Hopefully
// the fix allows replacing these with one attribute on ResolveExpr.
ABSL_ATTRIBUTE_NOINLINE
absl::StatusOr<std::unique_ptr<const ResolvedExpr>>
Resolver::ResolveBracedConstructorInUpdateContext(
    const ASTNode& location, std::unique_ptr<const ResolvedExpr> expr_to_modify,
    std::string alias, const ASTBracedConstructor& ast_braced_constructor,
    ExprResolutionInfo* expr_resolution_info) {
  RETURN_ERROR_IF_OUT_OF_STACK_SPACE();

  if (!expr_to_modify->type()->IsProto()) {
    return MakeSqlErrorAt(&location)
           << "UPDATE constructor expects an input type of proto type, but "
           << "found type "
           << expr_to_modify->type()->ShortTypeName(product_mode());
  }

  const Type* proto_type = expr_to_modify->type();

  std::vector<std::unique_ptr<const ResolvedUpdateFieldItem>>
      resolved_update_field_items;
  if (ast_braced_constructor.fields().empty()) {
    return std::move(expr_to_modify);
  }

  for (int i = 0; i < ast_braced_constructor.fields().size(); ++i) {
    const ASTBracedConstructorField* ast_field =
        ast_braced_constructor.fields(i);

    GOOGLESQL_ASSIGN_OR_RETURN(ResolvedBuildProtoArg arg,
                     ResolveBracedConstructorField(
                         ast_field, proto_type->AsProto(), i,
                         /*allow_field_paths=*/true, expr_to_modify.get(),
                         expr_resolution_info));
    GOOGLESQL_RETURN_IF_ERROR(CoerceExprToType(
        ast_field->value()->expression(), arg.leaf_field_type,
        kImplicitAssignment,
        "Cannot update field of type $0 with value of type $1", &arg.expr));

    ResolvedUpdateFieldItem::Operation operation;
    switch (ast_field->braced_constructor_lhs()->operation()) {
      case ASTBracedConstructorLhs::UPDATE_SINGLE:
        operation = ResolvedUpdateFieldItem::UPDATE_SINGLE;
        break;
      case ASTBracedConstructorLhs::UPDATE_MANY:
        operation = ResolvedUpdateFieldItem::UPDATE_MANY;
        break;
      case ASTBracedConstructorLhs::UPDATE_SINGLE_NO_CREATION:
        operation = ResolvedUpdateFieldItem::UPDATE_SINGLE_NO_CREATION;
        break;
    }
    resolved_update_field_items.push_back(MakeResolvedUpdateFieldItem(
        std::move(arg.expr), arg.field_descriptor_path, operation));
  }

  return MakeResolvedUpdateConstructor(proto_type, std::move(expr_to_modify),
                                       alias,
                                       std::move(resolved_update_field_items));
}

// TODO: The noinline attribute is to prevent the stack usage
// being added to its caller "Resolver::ResolveExpr" which is a recursive
// function. Now the attribute has to be added for all callees. Hopefully
// the fix allows replacing these with one attribute on ResolveExpr.
ABSL_ATTRIBUTE_NOINLINE
absl::Status Resolver::ResolveUpdateConstructor(
    const ASTUpdateConstructor& ast_update_constructor,
    ExprResolutionInfo* expr_resolution_info,
    std::unique_ptr<const ResolvedExpr>* resolved_expr_out) {
  RETURN_ERROR_IF_OUT_OF_STACK_SPACE();

  std::string alias;
  std::unique_ptr<const ResolvedExpr> expr_to_modify;
  GOOGLESQL_RETURN_IF_ERROR(ResolveUpdateConstructorProtoExpression(
      ast_update_constructor, expr_resolution_info, alias, expr_to_modify));

  GOOGLESQL_RET_CHECK(ast_update_constructor.braced_constructor());
  GOOGLESQL_ASSIGN_OR_RETURN(
      *resolved_expr_out,
      ResolveBracedConstructorInUpdateContext(
          ast_update_constructor, std::move(expr_to_modify), std::move(alias),
          *ast_update_constructor.braced_constructor(), expr_resolution_info));
  return absl::OkStatus();
}

// Copies a specific annotation from one AnnotationMap to another.
// Returns the modified target AnnotationMap. If the target was nullptr but the
// new annotation needs to be added, creates a new AnnotationMap and
// internalizes it in `type_factory`, and returns the pointer to that map.
// REQUIRES:
// - The two must have compatible structure.
// - The target annotation must *NOT* exist in the target yet.
// For example: if the input annotation map is <{X:1}, {collation:"und:ci"}>,
// and the target annotation map is <{id:1}, {id:2}>, the output of importing
// collation from the source to the target will be
// <{id:1}, {id:2, collation:"und:ci"}>.
static absl::StatusOr<const AnnotationMap*> ImportSpecificAnnotation(
    int target_annotation_id, const Type* type, const AnnotationMap* source,
    TypeFactory& type_factory, AnnotationMap* target) {
  GOOGLESQL_RET_CHECK(target == nullptr || target->Empty() ||
            target->GetAnnotation(target_annotation_id) == nullptr);

  GOOGLESQL_RET_CHECK(source == nullptr || source->HasCompatibleStructure(type));
  GOOGLESQL_RET_CHECK(target == nullptr || target->HasCompatibleStructure(type));
  if (source == nullptr || source->Empty()) {
    // The source does not have the annotation in itself or any subtrees, so
    // nothing to import. The target stays unchanged.
    return target;
  }

  // Used only if we need to create an annotation map for target to insert the
  // new annotation into.
  std::unique_ptr<AnnotationMap> owned_target;

  const SimpleValue* value_at_current_level =
      source->GetAnnotation(target_annotation_id);
  if (value_at_current_level != nullptr) {
    if (target == nullptr) {
      owned_target = AnnotationMap::Create(type);
      target = owned_target.get();
    }
    target->SetAnnotation(target_annotation_id, *value_at_current_level);
  }

  if (source->IsStructMap()) {
    const StructAnnotationMap* source_struct_map = source->AsStructMap();
    std::vector<const Type*> type_components = type->ComponentTypes();
    GOOGLESQL_RET_CHECK_EQ(type_components.size(), source_struct_map->num_fields());

    for (int i = 0; i < source->AsStructMap()->num_fields(); ++i) {
      GOOGLESQL_ASSIGN_OR_RETURN(
          const AnnotationMap* target_field,
          ImportSpecificAnnotation(
              target_annotation_id, type_components[i],
              source->AsStructMap()->field(i), type_factory,
              target == nullptr ? nullptr
                                : target->AsStructMap()->mutable_field(i)));
      if (target_field != nullptr) {
        if (target == nullptr) {
          owned_target = AnnotationMap::Create(type);
          target = owned_target.get();
        }
        GOOGLESQL_RETURN_IF_ERROR(target->AsStructMap()->CloneIntoField(i, target_field));
      }
    }
  }

  if (owned_target == nullptr) {
    // We never needed to create a new target map.
    return target;
  }

  owned_target->Normalize();
  if (owned_target->Empty()) {
    return nullptr;
  }
  return type_factory.TakeOwnership(std::move(owned_target));
}

// TODO: The noinline attribute is to prevent the stack usage
// being added to its caller "Resolver::ResolveExpr" which is a recursive
// function. Now the attribute has to be added for all callees. Hopefully
// the fix allows replacing these with one attribute on ResolveExpr.
ABSL_ATTRIBUTE_NOINLINE
absl::Status Resolver::ResolveArrayConstructor(
    const ASTArrayConstructor* ast_array_constructor, const Type* inferred_type,
    ExprResolutionInfo* expr_resolution_info,
    std::unique_ptr<const ResolvedExpr>* resolved_expr_out) {
  RETURN_ERROR_IF_OUT_OF_STACK_SPACE();

  // We find the inferred type for elements of the array looking at the
  // following in order:
  //
  // 1. The explicit type on the ARRAY.
  // 2. The inferred type for the expression passed down to us.
  //
  // We use this inferred type only for passing down inferred types for
  // elements of the struct down the expression tree.
  const Type* inferred_element_type = nullptr;
  const ArrayType* array_type = nullptr;
  // Indicates whether the array constructor has an explicit element type.
  bool has_explicit_type = false;
  // TODO: Support collated type in array constructor later.
  if (ast_array_constructor->type() != nullptr) {
    GOOGLESQL_RETURN_IF_ERROR(ResolveArrayType(ast_array_constructor->type(),
                                     {.context = "literal value construction"},
                                     &array_type,
                                     /*resolved_type_modifiers=*/nullptr));
    has_explicit_type = true;
    inferred_element_type = array_type->element_type();
  }
  if (inferred_element_type == nullptr && inferred_type != nullptr &&
      inferred_type->IsArray()) {
    inferred_element_type = inferred_type->AsArray()->element_type();
  }

  // Resolve all the element expressions and collect the set of element types.
  InputArgumentTypeSet element_type_set;
  std::vector<std::unique_ptr<const ResolvedExpr>> resolved_elements;
  resolved_elements.reserve(ast_array_constructor->elements().size());
  for (const ASTExpression* element : ast_array_constructor->elements()) {
    std::unique_ptr<const ResolvedExpr> resolved_expr;
    GOOGLESQL_RETURN_IF_ERROR(ResolveExpr(element, expr_resolution_info, &resolved_expr,
                                inferred_element_type));

    if (resolved_expr->type()->IsArray()) {
      return MakeSqlErrorAt(element)
             << "Cannot construct array with element type "
             << resolved_expr->type()->ShortTypeName(product_mode())
             << " because nested arrays are not supported";
    }

    // If at least one element looks like an expression, the array's type should
    // be as firm as an expression's type for supertyping, function signature
    // matching, etc.
    if (resolved_expr->node_kind() == RESOLVED_LITERAL &&
        resolved_expr->GetAs<ResolvedLiteral>()->has_explicit_type()) {
      has_explicit_type = true;
    }

    GOOGLESQL_ASSIGN_OR_RETURN(
        InputArgumentType input_argument_type,
        GetInputArgumentTypeForExpr(
            resolved_expr.get(), /*pick_default_type_for_untyped_expr=*/false,
            analyzer_options()));
    element_type_set.Insert(input_argument_type);
    resolved_elements.push_back(std::move(resolved_expr));
  }

  // If array type is not explicitly mentioned, use the common supertype of the
  // element type set as the array element type.
  if (array_type == nullptr) {
    if (resolved_elements.empty()) {
      // If neither array_type nor element is specified, empty array [] is
      // always resolved as an ARRAY<int64>, which is similar to resolving
      // NULL. The coercion rules may make the empty array [] change type.
      GOOGLESQL_RETURN_IF_ERROR(type_factory_->MakeArrayType(type_factory_->get_int64(),
                                                   &array_type));
    } else {
      const Type* super_type = nullptr;
      GOOGLESQL_RETURN_IF_ERROR(
          coercer_.GetCommonSuperType(element_type_set, &super_type));
      if (super_type == nullptr) {
        // We need a special case for a literal array with a mix of INT64 and
        // UINT64 arguments.  Normally, there is no supertype.  But given that
        // we want to allow array literals like '[999999999999999999999999,1]',
        // we will define the array type as UINT64.  There still might be a
        // negative INT64 and the array literal may still not work, but it
        // enables the previous use case.
        bool all_integer_literals = true;
        for (const InputArgumentType& argument_type :
             element_type_set.arguments()) {
          if (argument_type.is_untyped()) {
            continue;
          }
          if (!argument_type.is_literal() ||
              !argument_type.type()->IsInteger()) {
            all_integer_literals = false;
            break;
          }
        }
        if (!all_integer_literals) {
          return MakeSqlErrorAt(ast_array_constructor)
                 << "Array elements of types " << element_type_set.ToString()
                 << " do not have a common supertype"
                 << GetTypeCompatibilityExplanation(
                        element_type_set.arguments());
        }
        super_type = type_factory_->get_uint64();
      }
      GOOGLESQL_RETURN_IF_ERROR(type_factory_->MakeArrayType(super_type, &array_type));
    }
  }

  // Note that `nullptr` is different from `std::nullopt` here.
  // If the value isn't set at all (i.e., `std::nullopt`), it means no explicit
  // target type was provided.
  // However, `nullptr` means that there was an explicit target type, and its
  // declared annotations are `nullptr` (no annotations), e.g. the explicit ctor
  // `ARRAY<STRING>[...]` is dropping collation from any array elements.
  std::optional<const AnnotationMap*> explicit_target_annotation_map;
  if (ast_array_constructor->type() != nullptr) {
    // TODO: b/449016617 - This will need to check the target collation if
    // explicitly specified via the COLLATE syntax in the type, e.g.
    // ARRAY<STRING COLLATE 'und:ci'>[...]
    // or if type precision is supported, e.g. `ARRAY<NUMERIC(5,2)>[...]`
    // or `ARRAY<TIMESTAMP(12)>[...]`
    explicit_target_annotation_map = nullptr;
  }

  // Add casts or convert literal values to array element type if necessary.
  bool is_array_literal = true;
  bool has_annotations = false;
  for (int i = 0; i < resolved_elements.size(); ++i) {
    // The target annotation map is the explicitly stated one if available.
    // Otherwise, set it to the element's own annotations so it stays unchanged.
    const AnnotationMap* target_element_annotation_map;

    if (ast_array_constructor->type() != nullptr) {
      GOOGLESQL_RET_CHECK(explicit_target_annotation_map.has_value());
      if (*explicit_target_annotation_map == nullptr) {
        target_element_annotation_map = nullptr;
      } else {
        const StructAnnotationMap* struct_map =
            (*explicit_target_annotation_map)->AsStructMap();
        GOOGLESQL_RET_CHECK_EQ(struct_map->num_fields(), 1);
        target_element_annotation_map = struct_map->field(0);
      }

      if (language().LanguageFeatureEnabled(
              FEATURE_BUGGY_LEAK_ANNOTATIONS_TO_ARRAY_CONSTRUCTOR_EXPLICIT_TYPE)) {  // NOLINT
        auto buggy_annotation_map =
            target_element_annotation_map != nullptr
                ? target_element_annotation_map->Clone()
                : AnnotationMap::Create(array_type->element_type());

        // The input annotation may not match the target type, e.g. if the
        // current argument is untyped, e.g. a NULL or []  (empty array).
        // In this case, we do not bother propagating annotations.
        if (!buggy_annotation_map->HasCompatibleStructure(
                resolved_elements[i]->type())) {
          buggy_annotation_map = nullptr;
        } else {
          // 1. Clear collations from this map. Today, this is not yet possible
          //    to specify in the syntax as we do not support the COLLATE syntax
          //    in type declarations yet. When supported, we should replace the
          //    following check with:
          //        buggy_annotation_map->UnsetAnnotationRecursively(
          //            CollationAnnotation::GetId());
          GOOGLESQL_RET_CHECK(!CollationAnnotation::ExistsIn(buggy_annotation_map.get()));
          // 2. Copy collation from the element's annotation map at every
          // subtree.
          if (buggy_annotation_map != nullptr) {
            GOOGLESQL_ASSIGN_OR_RETURN(
                target_element_annotation_map,
                ImportSpecificAnnotation(
                    CollationAnnotation::GetId(), array_type->element_type(),
                    resolved_elements[i]->type_annotation_map(), *type_factory_,
                    buggy_annotation_map.get()));
          }

          GOOGLESQL_RET_CHECK_EQ(target_element_annotation_map,
                       buggy_annotation_map.get());
          GOOGLESQL_ASSIGN_OR_RETURN(
              target_element_annotation_map,
              type_factory_->TakeOwnership(std::move(buggy_annotation_map)));

          // TODO: In the future, we would need to also pass the
          // target TypeModifiers for the general explicit cases, e.g.
          // ARRAY<STRUCT<NUMERIC(5,2), STRING(10) COLLATE 'und:ci'>>[...]
          //
          // Finally, log a warning
          GOOGLESQL_RETURN_IF_ERROR(AddDeprecationWarning(
              ast_array_constructor,
              DeprecationWarning::BUGGY_COLLATION_LEAK_IN_ARRAY_CONSTRUCTOR,
              absl::StrCat(
                  "Array constructor with explicit type leaks collation "
                  "from argument ",
                  i)));
        }
      }
    } else {
      // Set the target annotation map to the element's own annotations, so that
      // they stay unchanged.
      target_element_annotation_map =
          resolved_elements[i]->type_annotation_map();
    }
    GOOGLESQL_RETURN_IF_ERROR(CoerceExprToType(
        ast_array_constructor->element(i),
        AnnotatedType(array_type->element_type(),
                      target_element_annotation_map),
        kImplicitCoercion, "Array element type $1 does not coerce to $0",
        &resolved_elements[i]));

    if (resolved_elements[i]->node_kind() != RESOLVED_LITERAL) {
      is_array_literal = false;
    }

    if (resolved_elements[i]->type_annotation_map() != nullptr &&
        !resolved_elements[i]->type_annotation_map()->Empty()) {
      has_annotations = true;
    }
  }

  if (is_array_literal && !has_annotations) {
    // // TODO: b/449016617 - This branch will not work when the explicit type
    // has a target collation, as each element will now need to be coerced. The
    // condition should be adjusted instead to track `needs_to_coerce_element`
    // instead of `has_annotations`, for example.
    // It also will need to consider the bug compatibility behavior where
    // collation may leak through an explicit type that doesn't have it.
    std::vector<Value> element_values;
    element_values.reserve(resolved_elements.size());
    for (int i = 0; i < resolved_elements.size(); ++i) {
      const ResolvedExpr* element = resolved_elements[i].get();
      GOOGLESQL_RET_CHECK_EQ(element->node_kind(), RESOLVED_LITERAL)
          << element->DebugString();
      element_values.emplace_back(element->GetAs<ResolvedLiteral>()->value());
    }
    *resolved_expr_out = MakeResolvedLiteral(
        ast_array_constructor, {array_type, /*annotation_map=*/nullptr},
        Value::Array(array_type, element_values), has_explicit_type);
  } else {
    const std::vector<const ASTNode*> ast_element_locations =
        ToASTNodes(ast_array_constructor->elements());
    std::unique_ptr<ResolvedFunctionCall> resolved_function_call;

    GOOGLESQL_RETURN_IF_ERROR(function_resolver_->ResolveGeneralFunctionCall(
        ast_array_constructor, ast_element_locations,
        /*match_internal_signatures=*/false, "$make_array",
        /*is_analytic=*/false, std::move(resolved_elements),
        /*named_arguments=*/{}, array_type, &resolved_function_call));
    *resolved_expr_out = std::move(resolved_function_call);

    // If the array elements had annotations, we first resolve it as a
    // $make_array function call above to determine the array's annotation map.
    // Then, we convert the function call back to a literal array,
    // preserving the annotations.
    if (is_array_literal && has_annotations) {
      const ResolvedFunctionCall* function_call =
          (*resolved_expr_out)->GetAs<ResolvedFunctionCall>();
      GOOGLESQL_RET_CHECK(function_call->type_annotation_map() != nullptr &&
                !function_call->type_annotation_map()->Empty());
      std::vector<Value> element_values;
      element_values.reserve(function_call->argument_list_size());
      for (const auto& element : function_call->argument_list()) {
        GOOGLESQL_RET_CHECK_EQ(element->node_kind(), RESOLVED_LITERAL)
            << element->DebugString();
        element_values.push_back(element->GetAs<ResolvedLiteral>()->value());
      }

      *resolved_expr_out = MakeResolvedLiteral(
          ast_array_constructor,
          {array_type, function_call->type_annotation_map()},
          Value::Array(array_type, element_values), has_explicit_type);
    }
  }
  return absl::OkStatus();
}

void Resolver::TryCollapsingExpressionsAsLiterals(
    const ASTNode* ast_location,
    std::unique_ptr<const ResolvedNode>* node_ptr) {
  // We collect mutable pointers to all the nodes present inside 'node_ptr'
  // during which we ensure that all the children nodes come after their
  // parents (index-wise).
  std::vector<std::unique_ptr<const ResolvedNode>*> mutable_child_node_ptrs;
  mutable_child_node_ptrs.push_back(node_ptr);
  int consumed = 0;
  while (consumed < mutable_child_node_ptrs.size()) {
    // Remove constness so that we can modify the structure of the given
    // resolved node bottom-up. This will not change any semantics and is needed
    // to mutate the node pointers to point to their newly collapsed literal
    // form.
    ResolvedNode* node =
        const_cast<ResolvedNode*>(mutable_child_node_ptrs[consumed]->get());
    node->AddMutableChildNodePointers(&mutable_child_node_ptrs);
    consumed++;
  }

  // We traverse the vector 'mutable_child_node_ptr' from the back so that we
  // process the tree inside 'node_ptr' bottom-up. This also ensures that
  // whenever we mutate a pointer X, all of X's children have already been
  // processed.
  while (!mutable_child_node_ptrs.empty()) {
    std::unique_ptr<const ResolvedNode>* mutable_node_ptr =
        mutable_child_node_ptrs.back();
    const ResolvedNode* node = mutable_node_ptr->get();
    mutable_child_node_ptrs.pop_back();

    // Collapse the ResolvedMakeStruct into a struct literal if all the field
    // expressions inside the struct are literals.
    if (node->node_kind() == RESOLVED_MAKE_STRUCT) {
      const ResolvedMakeStruct* make_struct = node->GetAs<ResolvedMakeStruct>();
      std::vector<Value> literal_values;
      bool is_struct_literal = true;
      for (const auto& field_expr : make_struct->field_list()) {
        is_struct_literal =
            is_struct_literal && field_expr->node_kind() == RESOLVED_LITERAL;
        if (is_struct_literal) {
          literal_values.push_back(
              field_expr->GetAs<ResolvedLiteral>()->value());
        } else {
          // Break early from the loop if not a struct literal.
          break;
        }
      }

      if (is_struct_literal) {
        *mutable_node_ptr = MakeResolvedLiteral(
            ast_location, {make_struct->type(), /*annotation_map=*/nullptr},
            Value::Struct(make_struct->type()->AsStruct(), literal_values),
            /*has_explicit_type=*/false);
      }
    } else if (node->node_kind() == RESOLVED_FUNCTION_CALL &&
               node->GetAs<ResolvedFunctionCall>()->function()->Name() ==
                   "$make_array") {
      // Collapse the $make_array(...) into an array literal if all its element
      // fields are literals. This mostly handles the case where there is an
      // array of newly formed struct literals (after collapsing
      // ResolvedMakeStructs).
      const ResolvedFunctionCall* make_array =
          node->GetAs<ResolvedFunctionCall>();
      std::vector<Value> literal_values;
      bool is_array_literal = true;
      for (int i = 0; i < make_array->argument_list().size(); ++i) {
        is_array_literal =
            is_array_literal &&
            make_array->argument_list()[i]->node_kind() == RESOLVED_LITERAL;
        if (is_array_literal) {
          const ResolvedLiteral* argument_literal =
              make_array->argument_list()[i]->GetAs<ResolvedLiteral>();
          literal_values.push_back(argument_literal->value());
        } else {
          // Break early from the loop if not an array literal.
          break;
        }
      }

      if (is_array_literal) {
        *mutable_node_ptr = MakeResolvedLiteral(
            ast_location, {make_array->type(), /*annotation_map=*/nullptr},
            Value::Array(make_array->type()->AsArray(), literal_values),
            /*has_explicit_type=*/false);
      }
    }
  }
}

// TODO: The noinline attribute is to prevent the stack usage
// being added to its caller "Resolver::ResolveExpr" which is a recursive
// function. Now the attribute has to be added for all callees. Hopefully
// the fix allows replacing these with one attribute on ResolveExpr.
ABSL_ATTRIBUTE_NOINLINE
absl::Status Resolver::ResolveStructConstructorWithParens(
    const ASTStructConstructorWithParens* ast_struct_constructor,
    const Type* inferred_type, ExprResolutionInfo* expr_resolution_info,
    std::unique_ptr<const ResolvedExpr>* resolved_expr_out) {
  RETURN_ERROR_IF_OUT_OF_STACK_SPACE();

  return ResolveStructConstructorImpl(
      ast_struct_constructor,
      /*ast_struct_type=*/nullptr, ast_struct_constructor->field_expressions(),
      /*ast_field_identifiers*/ {}, inferred_type, /*require_name_match*/ false,
      expr_resolution_info, resolved_expr_out);
}

// TODO: The noinline attribute is to prevent the stack usage
// being added to its caller "Resolver::ResolveExpr" which is a recursive
// function. Now the attribute has to be added for all callees. Hopefully
// the fix allows replacing these with one attribute on ResolveExpr.
ABSL_ATTRIBUTE_NOINLINE
absl::Status Resolver::ResolveStructConstructorWithKeyword(
    const ASTStructConstructorWithKeyword* ast_struct_constructor,
    const Type* inferred_type, ExprResolutionInfo* expr_resolution_info,
    std::unique_ptr<const ResolvedExpr>* resolved_expr_out) {
  RETURN_ERROR_IF_OUT_OF_STACK_SPACE();

  std::vector<const ASTExpression*> ast_field_expressions;
  std::vector<const ASTIdentifier*> ast_field_identifiers;
  for (const ASTStructConstructorArg* arg : ast_struct_constructor->fields()) {
    ast_field_expressions.push_back(arg->expression());
    ast_field_identifiers.push_back(arg->alias() ? arg->alias()->identifier()
                                                 : nullptr);
  }

  return ResolveStructConstructorImpl(
      ast_struct_constructor, ast_struct_constructor->struct_type(),
      ast_field_expressions, ast_field_identifiers, inferred_type,
      /*require_name_match=*/false, expr_resolution_info, resolved_expr_out);
}

absl::Status Resolver::ResolveStructConstructorImpl(
    const ASTNode* ast_location, const ASTStructType* ast_struct_type,
    const absl::Span<const ASTExpression* const> ast_field_expressions,
    const absl::Span<const ASTIdentifier* const> ast_field_identifiers,
    const Type* inferred_type, bool require_name_match,
    ExprResolutionInfo* expr_resolution_info,
    std::unique_ptr<const ResolvedExpr>* resolved_expr_out) {
  RETURN_ERROR_IF_OUT_OF_STACK_SPACE();
  if (!ast_field_identifiers.empty()) {
    GOOGLESQL_RET_CHECK_EQ(ast_field_expressions.size(), ast_field_identifiers.size());
  }

  // If we have a type from the AST, use it.  Otherwise, we'll collect field
  // names and types and make a new StructType below.
  // TODO: Support collation name in struct constructor later.
  const StructType* struct_type = nullptr;
  if (ast_struct_type != nullptr) {
    GOOGLESQL_RETURN_IF_ERROR(ResolveStructType(ast_struct_type,
                                      {.context = "literal value construction"},
                                      &struct_type,
                                      /*resolved_type_modifiers=*/nullptr));
  }

  // We find the inferred type for the struct following in order:
  // 1. The explicit type on the STRUCT.
  // 2. The inferred type for the struct constructor expression passed down to
  //    us.
  // We use this inferred type only for passing down inferred types for
  // elements of the struct down the expression tree.
  const StructType* inferred_struct_type = nullptr;
  bool struct_has_explicit_type = false;

  if (struct_type != nullptr) {
    if (struct_type->num_fields() != ast_field_expressions.size()) {
      return MakeSqlErrorAt(ast_struct_type)
             << "STRUCT type has " << struct_type->num_fields()
             << " fields but constructor call has "
             << ast_field_expressions.size() << " fields";
    }
    // The struct has an explicit type, for example STRUCT<int32, int64>(1, 2).
    struct_has_explicit_type = true;
    inferred_struct_type = struct_type;
  }

  if (inferred_struct_type == nullptr && inferred_type != nullptr &&
      inferred_type->IsStruct()) {
    inferred_struct_type = inferred_type->AsStruct();
  }

  if (require_name_match && inferred_struct_type != nullptr) {
    // Need to additionally check field name matching.
    if (inferred_struct_type->num_fields() != ast_field_identifiers.size()) {
      return MakeSqlErrorAt(ast_location)
             << "Require naming match but field num does not match, expected: "
             << inferred_struct_type->num_fields()
             << ", actual: " << ast_field_identifiers.size();
    }
    for (int i = 0; i < ast_field_identifiers.size(); ++i) {
      const ASTIdentifier* ast_identifier = ast_field_identifiers[i];
      if (!googlesql_base::CaseEqual(inferred_struct_type->field(i).name,
                                  ast_identifier->GetAsIdString().ToString())) {
        return MakeSqlErrorAt(ast_identifier)
               << "Require naming match but field name does not match at "
                  "position "
               << i << ": '" << inferred_struct_type->field(i).name << "' vs '"
               << ast_identifier->GetAsIdString().ToString() << "'";
      }
    }
  }

  // Resolve all the field expressions.
  std::vector<std::unique_ptr<const ResolvedExpr>> resolved_field_expressions;
  std::vector<StructType::StructField> struct_fields;

  // We create a struct literal only when all of its fields are literal
  // coercible, so that we can later treat the struct literal as field-wise
  // literal coercible.
  // NOTE: This does not change any behavior, i.e. even if we don't collapse the
  // MakeStruct into a ResolvedLiteral, we will still treat it as field-wise
  // literal coercible (for appropriate fields) by looking underneath the
  // MakeStruct.
  bool is_struct_literal = true;

  // Identifies whether all the struct fields have an explicit type.
  // Initialized to false so that non-explicitly typed structs with
  // no fields are not marked as has_explicit_type.
  bool all_fields_have_explicit_type = false;

  // The has_explicit_type() property of the first field in the struct.
  // Only relevant for a literal field.
  bool first_field_has_explicit_type = false;

  // Only filled if <is_struct_literal>.
  std::vector<Value> literal_values;

  for (int i = 0; i < ast_field_expressions.size(); ++i) {
    const ASTExpression* ast_expression = ast_field_expressions[i];
    const ASTNode* ast_parent = ast_expression->parent();
    if (ast_parent->Is<ASTBracedConstructorFieldValue>()) {
      const auto* field_value =
          ast_parent->GetAsOrDie<ASTBracedConstructorFieldValue>();
      if (!field_value->colon_prefixed()) {
        return MakeSqlErrorAt(field_value)
               << "Struct field " << (i + 1)
               << " should use colon(:) to separate field and value";
      }
    }
    std::unique_ptr<const ResolvedExpr> resolved_expr;
    const Type* inferred_field_type = nullptr;
    if (inferred_struct_type && i < inferred_struct_type->num_fields()) {
      inferred_field_type = inferred_struct_type->field(i).type;
    }
    GOOGLESQL_RETURN_IF_ERROR(ResolveExpr(ast_expression, expr_resolution_info,
                                &resolved_expr, inferred_field_type));

    if (struct_type != nullptr) {
      // If we have a target type, try to coerce the field value to that type.
      const Type* target_field_type = struct_type->field(i).type;
      if (!resolved_expr->type()->Equals(target_field_type) ||
          // TODO: The condition below will be comparing the input
          // field collation and target collation after
          // STRUCT<STRING COLLATE '...'> syntax is supported.
          CollationAnnotation::ExistsIn(resolved_expr->type_annotation_map())) {
        SignatureMatchResult result;
        GOOGLESQL_ASSIGN_OR_RETURN(const InputArgumentType input_argument_type,
                         GetInputArgumentTypeForExpr(
                             resolved_expr.get(),
                             /*pick_default_type_for_untyped_expr=*/false,
                             analyzer_options()));
        if (!coercer_.CoercesTo(input_argument_type, target_field_type,
                                /*is_explicit=*/false, &result)) {
          return MakeSqlErrorAt(ast_expression)
                 << "Struct field " << (i + 1) << " has type "
                 << input_argument_type.UserFacingName(product_mode())
                 << " which does not coerce to "
                 << target_field_type->ShortTypeName(product_mode());
        }

        // We cannot use CoerceExprToType in this case because we need to
        // propagate 'struct_has_explicit_type' so that NULL and [] literals
        // are typed correctly.
        GOOGLESQL_RETURN_IF_ERROR(function_resolver_->AddCastOrConvertLiteral(
            ast_expression, target_field_type, /*format=*/nullptr,
            /*time_zone=*/nullptr, TypeParameters(),
            /*scan=*/nullptr, struct_has_explicit_type,
            /*return_null_on_error=*/false, &resolved_expr));
      }

      // For STRUCT<TYPE> constructors, we mark the field literals with
      // has_explicit_type=true so that they do not coerce further like
      // literals.
      if (resolved_expr->node_kind() == RESOLVED_LITERAL) {
        resolved_expr = MakeResolvedLiteral(
            ast_expression, resolved_expr->annotated_type(),
            resolved_expr->GetAs<ResolvedLiteral>()->value(),
            struct_has_explicit_type);
      }

      if (!require_name_match && !ast_field_identifiers.empty() &&
          ast_field_identifiers[i] != nullptr) {
        return MakeSqlErrorAt(ast_field_identifiers[i])
               << "STRUCT constructors cannot specify both an explicit type "
                  "and field names with AS";
      }
    } else {
      // Otherwise, we need to compute the struct field type to create.
      IdString alias;
      if (ast_field_identifiers.empty()) {
        // We are in the (...) construction syntax and will always
        // generate anonymous fields.
      } else {
        // We are in the STRUCT(...) constructor syntax and will use the
        // explicitly provided aliases, or try to infer aliases from the
        // expression.
        if (ast_field_identifiers[i] != nullptr) {
          alias = ast_field_identifiers[i]->GetAsIdString();
        } else {
          alias = GetAliasForExpression(ast_field_expressions[i]);
        }
      }
      struct_fields.push_back({alias.ToString(), resolved_expr->type()});
    }

    // We create a struct literal if all the fields are literals, and either
    // <struct_has_explicit_type> (all fields will be coerced to the
    // explicit struct field types with the resulting struct marked as
    // has_explicit_type) or all the fields' has_explicit_type-ness
    // is the same.
    if (is_struct_literal) {
      if (resolved_expr->node_kind() != RESOLVED_LITERAL) {
        is_struct_literal = false;
      } else {
        const ResolvedLiteral* resolved_literal =
            resolved_expr->GetAs<ResolvedLiteral>();

        // We cannot create a literal if there is a non-explicit NULL
        // literal, since that can implicitly coerce to any type and we have
        // no way to represent that in the field Value of a Struct Value.
        // For example, consider the literal struct 'STRUCT(NULL, NULL)'.
        // If we create a Value of type STRUCT<int64, int64> then want to
        // coerce that to STRUCT<int64, struct<string, int32>> then it will
        // fail since the second field of type int64 cannot coerce to the
        // second field of the target type struct<string, int32>.
        if (struct_type == nullptr) {
          // If struct_type == nullptr then we are not forcing this to be an
          // explicitly typed struct.  Otherwise, we *can* allow this to
          // be a struct literal regardless of whether a null field is
          // explicitly typed or not.
          if (resolved_literal->value().is_null() &&
              !resolved_literal->has_explicit_type()) {
            is_struct_literal = false;
          }
          if (i == 0) {
            first_field_has_explicit_type =
                resolved_literal->has_explicit_type();
            all_fields_have_explicit_type =
                resolved_literal->has_explicit_type();
          } else {
            if (!resolved_literal->has_explicit_type()) {
              all_fields_have_explicit_type = false;
            }
            if (first_field_has_explicit_type !=
                resolved_literal->has_explicit_type()) {
              // If two fields do not have the same has_explicit_type property,
              // then do not create a struct literal for this.
              is_struct_literal = false;
              literal_values.clear();
            }
          }
        }
      }
    }

    if (is_struct_literal) {
      literal_values.push_back(
          resolved_expr->GetAs<ResolvedLiteral>()->value());
    }

    resolved_field_expressions.push_back(std::move(resolved_expr));
  }

  if (struct_type == nullptr) {
    GOOGLESQL_RET_CHECK_EQ(struct_fields.size(), ast_field_expressions.size());
    GOOGLESQL_RETURN_IF_ERROR(type_factory_->MakeStructType(struct_fields, &struct_type));
  }

  if (absl::c_any_of(struct_type->fields(), [&](const StructField& field) {
        return field.type->IsMeasureType();
      })) {
    return MakeSqlErrorAt(ast_location) << "STRUCT type cannot contain MEASURE";
  }
  if (absl::c_any_of(struct_type->fields(), [&](const StructField& field) {
        return field.type->IsRow();
      })) {
    return MakeSqlErrorAt(ast_location)
           << "STRUCT type cannot contain ROW-type values";
  }

  // Resolve struct as a MakeStruct node first to calculate annotations for the
  // final struct output.
  auto node = MakeResolvedMakeStruct(struct_type,
                                     std::move(resolved_field_expressions));
  if (ast_struct_type != nullptr) {
    MaybeRecordParseLocation(ast_struct_type, node.get());
  }
  GOOGLESQL_RETURN_IF_ERROR(CheckAndPropagateAnnotations(
      /*error_node=*/ast_struct_type, node.get()));

  if (is_struct_literal) {
    GOOGLESQL_RET_CHECK_EQ(struct_type->num_fields(), literal_values.size());
    *resolved_expr_out = MakeResolvedLiteral(
        ast_location, {struct_type, node->type_annotation_map()},
        Value::Struct(struct_type, literal_values),
        struct_has_explicit_type || all_fields_have_explicit_type);
  } else {
    // Since this isn't a literal, return the MakeStruct node we created above.
    *resolved_expr_out = std::move(node);
  }
  return absl::OkStatus();
}

ResolvedNonScalarFunctionCallBase::NullHandlingModifier
Resolver::ResolveNullHandlingModifier(
    ASTFunctionCall::NullHandlingModifier ast_null_handling_modifier) {
  switch (ast_null_handling_modifier) {
    case ASTFunctionCall::DEFAULT_NULL_HANDLING:
      return ResolvedNonScalarFunctionCallBase::DEFAULT_NULL_HANDLING;
    case ASTFunctionCall::IGNORE_NULLS:
      return ResolvedNonScalarFunctionCallBase::IGNORE_NULLS;
    case ASTFunctionCall::RESPECT_NULLS:
      return ResolvedNonScalarFunctionCallBase::RESPECT_NULLS;
      // No "default:". Let the compilation fail in case an entry is added to
      // the enum without being handled here.
  }
}

// Checks whether the aggregate filtering feature ((broken link))
// is enabled and supported for the given function call.
template <typename ASTFilterExpression>
static absl::Status CheckAggregateFilteringModifierSupport(
    const ASTFilterExpression* ast_filter_expr,
    const ResolvedFunctionCall* resolved_function,
    const LanguageOptions& language, const char* modifier_name) {
  // If there are no filtering expressions, there is nothing to check.
  if (ast_filter_expr == nullptr) {
    return absl::OkStatus();
  }
  if (!language.LanguageFeatureEnabled(FEATURE_AGGREGATE_FILTERING)) {
    return MakeSqlErrorAt(ast_filter_expr) << absl::StrFormat(
               "%s is not supported in function calls", modifier_name);
  }
  // Check if the function supports the filtering modifier.
  const Function* function = resolved_function->function();
  // Normal aggregate functions like COUNT become differential privacy functions
  // when used with 'WITH DIFFERENTIAL_PRIVACY'. This can result in confusing
  // (and incorrect) error messages like "COUNT function does not support WHERE
  // modifier". To avoid this, we provide more information in the error message
  // to indicate that it is an anonymized / differentially private aggregate
  // function.
  absl::string_view function_type =
      function->Is<AnonFunction>()
          ? "Anonymization / Differential Privacy Function"
          : "Function";
  if constexpr (std::is_same_v<ASTFilterExpression, ASTHaving>) {
    if (!function->SupportsHavingFilterModifier()) {
      return MakeSqlErrorAt(ast_filter_expr)
             << absl::AsciiStrToUpper(function->SQLName())
             << absl::StrFormat(" %s does not support %s", function_type,
                                modifier_name);
    }
  } else if constexpr (std::is_same_v<ASTFilterExpression, ASTWhereClause>) {
    if (!function->SupportsWhereModifier()) {
      return MakeSqlErrorAt(ast_filter_expr)
             << absl::AsciiStrToUpper(function->SQLName())
             << absl::StrFormat(" %s does not support %s", function_type,
                                modifier_name);
    }
  }
  // Check if the function is an aggregate function.
  if (!resolved_function->function()->IsAggregate()) {
    return MakeSqlErrorAt(ast_filter_expr) << absl::StrFormat(
               "%s is not supported for non-aggregate function calls",
               modifier_name);
  }
  return absl::OkStatus();
}

// Resolves arguments to ORDER BY and HAVING expressions in multi-level
// aggregate functions. These expression may introduce new nested aggregate
// functions (e.g. `ORDER BY AVG(X)`, `HAVING COUNT(*) > 0`, etc.), so we
// handle them in their their own query resolution context
// (multi_level_aggregate_info) and later propagate them as
// computed columns to the aggregate function.
static absl::Status ResolveExprWithPotentialNestedAggregate(
    const ASTFunctionCall* ast_function_call,
    ExprResolutionInfo* expr_resolution_info,
    QueryResolutionInfo& multi_level_aggregate_info,
    absl::AnyInvocable<
        absl::Status(ExprResolutionInfo* new_expr_resolution_info) &&>
        resolve_expr_callback) {
  GOOGLESQL_RET_CHECK(ast_function_call->group_by() != nullptr);
  GOOGLESQL_RET_CHECK(!ast_function_call->group_by()->grouping_items().empty());

  auto new_expr_resolution_info =
      ExprResolutionInfo::MakeChildForMultiLevelAggregation(
          expr_resolution_info, &multi_level_aggregate_info,
          expr_resolution_info->name_scope);

  const size_t original_grouping_keys_count =
      multi_level_aggregate_info.group_by_column_state_list().size();
  const size_t original_nested_aggregate_count =
      multi_level_aggregate_info
          .num_aggregate_columns_to_compute_across_all_scopes();

  // Resolve the expression with the new `ExprResolutionInfo` and perform checks
  // to ensure that the number of grouping keys and nested aggregate functions
  // do not change after resolving the expression.
  GOOGLESQL_RETURN_IF_ERROR(
      std::move(resolve_expr_callback)(new_expr_resolution_info.get()));

  // The number of grouping keys should not change after resolving the
  // expression.
  GOOGLESQL_RET_CHECK_EQ(multi_level_aggregate_info.group_by_column_state_list().size(),
               original_grouping_keys_count);

  // The number of nested aggregate functions may increase after resolving
  // the expression, since it's possible to introduce new aggregate functions
  // (e.g. 'HAVING COUNT(*) > 10', 'ORDER BY AVG(X), SUM(Y)')
  GOOGLESQL_RET_CHECK_GE(multi_level_aggregate_info
                   .num_aggregate_columns_to_compute_across_all_scopes(),
               original_nested_aggregate_count);

  return absl::OkStatus();
}

// Return true if the `resolved_expr` contains an `ExpressionColumn`.
// Populates  `expression_column_out` with the first expression column found,
// or nullptr if none is found.
static bool ContainsExpressionColumn(
    const ResolvedExpr* resolved_expr,
    const ResolvedExpressionColumn*& expression_column_out) {
  expression_column_out = nullptr;
  std::vector<const ResolvedNode*> expression_columns;
  resolved_expr->GetDescendantsWithKinds({RESOLVED_EXPRESSION_COLUMN},
                                         &expression_columns);
  if (expression_columns.empty()) {
    return false;
  }
  expression_column_out =
      expression_columns.front()->GetAs<ResolvedExpressionColumn>();
  return true;
}

// TODO - Add support for horizontal aggregation.
absl::Status Resolver::ResolveAggregateFunctionHavingModifier(
    const ASTFunctionCall* ast_function_call,
    const ResolvedFunctionCall* resolved_function_call,
    ExprResolutionInfo* expr_resolution_info,
    QueryResolutionInfo& multi_level_aggregate_info,
    std::unique_ptr<const ResolvedExpr>* resolved_expr_out) {
  static constexpr char kHavingModifier[] = "HAVING modifier";
  const ASTHaving* having_expr = ast_function_call->having_expr();
  if (having_expr == nullptr) {
    return absl::OkStatus();
  }
  GOOGLESQL_RETURN_IF_ERROR(CheckAggregateFilteringModifierSupport<ASTHaving>(
      ast_function_call->having_expr(), resolved_function_call, language(),
      kHavingModifier));

  // The HAVING modifier is supported for the current language features and
  // function, but we should still ensure a group by is present.
  GOOGLESQL_RET_CHECK(MultiLevelAggregationPresent(ast_function_call));

  // Resolves the HAVING modifier with a new `ExprResolutionInfo` to collect
  // any new aggregate functions declared within.
  GOOGLESQL_RETURN_IF_ERROR(ResolveExprWithPotentialNestedAggregate(
      ast_function_call, expr_resolution_info, multi_level_aggregate_info,
      [&](ExprResolutionInfo* new_expr_resolution_info) {
        auto status = ResolveExpr(having_expr->expression(),
                                  new_expr_resolution_info, resolved_expr_out);
        if (multi_level_aggregate_info.HasGroupingCall()) {
          status = MakeSqlErrorAt(having_expr->expression())
                   << "GROUPING function not allowed in HAVING modifier of a "
                      "multi-level aggregate.";
        }
        return status;
      }));
  GOOGLESQL_RETURN_IF_ERROR(CoerceExprToBool(having_expr->expression(), kHavingModifier,
                                   resolved_expr_out));

  // Disallow `ExpressionColumn` within the HAVING filter modifier for a
  // multi-level aggregate function. The rationale is identical to why we
  // disallow `ExpressionColumn` within arguments to a multi-level aggregate
  // function.
  const ResolvedExpressionColumn* expression_column = nullptr;
  if (ContainsExpressionColumn(resolved_expr_out->get(), expression_column)) {
    GOOGLESQL_RET_CHECK(expression_column != nullptr);
    return MakeSqlErrorAt(ast_function_call)
           << "Expression column " << expression_column->name()
           << " cannot be used in the HAVING modifier to a multi-level "
              "aggregate function.";
  }
  return absl::OkStatus();
}

static absl::Status MakeFunctionDoesNotSupportOrderByError(
    const ASTOrderBy* ast_order_by, const Function* function) {
  return MakeSqlErrorAt(ast_order_by)
         << function->SQLName() << " does not support ORDER BY in arguments";
}

static absl::StatusOr<bool> DoesAggFunctionCallHaveSameExprInArgs(
    const ResolvedExpr* expr,
    absl::Span<const std::unique_ptr<const ResolvedExpr>> arg_list,
    const LanguageOptions& language_options) {
  bool is_same_expr;
  for (const auto& argument : arg_list) {
    GOOGLESQL_ASSIGN_OR_RETURN(is_same_expr, IsSameExpressionForGroupBy(
                                       argument.get(), expr, language_options));
    if (is_same_expr) {
      return true;
    }
  }
  return false;
}

absl::Status Resolver::ResolveAggregateFunctionOrderByModifiers(
    const ASTFunctionCall* ast_function_call,
    std::unique_ptr<ResolvedFunctionCall>* resolved_function_call,
    ExprResolutionInfo* expr_resolution_info,
    QueryResolutionInfo& multi_level_aggregate_info,
    std::vector<std::unique_ptr<const ResolvedComputedColumn>>*
        computed_columns_for_horizontal_aggregation,
    std::vector<std::unique_ptr<const ResolvedOrderByItem>>*
        resolved_order_by_items) {
  QueryResolutionInfo* const query_resolution_info =
      expr_resolution_info->query_resolution_info;
  const Function* function = (*resolved_function_call)->function();
  const ASTOrderBy* order_by_arguments = ast_function_call->order_by();

  const bool is_multi_level_aggregate =
      MultiLevelAggregationPresent(ast_function_call);

  if (IsMatchRecognizeMeasuresFunction(function)) {
    GOOGLESQL_RET_CHECK(match_recognize_state_.has_value());
    GOOGLESQL_RET_CHECK(match_recognize_state_->match_row_number_column.IsInitialized());
    GOOGLESQL_RET_CHECK_EQ(function->NumSignatures(), 1);
    const int64_t context_id = function->GetSignature(0)->context_id();
    GOOGLESQL_RET_CHECK(context_id == FN_FIRST_AGG || context_id == FN_LAST_AGG);

    // There is currently no way to call FIRST() and LAST() in a horizontal
    // aggregation.
    GOOGLESQL_RET_CHECK(!expr_resolution_info->in_horizontal_aggregation);

    // FIRST() and LAST() are ordered by match_row_number. They do not support
    // the ORDER BY modifier as the row number is implicitly filled in.
    // The use case of the first or last value based on a custom ordering is
    // already served by ANY_VALUE(... HAVING MIN/MAX ...)
    GOOGLESQL_RET_CHECK(!function->SupportsOrderingArguments());
    if (order_by_arguments != nullptr) {
      return MakeFunctionDoesNotSupportOrderByError(order_by_arguments,
                                                    function);
    }

    // Add the order by match_row_number.
    resolved_order_by_items->push_back(MakeResolvedOrderByItem(
        MakeColumnRef(match_recognize_state_->match_row_number_column),
        /*collation_name=*/nullptr, /*is_descending=*/false,
        ResolvedOrderByItemEnums::ORDER_UNSPECIFIED));
    return absl::OkStatus();
  }

  if (order_by_arguments == nullptr) {
    if (expr_resolution_info->in_horizontal_aggregation &&
        function->SupportsOrderingArguments()) {
      if (!language().LanguageFeatureEnabled(FEATURE_ORDER_BY_IN_AGGREGATE)) {
        return MakeSqlErrorAt(ast_function_call)
               << "ORDER BY in aggregate function is not supported and this "
                  "horizontal aggregate is evaluated in array order";
      } else if (ast_function_call->distinct()) {
        return MakeSqlErrorAt(ast_function_call)
               << "Horizontal aggregates are implicitly ordered by the "
                  "underlying array order unless an explicit ORDER BY is "
                  "specified. An aggregate function that has both DISTINCT and "
                  "ORDER BY arguments can only ORDER BY expressions that are "
                  "arguments to the function";
      }
    }

    if (function->SupportsOrderingArguments() && !is_multi_level_aggregate &&
        expr_resolution_info->name_scope->allows_match_number_function()) {
      // The current aggregate is directly contained in a MATCH_RECOGNIZE
      // clause (not through a subquery), supports ordering, and is
      // not multi-level, and no ORDER BY was specified.
      // It gets the implicit ordering of MATCH_ROW_NUMBER().
      GOOGLESQL_RET_CHECK(match_recognize_state_.has_value());
      GOOGLESQL_RET_CHECK(
          match_recognize_state_->match_row_number_column.IsInitialized());

      bool apply_implicit_ordering = true;
      if (ast_function_call->distinct()) {
        // Make a dummy column ref (Not through MakeColumnRef() which records
        // access) to see if it's already in the args.
        auto dummy_col_ref = MakeResolvedColumnRef(
            match_recognize_state_->match_row_number_column.type(),
            match_recognize_state_->match_row_number_column,
            /*is_correlated=*/false);
        GOOGLESQL_ASSIGN_OR_RETURN(
            apply_implicit_ordering,
            DoesAggFunctionCallHaveSameExprInArgs(
                dummy_col_ref.get(), (*resolved_function_call)->argument_list(),
                language()));
      }

      if (apply_implicit_ordering) {
        if (!language().LanguageFeatureEnabled(FEATURE_ORDER_BY_IN_AGGREGATE)) {
          return MakeSqlErrorAt(ast_function_call)
                 << function->QualifiedSQLName(/*capitalize_qualifier=*/true)
                 << " is not supported in the MEASURES clause of "
                    "MATCH_RECOGNIZE";
        }

        // Add the order by match_row_number.
        // Add the order by match_row_number.
        resolved_order_by_items->push_back(MakeResolvedOrderByItem(
            MakeColumnRef(match_recognize_state_->match_row_number_column),
            /*collation_name=*/nullptr, /*is_descending=*/false,
            ResolvedOrderByItemEnums::ORDER_UNSPECIFIED));
      }
    }
    return absl::OkStatus();
  }

  if (!language().LanguageFeatureEnabled(FEATURE_ORDER_BY_IN_AGGREGATE)) {
    return MakeSqlErrorAt(order_by_arguments)
           << "ORDER BY in aggregate function is not supported";
  }

  // Checks whether the function supports ordering in arguments if
  // there is an order by specified.
  if (!function->SupportsOrderingArguments()) {
    return MakeSqlErrorAt(order_by_arguments)
           << function->QualifiedSQLName(/*capitalize_qualifier=*/true)
           << " does not support ORDER BY in arguments";
  }

  const std::vector<std::unique_ptr<const ResolvedExpr>>& arg_list =
      (*resolved_function_call)->argument_list();

  if (arg_list.empty()) {
    return MakeSqlErrorAt(order_by_arguments)
           << "ORDER BY in aggregate function call with no arguments is not "
              "allowed";
  }

  // Resolves the ordering expression in arguments. For multi-level aggregation,
  // the ordering expression may introduce new nested aggregate functions (e.g.
  // ORDER BY (AVG(X))), so use a new `ExprResolutionInfo` and
  // `multi_level_aggregate_info` to resolve these nested aggregate functions.
  std::vector<OrderByItemInfo> order_by_info;
  if (is_multi_level_aggregate) {
    GOOGLESQL_RETURN_IF_ERROR(ResolveExprWithPotentialNestedAggregate(
        ast_function_call, expr_resolution_info, multi_level_aggregate_info,
        [&](ExprResolutionInfo* new_expr_resolution_info) {
          absl::Status status;
          status = ResolveOrderingExprs(
              order_by_arguments->ordering_expressions(),
              new_expr_resolution_info,
              /*allow_ordinals=*/true, "ORDER BY in aggregate", &order_by_info);
          if (multi_level_aggregate_info.HasGroupingCall()) {
            status =
                MakeSqlErrorAt(order_by_arguments)
                << "GROUPING function not allowed in ORDER BY modifier of a "
                   "multi-level aggregate.";
          }
          return status;
        }));
  } else {
    GOOGLESQL_RETURN_IF_ERROR(ResolveOrderingExprs(
        order_by_arguments->ordering_expressions(), expr_resolution_info,
        /*allow_ordinals=*/true, "ORDER BY in aggregate", &order_by_info));
  }

  // Checks if there is any order by index.
  // Supporting order by index here makes little sense as the function
  // arguments are not resolved to columns as opposed to the order-by
  // after a select. The Postgres spec has the same restriction.
  for (const OrderByItemInfo& item_info : order_by_info) {
    if (item_info.is_select_list_index()) {
      return MakeSqlErrorAt(order_by_arguments)
             << "Aggregate functions do not allow ORDER BY by index in "
                "arguments";
    }
  }

  if (ast_function_call->distinct()) {
    // If both DISTINCT and ORDER BY are present, the ORDER BY arguments
    // must be a subset of the DISTINCT arguments.
    for (const OrderByItemInfo& item_info : order_by_info) {
      GOOGLESQL_ASSIGN_OR_RETURN(
          bool is_order_by_argument_matched,
          DoesAggFunctionCallHaveSameExprInArgs(
              item_info.order_expression.get(), arg_list, language()));
      if (!is_order_by_argument_matched) {
        return MakeSqlErrorAt(item_info.ast_location)
               << "An aggregate function that has both DISTINCT and ORDER BY "
                  "arguments can only ORDER BY expressions that are "
                  "arguments to the function";
      }
    }
  }

  // Make a copy of the grouping keys in `multi_level_aggregate_info`. If the
  // resolved expression of an ORDER BY argument matches the resolved expression
  // for a grouping key, then the ORDER BY argument is resolved as a reference
  // to the grouping key.
  std::vector<std::unique_ptr<const ResolvedComputedColumn>>
      multi_level_aggregate_grouping_keys_copy;
  for (const GroupByColumnState& grouping_key :
       multi_level_aggregate_info.group_by_column_state_list()) {
    GOOGLESQL_ASSIGN_OR_RETURN(
        std::unique_ptr<ResolvedComputedColumn> copied_grouping_key,
        ResolvedASTDeepCopyVisitor::Copy(grouping_key.computed_column.get()));
    multi_level_aggregate_grouping_keys_copy.push_back(
        std::move(copied_grouping_key));
  }
  const size_t original_multi_level_aggregate_grouping_keys_count =
      multi_level_aggregate_grouping_keys_copy.size();
  std::vector<std::unique_ptr<const ResolvedComputedColumn>>*
      to_compute_before_aggregation = nullptr;
  if (is_multi_level_aggregate) {
    to_compute_before_aggregation = &multi_level_aggregate_grouping_keys_copy;
  } else if (query_resolution_info != nullptr) {
    to_compute_before_aggregation =
        query_resolution_info
            ->select_list_columns_to_compute_before_aggregation();
  }
  if (expr_resolution_info->in_horizontal_aggregation) {
    GOOGLESQL_RET_CHECK(!is_multi_level_aggregate);
    to_compute_before_aggregation = computed_columns_for_horizontal_aggregation;
  }

  GOOGLESQL_RET_CHECK(to_compute_before_aggregation != nullptr);
  GOOGLESQL_RETURN_IF_ERROR(AddColumnsForOrderByExprs(kOrderById, &order_by_info,
                                            to_compute_before_aggregation));

  // If `multi_level_aggregate_grouping_keys` has grown in size, then
  // `ast_function_call` is a multi-level aggregate function and at least one of
  // the ORDER BY arguments failed to resolve to a column in `group_by_list` or
  // `group_by_aggregate_list`, which is a violation of the ResolvedAST
  // contract.
  GOOGLESQL_RET_CHECK_GE(multi_level_aggregate_grouping_keys_copy.size(),
               original_multi_level_aggregate_grouping_keys_count);
  if (multi_level_aggregate_grouping_keys_copy.size() >
      original_multi_level_aggregate_grouping_keys_count) {
    ResolvedColumn order_by_column =
        multi_level_aggregate_grouping_keys_copy
            [original_multi_level_aggregate_grouping_keys_count]
                ->column();
    auto it = std::find_if(order_by_info.begin(), order_by_info.end(),
                           [order_by_column](const OrderByItemInfo& item_info) {
                             return item_info.order_column == order_by_column;
                           });
    GOOGLESQL_RET_CHECK(it != order_by_info.end());
    return MakeSqlErrorAt(it->ast_location)
           << "ORDER BY argument is neither an aggregate function nor a "
              "grouping key.";
  }
  // We may have precomputed some ORDER BY expression columns before
  // aggregation.  If any aggregate function arguments match those
  // precomputed columns, then update the argument to reference the
  // precomputed column (and avoid recomputing the argument expression).
  if (!to_compute_before_aggregation->empty()) {
    std::vector<std::unique_ptr<const ResolvedExpr>> updated_args =
        (*resolved_function_call)->release_argument_list();
    bool is_same_expr;
    for (int arg_idx = 0; arg_idx < updated_args.size(); ++arg_idx) {
      for (const std::unique_ptr<const ResolvedComputedColumn>&
               computed_column : *to_compute_before_aggregation) {
        GOOGLESQL_ASSIGN_OR_RETURN(
            is_same_expr,
            IsSameExpressionForGroupBy(updated_args[arg_idx].get(),
                                       computed_column->expr(), language()));
        if (is_same_expr) {
          updated_args[arg_idx] = MakeColumnRef(computed_column->column());
          break;
        }
      }
    }
    (*resolved_function_call)->set_argument_list(std::move(updated_args));
  }

  return ResolveOrderByItems(/*name_list=*/{}, {order_by_info},
                             /*is_pipe_order_by=*/false,
                             resolved_order_by_items);
}

absl::Status Resolver::ResolveAggregateFunctionLimitModifier(
    const ASTFunctionCall* ast_function_call, const Function* function,
    std::unique_ptr<const ResolvedExpr>* limit_expr) {
  const ASTLimitOffset* limit_offset = ast_function_call->limit_offset();
  if (limit_offset == nullptr) {
    return absl::OkStatus();
  }
  if (!language().LanguageFeatureEnabled(FEATURE_LIMIT_IN_AGGREGATE)) {
    return MakeSqlErrorAt(limit_offset)
           << "LIMIT in aggregate function arguments is not supported";
  }

  // Checks whether the function supports limit in arguments if
  // there is a LIMIT specified.
  if (!function->SupportsLimitArguments()) {
    return MakeSqlErrorAt(limit_offset)
           << function->QualifiedSQLName(/*capitalize_qualifier=*/true)
           << " does not support LIMIT in arguments";
  }
  // Returns an error if an OFFSET is specified.
  if (limit_offset->offset() != nullptr) {
    return MakeSqlErrorAt(limit_offset->offset())
           << function->QualifiedSQLName(/*capitalize_qualifier=*/true)
           << " does not support OFFSET in arguments";
  }

  if (limit_offset->has_limit_all()) {
    return MakeSqlErrorAt(limit_offset->limit())
           << "LIMIT ALL in aggregate function arguments is not supported";
  }

  GOOGLESQL_RET_CHECK(limit_offset->limit_expression() != nullptr);

  auto expr_resolution_info =
      std::make_unique<ExprResolutionInfo>(empty_name_scope_.get(), "LIMIT");
  return ResolveLimitOrOffsetExpr(limit_offset->limit_expression(),
                                  /*clause_name=*/"LIMIT",
                                  expr_resolution_info.get(), limit_expr);
}

// When resolving a multi-level aggregate expression, the innermost aggregate
// function may introduce new select list columns to compute before aggregation.
// Those columns are stored in `multi_level_aggregate_info` QRI, and need to be
// moved to the outer QRI so that they can be projected before constructing
// the ResolvedAggregateScan.
static void AddColumnsToComputeBeforeAggregation(
    QueryResolutionInfo* query_resolution_info,
    QueryResolutionInfo& multi_level_aggregate_info) {
  if (query_resolution_info == nullptr) {
    return;
  }
  std::vector<std::unique_ptr<const ResolvedComputedColumn>>*
      cols_to_compute_before_aggregation =
          query_resolution_info
              ->select_list_columns_to_compute_before_aggregation();
  std::vector<std::unique_ptr<const ResolvedComputedColumn>>
      cols_to_compute_before_aggregation_from_nested_aggregate =
          multi_level_aggregate_info
              .release_select_list_columns_to_compute_before_aggregation();
  cols_to_compute_before_aggregation->insert(
      cols_to_compute_before_aggregation->end(),
      std::make_move_iterator(
          cols_to_compute_before_aggregation_from_nested_aggregate.begin()),
      std::make_move_iterator(
          cols_to_compute_before_aggregation_from_nested_aggregate.end()));
}

static absl::StatusOr<
    std::vector<std::unique_ptr<const ResolvedComputedColumnBase>>>
GetNestedAggregateColumns(QueryResolutionInfo& multi_level_aggregate_info) {
  if (!multi_level_aggregate_info.scoped_aggregation_state()
           ->row_range_determined) {
    GOOGLESQL_RET_CHECK(multi_level_aggregate_info.scoped_aggregate_columns_to_compute()
                  .empty());
    return multi_level_aggregate_info
        .release_unscoped_aggregate_columns_to_compute();
  }
  GOOGLESQL_RET_CHECK(multi_level_aggregate_info.unscoped_aggregate_columns_to_compute()
                .empty());
  auto scoped_aggregate_columns_to_compute =
      multi_level_aggregate_info.release_scoped_aggregate_columns_to_compute();
  // The map could be empty if we have no aggregations in the measure at all,
  // or no nested aggs.
  GOOGLESQL_RET_CHECK_LE(scoped_aggregate_columns_to_compute.size(), 1);
  if (scoped_aggregate_columns_to_compute.empty()) {
    return std::vector<std::unique_ptr<const ResolvedComputedColumnBase>>();
  }
  if (scoped_aggregate_columns_to_compute.begin()->first.empty()) {
    GOOGLESQL_RET_CHECK(!multi_level_aggregate_info.scoped_aggregation_state()
                   ->target_pattern_variable_ref.has_value());
  } else {
    GOOGLESQL_RET_CHECK_EQ(scoped_aggregate_columns_to_compute.begin()->first,
                 *multi_level_aggregate_info.scoped_aggregation_state()
                      ->target_pattern_variable_ref);
  }
  return std::move(scoped_aggregate_columns_to_compute.begin()->second);
}

static absl::Status MakeNestedAggregateNotSupportedError(
    const ASTFunctionCall* enclosing_function_call,
    absl::string_view nested_aggregate_function_name) {
  return MakeSqlErrorAt(enclosing_function_call)
         << "Multi-level aggregate function does not support "
         << nested_aggregate_function_name
         << " as a nested aggregate function.";
}

static absl::Status EnsureNestedAggregatesSupportGroupByModifier(
    const ASTFunctionCall* enclosing_function_call,
    absl::Span<const std::unique_ptr<const ResolvedComputedColumnBase>>
        nested_aggregates) {
  for (const auto& nested_aggregate : nested_aggregates) {
    GOOGLESQL_RET_CHECK(nested_aggregate->expr()->Is<ResolvedAggregateFunctionCall>());
    auto aggregate_function_call =
        nested_aggregate->expr()->GetAs<ResolvedAggregateFunctionCall>();
    if (!aggregate_function_call->function()->SupportsGroupByModifier()) {
      return MakeNestedAggregateNotSupportedError(
          enclosing_function_call,
          aggregate_function_call->function()->SQLName());
    }
  }
  return absl::OkStatus();
}

absl::Status Resolver::ResolveWhereModifier(
    const ASTFunctionCall* ast_function_call,
    const ResolvedFunctionCall* resolved_function_call,
    const NameScope* name_scope,
    std::unique_ptr<const ResolvedExpr>* resolved_where_expr) {
  static constexpr char kWhereModifier[] = "WHERE modifier";
  GOOGLESQL_RETURN_IF_ERROR(CheckAggregateFilteringModifierSupport<ASTWhereClause>(
      ast_function_call->where_expr(), resolved_function_call, language(),
      kWhereModifier));

  return ResolveWhere(ast_function_call->where_expr(), name_scope,
                      kWhereModifier, /*query_resolution_info=*/nullptr,
                      resolved_where_expr);
}

absl::Status Resolver::FinishResolvingAggregateFunction(
    const ASTFunctionCall* ast_function_call,
    std::unique_ptr<ResolvedFunctionCall>* resolved_function_call,
    ExprResolutionInfo* expr_resolution_info,
    std::unique_ptr<QueryResolutionInfo> multi_level_aggregate_info,
    std::unique_ptr<const ResolvedExpr>* resolved_expr_out,
    std::optional<ResolvedColumn>& out_unconsumed_side_effect_column) {
  ABSL_DCHECK(expr_resolution_info != nullptr);
  QueryResolutionInfo* const query_resolution_info =
      expr_resolution_info->query_resolution_info;
  const Function* function = (*resolved_function_call)->function();

  if (!expr_resolution_info->allows_aggregation &&
      !expr_resolution_info->allows_horizontal_aggregation) {
    return MakeSqlErrorAt(ast_function_call)
           << function->QualifiedSQLName(/*capitalize_qualifier=*/true)
           << " not allowed in " << expr_resolution_info->clause_name
           << (expr_resolution_info->is_post_distinct()
                   ? " after SELECT DISTINCT"
                   : "");
  } else if (query_resolution_info == nullptr &&
             !expr_resolution_info->allows_horizontal_aggregation) {
    return MakeSqlErrorAt(ast_function_call)
           << function->QualifiedSQLName(/*capitalize_qualifier=*/true)
           << " not expected";
  }

  const std::vector<std::unique_ptr<const ResolvedExpr>>& arg_list =
      (*resolved_function_call)->argument_list();
  if (ast_function_call->distinct()) {
    if (!function->SupportsDistinctModifier()) {
      return MakeSqlErrorAt(ast_function_call)
             << function->QualifiedSQLName(/*capitalize_qualifier=*/true)
             << " does not support DISTINCT in arguments";
    }

    if (arg_list.empty()) {
      return MakeSqlErrorAt(ast_function_call)
             << "DISTINCT function call with no arguments is not allowed";
    }

    for (const std::unique_ptr<const ResolvedExpr>& argument : arg_list) {
      std::string no_grouping_type;
      if (!argument->type()->SupportsGrouping(language(), &no_grouping_type)) {
        return MakeSqlErrorAt(ast_function_call)
               << "Aggregate functions with DISTINCT cannot be used with "
                  "arguments of type "
               << no_grouping_type;
      }
    }
  }

  // WHERE modifier resolution.
  std::unique_ptr<const ResolvedExpr> resolved_where_expr;
  GOOGLESQL_RETURN_IF_ERROR(ResolveWhereModifier(
      ast_function_call, resolved_function_call->get(),
      expr_resolution_info->aggregate_name_scope, &resolved_where_expr));

  // HAVING {MAX|MIN} modifier resolution.
  std::unique_ptr<const ResolvedAggregateHavingModifier>
      resolved_having_modifier;
  GOOGLESQL_RETURN_IF_ERROR(ResolveHavingMaxMinModifier(
      ast_function_call, resolved_function_call->get(), expr_resolution_info,
      &resolved_having_modifier));

  // HAVING modifier resolution. Note that this is different from the
  // HAVING {MAX|MIN} modifier resolution above. The HAVING {MAX|MIN} modifier
  // is mutually exclusive with this HAVING modifier used in multi-level
  // aggregation.
  std::unique_ptr<const ResolvedExpr> resolved_having_expr;
  GOOGLESQL_RETURN_IF_ERROR(ResolveAggregateFunctionHavingModifier(
      ast_function_call, resolved_function_call->get(), expr_resolution_info,
      *multi_level_aggregate_info, &resolved_having_expr));

  // ORDER BY modifier resolution.
  std::vector<std::unique_ptr<const ResolvedComputedColumn>>
      computed_columns_for_horizontal_aggregation;
  std::vector<std::unique_ptr<const ResolvedOrderByItem>>
      resolved_order_by_items;
  GOOGLESQL_RETURN_IF_ERROR(ResolveAggregateFunctionOrderByModifiers(
      ast_function_call, resolved_function_call, expr_resolution_info,
      *multi_level_aggregate_info, &computed_columns_for_horizontal_aggregation,
      &resolved_order_by_items));

  // We may have collected additional columns that need to be computed before
  // aggregation when resolving the HAVING and ORDER BY expressions above.
  // Propagate these new columns from `multi_level_aggregate_info` to the
  // `outer_query_resolution_info`.
  AddColumnsToComputeBeforeAggregation(query_resolution_info,
                                       *multi_level_aggregate_info);

  // LIMIT modifier resolution.
  std::unique_ptr<const ResolvedExpr> limit_expr;
  GOOGLESQL_RETURN_IF_ERROR(ResolveAggregateFunctionLimitModifier(ast_function_call,
                                                        function, &limit_expr));

  // CLAMPED BETWEEN function support check. This is resolved elsewhere as
  // arguments to the function call.
  const ASTClampedBetweenModifier* clamped_between_modifier =
      ast_function_call->clamped_between_modifier();
  if (clamped_between_modifier != nullptr) {
    // Checks whether the function supports clamped between in arguments if
    // there is a CLAMPED BETWEEN specified.
    if (!function->SupportsClampedBetweenModifier()) {
      return MakeSqlErrorAt(clamped_between_modifier)
             << function->QualifiedSQLName(/*capitalize_qualifier=*/true)
             << " does not support CLAMPED BETWEEN in arguments";
    }
  }

  // Checks if there exists aggregate or analytic function after resolving
  // the aggregate arguments, ORDER BY modifier, and HAVING modifier.
  if (expr_resolution_info->findings.has_aggregation) {
    if (!language().LanguageFeatureEnabled(FEATURE_MULTILEVEL_AGGREGATION)) {
      return MakeSqlErrorAt(ast_function_call)
             << "Aggregations of aggregations are not allowed";
    } else if (ast_function_call->group_by() == nullptr ||
               ast_function_call->group_by()->grouping_items().empty()) {
      return MakeSqlErrorAt(ast_function_call)
             << "Multi-level aggregation requires the enclosing aggregate "
                "function to have one or more GROUP BY modifiers.";
    }
  }
  if (expr_resolution_info->findings.has_analytic) {
    return MakeSqlErrorAt(ast_function_call)
           << "Analytic functions cannot be arguments to aggregate functions";
  }

  expr_resolution_info->findings.has_aggregation = true;

  ResolvedNonScalarFunctionCallBase::NullHandlingModifier
      resolved_null_handling_modifier = ResolveNullHandlingModifier(
          ast_function_call->null_handling_modifier());
  if (resolved_null_handling_modifier !=
      ResolvedNonScalarFunctionCallBase::DEFAULT_NULL_HANDLING) {
    if (!analyzer_options_.language().LanguageFeatureEnabled(
            FEATURE_NULL_HANDLING_MODIFIER_IN_AGGREGATE)) {
      return MakeSqlErrorAt(ast_function_call)
             << "IGNORE NULLS and RESPECT NULLS in aggregate functions are not "
                "supported";
    }
    if (!function->SupportsNullHandlingModifier()) {
      return MakeSqlErrorAt(ast_function_call)
             << "IGNORE NULLS and RESPECT NULLS are not allowed for aggregate "
                "function "
             << function->Name();
    }
  }

  bool args_have_annotations = false;
  for (const std::unique_ptr<const ResolvedExpr>& arg : arg_list) {
    if (arg->type_annotation_map() != nullptr &&
        !arg->type_annotation_map()->Empty()) {
      args_have_annotations = true;
      break;
    }
  }

  auto compute_input_argument_types =
      [&](absl::Span<const std::unique_ptr<const ResolvedExpr>> args)
      -> absl::StatusOr<std::vector<InputArgumentType>> {
    std::vector<InputArgumentType> input_arguments;
    input_arguments.reserve(args.size());
    for (const std::unique_ptr<const ResolvedExpr>& arg : args) {
      GOOGLESQL_ASSIGN_OR_RETURN(
          InputArgumentType input_argument,
          GetInputArgumentTypeForExpr(
              arg.get(),
              /*pick_default_type_for_untyped_expr=*/
              language().LanguageFeatureEnabled(
                  FEATURE_TEMPLATED_SQL_FUNCTION_RESOLVE_WITH_TYPED_ARGS),
              analyzer_options()));
      input_arguments.push_back(input_argument);
    }
    return input_arguments;
  };

  std::shared_ptr<ResolvedFunctionCallInfo> function_call_info = nullptr;
  if (function->Is<TemplatedSQLFunction>()) {
    // We do not support lambdas in UDF yet.
    GOOGLESQL_RET_CHECK((*resolved_function_call)->generic_argument_list().empty())
        << "Should not have generic arguments";

    // TODO: The core of this block is in common with code
    // in function_resolver.cc for handling templated scalar functions.
    // Refactor the common code into common helper functions.
    function_call_info.reset(new ResolvedFunctionCallInfo());
    const TemplatedSQLFunction* sql_function =
        function->GetAs<TemplatedSQLFunction>();
    GOOGLESQL_ASSIGN_OR_RETURN(std::vector<InputArgumentType> input_arguments,
                     compute_input_argument_types(
                         (*resolved_function_call)->argument_list()));

    // Call the TemplatedSQLFunction::Resolve() method to get the output type.
    // Use a new empty cycle detector, or the cycle detector from this
    // Resolver if we are analyzing one or more templated function calls.
    CycleDetector owned_cycle_detector;
    AnalyzerOptions analyzer_options = analyzer_options_;
    if (analyzer_options.find_options().cycle_detector() == nullptr) {
      analyzer_options.mutable_find_options()->set_cycle_detector(
          &owned_cycle_detector);
    }

    const absl::Status resolve_status =
        function_resolver_->ResolveTemplatedSQLFunctionCall(
            ast_function_call, *sql_function, analyzer_options, input_arguments,
            arg_list, (*resolved_function_call)->signature(),
            &function_call_info);

    if (!resolve_status.ok()) {
      // TODO:  This code matches the code from
      // ResolveGeneralFunctionCall() in function_resolver.cc for handling
      // templated scalar SQL functions.  There is similar, but not
      // equivalent code for handling templated TVFs.  We should look
      // at making them all consistent, and maybe implementing the
      // common code through a helper function.
      //
      // The Resolve method returned an error status that is already updated
      // based on the <analyzer_options> ErrorMessageMode.  Make a new
      // ErrorSource based on the <resolve_status>, and return a new error
      // status that indicates that the function call is invalid, while
      // indicating the function call location for the error.
      return WrapNestedErrorStatus(
          ast_function_call,
          absl::StrCat("Invalid function ", sql_function->Name()),
          resolve_status, analyzer_options_.error_message_mode());
    }

    std::unique_ptr<FunctionSignature> new_signature =
        std::make_unique<FunctionSignature>(
            (*resolved_function_call)->signature());

    new_signature->SetConcreteResultType(
        function_call_info->GetAs<TemplatedSQLFunctionCall>()->expr()->type());

    (*resolved_function_call)->set_signature(*new_signature);
    GOOGLESQL_RET_CHECK((*resolved_function_call)->signature().IsConcrete())
        << "result_signature: '"
        << (*resolved_function_call)->signature().DebugString() << "'";
  }

  // Propagate back any scoping information discovered while resolving the
  // nested aggs. The state must be compatible because it was checked while
  // analyzing nested aggs.
  GOOGLESQL_RET_CHECK(query_resolution_info != nullptr);
  *query_resolution_info->scoped_aggregation_state() =
      *multi_level_aggregate_info->scoped_aggregation_state();

  GOOGLESQL_ASSIGN_OR_RETURN(
      std::vector<std::unique_ptr<const ResolvedComputedColumnBase>>
          aggregate_columns_to_compute,
      GetNestedAggregateColumns(*multi_level_aggregate_info));
  GOOGLESQL_RETURN_IF_ERROR(EnsureNestedAggregatesSupportGroupByModifier(
      ast_function_call, aggregate_columns_to_compute));

  // Build an AggregateFunctionCall to replace the regular FunctionCall.
  std::unique_ptr<ResolvedAggregateFunctionCall> resolved_agg_call =
      MakeResolvedAggregateFunctionCall(
          (*resolved_function_call)->type(), function,
          (*resolved_function_call)->signature(),
          (*resolved_function_call)->release_argument_list(),
          (*resolved_function_call)->release_generic_argument_list(),
          (*resolved_function_call)->error_mode(),
          ast_function_call->distinct(), resolved_null_handling_modifier,
          std::move(resolved_where_expr), std::move(resolved_having_modifier),
          std::move(resolved_order_by_items), std::move(limit_expr),
          function_call_info,
          multi_level_aggregate_info->release_group_by_columns_to_compute(),
          std::move(aggregate_columns_to_compute),
          std::move(resolved_having_expr));
  if (ast_function_call->group_by() != nullptr &&
      ast_function_call->group_by()->hint() != nullptr) {
    std::vector<std::unique_ptr<const ResolvedOption>> resolved_hints;
    GOOGLESQL_RETURN_IF_ERROR(ResolveHintAndAppend(ast_function_call->group_by()->hint(),
                                         &resolved_hints));
    resolved_agg_call->set_group_by_hint_list(std::move(resolved_hints));
  }
  resolved_agg_call->set_hint_list(
      (*resolved_function_call)->release_hint_list());
  MaybeRecordFunctionCallParseLocation(ast_function_call,
                                       resolved_agg_call.get());

  // In the first resolution, we didn't look at "DISTINCT" so we need to
  // re-resolve collation.
  // TODO: this logic should be moved to the function resolver.
  GOOGLESQL_RETURN_IF_ERROR(MaybeResolveCollationForFunctionCallBase(
      /*error_location=*/ast_function_call, resolved_agg_call.get()));

  // We already propagated annotations before.
  resolved_agg_call->set_type_annotation_map(
      resolved_function_call->get()->type_annotation_map());

  if (expr_resolution_info->in_horizontal_aggregation) {
    auto& info = expr_resolution_info->horizontal_aggregation_info;
    if (info.has_value()) {
      auto& [array_column, array_is_correlated, element_column] = *info;
      const Type* type = resolved_agg_call->type();
      GOOGLESQL_ASSIGN_OR_RETURN(
          *resolved_expr_out,
          ResolvedArrayAggregateBuilder()
              .set_type(type)
              .set_aggregate(std::move(resolved_agg_call))
              .set_array(MakeResolvedColumnRef(
                  array_column.type(), array_column, array_is_correlated))
              .set_element_column(element_column)
              .set_pre_aggregate_computed_column_list(
                  std::move(computed_columns_for_horizontal_aggregation))
              .Build());
      // In case there's a sibling horizontal aggregation we have to reset this
      // map. Example: SUM(x) + SUM(y).
      info.reset();
      return absl::OkStatus();
    } else {
      return MakeSqlErrorAt(ast_function_call)
             << "Horizontal aggregation without an array-typed variable is not "
                "allowed. Normal vertical aggregation is not allowed in this "
                "syntactic context";
    }
  } else {
    GOOGLESQL_RET_CHECK(!expr_resolution_info->horizontal_aggregation_info.has_value());
  }

  // If a GROUPING function is being evaluated, return early without adding
  // the resolved_aggregate_call to the aggregate_columns_to_compute. This is
  // because GROUPING is a special aggregate function with its resolvedAST
  // being represented in the aggregate_scan->grouping_call_list.
  if (IsGroupingFunction(function)) {
    if (expr_resolution_info->query_resolution_info->IsGqlReturn()) {
      return MakeSqlErrorAt(ast_function_call)
             << "GROUPING function is not allowed in RETURN";
    }
    if (analyzing_expression_) {
      return MakeSqlErrorAt(ast_function_call)
             << "GROUPING function is not supported in standalone expression "
                "resolution";
    }
    std::unique_ptr<ResolvedColumn> resolved_grouping_column;
    GOOGLESQL_RETURN_IF_ERROR(AddColumnToGroupingListFirstPass(
        ast_function_call, std::move(resolved_agg_call), expr_resolution_info,
        &resolved_grouping_column));
    *resolved_expr_out = MakeColumnRef(*resolved_grouping_column);
    return absl::OkStatus();
  }

  // When analyzing standalone expressions, we want to inline the aggregate
  // function call within the expression tree rather than adding it to
  // QueryResolutionInfo. There are 3 cases where we don't want to do this:
  //
  // 1. If the aggregate function is an argument to a multi-level aggregate
  //    function call.
  // 2. If the aggregate function is nested within a subquery.
  // 3. If the expression is a graph measure expression.
  //
  // QueryResolutionInfo stores the information to handle cases 1 and 3; the
  // second case is handled by checking for a previous scope.
  if ((analyzing_expression_ &&
       analyzer_options_.allow_aggregate_standalone_expression() &&
       !query_resolution_info->is_nested_aggregation() &&
       expr_resolution_info->name_scope->previous_scope() == nullptr) ||
      expr_resolution_info->is_graph_measure_expression) {
    // For a standalone expression, leave the aggregate call inline rather than
    // put it into a column.
    *resolved_expr_out = std::move(resolved_agg_call);
    GOOGLESQL_RET_CHECK(!multi_level_aggregate_info->scoped_aggregation_state()
                   ->target_pattern_variable_ref.has_value());
    return absl::OkStatus();
  }

  // If this <ast_function_call> is the top level function call in
  // <expr_resolution_info> and it has an alias, then use that alias.
  // Otherwise create an internal alias for this expression.
  IdString alias = GetColumnAliasForTopLevelExpression(expr_resolution_info,
                                                       ast_function_call);
  if (alias.empty() || !analyzer_options_.preserve_column_aliases()) {
    alias = MakeIdString(absl::StrCat(
        "$agg",
        1 + query_resolution_info
                ->num_aggregate_columns_to_compute_across_all_scopes()));
  }

  // Instead of leaving the aggregate ResolvedFunctionCall inline, we pull
  // it out into <query_resolution_info->aggregate_columns_to_compute().
  // The actual ResolvedExpr we return is a ColumnRef pointing to that
  // function call.
  const IdString* query_alias = &kAggregateId;
  if (query_resolution_info->IsGqlReturn() ||
      query_resolution_info->IsGqlWith()) {
    query_alias = &kGraphTableId;
  }
  ResolvedColumn aggregate_column(AllocateColumnId(), *query_alias, alias,
                                  resolved_agg_call->annotated_type());

  std::unique_ptr<const ResolvedComputedColumnBase> computed_column;
  if (language().LanguageFeatureEnabled(
          FEATURE_ENFORCE_CONDITIONAL_EVALUATION) &&
      side_effect_scope_depth_ > 0) {
    ResolvedColumn side_effects_column(AllocateColumnId(), kAggregateId,
                                       MakeIdString("$side_effects"),
                                       types::BytesType());
    out_unconsumed_side_effect_column = side_effects_column;
    GOOGLESQL_ASSIGN_OR_RETURN(computed_column,
                     ResolvedDeferredComputedColumnBuilder()
                         .set_column(aggregate_column)
                         .set_expr(std::move(resolved_agg_call))
                         .set_side_effect_column(side_effects_column)
                         .Build());
  } else {
    GOOGLESQL_ASSIGN_OR_RETURN(computed_column,
                     ResolvedComputedColumnBuilder()
                         .set_column(aggregate_column)
                         .set_expr(std::move(resolved_agg_call))
                         .Build());
  }

  query_resolution_info->AddAggregateComputedColumn(ast_function_call,
                                                    std::move(computed_column));
  if (!query_resolution_info->is_nested_aggregation()) {
    // This is a top-level aggregation, not nested in another.
    // Pin the row range, clear the scoped aggregation state as siblings are
    // free to have different ranges, e.g. MAX(A.x) - MIN(B.x) is a valid
    // expression.
    if (!query_resolution_info->scoped_aggregation_state()
             ->row_range_determined) {
      GOOGLESQL_RETURN_IF_ERROR(query_resolution_info->PinToRowRange(std::nullopt));
    }
    *query_resolution_info->scoped_aggregation_state() = {};
  }

  *resolved_expr_out = MakeColumnRef(aggregate_column);

  return absl::OkStatus();
}

absl::Status Resolver::ResolveExpressionArgument(
    const ASTExpression* arg, ExprResolutionInfo* expr_resolution_info,
    std::vector<std::unique_ptr<const ResolvedExpr>>* resolved_arguments) {
  RETURN_ERROR_IF_OUT_OF_STACK_SPACE();
  std::unique_ptr<const ResolvedExpr> resolved_arg;
  GOOGLESQL_RETURN_IF_ERROR(ResolveExpr(arg, expr_resolution_info, &resolved_arg));
  resolved_arguments->push_back(std::move(resolved_arg));
  return absl::OkStatus();
}

// If the `arg` is an ASTLambda or it is an ASTNamedArgument with an ASTLambda
// `expr`, returns the ASTLambda. Otherwise returns nullptr.
static const ASTLambda* GetLambdaArgument(const ASTExpression* arg) {
  if (arg->Is<ASTLambda>()) {
    return arg->GetAsOrDie<ASTLambda>();
  }
  if (arg->Is<ASTNamedArgument>()) {
    return GetLambdaArgument(arg->GetAsOrDie<ASTNamedArgument>()->expr());
  }
  return nullptr;
}

ABSL_ATTRIBUTE_NOINLINE
absl::Status Resolver::ResolveExpressionArguments(
    ExprResolutionInfo* expr_resolution_info,
    const absl::Span<const ASTExpression* const> arguments,
    const std::map<int, SpecialArgumentType>& argument_option_map,
    std::vector<std::unique_ptr<const ResolvedExpr>>* resolved_arguments_out,
    std::vector<const ASTNode*>* ast_arguments_out,
    const ASTFunctionCall* inside_ast_function_call) {
  RETURN_ERROR_IF_OUT_OF_STACK_SPACE();

  // These reservations could be low for special cases like interval args
  // that turn into multiple elements.  We'll guess we have at most one
  // of those and need at most one extra slot.
  resolved_arguments_out->reserve(arguments.size() + 1);
  ast_arguments_out->reserve(arguments.size() + 1);

  for (int idx = 0; idx < arguments.size(); ++idx) {
    const ASTExpression* arg = arguments[idx];
    const SpecialArgumentType* special_argument_type =
        googlesql_base::FindOrNull(argument_option_map, idx);
    if (special_argument_type != nullptr) {
      // Note: Special argument types assigned to argument 0 do not work in
      // chained function calls.  The base argument is resolved as a standard
      // expression before interpreting the function call.
      switch (*special_argument_type) {
        case SpecialArgumentType::INTERVAL:
          GOOGLESQL_RETURN_IF_ERROR(ResolveIntervalArgument(arg, expr_resolution_info,
                                                  resolved_arguments_out,
                                                  ast_arguments_out));
          break;
        case SpecialArgumentType::DATEPART: {
          std::unique_ptr<const ResolvedExpr> resolved_argument;
          GOOGLESQL_RETURN_IF_ERROR(ResolveDatePartArgument(arg, &resolved_argument));
          resolved_arguments_out->push_back(std::move(resolved_argument));
          ast_arguments_out->push_back(arg);
          break;
        }
        case SpecialArgumentType::NORMALIZE_MODE: {
          std::unique_ptr<const ResolvedExpr> resolved_argument;
          GOOGLESQL_RETURN_IF_ERROR(
              ResolveNormalizeModeArgument(arg, &resolved_argument));
          resolved_arguments_out->push_back(std::move(resolved_argument));
          ast_arguments_out->push_back(arg);
          break;
        }
        case SpecialArgumentType::PROPERTY_NAME: {
          GOOGLESQL_RETURN_IF_ERROR(ResolvePropertyNameArgument(
              arg, *resolved_arguments_out, *ast_arguments_out));
          break;
        }
      }
    } else if (arg->Is<ASTSequenceArg>()) {
      if (!language().LanguageFeatureEnabled(FEATURE_SEQUENCE_ARG)) {
        return MakeSqlErrorAt(arg) << "Sequence args are not supported";
      }
      resolved_arguments_out->push_back(nullptr);
      ast_arguments_out->push_back(arg);
    } else if (const ASTLambda* lambda = GetLambdaArgument(arg);
               lambda != nullptr) {
      // Report error at `lambda` even if `arg` itself is a named argument
      // because validation errors in this branch, if any, are related to
      // lambda, not the name of the argument.
      if (!language().LanguageFeatureEnabled(FEATURE_INLINE_LAMBDA_ARGUMENT)) {
        return MakeSqlErrorAt(lambda) << "Lambda is not supported";
      }
      if (expr_resolution_info->in_horizontal_aggregation) {
        return MakeSqlErrorAt(lambda) << "Lambda arguments are not supported "
                                         "in horizontal aggregation";
      }
      GOOGLESQL_RETURN_IF_ERROR(ValidateLambdaArgumentListIsIdentifierList(lambda));
      // Normally all function arguments are resolved, then signatures are
      // matched against them. Lambdas, such as `e->e>0`, don't have explicit
      // types for the arguments. Types of arguments of lambda can only be
      // inferred based on types of other arguments and functions signature.
      // Then the lambda body referencing the arguments can be resolved. We put
      // nullptrs instead ResolvedExpr in place of lambdas, resolve the lambdas
      // during signature matching and fill in the generic_argument_list
      // appropriately after signature matching. The resolved argument list with
      // nullptr only hangs around during arguments resolving and signature
      // matching.
      //
      resolved_arguments_out->push_back(nullptr);
      ast_arguments_out->push_back(lambda);
    } else {
      absl::Status status = ResolveExpressionArgument(arg, expr_resolution_info,
                                                      resolved_arguments_out);

      // If the first argument inside a chained function call fails to resolve,
      // try giving an error message suggesting a fix to the function call.
      //
      // For example, when the arg is the ASTDotIdentifier for `safe`,
      //   `(expr).safe.sqrt()` should be `(expr).(safe.sqrt)()`.
      //
      // In this case, `sqrt` was also a valid function name without `safe`,
      // so we didn't catch the error when looking up the function name.
      // We catch it when resolving the ASTDotIdentifier `safe` fails.
      if (!status.ok() && idx == 0 && inside_ast_function_call != nullptr &&
          inside_ast_function_call->is_chained_call()) {
        GOOGLESQL_RET_CHECK(!inside_ast_function_call->arguments().empty());
        GOOGLESQL_RET_CHECK_EQ(arg, inside_ast_function_call->arguments().front());

        GOOGLESQL_RETURN_IF_ERROR(TryMakeErrorSuggestionForChainedCall(
            status, inside_ast_function_call));
      }

      GOOGLESQL_RETURN_IF_ERROR(status);
      ast_arguments_out->push_back(arg);
    }
  }
  GOOGLESQL_RET_CHECK_EQ(ast_arguments_out->size(), resolved_arguments_out->size());
  return absl::OkStatus();
}

absl::Status Resolver::ResolveFunctionCallWithResolvedArguments(
    const ASTNode* ast_location,
    const std::vector<const ASTNode*>& arg_locations,
    bool match_internal_signatures, absl::string_view function_name,
    std::vector<std::unique_ptr<const ResolvedExpr>> resolved_arguments,
    std::vector<NamedArgumentInfo> named_arguments,
    ExprResolutionInfo* expr_resolution_info,
    std::unique_ptr<const ResolvedExpr>* resolved_expr_out) {
  const Function* function;
  ResolvedFunctionCallBase::ErrorMode error_mode;
  GOOGLESQL_RETURN_IF_ERROR(LookupFunctionFromCatalog(
      ast_location, {std::string(function_name)},
      FunctionNotFoundHandleMode::kReturnError, &function, &error_mode));
  return ResolveFunctionCallWithResolvedArguments(
      ast_location, arg_locations, match_internal_signatures, function,
      error_mode, std::move(resolved_arguments), std::move(named_arguments),
      expr_resolution_info, /*multi_level_aggregate_info=*/nullptr,
      resolved_expr_out);
}

absl::Status Resolver::ResolveProtoDefaultIfNull(
    const ASTNode* ast_location,
    std::vector<std::unique_ptr<const ResolvedExpr>> resolved_arguments,
    std::unique_ptr<const ResolvedExpr>* resolved_expr_out) {
  GOOGLESQL_RET_CHECK_EQ(resolved_arguments.size(), 1);

  std::unique_ptr<const ResolvedExpr> resolved_argument =
      std::move(resolved_arguments[0]);
  if (resolved_argument->node_kind() != RESOLVED_GET_PROTO_FIELD) {
    return MakeSqlErrorAt(ast_location) << "The PROTO_DEFAULT_IF_NULL input "
                                           "expression must end with a proto "
                                           "field access";
  }
  const ResolvedGetProtoField* resolved_field_access =
      resolved_argument->GetAs<ResolvedGetProtoField>();
  if (resolved_field_access->type()->IsProto()) {
    return MakeSqlErrorAt(ast_location)
           << "The PROTO_DEFAULT_IF_NULL input expression "
              "cannot access a field with type message; Field "
           << resolved_field_access->field_descriptor()->full_name()
           << " is of message type";
  } else if (resolved_field_access->field_descriptor()->is_required()) {
    return MakeSqlErrorAt(ast_location)
           << "The field accessed by PROTO_DEFAULT_IF_NULL input expression "
              "cannot access a required field; Field "
           << resolved_field_access->field_descriptor()->full_name()
           << " is required";
  } else if (resolved_field_access->get_has_bit()) {
    return MakeSqlErrorAt(ast_location)
           << "The PROTO_DEFAULT_IF_NULL function does not accept expressions "
              "that result in a 'has_' virtual field access";
  }

  // We check the input AST types so this, from
  // PROTO_DEFAULT_IF_NULL(PROTO_DEFAULT_IF_NULL(x)) is impossible.
  GOOGLESQL_RET_CHECK(!resolved_field_access->return_default_value_when_unset());

  if (!ProtoType::GetUseDefaultsExtension(
          resolved_field_access->field_descriptor()) &&
      (resolved_field_access->field_descriptor()->has_presence() ||
       !language().LanguageFeatureEnabled(
           FEATURE_IGNORE_PROTO3_USE_DEFAULTS))) {
    return MakeSqlErrorAt(ast_location)
           << "The field accessed by PROTO_DEFAULT_IF_NULL must have a usable "
              "default value; Field "
           << resolved_field_access->field_descriptor()->full_name()
           << " is annotated to ignore proto defaults";
  }

  // We could eliminate the need for a const_cast by re-resolving the ASTNode
  // argument using ResolveFieldAccess() and ResolveExtensionFieldAccess()
  // directly, but that seems like more trouble than it's worth.
  const_cast<ResolvedGetProtoField*>(resolved_field_access)
      ->set_return_default_value_when_unset(true);

  *resolved_expr_out = std::move(resolved_argument);

  return absl::OkStatus();
}

namespace {

bool IsProtoDefaultIfNull(const Function* function) {
  return function->NumSignatures() == 1 &&
         function->signatures()[0].context_id() == FN_PROTO_DEFAULT_IF_NULL &&
         function->IsGoogleSQLBuiltin();
}

bool IsFlatten(const Function* function) {
  return function->NumSignatures() == 1 &&
         function->signatures()[0].context_id() == FN_FLATTEN &&
         function->IsGoogleSQLBuiltin();
}

bool IsRegexpExtractGroupsFunction(
    const Function* function, const FunctionSignature& function_signature) {
  return function->IsGoogleSQLBuiltin() &&
         (function_signature.context_id() == FN_REGEXP_EXTRACT_GROUPS_STRING ||
          function_signature.context_id() == FN_REGEXP_EXTRACT_GROUPS_BYTES);
}

}  // namespace

absl::StatusOr<std::unique_ptr<const ResolvedExpr>>
Resolver::WrapInASideEffectCall(const ASTNode* ast_location,
                                std::unique_ptr<const ResolvedExpr> expr,
                                const ResolvedColumn& side_effect_column,
                                ExprResolutionInfo& expr_resolution_info) {
  FakeASTNode fake_ast_location;
  std::vector<const ASTNode*> side_effect_arg_locations{&fake_ast_location,
                                                        &fake_ast_location};

  std::vector<std::unique_ptr<const ResolvedExpr>> side_effect_resolved_args;
  side_effect_resolved_args.push_back(std::move(expr));
  side_effect_resolved_args.push_back(MakeColumnRef(side_effect_column));

  std::unique_ptr<const ResolvedExpr> resolved_expr_out;
  GOOGLESQL_RETURN_IF_ERROR(ResolveFunctionCallWithResolvedArguments(
      ast_location, side_effect_arg_locations,
      /*match_internal_signatures=*/true, "$with_side_effects",
      std::move(side_effect_resolved_args),
      /*named_arguments=*/{}, &expr_resolution_info, &resolved_expr_out));
  return std::move(resolved_expr_out);
}

// Returns an error if `resolved_arguments` contain any MEASURE-typed arguments
// and `function` is not the AGGREGATE function, or any argument has ROW type.
static absl::Status EnsureNoIllegallyTypedArguments(
    const Function* function, const std::vector<const ASTNode*>& arg_locations,
    absl::Span<const std::unique_ptr<const ResolvedExpr>> resolved_arguments) {
  GOOGLESQL_RET_CHECK_EQ(arg_locations.size(), resolved_arguments.size());
  for (size_t i = 0; i < resolved_arguments.size(); ++i) {
    if (resolved_arguments[i] == nullptr) {
      continue;
    }
    const ResolvedExpr& resolved_argument = *resolved_arguments[i];
    if (resolved_argument.type()->IsMeasureType() &&
        !IsMeasureAggFunction(function)) {
      return MakeSqlErrorAt(arg_locations[i])
             << "MEASURE-typed arguments are only permitted in the AGG "
                "function";
    }
    if (resolved_argument.type()->IsRow()) {
      return MakeSqlErrorAt(arg_locations[i])
             << "ROW-typed values cannot be passed as function arguments";
    }
  }
  return absl::OkStatus();
}

absl::StatusOr<std::unique_ptr<const ResolvedExpr>>
Resolver::ResolveAsMatchRecognizePhysicalNavigationFunction(
    const ASTNode* ast_location, const Function* function,
    ExprResolutionInfo* expr_resolution_info,
    std::unique_ptr<ResolvedFunctionCall> resolved_function_call) {
  GOOGLESQL_RET_CHECK_EQ(function->NumSignatures(), 1);
  GOOGLESQL_RET_CHECK(expr_resolution_info->in_match_recognize_define);
  int64_t signature_id = function->GetSignature(0)->context_id();
  switch (signature_id) {
    case FN_NEXT:
    case FN_PREV: {
      GOOGLESQL_RET_CHECK(expr_resolution_info->query_resolution_info != nullptr);
      GOOGLESQL_RET_CHECK(expr_resolution_info->query_resolution_info->analytic_resolver()
                    ->in_match_recognize_window_context());

      // Resolve as a function call to LEAD/LAG.
      std::string replacement_function_name = "";
      FunctionSignatureId replacement_signature_id;

      if (signature_id == FN_NEXT) {
        replacement_function_name = "lead";
        replacement_signature_id = FN_LEAD;
      } else {
        GOOGLESQL_RET_CHECK_EQ(signature_id, FN_PREV);
        replacement_function_name = "lag";
        replacement_signature_id = FN_LAG;
      }
      const Function* replacement_function = nullptr;
      GOOGLESQL_RET_CHECK_OK(catalog_->FindFunction({replacement_function_name},
                                          &replacement_function,
                                          analyzer_options_.find_options()));

      resolved_function_call->set_function(replacement_function);
      resolved_function_call->set_signature(FunctionSignature(
          resolved_function_call->signature(), replacement_signature_id));

      const ASTFunctionCall* ast_function_call =
          ast_location->GetAsOrDie<ASTFunctionCall>();
      return expr_resolution_info->query_resolution_info->analytic_resolver()
          ->AddToMatchRecognizeMainAnalyticGroup(
              ast_function_call, expr_resolution_info,
              std::move(resolved_function_call),
              /*resolved_window_frame=*/nullptr);
      break;
    }
    default:
      GOOGLESQL_RET_CHECK_FAIL() << "Expected a MATCH_RECOGNIZE special function, found: "
                       << function->QualifiedSQLName(
                              /*capitalize_qualifier=*/true);
  }
}

absl::Status Resolver::ValidateRegexpExtractGroupsResultCast(
    const ResolvedFunctionCall* resolved_function_call, const Type* result_type,
    const Type* cast_result_type) {
  // Given the function call:
  //   REGEXP_EXTRACT_GROUPS(
  //       "abc-123", r"(?<name>[a-z]+)-(?<id__INT64>[0-9])+")
  // `result_type` will be STRUCT<name STRING, id STRING>
  // `cast_result_type` will be STRUCT<name STRING, id INT64>
  GOOGLESQL_RET_CHECK(result_type->IsStruct());
  GOOGLESQL_RET_CHECK(cast_result_type->IsStruct());
  const StructType* result_struct_type = result_type->AsStruct();
  const StructType* castable_result_struct_type = cast_result_type->AsStruct();
  GOOGLESQL_RET_CHECK_EQ(result_struct_type->num_fields(),
               castable_result_struct_type->num_fields());

  // Check whether each field can be cast.
  ExtendedCompositeCastEvaluator unused_extended_conversion_evaluator =
      ExtendedCompositeCastEvaluator::Invalid();
  for (int i = 0; i < result_struct_type->num_fields(); ++i) {
    const StructField& result_field = result_struct_type->field(i);
    const StructField& cast_result_field =
        castable_result_struct_type->field(i);
    GOOGLESQL_RET_CHECK_EQ(result_field.name, cast_result_field.name);

    InputArgumentType field_arg(result_field.type, /*is_query_parameter=*/false,
                                /*is_literal_for_constness=*/false);
    SignatureMatchResult result;
    GOOGLESQL_ASSIGN_OR_RETURN(bool is_field_cast_valid,
                     coercer_.CoercesTo(field_arg, cast_result_field.type,
                                        /*is_explicit=*/true, &result,
                                        &unused_extended_conversion_evaluator));
    if (!is_field_cast_valid) {
      std::string capturing_group = cast_result_field.name.empty()
                                        ? absl::StrCat(i + 1)
                                        : cast_result_field.name;
      return MakeSqlError()
             << "The extracted value for capturing group " << capturing_group
             << " cannot be cast from " << result_field.type->DebugString()
             << " to " << cast_result_field.type->DebugString();
    }
  }

  // Do one final check that the STRUCT->STRUCT cast is valid. This should
  // always succeed, since we have already done thwe field-level checks.
  GOOGLESQL_ASSIGN_OR_RETURN(bool is_struct_cast_valid,
                   CheckExplicitCast(resolved_function_call, cast_result_type,
                                     &unused_extended_conversion_evaluator));
  GOOGLESQL_RET_CHECK(is_struct_cast_valid);
  return absl::OkStatus();
}

namespace {

// Returns a struct type with a field for each capturing group in the regular
// expression. The field types for named groups  with type suffixes are replaced
// with the corresponding simple types.
absl::StatusOr<const Type*> ExtractGroupsAutoCastedResultStruct(
    const functions::RegExp& regexp, const Type* original_result_type,
    TypeFactory* type_factory, const LanguageOptions& language_options) {
  GOOGLESQL_RET_CHECK(original_result_type->IsStruct());
  const std::vector<StructField>& original_struct_fields =
      original_result_type->AsStruct()->fields();

  GOOGLESQL_ASSIGN_OR_RETURN(functions::RegExp::ParsedCapturingGroups group_infos,
                   regexp.ParseCapturingGroups());
  GOOGLESQL_RET_CHECK_EQ(group_infos.size(), original_struct_fields.size());

  std::vector<StructField> struct_fields;
  struct_fields.reserve(group_infos.size());

  for (int i = 0; i < group_infos.size(); ++i) {
    const functions::RegExp::CapturingGroupInfo& group_info = group_infos[i];
    GOOGLESQL_RET_CHECK_EQ(group_info.field_name, original_struct_fields[i].name);
    // Unnamed group.
    if (group_info.type_suffix.empty()) {
      struct_fields.push_back(original_struct_fields[i]);
      continue;
    }

    // Named group.
    const Type* field_type = nullptr;
    TypeKind type_kind = Type::ResolveBuiltinTypeNameToKindIfSimple(
        group_info.type_suffix, language_options);
    if (type_kind != TYPE_UNKNOWN) {
      const Type* mapped_type = type_factory->MakeSimpleType(type_kind);
      if (mapped_type->IsSupportedType(language_options)) {
        field_type = mapped_type;
      }
    }
    if (field_type == nullptr) {
      return absl::InvalidArgumentError(absl::StrCat(
          "Expected a type name as the suffix of the capturing group '",
          group_info.group_name,
          "' in the regexp argument to REGEXP_EXTRACT_GROUPS. The suffix '",
          group_info.type_suffix, "' is not a valid type name."));
    }
    struct_fields.push_back({std::string(group_info.field_name), field_type});
  }

  const Type* result_type = nullptr;
  GOOGLESQL_RETURN_IF_ERROR(type_factory->MakeStructTypeFromVector(
      std::move(struct_fields), &result_type));
  return result_type;
}

}  // namespace

absl::StatusOr<std::unique_ptr<const ResolvedExpr>>
Resolver::ResolveAsRegexpExtractGroupsFunction(
    const ASTNode*, const Function* function,
    ExprResolutionInfo* expr_resolution_info,
    std::unique_ptr<ResolvedFunctionCall> resolved_function_call) {
  GOOGLESQL_RET_CHECK(resolved_function_call->argument_list_size() == 2);
  // TODO: b/429933160 - Support analysis-constant regexp.
  GOOGLESQL_RET_CHECK(resolved_function_call->argument_list(1)->Is<ResolvedLiteral>());
  const Value& regexp_value = resolved_function_call->argument_list(1)
                                  ->GetAs<ResolvedLiteral>()
                                  ->value();
  GOOGLESQL_RET_CHECK(regexp_value.type()->IsString() || regexp_value.type()->IsBytes());

  GOOGLESQL_ASSIGN_OR_RETURN(
      std::unique_ptr<const functions::RegExp> regexp,
      regexp_value.type()->IsString()
          ? functions::MakeRegExpUtf8(regexp_value.string_value())
          : functions::MakeRegExpBytes(regexp_value.bytes_value()));

  // Get the result type from the resolved function call and check that it
  // matches the expected type.
  const Type* result_type =
      resolved_function_call->signature().result_type().type();
  GOOGLESQL_RET_CHECK(result_type->IsStruct());
  {
    GOOGLESQL_ASSIGN_OR_RETURN(const Type* expected_result_type,
                     regexp->ExtractGroupsResultStruct(type_factory_));
    GOOGLESQL_RET_CHECK(result_type->Equals(expected_result_type))
        << "Return type in signature does not match the expected type "
        << result_type->DebugString() << " vs "
        << expected_result_type->DebugString();
  }

  GOOGLESQL_ASSIGN_OR_RETURN(const Type* cast_result_type,
                   ExtractGroupsAutoCastedResultStruct(
                       *regexp, result_type, type_factory_, language()));
  GOOGLESQL_RET_CHECK_NE(cast_result_type, nullptr);
  if (result_type->Equals(cast_result_type)) {
    // If the types are identical, then no cast is needed.
    return resolved_function_call;
  }

  GOOGLESQL_RETURN_IF_ERROR(ValidateRegexpExtractGroupsResultCast(
      resolved_function_call.get(), result_type, cast_result_type));

  return MakeResolvedCast(cast_result_type, std::move(resolved_function_call),
                          /* return_null_on_error= */ false,
                          /* extended_cast= */ {}, /* format= */ {},
                          /* time_zone= */ {}, TypeModifiers());
}

ABSL_ATTRIBUTE_NOINLINE
absl::Status Resolver::ResolveFunctionCallWithResolvedArguments(
    const ASTNode* ast_location,
    const std::vector<const ASTNode*>& arg_locations,
    bool match_internal_signatures, const Function* function,
    ResolvedFunctionCallBase::ErrorMode error_mode,
    std::vector<std::unique_ptr<const ResolvedExpr>> resolved_arguments,
    std::vector<NamedArgumentInfo> named_arguments,
    ExprResolutionInfo* expr_resolution_info,
    std::unique_ptr<QueryResolutionInfo> multi_level_aggregate_info,
    std::unique_ptr<const ResolvedExpr>* resolved_expr_out) {
  RETURN_ERROR_IF_OUT_OF_STACK_SPACE();

  GOOGLESQL_RETURN_IF_ERROR(EnsureNoIllegallyTypedArguments(function, arg_locations,
                                                  resolved_arguments));

  // Generated columns, ABSL_CHECK constraints, and expressions that are stored in an
  // index have specific limitations on VOLATILE/STABLE functions. GoogleSQL
  // relies upon each engine to provide volatility information for non-builtin
  // functions (including user defined functions).
  // TODO: Checking function volalitity is still not enough. A
  // ColumnRef can be volatile if it's a logical generated column that calls
  // other volatile functions. Such column cannot be used in places where
  // volatility is not allowed.
  if (analyzing_nonvolatile_stored_expression_columns_ &&
      function->function_options().volatility == FunctionEnums::VOLATILE) {
    return MakeSqlErrorAt(ast_location)
           << function->QualifiedSQLName(/* capitalize_qualifier= */ true)
           << " is not allowed in expressions that are stored as each "
              "invocation might return a different value";
  }
  if (analyzing_check_constraint_expression_ &&
      function->function_options().volatility != FunctionEnums::IMMUTABLE) {
    return MakeSqlErrorAt(ast_location)
           << function->QualifiedSQLName(/* capitalize_qualifier= */ true)
           << " is not allowed in CHECK"
           << " constraint expression as each invocation might return a "
              "different value";
  }

  // Now resolve the function call, including overload resolution.
  std::unique_ptr<ResolvedFunctionCall> resolved_function_call;

  GOOGLESQL_RETURN_IF_ERROR(function_resolver_->ResolveGeneralFunctionCall(
      ast_location, arg_locations, match_internal_signatures, function,
      error_mode,
      /*is_analytic=*/false, std::move(resolved_arguments),
      std::move(named_arguments), /*expected_result_type=*/nullptr,
      expr_resolution_info->name_scope, &resolved_function_call));

  GOOGLESQL_RET_CHECK_NE(
      resolved_function_call->signature().result_type().original_kind(),
      __SignatureArgumentKind__switch_must_have_a_default__);

  if (function->IsDeprecated()) {
    GOOGLESQL_RETURN_IF_ERROR(AddDeprecationWarning(
        ast_location, DeprecationWarning::DEPRECATED_FUNCTION,
        absl::StrCat(function->QualifiedSQLName(/*capitalize_qualifier=*/true),
                     " is deprecated")));
  }
  if (resolved_function_call->signature().IsDeprecated()) {
    GOOGLESQL_RETURN_IF_ERROR(AddDeprecationWarning(
        ast_location, DeprecationWarning::DEPRECATED_FUNCTION_SIGNATURE,
        absl::StrCat("Using a deprecated function signature for ",
                     function->QualifiedSQLName())));
  }

  // Down-casting the 'ast_location' pointer doesn't seem like the right thing
  // to be doing here. Probably we should be plumbing the hints into this code
  // explicitly.
  // TODO: Eliminate this bad code smell.
  const auto* ast_function = ast_location->GetAsOrNull<ASTFunctionCall>();
  if (ast_function != nullptr) {
    GOOGLESQL_RETURN_IF_ERROR(ResolveHintsForNode(ast_function->hint(),
                                        resolved_function_call.get()));
  }

  GOOGLESQL_RETURN_IF_ERROR(AddAdditionalDeprecationWarningsForCalledFunction(
      ast_location, resolved_function_call->signature(),
      function->QualifiedSQLName(/*capitalize_qualifier=*/true),
      /*is_tvf=*/false));

  // If the function call resolved to an aggregate function, then do the
  // extra processing required for aggregates.
  if (multi_level_aggregate_info == nullptr) {
    multi_level_aggregate_info = std::make_unique<QueryResolutionInfo>(
        this, expr_resolution_info->query_resolution_info);
  }
  if (function->IsAggregate()) {
    const ASTFunctionCall* ast_function_call =
        ast_location->GetAsOrNull<ASTFunctionCall>();
    GOOGLESQL_RET_CHECK(ast_function_call != nullptr);
    std::optional<ResolvedColumn> unconsumed_side_effect_column;
    GOOGLESQL_RETURN_IF_ERROR(FinishResolvingAggregateFunction(
        ast_function_call, &resolved_function_call, expr_resolution_info,
        std::move(multi_level_aggregate_info), resolved_expr_out,
        unconsumed_side_effect_column));
    if (language().LanguageFeatureEnabled(
            FEATURE_ENFORCE_CONDITIONAL_EVALUATION) &&
        side_effect_scope_depth_ > 0 &&
        !IsGroupingFunction(function)
        // Horizontal aggregates control their own side effects.
        && !expr_resolution_info->in_horizontal_aggregation) {
      // This is an aggregate that got separated from an enclosing side-effect-
      // controlled expression. Wrap the resulting ColumnRef into a function
      // call that ties it with the related side effect columns.
      //
      // One exception is GROUPING() function, it is not added to to the
      // aggregate_columns_to_compute. It is a special aggregate function with
      // its resolvedAST being represented in the
      // aggregate_scan->grouping_call_list.
      GOOGLESQL_RET_CHECK(unconsumed_side_effect_column.has_value())
          << "unconsumed_side_effect_column_ is empty while resolving "
          << ast_function_call->DebugString();

      GOOGLESQL_ASSIGN_OR_RETURN(
          *resolved_expr_out,
          WrapInASideEffectCall(ast_location, std::move(*resolved_expr_out),
                                *unconsumed_side_effect_column,
                                *expr_resolution_info));
    }
    return absl::OkStatus();
  }

  if (function->mode() == Function::ANALYTIC) {
    return MakeSqlErrorAt(ast_location)
           << function->QualifiedSQLName(/*capitalize_qualifier=*/true)
           << " cannot be called without an OVER clause";
  }
  ABSL_DCHECK_EQ(function->mode(), Function::SCALAR);

  // We handle the PROTO_DEFAULT_IF_NULL() function here so that it can be
  // resolved to a ResolvedGetProtoField.
  if (IsProtoDefaultIfNull(function)) {
    if (!language().LanguageFeatureEnabled(FEATURE_PROTO_DEFAULT_IF_NULL)) {
      return MakeSqlErrorAt(ast_location)
             << "The PROTO_DEFAULT_IF_NULL function is not supported";
    }
    return ResolveProtoDefaultIfNull(
        ast_location, resolved_function_call->release_argument_list(),
        resolved_expr_out);
  }

  if (IsFlatten(function)) {
    if (expr_resolution_info->in_horizontal_aggregation) {
      return MakeSqlErrorAt(ast_location)
             << "Horizontal aggregation expression must not include FLATTEN";
    }
    GOOGLESQL_RET_CHECK(
        language().LanguageFeatureEnabled(FEATURE_UNNEST_AND_FLATTEN_ARRAYS))
        << "The FLATTEN function is not supported";
    GOOGLESQL_RET_CHECK_EQ(1, resolved_function_call->argument_list_size());
    *resolved_expr_out =
        std::move(resolved_function_call->release_argument_list()[0]);
    return absl::OkStatus();
  }

  if (IsMatchRecognizeMeasuresFunction(function)) {
    GOOGLESQL_RET_CHECK_EQ(function->NumSignatures(), 1);
    GOOGLESQL_RET_CHECK(match_recognize_state_.has_value());
    GOOGLESQL_RET_CHECK(!expr_resolution_info->in_match_recognize_define);
    if (!expr_resolution_info->name_scope->allows_match_number_function()) {
      // MR state is initialized, but the namescope doesn't even allow
      // MATCH_NUMBER(). This means that the function is called from a
      // subquery and not directly from the MEASURES clause.
      return MakeSqlErrorAt(ast_location)
             << function->SQLName() << " must be called directly from the "
             << "MEASURES clause, not from a subquery";
    }
    int64_t signature_id = function->GetSignature(0)->context_id();
    switch (signature_id) {
      case FN_MATCH_NUMBER: {
        // Rewrite to a column ref, exposing the actual state.
        GOOGLESQL_RET_CHECK(match_recognize_state_->match_number_column.IsInitialized());
        GOOGLESQL_RET_CHECK(
            expr_resolution_info->name_scope->allows_match_number_function());
        *resolved_expr_out =
            MakeColumnRef(match_recognize_state_->match_number_column);
        GOOGLESQL_RET_CHECK_EQ(resolved_expr_out->get()->type()->kind(), TYPE_INT64);
        break;
      }
      case FN_MATCH_ROW_NUMBER: {
        GOOGLESQL_RET_CHECK(
            match_recognize_state_->match_row_number_column.IsInitialized());
        if (!expr_resolution_info->name_scope->allows_match_row_functions()) {
          return MakeSqlErrorAt(ast_location)
                 << function->QualifiedSQLName(/*capitalize_qualifier=*/true)
                 << " must be aggregated";
        }
        // Rewrite to a column ref, exposing the actual state.
        *resolved_expr_out =
            MakeColumnRef(match_recognize_state_->match_row_number_column);
        GOOGLESQL_RET_CHECK_EQ(resolved_expr_out->get()->type()->kind(), TYPE_INT64);
        break;
      }
      case FN_CLASSIFIER: {
        // Rewrite to a column ref, exposing the actual state.
        if (!expr_resolution_info->name_scope->allows_match_row_functions()) {
          return MakeSqlErrorAt(ast_location)
                 << function->QualifiedSQLName(/*capitalize_qualifier=*/true)
                 << " must be aggregated";
        }
        GOOGLESQL_RET_CHECK(match_recognize_state_->classifier_column.IsInitialized());
        *resolved_expr_out =
            MakeColumnRef(match_recognize_state_->classifier_column);
        GOOGLESQL_RET_CHECK_EQ(resolved_expr_out->get()->type()->kind(), TYPE_STRING);
        break;
      }
      default:
        GOOGLESQL_RET_CHECK_FAIL()
            << "Expected a MATCH_RECOGNIZE special function, found: "
            << function->QualifiedSQLName(/*capitalize_qualifier=*/true);
    }
    return absl::OkStatus();
  }

  if (IsMatchRecognizePhysicalNavigationFunction(function)) {
    GOOGLESQL_ASSIGN_OR_RETURN(*resolved_expr_out,
                     ResolveAsMatchRecognizePhysicalNavigationFunction(
                         ast_location, function, expr_resolution_info,
                         std::move(resolved_function_call)));

    return absl::OkStatus();
  }

  if (IsRegexpExtractGroupsFunction(function,
                                    resolved_function_call->signature())) {
    GOOGLESQL_ASSIGN_OR_RETURN(*resolved_expr_out,
                     ResolveAsRegexpExtractGroupsFunction(
                         ast_location, function, expr_resolution_info,
                         std::move(resolved_function_call)),
                     _.With(googlesql::LocationOverride(ast_location)));
    return absl::OkStatus();
  }

  *resolved_expr_out = std::move(resolved_function_call);
  return absl::OkStatus();
}

ABSL_ATTRIBUTE_NOINLINE
absl::Status Resolver::LookupFunctionFromCatalog(
    const ASTNode* ast_location,
    absl::Span<const std::string> function_name_path,
    FunctionNotFoundHandleMode handle_mode, const Function** function,
    ResolvedFunctionCallBase::ErrorMode* error_mode) const {
  *error_mode = ResolvedFunctionCallBase::DEFAULT_ERROR_MODE;

  // This is the function name path with SAFE stripped off, if applicable.
  absl::Span<const std::string> stripped_name = function_name_path;
  bool is_stripped = false;

  absl::Status find_status;

  // Look up the SAFE name first, to avoid tricky cases where a UDF could
  // be used to hide a builtin if the engine didn't block that.
  if (function_name_path.size() > 1 &&
      googlesql_base::CaseEqual(function_name_path[0], "SAFE")) {
    // Check this first because we don't want behavior to vary based
    // on feature flags, other than letting a query succeed.
    if (!language().LanguageFeatureEnabled(FEATURE_SAFE_FUNCTION_CALL)) {
      return MakeSqlErrorAtLocalNode(ast_location)
             << "Function calls with SAFE are not supported";
    }

    stripped_name.remove_prefix(1);
    is_stripped = true;
    find_status = catalog_->FindFunction(stripped_name, function,
                                         analyzer_options_.find_options());
    if (find_status.ok()) {
      if (!(*function)->SupportsSafeErrorMode()) {
        return MakeSqlErrorAtLocalNode(ast_location)
               << "Function " << IdentifierPathToString(stripped_name)
               << " does not support SAFE error mode";
      }
      *error_mode = ResolvedFunctionCallBase::SAFE_ERROR_MODE;
    }
  } else {
    find_status = catalog_->FindFunction(function_name_path, function,
                                         analyzer_options_.find_options());
  }

  bool function_lookup_succeeded = find_status.ok();
  bool all_required_features_are_enabled = true;
  if (function_lookup_succeeded) {
    all_required_features_are_enabled =
        (*function)->function_options().CheckAllRequiredFeaturesAreEnabled(
            language().GetEnabledLanguageFeatures());

    // GROUPING isn't a real function since its argument isn't evaluated
    // as an expression.  It shouldn't be allowed in chained call syntax.
    const auto* ast_function = ast_location->GetAsOrNull<ASTFunctionCall>();
    if (ast_function != nullptr && ast_function->is_chained_call() &&
        IsGroupingFunction(*function)) {
      return MakeSqlErrorAt(ast_function)
             << "Function " << IdentifierPathToString(function_name_path)
             << " does not support chained function calls";
    }
  }

  // We return UNIMPLEMENTED errors from the Catalog directly to the caller.
  if (find_status.code() == absl::StatusCode::kUnimplemented ||
      find_status.code() == absl::StatusCode::kPermissionDenied ||
      (find_status.code() == absl::StatusCode::kNotFound &&
       handle_mode == FunctionNotFoundHandleMode::kReturnNotFound)) {
    return find_status;
  }

  if (!find_status.ok() && find_status.code() != absl::StatusCode::kNotFound) {
    // The FindFunction() call can return an invalid argument error, for
    // example, when looking up LazyResolutionFunctions (which are resolved
    // upon lookup).
    //
    // Rather than directly return the <find_status>, we update the location
    // of the error to indicate the function call in this statement.  We also
    // preserve the ErrorSource payload from <find_status>, since that
    // indicates source errors for this error.
    return WrapNestedErrorStatus(
        ast_location,
        absl::StrCat("Invalid function ",
                     IdentifierPathToString(stripped_name)),
        find_status, analyzer_options_.error_message_mode());
  } else if (find_status.code() == absl::StatusCode::kNotFound ||
             !all_required_features_are_enabled) {
    std::string error_message;
    const TableValuedFunction* tvf = nullptr;
    if (catalog_
            ->FindTableValuedFunction(function_name_path, &tvf,
                                      analyzer_options_.find_options())
            .ok()) {
      absl::StrAppend(&error_message,
                      "Table-valued function is not expected here: ",
                      IdentifierPathToString(stripped_name));
    } else {
      absl::StrAppend(&error_message, "Function not found: ",
                      IdentifierPathToString(stripped_name));
    }
    if (all_required_features_are_enabled) {
      // Do not make a suggestion if the function is disabled because of missing
      // language features. By default that function will be added to the
      // function map, thus the function itself will be the suggestion result,
      // which is confusing.
      const std::string function_suggestion =
          catalog_->SuggestFunction(stripped_name);
      if (!function_suggestion.empty()) {
        absl::StrAppend(
            &error_message, "; Did you mean ",
            is_stripped ? absl::StrCat(function_name_path[0], ".") : "",
            function_suggestion, "?");
      }
    }
    return MakeSqlErrorAtLocalNode(ast_location) << error_message;
  }
  return find_status;
}

ABSL_ATTRIBUTE_NOINLINE
absl::Status Resolver::ResolveFunctionCallByNameWithoutAggregatePropertyCheck(
    const ASTNode* ast_location, absl::string_view function_name,
    const absl::Span<const ASTExpression* const> arguments,
    const std::map<int, SpecialArgumentType>& argument_option_map,
    ExprResolutionInfo* expr_resolution_info,
    std::unique_ptr<const ResolvedExpr>* resolved_expr_out) {
  const std::vector<std::string> function_name_path = {
      std::string(function_name)};
  const Function* function;
  ResolvedFunctionCallBase::ErrorMode error_mode;
  GOOGLESQL_RETURN_IF_ERROR(LookupFunctionFromCatalog(
      ast_location, function_name_path,
      FunctionNotFoundHandleMode::kReturnError, &function, &error_mode));
  return ResolveFunctionCallImpl(ast_location, function, error_mode, arguments,
                                 argument_option_map, expr_resolution_info,
                                 resolved_expr_out);
}

absl::Status Resolver::ResolveFunctionCallWithLiteralRetry(
    const ASTNode* ast_location, absl::string_view function_name,
    const absl::Span<const ASTExpression* const> arguments,
    const std::map<int, SpecialArgumentType>& argument_option_map,
    ExprResolutionInfo* expr_resolution_info,
    std::unique_ptr<const ResolvedExpr>* resolved_expr_out) {
  RETURN_ERROR_IF_OUT_OF_STACK_SPACE();

  const absl::Status status =
      ResolveFunctionCallByNameWithoutAggregatePropertyCheck(
          ast_location, function_name, arguments, argument_option_map,
          expr_resolution_info, resolved_expr_out);
  if (status.ok() || status.code() != absl::StatusCode::kInvalidArgument) {
    return status;
  }

  // If initial resolution failed, then try marking the literals as explicitly
  // typed.  This blocks literal coercion, which fails for cases like
  //   `where KitchenSink.int32_val BETWEEN -1 AND 5000000000`
  // Then casts will be added, making this valid.
  //
  // The comment previous said this is also used for
  //   `where KitchenSink.int32_val < 5000000000`
  // but that no longer seems true.  This is now used only for BETWEEN.
  const std::vector<std::string> function_name_path{std::string(function_name)};
  const Function* function;
  ResolvedFunctionCallBase::ErrorMode error_mode;
  GOOGLESQL_RETURN_IF_ERROR(LookupFunctionFromCatalog(
      ast_location, function_name_path,
      FunctionNotFoundHandleMode::kReturnError, &function, &error_mode));
  std::vector<std::unique_ptr<const ResolvedExpr>> resolved_arguments;
  std::vector<const ASTNode*> ast_arguments;
  GOOGLESQL_RETURN_IF_ERROR(ResolveExpressionArguments(
      expr_resolution_info, arguments, argument_option_map, &resolved_arguments,
      &ast_arguments));

  // After resolving the arguments, mark literal arguments as having an explicit
  // type.
  GOOGLESQL_RETURN_IF_ERROR(UpdateLiteralsToExplicit(arguments, &resolved_arguments));
  const absl::Status new_status = ResolveFunctionCallWithResolvedArguments(
      ast_location, ast_arguments, /*match_internal_signatures=*/false,
      function, error_mode, std::move(resolved_arguments),
      /*named_arguments=*/{}, expr_resolution_info,
      /*multi_level_aggregate_info=*/nullptr, resolved_expr_out);
  if (!new_status.ok()) {
    // Return the original error.
    return status;
  }
  return absl::OkStatus();
}

absl::Status Resolver::UpdateLiteralsToExplicit(
    const absl::Span<const ASTExpression* const> ast_arguments,
    std::vector<std::unique_ptr<const ResolvedExpr>>* resolved_expr_list) {
  RETURN_ERROR_IF_OUT_OF_STACK_SPACE();

  GOOGLESQL_RET_CHECK_EQ(ast_arguments.size(), resolved_expr_list->size());
  for (int i = 0; i < resolved_expr_list->size(); ++i) {
    const ResolvedExpr* expr = (*resolved_expr_list)[i].get();
    if (expr->node_kind() != RESOLVED_LITERAL) {
      continue;
    }
    const ResolvedLiteral* expr_literal = expr->GetAs<ResolvedLiteral>();
    // Skip the cast if the literal is already explicitly typed.
    if (expr_literal->has_explicit_type()) {
      continue;
    }
    GOOGLESQL_RETURN_IF_ERROR(function_resolver_->AddCastOrConvertLiteral(
        ast_arguments[i], expr->type(), /*format=*/nullptr,
        /*time_zone=*/nullptr, TypeParameters(), /*scan=*/nullptr,
        /*set_has_explicit_type=*/true,
        /*return_null_on_error=*/false, &(*resolved_expr_list)[i]));
  }
  return absl::OkStatus();
}

// Creates a post-grouping namescope to resolve arguments to a multi-level
// aggregate function call. The post-grouping namescope must ensure that any
// arguments to the multi-level aggregate function call are resolved to a
// column from the `group_by_list` or `group_by_aggregate_list` (or a correlated
// column).
//
// However, if `function_argument_info` is not null, then it is possible for a
// name to be resolved to a `ResolvedArgumentRef`. This typically happens when
// resolving a multi-level aggregate function call in the body of a UDF, UDA or
// TVF. Such a resolution is permitted ONLY if the `ResolvedArgumentRef` is a
// 'grouping constant' within the context of the multi-level aggregate function
// call. The only scenario where the `ResolvedArgumentRef` is NOT a
// 'grouping constant' is when the argument is an AGGREGATE argument that is not
// explicitly grouped by (e.g. 'SUM(f) GROUP BY e').
//
// Handling this correctly requires constructing the post-grouping namescope in
// a manner such that all AGGREGATE argument names are **shadowed** by a name in
// the post-grouping namescope (or one of its previous namescopes).
//
// TODO: Augment the logic here to support correct lookups of
// `ExpressionColumn` as well.
static absl::StatusOr<std::unique_ptr<NameScope>>
CreatePostGroupingNameScopeForMultiLevelAggregation(
    const FunctionArgumentInfo* function_argument_info,
    const NameScope* aggregate_name_scope,
    QueryResolutionInfo* new_query_resolution_info,
    IdStringPool* id_string_pool) {
  GOOGLESQL_RET_CHECK_NE(aggregate_name_scope, nullptr);

  // Simple case: No `function_argument_info`.
  if (function_argument_info == nullptr) {
    std::unique_ptr<NameScope> post_grouping_name_scope;
    GOOGLESQL_RETURN_IF_ERROR(aggregate_name_scope->CreateNameScopeGivenValidNamePaths(
        new_query_resolution_info->group_by_valid_field_info_map(),
        &post_grouping_name_scope));
    return post_grouping_name_scope;
  }

  // If here, `function_argument_info` is not null and name may resolve to a
  // `ResolvedArgumentRef`. This can result in incorrect ResolvedASTs for
  // multi-level aggregates.
  //
  // For example, consider a UDA with the body 'SUM(f GROUP BY e)', where f is
  // an AGGREGATE argument. Without proper handling, the ResolvedAST for this
  // function call will look like:
  //
  // +-aggregate_expression_list=
  //   +-$agg1#1 :=
  //     +-AggregateFunctionCall(GoogleSQL:sum(INT64) -> INT64)
  //       +-ArgumentRef(..., type=INT64, name="f", ...)
  //       +-group_by_list=
  //         +-$groupbymod#1 := ArgumentRef(..., type=INT64, name="e", ...)
  //
  // This is incorrect, because the argument 'f' is not a grouping constant
  // within the context of the multi-level aggregate function call. Grouping
  // constness is currently enforced via namescope lookups, and names that bind
  // as `ResolvedArgumentRef` are those that cannot be found via the namescope
  // lookup.
  //
  // To handle this correctly, we must ensure that all AGGREGATE args in
  // `function_argument_info` are shadowed by a name in the post-grouping
  // namescope (or one of its previous scopes). Shadowing will create
  // ACCESS_ERROR NameTargets in the post-grouping namescope (with valid name
  // path information) to ensure that the correct fields (and columns) are
  // available for AGGREGATE args in the post-grouping namescope.

  // Begin post-grouping namescope construction.

  // Step 1: Find valid name paths for each `ResolvedArgumentRef` in the
  // `group_by_column_state_list` (regardless of whether the argument will be
  // shadowed). Note that a single name can map to multiple columns if multiple
  // GROUP BY modifiers reference the same name (e.g. GROUP BY struct_arg.field1
  // ,struct_arg.field2).
  IdStringHashMapCase<absl::flat_hash_map<ResolvedColumn, ValidNamePath>>
      arg_name_info;
  for (const GroupByColumnState& group_by_column_state :
       new_query_resolution_info->group_by_column_state_list()) {
    ValidNamePath valid_name_path;
    const ResolvedColumn& target_column =
        group_by_column_state.computed_column->column();
    const ResolvedArgumentRef* argument_ref = GetSourceArgumentRefAndNamePath(
        group_by_column_state.computed_column->expr(), target_column,
        &valid_name_path, id_string_pool);
    if (argument_ref == nullptr) {
      continue;
    }
    IdString arg_name = id_string_pool->Make(argument_ref->name());
    arg_name_info[arg_name].insert({target_column, valid_name_path});
  }

  // Step 2: Construct a name list to shadow all AGGREGATE argument names. If an
  // AGGREGATE argument name can be found in `arg_name_info`, update
  // the ValidFieldInfoMap with all valid name paths for the name.
  std::shared_ptr<NameList> name_list = std::make_shared<NameList>();
  for (const FunctionArgumentInfo::ArgumentDetails* arg_details :
       function_argument_info->GetArgumentDetails()) {
    // The only args that need shadowing are AGGREGATE args that have a name not
    // already accessible from a namescope. NOT AGGREGATE args don't need to be
    // shadowed, since they are grouping constants within the context of the
    // multi-level aggregate.
    if (arg_details->arg_kind != ResolvedArgumentRef::AGGREGATE ||
        aggregate_name_scope->HasName(arg_details->name)) {
      continue;
    }

    ResolvedColumn source_column;
    auto it = arg_name_info.find(arg_details->name);
    if (it != arg_name_info.end()) {
      const absl::flat_hash_map<ResolvedColumn, ValidNamePath>& column_info =
          it->second;
      GOOGLESQL_RET_CHECK(!column_info.empty());
      for (const auto& [column, valid_name_path] : column_info) {
        // Doesn't matter which source column we use, as long as it is
        // initialized. `CreateNameScopeGivenValidNamePaths` will mark the name
        // target as an access error with valid name path information specifying
        // columns that are valid to access from that name.
        if (!source_column.IsInitialized()) {
          source_column = column;
        }
        new_query_resolution_info->mutable_group_by_valid_field_info_map()
            ->InsertNamePath(source_column, valid_name_path);
      }
    }
    GOOGLESQL_RETURN_IF_ERROR(name_list->AddColumn(arg_details->name, source_column,
                                         /*is_explicit=*/true));
  }

  // Step 3: Create the post-grouping namescope. First, create a name scope
  // containing the shadowed AGGREGATE args from step 2.
  std::unique_ptr<NameScope> name_scope_with_shadowed_aggregate_args;
  GOOGLESQL_RETURN_IF_ERROR(aggregate_name_scope->CopyNameScopeWithOverridingNames(
      name_list, &name_scope_with_shadowed_aggregate_args));
  // Then, construct the post-grouping namescope using `valid_field_info_map`
  // to indicate valid paths that can be accessed from each name.
  std::unique_ptr<NameScope> post_grouping_name_scope;
  GOOGLESQL_RETURN_IF_ERROR(
      name_scope_with_shadowed_aggregate_args
          ->CreateNameScopeGivenValidNamePaths(
              new_query_resolution_info->group_by_valid_field_info_map(),
              &post_grouping_name_scope));
  return post_grouping_name_scope;
}

absl::Status Resolver::ResolveFunctionCallImpl(
    const ASTNode* ast_location, const Function* function,
    ResolvedFunctionCallBase::ErrorMode error_mode,
    const absl::Span<const ASTExpression* const> arguments,
    const std::map<int, SpecialArgumentType>& argument_option_map,
    ExprResolutionInfo* expr_resolution_info,
    std::unique_ptr<const ResolvedExpr>* resolved_expr_out) {
  RETURN_ERROR_IF_OUT_OF_STACK_SPACE();

  absl::Cleanup scope_depth_cleanup = [this, function] {
    if (function->MaySuppressSideEffects()) {
      side_effect_scope_depth_--;
    }
  };
  if (function->MaySuppressSideEffects()) {
    side_effect_scope_depth_++;
  }

  // Check if the function call contains any named arguments.
  std::vector<NamedArgumentInfo> named_arguments;
  for (int i = 0; i < arguments.size(); ++i) {
    const ASTExpression* arg = arguments[i];
    if (arg->node_kind() == AST_NAMED_ARGUMENT) {
      const ASTNamedArgument* named_arg = arg->GetAs<ASTNamedArgument>();
      named_arguments.emplace_back(named_arg->name()->GetAsIdString(), i,
                                   named_arg);
    }
  }
  GOOGLESQL_RETURN_IF_ERROR(ValidateNamedLambdas(function, arguments));

  if (function->function_options().volatility == FunctionEnums::VOLATILE) {
    expr_resolution_info->findings.has_volatile = true;
  }

  // A "flatten" function allows the child to flatten.
  FlattenState::Restorer restorer;
  GOOGLESQL_RET_CHECK_EQ(nullptr, expr_resolution_info->flatten_state.active_flatten());
  if (IsFlatten(function)) {
    // We check early for too many arguments as otherwise we can GOOGLESQL_RET_CHECK for
    // resolving arguments with flatten in progress which happens before arg
    // count checking.
    if (arguments.size() != 1) {
      return MakeSqlErrorAt(ast_location)
             << "Number of arguments does not match for function FLATTEN. "
                "Supported signature: FLATTEN(ARRAY)";
    }
    expr_resolution_info->flatten_state.set_can_flatten(true, &restorer);
  }

  if (IsProtoDefaultIfNull(function)) {
    for (auto& a : arguments) {
      if (a->node_kind() != AST_DOT_IDENTIFIER &&
          a->node_kind() != AST_DOT_GENERALIZED_FIELD &&
          a->node_kind() != AST_PATH_EXPRESSION) {
        // This list of permitted input types is intended to cover all proto
        // field accesses. The reason for special casing here is that in the
        // resolved tree, the output of these functions looks exactly like a
        // proto access, so they could be repeatedly invoked like
        // PROTO_DEFAULT_IF_NULL(PROTO_DEFAULT_IF_NULL(x)), which is confusing.
        // We considered adding a flag to the resolved node or adding an extra
        // marker node to the tree, but this way is simpler and we believe new
        // AST node types wrapping proto access will be rare.
        return MakeSqlErrorAt(ast_location)
               << "The " << absl::AsciiStrToUpper(function->Name())
               << " input expression must end with a proto field access";
      }
    }
  }

  std::vector<std::unique_ptr<const ResolvedExpr>> resolved_arguments;
  std::vector<const ASTNode*> ast_arguments;

  GOOGLESQL_RET_CHECK(ast_location != nullptr);
  const bool is_ast_function_call = ast_location->Is<ASTFunctionCall>();
  const ASTFunctionCall* ast_function_call =
      ast_location->GetAsOrNull<ASTFunctionCall>();

  std::unique_ptr<QueryResolutionInfo> multi_level_aggregate_info = nullptr;
  ExprResolutionInfo* post_arguments_expr_resolution_info =
      expr_resolution_info;
  std::unique_ptr<NameScope> post_grouping_name_scope = nullptr;
  std::unique_ptr<ExprResolutionInfo> post_grouping_expr_resolution_info =
      nullptr;

  if (!function->IsAggregate() || !is_ast_function_call ||
      (ast_function_call->group_by() == nullptr)) {
    // Simple case: For functions that are not multi-level aggregates, just
    // resolve the arguments and return.
    GOOGLESQL_RETURN_IF_ERROR(ResolveExpressionArguments(
        expr_resolution_info, arguments, argument_option_map,
        &resolved_arguments, &ast_arguments, ast_function_call));
  } else {
    // At this point in the code, the following variants hold:
    //
    // 1. `function` is an aggregate function.
    // 2. `ast_location` is an ASTFunctionCall (not an ASTAnalyticFunctionCall).
    //    This means that the function call does not have the OVER clause and so
    //    is not an aggregate function over an analytic window.
    // 3. `ast_group_by` is not null.
    //
    // Thus, the function call is a multi-level aggregate function.
    //
    // After this point, errors should use `ast_function_call` rather than
    // `ast_location`.
    ast_location = ast_function_call;
    GOOGLESQL_RETURN_IF_ERROR(ResolvedMultiLevelAggregateArguments(
        ast_function_call, function, arguments, argument_option_map,
        expr_resolution_info, multi_level_aggregate_info,
        post_grouping_expr_resolution_info, post_grouping_name_scope,
        resolved_arguments, ast_arguments));
    post_arguments_expr_resolution_info =
        post_grouping_expr_resolution_info.get();
  }

  return ResolveFunctionCallWithResolvedArguments(
      ast_location, ast_arguments, /*match_internal_signatures=*/false,
      function, error_mode, std::move(resolved_arguments),
      std::move(named_arguments), post_arguments_expr_resolution_info,
      std::move(multi_level_aggregate_info), resolved_expr_out);
}

ABSL_ATTRIBUTE_NOINLINE
absl::Status Resolver::ResolvedMultiLevelAggregateArguments(
    const ASTFunctionCall* ast_function_call, const Function* function,
    absl::Span<const ASTExpression* const> arguments,
    const std::map<int, SpecialArgumentType>& argument_option_map,
    ExprResolutionInfo* expr_resolution_info,
    std::unique_ptr<QueryResolutionInfo>& multi_level_aggregate_info,
    std::unique_ptr<ExprResolutionInfo>& post_grouping_expr_resolution_info,
    std::unique_ptr<NameScope>& post_grouping_name_scope,
    std::vector<std::unique_ptr<const ResolvedExpr>>& resolved_arguments,
    std::vector<const ASTNode*>& ast_arguments) {
  GOOGLESQL_RET_CHECK(ast_function_call != nullptr);
  GOOGLESQL_RET_CHECK(ast_function_call->group_by() != nullptr);

  // If aggregation is not allowed in the current context, return an error.
  // This is an 'early-catch' scenario when a multi-level aggregate is used in a
  // context where it is not allowed (e.g. inside a UDF).
  if (!expr_resolution_info->allows_aggregation) {
    return MakeSqlErrorAt(ast_function_call)
           << function->QualifiedSQLName(/*capitalize_qualifier=*/true)
           << " not allowed in " << expr_resolution_info->clause_name;
  }

  // Check if function supports multi-level aggregation.
  if (!function->SupportsGroupByModifier()) {
    // Normal aggregate functions like COUNT become differential privacy
    // functions when used with 'WITH DIFFERENTIAL_PRIVACY'. This can result in
    // confusing (and incorrect) error messages like "COUNT does not support
    // GROUP BY modifiers". To avoid this, we provide more information in the
    // error message to indicate that it is an anonymized / differentially
    // private aggregate function.
    absl::string_view function_type =
        function->Is<AnonFunction>()
            ? " Anonymization / Differential Privacy Function"
            : " Function";
    return MakeSqlErrorAt(ast_function_call)
           << absl::AsciiStrToUpper(function->SQLName()) << function_type
           << " does not support GROUP BY modifiers";
  }

  if (!language().LanguageFeatureEnabled(
          FEATURE_MULTILEVEL_AGGREGATION_ON_UDAS)) {
    if (!function->IsGoogleSQLBuiltin()) {
      return MakeSqlErrorAt(ast_function_call)
             << "GROUP BY modifiers are not yet supported on user-defined "
                "functions.";
    }
  }

  // Helper lambda to unwind a resolved `grouping_expr` and capture any
  // potential columns that may be visible post-grouping. This information will
  // be used to create a post-grouping name scope, which will be used to resolve
  // arguments in step 4.
  auto update_query_resolution_info_with_valid_name_paths =
      [](const ResolvedExpr* grouping_expr, ResolvedColumn target_column,
         QueryResolutionInfo* query_resolution_info,
         IdStringPool* id_string_pool) {
        ResolvedColumn source_column;
        bool is_correlated = false;
        ValidNamePath valid_name_path;
        // `group_by_valid_field_info_map` is used to determine which
        // local-namescope columns are visible post-grouping. Since correlated
        // columns are not looked up from the local-namescope, we can skip
        // adding them to the map.
        if (GetSourceColumnAndNamePath(grouping_expr, target_column,
                                       &source_column, &is_correlated,
                                       &valid_name_path, id_string_pool) &&
            !is_correlated) {
          query_resolution_info->mutable_group_by_valid_field_info_map()
              ->InsertNamePath(source_column, valid_name_path);
          return true;
        }
        return false;
      };

  // Step 1: Create a new `QueryResolutionInfo` and copy over relevant grouping
  // expressions from the current `QueryResolutionInfo`, unless GROUPING SETS,
  // CUBE or ROLLUP are present. The new `QueryResolutionInfo` will be used to
  // resolve nested aggregate function arguments.
  QueryResolutionInfo* current_query_resolution_info =
      expr_resolution_info->query_resolution_info;
  GOOGLESQL_RET_CHECK(current_query_resolution_info != nullptr);
  // The child QRI needs to point to the same ScopedAggregationState as the
  // parent, in order to figure out the correct input range and to detect error
  // cases such as AVG(max(A.x) ORDER BY min(B.y) GROUP BY z)
  // Where A and B are pattern variables in MATCH_RECOGNIZE, since the whole
  // multi-level aggregation must range over the same set of rows.
  multi_level_aggregate_info = std::make_unique<QueryResolutionInfo>(
      this, current_query_resolution_info);

  ResolvedASTDeepCopyVisitor deep_copier;
  if (!current_query_resolution_info->HasGroupByGroupingSets()) {
    for (const GroupByColumnState& group_by_column_state :
         current_query_resolution_info->group_by_column_state_list()) {
      GOOGLESQL_RETURN_IF_ERROR(
          group_by_column_state.computed_column->expr()->Accept(&deep_copier));
      GOOGLESQL_ASSIGN_OR_RETURN(std::unique_ptr<ResolvedExpr> copied_grouping_expr,
                       deep_copier.ConsumeRootNode<ResolvedExpr>());
      ResolvedColumn column(AllocateColumnId(), MakeIdString("$group_by_list"),
                            MakeIdString("$groupbymod"),
                            copied_grouping_expr->annotated_type());
      // Only copy over the grouping expression if it is 'resolvable' as a
      // post-grouping column.
      if (update_query_resolution_info_with_valid_name_paths(
              copied_grouping_expr.get(), column,
              multi_level_aggregate_info.get(), id_string_pool_)) {
        multi_level_aggregate_info->AddGroupByComputedColumnIfNeeded(
            column, std::move(copied_grouping_expr),
            group_by_column_state.pre_group_by_expr,
            /*override_existing_column=*/true);
      } else {
        ValidNamePath unused;
        const ResolvedArgumentRef* argument_ref =
            GetSourceArgumentRefAndNamePath(copied_grouping_expr.get(), column,
                                            &unused, id_string_pool_);
        if (argument_ref != nullptr) {
          multi_level_aggregate_info->AddGroupByComputedColumnIfNeeded(
              column, std::move(copied_grouping_expr),
              group_by_column_state.pre_group_by_expr,
              /*override_existing_column=*/true);
        }
      }
    }
  }

  // Step 2: Resolve all GROUP BY modifiers in the aggregate function, and add
  // them to `multi_level_aggregate_info`.
  // Deliberately use `aggregate_name_scope` for resolving the modifiers; since
  // the normal `name_scope` may be marking non-grouping keys as target access
  // errors (see step 4), which is not correct when resolving grouping items.
  auto grouping_item_expr_resolution_info =
      std::make_unique<ExprResolutionInfo>(
          expr_resolution_info,
          ExprResolutionInfoOptions{
              .name_scope = expr_resolution_info->aggregate_name_scope,
              .allows_aggregation = false,
              .allows_analytic = false,
              .clause_name = "GROUP BY inside aggregate"});
  for (const ASTGroupingItem* grouping_item :
       ast_function_call->group_by()->grouping_items()) {
    std::unique_ptr<const ResolvedExpr> resolved_grouping_expr;
    GOOGLESQL_RETURN_IF_ERROR(ResolveGroupingItem(
        grouping_item, grouping_item_expr_resolution_info.get(),
        &resolved_grouping_expr));
    // If the GROUP BY modifier names an equivalent grouping expression already
    // present in the `multi_level_aggregate_info`, then skip it.
    if (multi_level_aggregate_info->GetEquivalentGroupByComputedColumnOrNull(
            resolved_grouping_expr.get()) != nullptr) {
      continue;
    }
    ResolvedColumn column(AllocateColumnId(), MakeIdString("$group_by_list"),
                          MakeIdString("$groupbymod"),
                          resolved_grouping_expr->annotated_type());
    update_query_resolution_info_with_valid_name_paths(
        resolved_grouping_expr.get(), column, multi_level_aggregate_info.get(),
        id_string_pool_);
    // `override_existing_column` can be false since we check for equivalent
    // columns above.
    multi_level_aggregate_info->AddGroupByComputedColumnIfNeeded(
        column, std::move(resolved_grouping_expr),
        /*pre_group_by_expr=*/nullptr, /*override_existing_column=*/false);
  }

  // Step 4: Construct a new Namescope indicating which columns can be accessed
  // post-grouping.
  GOOGLESQL_ASSIGN_OR_RETURN(
      post_grouping_name_scope,
      CreatePostGroupingNameScopeForMultiLevelAggregation(
          function_argument_info_, expr_resolution_info->aggregate_name_scope,
          multi_level_aggregate_info.get(), id_string_pool_));

  // Step 5: Construct a new `ExprResolutionInfo` from the
  // `multi_level_aggregate_info` and set the `name_scope` to be the
  // `post_grouping_name_scope`. Use the new `ExprResolutionInfo` to resolve
  // the function arguments, with `multi_level_aggregate_info` holding any
  // resolved nested aggregate functions.
  std::unique_ptr<ExprResolutionInfo>
      multi_level_aggregate_expr_resolution_info =
          ExprResolutionInfo::MakeChildForMultiLevelAggregation(
              expr_resolution_info, multi_level_aggregate_info.get(),
              post_grouping_name_scope.get());
  GOOGLESQL_RETURN_IF_ERROR(ResolveExpressionArguments(
      multi_level_aggregate_expr_resolution_info.get(), arguments,
      argument_option_map, &resolved_arguments, &ast_arguments,
      ast_function_call));
  multi_level_aggregate_expr_resolution_info = nullptr;

  // GROUPING function cannot be within a multi-level aggregate.
  if (multi_level_aggregate_info->HasGroupingCall()) {
    return MakeNestedAggregateNotSupportedError(ast_function_call, "GROUPING");
  }

  // Disallow `ExpressionColumn` as multi-level aggregate function arguments.
  // We do not currently support 'grouping-constness' deduction for
  // ExpressionColumns. This is because ExpressionColumns can be resolved via a
  // callback, so the resolver may not have the names of all ExpressionColumns
  // at the time of resolution, which makes constructing a correct post-grouping
  // namescope impossible.
  for (const auto& resolved_arg : resolved_arguments) {
    const ResolvedExpressionColumn* expression_column = nullptr;
    if (ContainsExpressionColumn(resolved_arg.get(), expression_column)) {
      GOOGLESQL_RET_CHECK(expression_column != nullptr);
      return MakeSqlErrorAt(ast_function_call)
             << "Expression column " << expression_column->name()
             << " cannot be an argument to a multi-level aggregate function.";
    }
  }

  // Create a new `ExprResolutionInfo` with the namescope modified to be the
  // `post_grouping_name_scope`, and use it to finish resolving this aggregate
  // function. This ensures aggregate function clauses like ORDER BY and LIMIT
  // are resolved against post-grouping columns.
  post_grouping_expr_resolution_info = std::make_unique<ExprResolutionInfo>(
      expr_resolution_info,
      ExprResolutionInfoOptions{.name_scope = post_grouping_name_scope.get(),
                                .allow_new_scopes = true});
  return absl::OkStatus();
}

IdString Resolver::GetColumnAliasForTopLevelExpression(
    ExprResolutionInfo* expr_resolution_info, const ASTExpression* ast_expr) {
  const IdString alias = expr_resolution_info->column_alias;
  if (expr_resolution_info->top_level_ast_expr == ast_expr &&
      !IsInternalAlias(alias)) {
    return alias;
  }
  return IdString();
}

absl::Status Resolver::CoerceExprToBool(
    const ASTNode* ast_location, absl::string_view clause_name,
    std::unique_ptr<const ResolvedExpr>* resolved_expr) const {
  auto make_error_msg = [clause_name](absl::string_view target_type_name,
                                      absl::string_view actual_type_name) {
    return absl::Substitute("$2 should return type $0, but returns $1",
                            target_type_name, actual_type_name, clause_name);
  };
  return CoerceExprToType(ast_location, type_factory_->get_bool(),
                          kImplicitCoercion, make_error_msg, resolved_expr);
}

ABSL_ATTRIBUTE_NOINLINE
absl::Status Resolver::CoerceExprToType(
    const ASTNode* ast_location, AnnotatedType annotated_target_type,
    CoercionMode mode, CoercionErrorMessageFunction make_error,
    std::unique_ptr<const ResolvedExpr>* resolved_expr) const {
  const Type* target_type = annotated_target_type.type;
  const AnnotationMap* target_type_annotation_map =
      annotated_target_type.annotation_map;
  GOOGLESQL_RET_CHECK_NE(target_type, nullptr);
  GOOGLESQL_RET_CHECK_NE(resolved_expr, nullptr);
  GOOGLESQL_RET_CHECK_NE(resolved_expr->get(), nullptr);
  const AnnotationMap* source_type_annotation_map =
      resolved_expr->get()->type_annotation_map();
  if (target_type_annotation_map != nullptr) {
    GOOGLESQL_RET_CHECK(target_type_annotation_map->HasCompatibleStructure(target_type))
        << "The type annotation map "
        << target_type_annotation_map->DebugString()
        << " is not compatible with the target type "
        << target_type->DebugString();
  }
  if (target_type->Equals(resolved_expr->get()->type()) &&
      AnnotationMap::HasEqualAnnotations(source_type_annotation_map,
                                         target_type_annotation_map,
                                         CollationAnnotation::GetId())) {
    return absl::OkStatus();
  }

  // Untyped NULL can make the Coerce more flexible.
  GOOGLESQL_ASSIGN_OR_RETURN(
      InputArgumentType expr_arg_type,
      GetInputArgumentTypeForExpr(resolved_expr->get(),
                                  /*pick_default_type_for_untyped_expr=*/false,
                                  analyzer_options()));
  SignatureMatchResult sig_match_result;
  Coercer coercer(type_factory_, &language(), catalog_);
  bool success;
  switch (mode) {
    case kImplicitAssignment:
      success = coercer.AssignableTo(expr_arg_type, target_type,
                                     /*is_explicit=*/false, &sig_match_result);
      break;
    case kImplicitCoercion:
      success = coercer.CoercesTo(expr_arg_type, target_type,
                                  /*is_explicit=*/false, &sig_match_result);
      break;
    case kExplicitCoercion:
      success = coercer.CoercesTo(expr_arg_type, target_type,
                                  /*is_explicit=*/true, &sig_match_result);
      break;
  }
  if (!success) {
    const std::string target_type_name =
        target_type->ShortTypeName(language().product_mode());
    const std::string expr_type_name =
        expr_arg_type.UserFacingName(language().product_mode());
    return MakeSqlErrorAt(ast_location)
           << make_error(target_type_name, expr_type_name);
  }

  // The coercion is legal, so implement it by adding a cast.  Note that
  // AddCastOrConvertLiteral() adds a cast node only when necessary.
  Collation target_type_collation;
  if (target_type_annotation_map != nullptr) {
    GOOGLESQL_ASSIGN_OR_RETURN(target_type_collation,
                     Collation::MakeCollation(*target_type_annotation_map));
  }

  GOOGLESQL_RETURN_IF_ERROR(function_resolver_->AddCastOrConvertLiteral(
      ast_location, {target_type, target_type_annotation_map},
      /*format=*/nullptr,
      /*time_zone=*/nullptr,
      TypeModifiers::MakeTypeModifiers(TypeParameters(),
                                       std::move(target_type_collation)),
      /*scan=*/nullptr,
      /*set_has_explicit_type=*/false,
      /*return_null_on_error=*/false, resolved_expr));

  return absl::OkStatus();
}

absl::Status Resolver::CoerceExprToType(
    const ASTNode* ast_location, const Type* target_type, CoercionMode mode,
    CoercionErrorMessageFunction make_error,
    std::unique_ptr<const ResolvedExpr>* resolved_expr) const {
  return CoerceExprToType(
      ast_location, AnnotatedType(target_type, /*annotation_map=*/nullptr),
      mode, make_error, resolved_expr);
}

absl::Status Resolver::CoerceExprToType(
    const ASTNode* ast_location, const Type* target_type, CoercionMode mode,
    absl::string_view error_template,
    std::unique_ptr<const ResolvedExpr>* resolved_expr) const {
  return CoerceExprToType(
      ast_location, AnnotatedType(target_type, /*annotation_map=*/nullptr),
      mode, error_template, resolved_expr);
}

absl::Status Resolver::CoerceExprToType(
    const ASTNode* ast_location, AnnotatedType annotated_target_type,
    CoercionMode mode, absl::string_view error_template,
    std::unique_ptr<const ResolvedExpr>* resolved_expr) const {
  auto make_error_msg = [error_template](absl::string_view target_type_name,
                                         absl::string_view actual_type_name) {
    return absl::Substitute(error_template, target_type_name, actual_type_name);
  };
  return CoerceExprToType(ast_location, annotated_target_type, mode,
                          make_error_msg, resolved_expr);
}

absl::Status Resolver::CoerceExprToType(
    const ASTNode* ast_location, const Type* target_type, CoercionMode mode,
    std::unique_ptr<const ResolvedExpr>* resolved_expr) const {
  return CoerceExprToType(
      ast_location, AnnotatedType(target_type, /*annotation_map=*/nullptr),
      mode, resolved_expr);
}

absl::Status Resolver::CoerceExprToType(
    const ASTNode* ast_location, AnnotatedType annotated_target_type,
    CoercionMode mode,
    std::unique_ptr<const ResolvedExpr>* resolved_expr) const {
  return CoerceExprToType(ast_location, annotated_target_type, mode,
                          "Expected type $0; found $1", resolved_expr);
}

absl::Status Resolver::ResolveExecuteImmediateArgument(
    const ASTExecuteUsingArgument* argument, ExprResolutionInfo* expr_info,
    std::unique_ptr<const ResolvedExecuteImmediateArgument>* output) {
  const std::string alias =
      argument->alias() != nullptr ? argument->alias()->GetAsString() : "";

  std::unique_ptr<const ResolvedExpr> expression;
  GOOGLESQL_RETURN_IF_ERROR(ResolveExpr(argument->expression(), expr_info, &expression));

  *output = MakeResolvedExecuteImmediateArgument(alias, std::move(expression));

  return absl::OkStatus();
}

absl::StatusOr<functions::DateTimestampPart> Resolver::ResolveDateTimestampPart(
    const ASTIdentifier* date_part_identifier) {
  std::unique_ptr<const ResolvedExpr> resolved_date_part;
  GOOGLESQL_RETURN_IF_ERROR(
      ResolveDatePartArgument(date_part_identifier, &resolved_date_part));
  GOOGLESQL_RET_CHECK(resolved_date_part->node_kind() == RESOLVED_LITERAL &&
            resolved_date_part->type()->IsEnum());
  return static_cast<functions::DateTimestampPart>(
      resolved_date_part->GetAs<ResolvedLiteral>()->value().enum_value());
}

// TODO: The noinline attribute is to prevent the stack usage
// being added to its caller "Resolver::ResolveExpr" which is a recursive
// function. Now the attribute has to be added for all callees. Hopefully
// the fix allows replacing these with one attribute on ResolveExpr.
ABSL_ATTRIBUTE_NOINLINE
absl::Status Resolver::ResolveIntervalExpr(
    const ASTIntervalExpr* interval_expr,
    ExprResolutionInfo* expr_resolution_info,
    std::unique_ptr<const ResolvedExpr>* resolved_expr_out) {
  // Resolve the INTERVAL value expression.
  const ASTExpression* interval_value_expr = interval_expr->interval_value();

  std::unique_ptr<const ResolvedExpr> resolved_interval_value;
  GOOGLESQL_RETURN_IF_ERROR(ResolveExpr(interval_value_expr, expr_resolution_info,
                              &resolved_interval_value));

  // Resolve the date part identifier and verify that it is a valid date part.
  GOOGLESQL_ASSIGN_OR_RETURN(functions::DateTimestampPart date_part,
                   ResolveDateTimestampPart(interval_expr->date_part_name()));

  // Optionally resolve the second date part identifier.
  functions::DateTimestampPart date_part_to;
  if (interval_expr->date_part_name_to() != nullptr) {
    GOOGLESQL_ASSIGN_OR_RETURN(date_part_to, ResolveDateTimestampPart(
                                       interval_expr->date_part_name_to()));
  }

  // TODO: Clarify whether integer literals (with optional sign) are
  // allowed, or only integer expressions are allowed.
  if (resolved_interval_value->type()->IsInt64()) {
    if (interval_expr->date_part_name_to() != nullptr) {
      return MakeSqlErrorAt(interval_expr->date_part_name_to())
             << "The INTERVAL keyword followed by an integer expression can "
                "only specify a single datetime field, not a field range. "
                "Consider using INTERVAL function for specifying multiple "
                "datetime fields.";
    }

    std::vector<std::unique_ptr<const ResolvedExpr>> resolved_arguments;
    std::vector<const ASTNode*> arg_locations;
    resolved_arguments.push_back(std::move(resolved_interval_value));
    arg_locations.push_back(interval_expr);

    std::unique_ptr<const ResolvedExpr> resolved_date_part;
    GOOGLESQL_RETURN_IF_ERROR(ResolveDatePartArgument(interval_expr->date_part_name(),
                                            &resolved_date_part));
    resolved_arguments.push_back(std::move(resolved_date_part));
    arg_locations.push_back(interval_expr->date_part_name());

    return ResolveFunctionCallWithResolvedArguments(
        interval_expr, arg_locations, /*match_internal_signatures=*/false,
        "$interval", std::move(resolved_arguments), /*named_arguments=*/{},
        expr_resolution_info, resolved_expr_out);
  }

  Value interval_string_value;
  bool is_string_literal =
      resolved_interval_value->node_kind() == RESOLVED_LITERAL &&
      resolved_interval_value->type()->IsString();
  bool supports_analysis_time_constant = language().LanguageFeatureEnabled(
      FEATURE_ANALYSIS_CONSTANT_INTERVAL_CONSTRUCTOR);
  // TODO: Support <sign> <string literal> as well.
  if (is_string_literal) {
    interval_string_value =
        resolved_interval_value->GetAs<ResolvedLiteral>()->value();
  } else if (!supports_analysis_time_constant) {
    return MakeSqlErrorAt(interval_value_expr)
           << "Invalid interval literal. Expected INTERVAL keyword to be "
              "followed by an INT64 expression or STRING literal";
  } else {
    if (!resolved_interval_value->type()->IsString()) {
      return MakeSqlErrorAt(interval_value_expr)
             << "Expected a STRING value knowable at compile time in INTERVAL "
                "constructor, but got an expression of type "
             << resolved_interval_value->type()->ShortTypeName(product_mode());
    }
    if (!IsAnalysisConstant(resolved_interval_value.get())) {
      return MakeSqlErrorAt(interval_value_expr)
             << "Invalid interval literal. Expected INTERVAL keyword to be "
                "followed by an INT64 expression or STRING value knowable at "
                "compile time.";
    }
    if (!resolved_interval_value->Is<ResolvedConstant>()) {
      return absl::UnimplementedError(
          "Unimplemented compile time constant type for INTERVAL constructor. "
          "Consider using a SQL constant instead");
    }
    absl::StatusOr<Value> constant_value_or_status = GetResolvedConstantValue(
        *resolved_interval_value->GetAs<ResolvedConstant>(), analyzer_options_);
    if (ShouldPropagateError(constant_value_or_status.status())) {
      return constant_value_or_status.status();
    }
    // Named constant body expression with runtime error should never be
    // lazily evaluated.
    GOOGLESQL_RET_CHECK(!absl::IsOutOfRange(constant_value_or_status.status()))
        << "Evaluation of constant expressions during semantic analysis "
        << "produced a runtime error: "
        << constant_value_or_status.status().message();
    // For all other errors, normalize the lazy evaluation error by
    // throwing SQL error with prefixed error message.
    if (!constant_value_or_status.ok()) {
      return MakeSqlErrorAt(interval_value_expr)
             << "Interval value cannot be computed because the value of "
                "constant "
             << resolved_interval_value->GetAs<ResolvedConstant>()
                    ->constant()
                    ->Name()
             << " is not available: "
             << constant_value_or_status.status().message();
    }
    interval_string_value = constant_value_or_status.value();
  }

  bool allow_nanos =
      language().LanguageFeatureEnabled(FEATURE_TIMESTAMP_NANOS) ||
      !language().LanguageFeatureEnabled(
          FEATURE_ENFORCE_MICROS_MODE_IN_INTERVAL_TYPE);

  if (interval_string_value.is_null()) {
    *resolved_expr_out =
        MakeResolvedLiteral(interval_expr, Value::NullInterval());
    return absl::OkStatus();
  }
  absl::StatusOr<IntervalValue> interval_value_or_status;
  if (interval_expr->date_part_name_to() == nullptr) {
    interval_value_or_status = IntervalValue::ParseFromString(
        interval_string_value.string_value(), date_part, allow_nanos);
  } else {
    interval_value_or_status =
        IntervalValue::ParseFromString(interval_string_value.string_value(),
                                       date_part, date_part_to, allow_nanos);
  }
  // Not using GOOGLESQL_ASSIGN_OR_RETURN, because we need to translate OutOfRange error
  // into InvalidArgument error, and add location.
  if (!interval_value_or_status.ok()) {
    return MakeSqlErrorAt(interval_expr)
           << interval_value_or_status.status().message();
  }
  Value value(Value::Interval(interval_value_or_status.value()));
  *resolved_expr_out = MakeResolvedLiteral(interval_expr, value);
  return absl::OkStatus();
}

ABSL_ATTRIBUTE_NOINLINE
absl::Status Resolver::ResolveWithExpr(
    const ASTWithExpression* with_expr,
    ExprResolutionInfo* parent_expr_resolution_info,
    std::unique_ptr<const ResolvedExpr>* resolved_expr_out,
    const Type* inferred_type) {
  RETURN_ERROR_IF_OUT_OF_STACK_SPACE();
  if (!analyzer_options_.language().LanguageFeatureEnabled(
          FEATURE_WITH_EXPRESSION)) {
    return MakeSqlErrorAt(with_expr) << "WITH expressions are not enabled.";
  }
  if (parent_expr_resolution_info->in_horizontal_aggregation) {
    return MakeSqlErrorAt(with_expr)
           << "WITH expressions are not supported in horizontal aggregation.";
  }

  auto with_expression_aliases = std::make_shared<NameList>();
  IdStringHashMapCase<NameTarget> disallowed_access_targets;
  std::unique_ptr<NameScope> scalar_scope;
  std::unique_ptr<NameScope> aggregate_scope;
  std::unique_ptr<NameScope> analytic_scope;

  bool child_has_analytic = false;
  bool child_has_aggregation = false;

  // Makes an ExprResolutionInfo based on the parent resolution info, the
  // observed WITH expression aliases.
  auto resolve_expr_with_visible_names =
      [&](const ASTExpression* ast_expr,
          std::unique_ptr<const ResolvedExpr>* resolved_expr) -> absl::Status {
    GOOGLESQL_RETURN_IF_ERROR(parent_expr_resolution_info->name_scope
                        ->CopyNameScopeWithOverridingNames(
                            with_expression_aliases, &scalar_scope));
    GOOGLESQL_RETURN_IF_ERROR(parent_expr_resolution_info->aggregate_name_scope
                        ->CopyNameScopeWithOverridingNameTargets(
                            disallowed_access_targets, &aggregate_scope));
    GOOGLESQL_RETURN_IF_ERROR(parent_expr_resolution_info->analytic_name_scope
                        ->CopyNameScopeWithOverridingNameTargets(
                            disallowed_access_targets, &analytic_scope));
    auto resolution_info = std::make_unique<ExprResolutionInfo>(
        parent_expr_resolution_info,
        ExprResolutionInfoOptions{
            .name_scope = scalar_scope.get(),
            .aggregate_name_scope = aggregate_scope.get(),
            .analytic_name_scope = analytic_scope.get(),
            .allow_new_scopes = true,
            .clause_name = "WITH expression",
            .top_level_ast_expr = with_expr,
            .column_alias = IdString(),
        });
    GOOGLESQL_RETURN_IF_ERROR(
        ResolveExpr(ast_expr, resolution_info.get(), resolved_expr));
    child_has_analytic =
        child_has_analytic || resolution_info->findings.has_analytic;
    child_has_aggregation =
        child_has_aggregation || resolution_info->findings.has_aggregation;
    parent_expr_resolution_info->horizontal_aggregation_info =
        resolution_info->horizontal_aggregation_info;
    return absl::OkStatus();
  };

  // Resolve the computed columns. Each computed column (and the output expr)
  // can reference names from previous computed columns within the WITH
  // expression. This requires copying the name scope for each computed column
  // in the WITH expr.
  const IdString with_expr_table_name = id_string_pool_->Make("$with_expr");
  std::vector<std::unique_ptr<ResolvedComputedColumn>> computed_columns;
  for (int i = 0; i < with_expr->variables()->columns().size(); ++i) {
    const ASTSelectColumn& ast_column = *with_expr->variables()->columns(i);
    std::unique_ptr<const ResolvedExpr> expr;
    GOOGLESQL_RETURN_IF_ERROR(
        resolve_expr_with_visible_names(ast_column.expression(), &expr));
    ResolvedColumn column(AllocateColumnId(), with_expr_table_name,
                          ast_column.alias()->GetAsIdString(), expr->type());
    computed_columns.push_back(
        MakeResolvedComputedColumn(column, std::move(expr)));

    // Update the name scopes with the new name for this expr.
    GOOGLESQL_RETURN_IF_ERROR(with_expression_aliases->AddColumn(column.name_id(), column,
                                                       /*is_explicit=*/true));

    // The aggregate scope needs to be updated with the new name, otherwise we
    // could get really confusing errors. However, the names introduced by WITH
    // cannot be used in aggregations. In the general case, we just provide
    // an error NameTarget for this name. However, if the expression a path
    // expression, we can additionally provide the valid name for the given
    // alias.
    NameTarget prevent_access_target(column, /*is_explicit=*/true);
    prevent_access_target.SetAccessError(
        prevent_access_target.kind(),
        absl::StrCat(
            "Column '", column.name(),
            "' is introduced by WITH. Columns introduced by WITH cannot be "
            "used in the arguments to aggregate or analytic functions."));
    if (ast_column.expression()->node_kind() == AST_PATH_EXPRESSION) {
      const auto* path_expr =
          ast_column.expression()->GetAsOrDie<ASTPathExpression>();
      prevent_access_target.set_valid_name_path_list(
          {ValidNamePath{path_expr->ToIdStringVector(), column}});
    }
    disallowed_access_targets[column.name_id()] =
        std::move(prevent_access_target);
  }

  std::unique_ptr<const ResolvedExpr> output_expr;
  {
    // Resolve the output_expr.
    GOOGLESQL_RETURN_IF_ERROR(
        resolve_expr_with_visible_names(with_expr->expression(), &output_expr));
  }

  const Type* const output_expr_type = output_expr->type();
  *resolved_expr_out = MakeResolvedWithExpr(
      output_expr_type, std::move(computed_columns), std::move(output_expr));
  parent_expr_resolution_info->findings.has_analytic =
      parent_expr_resolution_info->findings.has_analytic || child_has_analytic;
  parent_expr_resolution_info->findings.has_aggregation =
      parent_expr_resolution_info->findings.has_aggregation ||
      child_has_aggregation;
  return absl::OkStatus();
}

}  // namespace googlesql
