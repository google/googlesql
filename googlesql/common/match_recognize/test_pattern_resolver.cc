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

#include "googlesql/common/match_recognize/test_pattern_resolver.h"

#include <memory>
#include <string>
#include <variant>
#include <vector>

#include "googlesql/public/analyzer.h"
#include "googlesql/public/analyzer_options.h"
#include "googlesql/public/builtin_function_options.h"
#include "googlesql/public/options.pb.h"
#include "googlesql/public/simple_catalog.h"
#include "googlesql/public/types/type.h"
#include "googlesql/public/types/type_factory.h"
#include "googlesql/resolved_ast/resolved_ast.h"
#include "googlesql/resolved_ast/resolved_ast_enums.pb.h"
#include "absl/container/btree_set.h"
#include "absl/log/log.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/str_join.h"
#include "absl/strings/string_view.h"
#include "absl/strings/substitute.h"
#include "googlesql/base/case.h"
#include "re2/re2.h"
#include "googlesql/base/status_macros.h"

namespace googlesql::functions::match_recognize {

// Returns a sorted list of all symbols that appear in a pattern, with no
// duplicates.
static std::vector<std::string> ExtractSymbolsFromPattern(
    absl::string_view pattern_text) {
  absl::btree_set<std::string, googlesql_base::StringCaseLess> symbols;
  std::string at_prefix;
  std::string symbol;
  static const LazyRE2 symbol_or_query_param_pattern = {
      "(@?)([A-Za-z_][A-Za-z0-9_]*)"};
  while (RE2::FindAndConsume(&pattern_text, *symbol_or_query_param_pattern,
                             &at_prefix, &symbol)) {
    if (!at_prefix.empty()) {
      // This is a query parameter, not a pattern variable.
      continue;
    }
    symbols.insert(symbol);
  }
  return std::vector<std::string>(symbols.begin(), symbols.end());
}

TestPatternResolver::TestPatternResolver(bool log_queries)
    : type_factory_(std::make_unique<TypeFactory>()),
      catalog_(std::make_unique<SimpleCatalog>("TestPatternResolver")),
      log_queries_(log_queries) {
  catalog_->AddBuiltinFunctions(BuiltinFunctionOptions::AllReleasedFunctions());
}

absl::StatusOr<const ResolvedMatchRecognizeScan*>
TestPatternResolver::ResolvePattern(
    absl::string_view pattern_text,
    const MatchRecognizeScanGenerationOptions& options) {
  // First, extract symbol names present in the pattern expression so we can
  // build a DEFINE clause to define all of the symbols.
  std::vector<std::string> symbols = ExtractSymbolsFromPattern(pattern_text);
  if (symbols.empty()) {
    // MATCH_RECOGNIZE does not currently support patterns which don't reference
    // any symbols.
    return absl::InvalidArgumentError(
        "Pattern must refer to at least one symbol");
  }
  std::string define_clause = absl::StrJoin(
      symbols, ", ", [](std::string* out, absl::string_view symbol) {
        absl::StrAppend(out, symbol, " AS TRUE");
      });

  std::string after_match_skip_clause;
  switch (options.after_match_skip_mode) {
    case ResolvedMatchRecognizeScanEnums::END_OF_MATCH:
      after_match_skip_clause = "PAST LAST ROW";
      break;
    case ResolvedMatchRecognizeScanEnums::NEXT_ROW:
      after_match_skip_clause = "TO NEXT ROW";
      break;
    default:
      return absl::UnimplementedError(
          absl::StrCat("Unknown AfterMatchSkipMode: ",
                       ResolvedMatchRecognizeScanEnums::AfterMatchSkipMode_Name(
                           options.after_match_skip_mode)));
  }

  std::string options_clause;
  if (!options.longest_match_mode_sql.empty()) {
    options_clause = absl::Substitute("OPTIONS(use_longest_match=$0)",
                                      options.longest_match_mode_sql);
  }

  std::string query = absl::Substitute(R"sql(
    WITH t AS (SELECT * FROM UNNEST([1]) AS x)
    SELECT * FROM t
    MATCH_RECOGNIZE (
      ORDER BY x
      MEASURES MIN(x) AS min_x
      AFTER MATCH SKIP $2
      PATTERN ($0)
      DEFINE $1
      $3
    ))sql",
                                       pattern_text, define_clause,
                                       after_match_skip_clause, options_clause);
  ABSL_LOG_IF(INFO, log_queries_)
      << "Resolving generated MATCH_RECOGNIZE query: " << query;

  AnalyzerOptions analyzer_options;
  analyzer_options.set_error_message_mode(
      ErrorMessageMode::ERROR_MESSAGE_MULTI_LINE_WITH_CARET);
  analyzer_options.mutable_language()->EnableLanguageFeature(
      FEATURE_MATCH_RECOGNIZE);
  GOOGLESQL_RETURN_IF_ERROR(analyzer_options.mutable_language()->EnableReservableKeyword(
      "MATCH_RECOGNIZE"));
  if (std::holds_alternative<QueryParametersMap>(options.parameters)) {
    analyzer_options.set_parameter_mode(PARAMETER_NAMED);
    for (const auto& [name, type] :
         std::get<QueryParametersMap>(options.parameters)) {
      GOOGLESQL_RETURN_IF_ERROR(analyzer_options.AddQueryParameter(name, type));
    }
  } else if (std::holds_alternative<std::vector<const Type*>>(
                 options.parameters)) {
    analyzer_options.set_parameter_mode(PARAMETER_POSITIONAL);
    for (const Type* param_type :
         std::get<std::vector<const Type*>>(options.parameters)) {
      GOOGLESQL_RETURN_IF_ERROR(analyzer_options.AddPositionalQueryParameter(param_type));
    }
  }
  analyzer_outputs_.emplace_back();
  GOOGLESQL_RETURN_IF_ERROR(AnalyzeStatement(query, analyzer_options, catalog_.get(),
                                   type_factory_.get(),
                                   &analyzer_outputs_.back()));
  return analyzer_outputs_.back()
      ->resolved_statement()
      ->GetAs<ResolvedQueryStmt>()
      ->query()
      ->GetAs<ResolvedWithScan>()
      ->query()
      ->GetAs<ResolvedProjectScan>()
      ->input_scan()
      ->GetAs<ResolvedProjectScan>()
      ->input_scan()
      ->GetAs<ResolvedMatchRecognizeScan>();
}
}  // namespace googlesql::functions::match_recognize
