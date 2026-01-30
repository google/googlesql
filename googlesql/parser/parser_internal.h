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

#ifndef GOOGLESQL_PARSER_PARSER_INTERNAL_H_
#define GOOGLESQL_PARSER_PARSER_INTERNAL_H_

#include <any>
#include <cctype>
#include <cstddef>
#include <memory>
#include <optional>
#include <queue>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

#include "googlesql/base/arena.h"
#include "googlesql/common/errors.h"
#include "googlesql/common/warning_sink.h"
#include "googlesql/parser/ast_node_factory.h"
#include "googlesql/parser/ast_node_kind.h"
#include "googlesql/parser/macros/macro_catalog.h"
#include "googlesql/parser/parse_tree.h"
#include "googlesql/parser/parser_mode.h"
#include "googlesql/parser/parser_runtime_info.h"
#include "googlesql/parser/statement_properties.h"
#include "googlesql/parser/tm_token.h"
#include "googlesql/public/id_string.h"
#include "googlesql/public/language_options.h"
#include "googlesql/public/parse_location.h"
#include "googlesql/public/strings.h"
#include "absl/base/nullability.h"
#include "googlesql/base/check.h"
#include "absl/log/log.h"
#include "absl/status/status.h"
#include "absl/strings/match.h"
#include "absl/strings/str_join.h"
#include "absl/strings/string_view.h"
#include "absl/strings/substitute.h"
#include "absl/types/span.h"
#include "googlesql/base/ret_check.h"
#include "googlesql/base/status_macros.h"

#define PARSER_LA_IS_EMPTY() next_symbol_.symbol == noToken

// Signals the token disambiguation buffer that a new statement is starting.
// The second argument indicates whether the parser lookahead buffer is
// populated or not.
#define OVERRIDE_NEXT_TOKEN_LOOKBACK(expected_token, lookback_token)           \
  do {                                                                         \
    GOOGLESQL_RETURN_IF_ERROR(OverrideNextTokenLookback(tokenizer, PARSER_LA_IS_EMPTY(), \
                                              Token::expected_token,           \
                                              Token::lookback_token));         \
  } while (0)

// Overrides the lookback token kind to be `new_token_kind` for the most
// recently returned token by the lookahead transformer. `location` is the error
// location to report when the override fails.
#define OVERRIDE_CURRENT_TOKEN_LOOKBACK(location, new_token_kind)             \
  do {                                                                        \
    GOOGLESQL_RET_CHECK(PARSER_LA_IS_EMPTY()) << "The parser lookahead buffer must be " \
                                       "empty to override the current token"; \
    GOOGLESQL_RETURN_IF_ERROR(                                                          \
        OverrideCurrentTokenLookback(tokenizer, Token::new_token_kind));      \
  } while (0)

