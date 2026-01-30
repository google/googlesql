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

#ifndef GOOGLESQL_COMMON_OPTIONS_UTILS_H_
#define GOOGLESQL_COMMON_OPTIONS_UTILS_H_

#include <algorithm>
#include <cstdint>
#include <string>
#include <utility>
#include <vector>

#include "googlesql/public/analyzer_options.h"
#include "googlesql/public/evaluator.h"
#include "googlesql/public/options.pb.h"
#include "googlesql/public/strings.h"
#include "absl/container/btree_set.h"
#include "absl/container/flat_hash_map.h"
#include "absl/container/flat_hash_set.h"
#include "absl/status/statusor.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/str_join.h"
#include "absl/strings/str_split.h"
#include "absl/strings/string_view.h"
#include "absl/types/span.h"
#include "google/protobuf/descriptor.h"
#include "googlesql/base/ret_check.h"
#include "googlesql/base/status_macros.h"

namespace googlesql::internal {

template <typename EnumT>
struct EnumOptionsEntry {
  // Canonicalized description of the input string.
  //   This has all whitespace removed, forced to upper case
  std::string description;
  absl::btree_set<EnumT> options;
};

//
// ParseEnabledAstRewrites()
//
// Helper function for use in tests and flags to parse a string representing
// the set of Rewriters to support.
//
// See ParseEnumOptionsSet for information on this format.
//
// The default list of allowed bases is
//   NONE:    Empty set
//   ALL:     Every valid enum
//   DEFAULT: Every enum marked with [rewrite_options).default_enabled = true]
//            this matches AnalyzerOptions::DefaultRewrites()
absl::StatusOr<EnumOptionsEntry<ResolvedASTRewrite>> ParseEnabledAstRewrites(
    absl::string_view options_str);

// Wrapper class for use in flags.
struct EnabledAstRewrites {
  absl::btree_set<ResolvedASTRewrite> enabled_ast_rewrites;
};

// Helper function to parse a flag using ParseEnabledAstRewrites. To Use:
// ABSL_FLAG(EnabledAstRewrites, <flag_name>, EnabledAstRewrites{},
//           "<description>");
bool AbslParseFlag(absl::string_view text, EnabledAstRewrites* p,
                   std::string* error);

std::string AbslUnparseFlag(EnabledAstRewrites p);

//
// ParseEnabledLanguageFeatures()
//
// Helper function for use in tests and flags to parse a string representing
// the set of LanguageFeature to enable.
//
// See ParseEnumOptionsSet for information on this format.
//
// The default list of allowed bases is
//   NONE:    Empty set
//   ALL:     Every valid enum; Not recommended, includes many deprecated
//            features.
//   MAXIMUM: LanguageFeatures marked `ideally_enabled`; matches
//            `LanguageOptions::EnableMaximumLanguageFeatures`.
//   DEV:     MAXIMUM plus Features marked `in_development`; matches
//            `LanguageOptions::EnableMaximumLanguageFeaturesForDevelopment`.
//
absl::StatusOr<EnumOptionsEntry<LanguageFeature>> ParseEnabledLanguageFeatures(
    absl::string_view options_str);

// Wrapper class for use in flags.
struct EnabledLanguageFeatures {
  absl::btree_set<LanguageFeature> enabled_language_features;
  static inline constexpr absl::string_view kFlagDescription =
      R"(The Language Features to enable in parser and analyzer, the format is:
        <BASE>[,+<ADDED_OPTION>][,-<REMOVED_OPTION>]...
      Where BASE is one of:
        'NONE'    : the empty set
        'ALL'     : Every valid enum; Not recommended, includes many
                      deprecated features.
        'MAXIMUM': LanguageFeatures marked `ideally_enabled`; matches
                     `LanguageOptions::EnableMaximumLanguageFeatures`.
        'DEV'    : MAXIMUM plus Features marked `in_development`; matches
                     `LanguageOptions::EnableMaximumLanguageFeaturesForDevelopment`

      values must be listed with 'FEATURE_' stripped
        Example:
          --enabled_language_features='MAXIMUM,-TABLESAMPLE,+ANONYMIZATION'
      Will enable all the ideally enabled features plus ANONYMIZATION, but excluding
      TABLESAMPLE)";
};

// Helper function to parse a flag using ParseEnabledLanguageFeatures. To Use:
// ABSL_FLAG(EnabledLanguageFeatures, <flag_name>, EnabledLanguageFeatures{},
//           "<description>");
bool AbslParseFlag(absl::string_view text, EnabledLanguageFeatures* p,
                   std::string* error);

std::string AbslUnparseFlag(EnabledLanguageFeatures p);

