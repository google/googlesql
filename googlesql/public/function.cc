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

#include "googlesql/public/function.h"

#include <ctype.h>

#include <algorithm>
#include <map>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "googlesql/base/logging.h"
#include "googlesql/common/errors.h"
#include "googlesql/common/function_utils.h"
#include "googlesql/proto/function.pb.h"
#include "googlesql/public/function_signature.h"
#include "googlesql/public/language_options.h"
#include "googlesql/public/options.pb.h"
#include "googlesql/public/types/type_deserializer.h"
#include "googlesql/resolved_ast/resolved_ast_enums.pb.h"
#include "absl/status/status.h"
#include "absl/strings/match.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/str_join.h"
#include "absl/strings/str_replace.h"
#include "absl/strings/string_view.h"
#include "absl/types/span.h"
#include "re2/re2.h"
#include "googlesql/base/ret_check.h"
#include "googlesql/base/status_macros.h"

namespace googlesql {

const FunctionEnums::WindowOrderSupport FunctionOptions::ORDER_UNSUPPORTED;
const FunctionEnums::WindowOrderSupport FunctionOptions::ORDER_OPTIONAL;
const FunctionEnums::WindowOrderSupport FunctionOptions::ORDER_REQUIRED;

absl::Status FunctionOptions::Deserialize(
    const FunctionOptionsProto& proto,
    std::unique_ptr<FunctionOptions>* result) {
  std::unique_ptr<FunctionOptions> options;
  if (proto.supports_over_clause()) {
    options = std::make_unique<FunctionOptions>(
        proto.window_ordering_support(), proto.supports_window_framing());
  } else {
    GOOGLESQL_RET_CHECK(!proto.supports_window_framing());
    GOOGLESQL_RET_CHECK_EQ(proto.window_ordering_support(), ORDER_UNSUPPORTED);
    options = std::make_unique<FunctionOptions>();
  }

  options->set_arguments_are_coercible(proto.arguments_are_coercible());
  options->set_is_deprecated(proto.is_deprecated());
  options->set_alias_name(proto.alias_name());
  options->set_sql_name(proto.sql_name());
  options->set_allow_external_usage(proto.allow_external_usage());
  options->set_volatility(proto.volatility());
  options->set_supports_order_by(proto.supports_order_by());
  options->set_supports_safe_error_mode(proto.supports_safe_error_mode());
  for (const int each : proto.required_language_feature()) {
    options->AddRequiredLanguageFeature(LanguageFeature(each));
  }
  options->set_supports_limit(proto.supports_limit());
  options->set_supports_null_handling_modifier(
      proto.supports_null_handling_modifier());
  options->set_supports_having_modifier(proto.supports_having_modifier());
  options->set_supports_clamped_between_modifier(
      proto.supports_clamped_between_modifier());
  options->set_uses_upper_case_sql_name(proto.uses_upper_case_sql_name());
  options->set_may_suppress_side_effects(proto.may_suppress_side_effects());
  options->module_name_from_import.reserve(
      proto.module_name_from_import_size());
  for (const auto& module_name : proto.module_name_from_import()) {
    options->module_name_from_import.push_back(module_name);
  }
  *result = std::move(options);
  return absl::OkStatus();
}

void FunctionOptions::Serialize(FunctionOptionsProto* proto) const {
  proto->Clear();
  proto->set_supports_over_clause(supports_over_clause);
  proto->set_window_ordering_support(window_ordering_support);
  proto->set_supports_window_framing(supports_window_framing);
  proto->set_arguments_are_coercible(arguments_are_coercible);
  proto->set_is_deprecated(is_deprecated);
  proto->set_alias_name(alias_name);
  proto->set_sql_name(sql_name);
  proto->set_allow_external_usage(allow_external_usage);
  proto->set_volatility(volatility);
  proto->set_supports_order_by(supports_order_by);
  proto->set_supports_safe_error_mode(supports_safe_error_mode);
  proto->set_supports_having_modifier(supports_having_modifier);
  proto->set_supports_clamped_between_modifier(
      supports_clamped_between_modifier);
  proto->set_uses_upper_case_sql_name(uses_upper_case_sql_name);

  for (const LanguageFeature each : required_language_features) {
    proto->add_required_language_feature(each);
  }
  proto->set_supports_limit(supports_limit);
  proto->set_supports_null_handling_modifier(supports_null_handling_modifier);
  proto->set_may_suppress_side_effects(may_suppress_side_effects);
  for (const std::string& module_name : module_name_from_import) {
    proto->add_module_name_from_import(module_name);
  }
}

FunctionOptions& FunctionOptions::set_evaluator(
    const FunctionEvaluator& function_evaluator) {
  set_evaluator_factory(
      [function_evaluator](const FunctionSignature&) {
        return function_evaluator;
      });
  return *this;
}

bool FunctionOptions::CheckAllRequiredFeaturesAreEnabled(
    const LanguageOptions::LanguageFeatureSet& enabled_features) const {
  for (const LanguageFeature& feature : required_language_features) {
    if (enabled_features.find(feature) == enabled_features.end()) {
      return false;
    }
  }
  return true;
}

bool FunctionOptions::RequiresFeature(LanguageFeature feature) const {
  return required_language_features.find(feature) !=
         required_language_features.end();
}

const char Function::kGoogleSQLFunctionGroupName[] = "GoogleSQL";

Function::Function(std::vector<std::string> name_path, absl::string_view group,
                   Mode mode,
                   std::vector<FunctionSignature> function_signatures,
                   FunctionOptions function_options)
    : function_name_path_(std::move(name_path)),
      group_(std::move(group)),
      mode_(mode),
      function_signatures_(std::move(function_signatures)),
      function_options_(std::move(function_options)) {
  GOOGLESQL_CHECK_OK(CheckWindowSupportOptions());
  for (const FunctionSignature& signature : function_signatures_) {
    GOOGLESQL_CHECK_OK(signature.IsValidForFunction())
        << signature.DebugString(FullName());
  }
  GOOGLESQL_CHECK_OK(CheckMultipleSignatureMatchingSameFunctionCall());
}

// A FunctionDeserializer for functions by group name. Case-sensitive. Thread
// safe after module initializers.
static std::map<std::string, Function::FunctionDeserializer>*
FunctionDeserializers() {
  static auto* function_deserializers =
      new std::map<std::string, Function::FunctionDeserializer>;
  return function_deserializers;
}

absl::Status Function::Deserialize(
    const FunctionProto& proto,
    const std::vector<const google::protobuf::DescriptorPool*>& pools,
    TypeFactory* factory, std::unique_ptr<Function>* result) {
  GOOGLESQL_ASSIGN_OR_RETURN(*result,
                   Deserialize(proto, TypeDeserializer(factory, pools)));
  return absl::OkStatus();
}

absl::StatusOr<std::unique_ptr<Function>> Function::Deserialize(
    const FunctionProto& proto, const TypeDeserializer& type_deserializer) {
  // First check if there is a custom deserializer for this function group.
  // If so, invoke it early and return instead.
  if (proto.has_group()) {
    FunctionDeserializer* custom_deserializer =
        googlesql_base::FindOrNull(*FunctionDeserializers(), proto.group());
    if (custom_deserializer != nullptr) {
      std::unique_ptr<Function> result;
      GOOGLESQL_RETURN_IF_ERROR((*custom_deserializer)(
          proto,
          std::vector<const google::protobuf::DescriptorPool*>(
              type_deserializer.descriptor_pools().begin(),
              type_deserializer.descriptor_pools().end()),
          type_deserializer.type_factory(), &result));
      return result;
    }
  }

  std::vector<std::string> name_path;
  for (const std::string& name : proto.name_path()) {
    name_path.push_back(name);
  }

  std::vector<FunctionSignature> function_signatures;
  for (const auto& signature_proto : proto.signature()) {
    GOOGLESQL_ASSIGN_OR_RETURN(
        std::unique_ptr<FunctionSignature> signature,
        FunctionSignature::Deserialize(signature_proto, type_deserializer));
    function_signatures.push_back(*signature);
  }

  std::unique_ptr<FunctionOptions> options;
  GOOGLESQL_RETURN_IF_ERROR(FunctionOptions::Deserialize(proto.options(), &options));

  auto function = std::make_unique<Function>(
      name_path, proto.group(), proto.mode(), function_signatures, *options);
  function->set_sql_security(proto.sql_security());
  function->set_statement_context(proto.statement_context());
  return function;
}

absl::Status Function::Serialize(
    FileDescriptorSetMap* file_descriptor_set_map,
    FunctionProto* proto, bool omit_signatures) const {
  for (const std::string& name : this->FunctionNamePath()) {
    proto->add_name_path(name);
  }

  if (!omit_signatures) {
    for (const googlesql::FunctionSignature& signature : this->signatures()) {
      GOOGLESQL_RETURN_IF_ERROR(signature.Serialize(
          file_descriptor_set_map, proto->add_signature()));
    }
  }

  proto->set_mode(mode());
  proto->set_group(GetGroup());
  if (sql_security() !=
      ResolvedCreateStatementEnums::SQL_SECURITY_UNSPECIFIED) {
    proto->set_sql_security(sql_security());
  }
  if (statement_context() != CONTEXT_DEFAULT) {
    proto->set_statement_context(statement_context());
  }
  function_options().Serialize(proto->mutable_options());

  return absl::OkStatus();
}

// static
void Function::RegisterDeserializer(const std::string& group_name,
                                    FunctionDeserializer deserializer) {
  // ABSL_CHECK validated -- This is used at initialization time only.
  ABSL_CHECK(googlesql_base::InsertIfNotPresent(FunctionDeserializers(), group_name,
                                deserializer));
}

std::string Function::FullName(bool include_group) const {
  return absl::StrCat((include_group ? absl::StrCat(group_, ":") : ""),
                      absl::StrJoin(function_name_path_, "."));
}

std::string Function::SQLName() const {
  std::string name;
  if (!function_options_.sql_name.empty()) {
    name = function_options_.sql_name;
  } else if (absl::StartsWith(Name(), "$")) {
    // The name starts with '$' so it is an internal function name.  Strip off
    // the leading '$', convert all '_' to ' ', and upper case it.
    name = absl::StrReplaceAll(Name().substr(1), {{"_", " "}});
  } else if (IsGoogleSQLBuiltin()) {
    name = FullName(/*include_group=*/ false);
  } else {
    name = FullName();
  }
  if (function_options_.uses_upper_case_sql_name) {
    absl::AsciiStrToUpper(&name);
  }
  return name;
}

std::string Function::QualifiedSQLName(bool capitalize_qualifier) const {
  std::string qualifier;
  switch (mode_) {
    case Function::AGGREGATE:
      qualifier = "aggregate ";
      break;
    case Function::ANALYTIC:
      qualifier = "analytic ";
      break;
    default:
      break;
  }
  if (is_operator()) {
    absl::StrAppend(&qualifier, "operator ");
  } else {
    absl::StrAppend(&qualifier, "function ");
  }
  if (!qualifier.empty() && capitalize_qualifier) {
    qualifier[0] = toupper(qualifier[0]);
  }
  return absl::StrCat(qualifier, SQLName());
}

int Function::NumSignatures() const {
  return signatures().size();
}

const std::vector<FunctionSignature>& Function::signatures() const {
  return function_signatures_;
}

void Function::ResetSignatures(
    const std::vector<FunctionSignature>& signatures) {
  function_signatures_ = signatures;
  for (const FunctionSignature& signature : signatures) {
    GOOGLESQL_CHECK_OK(signature.IsValidForFunction())
        << signature.DebugString(FullName());
  }
}

// Check that `current_signature` and `new_signature` could possibly match one
// function call with lambda.
static bool SignaturesWithLambdaCouldMatchOneFunctionCall(
    const FunctionSignature& current_signature,
    const FunctionSignature& new_signature) {
  if (current_signature.arguments().size() !=
      new_signature.arguments().size()) {
    return false;
  }
  bool has_lambda = false;
  for (int i = 0; i < current_signature.arguments().size(); i++) {
    const auto& cur_arg = current_signature.argument(i);
    const auto& new_arg = new_signature.argument(i);
    has_lambda = has_lambda || cur_arg.IsLambda() || new_arg.IsLambda();
    if (cur_arg.IsLambda() && new_arg.IsLambda()) {
      if (cur_arg.lambda().argument_types().size() ==
          new_arg.lambda().argument_types().size()) {
        continue;
      }
      return false;
    }
    // If one arg is lambda and the other is not.
    if (cur_arg.IsLambda() != new_arg.IsLambda()) {
      return false;
    }
  }
  return has_lambda;
}

// Check that we don't have multiple signatures with lambda possibly matching
// the same function call. An example is a function with following signatures:
//     Func(T1, T1->BOOL)
//     Func(INT64, INT64->BOOL);
// for function call: Func(1, e->e>0); The two signatures both match the call.
static absl::Status CheckLambdaSignatures(
    const absl::Span<const FunctionSignature> current_signatures,
    const FunctionSignature& new_signature) {
  for (const auto& current_signature : current_signatures) {
    GOOGLESQL_RET_CHECK(!SignaturesWithLambdaCouldMatchOneFunctionCall(current_signature,
                                                             new_signature))
        << "Having two signatures with the same lambda at the same argument "
           "index is not allowed. Signature 1: "
        << current_signature.DebugString()
        << " Signature 2: " << new_signature.DebugString();
  }
  return absl::OkStatus();
}

absl::Status Function::CheckMultipleSignatureMatchingSameFunctionCall() const {
  for (int i = 1; i < function_signatures_.size(); i++) {
    GOOGLESQL_RETURN_IF_ERROR(CheckLambdaSignatures(
        absl::MakeConstSpan(function_signatures_).subspan(0, i),
        function_signatures_[i]));
  }
  return absl::OkStatus();
}

void Function::AddSignature(const FunctionSignature& signature) {
  GOOGLESQL_CHECK_OK(CheckLambdaSignatures(function_signatures_, signature))
      << signature.DebugString(FullName());
  function_signatures_.push_back(signature);
  GOOGLESQL_CHECK_OK(signature.IsValidForFunction()) << signature.DebugString(FullName());
}

absl::Status Function::AddSignature(const TypeKind result_kind,
                                    absl::Span<const TypeKind> input_kinds,
                                    void* context, TypeFactory* factory) {
  if (!Type::IsSimpleType(result_kind)) {
    return MakeSqlError()
           << "Result TypeKinds should be simple type kinds, but found: "
           << Type::TypeKindToString(result_kind, PRODUCT_INTERNAL);
  }

  FunctionArgumentTypeList arguments;
  for (const TypeKind input_kind : input_kinds) {
    if (!Type::IsSimpleType(input_kind)) {
      return MakeSqlError()
             << "Input TypeKinds should be simple type kinds, but found: "
             << Type::TypeKindToString(input_kind, PRODUCT_INTERNAL);
    }
    arguments.push_back(
        FunctionArgumentType(factory->MakeSimpleType(input_kind)));
  }

  AddSignature(FunctionSignature(
      FunctionArgumentType(factory->MakeSimpleType(result_kind)),
      arguments, context));
  return absl::OkStatus();
}

Function* Function::AddSignatureOrDie(const TypeKind result_kind,
                                      absl::Span<const TypeKind> input_kinds,
                                      void* context, TypeFactory* factory) {
  GOOGLESQL_CHECK_OK(AddSignature(result_kind, input_kinds, context, factory));
  return this;
}

const FunctionSignature* Function::GetSignature(int idx) const {
  if (idx < 0 || idx >= NumSignatures()) {
    return nullptr;
  }
  return &(function_signatures_[idx]);
}

FunctionSignature* Function::GetMutableSignature(int idx) {
  if (idx < 0 || idx >= NumSignatures()) {
    return nullptr;
  }
  return &(function_signatures_[idx]);
}

std::string Function::DebugString(bool verbose) const {
  if (verbose) {
    return absl::StrCat(
        FullName(),
        (alias_name().empty() ? "" : absl::StrCat("|", alias_name())),
        (function_signatures_.empty() ? "" : "\n"),
        FunctionSignature::SignaturesToString(function_signatures_, verbose));
  }
  return FullName();
}

std::string Function::GetSQL(std::vector<std::string> inputs,
                             const FunctionSignature* signature,
                             absl::string_view arguments_prefix,
                             absl::string_view arguments_suffix, bool safe_call,
                             bool chained_call) const {
  // Save the original first argument so we can detect below if
  // UpdateArgsForGetSQL modified it (e.g. to make it a named argument).
  const std::string orig_input0 = inputs.empty() ? "" : inputs.front();

  UpdateArgsForGetSQL(signature, &inputs);

  if (GetSQLCallback() != nullptr) {
    // The callback uses an older signature that requires hacking the
    // prefix and suffix in as part of inputs.
    if (!arguments_prefix.empty()) {
      if (inputs.empty()) {
        inputs.push_back(std::string(arguments_prefix));
      } else {
        inputs[0] = absl::StrCat(arguments_prefix, " ", inputs[0]);
      }
    }
    if (!arguments_suffix.empty()) {
      if (inputs.empty()) {
        inputs.push_back(std::string(arguments_suffix));
      } else {
        absl::StrAppend(&inputs.back(), " ", arguments_suffix);
      }
    }

    std::string sql = GetSQLCallback()(inputs);

    if (safe_call) {
      // By default, assume that we can use "SAFE." prefixes on the generated
      // SQL to make a SAFE call, since the looks like a function call.
      // Operators that don't support this shouldn't have SAFE calls.
      sql = absl::StrCat("SAFE.", sql);
    }

    return sql;
  }

  // Generate a chained call when requested, but only if possible.
  // Chained calls won't work for zero-arg functions or if the first argument
  // was a named argument or other special form.  We detect those cases by
  // checking if the first argument was modified by UpdateArgsForGetSQL.
  if (chained_call && (inputs.empty() || orig_input0 != inputs.front())) {
    chained_call = false;
  }

  std::string name;
  if (safe_call) {
    name = "SAFE.";
  }

  absl::StrAppend(&name, FullName(/*include_group=*/false));

  if (chained_call && absl::StrContains(name, '.')) {
    name = absl::StrCat("(", name, ")");
  }

  if (function_options_.uses_upper_case_sql_name) {
    absl::AsciiStrToUpper(&name);
  }
  if (chained_call) {
    // Currently, we always add parentheses around the base argument.
    // Detecting when they are unneeded is tricky without an AST.
    // Looking for a trailing ")" isn't enough because an expression like
    // "a + b()" requires parentheses too.
    name = absl::StrCat("(", inputs.front(), ").", name);
    inputs.erase(inputs.begin());
  }
  return absl::StrCat(
      name, "(", arguments_prefix, arguments_prefix.empty() ? "" : " ",
      absl::StrJoin(inputs, ", "), arguments_suffix.empty() ? "" : " ",
      arguments_suffix, ")");
}

absl::Status Function::CheckPreResolutionArgumentConstraints(
    const std::vector<InputArgumentType>& arguments,
    const LanguageOptions& language_options) const {
  if (PreResolutionConstraints() == nullptr) {
    return absl::OkStatus();
  }
  return PreResolutionConstraints()(arguments, language_options);
}

absl::Status Function::CheckPostResolutionArgumentConstraints(
    const FunctionSignature& signature,
    const std::vector<InputArgumentType>& arguments,
    const LanguageOptions& language_options) const {
  if (PostResolutionConstraints() == nullptr) {
    return absl::OkStatus();
  }
  GOOGLESQL_RET_CHECK(signature.IsConcrete() ||
            (signature.HasConcreteArguments() &&
             GetComputeResultTypeCallback() != nullptr))
      << "CheckPostResolutionArgumentConstraints of " << QualifiedSQLName()
      << " must be called with a concrete signature, or concrete arguments"
      << " with a ComputeResultTypeCallback";
  GOOGLESQL_RET_CHECK_EQ(signature.NumConcreteArguments(), arguments.size())
      << "Concrete arguments of " << QualifiedSQLName()
      << " must match the actual argument list";
  return PostResolutionConstraints()(signature, arguments, language_options);
}

// static
std::string Function::GetGenericNoMatchingFunctionSignatureErrorMessage(
    absl::string_view qualified_function_name,
    absl::Span<const InputArgumentType> arguments, ProductMode product_mode,
    absl::Span<const absl::string_view> argument_names,
    bool argument_types_on_new_line) {
  return absl::StrCat(
      "No matching signature for ", qualified_function_name,
      (arguments.empty()
           ? " with no arguments"
           : absl::StrCat(argument_types_on_new_line ? "\n  Argument types: "
                                                     : " for argument types: ",
                          InputArgumentType::ArgumentsToString(
                              arguments, product_mode, argument_names))));
}

std::string Function::GetNoMatchingFunctionSignatureErrorMessage(
    const std::vector<InputArgumentType>& arguments, ProductMode product_mode,
    absl::Span<const absl::string_view> argument_names,
    bool argument_types_on_new_line) const {
  if (GetNoMatchingSignatureCallback() != nullptr) {
    return GetNoMatchingSignatureCallback()(QualifiedSQLName(), arguments,
                                            product_mode);
  }
  return GetGenericNoMatchingFunctionSignatureErrorMessage(
      QualifiedSQLName(), arguments, product_mode, argument_names,
      argument_types_on_new_line);
}

// TODO: When we use this to make error messages for signatures that
// take templated args like ANY, the error messages aren't very good.  Fix
// this.
std::string Function::GetSupportedSignaturesUserFacingText(
    const LanguageOptions& language_options,
    FunctionArgumentType::NamePrintingStyle print_style, int* num_signatures,
    bool print_template_details) const {
  // Make a good guess
  *num_signatures = NumSignatures();
  if (HideSupportedSignatures()) {
    return "";
  }
  if (GetSupportedSignaturesCallback() != nullptr) {
    return GetSupportedSignaturesCallback()(language_options, *this);
  }
  std::string supported_signatures;
  *num_signatures = 0;
  for (const FunctionSignature& signature : signatures()) {
    // Ignore deprecated signatures, and signatures that include
    // unsupported data types.
    if (signature.HideInSupportedSignatureList(language_options)) {
      continue;
    }
    if (!supported_signatures.empty()) {
      absl::StrAppend(&supported_signatures, "; ");
    }
    (*num_signatures)++;
    if (HasSignatureTextCallback()) {
      absl::StrAppend(
          &supported_signatures,
          GetSignatureTextCallback()(language_options, *this, signature));
    } else {
      std::vector<std::string> argument_texts =
          signature.GetArgumentsUserFacingTextWithCardinality(
              language_options, print_style, print_template_details);
      absl::StrAppend(&supported_signatures, GetSQL(argument_texts));
    }
  }
  return supported_signatures;
}

const ArgumentConstraintsCallback& Function::PreResolutionConstraints() const {
  return function_options_.pre_resolution_constraint;
}

const PostResolutionArgumentConstraintsCallback&
Function::PostResolutionConstraints() const {
  return function_options_.post_resolution_constraint;
}

const ComputeResultTypeCallback& Function::GetComputeResultTypeCallback()
    const {
  return function_options_.compute_result_type_callback;
}

const FunctionGetSQLCallback& Function::GetSQLCallback() const {
  return function_options_.get_sql_callback;
}

const NoMatchingSignatureCallback& Function::GetNoMatchingSignatureCallback()
    const {
  return function_options_.no_matching_signature_callback;
}

const SupportedSignaturesCallback& Function::GetSupportedSignaturesCallback()
    const {
  return function_options_.supported_signatures_callback;
}

bool Function::HideSupportedSignatures() const {
  return function_options_.hide_supported_signatures;
}

bool Function::HasSignatureTextCallback() const {
  return GetSignatureTextCallback() != nullptr;
}

const SignatureTextCallback& Function::GetSignatureTextCallback() const {
  return function_options_.signature_text_callback;
}

const BadArgumentErrorPrefixCallback&
Function::GetBadArgumentErrorPrefixCallback() const {
  return function_options_.bad_argument_error_prefix_callback;
}

FunctionEvaluatorFactory Function::GetFunctionEvaluatorFactory() const {
  return function_options_.function_evaluator_factory;
}

AggregateFunctionEvaluatorFactory
Function::GetAggregateFunctionEvaluatorFactory() const {
  return function_options_.aggregate_function_evaluator_factory;
}

absl::Status Function::CheckWindowSupportOptions() const {
  if (IsScalar() && SupportsOverClause()) {
    return MakeSqlError() << "Scalar functions cannot support OVER clause";
  }
  if (IsAnalytic() && !SupportsOverClause()) {
    return MakeSqlError() << "Analytic functions must support OVER clause";
  }
  return absl::OkStatus();
}

bool Function::SupportsOverClause() const {
  return function_options_.supports_over_clause;
}

bool Function::SupportsWindowOrdering() const {
  return function_options_.window_ordering_support ==
             FunctionOptions::ORDER_REQUIRED ||
         function_options_.window_ordering_support ==
             FunctionOptions::ORDER_OPTIONAL;
}

bool Function::RequiresWindowOrdering() const {
  return function_options_.window_ordering_support ==
             FunctionOptions::ORDER_REQUIRED;
}

bool Function::SupportsWindowFraming() const {
  return function_options_.supports_window_framing;
}

bool Function::SupportsOrderingArguments() const {
  return function_options_.supports_order_by;
}

bool Function::SupportsLimitArguments() const {
  return function_options_.supports_limit;
}

bool Function::SupportsNullHandlingModifier() const {
  return function_options_.supports_null_handling_modifier;
}

bool Function::SupportsSafeErrorMode() const {
  return function_options_.supports_safe_error_mode;
}

bool Function::SupportsHavingModifier() const {
  return function_options_.supports_having_modifier;
}

bool Function::SupportsDistinctModifier() const {
  return function_options_.supports_distinct_modifier;
}

bool Function::SupportsGroupByModifier() const {
  return function_options_.supports_group_by_modifier;
}

bool Function::SupportsWhereModifier() const {
  return function_options_.supports_where_modifier;
}

bool Function::SupportsHavingFilterModifier() const {
  return function_options_.supports_having_filter_modifier;
}

bool Function::SupportsClampedBetweenModifier() const {
  return function_options_.supports_clamped_between_modifier;
}

bool Function::is_operator() const { return FunctionIsOperator(*this); }

}  // namespace googlesql