namespace googlesql {
// Forward declarations to avoid an interface and a v-table lookup on every
// token.
namespace parser {
class LookaheadTransformer;

// These are defined here because some of our tests rely on grabbing quoted
// words in order to ensure test completeness of tokens. Indirecting these
// strings as globals here allows us to use them in generating warnings without
// interrupting those tests.
inline constexpr absl::string_view kQualify = "QUALIFY";
inline constexpr absl::string_view kGraphTable = "GRAPH_TABLE";

// Parses `input` in mode `mode`, starting at byte offset `start_byte_offset`.
// Returns the output tree in `output`, or returns an annotated error.
//
// Memory allocation:
// - Identifiers are allocated from `id_string_pool`.
// - ASTNodes are allocated from `arena`.
// - ASTNodes still need to be deleted before the memory pools are destroyed.
//   Ownership of all allocated ASTNodes except for the root output is
//   returned in `other_allocated_ast_nodes`.
// The caller should keep `id_string_pool` and `arena` alive until all the
// returned ASTNodes have been deallocated.
//
// If mode is `kNextStatementKind`, then the next statement kind is returned
// in `ast_statement_properties`, and statement level hints are returned in
// `output`. In this mode, `statement_end_byte_offset` is *not* set.
//
// If mode is kNextStatement, the byte offset past the current statement's
// closing semicolon is returned in `statement_end_byte_offset`. If the
// statement did not end in a semicolon, then `statement_end_byte_offset` is
// set to -1, and the input was guaranteed to be parsed to the end.
//
// If mode is `kStatement`, then `statement_end_byte_offset` is not set.
absl::Status ParseInternal(
    ParserMode mode, absl::string_view filename, absl::string_view input,
    int start_byte_offset, IdStringPool* id_string_pool, googlesql_base::UnsafeArena* arena,
    const LanguageOptions& language_options,
    MacroExpansionMode macro_expansion_mode,
    const macros::MacroCatalog* macro_catalog, std::unique_ptr<ASTNode>* output,
    ParserRuntimeInfo& runtime_info, WarningSink& warning_sink,
    std::vector<std::unique_ptr<ASTNode>>* other_allocated_ast_nodes,
    ASTStatementProperties* ast_statement_properties,
    int* statement_end_byte_offset);

// The internal namespace contains
namespace internal {

// Forward declarations of wrappers so that the generated parser can call the
// lookahead transformer without an interface and a v-table lookup on every
// token.
using googlesql::parser::LookaheadTransformer;
using googlesql::parser::ParserMode;

void PushParserMode(LookaheadTransformer&, ParserMode);
void PopParserMode(LookaheadTransformer&);
int GetNextToken(LookaheadTransformer&, absl::string_view*,
                 ParseLocationRange*);
absl::Status OverrideNextTokenLookback(LookaheadTransformer&, bool,
                                       parser::Token, parser::Token);
absl::Status OverrideCurrentTokenLookback(LookaheadTransformer&, parser::Token);

enum class NotKeywordPresence { kPresent, kAbsent };

enum class AllOrDistinctKeyword {
  kAll,
  kDistinct,
  kNone,
};

enum class PrecedingOrFollowingKeyword { kPreceding, kFollowing };

enum class ShiftOperator { kLeft, kRight };

enum class TableOrTableFunctionKeywords {
  kTableKeyword,
  kTableAndFunctionKeywords
};

enum class ImportType {
  kModule,
  kProto,
};

enum class IndexTypeKeywords {
  kNone,
  kSearch,
  kVector,
};

struct PivotOrUnpivotClause {
  ASTPivotClause* pivot_clause;
  ASTUnpivotClause* unpivot_clause;
  ASTAlias* alias;
};

struct ClausesFollowingFrom {
  ASTNode* where;
  ASTNode* group_by;
  ASTNode* having;
  ASTNode* qualify;
  ASTNode* window;
};

struct GeneratedOrDefaultColumnInfo {
  ASTExpression* default_expression;
  ASTGeneratedColumnInfo* generated_column_info;
};

struct BeginEndBlockOrLanguageAsCode {
  ASTScript* body;
  ASTIdentifier* language;
  ASTNode* code;
};

struct PathExpressionWithScope {
  ASTExpression* maybe_dashed_path_expression;
  bool is_temp_table;
};

struct ColumnMatchSuffix {
  ASTSetOperationColumnMatchMode* column_match_mode;
  ASTColumnList* column_list;
};

struct GroupByPreamble {
  ASTNode* hint;
  bool and_order_by;
};

struct GraphDynamicLabelProperties {
  ASTNode* dynamic_label;
  ASTNode* dynamic_properties;
};

struct CreateIndexStatementSuffix {
  ASTNode* partition_by;
  ASTNode* options_list;
  ASTNode* spanner_index_innerleaving_clause;
};

// Makes a ParseLocationRange from the start of `start` to the end of `end`.
inline ParseLocationRange MakeLocationRange(const ParseLocationRange& start,
                                            const ParseLocationRange& end) {
  return ParseLocationRange(start.start(), end.end());
}

// This node is used for temporarily aggregating together components of an
// identifier that are separated by various characters, such as slash ("/"),
// dash ("-"), and colon (":") to enable supporting table paths of the form:
// /span/nonprod-test:db.Table without any escaping.  This node exists
// temporarily to hold intermediate values, and will not be part of the final
// parse tree.
class SeparatedIdentifierTmpNode final : public googlesql::ASTNode {
 public:
  static inline constexpr googlesql::ASTNodeKind kConcreteNodeKind =
      googlesql::AST_FAKE;

  SeparatedIdentifierTmpNode() : ASTNode(kConcreteNodeKind) {}
  void Accept(googlesql::ParseTreeVisitor* visitor, void* data) const override {
    ABSL_LOG(FATAL)  // Crash OK
        << "SeparatedIdentifierTmpNode does not support Accept";
  }
  absl::Status Accept(ParseTreeStatusVisitor& visitor,
                      std::any& output) const override {
    GOOGLESQL_RET_CHECK_FAIL() << "SeparatedIdentifierTmpNode does not support Accept";
  }
  absl::StatusOr<googlesql::VisitResult> Accept(
      googlesql::NonRecursiveParseTreeVisitor* visitor) const override {
    ABSL_LOG(FATAL)  // Crash OK
        << "SeparatedIdentifierTmpNode does not support Accept";
  }
  // This is used to represent an unquoted full identifier path that may contain
  // slashes ("/"), dashes ('-'), and colons (":"). This requires special
  // handling because of the ambiguity in the lexer between an identifier and a
  // number. For example:
  // /span/nonprod-5:db-3.Table
  // The lexer takes this to be
  // /,span,/,nonprod,-,5,:,db,-,3.,Table
  // Where tokens like 3. are treated as a FLOATING_POINT_LITERAL, so the
  // natural path separator "." is lost. For more information on this, see the
  // 'slashed_identifier' rule.

