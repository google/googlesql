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

#ifndef GOOGLESQL_TOOLS_FORMATTER_INTERNAL_FUSIBLE_TOKENS_H_
#define GOOGLESQL_TOOLS_FORMATTER_INTERNAL_FUSIBLE_TOKENS_H_

#include <ostream>
#include <vector>

#include "googlesql/tools/formatter/internal/token.h"
#include "absl/container/flat_hash_map.h"
#include "absl/strings/string_view.h"
#include "absl/types/span.h"

namespace googlesql::formatter::internal {

// This file contains helper functions that identify fusible tokens.
// Fusible tokens are known combinations of SQL keywords that should never be
// split into multiple lines by the formatter. For instance "UNION ALL",
// "ORDER BY", "LEFT OUTER JOIN". These tokens are "fused" into a single Chunk,
// thus formatter never considers adding a line break between them.
//
// Most of the functions are exposed in this header for testing purposes only.
// The API used by the rest of the formatter code is FindMaxFusibleTokenIndex.
//
// Note that formatter also fuses other token types into a single chunk, see
// `IsPartOfSameChunk` in chunk.cc.

// FusibleTokens stores the sequence of SQL keywords as a vector. This gives a
// human-friendly way to define all possible fusible sequences. These sequences
// are converted into token trees to improve matching performance.
struct FusibleTokens {
  // Represents a match for a single keyword or identifier token, which is a
  // word (punctuation characters like (,.) are considered by tokenizer as
  // keywords).
  static constexpr char kAnyWord[] = "<W>";
  // kIdentifier can match multi-token identifiers, e.g. 'namespace.Foo'.
  // To be used only if an identifier is in the middle of a fusible group. If
  // the token is in the end, it is OK to use any token match (""). Further
  // parts of the identifier will be appended later by the default logic in
  // IsPartOfSameChunk.
  static constexpr char kIdentifier[] = "<ID>";
  // kString matches any string literal, e.g. 'a string'.
  static constexpr char kString[] = "'S'";

  // Contains the list of tokens separated by spaces. The name is short to make
  // initialization list less verbose. The sequence may contain optional tokens
  // (end with '?') and one-of groups ( several tokens in square brackets,
  // separated by '|'). For example:
  // "CREATE [PUBLIC|PRIVATE] FUNCTION" is the same as two sequences:
  // "CREATE PUBLIC FUNCTION" and "CREATE PRIVATE FUNCTION"
  // "CREATE TEMP? TABLE" == "CREATE TABLE" and "CREATE TEMP TABLE"
  // One-off group can be also optional:
  // "CREATE [TEMP|TEMPORARY]? TABLE" == "CREATE TABLE", "CREATE TEMP TABLE",
  // "CREATE TEMPORARY TABLE".
  absl::string_view t;
  // If set to anything but UNKNOWN, tells that these tokens can be fused only
  // if the first token is of the specified type.
  Token::Type start_with = Token::Type::UNKNOWN;
  // If set to anything but UNKNOWN, the first token in matched sequence will be
  // marked as the specified type.
  Token::Type mark_as = Token::Type::UNKNOWN;
  // If set to true, only full matches of the list of tokens will be considered
  // fusible. Otherwise, partial matches will also be fusible.
  bool full_match_only = false;

  // Allow googletest matchers to compare and print FusibleTokens.
  friend bool operator==(const FusibleTokens& a, const FusibleTokens& b);
  friend std::ostream& operator<<(std::ostream& os,
                                  const FusibleTokens& tokens);
  // Support sorting the tokens.
  friend bool operator<(const FusibleTokens& a, const FusibleTokens& b);
};

// Stores fusible tokens in a tree structure. For instance, the tree:
// "CREATE"
//  |
//  +- "FUNCTION"
//  |
//  +- "TABLE"
//
// encodes two fusible groups: {"CREATE", "FUNCTION"} and {"CREATE", "TABLE"}.
struct FusibleGroup {
  // Used to mark an empty root for the tree. Root is used to group together
  // multiple disjoint fusible groups into a single tree.
  static constexpr char kRoot[] = "<root>";

