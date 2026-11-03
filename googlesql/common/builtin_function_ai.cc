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

#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "googlesql/common/builtin_function_internal.h"
#include "googlesql/common/errors.h"
#include "googlesql/public/builtin_function.pb.h"
#include "googlesql/public/builtin_function_options.h"
#include "googlesql/public/function.h"
#include "googlesql/public/function.pb.h"
#include "googlesql/public/function_signature.h"
#include "googlesql/public/options.pb.h"
#include "googlesql/public/type.h"
#include "absl/functional/bind_front.h"
#include "absl/log/log.h"
#include "absl/status/status.h"
#include "googlesql/base/status_macros.h"
#include "absl/strings/ascii.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/str_join.h"
#include "absl/strings/string_view.h"
#include "absl/types/span.h"
#include "googlesql/base/ret_check.h"

namespace googlesql {

constexpr absl::string_view kPromptArgName = "prompt";
constexpr absl::string_view kPayloadArgName = "payload";
constexpr absl::string_view kModelArgName = "model";
constexpr absl::string_view kOptionsArgName = "options";

// Verify that the STRUCT overload for `prompt` doesn't have more than one
// anonymous data payload field. According to the spec, the first STRUCT field
// is the primary instruction field, while the second field and onwards are the
// data payload fields.
static absl::Status ValidateStructPrompt(
    const FunctionSignature& signature,
    absl::Span<const InputArgumentType> args,
    const LanguageOptions& language_options) {
  GOOGLESQL_RET_CHECK(signature.arguments().size() == args.size());
  GOOGLESQL_RET_CHECK(!args.empty());
  const InputArgumentType& prompt_arg = args[0];
  const Type* prompt_type = prompt_arg.type();
  if (signature.arguments()[0].argument_name() == kPromptArgName &&
      prompt_type != nullptr) {
    if (prompt_type->IsStruct()) {
      const StructType* struct_type = prompt_type->AsStruct();
      int anonymous_field_count = 0;
      // Assume first field counts as the primary instruction field, which
      // shouldn't be checked. The rest of the fields are the data payload
      // fields.
      for (int i = 1; i < struct_type->num_fields(); ++i) {
        if (struct_type->field(i).name.empty()) {
          anonymous_field_count++;
        }
        if (anonymous_field_count > 1) {
          return MakeSqlError() << "More than one anonymous data payload field "
                                   "is not allowed";
        }
      }
    }
  }
  return absl::OkStatus();
}

// Verifies that the `AI.IF` `options` argument has a `STRUCT` type for
// signatures where `options` uses `ARG_KIND_EXPR_ARBITRARY`. Returns a
// signature mismatch message if `options` is not a `STRUCT`, or an empty string
// otherwise.
static std::string CheckStructForAiIfOptionsConstraint(
    ProductMode product_mode, const FunctionSignature& matched_signature,
    absl::Span<const InputArgumentType> arguments) {
  const FunctionArgumentTypeList& matched_signature_args =
      matched_signature.arguments();
  if (!matched_signature_args.empty() && !arguments.empty()) {
    const FunctionArgumentType& last_arg = matched_signature_args.back();
    if (last_arg.argument_name() == kOptionsArgName &&
        last_arg.original_kind() == ARG_KIND_EXPR_ARBITRARY) {
      const InputArgumentType& options_arg = arguments.back();
      if (options_arg.type() == nullptr || !options_arg.type()->IsStruct()) {
        return absl::StrCat(
            "Named argument `options`: expected STRUCT but found ",
            options_arg.UserFacingName(product_mode));
      }
    }
  }
  return "";
}

// Formats the SQL representation of an `AI.IF` call or signature. Replaces
// `options => ANY` with `options => STRUCT` in user-facing signature strings,
// since `ARG_KIND_EXPR_ARBITRARY` is used to represent the `STRUCT` overload of
// `options`.
static std::string AiIfFunctionSQL(absl::Span<const std::string> inputs) {
  if (!inputs.empty() && inputs.back() == "options => ANY") {
    std::vector<std::string> updated_inputs(inputs.begin(), inputs.end());
    updated_inputs.back() = "options => STRUCT";
    return absl::StrCat("AI.IF(", absl::StrJoin(updated_inputs, ", "), ")");
  }
  return absl::StrCat("AI.IF(", absl::StrJoin(inputs, ", "), ")");
}

// Generates the string version of a signature ID from a list of `args`.
static absl::StatusOr<std::string> GetSignatureIdFromArgs(
    const FunctionArgumentTypeList& args) {
  std::string id = "FN_AI_IF_WITH";
  for (const FunctionArgumentType& arg : args) {
    std::string arg_name_upper = absl::AsciiStrToUpper(arg.argument_name());
    if (arg.kind() == ARG_KIND_EXPR_STRUCT_ANY ||
        arg.kind() == ARG_KIND_EXPR_ARBITRARY) {
      absl::StrAppend(&id, "_STRUCT_", arg_name_upper);
    } else if (arg.kind() == ARG_KIND_EXPR_ANY_1) {
      absl::StrAppend(&id, "_ANY_", arg_name_upper);
    } else if (arg.kind() == ARG_KIND_MODEL) {
      absl::StrAppend(&id, "_MODEL_REF");
    } else {
      GOOGLESQL_RET_CHECK_NE(arg.type(), nullptr) << "Unsupported argument type.";
      absl::StrAppend(&id, "_", arg.type()->DebugString(), "_", arg_name_upper);
    }
  }
  return id;
}

// Computes the cross product of `current_signatures` and `arg_types`, returning
// a new list of signatures with the new argument added.
// AI.IF example:
// curr_signature_args = {{string prompt}, {struct prompt}}
// output: {{string prompt}, {struct prompt}, {string prompt, any payload}}
std::vector<FunctionArgumentTypeList> CrossProduct(
    absl::Span<const FunctionArgumentTypeList> curr_signature_args,
    absl::Span<const std::optional<FunctionArgumentType>> arg_types) {
  std::vector<FunctionArgumentTypeList> result;
  // Each `sig_args` represents the set of arguments for one logical signature.
  for (const FunctionArgumentTypeList& sig_args : curr_signature_args) {
    // Each `arg_type` represents one possible FunctionArgumentType for a new
    // set of logical signatures that includes argument with `arg_name`.
    for (const std::optional<FunctionArgumentType>& arg_type : arg_types) {
      // `arg_type` may be an optional argument, which is why sometimes
      // `arg_type` may be std::nullopt to represent `arg_name` being
      // unspecified.
      if (arg_type.has_value()) {
        FunctionArgumentTypeList new_sig_args = sig_args;
        new_sig_args.emplace_back(*(std::move(arg_type)));
        result.emplace_back(new_sig_args);
      }
    }
  }
  result.insert(result.end(), curr_signature_args.begin(),
                curr_signature_args.end());
  return result;
}

// Generates AI.IF signatures for a given prompt type. Keeps main AI.IF logic
// separated from future AI functions.

// For now, since we would like to distinguish explicitly passed NULL
// arguments from omitted arguments as different AI.IF invocations and
// because no default values exist when arguments are unspecified,
// FunctionArgumentType::OPTIONAL wouldn't fit here.
std::vector<FunctionArgumentTypeList> GenerateAiIfSignatureArgs() {
  FunctionArgumentTypeOptions prompt_arg_type_options =
      FunctionArgumentTypeOptions().set_argument_name(kPromptArgName,
                                                      kPositionalOrNamed);
  FunctionArgumentTypeOptions payload_arg_type_options =
      FunctionArgumentTypeOptions().set_argument_name(kPayloadArgName,
                                                      kPositionalOrNamed);
  FunctionArgumentTypeOptions model_arg_type_options =
      FunctionArgumentTypeOptions()
          .set_argument_name(kModelArgName, kNamedOnly)
          .set_must_be_query_constant();
  FunctionArgumentTypeOptions options_arg_type_options =
      FunctionArgumentTypeOptions()
          .set_argument_name(kOptionsArgName, kNamedOnly)
          .set_must_be_query_constant();
  const Type* string_type = types::StringType();
  const Type* json_type = types::JsonType();
  FunctionArgumentTypeList prompt_types = {
      FunctionArgumentType(string_type, prompt_arg_type_options),
      FunctionArgumentType(ARG_KIND_EXPR_STRUCT_ANY, prompt_arg_type_options)};
  std::vector<std::optional<FunctionArgumentType>> payload_arg_types = {
      std::nullopt,
      FunctionArgumentType(ARG_KIND_EXPR_ANY_1, payload_arg_type_options)};
  std::vector<std::optional<FunctionArgumentType>> model_arg_types = {
      std::nullopt, FunctionArgumentType(string_type, model_arg_type_options),
      FunctionArgumentType(ARG_KIND_MODEL, model_arg_type_options)};
  // Use `ARG_KIND_EXPR_ARBITRARY` (constrained to `STRUCT` via
  // `CheckStructForAiIfOptionsConstraint`) instead of
  // `ARG_KIND_EXPR_STRUCT_ANY` for the `STRUCT` overload of `options` so that
  // `prompt` and `options` are not forced to share a common `STRUCT` type
  // during signature matching.
  std::vector<std::optional<FunctionArgumentType>> options_arg_types = {
      std::nullopt, FunctionArgumentType(json_type, options_arg_type_options),
      FunctionArgumentType(ARG_KIND_EXPR_ARBITRARY, options_arg_type_options),
      FunctionArgumentType(string_type, options_arg_type_options)};

  std::vector<FunctionArgumentTypeList> all_sigs;

  for (const auto& prompt_type : prompt_types) {
    std::vector<FunctionArgumentTypeList> sigs;
    sigs.push_back({prompt_type});
    if (prompt_type.type() == string_type) {
      sigs = CrossProduct(sigs, payload_arg_types);
    }
    sigs = CrossProduct(sigs, model_arg_types);
    sigs = CrossProduct(sigs, options_arg_types);
    all_sigs.insert(all_sigs.end(), sigs.begin(), sigs.end());
  }
  return all_sigs;
}

absl::Status GetAIFunctions(TypeFactory* /*type_factory*/,
                            const GoogleSQLBuiltinFunctionOptions& options,
                            NameToFunctionMap* functions) {
  // TODO: Remove this check once engine catalogs are compatible
  // with the GoogleSQL built-in AI.IF function.
  if (options.language_options.LanguageFeatureEnabled(FEATURE_DISABLE_AI_IF)) {
    return absl::OkStatus();
  }
  // Volatile because AI functions will have non-deterministic results; the
  // results depend on the LLM chosen.
  FunctionOptions function_options =
      FunctionOptions()
          .set_volatility(FunctionEnums::VOLATILE)
          .set_post_resolution_argument_constraint(ValidateStructPrompt)
          .set_get_sql_callback(&AiIfFunctionSQL);

  // Generate all AI.IF signature argument combinations first.
  std::vector<FunctionArgumentTypeList> all_ai_if_signature_args =
      GenerateAiIfSignatureArgs();

  // Add and create AI.IF signatures with those argument combinations.
  std::vector<FunctionSignatureOnHeap> ai_if_signatures;
  ai_if_signatures.reserve(all_ai_if_signature_args.size());
  FunctionSignatureId enum_id;
  for (const FunctionArgumentTypeList& signature_arg :
       all_ai_if_signature_args) {
    GOOGLESQL_ASSIGN_OR_RETURN(std::string signature_id,
                     GetSignatureIdFromArgs(signature_arg));
    GOOGLESQL_RET_CHECK(FunctionSignatureId_Parse(signature_id, &enum_id))
        << "Failed to parse signature id: " << signature_id;

    // Only the `STRUCT` overload for `options` (which is always the last
    // argument when present) uses `ARG_KIND_EXPR_ARBITRARY`. Attach a
    // signature constraint to restrict it to `STRUCT` types.
    FunctionSignatureOptions signature_options;
    if (!signature_arg.empty() &&
        signature_arg.back().argument_name() == kOptionsArgName &&
        signature_arg.back().kind() == ARG_KIND_EXPR_ARBITRARY) {
      signature_options.set_constraints(
          absl::bind_front(&CheckStructForAiIfOptionsConstraint,
                           options.language_options.product_mode()));
    }
    ai_if_signatures.push_back({/*result_type=*/types::BoolType(),
                                /*arguments=*/signature_arg,
                                /*context_id=*/enum_id, signature_options});
  }

  InsertNamespaceFunction(functions, options, "ai", "if", Function::SCALAR,
                          ai_if_signatures, function_options);

  return absl::OkStatus();
}

}  // namespace googlesql
