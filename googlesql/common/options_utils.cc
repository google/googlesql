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

#include "googlesql/common/options_utils.h"

#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "google/protobuf/descriptor.h"
#include "googlesql/public/evaluator.h"
#include "googlesql/public/language_options.h"
#include "googlesql/public/parse_tokens.h"
#include "absl/container/btree_set.h"
#include "absl/container/flat_hash_map.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/ascii.h"
#include "absl/strings/string_view.h"
#include "absl/types/span.h"

namespace googlesql::internal {

template <typename EnumT>
std::string UnparseEnumOptionsSet(
    const absl::flat_hash_map<absl::string_view, absl::btree_set<EnumT>>&
        base_factory,
    absl::string_view strip_prefix, absl::btree_set<EnumT> options) {
  for (const auto& [base_name, base_options] : base_factory) {
    if (base_options == options) {
      return std::string(base_name);
    }
  }

  // No exact match, just pick none, and add
  ABSL_CHECK(base_factory.contains("NONE"));
  const google::protobuf::EnumDescriptor* enum_descriptor =
      google::protobuf::GetEnumDescriptor<EnumT>();

  std::string output = "NONE";
  for (EnumT option : options) {
    const google::protobuf::EnumValueDescriptor* enum_value_descriptor =
        enum_descriptor->FindValueByNumber(option);
    ABSL_CHECK(enum_value_descriptor);
    absl::string_view name = enum_value_descriptor->name();
    absl::ConsumePrefix(&name, strip_prefix);
    absl::StrAppend(&output, ",+", name);
  }
  return output;
}

//////////////////////////////////////////////////////////////////////////
// ResolvedASTRewrite
//////////////////////////////////////////////////////////////////////////
AnalyzerOptions::ASTRewriteSet GetAllRewrites() {
  return GetRewrites(true, true);
}

AnalyzerOptions::ASTRewriteSet GetRewrites(bool include_in_development,
                                           bool include_default_disabled) {
  AnalyzerOptions::ASTRewriteSet enabled_set;
  const google::protobuf::EnumDescriptor* descriptor =
      google::protobuf::GetEnumDescriptor<ResolvedASTRewrite>();
  for (int i = 0; i < descriptor->value_count(); ++i) {
    const google::protobuf::EnumValueDescriptor* value_descriptor = descriptor->value(i);
    if (value_descriptor->number() == 0) {
      // This is the "INVALID" entry. Skip this case.
      continue;
    }
    bool default_enabled = value_descriptor->options()
                               .GetExtension(rewrite_options)
                               .default_enabled();
    bool in_development = value_descriptor->options()
                              .GetExtension(rewrite_options)
                              .in_development();
    if (!default_enabled && !include_default_disabled) {
      continue;
    }
    if (in_development && !include_in_development) {
      continue;
    }
    enabled_set.insert(
        static_cast<ResolvedASTRewrite>(value_descriptor->number()));
  }
  return enabled_set;
}

absl::StatusOr<EnumOptionsEntry<ResolvedASTRewrite>> ParseEnabledAstRewrites(
    absl::string_view options_str) {
  return internal::ParseEnumOptionsSet<ResolvedASTRewrite>(
      {{"NONE", {}},
       {"ALL", GetRewrites(/*include_in_development=*/true,
                           /*include_default_disabled=*/true)},
       {"ALL_MINUS_DEV", GetRewrites(/*include_in_development=*/false,
                                     /*include_default_disabled=*/true)},
       {"DEFAULTS", GetRewrites(/*include_in_development=*/true,
                                /*include_default_disabled=*/false)},
       {"DEFAULTS_MINUS_DEV", GetRewrites(/*include_in_development=*/false,
                                          /*include_default_disabled=*/false)}},
      "REWRITE_", "Rewrite", options_str);
}

bool AbslParseFlag(absl::string_view text, EnabledAstRewrites* p,
                   std::string* error) {
  absl::StatusOr<EnumOptionsEntry<ResolvedASTRewrite>> entry =
      ParseEnabledAstRewrites(text);
  if (!entry.ok()) {
    *error = entry.status().message();
    return false;
  }
  p->enabled_ast_rewrites = entry->options;
  return true;
}

std::string AbslUnparseFlag(EnabledAstRewrites p) {
  return UnparseEnumOptionsSet<ResolvedASTRewrite>(
      {{"NONE", {}},
       {"ALL", GetRewrites(/*include_in_development=*/true,
                           /*include_default_disabled=*/true)},
       {"ALL_MINUS_DEV", GetRewrites(/*include_in_development=*/false,
                                     /*include_default_disabled=*/true)},
       {"DEFAULTS", GetRewrites(/*include_in_development=*/true,
                                /*include_default_disabled=*/false)},
       {"DEFAULTS_MINUS_DEV", GetRewrites(/*include_in_development=*/false,
                                          /*include_default_disabled=*/false)}},
      "REWRITE_", p.enabled_ast_rewrites);
}

//////////////////////////////////////////////////////////////////////////
// LanguageFeature
//////////////////////////////////////////////////////////////////////////
static absl::btree_set<LanguageFeature> GetAllLanguageFeatures() {
  absl::btree_set<LanguageFeature> enabled_set;
  const google::protobuf::EnumDescriptor* descriptor =
      google::protobuf::GetEnumDescriptor<LanguageFeature>();
  for (int i = 0; i < descriptor->value_count(); ++i) {
    const google::protobuf::EnumValueDescriptor* value_descriptor = descriptor->value(i);
    enabled_set.insert(
        static_cast<LanguageFeature>(value_descriptor->number()));
  }
  for (LanguageFeature fake_feature :
       {__LanguageFeature__switch_must_have_a_default__}) {
    enabled_set.erase(fake_feature);
  }
  return enabled_set;
}

static absl::btree_set<LanguageFeature> GetMaximumLanguageFeatures() {
  auto tmp = LanguageOptions::MaximumFeatures().GetEnabledLanguageFeatures();
  return absl::btree_set<LanguageFeature>{tmp.begin(), tmp.end()};
}

static absl::btree_set<LanguageFeature> GetDevLanguageFeatures() {
  LanguageOptions options;
  options.EnableMaximumLanguageFeaturesForDevelopment();

  return absl::btree_set<LanguageFeature>{
      options.GetEnabledLanguageFeatures().begin(),
      options.GetEnabledLanguageFeatures().end()};
}

absl::StatusOr<EnumOptionsEntry<LanguageFeature>> ParseEnabledLanguageFeatures(
    absl::string_view options_str) {
  return internal::ParseEnumOptionsSet<LanguageFeature>(
      {{"NONE", {}},
       {"ALL", GetAllLanguageFeatures()},
       {"MAXIMUM", GetMaximumLanguageFeatures()},
       {"DEV", GetDevLanguageFeatures()}},
      "FEATURE_", "Feature", options_str);
}

bool AbslParseFlag(absl::string_view text, EnabledLanguageFeatures* p,
                   std::string* error) {
  absl::StatusOr<EnumOptionsEntry<LanguageFeature>> entry =
      ParseEnabledLanguageFeatures(text);
  if (!entry.ok()) {
    *error = entry.status().message();
    return false;
  }
  p->enabled_language_features = entry->options;
  return true;
}

std::string AbslUnparseFlag(EnabledLanguageFeatures p) {
  return UnparseEnumOptionsSet<LanguageFeature>(
      {{"NONE", {}},
       {"ALL", GetAllLanguageFeatures()},
       {"MAXIMUM", GetMaximumLanguageFeatures()},
       {"DEV", GetDevLanguageFeatures()}},
      "FEATURE_", p.enabled_language_features);
}

namespace {
struct ParameterDefinition {
  std::string name;        // Parameter name, lowercase
  std::string value_expr;  // Expression that evaluates to parameter value
};

absl::string_view GetInputTextFromParseTokenRange(
    absl::string_view entire_text, absl::Span<const ParseToken> parse_tokens) {
  if (parse_tokens.empty()) {
    return "";
  }
  ParseLocationPoint start = parse_tokens.at(0).GetLocationRange().start();
  ParseLocationPoint end =
      parse_tokens.at(parse_tokens.size() - 1).GetLocationRange().end();

  return entire_text.substr(start.GetByteOffset(),
                            end.GetByteOffset() - start.GetByteOffset());
}

absl::Status QueryParameterDefinitionSyntaxError(
    const ParseToken& parse_token) {
  return absl::InvalidArgumentError(absl::StrCat(
      "Syntax error; expected name=value; got ", parse_token.GetImage()));
}

absl::Status QueryParameterDefinitionSyntaxError(
    absl::string_view text, absl::Span<const ParseToken> parse_tokens) {
  return absl::InvalidArgumentError(
      absl::StrCat("Syntax error; expected name=value; got ",
                   GetInputTextFromParseTokenRange(text, parse_tokens)));
}

// Consumes a ParseToken representing the "name" of the "name=value" syntax.
// Returns the name contained, in lowercase.
//
// Accepts quoted or unquoted identifiers, so long as it is not a reserved
// keyword.
absl::StatusOr<std::string> GetParameterNameFromParseToken(
    const ParseToken& token, const LanguageOptions& language_options) {
  if (token.IsIdentifier()) {
    return absl::AsciiStrToLower(token.GetIdentifier());
  } else if (token.IsKeyword()) {
    if (!language_options.IsReservedKeyword(token.GetKeyword())) {
      return absl::AsciiStrToLower(token.GetKeyword());
    }
  }
  return QueryParameterDefinitionSyntaxError(token);
}

// Parses 'text' into a list of ParameterDefinition's using the GoogleSQL
// tokenizer.
absl::StatusOr<std::vector<ParameterDefinition>> GetParameterDefinitions(
    absl::string_view text, const LanguageOptions& language_options) {
  std::vector<ParseToken> parse_tokens_vector;
  auto resume_location = ParseResumeLocation::FromStringView(text);

  std::vector<ParameterDefinition> definitions;
  absl::flat_hash_set<std::string> parameter_names;
  bool end_of_input = false;
  while (!end_of_input) {
    GOOGLESQL_RETURN_IF_ERROR(
        GetParseTokens(ParseTokenOptions{.stop_at_end_of_statement = true},
                       &resume_location, &parse_tokens_vector));

    GOOGLESQL_RET_CHECK(!parse_tokens_vector.empty());
    const ParseToken& last_token = parse_tokens_vector.back();
    GOOGLESQL_RET_CHECK(last_token.IsEndOfInput() || last_token.GetKeyword() == ";");
    if (last_token.IsEndOfInput()) {
      end_of_input = true;
    }
    absl::Span<const ParseToken> parse_tokens =
        absl::MakeConstSpan(parse_tokens_vector)
            .subspan(0, parse_tokens_vector.size() - 1);

    if (parse_tokens.empty() && end_of_input) {
      // Ignore trailing semi-colon at the end.
      break;
    }

    if (parse_tokens.size() <= 2 || parse_tokens.at(1).GetKeyword() != "=") {
      return QueryParameterDefinitionSyntaxError(text, parse_tokens);
    }

    ParameterDefinition& definition = definitions.emplace_back();
    GOOGLESQL_ASSIGN_OR_RETURN(
        definition.name,
        GetParameterNameFromParseToken(parse_tokens.at(0), language_options));
    if (!parameter_names.insert(definition.name).second) {
      return absl::InvalidArgumentError(
          absl::StrCat("Duplicate query parameter '", definition.name, "'"));
    }
    definition.value_expr =
        GetInputTextFromParseTokenRange(text, parse_tokens.subspan(2));
  }
  return definitions;
}

// Parses a string specifying query parameter names and values according to the
// syntax: name1=value1;name2=value2...
//  - Only named parameters allowed, and names cannot be empty.
//  - Parameter names are converted to lowercase, and duplicate parameter names
//    are not allowed.
//  - Parameter values are specified as expressions, and evaluated using the
//    provided AnalyzerOptions and Catalog.
//  - Parameter expressions may reference previously-defined parameters.
//
absl::StatusOr<ParameterValueMap> ParseQueryParameterFlagHelper(
    absl::string_view text, const AnalyzerOptions& analyzer_options,
    Catalog* catalog) {
  GOOGLESQL_ASSIGN_OR_RETURN(std::vector<ParameterDefinition> param_defs,
                   GetParameterDefinitions(text, analyzer_options.language()));
  AnalyzerOptions analyzer_options_with_params(analyzer_options);
  ParameterValueMap parameter_map;
  for (const ParameterDefinition& param_def : param_defs) {
    EvaluatorOptions evaluator_options;
    PreparedExpression prepared_expr(param_def.value_expr, evaluator_options);
    GOOGLESQL_RETURN_IF_ERROR(
        prepared_expr.Prepare(analyzer_options_with_params, catalog));
    GOOGLESQL_ASSIGN_OR_RETURN(Value parameter_value, prepared_expr.ExecuteAfterPrepare(
                                                /*columns=*/{},
                                                /*parameters=*/parameter_map));
    GOOGLESQL_RETURN_IF_ERROR(analyzer_options_with_params.AddQueryParameter(
        param_def.name, parameter_value.type()));
    parameter_map[param_def.name] = std::move(parameter_value);
  }
  return parameter_map;
}
}  // namespace

bool ParseQueryParameterFlag(absl::string_view text,
                             const AnalyzerOptions& analyzer_options,
                             Catalog* catalog, ParameterValueMap* map,
                             std::string* err) {
  absl::StatusOr<ParameterValueMap> status_or =
      ParseQueryParameterFlagHelper(text, analyzer_options, catalog);
  if (!status_or.ok()) {
    *err = status_or.status().message();
    return false;
  }
  *map = std::move(status_or.value());
  return true;
}

std::string UnparseQueryParameterFlag(const ParameterValueMap& map) {
  return absl::StrJoin(map, ";", [](std::string* out, const auto& param) {
    absl::StrAppend(out, ToIdentifierLiteral(param.first), "=",
                    param.second.GetSQL());
  });
}
}  // namespace googlesql::internal
