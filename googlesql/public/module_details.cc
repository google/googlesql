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

#include "googlesql/public/module_details.h"

#include <memory>
#include <optional>
#include <string>
#include <utility>

#include "googlesql/common/errors.h"
#include "googlesql/common/resolution_scope.h"
#include "googlesql/proto/module_options.pb.h"
#include "googlesql/public/constant_evaluator.h"
#include "googlesql/public/options.pb.h"
#include "googlesql/public/types/type.h"
#include "googlesql/public/value.h"
#include "googlesql/resolved_ast/resolved_ast.h"
#include "googlesql/resolved_ast/resolved_node_kind.pb.h"
#include "googlesql/base/case.h"
#include "absl/base/no_destructor.h"
#include "absl/container/btree_set.h"
#include "absl/container/flat_hash_map.h"
#include "absl/log/log.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/ascii.h"
#include "absl/strings/str_join.h"
#include "absl/strings/string_view.h"
#include "absl/strings/substitute.h"
#include "absl/types/span.h"
#include "googlesql/base/map_util.h"
#include "googlesql/base/status_macros.h"

namespace googlesql {
namespace {

// Option names.
constexpr absl::string_view kStubModuleType = "stub_module_type";
constexpr absl::string_view kUdfNamespace = "udf_namespace";
constexpr absl::string_view kUdfScalingFactor = "udf_scaling_factor";
constexpr absl::string_view kUdfServerAddress = "udf_server_address";
constexpr absl::string_view kUdfServerCatalog = "udf_server_catalog";
constexpr absl::string_view kUdfServerImportMode = "udf_server_import_mode";
constexpr absl::string_view kAllowedReferences = "allowed_references";

// Option values.
constexpr absl::string_view kUdfServerModeManual = "MANUAL";
constexpr absl::string_view kUdfServerModeAlwaysRunning =
    "SERVER_ADDRESS_FROM_MODULE";
constexpr absl::string_view kUdfServerModeOnDemand = "CALLER_PROVIDED";

std::optional<std::string> FindStringOption(
    absl::flat_hash_map<std::string, Value>& module_options,
    absl::string_view key) {
  const googlesql::Value* value =
      googlesql_base::FindOrNull(module_options, absl::AsciiStrToLower(key));
  if (value == nullptr) {
    return std::nullopt;
  }

  if (!value->type()->IsString()) {
    ABSL_LOG(WARNING) << absl::Substitute(
        "The option $0 is not a string, got type: $1", key,
        value->DebugString());
    return std::nullopt;
  }
  return value->string_value();
}

// TODO: allow type coercion from other numeric type to double.
std::optional<double> FindDoubleOption(
    absl::flat_hash_map<std::string, Value>& module_options,
    absl::string_view key) {
  const googlesql::Value* value =
      googlesql_base::FindOrNull(module_options, absl::AsciiStrToLower(key));
  if (value == nullptr) {
    ABSL_LOG(WARNING) << absl::Substitute("The option $0 is not found", key);
    return std::nullopt;
  }

  if (!value->type()->IsDouble()) {
    ABSL_LOG(WARNING) << absl::Substitute(
        "The option $0 is not a double, got type: $1", key,
        value->DebugString());
    return std::nullopt;
  }
  return value->double_value();
}

absl::StatusOr<PerModuleOptions::UdfServerImportMode>
GetUdfServerImportModeByName(absl::string_view udf_server_import_mode_string) {
  // TODO: Use proto enum names instead of re-defining allowed values.
  if (googlesql_base::CaseCompare(udf_server_import_mode_string,
                                           kUdfServerModeAlwaysRunning) == 0) {
    return PerModuleOptions::SERVER_ADDRESS_FROM_MODULE;
  } else if (googlesql_base::CaseCompare(
                 udf_server_import_mode_string, kUdfServerModeOnDemand) == 0) {
    return PerModuleOptions::CALLER_PROVIDED;
  } else if (googlesql_base::CaseCompare(udf_server_import_mode_string,
                                                  kUdfServerModeManual) == 0) {
    return PerModuleOptions::MANUAL;
  } else {
    return absl::InvalidArgumentError(absl::Substitute(
        "Unrecognized mode: $0, allowed modes are [$1]",
        udf_server_import_mode_string,
        absl::StrJoin({kUdfServerModeAlwaysRunning, kUdfServerModeOnDemand,
                       kUdfServerModeManual},
                      ",")));
  }
}

static absl::Status CheckModuleOptionType(const absl::string_view option_name,
                                          TypeKind expected_type_kind,
                                          const Type* actual_type) {
  if (actual_type->kind() == expected_type_kind) {
    return absl::OkStatus();
  }
  return MakeSqlError() << absl::Substitute(
             "Module option $0 is expected to have type $1, but got: $2",
             option_name,
             Type::TypeKindToString(expected_type_kind,
                                    ProductMode::PRODUCT_INTERNAL),
             actual_type->ShortTypeName(ProductMode::PRODUCT_INTERNAL));
}

absl::StatusOr<PerModuleOptions> GetUdfModuleOptions(
    absl::flat_hash_map<std::string, Value>& module_options) {
  // If a module has any UDF stub options, then module type is required.
  bool has_udf_stub_obtions = false;
  bool has_stub_module_type = false;
  PerModuleOptions options;
  for (const auto& [name, value] : module_options) {
    if (googlesql_base::CaseCompare(name, kStubModuleType) == 0) {
      GOOGLESQL_RETURN_IF_ERROR(CheckModuleOptionType(
          kStubModuleType, TypeKind::TYPE_STRING, value.type()));
      if (value.string_value() != kUdfServerCatalog) {
        return MakeSqlError() << absl::Substitute(
                   "Allowed stub module types are [$0], but got: $1",
                   kUdfServerCatalog, value.string_value());
      }
      has_stub_module_type = true;
    } else if (googlesql_base::CaseCompare(name, kUdfServerAddress) ==
               0) {
      GOOGLESQL_RETURN_IF_ERROR(CheckModuleOptionType(
          kUdfServerAddress, TypeKind::TYPE_STRING, value.type()));
      options.set_udf_server_address(value.string_value());
    } else if (googlesql_base::CaseCompare(name, kUdfNamespace) == 0) {
      GOOGLESQL_RETURN_IF_ERROR(CheckModuleOptionType(
          kUdfNamespace, TypeKind::TYPE_STRING, value.type()));
      options.set_udf_namespace(value.string_value());
    } else if (googlesql_base::CaseCompare(name, kUdfScalingFactor) ==
               0) {
      GOOGLESQL_RETURN_IF_ERROR(CheckModuleOptionType(
          kUdfScalingFactor, TypeKind::TYPE_DOUBLE, value.type()));
      options.set_udf_scaling_factor(value.double_value());
    } else if (googlesql_base::CaseCompare(
                   name, kUdfServerImportMode) == 0) {
      GOOGLESQL_RETURN_IF_ERROR(CheckModuleOptionType(
          kUdfServerImportMode, TypeKind::TYPE_STRING, value.type()));
      GOOGLESQL_ASSIGN_OR_RETURN(auto udf_server_import_mode,
                       GetUdfServerImportModeByName(value.string_value()));
      options.set_udf_server_import_mode(udf_server_import_mode);
    } else {
      continue;
    }
    has_udf_stub_obtions = true;
  }
  if (has_udf_stub_obtions && !has_stub_module_type) {
    return absl::InvalidArgumentError(
        absl::Substitute("The option list for a UDF server stub module should "
                         "contain an option with name: $0",
                         kStubModuleType));
  }
  return options;
}

void OverridesModuleOptions(const PerModuleOptions& overrides,
                            PerModuleOptions& options) {
  if (overrides.has_udf_server_address()) {
    options.set_udf_server_address(overrides.udf_server_address());
  }
  if (overrides.has_udf_namespace()) {
    options.set_udf_namespace(overrides.udf_namespace());
  }
  if (overrides.has_udf_scaling_factor()) {
    options.set_udf_scaling_factor(overrides.udf_scaling_factor());
  }
  if (overrides.has_udf_server_import_mode()) {
    options.set_udf_server_import_mode(overrides.udf_server_import_mode());
  }
}

}  // namespace

// static
ModuleDetails ModuleDetails::CreateEmpty() {
  return ModuleDetails("", std::nullopt, ResolutionScope::kBuiltin);
}

// static
absl::StatusOr<ModuleDetails> ModuleDetails::Create(
    absl::string_view module_fullname,
    absl::Span<const std::unique_ptr<const ResolvedOption>> resolved_options,
    ConstantEvaluator* constant_evaluator, ModuleOptions option_overrides,
    absl::Span<const std::string> module_name_from_import) {
  static const auto allowed_option_names =
      absl::NoDestructor<absl::btree_set<absl::string_view>>(
          {kAllowedReferences, kStubModuleType, kUdfNamespace,
           kUdfScalingFactor, kUdfServerAddress, kUdfServerCatalog,
           kUdfServerImportMode});
  absl::flat_hash_map<std::string, Value> literal_options;
  for (const auto& option : resolved_options) {
    const std::string lower_case_key = absl::AsciiStrToLower(option->name());
    if (!allowed_option_names->contains(lower_case_key)) {
      return absl::InvalidArgumentError(absl::Substitute(
          "Unexpected option name: $0, allowed option names are [$1]",
          lower_case_key, absl::StrJoin(*allowed_option_names, ", ")));
    }
    const ResolvedExpr* resolved_expr = option->value();
    Value value;
    if (resolved_expr->node_kind() == RESOLVED_LITERAL) {
      const auto* literal = resolved_expr->GetAs<ResolvedLiteral>();
      value = literal->value();
    } else if (constant_evaluator != nullptr) {
      GOOGLESQL_ASSIGN_OR_RETURN(
          value, constant_evaluator->Evaluate(*resolved_expr),
          _.SetCode(absl::StatusCode::kInvalidArgument).SetPrepend()
              << absl::Substitute("Error in module $0: ", module_fullname));
    } else {
      return absl::InvalidArgumentError(absl::Substitute(
          "ConstantEvaluator not specified for the expression of "
          "option $0 in module $1",
          option->name(), module_fullname));
    }
    if (!googlesql_base::InsertIfNotPresent(&literal_options, lower_case_key, value)) {
      return absl::InvalidArgumentError(absl::Substitute(
          "Duplicate option $0 in module $1", option->name(), module_fullname));
    }
  }

  if (resolved_options.empty()) {
    return ModuleDetails(module_fullname, std::nullopt,
                         ResolutionScope::kBuiltin, module_name_from_import);
  }

  std::optional<PerModuleOptions> udf_server_options;
  GOOGLESQL_ASSIGN_OR_RETURN(udf_server_options, GetUdfModuleOptions(literal_options));
  PerModuleOptions global_defaults =
      std::move(option_overrides.global_options());
  if (const PerModuleOptions* per_module_options = googlesql_base::FindOrNull(
          option_overrides.per_module_options(), module_fullname);
      per_module_options != nullptr) {
    OverridesModuleOptions(*per_module_options, global_defaults);
  }
  OverridesModuleOptions(global_defaults, *udf_server_options);

  ResolutionScope default_resolution_scope = ResolutionScope::kBuiltin;
  auto it = literal_options.find(kAllowedReferences);
  if (it != literal_options.end()) {
    Value value = it->second;
    GOOGLESQL_RETURN_IF_ERROR(CheckModuleOptionType(kAllowedReferences,
                                          TypeKind::TYPE_STRING, value.type()));
    const std::string lower_case_value =
        absl::AsciiStrToLower(value.string_value());
    if (lower_case_value == "global") {
      default_resolution_scope = ResolutionScope::kGlobal;
    } else if (lower_case_value != "builtin") {
      return MakeSqlError() << absl::Substitute(
                 "Allowed values for the $0 module option are [BUILTIN, "
                 "GLOBAL], but got: $1",
                 kAllowedReferences, value.string_value());
    }
  }

  return ModuleDetails(module_fullname, std::move(udf_server_options),
                       default_resolution_scope, module_name_from_import);
}

}  // namespace googlesql
