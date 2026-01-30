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

#include "googlesql/tools/formatter/internal/token.h"

#include <stddef.h>
#include <stdint.h>

#include <initializer_list>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "googlesql/parser/keywords.h"
#include "googlesql/public/formatter_options.h"
#include "googlesql/public/language_options.h"
#include "googlesql/public/options.pb.h"
#include "googlesql/public/parse_location.h"
#include "googlesql/public/parse_resume_location.h"
#include "googlesql/public/parse_tokens.h"
#include "googlesql/public/type.pb.h"
#include "googlesql/public/value.h"
#include "absl/container/flat_hash_map.h"
#include "absl/log/log.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/ascii.h"
#include "absl/strings/match.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/str_join.h"
#include "absl/strings/str_replace.h"
#include "absl/strings/str_split.h"
#include "absl/strings/string_view.h"
#include "absl/types/span.h"
#include "unicode/chariter.h"
#include "unicode/schriter.h"
#include "unicode/uchar.h"
#include "unicode/unistr.h"
#include "unicode/utypes.h"
#include "googlesql/base/flat_set.h"
#include "re2/re2.h"
#include "googlesql/base/status_macros.h"

namespace googlesql::formatter::internal {

namespace {

bool StrContainsIgnoreCase(absl::string_view haystack,
                           absl::string_view needle) {
  return absl::StrContains(absl::AsciiStrToUpper(haystack),
                           absl::AsciiStrToUpper(needle));
}

// Like absl::ConsumeSuffix, but case insensitive.
bool ConsumeCaseSuffix(absl::string_view* s, absl::string_view expected) {
  if (absl::EndsWithIgnoreCase(*s, expected)) {
    s->remove_suffix(expected.size());
    return true;
  }
  return false;
}

// Like absl::ConsumePrefix, but case insensitive.
bool ConsumeCasePrefix(absl::string_view* s, absl::string_view expected) {
  if (absl::StartsWithIgnoreCase(*s, expected)) {
    s->remove_prefix(expected.size());
    return true;
  }
  return false;
}

// Various types of invalid tokens that have different rules when they should be
// terminated.
enum class InvalidTokenType {
  OTHER,
  SINGLE_LINE_COMMENT,
  MULTILINE_COMMENT,
  SINGLE_QUOTE_STRING,
  DOUBLE_QUOTE_STRING,
  BACKTICK_IDENTIFIER,
  MULTILINE_STRING_SINGLE_QUOTES,
  MULTILINE_STRING_DOUBLE_QUOTES,
};

// Detects the token type that starts at given location and adjusts the iterator
// past the token start.
InvalidTokenType DetectTokenType(icu::StringCharacterIterator* current_char) {
  char32_t ch = current_char->current32();
  // The token should be at least one character long.
  current_char->next32();

  // Try to consume string literal prefixes.
  bool raw_string = false;
  bool byte_string = false;
  while (current_char->hasNext() && !(raw_string && byte_string)) {
    if ((ch == 'b' || ch == 'B') && !byte_string) {
      byte_string = true;
      ch = current_char->current32();
      current_char->next32();
    } else if ((ch == 'r' || ch == 'R') && !raw_string) {
      raw_string = true;
      ch = current_char->current32();
      current_char->next32();
    } else {
      break;
    }
  }

  if (!current_char->hasNext()) {
    // We don't care what type of token it is if we reached the end of input:
    // in any case the token stops here.
    return InvalidTokenType::OTHER;
  }

  switch (ch) {
    case '\'': {
      if (current_char->current32() == '\'') {
        if (current_char->hasNext()) {
          if (current_char->next32() == '\'') {
            // Adjust iterator to point after the triple quote: '''
            current_char->next32();
            return InvalidTokenType::MULTILINE_STRING_SINGLE_QUOTES;
          } else {
            current_char->previous32();
          }
        }
      }
      return InvalidTokenType::SINGLE_QUOTE_STRING;
    }
    case '\"':
      if (current_char->current32() == '\"') {
        if (current_char->hasNext()) {
          if (current_char->next32() == '\"') {
            // Adjust iterator to point after the triple quote: '"'
            current_char->next32();
            return InvalidTokenType::MULTILINE_STRING_DOUBLE_QUOTES;
          } else {
            current_char->previous32();
          }
        }
      }
      return InvalidTokenType::DOUBLE_QUOTE_STRING;
    case '`':
      if (!raw_string && !byte_string) {
        return InvalidTokenType::BACKTICK_IDENTIFIER;
      } else {
        return InvalidTokenType::OTHER;
      }
    case '/':
      if (raw_string || byte_string) {
        return InvalidTokenType::OTHER;
      } else if (current_char->current32() == '*') {
        // Adjust iterator to point after the comment start: /*
        current_char->next32();
        return InvalidTokenType::MULTILINE_COMMENT;
      } else if (current_char->current32() == '/') {
        current_char->next32();
        return InvalidTokenType::SINGLE_LINE_COMMENT;
      } else {
        return InvalidTokenType::OTHER;
      }
    case '-':
      if (!raw_string && !byte_string && current_char->current32() == '-') {
        current_char->next32();
        return InvalidTokenType::SINGLE_LINE_COMMENT;
      } else {
        return InvalidTokenType::OTHER;
      }
    case '#':
      if (!raw_string && !byte_string) {
        return InvalidTokenType::SINGLE_LINE_COMMENT;
      } else {
        return InvalidTokenType::OTHER;
      }
    default:
      return InvalidTokenType::OTHER;
  }
}

// Checks if the current or next characters end the current token.
// Returns the iterator pointing to the last character of the token (or end of
// input).
icu::StringCharacterIterator FindTokenEnd(icu::StringCharacterIterator it,
                                          InvalidTokenType type) {
  int state = 0;
  while (it.hasNext()) {
    const char32_t ch = it.current32();

    switch (type) {
      // For a "simple string wait until a closing " or a line break.
      case InvalidTokenType::SINGLE_QUOTE_STRING:
        if (ch == '\'') {
          // Token ends after the closing quote.
          it.next32();
          return it;
        } else if (ch == '\r' || ch == '\n') {
          return it;
        }
        break;
      case InvalidTokenType::DOUBLE_QUOTE_STRING:
        if (ch == '\"') {
          // Token ends after the closing quote.
          it.next32();
          return it;
        } else if (ch == '\r' || ch == '\n') {
          return it;
        }
        break;
      case InvalidTokenType::BACKTICK_IDENTIFIER:
        if (ch == '`') {
          // Token ends after the closing backtick.
          it.next32();
          return it;
        } else if (ch == '\r' || ch == '\n') {
          return it;
        }
        break;
      // For a """multiline string literals wait until closing triple quotes
      // """.
      case InvalidTokenType::MULTILINE_STRING_SINGLE_QUOTES:
        if (ch == '\'') {
          state++;
        } else {
          state = 0;
        }
        if (state == 3) {
          it.next32();
          return it;
        }
        break;
      case InvalidTokenType::MULTILINE_STRING_DOUBLE_QUOTES:
        if (ch == '\"') {
          state++;
        } else {
          state = 0;
        }
        if (state == 3) {
          it.next32();
          return it;
        }
        break;
      // For a -- single line comment wait until the line end.
      case InvalidTokenType::SINGLE_LINE_COMMENT:
        if (ch == '\r' || ch == '\n') {
          return it;
        }
        break;
      // For a /*multiline comment wait until closing */.
      case InvalidTokenType::MULTILINE_COMMENT:
        if (ch == '*') {
          state = 1;
        } else if (state == 1 && ch == '/') {
          it.next32();
          return it;
        } else {
          state = 0;
        }
        break;
      // Default behavior: a token ends either with a whitespace character
      // (e.g. "SELECT "), or when another token starts
      // (e.g., in "a.b" - token `a` ends at `.`).
      case InvalidTokenType::OTHER:
        static const auto* new_token_start = new googlesql_base::flat_set<char32_t>(
            {'.', ',', ':', ';', '(', ')', '[', ']', '{',  '}', '<',
             '>', '+', '-', '*', '/', '=', '!', '#', '\'', '"', '`'});
        if (u_isUWhiteSpace(ch) || new_token_start->contains(ch)) {
          return it;
        }
        break;
    }
    it.next32();
  }
  return it;
}

// Shortcut functions to find token's start and end positions in the original
// input.
inline int TokenStartOffset(const ParseToken& token) {
  return token.GetLocationRange().start().GetByteOffset();
}
inline int TokenEndOffset(const ParseToken& token) {
  return token.GetLocationRange().end().GetByteOffset();
}

int DistanceBetweenTwoIndexInBytes(const icu::UnicodeString& txt, int left,
                                   int right) {
  return txt.extract(left, right - left, /*target=*/(char*)nullptr,
                     /*targetLength=*/(uint32_t)0);
}

// Returns end of line offset in `str` for the line containing `start`.
int FindLineEnd(absl::string_view str, int start) {
  std::string_view text_str_view = str.substr(start);
  icu::UnicodeString txt(text_str_view.data(),
                         static_cast<int32_t>(text_str_view.size()));
  icu::StringCharacterIterator it(txt);
  while (it.current32() != '\n' && it.hasNext()) {
    it.next32();
  }
  int end_offset = start + DistanceBetweenTwoIndexInBytes(txt, it.startIndex(),
                                                          it.getIndex());
  if (it.hasNext()) {
    it.next32();
    if (it.hasNext() && it.next32() == '\r') {
      ++end_offset;
    }
  }
  return end_offset;
}

// Returns the amount of line breaks between the `token` and previous non-space
// character in the `sql` (or beginning of `sql` if it is the first token).
int CountLineBreaksBefore(const ParseToken& token, const ParseToken* prev_token,
                          absl::string_view sql) {
  int line_break_count = 0;
  int start_from = 0;
  if (prev_token != nullptr) {
    // Single line comment tokens consume greedily trailing line breaks into
    // their image. So we include the token body into the string we scan looking
    // for line breaks.
    start_from = prev_token->IsComment() ? TokenStartOffset(*prev_token)
                                         : TokenEndOffset(*prev_token);
  }
  absl::string_view sql_before_token_substr =
      sql.substr(start_from, TokenStartOffset(token) - start_from);
  icu::UnicodeString sql_before_token(
      sql_before_token_substr.data(),
      static_cast<int32_t>(sql_before_token_substr.size()));
  icu::StringCharacterIterator c(sql_before_token);
  c.move32(-1, icu::CharacterIterator::kEnd);
  do {
    if (!u_isUWhiteSpace(c.current32())) {
      break;
    }
    if (c.current32() == '\n') {
      line_break_count++;
    }
  } while (c.hasPrevious() && c.previous32());
  return line_break_count;
}

// Updates tokens with the number of line breaks before and after the token.
void CountLineBreaksAroundTokens(const ParseResumeLocation& parse_location,
                                 std::vector<Token>& tokens) {
  tokens[0].SetLineBreaksBefore(CountLineBreaksBefore(
      tokens[0], /*prev_token=*/nullptr, parse_location.input()));
  for (int t = 1; t < tokens.size(); ++t) {
    int line_breaks_count = CountLineBreaksBefore(tokens[t], &tokens[t - 1],
                                                  parse_location.input());
    // If the previous token was multiline, always have at least 1 line
    // break after it, even if there was no breaks in the original input.
    if (line_breaks_count == 0 && tokens[t - 1].IsMultiline()) {
      ++line_breaks_count;
    }
    tokens[t].SetLineBreaksBefore(line_breaks_count);
    tokens[t - 1].SetLineBreaksAfter(line_breaks_count);
  }

  // Always add a line break after the very last comment in the statement.
  tokens.back().SetLineBreaksAfter(1);
}

// Returns true if the given token is a multiline, before it is marked as such.
bool IsMultilineToken(const Token& token) {
  return absl::StrContains(token.GetImage(), '\n') &&
         // Only a `/*...*/` comment can be multiline. Other comment types
         // greedily capture line breaks after them, although they are not
         // multiline.
         (token.IsSlashStarComment() || !token.IsComment());
}

// Marks all multiline comment tokens and removes trailing whitespace from
// interior lines.
void MarkAndFixUpAllMultilineTokens(std::vector<Token>& tokens,
                                    const FormatterOptions& options) {
  for (Token& token : tokens) {
    if (IsMultilineToken(token)) {
      token.MarkAsMultiline();
      if (!options.IsFormattingComments() || !token.IsComment()) {
        continue;
      }
      std::vector<absl::string_view> lines =
          absl::StrSplit(token.GetImage(), options.NewLineType());
      for (absl::string_view& line : lines) {
        line = absl::StripTrailingAsciiWhitespace(line);
      }
      const std::string new_image = absl::StrJoin(lines, options.NewLineType());
      if (new_image != token.GetImage()) {
        // We can't alter the image of a token, so we create a new one.
        token = Token(token, /*keyword=*/"", new_image);
      }
    }
  }
}

// Returns byte offset of the iterator `it` that iterates over unicode text
// `txt` that starts at the given `resume_location`.
int ByteOffsetFromUtfIterator(const ParseResumeLocation& resume_location,
                              const icu::UnicodeString& txt,
                              const icu::StringCharacterIterator& it) {
  return resume_location.byte_position() +
         DistanceBetweenTwoIndexInBytes(txt, it.startIndex(), it.getIndex());
}

// Creates a ParseLocationPoint pointing to the position of the iterator `it`,
// assuming the same relations between `it`, `txt` and `resume_location` as in
// previous function.
ParseLocationPoint ParseLocationPointFromUtfIterator(
    const ParseResumeLocation& resume_location, const icu::UnicodeString& txt,
    const icu::StringCharacterIterator& it) {
  return ParseLocationPoint::FromByteOffset(
      ByteOffsetFromUtfIterator(resume_location, txt, it));
}

// Same as above, but creates a ParseLocationRange [`start`..`end`).
ParseLocationRange ParseLocationRangeFromUtfIterators(
    const ParseResumeLocation& resume_location, const icu::UnicodeString& txt,
    const icu::StringCharacterIterator& start,
    const icu::StringCharacterIterator& end) {
  ParseLocationRange location;
  location.set_start(
      ParseLocationPointFromUtfIterator(resume_location, txt, start));
  location.set_end(
      ParseLocationPointFromUtfIterator(resume_location, txt, end));
  return location;
}

// Creates UnicodeText that points to the remaining input for given `location`.
icu::UnicodeString RemainingInputAsUnicode(const ParseResumeLocation& location,
                                           int end_position = -1) {
  absl::string_view remaining_input =
      location.input().substr(location.byte_position());
  if (end_position > 0) {
    remaining_input =
        remaining_input.substr(0, end_position - location.byte_position());
  }
  icu::UnicodeString utf = icu::UnicodeString::fromUTF8(remaining_input);
  const UChar32 null[1] = {'\0'};
  const UChar32 space[1] = {' '};
  utf.findAndReplace(icu::UnicodeString::fromUTF32(null, 1),
                     icu::UnicodeString::fromUTF32(space, 1));
  const UChar32 invalid[1] = {65533};
  utf.findAndReplace(icu::UnicodeString::fromUTF32(invalid, 1),
                     icu::UnicodeString::fromUTF32(space, 1));
  return utf;
}

// Returns iterator pointing to the first non-whitespace character between
// `first` and `last`.
icu::StringCharacterIterator SkipAllWhitespaces(
    icu::StringCharacterIterator it) {
  while (it.hasNext()) {
    if (!u_isUWhiteSpace(it.current32())) {
      return it;
    }
    it.next32();
  }
  return it;
}

// Adjust position of the given `location` to the first non-whitespace
// character.
void SkipAllWhitespaces(ParseResumeLocation* location) {
  icu::UnicodeString txt = RemainingInputAsUnicode(*location);
  icu::StringCharacterIterator it(txt);
  it = SkipAllWhitespaces(it);
  if (it.getIndex() != it.startIndex()) {
    int byte_position = ByteOffsetFromUtfIterator(*location, txt, it);
    location->set_byte_position(byte_position);
  }
}

// Returns iterator pointing to the first non-space character or first character
// after a line break - whatever comes first.
icu::StringCharacterIterator SkipSpacesUntilEndOfLine(
    icu::StringCharacterIterator it) {
  while (it.hasNext()) {
    if (it.current32() == '\r' || it.current32() == '\n') {
      // Consume any of the following line breaks: '\r' '\r\n', '\n'.
      if (it.current32() == '\r') {
        it.next32();
      }
      if (it.hasNext() && it.current32() == '\n') {
        it.next32();
      }
      return it;
    } else if (!u_isUWhiteSpace(it.current32())) {
      return it;
    }
    it.next32();
  }
  return it;
}

// Returns iterator pointing to the first non-comment token on the current line
// or first character after the line break - whatever comes first.
icu::StringCharacterIterator SkipSpacesAndCommentsUntilEndOfLine(
    icu::StringCharacterIterator it) {
  while (it.hasNext()) {
    if (it.current32() == '\r' || it.current32() == '\n') {
      // Consume any of the following line breaks: '\r' '\r\n', '\n'.
      if (it.current32() == '\r') {
        it.next32();
      }
      if (it.hasNext() && it.current32() == '\n') {
        it.next32();
      }
      return it;
    } else if (!u_isUWhiteSpace(it.current32())) {
      // `DetectTokenType` moves the iterator past the token start and we don't
      // want that if the token is not a comment.
      icu::StringCharacterIterator copy = it;
      InvalidTokenType type = DetectTokenType(&copy);
      if (type == InvalidTokenType::SINGLE_LINE_COMMENT ||
          type == InvalidTokenType::MULTILINE_COMMENT) {
        it = FindTokenEnd(copy, type);
        continue;
      } else {
        return it;
      }
    }
    it.next32();
  }
  return it;
}

// Checks if there is an end-of-line comment on the same line with the given
// `parse_location`. If there is one, parses it and appends to `tokens`
// adjusting `parse_location` accordingly.
void ParseEndOfLineCommentIfAny(ParseResumeLocation* parse_location,
                                std::vector<Token>& tokens) {
  // Check if there is anything between the current parse location and the end
  // of line.
  size_t end_of_line_pos =
      parse_location->input().find('\n', parse_location->byte_position());
  if (end_of_line_pos == absl::string_view::npos) {
    end_of_line_pos = parse_location->input().size();
  }
  if (end_of_line_pos == parse_location->byte_position()) {
    return;
  }

  // Adjust the parse location to point to the next token.
  SkipAllWhitespaces(parse_location);
  if (parse_location->byte_position() > end_of_line_pos) {
    return;
  }

  // Parse one more token to check if it is an end-of-line comment.
  ParseTokenOptions options{.max_tokens = 1, .include_comments = true};
  // We copy parse location, since when it is used with max_tokens option, the
  // location becomes not-resumable.
  ParseResumeLocation comment_location = *parse_location;
  std::vector<ParseToken> comment_tokens;
  if (!GetParseTokens(options, &comment_location, &comment_tokens).ok()) {
    // Unable to parse the next token - it is probably some broken sql.
    return;
  }

  // Don't use the parsed token if it is not a comment, or it starts after the
  // end of line (safety check).
  if (comment_tokens.empty() || !comment_tokens[0].IsComment() ||
      TokenStartOffset(comment_tokens[0]) > end_of_line_pos) {
    return;
  }

  // Check if there is another token on the same line, which makes this comment
  // not an end of line comment.
  if (comment_location.byte_position() < end_of_line_pos) {
    SkipAllWhitespaces(&comment_location);
    if (comment_location.byte_position() < end_of_line_pos) {
      return;
    }
  }

  parse_location->set_byte_position(comment_location.byte_position());
  tokens.emplace_back(comment_tokens[0]);
  parse_location->set_byte_position(TokenEndOffset(tokens.back()));
}

// List of SQL constructs that require special handling, e.g., grouping multiple
// parse tokens into one.
enum class GroupingType {
  kNone,
  // Java gORM bind statement "BIND name type": (broken link)
  // The statement ends without a semicolon.
  kGormBindStmt,
  // Legacy set statement: "SET variable value". The statement allows invalid
  // characters in the value (dashes, slashes, colons) without quotes, e.g.:
  //   SET accounting_group group-name
  // The statement also may end without a trailing semicolon.
  kLegacySetStmt,
  // Legacy define table syntax:
  // DEFINE TABLE name <<EOF
  //   <non-sql content>
  // EOF;
  // The entire region "<<EOF..EOF" becomes a single token.
  kLegacyDefineTableStmt,
  // A file region between "SQLFORMAT:OFF" and "SQLFORMAT:ON" comments. The
  // entire region becomes a single token.
  kNoFormatBlock,
  // Legacy "LOAD something" or "SOURCE something" statement, where 'something'
  // can be a string, identifier or path. The statement might end without a
  // semicolon.
  kLegacyLoadOrSourceStmt,
  // A {token} in curly braces. These are not part of GoogleSQL, but
  // widely used by various templating frameworks. The token might contain
  // multiple braces, e.g.: {{token}}.
  kCurlyBracesParameter,
  // Jinja {% code blocks %} and {# comments #}. These are very similar to curly
  // braces parameters, but allow spaces inside and don't act as identifiers.
  // These expressions are treated as SQL comments, e.g.:
  //  SELECT
  //    {% if env == 'prod' %}
  //    user_id,
  //    {% endif %}
  //    ...
  kJinjaExpression,
  // Legacy ASSERT statements have unquoted description in square brackets:
  // "ASSERT [error message] AS ...".
  kLegacyAssertDescription,
  // C++ style comments starting with // are accepted by some frameworks.
  kDoubleSlashComment,
  // In exceptional cases we need to drop the remaining parsed tokens and
  // reparse the rest of sql.
  kNeedRetokenizing,
  // Embedded SQL inside a string literal.
  kEmbeddedSql,
};

// Holds a state of grouping multiple GoogleSQL tokens into a single formatter
// token.
struct TokenGroupingState {
  // Currently active grouping type. kNone means no grouping is active.
  GroupingType type = GroupingType::kNone;
  // Start position of the currently grouped token in the input file.
  int start_position = -1;
  // End position of the grouped token, if known.
  int end_position = -1;
  // True if the current token starts a statement.
  bool is_statement_start = false;
  // True if we just started parsing tokens and it might be (or might not be) a
  // DEFINE MACRO statement.
  bool may_be_define_macro = false;
  // True if the current tokens are inside DEFINE MACRO statement.
  bool is_define_macro_body = false;