  // Current token matcher (a keyword, kRoot, kAnyWord or kIdentifier).
  absl::string_view token;

  // If not set to UNKNOWN, the token must be of the specified type.
  Token::Type type = Token::Type::UNKNOWN;
  // If not set to UNKNOWN, the first token in the matched sequence will be
  // marked with the specified type.
  // Only the last FusibleGroup in the chain should use this field, so
  // that "LEFT OUTER JOIN" is marked as top level, but "LEFT" is not.
  Token::Type mark_as = Token::Type::UNKNOWN;
  // If set to true, only full matches of the group of tokens will be considered
  // fusible. Otherwise, partial matches will also be fusible.
  bool full_match_only = false;
  // Next tokens that continue the current group.
  absl::flat_hash_map<absl::string_view, std::vector<FusibleGroup>> next_tokens;
};

// Stores a match location for the token that starts the given fusible group.
struct FusibleMatch {
  // Pointer to the group corresponding to the matched token.
  const FusibleGroup* group;
  // Position of the last token that matches group's token. In most cases, the
  // match is a single token, but FusibleGroup::kIdentifier can match multiple
  // tokens.
  int end_position;
};

// A sequence of fusible token matches. The class hides implementation detail:
// there is always an empty match at the start of the array that corresponds to
// FusibleGroup's root.
class FusibleMatchVector : std::vector<FusibleMatch> {
 public:
  typedef std::vector<FusibleMatch>::const_iterator const_iterator;

  // Creates a new empty match with the provided starting index.
  static FusibleMatchVector EmptyMatch(int start_index);

  // Returns a copy of the current match vector with additional match appended.
  FusibleMatchVector Append(const FusibleGroup& matched_group,
                            int end_position) const;
  // Returns true if the current match vector doesn't contain any real matches.
  bool Empty() const;
  // Returns the index of the first matched token.
  int StartIndex() const;
  // Returns the index of the last matched token.
  int EndPosition() const;
  // Returns the amount of matches in this vector.
  int MatchSize() const;
  // Returns the last matched fusible group recorded in this vector.
  const FusibleGroup& CurrentFusibleGroup() const;
  // Removes the last match from the current match vector.
  void PopBack();
  // Const iterator over real matches.
  const_iterator Begin() const;
  const_iterator End() const;

 private:
  FusibleMatchVector() = default;
};

// Wrapper for a static list of all possible fusible token groups.
const std::vector<FusibleTokens>* GetFusibleTokens();
// Wrapper for a static tree of fusible groups.
const FusibleGroup* GetFusibleGroups();

// Parses a token pattern string into vector of all possible token sequences.
// For instance, the pattern "CREATE [PUBLIC|PRIVATE] FUNCTION" results in two
// sequences:
// {"CREATE", "PUBLIC", "FUNCTION"} and
// {"CREATE", "PRIVATE", "FUNCTION"}.
std::vector<std::vector<absl::string_view>> ParseFusibleTokens(
    absl::string_view pattern);

// Converts a list of token groups into a tree.
const FusibleGroup* FusibleGroupsFromTokens(
    absl::Span<const FusibleTokens> fusible_tokens);

// Returns the maximum token position in <tokens> that can be grouped into a
// single chunk with the token at <start_index>; i.e., these tokens should be
// never split into multiple lines when formatting.
//
// Examples:
//   tokens=["CREATE", "TEMP", "FUNCTION", "Foo", "(", "a", "STRING"];
//   start_index=0
//     >> Returns 3, since the first 4 tokens should not be broken apart.
//   tokens=["CREATE", "TEMP", "FUNCTION", "Foo", "(", "a", "STRING"];
//   start_index=2
//     >> Returns 0, since "FUNCTION' does not start a fusible group.
//
// The returned integer will be a number between [start_index, tokens.size()).
int FindMaxFusibleTokenIndex(const std::vector<Token*>& tokens,
                             int start_index);

}  // namespace googlesql::formatter::internal

#endif  // GOOGLESQL_TOOLS_FORMATTER_INTERNAL_FUSIBLE_TOKENS_H_
