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

#ifndef GOOGLESQL_PARSER_DEIDENTIFY_H_
#define GOOGLESQL_PARSER_DEIDENTIFY_H_

#include <set>
#include <string>

#include "googlesql/parser/ast_node_kind.h"
#include "googlesql/parser/parser.h"
#include "googlesql/public/language_options.h"
#include "absl/container/flat_hash_map.h"
#include "absl/status/statusor.h"
#include "absl/strings/str_format.h"
#include "absl/strings/str_join.h"
#include "absl/strings/string_view.h"

namespace googlesql {
namespace parser {

// Result from deidentification mapping.  Includes the deidentified SQL along
// with a map of anonymized identifiers and literals that can be used to rebuild
// an equivalent SQL statement to the input.
struct DeidentificationResult {
  absl::flat_hash_map<std::string, std::string> remappings;
  std::string deidentified_sql;

  template <typename Sink>
  friend void AbslStringify(Sink& sink, const DeidentificationResult& res) {
    sink.Append(absl::StrFormat(
        "SQL: %s\n%s\n", res.deidentified_sql,
        absl::StrJoin(res.remappings.begin(), res.remappings.end(), "\n",
                      absl::PairFormatter(absl::AlphaNumFormatter(), ": ",
                                          absl::StreamFormatter()))));
  }
};

// Return cleaned SQL with comments stripped, all identifiers relabelled
// consistently starting from A and then literals replaced by ? like parameters.
absl::StatusOr<std::string> DeidentifySQLIdentifiersAndLiterals(
    absl::string_view input,
    const ParserOptions& parser_options =
        ParserOptions(LanguageOptions::MaximumFeatures()));

// Return cleaned SQL with comments stripped, identifiers and literals
// relabelled based on the provided set of kinds.  Updated nodes are labeled
// consistently starting from A, avoiding any keywords.
// The deidentified_kinds parameter changes any matching node to a zero,
// redacted or '?' value like parameters.  The remapped_kinds will replace each
// matching identifier or literal node with a label and return the mapping in
// the result.
absl::StatusOr<DeidentificationResult> DeidentifySQLWithMapping(
    absl::string_view input, std::set<ASTNodeKind> deidentified_kinds,
    std::set<ASTNodeKind> remapped_kinds,
    const ParserOptions& parser_options =
        ParserOptions(LanguageOptions::MaximumFeatures()));
}  // namespace parser
}  // namespace googlesql

#endif  // GOOGLESQL_PARSER_DEIDENTIFY_H_