  void Reset() {
    start_position = -1;
    end_position = -1;
    type = GroupingType::kNone;
  }
  bool IsEmpty() { return start_position < 0; }
};

bool MaybeAComment(absl::string_view token) {
  return !token.empty() &&
         (token[0] == '#' || absl::StartsWith(token, "--") ||
          absl::StartsWith(token, "/*") || absl::StartsWith(token, "//"));
}

// Returns true if `parse_token` is a string literal and the previous token
// (`prev_token`) look like a non-googlesql string literal prefix.
bool IsStringLiteralWithNonGoogleSqlPrefix(const ParseToken& parse_token,
                                           const Token& prev_token) {
  if (!parse_token.IsValue()) {
    return false;
  }
  const char start = parse_token.GetImage()[0];
  if (start != '\'' && start != '\"') {
    return false;
  }
  // There should be no spaces between the prefix and the string literal.
  if (TokenEndOffset(prev_token) != TokenStartOffset(parse_token)) {
    return false;
  }
  // T-SQL unicode string literal prefix "N"
  // https://docs.microsoft.com/en-us/sql/t-sql/data-types/constants-transact-sql?view=sql-server-ver15#unicode-strings
  return prev_token.GetImage().length() == 1 && prev_token.GetKeyword() == "N";
}

// Returns true if the given comment string starts a no format region.
bool IsStartOfNoFormatRegion(absl::string_view comment_body) {
  return StrContainsIgnoreCase(comment_body, "SQLFORMAT:OFF");
}

// Returns true if `parse_token` starts a no format region.
bool IsStartOfNoFormatRegion(const ParseToken& parse_token) {
  return parse_token.IsComment() &&
         IsStartOfNoFormatRegion(parse_token.GetImage());
}

// Returns true if given comment string ends a no format region.
bool IsEndOfNoFormatRegion(absl::string_view comment_body) {
  return StrContainsIgnoreCase(comment_body, "SQLFORMAT:ON");
}

// Returns true if `parse_token` may start a SET statement.
bool IsSetStatementStart(const ParseToken& parse_token,
                         const TokenGroupingState& grouping_state) {
  return (grouping_state.is_statement_start ||
          grouping_state.is_define_macro_body) &&
         parse_token.GetKeyword() == "SET";
}

// Returns true if `parse_token` starts a DEFINE statement.
bool IsDefineStatementStart(const ParseToken& parse_token,
                            const TokenGroupingState& grouping_state) {
  return grouping_state.is_statement_start &&
         parse_token.GetKeyword() == "DEFINE";
}

//  Returns true if the `parse_token` starts a BIND statement.
bool IsBindStatementStart(const ParseToken& parse_token,
                          const TokenGroupingState& grouping_state) {
  return grouping_state.is_statement_start &&
         IsBindKeyword(parse_token.GetKeyword());
}

// Returns true if the `parse_token` may start a LOAD or SOURCE statement.
bool IsLoadOrSourceStatementStart(const ParseToken& parse_token,
                                  const ParseToken* prev_token,
                                  const TokenGroupingState& grouping_state,
                                  absl::string_view sql) {
  return (grouping_state.is_statement_start ||
          (grouping_state.is_define_macro_body &&
           CountLineBreaksBefore(parse_token, prev_token, sql) > 0)) &&
         (parse_token.GetKeyword() == "LOAD" ||
          parse_token.GetKeyword() == "SOURCE");
}

// Returns true if the `parse_token` might start a token in curly braces.
bool IsCurlyBracesParameterStart(const ParseToken& parse_token,
                                 absl::Span<const Token> tokens) {
  return parse_token.GetImage() == "{" &&
         (tokens.empty() || tokens.back().GetImage() != "@");
}

// Returns true if the `parse_token` together with the previously parsed token
// might start a jinja block or comment:
// {% ... %} or {# ... #}.
bool IsJinjaExpressionStart(const ParseToken& parse_token,
                            absl::Span<const Token> tokens) {
  return (parse_token.GetImage() == "%" ||
          absl::StartsWith(parse_token.GetImage(), "#")) &&
         !tokens.empty() && tokens.back().GetImage() == "{" &&
         !SpaceBetweenTokensInInput(tokens.back(), parse_token);
}

// Returns true if the `parse_token` starts a description of a legacy ASSERT
// statement: "ASSERT [error message] AS ...".
bool IsLegacyAssertDescriptionStart(const ParseToken& parse_token,
                                    absl::Span<const Token> tokens) {
  if (tokens.empty() || parse_token.GetImage() != "[") {
    return false;
  }
  int t = static_cast<int>(tokens.size()) - 1;
  if (tokens[t].GetKeyword() == "WARNING" && t > 0) {
    --t;
  }
  return tokens[t].GetKeyword() == "ASSERT";
}

// Returns true if the given `parse_token` and `prev_token` start a double slash
// comment.
bool IsDoubleSlashComment(const ParseToken& parse_token,
                          const Token& prev_token) {
  return prev_token.GetImage() == "/" && parse_token.GetImage() == "/" &&
         !SpaceBetweenTokensInInput(prev_token, parse_token);
}

// Returns true if the given `parse_token` is a string literal annotated as an
// embedded SQL string, e.g.: /*sql*/"SELECT 1;"
bool IsEmbeddedSqlStart(const ParseToken& parse_token,
                        const Token& prev_token) {
  return parse_token.IsValue() &&
         parse_token.GetValue().type_kind() == TYPE_STRING &&
         (prev_token.IsEmbeddedSqlAnnotation() ||
          prev_token.IsEmbeddedProtoAnnotation()) &&
         !absl::StripAsciiWhitespace(parse_token.GetValue().string_value())
              .empty() &&
         // Allow only multiline string literals (otherwise, formatter might
         // break a string by adding a line break to it).
         (absl::EndsWith(parse_token.GetImage(), "'''") ||
          absl::EndsWith(parse_token.GetImage(), "\"\"\""));
}

// Updates grouping state with the end of no format region if the given
// `parse_token` ends it.
void MaybeUpdateEndOfNoFormatRegion(const ParseToken& parse_token,
                                    absl::string_view sql,
                                    TokenGroupingState* grouping_state) {
  if (parse_token.IsEndOfInput() ||
      (parse_token.IsComment() &&
       IsEndOfNoFormatRegion(parse_token.GetImage()))) {
    grouping_state->end_position = TokenEndOffset(parse_token);
    return;
  }

  // Check if there is double slash comment with "// SQLFORMAT:ON".
  // This comment is not recognized by tokenizer and becomes multiple tokens.
  if (parse_token.GetKeyword() == "ON") {
    static LazyRE2 kDoubleSlashNoFormatEnd = {"(?i)//.*SQLFORMAT:$"};
    if (kDoubleSlashNoFormatEnd->Match(
            // sql before "ON".
            sql.substr(0, TokenStartOffset(parse_token)),
            grouping_state->start_position, TokenStartOffset(parse_token),
            RE2::UNANCHORED, nullptr, 0)) {
      // "SQLFORMAT:ON" may follow other tokens on the same line, which are
      // still part of the same comment.
      grouping_state->end_position =
          FindLineEnd(sql, TokenEndOffset(parse_token));
    }
  }
}

// Updates grouping state for the `parse_token` that starts a description of a
// legacy ASSERT statement: "ASSERT [error message] AS ...".
void UpdateGroupingStateForLegacyAssertDescription(
    const ParseToken& parse_token, TokenGroupingState* grouping_state) {
  grouping_state->type = GroupingType::kLegacyAssertDescription;
  grouping_state->start_position = TokenStartOffset(parse_token);
}

// Updates grouping state if the `parse_token` starts a legacy SET statement
// "SET variable value".
void MaybeUpdateGroupingStateForSetStatement(
    const ParseToken& parse_token, absl::string_view sql,
    TokenGroupingState* grouping_state) {
  // The pattern catches the remainder of SET statement after the SET keyword.
  // It also has two capture groups:
  // 1st - captures everything up to the right SET statement operand;
  // 2nd - captures the operand. E.g.:
  // SET  parameter   value  ;
  //    [  group 1   ][g.2]
  //    [    entire match    ]
  static LazyRE2 kSetStatementRegex = {
      R"regex((?im)(\s+[a-z0-9_.]+\s+)([^=;\s]+)\s*(?:;|$))regex"};
  // The same but for GoogleSQL-like SET statements: "SET param = value;".
  // Makes sure that 'value' is not an escaped string or a beginning of a
  // multiline statement: requires that the value is followed by a semicolon.
  static LazyRE2 kSetStatementWithEqualsRegex = {
      R"regex((?im)(\s+[a-z0-9_.]+\s*=\s*)([^\s(\[=;'"`]+)\s*;)regex"};

  absl::string_view matched_strings[3];
  if (kSetStatementRegex->Match(sql, TokenEndOffset(parse_token), sql.size(),
                                RE2::ANCHOR_START, matched_strings, 3) ||
      kSetStatementWithEqualsRegex->Match(sql, TokenEndOffset(parse_token),
                                          sql.size(), RE2::ANCHOR_START,
                                          matched_strings, 3)) {
    if (MaybeAComment(matched_strings[2])) {
      // The regexps above might catch a comment instead of the value operand.
      return;
    }
    grouping_state->start_position =
        TokenEndOffset(parse_token) +
        static_cast<int>(matched_strings[1].size());
    grouping_state->end_position = grouping_state->start_position +
                                   static_cast<int>(matched_strings[2].size());
    grouping_state->type = GroupingType::kLegacySetStmt;
  }
}

// Updates grouping state if the `parse_token` starts a legacy DEFINE TABLE
// statement:
// DEFINE TABLE name <<EOF
//  <definition>
// EOF;
void MaybeUpdateGroupingStateForDefineTable(
    const ParseToken& parse_token, absl::string_view sql,
    TokenGroupingState* grouping_state) {
  // The pattern catches the remainder of DEFINE TABLE statement after the
  // DEFINE keyword. It contains a capture group that captures everything before
  // "<<EOF" operator. E.g.:
  // DEFINE TABLE table_name <<EOF
  //       [    group 1     ]
  //       [    entire match     ]
  static LazyRE2 kSetDefineTableRegex = {
      R"regex((?im)(\s+(?:PERMANENT\s+|INLINE\s+)?TABLE\s+\S+\s+)<<EOF)regex"};

  absl::string_view matched_strings[2];
  if (kSetDefineTableRegex->Match(sql, TokenEndOffset(parse_token), sql.size(),
                                  RE2::ANCHOR_START, matched_strings, 2)) {
    grouping_state->start_position =
        TokenEndOffset(parse_token) +
        static_cast<int>(matched_strings[1].size());
    grouping_state->type = GroupingType::kLegacyDefineTableStmt;
  }
}

// Updates grouping state if the `parse_token` starts a Java gORM BIND
// statement: "BIND name type".
void MaybeUpdateGroupingStateForGormBindStatement(
    const ParseToken& parse_token, absl::string_view sql,
    TokenGroupingState* grouping_state) {
  // The pattern catches the remainder of the statement after the BIND keyword:
  // " variable type".
  static LazyRE2 kBindStatementRegex = {
      R"regex((?m)\s+[^;\s]+\s+[^;\s]+\s*$)regex"};

  absl::string_view matched_string;
  if (kBindStatementRegex->Match(sql, TokenEndOffset(parse_token), sql.size(),
                                 RE2::ANCHOR_START, &matched_string, 1)) {
    grouping_state->start_position = TokenStartOffset(parse_token);
    grouping_state->end_position =
        TokenEndOffset(parse_token) + static_cast<int>(matched_string.size());
    grouping_state->type = GroupingType::kGormBindStmt;
  }
}

// Updates grouping state if the `parse_token` starts a "LOAD something" or
// "SOURCE something" statement that might end without a semicolon.
void MaybeUpdateGroupingStateForLoadOrSourceStatement(
    const ParseToken& parse_token, absl::string_view sql,
    TokenGroupingState* grouping_state) {
  // The pattern makes sure that LOAD/SOURCE keyword is followed by a single
  // parameter. It also has two capture groups:
  // 1st - captures spaces before load parameter;
  // 2nd - captures the parameter. E.g.:
  // LOAD     parameter   <end_of_line>
  //     [g.1][  g.2  ]
  //     [ entire match  ]
  static LazyRE2 kLoadStatementRegex = {
      R"regex((?m)(\s+)([^;\s]+)\s*(?:;|$))regex"};

  absl::string_view matched_string[3];
  if (kLoadStatementRegex->Match(sql, TokenEndOffset(parse_token), sql.size(),
                                 RE2::ANCHOR_START, matched_string, 3)) {
    grouping_state->start_position = TokenEndOffset(parse_token) +
                                     static_cast<int>(matched_string[1].size());
    grouping_state->end_position = grouping_state->start_position +
                                   static_cast<int>(matched_string[2].size());
    grouping_state->type = GroupingType::kLegacyLoadOrSourceStmt;
  }
}

// Updates grouping state if the `parse_token` starts a token in curly braces.
// Returns true if the grouping state was changed.
bool MaybeUpdateGroupingStateForCurlyBracesParameter(
    const ParseToken& parse_token, absl::string_view sql,
    TokenGroupingState* grouping_state) {
  absl::string_view sql_substr = sql.substr(TokenEndOffset(parse_token));

  bool forbid_whitespaces = true;
  if ((TokenStartOffset(parse_token) > 0 &&
       sql[TokenStartOffset(parse_token) - 1] == '$') ||
      absl::StartsWith(sql_substr, "{")) {
    // Allow whitespaces inside {} only if it starts with $ or double {{.
    // Otherwise, this might be a proto constructor, which is a valid GoogleSQL.
    forbid_whitespaces = false;
  }
  icu::UnicodeString txt(sql_substr.data(),
                         static_cast<int32_t>(sql_substr.size()));
  int curly_braces_count = 1;
  icu::StringCharacterIterator it(txt);
  while (it.hasNext()) {
    if (it.current32() == '\n') {
      // Don't allow multiline parameters.
      break;
    } else if (forbid_whitespaces && u_isUWhiteSpace(it.current32())) {
      break;
    } else if (it.current32() == ':') {
      // Do not allow colon - this might be a braced proto constructor, where
      // user didn't put any spaces, e.g. "NEW foo{f:true}".
      break;
    } else if (it.current32() == '{') {
      curly_braces_count++;
    } else if (it.current32() == '}') {
      curly_braces_count--;
      if (curly_braces_count == 0) {
        if (!it.hasPrevious()) {
          // This is empty {} - don't treat it as a parameter.
          return false;
        }
        // Found end of the {token} in curly braces.
        grouping_state->start_position = TokenStartOffset(parse_token);
        grouping_state->end_position =
            // Position of txt.begin().
            TokenEndOffset(parse_token) +
            // Distance in bytes from txt.begin().
            DistanceBetweenTwoIndexInBytes(txt, it.startIndex(),
                                           it.getIndex()) +
            // Point after '}'.
            1;
        grouping_state->type = GroupingType::kCurlyBracesParameter;
        return true;
      }
    }
    it.next32();
  }

  return false;
}

// Searches for the end of Jinja expression and updates the grouping state
// accordingly. Note that the expression might be multiline, so if there are
// no closing braces, the rest of the input is considered as one big jinja
// expression, thus disabling formatting for it.
bool UpdateGroupingStateForJinjaExpression(const ParseToken& parse_token,
                                           absl::string_view sql,
                                           TokenGroupingState* grouping_state,
                                           std::vector<Token>& tokens) {
  const char end_char = parse_token.GetImage()[0];
  absl::string_view sql_substr = sql.substr(TokenStartOffset(parse_token) + 1);
  icu::UnicodeString txt(sql_substr.data(),
                         static_cast<int32_t>(sql_substr.size()));
  icu::StringCharacterIterator it(txt);
  bool found_closing_brace = false;
  while (it.hasNext()) {
    // Search for '%}' or '#}'.
    if (it.current32() != end_char) {
      it.next32();
      continue;
    }
    if (!it.hasNext()) {
      break;
    }
    it.next32();
    if (it.current32() == '}') {
      // Found end of the jinja expression; move iterator past '}'.
      it.next32();
      found_closing_brace = true;
      break;
    }
    it.next32();
  }

  if (!found_closing_brace) {
    return false;
  }

  // Position of '{' that is arleady in parsed tokens.
  grouping_state->start_position = TokenStartOffset(tokens.back());
  grouping_state->end_position =
      // Position of txt.begin().
      TokenStartOffset(parse_token) + 1 +
      // Distance in bytes from txt.begin().
      DistanceBetweenTwoIndexInBytes(txt, it.startIndex(), it.getIndex());
  grouping_state->type = GroupingType::kJinjaExpression;
  // Remove '{' from tokens we already parsed - it'll become a part of the
  // jinja expression.
  tokens.pop_back();
  return true;
}

// Updates grouping state with the start and end positions of a double slash
// comment.
void UpdateGroupingStateForDoubleSlashComment(
    const Token& comment_start, absl::string_view sql,
    TokenGroupingState* grouping_state) {
  grouping_state->start_position = TokenStartOffset(comment_start);
  grouping_state->type = GroupingType::kDoubleSlashComment;
  grouping_state->end_position =
      FindLineEnd(sql, TokenStartOffset(comment_start) + 2);
}

// Updates grouping state for an embedded SQL string. This is a special case
// when we need to re-tokenize the contents of the string, so this function
// calculates the range to be re-tokenized.
void UpdateGroupingStateForEmbeddedSql(const ParseToken& embedded_sql,
                                       TokenGroupingState* grouping_state) {
  grouping_state->type = GroupingType::kEmbeddedSql;
  // Find offsets of the string content.
  absl::string_view sql = embedded_sql.GetImage();
  int quotes_length = 1;
  if (absl::EndsWith(sql, "'''") || absl::EndsWith(sql, "\"\"\"")) {
    quotes_length = 3;
  }
  grouping_state->start_position =
      TokenStartOffset(embedded_sql) + quotes_length;
  grouping_state->end_position = TokenEndOffset(embedded_sql) - quotes_length;
  if (sql[0] == 'r' || sql[0] == 'R') {  // Consume raw string prefix if any.
    grouping_state->start_position++;
  }
}

// Creates a googlesql::ParseToken.
ParseToken CreateParseToken(absl::string_view sql, int start_position,
                            int end_position, ParseToken::Kind kind,
                            Value value = Value()) {
  ParseLocationRange location;
  location.set_start(ParseLocationPoint::FromByteOffset(start_position));
  location.set_end(ParseLocationPoint::FromByteOffset(end_position));
  std::string image(sql.substr(start_position, end_position - start_position));
  // The calling code is operating on non-SQL tokens. Presumably non-SQL tokens
  // may be considered non-adjacent.
  bool adjacent_to_prior_token = false;
  if (kind == ParseToken::VALUE) {
    return ParseToken(location, adjacent_to_prior_token, std::move(image), kind,
                      std::move(value));
  } else {
    return ParseToken(location, adjacent_to_prior_token, std::move(image),
                      kind);
  }
}

// Creates an EndOfInput token.
ParseToken EndOfInputToken(int position) {
  ParseLocationRange location;
  location.set_start(ParseLocationPoint::FromByteOffset(position));
  location.set_end(ParseLocationPoint::FromByteOffset(position));
  return ParseToken(location, /*adjacent_to_prior_token=*/true, "",
                    ParseToken::END_OF_INPUT);
}

// Returns i'th non comment token in `tokens` or nullptr if such token doesn't
// exist.
const Token* NonCommentToken(absl::Span<const Token> tokens, int i) {
  int t = 0;
  for (const auto& token : tokens) {
    if (token.IsComment()) {
      continue;
    }
    if (t == i) {
      return &token;
    }
    ++t;
  }
  return nullptr;
}

// Helper function to inject end of input token after statements that might end
// without a semicolon.
template <class TokenClass>
TokenGroupingState MaybeInjectEndOfInput(TokenClass& parse_token,
                                         absl::string_view sql,
                                         TokenGroupingState grouping_state,
                                         std::vector<Token>& tokens) {
  if (!(parse_token.IsEndOfInput() || parse_token.GetKeyword() == ";" ||
        // Do not inject end of input if we are inside DEFINE MACRO - it is
        // often used to combine multiple statements that do not require a
        // trailing semicolon.
        grouping_state.is_define_macro_body)) {
    tokens.emplace_back(EndOfInputToken(grouping_state.end_position));
    grouping_state.Reset();
  } else {
    grouping_state.Reset();
    grouping_state =
        MaybeMoveParseTokenIntoTokens(parse_token, sql, grouping_state, tokens);
  }
  return grouping_state;
}

// Moves given `parse_token` into `tokens` possibly grouping multiple tokens
// into a single one.
template <class TokenClass>
TokenGroupingState MaybeMoveParseTokenIntoTokens(
    TokenClass& parse_token, absl::string_view sql,
    TokenGroupingState grouping_state, std::vector<Token>& tokens) {
  switch (grouping_state.type) {
    case GroupingType::kNone: {
      // No grouping is active at the moment. See if the current parse_token
      // starts a group.
      if (IsStartOfNoFormatRegion(parse_token)) {
        // parse_token starts no formatting region.
        grouping_state.type = GroupingType::kNoFormatBlock,
        grouping_state.start_position = TokenStartOffset(parse_token);
        // Ignore current parse_token - it will become a part of a bigger
        // NO_FORMAT token once we find the end of it.
        return grouping_state;
      }

      if (IsSetStatementStart(parse_token, grouping_state)) {
        MaybeUpdateGroupingStateForSetStatement(parse_token, sql,
                                                &grouping_state);
      } else if (IsDefineStatementStart(parse_token, grouping_state)) {
        MaybeUpdateGroupingStateForDefineTable(parse_token, sql,
                                               &grouping_state);
      } else if (IsBindStatementStart(parse_token, grouping_state)) {
        MaybeUpdateGroupingStateForGormBindStatement(parse_token, sql,
                                                     &grouping_state);
      } else if (IsLoadOrSourceStatementStart(
                     parse_token, tokens.empty() ? nullptr : &tokens.back(),
                     grouping_state, sql)) {
        MaybeUpdateGroupingStateForLoadOrSourceStatement(parse_token, sql,
                                                         &grouping_state);
      } else if (IsCurlyBracesParameterStart(parse_token, tokens)) {
        if (MaybeUpdateGroupingStateForCurlyBracesParameter(parse_token, sql,
                                                            &grouping_state)) {
          // Ignore the `parse_token` - it will be a part of curly braces.
          return grouping_state;
        }
      } else if (IsJinjaExpressionStart(parse_token, tokens)) {
        if (UpdateGroupingStateForJinjaExpression(parse_token, sql,
                                                  &grouping_state, tokens)) {
          // Ignore the `parse_token` - it will be a part of jinja expression.
          return grouping_state;
        }
      } else if (IsLegacyAssertDescriptionStart(parse_token, tokens)) {
        UpdateGroupingStateForLegacyAssertDescription(parse_token,
                                                      &grouping_state);
        // Ignore the `parse_token` - it will become a part of assert
        // description.
        return grouping_state;
      } else if (!tokens.empty() && IsStringLiteralWithNonGoogleSqlPrefix(
                                        parse_token, tokens.back())) {
        // Merge current string literal token with the previous token that was
        // already parsed as identifier, but in fact is a string literal prefix.
        ParseToken string_with_prefix = CreateParseToken(
            sql, TokenStartOffset(tokens.back()), TokenEndOffset(parse_token),
            ParseToken::VALUE, parse_token.GetValue());
        tokens.pop_back();
        tokens.emplace_back(string_with_prefix);
        return grouping_state;
      } else if (!tokens.empty() &&
                 IsDoubleSlashComment(parse_token, tokens.back())) {
        UpdateGroupingStateForDoubleSlashComment(tokens.back(), sql,
                                                 &grouping_state);
        // Remove '/' from already processed tokens: it'll become a part of the
        // comment token.
        tokens.pop_back();
        return grouping_state;
      } else if (!tokens.empty() &&
                 IsEmbeddedSqlStart(parse_token, tokens.back())) {
        UpdateGroupingStateForEmbeddedSql(parse_token, &grouping_state);
        tokens.back().SetType(Token::Type::STRING_ANNOTATION_COMMENT);
        return grouping_state;
      }
      break;
    }

    case GroupingType::kNoFormatBlock: {
      // Look for the end of the no format region.
      if (grouping_state.end_position < 0) {
        MaybeUpdateEndOfNoFormatRegion(parse_token, sql, &grouping_state);
      }
      if (grouping_state.end_position > 0 &&
          TokenEndOffset(parse_token) >= grouping_state.end_position) {
        // Reached the end of no format region.
        tokens.emplace_back(CreateParseToken(sql, grouping_state.start_position,
                                             grouping_state.end_position,
                                             ParseToken::COMMENT));
        tokens.back().SetType(Token::Type::NO_FORMAT);
        grouping_state.Reset();
        if (TokenStartOffset(parse_token) >= TokenEndOffset(tokens.back())) {
          // `parse_token` is actually the first token _after_ no format region.
          grouping_state = MaybeMoveParseTokenIntoTokens(
              parse_token, sql, grouping_state, tokens);
        }
      }
      // Ignore all tokens until we find the end of no format region.
      return grouping_state;
    }

    case GroupingType::kLegacySetStmt: {
      // If the set statement operand is multiline (grouping state end position
      // points at the end of line, hence will be in the middle of operand
      // token), update grouping state end position to include the entire token.
      if (TokenStartOffset(parse_token) < grouping_state.end_position &&
          TokenEndOffset(parse_token) > grouping_state.end_position) {
        grouping_state.end_position = TokenEndOffset(parse_token);
      }
      // Look for the end of set statement operand.
      if (parse_token.IsEndOfInput() ||
          TokenStartOffset(parse_token) >= grouping_state.end_position) {
        // Reached first token after the set statement operand.
        tokens.emplace_back(CreateParseToken(sql, grouping_state.start_position,
                                             grouping_state.end_position,
                                             ParseToken::IDENTIFIER));
        tokens.back().SetType(Token::Type::LEGACY_SET_OPERAND);
        // Legacy SET statements are allowed to end without a trailing
        // semicolon. Force tokenizer loop to stop after the SET statement by
        // injecting an EndOfInput token.
        return MaybeInjectEndOfInput(parse_token, sql, grouping_state, tokens);
      }

      // Skip tokens that are part of set statement operand.
      if (TokenStartOffset(parse_token) >= grouping_state.start_position &&
          TokenEndOffset(parse_token) <= grouping_state.end_position) {
        return grouping_state;
      }
      break;
    }

    case GroupingType::kLegacyDefineTableStmt: {
      // We enter legacy define table body mode when we see a statement
      // DEFINE TABLE foo <<EOF
      //   ...
      // EOF;
      // There are 3 stages:
      // 1. We pass through tokens as is until we reach "<<EOF" at
      //    grouping_state.start_position.
      // 2. We skip tokens until we reach the end of the statement.
      // 3. Once end of statement is reached - create a single token for the
      //    entire table body "<<EOF...EOF".
      if (parse_token.IsEndOfInput() ||
          (parse_token.GetKeyword() == "EOF" &&
           // Check the token start to make sure we are looking at the second
           // EOF token.
           TokenStartOffset(parse_token) > grouping_state.start_position + 2)) {
        // Went past the define table body: save it as a single token.
        tokens.emplace_back(CreateParseToken(sql, grouping_state.start_position,
                                             TokenEndOffset(parse_token),
                                             ParseToken::IDENTIFIER));
        tokens.back().SetType(Token::Type::LEGACY_DEFINE_TABLE_BODY);
        grouping_state.Reset();
        if (parse_token.IsEndOfInput()) {
          // Proceed to default token move to add EndOfInput token.
          break;
        }
        // We already copied `parse_token` as part of a group token.
        return grouping_state;
      }

      if (TokenStartOffset(parse_token) >= grouping_state.start_position) {
        // Skip tokens that are part of the define table body.
        return grouping_state;
      }

      if (TokenEndOffset(parse_token) > grouping_state.start_position) {
        // Token starts before <<EOF, but ends after it. This can happen when
        // EOF appears to be a part of the comment, e.g.:
        // DEFINE TABLE #comment <<EOF
        //              [parse_token ]
        // It means we marked the line as legacy define table statement by
        // mistake and should reset.
        grouping_state.Reset();
      }

      break;
    }

    case GroupingType::kGormBindStmt: {
      // Look for the end of the bind statement passing through all tokens.
      if (TokenStartOffset(parse_token) >= grouping_state.end_position) {
        // Reached first token after the bind statement: inject end of input.
        return MaybeInjectEndOfInput(parse_token, sql, grouping_state, tokens);
      }
      break;
    }

    case GroupingType::kLegacyLoadOrSourceStmt: {
      // Look for the end of the LOAD/SOURCE statement and combine all tokens
      // into one.
      if (parse_token.IsEndOfInput() ||
          TokenStartOffset(parse_token) >= grouping_state.end_position) {
        // Reached first token after the LOAD statement: combine everything into
        // a single token.
        tokens.emplace_back(CreateParseToken(sql, grouping_state.start_position,
                                             grouping_state.end_position,
                                             ParseToken::IDENTIFIER));
        // LOAD/SOURCE statements are allowed to end without a trailing
        // semicolon. Force tokenizer loop to stop after the statement by
        // injecting an EndOfInput token.
        return MaybeInjectEndOfInput(parse_token, sql, grouping_state, tokens);
      }

      // Ignore the token - it'll become a part of a bigger one.
      return grouping_state;
    }

    case GroupingType::kCurlyBracesParameter: {
      if (TokenEndOffset(parse_token) < grouping_state.end_position) {
        // Ignore all tokens within curly braces.
        return grouping_state;
      }
      tokens.emplace_back(CreateParseToken(sql, grouping_state.start_position,
                                           TokenEndOffset(parse_token),
                                           ParseToken::IDENTIFIER));
      tokens.back().SetType(Token::Type::CURLY_BRACES_TEMPLATE);
      grouping_state.Reset();
      return grouping_state;
    }

    case GroupingType::kJinjaExpression: {
      if (!parse_token.IsEndOfInput() &&
          TokenStartOffset(parse_token) < grouping_state.end_position) {
        // Ignore all tokens within curly braces.
        return grouping_state;
      }
      // Found first token after the jinja expression. Create a single token
      // for the entire expression, and rerun this function to check if the
      // current token starts a new special sequence.
      tokens.emplace_back(CreateParseToken(sql, grouping_state.start_position,
                                           grouping_state.end_position,
                                           ParseToken::COMMENT));
      grouping_state.Reset();
      return MaybeMoveParseTokenIntoTokens(parse_token, sql, grouping_state,
                                           tokens);
    }

    case GroupingType::kLegacyAssertDescription: {
      if (!parse_token.IsEndOfInput() && parse_token.GetImage() != "]") {
        // Ignore all tokens within square brackets.
        return grouping_state;
      }
      tokens.emplace_back(CreateParseToken(sql, grouping_state.start_position,
                                           TokenEndOffset(parse_token),
                                           ParseToken::IDENTIFIER));
      grouping_state.Reset();
      return grouping_state;
    }

    case GroupingType::kDoubleSlashComment: {
      if (parse_token.IsEndOfInput() ||
          TokenEndOffset(parse_token) > grouping_state.end_position) {
        // Found the first token after the // comment.
        ParseToken comment_token =
            CreateParseToken(sql, grouping_state.start_position,
                             grouping_state.end_position, ParseToken::COMMENT);
        grouping_state.Reset();

        if (TokenStartOffset(parse_token) < TokenEndOffset(comment_token)) {
          // Got a multiline token that starts inside the //-comment and ends
          // somewhere on a different line, e.g.:
          //  // '''
          //     SELECT 1;
          grouping_state.type = GroupingType::kNeedRetokenizing;
          grouping_state.start_position = TokenEndOffset(comment_token);
          tokens.emplace_back(std::move(comment_token));
          return grouping_state;
        }

        // The comment might contain SQLFORMAT:OFF, so process it once again:
        grouping_state = MaybeMoveParseTokenIntoTokens(comment_token, sql,
                                                       grouping_state, tokens);

        // Now process parse_token again, given the updated grouping_state.
        return MaybeMoveParseTokenIntoTokens(parse_token, sql, grouping_state,
                                             tokens);
      } else {
        // Ignore tokens that are part of the comment.
        return grouping_state;
      }
    }

    case GroupingType::kNeedRetokenizing: {
      return grouping_state;
    }

    case GroupingType::kEmbeddedSql: {
      return grouping_state;
    }
  }

  // Default action: copy the token to the result as is.
  tokens.emplace_back(std::move(parse_token));
  if (!tokens.back().IsComment()) {
    grouping_state.is_statement_start = false;
  }
  if (grouping_state.may_be_define_macro &&
      NonCommentToken(tokens, 2) != nullptr) {
    grouping_state.may_be_define_macro = false;
    if (NonCommentToken(tokens, 0)->GetKeyword() == "DEFINE" &&
        NonCommentToken(tokens, 1)->GetKeyword() == "MACRO") {
      grouping_state.is_define_macro_body = true;
    }
  }
  return grouping_state;
}

bool IsDoubleQuotedStringOrByteLiteral(absl::string_view str) {
  return !str.empty() && str.back() == '"';
}

// Converts the enclosing double-quotes of the string/bytes literal into single-
// quotes in-place only if it is safe and doesn't require escaping. Otherwise,
// this is a no-op.
void ConvertEnclosingDoubleQuotesToSingleIfSafe(std::string& image) {
  absl::string_view content;
  int enclosing_quotes_length;
  size_t prefix_length = image.find_first_of('"');
  if (absl::EndsWith(image, "\"\"\"")) {
    enclosing_quotes_length = 3;
    content = absl::ClippedSubstr(image, prefix_length + 3,
                                  image.size() - prefix_length - 6);
    // Don't convert the enclosing triple quotes if the bare content contains
    // triple single quotes or starts/ends with a single quote, because it
    // requires escaping.
    if (absl::StartsWith(content, "'") || absl::EndsWith(content, "'") ||
        absl::StrContains(content, "'''")) {
      return;
    }
  } else {
    enclosing_quotes_length = 1;
    content = absl::ClippedSubstr(image, prefix_length + 1,
                                  image.size() - prefix_length - 2);
    // Don't convert the enclosing quotes if the bare content contains
    // single quotes, because it requires escaping.
    if (absl::StrContains(content, "'")) {
      return;
    }
  }
  image.replace(prefix_length, enclosing_quotes_length, enclosing_quotes_length,
                '\'');
  image.replace(image.size() - enclosing_quotes_length, enclosing_quotes_length,
                enclosing_quotes_length, '\'');
}

// Creates a token that represents opening or closing quotes of a string
// literal, whose content is parsed as SQL. In this case, quotes of the string
// act as parentheses for the contents inside.
Token CreateTokenForQuotes(absl::string_view sql, int start_position,
                           int end_position, Token::Type type) {
  // Omit 'r' or 'b' that might appear before opening quotes of a string
  // literal, so that Token::GetKeyword() returns quotes without this prefix,
  // but Token::GetImage() still contains the full string.
  int quotes_start = start_position;
  while (quotes_start < end_position && sql[quotes_start] != '\'' &&
         sql[quotes_start] != '"') {
    quotes_start++;
  }
  absl::string_view quotes =
      sql.substr(quotes_start, end_position - quotes_start);
  Token token(
      CreateParseToken(sql, start_position, end_position, ParseToken::KEYWORD),
      quotes);
  token.SetType(type);
  return token;
}

// Parses contents of a string literal as SQL and adds the resulting tokens to
// the `tokens` vector.
absl::Status ParseEmbeddedSql(const ParseToken& string_literal,
                              absl::string_view sql,
                              const TokenGroupingState& grouping_state,
                              const FormatterOptions& options,
                              std::vector<Token>& tokens) {
  // Create a token that represents opening quotes of the string literal.
  Token::Type open_bracket_type = Token::Type::OPEN_BRACKET;
  if (tokens.back().IsEmbeddedProtoAnnotation()) {
    // We parse textproto as SQL as well: GoogleSQL has braced constructors
    // syntax that looks very similar to textproto.
    open_bracket_type = Token::Type::BRACED_CONSTR_OPEN_BRACKET;
  }
  tokens.push_back(CreateTokenForQuotes(sql, TokenStartOffset(string_literal),
                                        grouping_state.start_position,
                                        open_bracket_type));

  // Parse substring inside the quotes. We preserve all bytes before the string
  // start to get the correct byte offsets.
  absl::string_view embedded_sql = sql.substr(0, grouping_state.end_position);
  ParseResumeLocation embedded_location =
      ParseResumeLocation::FromStringView(embedded_sql);
  embedded_location.set_byte_position(grouping_state.start_position);
  // Do not format structured strings inside structured strings recursively.
  FormatterOptions embedded_options = options;
  embedded_options.FormatStructuredStrings(false);
  while (embedded_location.byte_position() < grouping_state.end_position) {
    GOOGLESQL_ASSIGN_OR_RETURN(
        std::vector<Token> embedded_tokens,
        TokenizeNextStatement(&embedded_location, embedded_options));
    if (embedded_tokens.empty()) {
      break;
    }
    for (auto&& token : embedded_tokens) {
      tokens.push_back(std::move(token));
    }
  }
  // Create a token that represents closing quotes of the string literal.
  const Token::Type close_bracket_type =
      open_bracket_type == Token::Type::OPEN_BRACKET
          ? Token::Type::CLOSE_BRACKET
          : Token::Type::BRACED_CONSTR_CLOSE_BRACKET;
  tokens.push_back(CreateTokenForQuotes(sql, grouping_state.end_position,
                                        TokenEndOffset(string_literal),
                                        close_bracket_type));
  return absl::OkStatus();
}

}  // namespace

Token ParseInvalidToken(ParseResumeLocation* resume_location,
                        int end_position) {
  // Iterate over input sql as a UTF8 string.
  icu::UnicodeString txt =
      RemainingInputAsUnicode(*resume_location, end_position);

  icu::StringCharacterIterator start(txt);
  icu::StringCharacterIterator it = SkipAllWhitespaces(start);
  if (!it.hasNext()) {
    // Reached end of input: construct end of input token.
    ParseLocationRange location =
        ParseLocationRangeFromUtfIterators(*resume_location, txt, it, it);
    // Update resume_location if needed.
    if (it.getIndex() != it.startIndex()) {
      resume_location->set_byte_position(
          ByteOffsetFromUtfIterator(*resume_location, txt, it));
    }
    return Token(ParseToken(location, /*adjacent_to_prior_token=*/true, "",
                            ParseToken::END_OF_INPUT));
  }

  const icu::StringCharacterIterator token_start = it;
  const InvalidTokenType type = DetectTokenType(&it);

  it = FindTokenEnd(it, type);
  icu::StringCharacterIterator token_end = it;

  // Trim trailing whitespaces.
  while (u_isUWhiteSpace(token_end.previous32()) &&
         token_end.getIndex() > token_start.getIndex()) {
  }
  token_end.next32();

  std::string image;
  txt.tempSubStringBetween(token_start.getIndex(), token_end.getIndex())
      .toUTF8String<std::string>(image);

  ParseLocationRange token_location = ParseLocationRangeFromUtfIterators(
      *resume_location, txt, token_start, token_end);
  resume_location->set_byte_position(
      ByteOffsetFromUtfIterator(*resume_location, txt, it));

  // Mark the token as identifier even if we know it was a string literal or a
  // comment: this is a broken token and the type is not important.
  Token token(ParseToken(token_location, /*adjacent_to_prior_token=*/false,
                         std::move(image), ParseToken::IDENTIFIER));
  token.SetType(Token::Type::INVALID_TOKEN);
  return token;
}

absl::StatusOr<std::vector<Token>> TokenizeStatement(
    absl::string_view sql, const FormatterOptions& options) {
  ParseResumeLocation parse_location = ParseResumeLocation::FromStringView(sql);
  GOOGLESQL_ASSIGN_OR_RETURN(std::vector<Token> tokens,
                   TokenizeNextStatement(&parse_location, options));
  if (parse_location.byte_position() < sql.size()) {
    return absl::InvalidArgumentError(absl::StrCat(
        "TokenizeStatement(absl::string_view sql, bool allow_invalid_tokens) "
        "should be used only on a string containing a single statement. Use "
        "TokenizeNextStatement in a loop to parse a multi-statement sql. ",
        sql.size() - parse_location.byte_position(),
        " bytes are left unparsed in sql:",
        sql.substr(parse_location.byte_position())));
  }
  return tokens;
}

// Replace tokens with missing whitespace at the beginning (after the comment
// symbol) or trailing whitespace.
void FixupCommentWhitespace(std::vector<Token>& tokens) {
  // Fixup comment leading and trailing whitespace.
  // Make sure there's a single space after the comment symbol.
  for (Token& token : tokens) {
    if (!token.IsComment()) {
      continue;
    }
    const absl::string_view image =
        absl::StripTrailingAsciiWhitespace(token.GetImage());
    if (image.empty()) {
      continue;
    }
    std::string new_image;
    if (image.length() > 1 && image[0] == '#' && image[1] != '#' &&
        image[1] != ' ') {
      new_image = absl::StrCat("# ", image.substr(1));
    } else if (image.length() > 2 && absl::StartsWith(image, "--") &&
               image[2] != '-' && image[2] != ' ') {
      new_image = absl::StrCat("-- ", image.substr(2));
    }
    if (!new_image.empty()) {
      // Create a token with updated image.
      token = Token(token, new_image, new_image);
      continue;
    }
  }
}

absl::StatusOr<std::vector<Token>> TokenizeNextStatement(
    ParseResumeLocation* parse_location, const FormatterOptions& options) {
  LanguageOptions language_options;
  language_options.EnableMaximumLanguageFeaturesForDevelopment();
  // TODO Allow macros as well
  ParseTokenOptions parser_options{.stop_at_end_of_statement = true,
                                   .include_comments = true,
                                   .language_options = language_options};

  std::vector<Token> tokens;
  std::vector<ParseToken> parse_tokens;
  TokenGroupingState grouping_state{.is_statement_start = true,
                                    .may_be_define_macro = true};
  do {
    parse_tokens.clear();
    const absl::Status parse_status =
        GetParseTokens(parser_options, parse_location, &parse_tokens);
    // Convert GoogleSQL tokens in Formatter's own tokens.
    tokens.reserve(tokens.size() + parse_tokens.size());
    for (auto&& parse_token : parse_tokens) {
      grouping_state = MaybeMoveParseTokenIntoTokens(
          parse_token, parse_location->input(), grouping_state, tokens);
      if (!tokens.empty() && tokens.back().IsEndOfInput()) {
        break;
      }
      if (grouping_state.type == GroupingType::kNeedRetokenizing) {
        parse_location->set_byte_position(grouping_state.start_position);
        grouping_state.Reset();
        break;
      }
      if (grouping_state.type == GroupingType::kEmbeddedSql) {
        if (options.IsFormattingStructuredStrings()) {
          GOOGLESQL_RETURN_IF_ERROR(ParseEmbeddedSql(parse_token, parse_location->input(),
                                           grouping_state, options, tokens));
        } else {
          // Do not reparse the string literal.
          tokens.emplace_back(std::move(parse_token));
        }
        grouping_state.Reset();
      }
    }
    if (!parse_status.ok()) {
      if (!options.IsAllowedInvalidTokens()) {
        return parse_status;
      }
      // GoogleSQL tokenizer couldn't parse any tokens at all. Unblock it by
      // parsing invalid token manually.
      if (parse_tokens.empty()) {
        const int invalid_token_max_end_position =
            grouping_state.end_position > parse_location->byte_position()
                ? grouping_state.end_position
                : -1;
        Token invalid_token =
            ParseInvalidToken(parse_location, invalid_token_max_end_position);
        grouping_state = MaybeMoveParseTokenIntoTokens(
            invalid_token, parse_location->input(), grouping_state, tokens);
      }
    }
    // Stop if we reached end of input, or we reached ';', which is not part of
    // a grouped token (e.g., no format section).
  } while (tokens.empty() ||
           !(tokens.back().IsEndOfInput() ||
             (grouping_state.IsEmpty() && tokens.back().GetKeyword() == ";")));

  if (tokens.back().IsEndOfInput()) {
    parse_location->set_byte_position(TokenEndOffset(tokens.back()));
    tokens.pop_back();
  }

  if (!tokens.empty() && tokens.back().GetKeyword() == ";") {
    ParseEndOfLineCommentIfAny(parse_location, tokens);
  }

  // Consume spaces after the end of the statement if any.
  SkipAllWhitespaces(parse_location);

  if (!tokens.empty()) {
    if (options.IsFormattingComments()) {
      FixupCommentWhitespace(tokens);
    }
    MarkAndFixUpAllMultilineTokens(tokens, options);
    CountLineBreaksAroundTokens(*parse_location, tokens);
  }

  return tokens;
}

bool SpaceBetweenTokensInInput(const ParseToken& left,
                               const ParseToken& right) {
  if (TokenEndOffset(left) == TokenStartOffset(right) &&
      left.GetLocationRange().end().filename() !=
          right.GetLocationRange().start().filename()) {
    // The tokens come from different files and their relative offsets are
    // meaningless. For formatting purposes, assume different files consistitues
    // whitespace between the tokens.
    ABSL_LOG(ERROR) << "For now, this path is only expected to be relevant when "
                << "the macro expander is active. The formatter does not use "
                << "macro expansion, so we don't expect this path to be taken.";
    return true;
  }
  return TokenEndOffset(left) < TokenStartOffset(right);
}

bool Token::IsOneOf(std::initializer_list<Type> types) const {
  for (const auto type : types) {
    if (type_ == type) {
      return true;
    }
  }
  return false;
}

bool Token::IsInlineComment() const {
  return IsComment() && GetLineBreaksAfter() == 0;
}

bool Token::IsEndOfLineComment() const {
  return IsComment() && GetLineBreaksBefore() == 0 && GetLineBreaksAfter() > 0;
}

bool Token::IsSlashStarComment() const {
  return IsComment() && absl::StartsWith(GetImage(), "/*");
}

namespace {
bool CanBeReservedKeyword(absl::string_view keyword) {
  const parser::KeywordInfo* info = parser::GetKeywordInfo(keyword);
  return info != nullptr && info->CanBeReserved();
}
}  // namespace

bool Token::IsReservedKeyword() const {
  // For formatting purposes, treat all conditionally reserved tokens as
  // reserved.
  return !IsIdentifier() && IsKeyword() &&
         !IsOneOf({Type::KEYWORD_AS_IDENTIFIER_FRAGMENT,
                   Type::COMPLEX_TOKEN_CONTINUATION,
                   Type::MACRO_NAME_IN_DEFINE_STMT,
                   Type::TABLE_NAME_IN_DEFINE_STMT}) &&
         CanBeReservedKeyword(GetKeyword());
}

bool Token::IsNonPunctuationKeyword() const {
  return IsMacroCall() || (IsKeyword() && absl::ascii_isalpha(GetImage()[0]));
}

bool Token::IsMacroCall() const { return IsKeyword() && GetImage()[0] == '$'; }

bool Token::IsCaseExprKeyword() const {
  static const auto* kCaseClauseKeywords =
      new googlesql_base::flat_set<absl::string_view>({"ELSE", "END", "THEN", "WHEN"});
  return !Is(Type::KEYWORD_AS_IDENTIFIER_FRAGMENT) &&
         kCaseClauseKeywords->contains(GetKeyword());
}

bool Token::IsStringLiteral() const {
  return IsValue() &&
         GetValue().type_kind() == googlesql::TypeKind::TYPE_STRING;
}

bool Token::IsBytesLiteral() const {
  return IsValue() && GetValue().type_kind() == googlesql::TypeKind::TYPE_BYTES;
}

bool Token::IsOpenAngleBracket() const {
  return Is(Type::OPEN_BRACKET) && GetKeyword() == "<";
}

bool Token::IsCloseAngleBracket() const {
  return Is(Type::CLOSE_BRACKET) && GetKeyword() == ">";
}

bool Token::IsEmbeddedSqlAnnotation() const {
  return IsSlashStarComment() && StrContainsIgnoreCase(GetImage(), "/*sql*/");
}

bool Token::IsEmbeddedProtoAnnotation() const {
  return IsSlashStarComment() &&
         (StrContainsIgnoreCase(GetImage(), "/*proto*/") ||
          StrContainsIgnoreCase(GetImage(), "/*txtpb*/"));
}

bool Token::MayBeIdentifier() const {
  return !UsedAsKeyword() &&
         (IsIdentifier() || Is(Token::Type::KEYWORD_AS_IDENTIFIER_FRAGMENT) ||
          // Tokenizer marks some non-reserved-keyword tokens as keywords, even
          // if they can be identifiers.
          (IsNonPunctuationKeyword() && !IsReservedKeyword()));
}

bool Token::MayBeStartOfIdentifier() const {
  return GetImage()[0] == '@' || MayBeIdentifier();
}

bool Token::MayBeIdentifierContinuation(const Token& prev_token) const {
  if (IsInlineComment()) {
    return true;
  }
  if (Is(Type::COMPLEX_TOKEN_CONTINUATION)) {
    return true;
  }
  if (GetKeyword() == ".") {
    return prev_token.GetKeyword() != ".";
  }
  if (MayBeIdentifier()) {
    return prev_token.Is(Token::Type::OPEN_PROTO_EXTENSION_PARENTHESIS) ||
           prev_token.GetImage()[0] == '@' || prev_token.GetKeyword() == "." ||
           prev_token.GetKeyword() == ":";
  }

  return IsOneOf({Type::OPEN_PROTO_EXTENSION_PARENTHESIS,
                  Type::CLOSE_PROTO_EXTENSION_PARENTHESIS,
                  Type::CURLY_BRACES_TEMPLATE}) ||
         GetKeyword() == ":";
}

std::string Token::ImageForPrinting(const FormatterOptions& options,
                                    bool is_first_token, int new_column,
                                    int original_column) const {
  std::string result;

  if (options.IsCapitalizeFunctions() && Is(Type::BUILTIN_FUNCTION)) {
    return std::string(GetKeyword());
  }

  if (options.IsCapitalizeKeywords() && !IsMacroCall()) {
    if (IsReservedKeyword() || UsedAsKeyword() || Is(Type::TOP_LEVEL_KEYWORD)) {
      // When the token is a reserved keyword, we use a canonical uppercase
      // representation instead.
      result = GetKeyword();
      if (!result.empty() || GetImage().empty()) {
        // Protect from accidentally marking a non-keyword token as keyword.
        // E.g., GetKeyword() returns empty string for a string literal.
        return std::string(GetKeyword());
      }
    }

    if (Is(Type::LEGACY_DEFINE_TABLE_BODY)) {
      // Capitalize EOF keywords on both ends of table body "<<EOF...EOF".
      absl::string_view image = absl::StripAsciiWhitespace(GetImage());
      const bool trimmed_prefix = ConsumeCasePrefix(&image, "<<eof");
      const bool trimmed_suffix = ConsumeCaseSuffix(&image, "eof");
      if (trimmed_prefix) {
        result.append("<<EOF");
      }
      result.append(image);
      if (trimmed_suffix) {
        result.append("EOF");
      }
      return result;
    }
  }

  // We use GetImage() here to preserve the input.
  result = std::string(absl::StripAsciiWhitespace(GetImage()));

  if (options.IsEnforcingSingleQuotes() && IsValue() &&
      IsDoubleQuotedStringOrByteLiteral(result)) {
    ConvertEnclosingDoubleQuotesToSingleIfSafe(result);
    return result;
  }

  if (IsComment() && IsMultiline() && is_first_token) {
    // This is a multi-line token, we need to re-align the lines within the
    // comment to the correct number of spaces.
    if (original_column - 1 != new_column) {
      result = absl::StrReplaceAll(
          result, {{"\n" + std::string(original_column - 1, ' '),
                    "\n" + std::string(new_column, ' ')},
                   {"\r" + std::string(original_column - 1, ' '),
                    "\r" + std::string(new_column, ' ')}});
    }
  }
  return result;
}

TokensView TokensView::FromTokens(std::vector<Token>* tokens) {
  TokensView view;
  view.Reserve(tokens->size());
  for (auto& token : *tokens) {
    view.Add(&token);
  }
  return view;
}

void TokensView::Reserve(size_t reserve_size) {
  all_.reserve(reserve_size);
  without_comments_.reserve(reserve_size);
}

void TokensView::Add(Token* token) {
  all_.push_back(token);
  if (!token->IsComment()) {
    without_comments_.push_back(token);
  }
}

OperatorPrecedenceEnum OperatorPrecedenceLevel(const Token& token) {
  // Please keep the list ordered by precedence.
  static const auto* precedence =
      new absl::flat_hash_map<absl::string_view, OperatorPrecedenceEnum>(
          {{"~", OperatorPrecedenceEnum::kUnaryOrBitwiseNegative},
           {"*", OperatorPrecedenceEnum::kMultiplicationOrDivision},
           {"/", OperatorPrecedenceEnum::kMultiplicationOrDivision},
           {"+", OperatorPrecedenceEnum::kAdditionOrSubtraction},
           {"-", OperatorPrecedenceEnum::kAdditionOrSubtraction},
           {"||", OperatorPrecedenceEnum::kAdditionOrSubtraction},
           {">>", OperatorPrecedenceEnum::kBitwiseShift},
           {"<<", OperatorPrecedenceEnum::kBitwiseShift},
           {"&", OperatorPrecedenceEnum::kBitwiseAnd},
           {"^", OperatorPrecedenceEnum::kBitwiseXor},
           {"|", OperatorPrecedenceEnum::kBitwiseOr},
           {"=", OperatorPrecedenceEnum::kComparisonOperator},
           {"<", OperatorPrecedenceEnum::kComparisonOperator},
           {">", OperatorPrecedenceEnum::kComparisonOperator},
           {"<=", OperatorPrecedenceEnum::kComparisonOperator},
           {">=", OperatorPrecedenceEnum::kComparisonOperator},
           {"!=", OperatorPrecedenceEnum::kComparisonOperator},
           {"<>", OperatorPrecedenceEnum::kComparisonOperator},
           {"LIKE", OperatorPrecedenceEnum::kComparisonOperator},
           {"BETWEEN", OperatorPrecedenceEnum::kComparisonOperator},
           {"IN", OperatorPrecedenceEnum::kComparisonOperator},
           {"IS", OperatorPrecedenceEnum::kComparisonOperator},
           {"NOT", OperatorPrecedenceEnum::kLogicalNot},
           {"AND", OperatorPrecedenceEnum::kLogicalAnd},
           {"OR", OperatorPrecedenceEnum::kLogicalOr}});

  if (token.Is(Token::Type::COMPARISON_OPERATOR)) {
    return OperatorPrecedenceEnum::kComparisonOperator;
  }
  auto it = precedence->find(token.GetKeyword());
  if (it == precedence->end()) {
    return OperatorPrecedenceEnum::kUnknownOperatorPrecedence;
  }
  return it->second;
}

bool IsOpenParenOrBracket(absl::string_view keyword) {
  static const auto* brackets =
      new googlesql_base::flat_set<absl::string_view>({"(", "[", "{"});
  return brackets->contains(keyword);
}

bool IsCloseParenOrBracket(absl::string_view keyword) {
  static const auto* brackets =
      new googlesql_base::flat_set<absl::string_view>({")", "]", "}"});
  return brackets->contains(keyword);
}

absl::string_view CorrespondingOpenBracket(absl::string_view keyword) {
  if (keyword.empty()) {
    return "<unknown>";
  }
  switch (keyword[0]) {
    case ')':
      return "(";
    case ']':
      return "[";
    case '}':
      return "{";
    case '>':
      return "<";
    // Quotes behave as parentheses when the contents of a string literal is
    // parsed as SQL.
    case '\'':
      return keyword.length() == 1 ? "'" : "'''";
    case '"':
      return keyword.length() == 1 ? "\"" : "\"\"\"";
    default:
      return "<unknown>";
  }
}

absl::string_view CorrespondingCloseBracket(absl::string_view keyword) {
  if (keyword.empty()) {
    return "<unknown>";
  }
  switch (keyword[0]) {
    case '(':
      return ")";
    case '[':
      return "]";
    case '{':
      return "}";
    case '<':
      return ">";
    // Quotes behave as parentheses when the contents of a string literal is
    // parsed as SQL.
    case '\'':
      return keyword.length() == 1 ? "'" : "'''";
    case '"':
      return keyword.length() == 1 ? "\"" : "\"\"\"";
    default:
      return "<unknown>";
  }
}

FormatterRange FindNextStatementOrComment(absl::string_view input, int start) {
  absl::string_view input_substr = input.substr(start);
  icu::UnicodeString txt(input_substr.data(),
                         static_cast<int32_t>(input_substr.size()));
  icu::StringCharacterIterator it = SkipAllWhitespaces(txt);
  if (!it.hasNext()) {
    return {static_cast<int>(input.size()), static_cast<int>(input.size())};
  }
  auto line_start = it;
  // Scroll back line start if needed.
  while (line_start.getIndex() != it.startIndex()) {
    char32_t prev_char = line_start.previous32();
    if (!u_isUWhiteSpace(prev_char) || prev_char == '\n' || prev_char == '\r') {
      line_start.next32();
      break;
    }
  }
  const int stmt_start =
      start + DistanceBetweenTwoIndexInBytes(txt, line_start.startIndex(),
                                             line_start.getIndex());
  bool first_token = true;
  bool format_off_section = false;
  while (it.hasNext()) {
    const icu::StringCharacterIterator token_start = it;
    InvalidTokenType type = DetectTokenType(&it);
    it = FindTokenEnd(it, type);
    if (!it.hasNext()) {
      break;
    }
    if (type == InvalidTokenType::MULTILINE_COMMENT ||
        type == InvalidTokenType::SINGLE_LINE_COMMENT) {
      // Check if the comment starts/ends a no-format region.
      icu::UnicodeString comment_body(
          txt, token_start.getIndex(),
          DistanceBetweenTwoIndexInBytes(txt, token_start.getIndex(),
                                         it.getIndex()));
      std::string comment_body_utf8;
      if (IsStartOfNoFormatRegion(
              comment_body.toUTF8String<std::string>(comment_body_utf8))) {
        format_off_section = true;
      }
      if (format_off_section && IsEndOfNoFormatRegion(comment_body_utf8)) {
        format_off_section = false;
      }
    }
    if (!format_off_section) {
      if (first_token && (type == InvalidTokenType::MULTILINE_COMMENT ||
                          type == InvalidTokenType::SINGLE_LINE_COMMENT)) {
        it = SkipSpacesUntilEndOfLine(it);
        return {stmt_start, start + DistanceBetweenTwoIndexInBytes(
                                        txt, it.startIndex(), it.getIndex())};
      } else if (it.current32() == ';') {
        it.next32();
        it = SkipSpacesAndCommentsUntilEndOfLine(it);
        return {stmt_start, start + DistanceBetweenTwoIndexInBytes(
                                        txt, it.startIndex(), it.getIndex())};
      }
      first_token = false;
    }
    it = SkipAllWhitespaces(it);
  }
  return {stmt_start, start + DistanceBetweenTwoIndexInBytes(
                                  txt, it.startIndex(), it.getIndex())};
}

bool IsBindKeyword(absl::string_view str) {
  static const auto* bind_keywords = new googlesql_base::flat_set<absl::string_view>({
      "BIND",
      "BIND_COLUMNS",
      "BIND_EXPRESSION",
      "BIND_ST_QUERY_CONFIG",
      "SET_ALIAS_TYPE",
  });
  return bind_keywords->contains(str);
}

}  // namespace googlesql::formatter::internal
