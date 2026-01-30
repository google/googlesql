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

#ifndef GOOGLESQL_COMMON_MATCH_RECOGNIZE_TEST_PATTERN_RESOLVER_H_
#define GOOGLESQL_COMMON_MATCH_RECOGNIZE_TEST_PATTERN_RESOLVER_H_

#include <memory>
#include <string>
#include <variant>
#include <vector>

#include "googlesql/public/analyzer_options.h"
#include "googlesql/public/analyzer_output.h"
#include "googlesql/public/simple_catalog.h"
#include "googlesql/public/types/type.h"
#include "googlesql/public/types/type_factory.h"
#include "googlesql/resolved_ast/resolved_ast.h"
#include "googlesql/resolved_ast/resolved_ast_enums.pb.h"
#include "absl/status/statusor.h"
#include "absl/strings/string_view.h"

namespace googlesql::functions::match_recognize {

// Options to control generation of a ResolvedMatchRecognizeScan from a pattern.
struct MatchRecognizeScanGenerationOptions {
  // Controls how the matcher behaves at the end of a match (e.g. AFTER MATCH
  // SKIP TO NEXT ROW vs. AFTER MATCH SKIP PAST LAST ROW).
  ResolvedMatchRecognizeScanEnums::AfterMatchSkipMode after_match_skip_mode =
      ResolvedMatchRecognizeScanEnums::END_OF_MATCH;

  // Specifies a SQL expression that determines whether longest-match mode is
  // enabled. This should either be a boolean literal or a query parameter.
  //
  // If empty, the generated query will not specify longest-match mode, which
  // will cause it to default to false.
  std::string longest_match_mode_sql;

  // Which type of query parameters, if any, to allow for quantifier bounds:
  //  - std::monostate => no query parameters supported (default).
  //  - QueryParametersMap => named query parameters.
  //  - std::vector<const type*> => positional query parameters.
  std::variant<std::monostate, QueryParametersMap, std::vector<const Type*>>
      parameters = std::monostate{};
};

// Helper class to allow tests to resolve MATCH_RECOGNIZE patterns into scans
// which can be fed into the pattern matching api, without needing to get bogged
// down by details, such as the syntax of the rest of the MATCH_RECOGNIZE clause
// or the SELECT query that contains it.
//
// This class is intended for tests specifically targeted at testing the pattern
// matching algorithm in isolation against various pattern elements. It is not
// intended for higher-level tests covering full evaluation of queries with
// MATCH_RECOGNIZE in them.
class TestPatternResolver {
 public:
  explicit TestPatternResolver(bool log_queries = true);

  // Consumes a MATCH_RECONGIZE pattern string and returns a
  // ResolvedMatchRecongizeScan based on a MATCH_RECOGNIZE clause that combines
  // the provided pattern with dummy DEFINE/MEASURES expressions.
  //
  // The returned object is owned by this TestPatternResolver object.
  //
  // `parameters` specifies which type of query parameters, if any, to allow for
  // quantifier bounds:
  //  - std::monostate => no query parameters supported (default).
  //  - QueryParametersMap => named query parameters.
  //  - std::vector<const type*> => positional query parameters.
  absl::StatusOr<const ResolvedMatchRecognizeScan*> ResolvePattern(
      absl::string_view pattern_text,
      const MatchRecognizeScanGenerationOptions& options = {});

 private:
  // Accumulated AnalyzerOutputs from all calls to ResolvePattern().
  std::vector<std::unique_ptr<const AnalyzerOutput>> analyzer_outputs_;

  // Shared TypeFactory for all calls to the analyzer.
  std::unique_ptr<TypeFactory> type_factory_;

  std::unique_ptr<SimpleCatalog> catalog_;

  // True to log queries generated (set to true in unit test, false in
  // benchmarks to avoid log spam).
  bool log_queries_ = true;
};
}  // namespace googlesql::functions::match_recognize

#endif  // GOOGLESQL_COMMON_MATCH_RECOGNIZE_TEST_PATTERN_RESOLVER_H_