//
// ParseEnumOptionsSet()
//
// This allows parsing strings of the form:
// <base>[,[+-]<enum_value>...]
// to construct a set of effectively enabled proto enums from a base set with
// items added or removed.
//
// Example:
//   auto default_values = {OPT_A, OPT_B};
//   auto all_values = {OPT_A, OPT_B, OPT_C, OPT_D}
//   GOOGLESQL_ASSIGN_OR_RETURN(auto entry = ParseEnumOptionsSet<OptEnum>(
//        {{"NONE", {}}, {"ALL", all_values}, {"DEFAULT", default_values},
//        /*strip_prefix=*/"OPT_",
//        /*error_context_name=*/"Opts",
//        /*options_str*/="NONE,+A,+D"));
//
// Would return the following: {OPT_A, OPT_D}
//
//   "ALL,-A,-D" -> {OPT_B, OPT_C}
//   "DEFAULT" -> {OPT_A, OPT_B}
//   "DEFAULT,-A,+C" -> {OPT_B, OPT_C}
//
// Explicit duplicate entries are an error
//   "DEFAULT,+A,+A" -> ERROR
//   "DEFAULT,-A,-A" -> ERROR
//   "DEFAULT,+A,-A" -> ERROR
//
// However, a redundant add/remove from a base (other than ALL or NONE) is okay:
//   "DEFAULT,+A" -> {OPT_A, OPT_B}
//
// A removal from a base called 'NONE' is an error:
//   "NONE,-A" -> ERROR
//
// An addition to a base called 'ALL' is an error:
//   "ALL,+A" -> ERROR
//
// ALL and NONE are otherwise not special, if they are not in the base_factory.
//
// Matching is case insensitive, and whitespace between tokens is ignored,
// However the +/- modifier must be a direct prefix to the enum value.
//
// Empty String: Error, a <base> must be part of the string
//   "" -> ERROR
//
//
// Args:
//   base_factory: a mapping from a given base to the initial set of
//     values associated with that base.  'NONE' and 'ALL', if mapped, have
//     special error handling, see above.
//   strip_prefix: This is stripped from the front of enum name before matching
//      with options_str.
//   error_context_name: usually the conceptual 'name' of the enum, used in
//        error messages.
//   options_str: the user strings.
//
template <typename EnumT>
absl::StatusOr<EnumOptionsEntry<EnumT>> ParseEnumOptionsSet(
    const absl::flat_hash_map<absl::string_view, absl::btree_set<EnumT>>&
        base_factory,
    absl::string_view strip_prefix,
    absl::string_view error_context_name,  // e.g. 'Rewrite'
    absl::string_view options_str) {
  const google::protobuf::EnumDescriptor* enum_descriptor =
      google::protobuf::GetEnumDescriptor<EnumT>();

  std::string upper_options = absl::AsciiStrToUpper(options_str);
  absl::string_view stripped_options =
      absl::StripAsciiWhitespace(upper_options);
  std::vector<absl::string_view> tokens =
      absl::StrSplit(stripped_options, ',', absl::SkipWhitespace());

  // Cheat on error handling by forcing zero tokens to an empty string
  // which we don't expect to matching anything in the base_factory.
  absl::string_view base_token =
      tokens.empty() ? absl::string_view{} : tokens[0];
  absl::btree_set<EnumT> output_options;

  if (auto it = base_factory.find(base_token); it == base_factory.end()) {
    std::vector<absl::string_view> bases;
    for (const auto& [base, _] : base_factory) {
      bases.push_back(base);
    }
    std::sort(bases.begin(), bases.end());

    GOOGLESQL_RET_CHECK_FAIL() << error_context_name
                     << " list should always start with one of "
                     << absl::StrJoin(bases, ", ");
  } else {
    output_options = it->second;
  }

  // Handle special tokens. These help to 'lint' the input, preventing foolish
  // stuff like 'NONE,-FEATURE_X' which probably indicates an error.
  bool is_all_mode = (base_token == "ALL");
  bool is_none_mode = (base_token == "NONE");

  absl::btree_set<std::string> canonicalized_edits;
  absl::flat_hash_set<EnumT> seen_overrides;

  for (absl::string_view entry : absl::MakeSpan(tokens).subspan(1)) {
    entry = absl::StripAsciiWhitespace(entry);
    bool enable = true;
    if (absl::ConsumePrefix(&entry, "-")) {
      enable = false;
    } else if (!absl::ConsumePrefix(&entry, "+")) {
      GOOGLESQL_RET_CHECK_FAIL() << error_context_name
                       << " entries should be prefixed with '+' or '-'";
    }
    GOOGLESQL_RET_CHECK(!absl::ConsumePrefix(&entry, strip_prefix))
        << "For consistency, do not include the " << strip_prefix << " prefix.";

    const google::protobuf::EnumValueDescriptor* enum_value_descriptor =
        enum_descriptor->FindValueByName(absl::StrCat(strip_prefix, entry));
    GOOGLESQL_RET_CHECK(enum_value_descriptor != nullptr) << entry;
    EnumT enum_value = static_cast<EnumT>(enum_value_descriptor->number());
    GOOGLESQL_RET_CHECK(seen_overrides.insert(enum_value).second)
        << "Duplicate entry for " << error_context_name << ": "
        << enum_value_descriptor->name();
    if (enable) {
      GOOGLESQL_RET_CHECK(!is_all_mode) << "Attempting to add " << error_context_name
                              << ", but already started from ALL.";
      output_options.insert(enum_value);
      canonicalized_edits.insert(absl::StrCat("+", entry));
    } else {
      GOOGLESQL_RET_CHECK(!is_none_mode) << "Attempting to remove " << error_context_name
                               << ", but already started from NONE.";
      output_options.erase(enum_value);
      canonicalized_edits.insert(absl::StrCat("-", entry));
    }
  }
  std::vector<std::string> description_parts;
  description_parts.emplace_back(base_token);
  if (!canonicalized_edits.empty()) {
    description_parts.push_back(absl::StrJoin(canonicalized_edits, ","));
  }
  std::string description = absl::StrJoin(description_parts, ",");
  return EnumOptionsEntry<EnumT>{description, output_options};
}