  // We represent this as a list of one or more 'PathParts' which are
  // implicitly separated by a dot ('.'). Each may be composed of one or more
  // 'IdParts' which is a list of the tokens that compose a single component of
  // the path (a single identifier) including any slashes, dashes, and/or
  // colons.
  // Thus, the example string above would be represented as the following:
  // {{"/", "span", "/", "nonprod", "-", "5", ":", "db", "-", "3"}, {"Table"}}

  // In order to save memory, these all contain string_view entries (backed by
  // the parser's copy of the input sql).
  // This also uses inlined vectors, because we rarely expect more than a few
  // entries at either level.
  // Note, in the event the size is large, this will allocate directly to the
  // heap, rather than into the arena.
  using IdParts = std::vector<absl::string_view>;
  using PathParts = std::vector<IdParts>;

  void set_path_parts(PathParts path_parts) {
    path_parts_ = std::move(path_parts);
  }

  PathParts&& release_path_parts() { return std::move(path_parts_); }
  absl::Status InitFields() final {
    {
      FieldLoader fl(this);  // Triggers check that there were no children.
      return fl.Finalize();
    }
  }

  // Returns a vector of identifier ASTNodes from `raw_parts`.
  // `raw_parts` represents a path as a list of lists. Each sublist contains the
  // raw components of an identifier. To form an ASTPathExpression, we
  // concatenate the components of each sublist together to form a single
  // identifier and return a list of these identifiers, which can be used to
  // build an ASTPathExpression.
  static inline absl::StatusOr<std::vector<googlesql::ASTNode*>> BuildPathParts(
      const ParseLocationRange& bison_location, PathParts raw_parts,
      ASTNodeFactory& node_factory) {
    if (raw_parts.empty()) {
      return absl::InvalidArgumentError(
          "Internal error: Empty slashed path expression");
    }
    std::vector<googlesql::ASTNode*> parts;
    for (int i = 0; i < raw_parts.size(); ++i) {
      SeparatedIdentifierTmpNode::IdParts& raw_id_parts = raw_parts[i];
      if (raw_id_parts.empty()) {
        return absl::InvalidArgumentError(
            "Internal error: Empty dashed identifier part");
      }
      // Trim trailing "." which is leftover from lexing float literals
      // like a/1.b -> {"a", "/", "1.", "b"}
      for (int j = 0; j < raw_id_parts.size(); ++j) {
        absl::string_view& dash_part = raw_id_parts[j];
        if (absl::EndsWith(dash_part, ".")) {
          dash_part.remove_suffix(1);
        }
      }
      parts.push_back(node_factory.MakeIdentifier(
          bison_location, absl::StrJoin(raw_id_parts, "")));
    }
    return parts;
  }

