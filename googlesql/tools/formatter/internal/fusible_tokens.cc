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

#include "googlesql/tools/formatter/internal/fusible_tokens.h"

#include <ostream>
#include <type_traits>
#include <vector>

#include "googlesql/tools/formatter/internal/token.h"
#include "absl/container/flat_hash_map.h"
#include "googlesql/base/check.h"
#include "absl/strings/match.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/str_split.h"
#include "absl/strings/string_view.h"
#include "absl/strings/strip.h"
#include "absl/types/span.h"

namespace googlesql::formatter::internal {
namespace {

// Recursive function that searches for a longest fusible sequence in `tokens`
// and returns corresponding match vector.
// `current_match` contains the required context: the last matched token index
// and the last matched FusibleGroup.
FusibleMatchVector FindLongestFusibleMatch(
    const std::vector<Token*>& tokens,
    const FusibleMatchVector& current_match) {
  const FusibleGroup& fusible_group = current_match.CurrentFusibleGroup();
  if (fusible_group.next_tokens.empty()) {
    // We reached the end of a fusible group - nothing else can be fused.
    return current_match;
  }
  int next_token = current_match.EndPosition() + 1;
  // Consume inline comments and query hints in the middle of a match.
  if (!current_match.Empty() && next_token < tokens.size()) {
    if (tokens[next_token]->GetKeyword() == "@") {
      if (next_token + 1 < tokens.size() && tokens[next_token + 1]->IsValue()) {
        // A query hint like "@number" - for number of shards.
        next_token += 2;
      } else if (next_token + 3 < tokens.size() &&
                 tokens[next_token + 1]->GetKeyword() == "{") {
        // A query hint like "@{hint=value}"
        int closing_curly_brace = next_token + 2;
        while (closing_curly_brace < tokens.size() &&
               tokens[closing_curly_brace]->GetKeyword() != "}") {
          closing_curly_brace++;
        }
        if (closing_curly_brace + 1 < tokens.size()) {
          next_token = closing_curly_brace + 1;
        }
      }
    }
    while (next_token < tokens.size() &&
           tokens[next_token]->IsInlineComment()) {
      next_token++;
    }
  }
  if (next_token == tokens.size() || tokens[next_token]->IsComment()) {
    // We reached the last token, or a stand-alone comment which cannot be a
    // part of a fusible group.
    return current_match;
  }

  int max_token = current_match.EndPosition();
  auto best_match = FusibleMatchVector::EmptyMatch(0);
  int match_pos;
  for (const auto& [fused_keyword, next_groups] : fusible_group.next_tokens) {
    for (const auto& next_group : next_groups) {
      match_pos = next_token;
      if (fused_keyword == FusibleTokens::kIdentifier) {
        // Current token should be a valid identifier start.
        if (!tokens[match_pos]->MayBeStartOfIdentifier()) {
          continue;
        }
        // Catch further parts of the identifier.
        while (match_pos + 1 < tokens.size() &&
               tokens[match_pos + 1]->MayBeIdentifierContinuation(
                   *tokens[match_pos])) {
          ++match_pos;
        }
      } else if (fused_keyword == FusibleTokens::kAnyWord) {
        if (!tokens[match_pos]->Is(Token::Type::INVALID_TOKEN) &&
            !tokens[match_pos]->IsNonPunctuationKeyword() &&
            !tokens[match_pos]->IsIdentifier()) {
          // kAny should match any identifier or alphabetic keyword (punctuation
          // like "(" are also keywords) - no match.
          continue;
        }
      } else if (fused_keyword == FusibleTokens::kString) {
        if (!tokens[match_pos]->IsStringLiteral()) {
          continue;
        }
      } else if (tokens[match_pos]->GetKeyword() != fused_keyword ||
                 (next_group.type != Token::Type::UNKNOWN &&
                  !tokens[match_pos]->Is(next_group.type)) ||
                 tokens[match_pos]->IsOneOf(
                     {Token::Type::KEYWORD_AS_IDENTIFIER_FRAGMENT,
                      Token::Type::COMPLEX_TOKEN_CONTINUATION}) ||
                 tokens[match_pos]->IsCaseExprKeyword()) {
        // No match.
        continue;
      }

      FusibleMatchVector match = FindLongestFusibleMatch(
          tokens, current_match.Append(next_group, match_pos));
      if (match.EndPosition() > max_token ||
          // If match length is equal, prefer the match that ends on a specific
          // keyword or identifier.
          (match.EndPosition() == max_token && !match.Empty() &&
           match.CurrentFusibleGroup().token != FusibleTokens::kAnyWord)) {
        best_match = match;
        max_token = match.EndPosition();
      }
    }
  }
  return best_match.Empty() ? current_match : best_match;
}

// Marks keyword tokens that matched given fusible sequence as keywords.
// This allows judging whether a non-reserved keyword is a keyword or
// identifier.
void AnnotateMatchedTokensAsKeywords(const std::vector<Token*>& tokens,
                                     const FusibleMatchVector& match_vector) {
  if (match_vector.MatchSize() < 2) {
    // Annotate as keywords only if the match contains at least 2 tokens.
    // Otherwise, this will annotate single-standing keywords, that appear to be
    // starting a fusing group. E.g., a match could be a single "SHOW", which
    // might act as an identifier.
    return;
  }
  FusibleMatchVector::const_iterator match_it = match_vector.Begin();
  FusibleMatchVector::const_iterator last_keyword_to_mark =
      --match_vector.End();
  // Remember if the last match says the whole group start has to be marked as
  // certain type.
  const Token::Type mark_as = last_keyword_to_mark->group->mark_as;
  // The trailing match for kAny may be not a keyword; kIdentifier match is
  // also not a keyword - skip those.
  while (last_keyword_to_mark != match_it &&
         (last_keyword_to_mark->group->token == FusibleTokens::kAnyWord ||
          last_keyword_to_mark->group->token == FusibleTokens::kIdentifier)) {
    --last_keyword_to_mark;
  }
  for (int i = match_vector.StartIndex();
       i <= last_keyword_to_mark->end_position; ++i) {
    // Consume inline comments.
    if (tokens[i]->IsComment()) {
      continue;
    }
    if (match_it->group->token == FusibleTokens::kIdentifier) {
      // Skip all tokens which appear to be a part of identifier.
      i = match_it->end_position;
    } else {
      // Mark keywords as keywords.
      if (tokens[i]->IsKeyword()) {
        tokens[i]->MarkUsedAsKeyword();
        if (mark_as != Token::Type::UNKNOWN &&
            match_it == match_vector.Begin()) {
          // Mark the first keyword in the match.
          tokens[i]->SetType(mark_as);
        }
      }
    }
    if (match_it == last_keyword_to_mark) {
      break;
    }
    ++match_it;
  }
}

// Helper function for `ParseFusibleTokens` that recursively parses the fusible
// pattern defined in `FusibleTokens::t`.
// `pattern` - is the entire string coming from `FusibleTokens::t` (used for
// error reporting only).
// `pattern_part` - is the current part of the pattern to parse (can be a single
// token or a one-of group).
// `end` - is the end of the pattern parts.
// `tokens` - is the current list of tokens that were parsed so far.
// `result` - collects the list of all token sequences produced by the pattern.
void ParseFusibleTokensRecursive(
    absl::string_view pattern,
    std::vector<absl::string_view>::const_iterator pattern_part,
    std::vector<absl::string_view>::const_iterator end,
    std::vector<absl::string_view>& tokens,
    std::vector<std::vector<absl::string_view>>& result) {
  if (pattern_part == end) {
    result.push_back(tokens);
    return;
  }

  absl::string_view token = *pattern_part;
  if (absl::EndsWith(token, "?")) {
    token = absl::StripSuffix(token, "?");
    // This is an optional part: create a token sequence without it.
    ParseFusibleTokensRecursive(pattern, (pattern_part + 1), end, tokens,
                                result);
  }
  if (absl::StartsWith(token, "[") && token.size() > 1) {
    ABSL_CHECK(absl::EndsWith(token, "]"))
        << "Syntax error in fusible tokens \"" << pattern
        << "\": missing closing square bracket (or a space inside [brakets]).\n"
        << "Expected format: \"TOKEN OPTIONAL? [ONE|OF|THESE] "
           "[OPTIONAL|ONE|OF]?\"";
    std::vector<absl::string_view> one_of = absl::StrSplit(
        absl::StripPrefix(absl::StripSuffix(token, "]"), "["), '|');
    for (const auto& t : one_of) {
      tokens.push_back(t);
      ParseFusibleTokensRecursive(pattern, (pattern_part + 1), end, tokens,
                                  result);
      tokens.pop_back();
    }
  } else {
    // Single token (no square brackets).
    tokens.push_back(token);
    ParseFusibleTokensRecursive(pattern, (pattern_part + 1), end, tokens,
                                result);
    tokens.pop_back();
  }
}

}  // namespace

const std::vector<FusibleTokens>* GetFusibleTokens() {
  // Note that partial matches are allowed by default; for instance "CROSS JOIN"
  // is a fusible group of length 2, since it matches the first 2 tokens of
  // "CROSS <W> JOIN". "<W>" matches any keyword, so "CROSS" plus any subsequent
  // token will return a fusible group of length at least 2.
  // See documentation of FusibleTokens::t for syntax of token pattern.
  const auto kTopLevel = Token::Type::TOP_LEVEL_KEYWORD;
  const auto kDdlKeyword = Token::Type::DDL_KEYWORD;
  static const auto* fusible_tokens = new std::vector<FusibleTokens>({
      // (broken link) start
      {.t = "ADD <W> IF EXISTS <W>", .mark_as = kTopLevel},
      {.t = "ADD <W> KEY", .mark_as = kTopLevel},
      // ADD ABSL_CHECK would be normally followed by an identifier, but
      // it is enough to fuse a single token only and if it was a part of
      // multi-token identifier, IsPartOfSameChunk will handle it.
      {.t = "ADD [ABSL_CHECK|COLUMN|CONSTRAINT] <W>", .mark_as = kTopLevel},
      {.t = "ADD [FILTER|INDEX]", .mark_as = kTopLevel},
      {.t = "AFTER <ID>", .start_with = kDdlKeyword},
      {.t = "ALL PRIVILEGES"},
      {.t = "ALTER <W> FUNCTION <W>"},
      {.t = "ALTER <W> FUNCTION IF EXISTS <W>"},
      {.t = "ALTER <W> IF EXISTS <W>", .mark_as = kTopLevel},
      {.t = "ALTER ALL ROW ACCESS? POLICIES"},
      {.t = "ALTER MATERIALIZED VIEW <W>"},
      {.t = "ALTER MATERIALIZED VIEW IF EXISTS <W>"},
      {.t = "ALTER ROW ACCESS? POLICY IF EXISTS"},
      {.t = "ALTER [COLUMN|CONSTRAINT|MODEL|PROCEDURE|ROUTINE|INDEX] <W>",
       .mark_as = kTopLevel},
      {.t = "ALTER [DATABASE|FUNCTION|SCHEMA|TABLE|VIEW] <W>"},
      {.t = "ANY [TABLE|TYPE]"},
      {.t = "CLONE DATA INTO <W>"},
      {.t = "CLUSTER BY"},
      {.t = "CONSTRAINT <W>", .start_with = kDdlKeyword},
      {.t = "COUNT ( * )"},
      // 2nd token can be TEMP, TEMPORARY, PRIVATE, PUBLIC.  Even if it is
      // misspelled (and not valid SQL), treat as a fused group for
      // simplicity.
      {.t = "CREATE <W> CONSTANT <ID> ="},
      {.t = "CREATE <W> CONSTANT IF NOT EXISTS <ID> ="},
      {.t = "CREATE <W> EXTERNAL TABLE <W>"},
      {.t = "CREATE <W> EXTERNAL TABLE IF NOT EXISTS <W>"},
      {.t = "CREATE <W> IF NOT EXISTS <W>"},
      {.t = "CREATE <W> [AGGREGATE|TABLE] FUNCTION <W>"},
      {.t = "CREATE <W> [AGGREGATE|TABLE] FUNCTION IF NOT EXISTS<W>"},
      {.t = "CREATE <W> [FUNCTION|MODEL|TABLE|VIEW] <W>"},
      {.t = "CREATE <W> [FUNCTION|MODEL|TABLE|VIEW] IF NOT EXISTS <W>"},
      {.t = "CREATE CONSTANT <ID> ="},
      {.t = "CREATE CONSTANT IF NOT EXISTS <ID> ="},
      {.t = "CREATE OR REPLACE <W> CONSTANT <ID> ="},
      {.t = "CREATE OR REPLACE <W> EXTERNAL TABLE <W>"},
      {.t = "CREATE OR REPLACE <W> EXTERNAL TABLE IF NOT EXISTS <W>"},
      {.t = "CREATE OR REPLACE <W> [AGGREGATE|TABLE] FUNCTION <W>"},
      {.t = "CREATE OR REPLACE <W> [FUNCTION|MODEL|TABLE|VIEW] <W>"},
      {.t = "CREATE OR REPLACE <W> [TABLE|VIEW] IF NOT EXISTS <W>"},
      {.t = "CREATE OR REPLACE CONSTANT <ID> ="},
      {.t = "CREATE OR REPLACE ROW ACCESS? POLICY IF NOT EXISTS"},
      {.t = "CREATE OR REPLACE [FUNCTION|MODEL|TABLE|TYPE|VIEW] <W>"},
      {.t = "CREATE ROLE"},
      {.t = "CREATE ROW ACCESS? POLICY IF NOT EXISTS"},
      {.t = "CREATE [DATABASE|FUNCTION|MODEL|TABLE|TYPE|VIEW] <W>"},
      {.t = "CREATE [DATABASE|MODEL] IF NOT EXISTS <W>"},
      {.t = "CROSS <W> JOIN", .mark_as = kTopLevel},
      {.t = "CROSS JOIN", .mark_as = kTopLevel},
      {.t = "CURRENT ROW"},
      {.t = "DEFAULT <W>", .start_with = kDdlKeyword},
      {.t = "DEFAULT NULL", .start_with = kDdlKeyword},
      {.t = "DEFINE MACRO <W>"},
      {.t = "DEFINE TABLE <ID>"},
      {.t = "DELETE FROM <W>"},
      {.t = "DESC TABLE <ID>"},
      {.t = "DESCRIBE TABLE <ID>"},
      {.t = "DROP <W> IF EXISTS <W>", .mark_as = kTopLevel},
      {.t = "DROP <W> KEY <W>", .mark_as = kTopLevel},
      {.t = "DROP ALL ROW ACCESS? POLICIES"},
      {.t = "DROP INDEX", .mark_as = kTopLevel},
      {.t = "DROP ROW ACCESS? POLICY IF EXISTS"},
      {.t = "DROP [COLUMN|CONSTRAINT|TABLE|VIEW] <W>", .mark_as = kTopLevel},
      {.t = "EXPORT DATA"},
      {.t = "FILL USING <W>", .start_with = kDdlKeyword},
      {.t = "FOLLOWING <ID>", .start_with = kDdlKeyword},
      {.t = "FOREIGN KEY"},
      {.t = "FULL <W> JOIN", .mark_as = kTopLevel},
      {.t = "FULL JOIN", .mark_as = kTopLevel},
      {.t = "FULL OUTER <W> JOIN", .mark_as = kTopLevel},
      {.t = "GENERATED AS", .start_with = kDdlKeyword},
      {.t = "GRANT TO"},
      {.t = "GROUP AND ORDER BY", .full_match_only = true},
      {.t = "GROUP BY"},
      {.t = "HASH JOIN", .mark_as = kTopLevel},
      {.t = "HAVING MAX"},
      {.t = "HAVING MIN"},
      {.t = "IGNORE NULLS"},
      {.t = "IMPORT [MODULE|PROTO]", .start_with = kTopLevel},
      {.t = "IN UNNEST"},
      {.t = "INNER <W> JOIN", .mark_as = kTopLevel},
      {.t = "INNER JOIN", .mark_as = kTopLevel},
      {.t = "INSERT INTO <ID>"},
      {.t = "INSERT OR? [IGNORE|REPLACE|UPDATE] INTO? <ID>"},
      {.t = "INSERT VALUES"},
      {.t = "IS DISTINCT FROM", .full_match_only = true},
      {.t = "IS NOT DISTINCT FROM", .full_match_only = true},
      {.t = "IS NOT? [FALSE|NULL|TRUE]"},
      {.t = "LEFT <W> JOIN", .mark_as = kTopLevel},
      {.t = "LEFT JOIN", .mark_as = kTopLevel},
      {.t = "LEFT OUTER <W> JOIN", .mark_as = kTopLevel},
      {.t = "LOOKUP JOIN", .mark_as = kTopLevel},
      {.t = "NATURAL CROSS <W> JOIN", .mark_as = kTopLevel},
      {.t = "NATURAL CROSS JOIN", .mark_as = kTopLevel},
      {.t = "NATURAL FULL <W> JOIN", .mark_as = kTopLevel},
      {.t = "NATURAL FULL JOIN", .mark_as = kTopLevel},
      {.t = "NATURAL FULL OUTER <W> JOIN", .mark_as = kTopLevel},
      {.t = "NATURAL HASH JOIN", .mark_as = kTopLevel},
      {.t = "NATURAL INNER <W> JOIN", .mark_as = kTopLevel},
      {.t = "NATURAL INNER JOIN", .mark_as = kTopLevel},
      {.t = "NATURAL LEFT <W> JOIN", .mark_as = kTopLevel},
      {.t = "NATURAL LEFT JOIN", .mark_as = kTopLevel},
      {.t = "NATURAL LEFT OUTER <W> JOIN", .mark_as = kTopLevel},
      {.t = "NATURAL LOOKUP JOIN", .mark_as = kTopLevel},
      {.t = "NATURAL RIGHT <W> JOIN", .mark_as = kTopLevel},
      {.t = "NATURAL RIGHT JOIN", .mark_as = kTopLevel},
      {.t = "NATURAL RIGHT OUTER <W> JOIN", .mark_as = kTopLevel},
      {.t = "NEW <W>"},
      {.t = "NOT BETWEEN"},
      {.t = "NOT DETERMINISTIC", .mark_as = kTopLevel},
      {.t = "NOT ENFORCED", .mark_as = kDdlKeyword},
      {.t = "NOT IN UNNEST"},
      {.t = "NOT LIKE"},
      {.t = "ON [TABLE|VIEW] <W>"},
      {.t = "ORDER BY"},
      {.t = "PARTITION BY"},
      {.t = "PRECEDING <ID>", .start_with = kDdlKeyword},
      {.t = "PRIMARY KEY"},
      {.t = "REFERENCES <ID>", .start_with = kDdlKeyword},
      {.t = "RENAME TO <W>", .mark_as = kTopLevel},
      {.t = "REPLACE FILTER", .mark_as = kTopLevel},
      {.t = "RESPECT NULLS"},
      {.t = "REVOKE FROM", .mark_as = kTopLevel},
      {.t = "RIGHT <W> JOIN", .mark_as = kTopLevel},
      {.t = "RIGHT JOIN", .mark_as = kTopLevel},
      {.t = "RIGHT OUTER <W> JOIN", .mark_as = kTopLevel},
      {.t = "SELECT * FROM UNNEST (", .full_match_only = true},
      {.t = "SELECT [ALL|DISTINCT]? AS <W>"},
      {.t = "SELECT [ALL|DISTINCT]? AS VALUE"},
      {.t = "SET <ID> =",
       .start_with = Token::Type::SET_STATEMENT_START,
       .full_match_only = true},
      {.t = "SET AS <W>"},
      {.t = "SET DATA TYPE <W>"},
      {.t = "SET OPTIONS"},
      {.t = "SHOW MATERIALIZED VIEWS"},
      {.t = "SQL SECURITY DEFINER"},
      {.t = "SQL SECURITY INVOKER"},
      {.t = "TABLE <ID>"},
      {.t = "UNBOUNDED [FOLLOWING|PRECEDING]"},
      {.t = "USE DATABASE <W>"},
      {.t = "WHEN MATCHED"},
      {.t = "WHEN NOT MATCHED BY SOURCE"},
      {.t = "WITH ANONYMIZATION OPTIONS (", .full_match_only = true},
      {.t = "WITH RECURSIVE"},
      {.t = "[ <ID> =", .mark_as = kTopLevel, .full_match_only = true},
      {.t = "[ DEFAULT <ID> =", .mark_as = kTopLevel, .full_match_only = true},
      // "INNER OUTER UNION ALL" is invalid SQL, but formatter allows it for
      // simplicity of the pattern.
      {.t = "[FULL|INNER|LEFT]? OUTER? [UNION|EXCEPT|INTERSECT] [ALL|DISTINCT] "
            "STRICT? BY NAME ON?",
       .mark_as = Token::Type::SET_OPERATOR_START},
      {.t = "[FULL|INNER|LEFT]? OUTER? [UNION|EXCEPT|INTERSECT] [ALL|DISTINCT] "
            "STRICT? CORRESPONDING BY?",
       .mark_as = Token::Type::SET_OPERATOR_START},
      // Normally, this sequence could be already covered by any of the previous
      // patterns, but 'mark_as' feature kicks in only when there is a full
      // match of all non-optional tokens in the pattern.
      {.t = "[FULL|INNER|LEFT]? OUTER? [UNION|EXCEPT|INTERSECT] [ALL|DISTINCT]",
       .mark_as = Token::Type::SET_OPERATOR_START},
      // (broken link) end
  });
  return fusible_tokens;
}

const FusibleGroup* GetFusibleGroups() {
  static const FusibleGroup* fusible_groups =
      FusibleGroupsFromTokens(*GetFusibleTokens());
  return fusible_groups;
}

std::vector<std::vector<absl::string_view>> ParseFusibleTokens(
    absl::string_view pattern) {
  std::vector<std::vector<absl::string_view>> result;
  std::vector<absl::string_view> parts = absl::StrSplit(pattern, ' ');
  std::vector<absl::string_view> tokens;
  tokens.reserve(parts.size());
  ParseFusibleTokensRecursive(pattern, parts.begin(), parts.end(), tokens,
                              result);
  return result;
}

const FusibleGroup* FusibleGroupsFromTokens(
    absl::Span<const FusibleTokens> fusible_tokens) {
  auto* root = new FusibleGroup();
  root->token = FusibleGroup::kRoot;
  for (const auto& tokens : fusible_tokens) {
    for (const auto& one_sequence : ParseFusibleTokens(tokens.t)) {
      auto* current_group = root;
      for (const auto& token : one_sequence) {
        std::vector<FusibleGroup>& fused_groups =
            current_group->next_tokens[token];
        FusibleGroup* fused_group = nullptr;
        for (auto& g : fused_groups) {
          if (g.type == tokens.start_with) {
            fused_group = &g;
            break;
          }
        }
        if (fused_group == nullptr) {
          fused_groups.push_back(FusibleGroup{.token = token});
          fused_group = &fused_groups.back();
        }
        fused_group->full_match_only = tokens.full_match_only;
        if (current_group == root) {
          fused_group->type = tokens.start_with;
        }
        current_group = fused_group;
      }
      current_group->mark_as = tokens.mark_as;
    }
  }

  return root;
}

bool operator==(const FusibleTokens& a, const FusibleTokens& b) {
  return a.t == b.t && a.mark_as == b.mark_as && a.start_with == b.start_with;
}

bool operator<(const FusibleTokens& a, const FusibleTokens& b) {
  if (a.t == b.t) {
    if (a.start_with == b.start_with) {
      return a.mark_as < b.mark_as;
    }
    return a.start_with < b.start_with;
  }
  return a.t < b.t;
}

std::ostream& operator<<(std::ostream& os, const FusibleTokens& tokens) {
  os << "{\"" << tokens.t << "\"";
  if (tokens.mark_as != Token::Type::UNKNOWN) {
    os << absl::StrCat(" mark_as=", tokens.mark_as);
  }
  if (tokens.start_with != Token::Type::UNKNOWN) {
    os << absl::StrCat(" start_with=", tokens.start_with);
  }
  os << "}";
  return os;
}

FusibleMatchVector FusibleMatchVector::EmptyMatch(int start_index) {
  FusibleMatchVector match;
  match.push_back(FusibleMatch{.group = GetFusibleGroups(),
                               .end_position = start_index - 1});
  return match;
}

FusibleMatchVector FusibleMatchVector::Append(const FusibleGroup& matched_group,
                                              int end_position) const {
  FusibleMatchVector result = *this;
  result.push_back(
      FusibleMatch{.group = &matched_group, .end_position = end_position});
  return result;
}

bool FusibleMatchVector::Empty() const { return size() <= 1; }

int FusibleMatchVector::StartIndex() const { return front().end_position + 1; }

int FusibleMatchVector::EndPosition() const { return back().end_position; }

int FusibleMatchVector::MatchSize() const {
  return static_cast<int>(size()) - 1;
}

void FusibleMatchVector::PopBack() { pop_back(); }

const FusibleGroup& FusibleMatchVector::CurrentFusibleGroup() const {
  return *back().group;
}

FusibleMatchVector::const_iterator FusibleMatchVector::Begin() const {
  return ++cbegin();
}

FusibleMatchVector::const_iterator FusibleMatchVector::End() const {
  return cend();
}

int FindMaxFusibleTokenIndex(const std::vector<Token*>& tokens,
                             int start_index) {
  auto match = FusibleMatchVector::EmptyMatch(start_index);
  match = FindLongestFusibleMatch(tokens, match);
  if (match.Empty()) {
    return start_index;
  }

  // If match ends on a wildcard match (`kAnyWord`), which is in the middle of a
  // fusible sequence - discard the last match. This is done to avoid grouping
  // "LEFT Foo", which is a partial match for "LEFT <kAny> JOIN".
  if (match.CurrentFusibleGroup().token == FusibleTokens::kAnyWord &&
      !match.CurrentFusibleGroup().next_tokens.empty()) {
    match.PopBack();
  }
  // If only a full match should be accepted, and the match ends in the middle
  // of a fusible sequence, the match is discarded.
  if (match.CurrentFusibleGroup().full_match_only) {
    if (!match.CurrentFusibleGroup().next_tokens.empty()) {
      return start_index;
    }
  }
  if (match.Empty()) {
    return start_index;
  }

  AnnotateMatchedTokensAsKeywords(tokens, match);

  return match.EndPosition();
}

}  // namespace googlesql::formatter::internal