// Returns the set of all ResolvedAST Rewrites, filtered based on input args.
// `include_in_development` controls whether rewrites with
// `in_development=true` are returned. `include_default_disabled` controls
// whether rewrites without `default_enabled=true` are returned. The special
// "INVALID" value (0) is never returned.
AnalyzerOptions::ASTRewriteSet GetRewrites(bool include_in_development,
                                           bool include_default_disabled);

// Returns all ResolvedAST Rewrites except the special "INVALID" value (0).
AnalyzerOptions::ASTRewriteSet GetAllRewrites();

// Implementations for AbslParseFlag()/AbslUnparseFlag() for wrapper classes
// which represent a list of named query parameters and values.
//
// syntax: name1=value1;name2=value2...
//  - General syntax notes:
//    - Trailing semi-colons are allowed, but ignored.
//    - Comments and whitespace are allowed anywhere (including surrounding ;
//      and "=" tokens).
//    - LanguageOptions for parsing are determined from
//      'analyzer_options.GetParserOptions()'.
//  - Parameter names:
//    - Are parsed as GoogleSQL identifier tokens.
//    - May not be empty (positional parameters are not supported).
//    - Must be enclosed in backticks if a reserved keyword, or is not a valid
//      identifier.
//    - Are converted to lowercase in the returned ParameterValueMap
//    - May not be duplicates (after lowercase conversion).
//  - Parameter values:
//    - Are parsed and evaluated as GoogleSQL expressions
//    - Analyzed using 'catalog' and 'analyzer_options'.
//    - Evaluated using the GoogleSQL evaluator.
//    - May reference previously-defined query parameters.
//
// Example usage:
//
// namespace MyNamespace {
//   struct MyParameterValueMap {
//     ParameterValueMap map;
//   };
//   bool AbslParseFlag(absl::string_view text, MyParameterValueMap* m,
//                      std::string* err) {
//      AnalyzerOptions analyzer_options = ...;
//      Catalog* catalog = ...;
//      return ParseQueryParameterFlag(text, analyzer_options, catalog, &m.map,
//                                     err);
//   }
//   std::string AbslUnparseFlag(const MyParameterValueMap& m) {
//      return UnparseQueryParameterFlag(m.map);
//   }
// }  // namespace MyNamespace
//
// ABSL_FLAG(MyParameterValueMap, parameters, {}, kQueryParameterMapHelpstring);
// $ foo ... '--parameters=p1=1;p2="abc" || "def";p3=CAST(@p1 AS STRING) || @p2'

bool ParseQueryParameterFlag(absl::string_view text,
                             const AnalyzerOptions& analyzer_options,
                             Catalog* catalog, ParameterValueMap* map,
                             std::string* err);

std::string UnparseQueryParameterFlag(const ParameterValueMap& map);

// Default helpstring for flags defined using ParseQueryParameterFlag()/
// UnparseQueryParameterFlag(). May be used as is, or annotated with scenario-
// specific detail about which language features, proto types, etc. are
// supported.
constexpr absl::string_view kQueryParameterMapHelpstring =
    "List of Query parameters, specified according to the syntax: "
    "name1=value1;name2=value2;...\n"
    "Parameter names are parsed as GoogleSQL identifiers. Parameter values are "
    "parsed as GoogleSQL expressions, and may reference previously-defined "
    "parameters.";

}  // namespace googlesql::internal

#endif  // GOOGLESQL_COMMON_OPTIONS_UTILS_H_