 private:
  PathParts path_parts_;
};

// TODO: C++23 - When "deduce this" feature is available in relevant toolchains
//       convert WithLocation family of functions to members of ASTNode.

// Sets the start location of `node` to `point`, and returns `node`.
template <typename ASTNodeType>
ASTNodeType* WithStartLocation(ASTNodeType* node,
                               const ParseLocationPoint& point) {
  node->set_start_location(point);
  return node;
}
// Sets the start location of `node` to the start of `location`, and returns
// `node`.
template <typename ASTNodeType>
ASTNodeType* WithStartLocation(ASTNodeType* node,
                               const ParseLocationRange& location) {
  return WithStartLocation(node, location.start());
}

// Sets the end location of `node` to `point`, and returns `node`.
template <typename ASTNodeType>
ASTNodeType* WithEndLocation(ASTNodeType* node,
                             const ParseLocationPoint& point) {
  node->set_end_location(point);
  return node;
}
// Sets the end location of `node` to the end of `location`, and returns
// `node`.
template <typename ASTNodeType>
ASTNodeType* WithEndLocation(ASTNodeType* node,
                             const ParseLocationRange& location) {
  return WithEndLocation(node, location.end());
}
// Sets the location of `node` to `location`, and returns `node`.
template <typename ASTNodeType>
ASTNodeType* WithLocation(ASTNodeType* node,
                          const ParseLocationRange& location) {
  node->set_location(location);
  return node;
}

// Functions in this internal namespace are helpers for template functions
// defined in this header. Do not call them directly outside the header.
namespace internal {
////////////////////////////////////////////////////////////////////////////////
//
//  Template deduction guides and utility templates.

// TODO: C++20 - Require the iterable concept instead of using the IsIterable
//               custon guide.

template <class ASTNodeType>
using IsASTNodeType =
    std::enable_if_t<std::is_base_of_v<ASTNode, ASTNodeType>, bool>;

template <typename, typename = std::void_t<>>
struct is_iterable : std::false_type {};
template <typename T>
struct is_iterable<T, std::void_t<decltype(std::declval<T&>().begin()++ !=
                                           std::declval<T&>().end())>>
    : std::true_type {};
template <typename T>
using IsIterable = std::enable_if_t<is_iterable<T>::value, bool>;

template <typename Func, typename... Args>
void ForEach(Func&& func, Args&&... args) {
  (func(std::forward<Args>(args)), ...);
}

template <typename Func, typename... Args>
void ForEachReverse(Func&& func, Args&&... args) {
  auto tuple = std::forward_as_tuple(std::forward<Args>(args)...);
  constexpr size_t kSize = sizeof...(Args);
  [&]<std::size_t... I>(std::index_sequence<I...>) {
    (func(std::get<kSize - 1 - I>(tuple)), ...);
  }(std::make_index_sequence<kSize>());
}

inline bool EnforceParentNonNull(ASTNode* /*absl_nonnull*/ parent) {
  // /*absl_nonnull*/ is not enforced, so try to catch missuses here.
  ABSL_DCHECK(parent != nullptr) << "Parse tree construction error: unable to add "
                               "children to null parent.";
  return parent != nullptr;
}

////////////////////////////////////////////////////////////////////////////////
//
//  Base case functions

// ASTNode*

inline bool GetNonEmptyLocation(ASTNode* node, bool from_right,
                                ParseLocationRange& location);

inline void AddToChildren(ASTNode* /*absl_nonnull*/ parent, ASTNode* child,
                          bool to_right);

// std::optional<U>

template <typename U>
inline bool GetNonEmptyLocation(const std::optional<U>& node, bool from_right,
                                ParseLocationRange& location);

template <typename U>
inline void AddToChildren(ASTNode* /*absl_nonnull*/ parent,
                          const std::optional<U>& child, bool to_right);

// std::vector<U>

template <typename U, IsIterable<U> = true>
inline bool GetNonEmptyLocation(U& node, bool from_right,
                                ParseLocationRange& location);

template <typename U, IsIterable<U> = true>
inline void AddToChildren(ASTNode* /*absl_nonnull*/ parent, const U& children,
                          bool to_right);

////////////////////////////////////////////////////////////////////////////////
//
// Base case implementations

inline bool GetNonEmptyLocation(ASTNode* node, bool from_right,
                                ParseLocationRange& location) {
  if (node == nullptr || node->start_location() == node->end_location()) {
    return false;
  }
  location = node->location();
  return true;
}

inline void AddToChildren(ASTNode* /*absl_nonnull*/ parent, ASTNode* child,
                          bool to_right) {
  if (EnforceParentNonNull(parent) && child != nullptr) {
    if (to_right) {
      parent->AddChild(child);
    } else {
      parent->AddChildFront(child);
    }
  }
}

template <typename U>
inline bool GetNonEmptyLocation(const std::optional<U>& node, bool from_right,
                                ParseLocationRange& location) {
  if (!node.has_value()) {
    return false;
  }
  return GetNonEmptyLocation(*node, from_right, location);
}

template <typename U>
inline void AddToChildren(ASTNode* /*absl_nonnull*/ parent,
                          const std::optional<U>& child, bool to_right) {
  if (child.has_value()) {
    AddToChildren(parent, child.value(), to_right);
  }
}

template <typename U, IsIterable<U>>
inline bool GetNonEmptyLocation(U& node, bool from_right,
                                ParseLocationRange& location) {
  if (from_right) {
    for (auto it = node.rbegin(); it != node.rend(); ++it) {
      if (GetNonEmptyLocation(*it, from_right, location)) {
        return true;
      }
    }
  } else {  // from_left
    for (auto it = node.begin(); it != node.end(); ++it) {
      if (GetNonEmptyLocation(*it, from_right, location)) {
        return true;
      }
    }
  }
  return false;
}

template <typename U, IsIterable<U>>
inline void AddToChildren(ASTNode* /*absl_nonnull*/ parent, const U& children,
                          bool to_right) {
  if (to_right) {
    for (const auto& child : children) {
      AddToChildren(parent, child, to_right);
    }
  } else {
    // Traverse the list in reverse order so that (a, b, c) for example, is
    // added to the front of the children list of `node` as (a, b, c).
    for (auto iter = children.rbegin(); iter != children.rend(); ++iter) {
      AddToChildren(parent, *iter, to_right);
    }
  }
}

////////////////////////////////////////////////////////////////////////////////
//
//  Recursive case functions

template <bool to_right, typename... TChildren>
void ExtendNodeRecursive(ASTNode* /*absl_nonnull*/ parent,
                         const ParseLocationPoint& new_loc,
                         TChildren... new_children) {
  if (!EnforceParentNonNull(parent)) {
    return;
  }
  if (to_right) {
    ForEach([&](auto& child) { AddToChildren(parent, child, to_right); },
            new_children...);
    parent->set_end_location(new_loc);
  } else {
    ForEachReverse([&](auto& child) { AddToChildren(parent, child, to_right); },
                   new_children...);
    parent->set_start_location(new_loc);
  }
}

template <bool to_right, typename... TChildren>
void ExtendNodeRecursive(ASTNode* /*absl_nonnull*/ parent,
                         TChildren... new_children) {
  if (!EnforceParentNonNull(parent)) {
    return;
  }
  ParseLocationRange new_range = parent->location();
  ParseLocationPoint new_point;
  bool found_location = false;
  if (to_right) {
    // Find the last non-empty location range in the children.
    ForEachReverse(
        [&](auto& child) {
          found_location =
              found_location || GetNonEmptyLocation(child, to_right, new_range);
        },
        new_children...);
    new_point = new_range.end();
  } else {
    // Find the first non-empty location range in the children.
    ForEach(
        [&](auto& child) {
          found_location =
              found_location || GetNonEmptyLocation(child, to_right, new_range);
        },
        new_children...);
    new_point = new_range.start();
  }
  ExtendNodeRecursive<to_right>(parent, new_point, new_children...);
}

}  // namespace internal

// TODO: C++23 - When "deduce this" feature is available in relevant toolchains
//       convert ExtendNode family of functions to members of ASTNode.

// Extends `parent` by appending `new_children` at the front of the children
// list and also extend `parent`'s location range to the left by settings its
// end point to 'new_start_location`.
//
// Returns `parent` to allow this function to be used on the right hand side of
// an assignment, in a chained fluent style, or in a return statement.
//
// `new_children` may contain nulls, which are ignored.
template <class ASTNodeType, typename... TChildren,
          internal::IsASTNodeType<ASTNodeType> = true>
ASTNodeType* ExtendNodeLeft(ASTNodeType* /*absl_nonnull*/ parent,
                            const ParseLocationPoint& new_start_location,
                            TChildren... new_children) {
  internal::ExtendNodeRecursive</*to_right=*/false>(parent, new_start_location,
                                                    new_children...);
  return parent;
}

// Extends `parent` by adding `new_children` at the front of the children list
// and also extend `parent`s location range to the left by settings its start
// point to the start location of the first child that has a non-empty location
// range.
//
// Returns `parent` to allow this function to be used on the right hand side of
// an assignment, in a chained fluent style, or in a return statement.
//
// `new_children` must have no  nullptr elements. To extend with a child that
// may be nullptr: use the overload that takes an explicit `new_end_location`
// argument.
template <class ASTNodeType, typename... TChildren,
          internal::IsASTNodeType<ASTNodeType> = true>
ASTNodeType* ExtendNodeLeft(ASTNodeType* /*absl_nonnull*/ parent,
                            TChildren... new_children) {
  internal::ExtendNodeRecursive</*to_right=*/false>(parent, new_children...);
  return parent;
}

// Extends `parent` by adding `new_children` at the back of the children list
// and also extend `parent`s location range to the right by settings its end
// point to 'new_end_location`.
//
// Returns `parent` to allow this function to be used on the right hand side of
// an assignment, in a chained fluent style, or in a return statement.
//
// `new_children` may have be nullptr elements. In that case the location range
// is still extended to `new_end_location`.
template <class ASTNodeType, typename... TChildren,
          internal::IsASTNodeType<ASTNodeType> = true>
ASTNodeType* ExtendNodeRight(ASTNodeType* /*absl_nonnull*/ parent,
                             const ParseLocationPoint& new_end_location,
                             TChildren... new_children) {
  internal::ExtendNodeRecursive</*to_right=*/true>(parent, new_end_location,
                                                   new_children...);
  return parent;
}

// Extends `parent` by adding `new_children` at the back of the children list
// and also, if `new_children` is not nullptr, extends `parent`s location range
// to the right by setting its end point to `new_children`s end location.
//
// Returns `parent` to allow this function to be used on the right hand side of
// an assignment, in a chained fluent style, or in a return statement.
//
// `new_children` must have no nullptr elements. To extend with a child that may
// may be nullptr: use the overload that takes an explicit `new_end_location`
// argument.
template <class ASTNodeType, typename... TChildren,
          internal::IsASTNodeType<ASTNodeType> = true>
ASTNodeType* ExtendNodeRight(ASTNodeType* /*absl_nonnull*/ parent,
                             TChildren... new_children) {
  internal::ExtendNodeRecursive</*to_right=*/true>(parent, new_children...);
  return parent;
}

// Returns the first location in 'locations' that is not empty. If none of the
// locations are nonempty, returns the first location.
template <typename Location>
inline Location FirstNonEmptyLocation(const Location& a, const Location& b) {
  if (a.start().GetByteOffset() != a.end().GetByteOffset()) {
    return a;
  }
  if (b.start().GetByteOffset() != b.end().GetByteOffset()) {
    return b;
  }
  return a;
}

// Generates a parse error with message `msg` at `location`.
absl::Status MakeSyntaxError(const ParseLocationRange& location,
                             absl::string_view msg);

// Generates a parse error with message `msg` at `location`.
absl::Status MakeSyntaxError(const std::optional<ParseLocationRange>& location,
                             absl::string_view msg);

// Generates a parse error if there are spaces between `left_loc` and
// `right_loc`. For example, this is used when we composite multiple existing
// tokens to match a complex symbol without reserving it as a new token.
absl::Status ValidateNoWhitespace(absl::string_view left,
                                  const ParseLocationRange& left_loc,
                                  absl::string_view right,
                                  const ParseLocationRange& right_loc);

template <typename Location, typename... MoreLocations>
inline Location NonEmptyRangeLocation(const Location& first_location,
                                      const MoreLocations&... locations) {
  std::optional<Location> range;
  for (const Location& location : {first_location, locations...}) {
    if (location.start().GetByteOffset() != location.end().GetByteOffset()) {
      if (!range.has_value()) {
        range = location;
      } else {
        if (location.start().GetByteOffset() < range->start().GetByteOffset()) {
          range->set_start(location.start());
        }
        if (location.end().GetByteOffset() > range->end().GetByteOffset()) {
          range->set_end(location.end());
        }
      }
    }
  }
  if (range.has_value()) {
    return *range;
  }
  return first_location;
}

// Helps detect illegal case where the NOT operator is used on the right hand
// side of a binary operator. Due to precedence rules, a NOT operator on the
// right hand side needs to be parenthesized.
//
// `rhs_expr`: an expression from the right hand side of a binary operator.
absl::Status ErrorIfUnparenthesizedNotExpression(ASTNode* rhs_expr);

// Makes a zero-length location range: [point, point).
// This is to simulate a required AST node whose child nodes are all optional.
// The location range of the node when all children are unspecified is an empty
// range.
template <typename LocationPoint>
inline ParseLocationRange LocationFromOffset(const LocationPoint& point) {
  return ParseLocationRange(point, point);
}

// Returns true if the given text can be an unquoted identifier or a reserved
// keyword. This is useful for cases that can accept any reserved keyword as
// identifier without quoting, such as a macro name.
inline absl::Status IsIdentifierOrKeyword(absl::string_view text) {
  ABSL_DCHECK(!text.empty());
  if (text.front() == '`') {
    // This is a quoted identifier. No other token coming from the lexer &
    // lookahead transformer starts with a backtick, not even error recovery
    // ones (i.e. tokens defined to capture some wrong pattern and provide a
    // better context or error message).
    std::string str;
    std::string error_string;
    int error_offset;
    return ParseGeneralizedIdentifier(text, &str, &error_string, &error_offset);
  }

  // Not a quoted identifier, so this must be a valid identifier or a keyword
  // (regardless of reserved or not). The regex is [A-Z_][A-Z_0-9]* (case-
  // insensitive, see the lexer rules). The first character must be an
  // underscore or alpha.
  if (text.front() != '_' && !std::isalpha(text.front())) {
    return MakeSqlError() << "Expected macro name";
  }

  // After the first, we only accept an alphanumerical or an underscore.
  for (int i = 1; i < text.length(); ++i) {
    if (text[i] != '_' && !std::isalnum(text[i])) {
      return MakeSqlError() << "Expected macro name";
    }
  }
  return absl::OkStatus();
}

template <typename TableExprType, typename = std::enable_if_t<std::is_base_of_v<
                                      ASTTableExpression, TableExprType>>>
inline TableExprType* MaybeApplyPivotOrUnpivot(
    TableExprType* table_expr, ASTPivotClause* pivot_clause,
    ASTUnpivotClause* unpivot_clause) {
  ABSL_DCHECK(pivot_clause == nullptr || unpivot_clause == nullptr)
      << "pivot_clause and unpivot_clause cannot both be non-null";
  if (pivot_clause != nullptr) {
    return ExtendNodeRight(table_expr, pivot_clause);
  }

  if (unpivot_clause != nullptr) {
    return ExtendNodeRight(table_expr, unpivot_clause);
  }

  return table_expr;
}

// `Location` is needed because when `left` or `right` is a parenthesized
// expression, their location doesn't include those parens, so `(left.start,
// right.end)` does not cover the full range.
inline ASTRowPatternExpression* MakeOrCombineRowPatternOperation(
    const ASTRowPatternOperation::OperationType op,
    ASTNodeFactory& node_factory, const ParseLocationRange& location,
    ASTRowPatternExpression* left, ASTRowPatternExpression* right) {
  if (left->node_kind() == AST_ROW_PATTERN_OPERATION &&
      left->GetAsOrDie<ASTRowPatternOperation>()->op_type() == op &&
      !left->parenthesized()) {
    return ExtendNodeRight(left, location.end(), right);
  } else {
    // if `left` is an unparenthesized empty pattern, its location will still
    // be the end offset of the last token, outside of @$.
    // Adjust its location to the start of the `|`.
    if (left->node_kind() == AST_EMPTY_ROW_PATTERN && !left->parenthesized()) {
      left->set_start_location(location.start());
      left->set_end_location(location.start());
    }

    auto* new_root = node_factory.CreateASTNode<ASTRowPatternOperation>(
        location, {left, right});
    new_root->set_op_type(op);
    return new_root;
  }
}

inline void SetGqlSubqueryQueryFields(ASTQuery* query) {
  // Graph subquery are braced {} not parenthesized (), so we unset for unparser
  // to skip the parenthesize printing
  query->set_is_nested(false);
  query->set_parenthesized(false);
}

// Constructs a graph subquery based on operators and an optional graph
// reference.
template <typename Location>
inline ASTQuery* MakeGraphSubquery(ASTGqlOperatorList* ops,
                                   ASTPathExpression* graph,
                                   ASTNodeFactory& node_factory,
                                   const Location& loc) {
  auto* graph_table =
      node_factory.CreateASTNode<ASTGraphTableQuery>(loc, {graph, ops});
  auto* graph_query =
      node_factory.CreateASTNode<ASTGqlQuery>(loc, {graph_table});
  auto* query = node_factory.CreateASTNode<ASTQuery>(loc, {graph_query});
  SetGqlSubqueryQueryFields(query);
  return query;
}

// Constructs a GQL path pattern graph subquery based on a path pattern and
// an optional graph reference.
template <typename Location>
inline ASTExpressionSubquery* MakeGqlExistsGraphPatternSubquery(
    ASTGraphPattern* graph_pattern, ASTPathExpression* graph, ASTNode* hint,
    ASTNodeFactory& node_factory, const Location& loc) {
  auto* path_pattern_query =
      node_factory.CreateASTNode<ASTGqlGraphPatternQuery>(
          loc, {graph, graph_pattern});
  auto* query = node_factory.CreateASTNode<ASTQuery>(loc, {path_pattern_query});
  SetGqlSubqueryQueryFields(query);
  auto* subquery =
      node_factory.CreateASTNode<ASTExpressionSubquery>(loc, {hint, query});
  subquery->set_modifier(googlesql::ASTExpressionSubquery::EXISTS);
  return subquery;
}

// Constructs a GQL linear ops graph subquery based on an op list and
// an optional graph reference.
template <typename Location>
inline ASTExpressionSubquery* MakeGqlExistsLinearOpsSubquery(
    ASTGqlOperatorList* op_list, ASTPathExpression* graph, ASTNode* hint,
    ASTNodeFactory& node_factory, const Location& loc) {
  auto* linear_ops_query =
      node_factory.CreateASTNode<ASTGqlLinearOpsQuery>(loc, {graph, op_list});
  auto* query = node_factory.CreateASTNode<ASTQuery>(loc, {linear_ops_query});
  SetGqlSubqueryQueryFields(query);
  auto* subquery =
      node_factory.CreateASTNode<ASTExpressionSubquery>(loc, {hint, query});
  subquery->set_modifier(googlesql::ASTExpressionSubquery::EXISTS);
  return subquery;
}

template <typename Location>
inline googlesql::ASTGraphLabelExpression* MakeOrCombineGraphLabelOperation(
    const googlesql::ASTGraphLabelOperation::OperationType label_op,
    ASTNodeFactory& node_factory, const Location& location,
    googlesql::ASTGraphLabelExpression* left,
    googlesql::ASTGraphLabelExpression* right) {
  if (left->node_kind() == googlesql::AST_GRAPH_LABEL_OPERATION &&
      left->GetAsOrDie<googlesql::ASTGraphLabelOperation>()->op_type() ==
          label_op &&
      !left->parenthesized()) {
    // Embrace and extend left's ASTNode to flatten a series of `label_op`.
    return ExtendNodeRight(left, location.end(), right);
  } else {
    auto* new_root = node_factory.CreateASTNode<ASTGraphLabelOperation>(
        location, {left, right});
    new_root->set_op_type(label_op);
    return new_root;
  }
}

template <typename Location>
inline googlesql::ASTGraphEdgePattern* MakeGraphEdgePattern(
    ASTNodeFactory& node_factory,
    googlesql::ASTGraphElementPatternFiller* filler,
    googlesql::ASTGraphEdgePattern::EdgeOrientation orientation,
    const Location& location) {
  auto* edge_pattern =
      node_factory.CreateASTNode<ASTGraphEdgePattern>(location, {filler});
  edge_pattern->set_orientation(orientation);
  return edge_pattern;
}

template <typename Location>
inline googlesql::ASTGraphElementLabelAndPropertiesList*
MakeGraphElementLabelAndPropertiesListImplicitDefaultLabel(
    ASTNodeFactory& node_factory, googlesql::ASTGraphProperties* properties,
    const Location& location) {
  auto* label_properties =
      node_factory.CreateASTNode<ASTGraphElementLabelAndProperties>(
          location, {nullptr, properties});
  auto* label_properties_list =
      node_factory.CreateASTNode<ASTGraphElementLabelAndPropertiesList>(
          location, {label_properties});
  return label_properties_list;
}

// Returns true if any node in the query subtree has a LockMode node.
inline bool HasLockMode(const googlesql::ASTNode* node) {
  if (node != nullptr) {
    // Use queue based BFS traversal to avoid stack overflow issues.
    std::queue<const googlesql::ASTNode*> node_queue;
    node_queue.push(node);

    while (!node_queue.empty()) {
      const googlesql::ASTNode* current_node = node_queue.front();
      node_queue.pop();

      if (current_node->node_kind() == googlesql::AST_LOCK_MODE) {
        return true;
      } else {
        for (int i = 0; i < current_node->num_children(); ++i) {
          if (current_node->child(i) != nullptr) {
            node_queue.push(current_node->child(i));
          }
        }
      }
    }
  }
  return false;
}

inline absl::StatusOr<GraphDynamicLabelProperties> MergeDynamicLabelProperties(
    const GraphDynamicLabelProperties& left,
    const GraphDynamicLabelProperties& right) {
  if (left.dynamic_label != nullptr && right.dynamic_label != nullptr) {
    return MakeSqlError() << "DYNAMIC LABEL cannot be used more than once in a "
                             "single element table";
  }
  if (left.dynamic_properties != nullptr &&
      right.dynamic_properties != nullptr) {
    return MakeSqlError()
           << "DYNAMIC PROPERTIES cannot be used more than once in a "
              "single element table";
  }
  return GraphDynamicLabelProperties{
      .dynamic_label =
          left.dynamic_label ? left.dynamic_label : right.dynamic_label,
      .dynamic_properties = left.dynamic_properties ? left.dynamic_properties
                                                    : right.dynamic_properties};
}

inline absl::Status AddKeywordReservationWarning(
    const absl::string_view keyword, const ParseLocationRange& location,
    WarningSink& warning_sink) {
  // TODO: this warning should point to documentation once
  // we have the engine-specific root URI to use.
  constexpr absl::string_view kReservedWordWarning =
      "$0 is used as an identifier. $0 may become a reserved word in the "
      "future. To make this statement robust, add backticks around $0 to make "
      "the identifier unambiguous";
  return warning_sink.AddWarning(
      DeprecationWarning::RESERVED_KEYWORD,
      MakeSqlErrorAtStart(location)
          << absl::Substitute(kReservedWordWarning, keyword));
}

using googlesql::ASTInsertStatement;

}  // namespace internal
}  // namespace parser
}  // namespace googlesql

#endif  // GOOGLESQL_PARSER_PARSER_INTERNAL_H_
