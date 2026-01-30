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

#include <string>
#include <vector>

#include "googlesql/common/builtin_function_internal.h"
#include "googlesql/common/errors.h"
#include "googlesql/public/analyzer_options.h"
#include "googlesql/public/builtin_function.pb.h"
#include "googlesql/public/builtin_function_options.h"
#include "googlesql/public/coercer.h"
#include "googlesql/public/function.h"
#include "googlesql/public/function.pb.h"
#include "googlesql/public/function_signature.h"
#include "googlesql/public/input_argument_type.h"
#include "googlesql/public/language_options.h"
#include "googlesql/public/options.pb.h"
#include "googlesql/public/strings.h"
#include "googlesql/public/type.pb.h"
#include "googlesql/public/types/array_type.h"
#include "googlesql/public/types/type.h"
#include "googlesql/public/types/type_factory.h"
#include "absl/algorithm/container.h"
#include "absl/base/no_destructor.h"
#include "absl/container/flat_hash_set.h"
#include "absl/functional/bind_front.h"
#include "googlesql/base/check.h"
#include "absl/status/status.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/str_format.h"
#include "absl/strings/string_view.h"
#include "absl/types/span.h"
#include "googlesql/base/ret_check.h"
#include "googlesql/base/status_macros.h"

namespace googlesql {

namespace {

absl::Status CheckMinArgs(absl::string_view function_name, int min_args,
                          absl::Span<const InputArgumentType> args,
                          const LanguageOptions& options) {
  if (args.size() < min_args) {
    return MakeSqlError() << absl::StrFormat(
               "Function %s() requires at least %d arguments", function_name,
               min_args);
  }
  return absl::OkStatus();
}

static absl::StatusOr<const Type*> ComputePathNodeType(
    Catalog* catalog, TypeFactory* type_factory, CycleDetector* cycle_detector,
    const FunctionSignature& signature,
    absl::Span<const InputArgumentType> arguments,
    const AnalyzerOptions& analyzer_options) {
  GOOGLESQL_RET_CHECK(arguments.size() == 1);
  GOOGLESQL_RET_CHECK(arguments[0].type()->IsGraphPath());
  return arguments[0].type()->AsGraphPath()->node_type();
}

static absl::StatusOr<const Type*> ComputePathNodesType(
    Catalog* catalog, TypeFactory* type_factory, CycleDetector* cycle_detector,
    const FunctionSignature& signature,
    absl::Span<const InputArgumentType> arguments,
    const AnalyzerOptions& analyzer_options) {
  GOOGLESQL_RET_CHECK(arguments.size() == 1);
  GOOGLESQL_RET_CHECK(arguments[0].type()->IsGraphPath());
  const ArrayType* array_type;
  GOOGLESQL_RETURN_IF_ERROR(type_factory->MakeArrayType(
      arguments[0].type()->AsGraphPath()->node_type(), &array_type));
  return array_type;
}

static absl::StatusOr<const Type*> ComputePathEdgesType(
    Catalog* catalog, TypeFactory* type_factory, CycleDetector* cycle_detector,
    const FunctionSignature& signature,
    absl::Span<const InputArgumentType> arguments,
    const AnalyzerOptions& analyzer_options) {
  GOOGLESQL_RET_CHECK(arguments.size() == 1);
  GOOGLESQL_RET_CHECK(arguments[0].type()->IsGraphPath());
  const ArrayType* array_type;
  GOOGLESQL_RETURN_IF_ERROR(type_factory->MakeArrayType(
      arguments[0].type()->AsGraphPath()->edge_type(), &array_type));
  return array_type;
}

static absl::StatusOr<const Type*> ComputePathCreateType(
    Catalog* catalog, TypeFactory* type_factory, CycleDetector* cycle_detector,
    const FunctionSignature& signature,
    absl::Span<const InputArgumentType> arguments,
    const AnalyzerOptions& analyzer_options) {
  absl::Span<const InputArgumentType> graph_element_arguments = arguments;
  while (!graph_element_arguments.empty() &&
         !graph_element_arguments.back().type()->IsGraphElement()) {
    graph_element_arguments =
        graph_element_arguments.subspan(0, graph_element_arguments.size() - 1);
  }
  GOOGLESQL_RET_CHECK(!graph_element_arguments.empty());
  GOOGLESQL_RET_CHECK(
      absl::c_all_of(graph_element_arguments, [](const InputArgumentType& arg) {
        return arg.type()->IsGraphElement();
      }));
  InputArgumentTypeSet node_types;
  InputArgumentTypeSet edge_types;
  for (int i = 0; i < graph_element_arguments.size(); ++i) {
    if (i % 2 == 0) {
      GOOGLESQL_RET_CHECK(graph_element_arguments[i].type()->AsGraphElement()->IsNode());
      node_types.Insert(graph_element_arguments[i]);
    } else {
      GOOGLESQL_RET_CHECK(graph_element_arguments[i].type()->AsGraphElement()->IsEdge());
      edge_types.Insert(graph_element_arguments[i]);
    }
  }
  Coercer coercer(type_factory, &analyzer_options.language(), catalog);
  const Type* node_type = nullptr;
  GOOGLESQL_RETURN_IF_ERROR(coercer.GetCommonSuperType(node_types, &node_type));
  GOOGLESQL_RET_CHECK(node_type != nullptr);
  GOOGLESQL_RET_CHECK(node_type->IsGraphElement());
  const GraphElementType* edge_type = nullptr;
  if (edge_types.empty()) {
    GOOGLESQL_RETURN_IF_ERROR(type_factory->MakeGraphElementType(
        node_type->AsGraphElement()->graph_reference(), GraphElementType::kEdge,
        {}, &edge_type));
  } else {
    const Type* supertype = nullptr;
    GOOGLESQL_RETURN_IF_ERROR(coercer.GetCommonSuperType(edge_types, &supertype));
    GOOGLESQL_RET_CHECK(supertype != nullptr);
    GOOGLESQL_RET_CHECK(supertype->IsGraphElement());
    edge_type = supertype->AsGraphElement();
  }
  const GraphPathType* path_type = nullptr;
  GOOGLESQL_RETURN_IF_ERROR(type_factory->MakeGraphPathType(node_type->AsGraphElement(),
                                                  edge_type, &path_type));
  return path_type;
}

absl::Status CheckDynamicPropertyEqualsArgs(
    const FunctionSignature& signature,
    absl::Span<const InputArgumentType> args,
    const LanguageOptions& language_options) {
  GOOGLESQL_RET_CHECK_EQ(args.size(), 3);
  GOOGLESQL_RET_CHECK(args[0].type()->IsGraphElement() &&
            args[0].type()->AsGraphElement()->is_dynamic());
  const Type* target_value_type = args[2].type();
  static const auto kSupportedTypes =
      absl::NoDestructor(absl::flat_hash_set<TypeKind>({
          TYPE_BOOL,
          TYPE_INT32,
          TYPE_UINT32,
          TYPE_INT64,
          TYPE_UINT64,
          TYPE_FLOAT,
          TYPE_DOUBLE,
          TYPE_STRING,
      }));
  if (!kSupportedTypes->contains(target_value_type->kind()) &&
      (!target_value_type->IsArray() ||
       !kSupportedTypes->contains(
           target_value_type->AsArray()->element_type()->kind()))) {
    return MakeSqlError()
           << "Unsupported equality comparison between dynamic property "
              "and value of type "
           << target_value_type->ShortTypeName(language_options.product_mode());
  }
  return absl::OkStatus();
}

}  // namespace

void GetGraphFunctions(TypeFactory* type_factory,
                       const GoogleSQLBuiltinFunctionOptions& options,
                       NameToFunctionMap* functions) {
  const Type* bool_type = type_factory->get_bool();
  const Type* string_type = type_factory->get_string();
  const Type* int64_type = type_factory->get_int64();
  const Type* json_type = type_factory->get_json();
  const ArrayType* string_array_type = types::StringArrayType();

  InsertFunction(
      functions, options, "$is_source_node", Function::SCALAR,
      {{bool_type,
        {ARG_TYPE_GRAPH_NODE, ARG_TYPE_GRAPH_EDGE},
        FN_IS_SOURCE_NODE}},
      FunctionOptions()
          .AddRequiredLanguageFeature(LanguageFeature::FEATURE_SQL_GRAPH)
          .set_supports_safe_error_mode(false)
          .set_sql_name("IS SOURCE OF")
          .set_get_sql_callback(
              absl::bind_front(&InfixFunctionSQL, "IS SOURCE OF")));

  InsertFunction(
      functions, options, "$is_dest_node", Function::SCALAR,
      {{bool_type,
        {ARG_TYPE_GRAPH_NODE, ARG_TYPE_GRAPH_EDGE},
        FN_IS_DEST_NODE}},
      FunctionOptions()
          .AddRequiredLanguageFeature(LanguageFeature::FEATURE_SQL_GRAPH)
          .set_supports_safe_error_mode(false)
          .set_sql_name("IS DESTINATION OF")
          .set_get_sql_callback(
              absl::bind_front(&InfixFunctionSQL, "IS DESTINATION OF")));

  // TODO: ALL_DIFFERENT(node, edge) does not work for now because
  // we don't support supertype between node and edge, though the standard
  // doesn't explicitly disable that.
  InsertFunction(
      functions, options, "all_different", Function::SCALAR,
      {{bool_type,
        {{ARG_TYPE_GRAPH_ELEMENT, FunctionArgumentType::REPEATED}},
        FN_ALL_DIFFERENT_GRAPH_ELEMENT}},
      FunctionOptions()
          .AddRequiredLanguageFeature(LanguageFeature::FEATURE_SQL_GRAPH)
          .set_supports_safe_error_mode(false)
          .set_sql_name("ALL_DIFFERENT")
          .set_pre_resolution_argument_constraint(
              absl::bind_front(&CheckMinArgs, "ALL_DIFFERENT", 2)));

  // TODO: SAME(node, edge) does not work for now because we don't
  // support supertype between node and edge, though the standard doesn't
  // explicitly disable that.
  InsertFunction(
      functions, options, "same", Function::SCALAR,
      {{bool_type,
        {{ARG_TYPE_GRAPH_ELEMENT, FunctionArgumentType::REPEATED}},
        FN_SAME_GRAPH_ELEMENT}},
      FunctionOptions()
          .AddRequiredLanguageFeature(LanguageFeature::FEATURE_SQL_GRAPH)
          .set_supports_safe_error_mode(false)
          .set_sql_name("SAME")
          .set_pre_resolution_argument_constraint(
              absl::bind_front(&CheckMinArgs, "SAME", 2)));

  InsertFunction(
      functions, options, "property_exists", Function::SCALAR,
      {{bool_type,
        {{ARG_TYPE_GRAPH_ELEMENT, string_type}},
        FN_PROPERTY_EXISTS}},
      FunctionOptions()
          .AddRequiredLanguageFeature(LanguageFeature::FEATURE_SQL_GRAPH)
          .set_supports_safe_error_mode(false)
          .set_sql_name("PROPERTY_EXISTS")
          .set_get_sql_callback(
              [](std::vector<std::string> inputs) -> std::string {
                ABSL_DCHECK_EQ(inputs.size(), 2);
                std::string property_name = inputs[1];
                ParseStringLiteral(inputs[1], &property_name).IgnoreError();
                return absl::StrCat("PROPERTY_EXISTS(", inputs[0], ",",
                                    property_name, ")");
              }));

  InsertFunction(
      functions, options, "labels", Function::SCALAR,
      {{string_array_type,
        {{ARG_TYPE_GRAPH_ELEMENT}},
        FN_LABELS_GRAPH_ELEMENT}},
      FunctionOptions()
          .AddRequiredLanguageFeature(LanguageFeature::FEATURE_SQL_GRAPH)
          .set_supports_safe_error_mode(false));

  InsertFunction(
      functions, options, "property_names", Function::SCALAR,
      {{string_array_type,
        {{ARG_TYPE_GRAPH_ELEMENT}},
        FN_PROPERTY_NAMES_GRAPH_ELEMENT}},
      FunctionOptions()
          .AddRequiredLanguageFeature(LanguageFeature::FEATURE_SQL_GRAPH)
          .set_supports_safe_error_mode(false));

  InsertFunction(
      functions, options, "element_id", Function::SCALAR,
      {{string_type, {{ARG_TYPE_GRAPH_ELEMENT}}, FN_ELEMENT_ID_GRAPH_ELEMENT}},
      FunctionOptions()
          .AddRequiredLanguageFeature(LanguageFeature::FEATURE_SQL_GRAPH)
          .set_supports_safe_error_mode(false)
          .set_volatility(FunctionEnums::STABLE));

  InsertFunction(
      functions, options, "source_node_id", Function::SCALAR,
      {{string_type, {{ARG_TYPE_GRAPH_EDGE}}, FN_SOURCE_NODE_ID}},
      FunctionOptions()
          .AddRequiredLanguageFeature(LanguageFeature::FEATURE_SQL_GRAPH)
          .set_supports_safe_error_mode(false)
          .set_volatility(FunctionEnums::STABLE));

  InsertFunction(
      functions, options, "destination_node_id", Function::SCALAR,
      {{string_type, {{ARG_TYPE_GRAPH_EDGE}}, FN_DESTINATION_NODE_ID}},
      FunctionOptions()
          .AddRequiredLanguageFeature(LanguageFeature::FEATURE_SQL_GRAPH)
          .set_supports_safe_error_mode(false)
          .set_volatility(FunctionEnums::STABLE));

  InsertFunction(
      functions, options, "element_definition_name", Function::SCALAR,
      {{string_type, {{ARG_TYPE_GRAPH_ELEMENT}}, FN_ELEMENT_DEFINITION_NAME}},
      FunctionOptions()
          .AddRequiredLanguageFeature(LanguageFeature::FEATURE_SQL_GRAPH)
          .set_supports_safe_error_mode(false));

  InsertFunction(functions, options, "path_length", Function::SCALAR,
                 {{int64_type, {{ARG_TYPE_GRAPH_PATH}}, FN_PATH_LENGTH}},
                 FunctionOptions()
                     .AddRequiredLanguageFeature(FEATURE_SQL_GRAPH)
                     .AddRequiredLanguageFeature(FEATURE_SQL_GRAPH_PATH_TYPE)
                     .set_supports_safe_error_mode(false)
                     .set_sql_name("PATH_LENGTH"));

  InsertFunction(functions, options, "nodes", Function::SCALAR,
                 {{ARG_TYPE_ARBITRARY, {{ARG_TYPE_GRAPH_PATH}}, FN_PATH_NODES}},
                 FunctionOptions()
                     .AddRequiredLanguageFeature(FEATURE_SQL_GRAPH)
                     .AddRequiredLanguageFeature(FEATURE_SQL_GRAPH_PATH_TYPE)
                     .set_supports_safe_error_mode(false)
                     .set_sql_name("NODES")
                     .set_compute_result_type_callback(ComputePathNodesType));

  InsertFunction(functions, options, "edges", Function::SCALAR,
                 {{ARG_TYPE_ARBITRARY, {{ARG_TYPE_GRAPH_PATH}}, FN_PATH_EDGES}},
                 FunctionOptions()
                     .AddRequiredLanguageFeature(FEATURE_SQL_GRAPH)
                     .AddRequiredLanguageFeature(FEATURE_SQL_GRAPH_PATH_TYPE)
                     .set_supports_safe_error_mode(false)
                     .set_sql_name("EDGES")
                     .set_compute_result_type_callback(ComputePathEdgesType));

  InsertFunction(functions, options, "path_first", Function::SCALAR,
                 {{ARG_TYPE_ARBITRARY, {{ARG_TYPE_GRAPH_PATH}}, FN_PATH_FIRST}},
                 FunctionOptions()
                     .AddRequiredLanguageFeature(FEATURE_SQL_GRAPH)
                     .AddRequiredLanguageFeature(FEATURE_SQL_GRAPH_PATH_TYPE)
                     .set_supports_safe_error_mode(false)
                     .set_sql_name("PATH_FIRST")
                     .set_compute_result_type_callback(ComputePathNodeType));

  InsertFunction(functions, options, "path_last", Function::SCALAR,
                 {{ARG_TYPE_ARBITRARY, {{ARG_TYPE_GRAPH_PATH}}, FN_PATH_LAST}},
                 FunctionOptions()
                     .AddRequiredLanguageFeature(FEATURE_SQL_GRAPH)
                     .AddRequiredLanguageFeature(FEATURE_SQL_GRAPH_PATH_TYPE)
                     .set_supports_safe_error_mode(false)
                     .set_sql_name("PATH_LAST")
                     .set_compute_result_type_callback(ComputePathNodeType));

  InsertFunction(functions, options, "path", Function::SCALAR,
                 {
                     {ARG_TYPE_ARBITRARY,
                      {ARG_TYPE_GRAPH_NODE,
                       {ARG_TYPE_GRAPH_EDGE, FunctionArgumentType::REPEATED},
                       {ARG_TYPE_GRAPH_NODE, FunctionArgumentType::REPEATED}},
                      FN_PATH_CREATE},
                 },
                 FunctionOptions()
                     .AddRequiredLanguageFeature(FEATURE_SQL_GRAPH)
                     .AddRequiredLanguageFeature(FEATURE_SQL_GRAPH_PATH_TYPE)
                     .set_supports_safe_error_mode(false)
                     .set_sql_name("PATH")
                     .set_compute_result_type_callback(ComputePathCreateType));

  InsertFunction(functions, options, "is_acyclic", Function::SCALAR,
                 {{bool_type, {{ARG_TYPE_GRAPH_PATH}}, FN_IS_ACYCLIC}},
                 FunctionOptions()
                     .AddRequiredLanguageFeature(FEATURE_SQL_GRAPH)
                     .AddRequiredLanguageFeature(FEATURE_SQL_GRAPH_PATH_TYPE)
                     .set_supports_safe_error_mode(false)
                     .set_sql_name("IS_ACYCLIC"));

  InsertFunction(functions, options, "is_trail", Function::SCALAR,
                 {{bool_type, {{ARG_TYPE_GRAPH_PATH}}, FN_IS_TRAIL}},
                 FunctionOptions()
                     .AddRequiredLanguageFeature(FEATURE_SQL_GRAPH)
                     .AddRequiredLanguageFeature(FEATURE_SQL_GRAPH_PATH_TYPE)
                     .set_supports_safe_error_mode(false)
                     .set_sql_name("IS_TRAIL"));

  InsertFunction(functions, options, "is_simple", Function::SCALAR,
                 {{bool_type, {{ARG_TYPE_GRAPH_PATH}}, FN_IS_SIMPLE}},
                 FunctionOptions()
                     .AddRequiredLanguageFeature(FEATURE_SQL_GRAPH)
                     .AddRequiredLanguageFeature(FEATURE_SQL_GRAPH_PATH_TYPE)
                     .set_supports_safe_error_mode(false)
                     .set_sql_name("IS_SIMPLE"));

  InsertFunction(
      functions, options, "$unchecked_path", Function::SCALAR,
      {
          {ARG_TYPE_ARBITRARY,
           {ARG_TYPE_GRAPH_NODE,
            {ARG_TYPE_GRAPH_EDGE, FunctionArgumentType::REPEATED},
            {ARG_TYPE_GRAPH_NODE, FunctionArgumentType::REPEATED}},
           FN_UNCHECKED_PATH_CREATE,
           FunctionSignatureOptions().set_is_internal(true)},
      },
      FunctionOptions()
          .AddRequiredLanguageFeature(FEATURE_SQL_GRAPH)
          .AddRequiredLanguageFeature(FEATURE_SQL_GRAPH_PATH_TYPE)
          .set_supports_safe_error_mode(false)
          .set_compute_result_type_callback(ComputePathCreateType));

  InsertFunction(
      functions, options, "$path_concat", Function::SCALAR,
      {{ARG_TYPE_GRAPH_PATH,
        {ARG_TYPE_GRAPH_PATH,
         {ARG_TYPE_GRAPH_PATH, FunctionArgumentType::REPEATED}},
        FN_CONCAT_PATH}},
      FunctionOptions()
          .set_get_sql_callback(absl::bind_front(&InfixFunctionSQL, "||"))
          .AddRequiredLanguageFeature(FEATURE_SQL_GRAPH)
          .AddRequiredLanguageFeature(FEATURE_SQL_GRAPH_PATH_TYPE)
          .set_supports_safe_error_mode(false));

  InsertFunction(functions, options, "$unchecked_path_concat", Function::SCALAR,
                 {{ARG_TYPE_GRAPH_PATH,
                   {ARG_TYPE_GRAPH_PATH,
                    {ARG_TYPE_GRAPH_PATH, FunctionArgumentType::REPEATED}},
                   FN_UNCHECKED_CONCAT_PATH,
                   FunctionSignatureOptions().set_is_internal(true)}},
                 FunctionOptions()
                     .AddRequiredLanguageFeature(FEATURE_SQL_GRAPH)
                     .AddRequiredLanguageFeature(FEATURE_SQL_GRAPH_PATH_TYPE)
                     .set_supports_safe_error_mode(false));

  InsertFunction(
      functions, options, "$dynamic_property_equals", Function::SCALAR,
      {{bool_type,
        {ARG_TYPE_GRAPH_ELEMENT, string_type, ARG_TYPE_ANY_1},
        FN_DYNAMIC_PROPERTY_EQUALS,
        FunctionSignatureOptions().set_is_internal(true)}},
      FunctionOptions()
          .AddRequiredLanguageFeature(FEATURE_SQL_GRAPH)
          .AddRequiredLanguageFeature(FEATURE_SQL_GRAPH_DYNAMIC_ELEMENT_TYPE)
          .set_supports_safe_error_mode(false)
          .set_post_resolution_argument_constraint(
              CheckDynamicPropertyEqualsArgs));
}

}  // namespace googlesql
