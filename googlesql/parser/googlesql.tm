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

// This file defines the lexer and parser used in the GoogleSQL analyzer.

language sql(cc);
namespace = "googlesql::parser"
includeGuardPrefix = "STORAGE_GOOGLESQL_PARSER"
abseilIncludePrefix = "absl"
dirIncludePrefix = "googlesql/parser/"
debugParser = false # Set to true to print the parser shift/reduce decisions.
optimizeTables = true
defaultReduce = true
minimizeDFA = true
trackReduces = true
tokenLine = true
filenamePrefix = "tm_"
caseInsensitive = true
scanBytes = true
skipByteOrderMark = false
variantStackEntry = true
// Use "-opt" rather than "_opt" or the default "opt" so that its less likely
// that this operator is confused for part of the symbol name.
optInstantiationSuffix = "-opt"
// Otherwise Texmapper tries to consruct C++ identifiers with the "-" and the
// C++ doesn't compile.
aliasIncludesOptSuffix = false

parseParams = [
  "TokenStream& tokenizer",
  "const LanguageOptions& language_options",
  "ASTNodeFactory& node_factory",
  "WarningSink& warning_sink",
  "MacroExpansionMode macro_expansion_mode",
  "ASTNode** ast_node_result",
  "ASTStatementProperties* ast_statement_properties",
  "int* statement_end_byte_offset",
]

:: lexer

/* These are some basic regex definitions that are used in the lexer rules
   below.
*/

decimal_digit               = /[0-9]/
decimal_digits              = /{decimal_digit}+/
hex_digit                   = /[0-9a-f]/
hex_integer                 = /(0x{hex_digit}+)/


/* Whitespace, including Unicode whitespace characters encoded as UTF-8, as well
   as all comments.
   https://www.cs.tut.fi/~jkorpela/chars/spaces.html

   OGHAM SPACE MARK (U+1680) is omitted because it looks like "-".
   MONGOLIAN VOWEL SEPARATOR (U+180E) is omitted because it has no width.
   ZERO WIDTH SPACE (U+200B) is omitted because it has no width.
   ZERO WIDTH NO-BREAK SPACE (U+FEFF) is omitted because it has no width.
*/
utf8_no_break_space            = /\u00A0/
utf8_en_quad                   = /\u2000/
utf8_em_quad                   = /\u2001/
utf8_en_space                  = /\u2002/
utf8_em_space                  = /\u2003/
utf8_three_per_em_space        = /\u2004/
utf8_four_per_em_space         = /\u2005/
utf8_six_per_em_space          = /\u2006/
utf8_figure_space              = /\u2007/
utf8_punctuation_space         = /\u2008/
utf8_thin_space                = /\u2009/
utf8_hair_space                = /\u200A/
utf8_narrow_no_break_space     = /\u202F/
utf8_medium_mathematical_space = /\u205F/
utf8_ideographic_space         = /\u3000/
whitespace_character           = /([ \n\r\t\x08\f\v]|{utf8_no_break_space}|{utf8_en_quad}|{utf8_em_quad}|{utf8_en_space}|{utf8_em_space}|{utf8_three_per_em_space}|{utf8_four_per_em_space}|{utf8_six_per_em_space}|{utf8_figure_space}|{utf8_punctuation_space}|{utf8_thin_space}|{utf8_hair_space}|{utf8_narrow_no_break_space}|{utf8_medium_mathematical_space}|{utf8_ideographic_space})/
whitespace_no_comments         = /{whitespace_character}+/

/* String/bytes literals and identifiers.

   The abbreviations here:
     sq = single quote(d)
     dq = double quote(d)
     bq = back quote(d)
     3 = triple quoted
     r = raw
     _0 = unterminated versions. They are used to return better error
          messages for unterminated strings.

   For instance, rsq3 means "raw triple single-quoted", or r'''...'''.

   The regexes accept arbitrary escapes instead of trying to narrow it down to
   just the valid set. This is safe because in valid strings the character after
   the escape is *always* eaten, even in raw strings. The actual validation of
   the escapes, and of things like UTF-8 structure, is done in the parser.
   This also allows us to use the same regex for raw strings that we use for any
   other string. Raw strings interpret the escapes differently (they allow all
   escapes and pass them through verbatim), but the termination condition is
   the same: escaped quotes don't count.

   In single quoted strings/bytes we don't accept \n so that a single-line
   unterminated string literal is recognized as an unterminated string literal
   at that point, instead of being bogusly matched up with another quote on a
   subsequent line. However, we do accept escaped newlines. These get a separate
   and nicer error message pointing directly at the escaped newline.
*/
any_escape              = /(\\(.|\n|\r|\r\n))/
sq                      = /\'/
sq3                     = /{sq}{sq}{sq}/
dq                      = /\"/
dq3                     = /{dq}{dq}{dq}/
bq                      = /\`/
no_backslash_sq_newline = /[^\'\\\n\r]/
no_backslash_dq_newline = /[^\"\\\n\r]/
no_backslash_sq         = /[^\'\\]/
no_backslash_dq         = /[^\"\\]/

/* Strings and bytes: */
sqtext_0                                  = /{sq}({no_backslash_sq_newline}|{any_escape})*/
sqtext                                    = /{sqtext_0}{sq}/
dqtext_0                                  = /{dq}({no_backslash_dq_newline}|{any_escape})*/
dqtext                                    = /{dqtext_0}{dq}/
sq3text_0                                 = /{sq3}(({sq}|{sq}{sq})?({no_backslash_sq}|{any_escape}))*/
sq3text                                   = /{sq3text_0}{sq3}/
dq3text_0                                 = /{dq3}(({dq}|{dq}{dq})?({no_backslash_dq}|{any_escape}))*/
dq3text                                   = /{dq3text_0}{dq3}/
string_literal                            = /r?({sqtext}|{dqtext}|{sq3text}|{dq3text})/
bytes_literal                             = /(b|rb|br)({sqtext}|{dqtext}|{sq3text}|{dq3text})/
unclosed_string_literal                   = /({sqtext_0}|{dqtext_0})/
unclosed_triple_quoted_string_literal     = /({sq3text_0}|{dq3text_0})/
unclosed_raw_string_literal               = /r({sqtext_0}|{dqtext_0})/
unclosed_triple_quoted_raw_string_literal = /r({sq3text_0}|{dq3text_0})/
unclosed_bytes_literal                    = /b({sqtext_0}|{dqtext_0})/
unclosed_triple_quoted_bytes_literal      = /b({sq3text_0}|{dq3text_0})/
unclosed_raw_bytes_literal                = /(rb|br)({sqtext_0}|{dqtext_0})/
unclosed_triple_quoted_raw_bytes_literal  = /(rb|br)({sq3text_0}|{dq3text_0})/

/* Identifiers: */
exponent_without_sign           = /E[0-9]+/
unquoted_identifier             = /[A-Z_][A-Z_0-9]*/
bqtext_0                        = /{bq}([^\\\`\r\n]|({any_escape}))*/
bqtext                          = /{bqtext_0}{bq}/
identifier                      = /{unquoted_identifier}|{bqtext}/
unclosed_escaped_identifier     = /{bqtext_0}/

/* C-style comments using slash+star.
   cs_ prefix is for "c-style comment", shortened to avoid long lines.
   For more information about how this works, see
   "Using one, even more complicated, pattern" from
   http://www.cs.man.ac.uk/~pjj/cs212/ex2_str_comm.html
*/
cs_start              =  /\/\*/
cs_not_star           = /[^*]/
cs_star               = /\*/
cs_not_star_or_slash  = /[^/*]/
cs_slash              = /\//
/* Contents of a C-style comment that may embed a * (or a sequence of stars)
   followed by not-a-slash. */
cs_embed_star         = /({cs_not_star}*({cs_star}+{cs_not_star_or_slash})*)*/
/* Matches the beginning of a comment, to detect unterminated comments. */
cs_comment_begin      = /{cs_start}{cs_embed_star}{cs_star}*/
cs_comment            = /{cs_start}{cs_embed_star}{cs_star}+{cs_slash}/

/* Dash comments using -- */
dash_comment          = /\-\-[^\r\n]*(\r|\n|\r\n)?/
/* # comment ignores anything from # to the end of the line. */
pound_comment         = /#[^\r\n]*(\r|\n|\r\n)?/
comment               = /({cs_comment}|{dash_comment}|{pound_comment})/

 /* All unescaping and error checking is done in the parser. This allows us */
 /* to give better error messages. */
invalid_token: /{unclosed_string_literal}/                    { SetUnclosedError("string literal"); }
invalid_token: /{unclosed_triple_quoted_string_literal}/      { SetTripleUnclosedError("string literal"); }
invalid_token: /{unclosed_raw_string_literal}/                { SetUnclosedError("raw string literal"); }
invalid_token: /{unclosed_triple_quoted_raw_string_literal}/  { SetTripleUnclosedError("raw string literal"); }
invalid_token: /{unclosed_bytes_literal}/                     { SetUnclosedError("bytes literal"); }
invalid_token: /{unclosed_triple_quoted_bytes_literal}/       { SetTripleUnclosedError("bytes literal"); }
invalid_token: /{unclosed_raw_bytes_literal}/                 { SetUnclosedError("raw bytes literal"); }
invalid_token: /{unclosed_triple_quoted_raw_bytes_literal}/   { SetTripleUnclosedError("raw bytes literal"); }
invalid_token: /{unclosed_escaped_identifier}/                { SetUnclosedError("identifier literal"); }
invalid_token: /{cs_comment_begin}/                           { SetUnclosedError("comment"); }

// These tokens are only used by the macro expander
"$" (DOLLAR_SIGN): /$/
"macro invocation" (MACRO_INVOCATION): /${unquoted_identifier}/
"macro builtin invocation" (MACRO_BUILTIN_INVOCATION): /$${unquoted_identifier}/
"macro argument reference" (MACRO_ARGUMENT_REFERENCE): /${decimal_digits}/

// Literals and identifiers. String, bytes and identifiers are not unescaped by
// the tokenizer. This is done in the parser so that we can give better error
// messages, pinpointing specific error locations in the token. This is really
// helpful for e.g. invalid escape codes.
STRING_LITERAL {absl::string_view}: /{string_literal}/
BYTES_LITERAL {absl::string_view}: /{bytes_literal}/
INTEGER_LITERAL {absl::string_view}:
FLOATING_POINT_LITERAL {absl::string_view}:
IDENTIFIER {absl::string_view}: /{identifier}/ -1
BACKSLASH: /\\/ // Only for lenient macro expansion

// Script labels. This is set apart from IDENTIFIER for two reasons:
// - Identifiers should still be disallowed at statement beginnings in all
//   other cases.
// - (Unreserved) Keywords don't need to be recognized as labels.
SCRIPT_LABEL {absl::string_view}:

// Comments. They are only returned if the tokenizer is run in a special comment
// preserving mode. They are not returned by the tokenizer when used with the
// Bison parser.
"comment" (COMMENT): /{comment}/

// Operators and punctuation. All punctuation must be referenced as "x", not
// 'x', or else bison will complain.  The corresponding token codes for single
// character punctuation are 'x' (i.e., the character code).
"*" (MULT): /\*/
"," (COMMA): /,/
";" (SEMICOLON): /;/
"(" (LPAREN): /\(/
")" (RPAREN): /\)/
"=" (ASSIGN): /=/
"+=" (KW_ADD_ASSIGN): /+=/
"-=" (KW_SUB_ASSIGN): /-=/
"!=" (KW_NOT_EQUALS_C_STYLE): /!=/
"<>" (KW_NOT_EQUALS_SQL_STYLE):
"<" (LT) {absl::string_view}: /</
"<=" (KW_LESS_EQUALS): /<=/
">" (GT): />/
">=" (KW_GREATER_EQUALS): />=/
"|" (OR): /\|/
"^" (XOR): /\^/
"&" (AND): /&/
"!" (EXCL): /!/
"%" (REM): /%/
"[" (LBRACK) {absl::string_view}: /\[/
"]" (RBRACK){absl::string_view}: /\]/
"@" (ATSIGN): /@/
"@@" (KW_DOUBLE_AT): /@@/
"::" (KW_DOUBLE_COLON): /::/
"?::" (KW_DOUBLE_COLON_QUESTION): /\?::/
"||" (KW_CONCAT_OP): /\|\|/
"+" (PLUS): /+/
"-" (MINUS) {absl::string_view}: /-/
"/" (DIV): /\//
"~" (TILDE): /~/
"**" (KW_UNPACK): /\*\*/
"." (DOT): /\./
KW_OPEN_HINT:
"}" (RBRACE): /\}/
"?" (QUEST): /\?/
// This is the opening `@` in a standalone integer hint.
KW_OPEN_INTEGER_HINT:
// This is the opening `@` in an integer hint followed by a @{...} hint.
OPEN_INTEGER_PREFIX_HINT:
"<<" (KW_SHIFT_LEFT): /<</
">>" (KW_SHIFT_RIGHT):
"=>" (KW_NAMED_ARGUMENT_ASSIGNMENT): /=>/
"->" (KW_LAMBDA_ARROW) {absl::string_view}: /->/
"|>" (KW_PIPE): /\|>/

// These are not used in the grammar. They are here for parity with the JavaCC
// tokenizer.
":" (COLON): /:/
"{" (LBRACE): /\{/

UNARY_NOT_PRECEDENCE:
EDGE_ENDPOINT_PRECEDENCE:
UNARY_PRECEDENCE:
DOUBLE_AT_PRECEDENCE:
PRIMARY_PRECEDENCE:
RUN_PRECEDENCE:

// FORCE_LA1_TOKEN? is used in some rules to force the parser to consume a token
// into its lookahead 1 buffer.
//
// When a state marker is the last symbol in a rule that otherwise would not
// have populated the lookahead buffer, the lookahead transformer may never have
// an opportunity to observe the marked state. The reduce moves the parser into
// a different state and a token is never requested from the state that is
// marked.
//
// Adding `FORCE_LA1_TOKEN?` after the state marker forces the parser to consume
// a token so that it can check whether it is `FORCE_LA1_TOKEN`. It never is,
// but by forcing the parser to consume the token the lookahead transfomer has
// an opportunity to observe the state we meant to mark.
FORCE_LA1_TOKEN:

// KEYWORDS
// --------
//
// To add a keyword:
// 1. Add a token rule in the ":: lexer" section of googlesql.tm. If the keyword
//    is a reserved keyword, add the token rule to the "RESERVED_KW_START"
//    section. Otherwise, add it to the "NONRESERVED_KW_START" section.
// 2. Add the keyword to the array in keywords.cc, with the appropriate class.
// 3. If the keyword can be used as an identifier, add it to the
//    "keyword_as_identifier" production in the grammar.
// 4. If the keyword is reserved, add it to the "reserved_keyword_rule"
//    production in the grammar.

// This sentinel allocates an integer smaller than all the values used for
// reserved keywords. Together with SENTINEL RESERVED KW END, a simple integer
// comparison can efficiently identify a token as a reserved keyword. This token
// is not produced by the lexer.
SENTINEL_RESERVED_KW_START {absl::string_view}:
KW_ALIGN_RESERVED {absl::string_view}:
"ALL" (KW_ALL) {absl::string_view}: /all/
"AND" (KW_AND) {absl::string_view}: /and/
"ANY" (KW_ANY) {absl::string_view}: /any/
"ARRAY" (KW_ARRAY) {absl::string_view}: /array/
"AS" (KW_AS) {absl::string_view}: /as/
"ASC" (KW_ASC) {absl::string_view}: /asc/
"ASSERT_ROWS_MODIFIED" (KW_ASSERT_ROWS_MODIFIED) {absl::string_view}: /assert_rows_modified/
"AT" (KW_AT) {absl::string_view}: /at/
"BETWEEN" (KW_BETWEEN) {absl::string_view}: /between/
"BY" (KW_BY) {absl::string_view}: /by/
"CASE" (KW_CASE) {absl::string_view}: /case/
"CAST" (KW_CAST) {absl::string_view}: /cast/
"COLLATE" (KW_COLLATE) {absl::string_view}: /collate/
"CREATE" (KW_CREATE) {absl::string_view}: /create/
"CROSS" (KW_CROSS) {absl::string_view}: /cross/
"CURRENT" (KW_CURRENT) {absl::string_view}: /current/
"DEFAULT" (KW_DEFAULT) {absl::string_view}: /default/
KW_DEFINE_FOR_MACROS {absl::string_view}:
"DEFINE" (KW_DEFINE) {absl::string_view}: /define/
"DESC" (KW_DESC) {absl::string_view}: /desc/
"DISTINCT" (KW_DISTINCT) {absl::string_view}: /distinct/
"ELSE" (KW_ELSE) {absl::string_view}: /else/
"END" (KW_END) {absl::string_view}: /end/
"ENUM" (KW_ENUM) {absl::string_view}: /enum/
"EXCEPT" (KW_EXCEPT) {absl::string_view}: /except/
"EXISTS" (KW_EXISTS) {absl::string_view}: /exists/
"EXTRACT" (KW_EXTRACT) {absl::string_view}: /extract/
"FALSE" (KW_FALSE) {absl::string_view}: /false/
"FOLLOWING" (KW_FOLLOWING) {absl::string_view}: /following/
"FOR" (KW_FOR) {absl::string_view}: /for/
"FROM" (KW_FROM) {absl::string_view}: /from/
"FULL" (KW_FULL) {absl::string_view}: /full/
KW_GRAPH_TABLE_RESERVED {absl::string_view}:
"GROUP" (KW_GROUP) {absl::string_view}: /group/
"GROUPING" (KW_GROUPING) {absl::string_view}: /grouping/
"HASH" (KW_HASH) {absl::string_view}: /hash/
"HAVING" (KW_HAVING) {absl::string_view}: /having/
"IF" (KW_IF) {absl::string_view}: /if/
"IGNORE" (KW_IGNORE) {absl::string_view}: /ignore/
"IN" (KW_IN) {absl::string_view}: /in/
"INNER" (KW_INNER) {absl::string_view}: /inner/
"INTERSECT" (KW_INTERSECT) {absl::string_view}: /intersect/
"INTERVAL" (KW_INTERVAL) {absl::string_view}: /interval/
"INTO" (KW_INTO) {absl::string_view}: /into/
"IS" (KW_IS) {absl::string_view}: /is/
"JOIN" (KW_JOIN) {absl::string_view}: /join/
"LEFT" (KW_LEFT) {absl::string_view}: /left/
"LIKE" (KW_LIKE) {absl::string_view}: /like/
"LIMIT" (KW_LIMIT) {absl::string_view}: /limit/
"LOOKUP" (KW_LOOKUP) {absl::string_view}: /lookup/
KW_MATCH_RECOGNIZE_RESERVED {absl::string_view}:
"MERGE" (KW_MERGE) {absl::string_view}: /merge/
"NATURAL" (KW_NATURAL) {absl::string_view}: /natural/
"NEW" (KW_NEW) {absl::string_view}: /new/
"NO" (KW_NO) {absl::string_view}: /no/
"NOT" (KW_NOT) {absl::string_view}: /not/
"NULL" (KW_NULL) {absl::string_view}: /null/
"NULLS" (KW_NULLS) {absl::string_view}: /nulls/
"ON" (KW_ON) {absl::string_view}: /on/
"OR" (KW_OR) {absl::string_view}: /or/
"ORDER" (KW_ORDER) {absl::string_view}: /order/
"OUTER" (KW_OUTER) {absl::string_view}: /outer/
"OVER" (KW_OVER) {absl::string_view}: /over/
"PARTITION" (KW_PARTITION) {absl::string_view}: /partition/
"PRECEDING" (KW_PRECEDING) {absl::string_view}: /preceding/
"PROTO" (KW_PROTO) {absl::string_view}: /proto/
"RANGE" (KW_RANGE) {absl::string_view}: /range/
"RECURSIVE" (KW_RECURSIVE) {absl::string_view}: /recursive/
"RESPECT" (KW_RESPECT) {absl::string_view}: /respect/
"RIGHT" (KW_RIGHT) {absl::string_view}: /right/
"ROLLUP" (KW_ROLLUP) {absl::string_view}: /rollup/
"ROWS" (KW_ROWS) {absl::string_view}: /rows/
"SELECT" (KW_SELECT) {absl::string_view}: /select/
"SET" (KW_SET) {absl::string_view}: /set/
"STRUCT" (KW_STRUCT) {absl::string_view}: /struct/
"TABLESAMPLE" (KW_TABLESAMPLE) {absl::string_view}: /tablesample/
"THEN" (KW_THEN) {absl::string_view}: /then/
"TO" (KW_TO) {absl::string_view}: /to/
"TRUE" (KW_TRUE) {absl::string_view}: /true/
"UNBOUNDED" (KW_UNBOUNDED) {absl::string_view}: /unbounded/
"UNION" (KW_UNION) {absl::string_view}: /union/
"USING" (KW_USING) {absl::string_view}: /using/
"WHEN" (KW_WHEN) {absl::string_view}: /when/
"WHERE" (KW_WHERE) {absl::string_view}: /where/
"WINDOW" (KW_WINDOW) {absl::string_view}: /window/
"WITH" (KW_WITH) {absl::string_view}: /with/
"UNNEST" (KW_UNNEST) {absl::string_view}: /unnest/

// These keywords may not be used in the grammar currently but are reserved
// for future use.
"CONTAINS" (KW_CONTAINS) {absl::string_view}: /contains/
"CUBE" (KW_CUBE) {absl::string_view}: /cube/
"ESCAPE" (KW_ESCAPE) {absl::string_view}: /escape/
"EXCLUDE" (KW_EXCLUDE) {absl::string_view}: /exclude/
"FETCH" (KW_FETCH) {absl::string_view}: /fetch/
"GROUPS" (KW_GROUPS) {absl::string_view}: /groups/
"LATERAL" (KW_LATERAL) {absl::string_view}: /lateral/
"OF" (KW_OF) {absl::string_view}: /of/
"SOME" (KW_SOME) {absl::string_view}: /some/
"TREAT" (KW_TREAT) {absl::string_view}: /treat/
"WITHIN" (KW_WITHIN) {absl::string_view}: /within/
KW_QUALIFY_RESERVED {absl::string_view}:
SENTINEL_RESERVED_KW_END {absl::string_view}:


// These tokens should not appear in the grammar rules. They are used in actions
// to provide context to the DisambiguationLexer layer so it can more
// effectively understand the context of previously seen tokens.
SENTINEL_LB_TOKEN_START:
// Used to look back at a token that was guaranteed to be the opening token of
// a block of statements. This is particularly used in the "script" rules.
LB_OPEN_STATEMENT_BLOCK:
// Used to look back to see that the previous token was KW_BEGIN and that it is
// known to be the first token in a statement. That could mean the opening
// BEGIN of a BEGIN...END statement block or it could mean the first keyword of
// BEGIN TRANSACTION.
LB_BEGIN_AT_STATEMENT_START:
// Used to look back at an EXPLAIN token that was used at the start of a SQL
// statement to introduce EXPLAIN {stmt}.
LB_EXPLAIN_SQL_STATEMENT:
// Used to lookback to see whether the previous hint is a statement-level hint.
LB_END_OF_STATEMENT_LEVEL_HINT:
// Represents a "." token used in a path expression, as opposed to the "." in
// floating point literals. This token is only used as a lookback by the
// lookahead_transformer.
LB_DOT_IN_PATH_EXPRESSION:
// Used to lookback to know whether the previous token was the paren before a
// nested DML statement.
LB_OPEN_NESTED_DML:
// Used as a lookback override in SELECT WITH <identifier> OPTIONS. See below
// for more details.
LB_WITH_IN_WITH_OPTIONS:
// Used to identify the start of a query following an CORRESPONDING BY (..)
LB_CLOSE_COLUMN_LIST:
// Used to identify the start of a query following CORRESPONDING
LB_SET_OP_QUANTIFIER:
// Identifies the "AS" keyword immediately before a parenthesized query.
LB_AS_BEFORE_QUERY:
// These two aren't mentioned in the lookahead transfomer but their typical
// forms are. The overrides separate the special cases from the typical forms.
LB_GRAPH_FOR_IN:
LB_LPAREN_NOT_OPEN_SUBQUERY:
// Used to identify the start of a parenthsized query in the insert statement
// where a parenthesized query can follow an identifer.
LB_PAREN_OPENS_QUERY:
// Used to identify the beginning of a query that follows a WITH clause.
LB_CLOSE_ALIASED_QUERY:
LB_END_OF_WITH_RECURSIVE:

SENTINEL_LB_TOKEN_END:

// The tokens in this section are reserved in the sense they cannot be used as
// identifiers in the parser. They are not produced directly by the lexer
// though, they are produced by disambiguation transformations after the main
// lexer.

KW_WITH_STARTING_WITH_EXPRESSION:
KW_EXCEPT_IN_SET_OP:
KW_FOR_BEFORE_LOCK_MODE:
KW_FULL_IN_SET_OP:
KW_INNER_IN_SET_OP:
KW_LEFT_IN_SET_OP:
KW_TABLE_FOR_TABLE_CLAUSE:
KW_TIMESTAMP_IN_WITH_TIMESTAMP_AS_ALIAS:
KW_REPLACE_AFTER_INSERT:
KW_UPDATE_AFTER_INSERT:
KW_FUNCTION_IN_TABLE_FUNCTION:
// Emitted by the lookahead transformer in place of KW_TYPE when it directly
// follows the GRAPH keyword in a CREATE/DROP PROPERTY GRAPH statement. This
// disambiguates `CREATE PROPERTY GRAPH TYPE <name> ...` from a property graph
// whose name is the (non-reserved) identifier `type`.
KW_TYPE_IN_PROPERTY_GRAPH_TYPE:
// This is a different token because using KW_NOT for BETWEEN/IN/LIKE would
// confuse the operator precedence parsing. Boolean NOT has a different
// precedence than NOT BETWEEN/IN/LIKE.
"NOT_SPECIAL" (KW_NOT_SPECIAL):

// These tokens represent errors discovered by the lexer. Passing errors as
// tokens gives the parser the chance to ignore the error in contexts where it
// it is valid. This pattern keeps the parser context in the parser and reduces
// the need for the lexer to be made aware of the parser's state.
ERROR_SCRIPT_LABEL_IS_RESERVED_KEYWORD:
ERROR_EXCEPT_IN_UNEXPECTED_CONTEXT:

// Guideance tokens help the lookahead transformer communicate decisions to the
// parser. The guideance tokens need to be identifiable so that they can be
// excluded from error messages. The sentinels help identify the block of
// tokens.
SENTINEL_GUIDANCE_TOKENS_START:
// This token is inserted by the token stream to indicate that the type symbol
// is empty in a column definition (e.g. in CREATE TABLE).
GUIDE_EMPTY_COLUMN_TYPE:

SENTINEL_GUIDANCE_TOKENS_END:

// This token is used alongside LB_WITH_IN_WITH_OPTIONS in The
// SELECT WITH <identifier> OPTIONS construct.
// There is a true ambiguity in the query prefix when using SELECT WITH OPTIONS.
// For example,
//   SELECT {opt_hint} WITH modification_kind OPTIONS(a = 1) alias, * FROM ...
// has two valid parses.
// 1. `OPTIONS(a = 1)` is an options list associated with WITH modification_kind
//    and `alias` is the first column in the select list
// 2. `OPTIONS(a = 1)` is a function call, `a = 1` is an expression computing
//    the argument, and `alias` is the column alias.
// This is a true ambiguity. Both parses are valid and no ammount of lookahead
// will eliminate the ambiguity. Our intention has been to force parse #1.
//
// A solution based on LALR(1) parser's "prefer shift" rule that chooses to
// shift options rather than reduce column name is not ideal because it causes
// collateral damage. Consider:
// * SELECT WITH mod options, more_columns...
//
// In this case, options cannot be an option list because it's not followed by
// `(`. LALR(1) shift/reduce resolution can't see that `(` because it takes two
// lookaheads. Instead, we use the disambiguation layer to solve this using
// two lookaheads.
//
// `LB_WITH_IN_WITH_OPTIONS` is used exclusively as a lookback override.
// The override is triggered by the parser before consuming the `WITH` token
// in the context of SELECT WITH. Then token disambiguation layer looks for this
// window:
//   lookback1 : LB_WITH_IN_WITH_OPTIONS
//   token     : IDENTIFER
//   lookahead1: KW_OPTIONS
//   lookahead2: '('
// When it sees this window it changes lookahead1 to
// `KW_OPTIONS_IN_WITH_OPTIONS` so that there is no ambiguity in the
// parser.
KW_OPTIONS_IN_WITH_OPTIONS:

// A special token to indicate that an integer or floating point literal is
// immediately followed by an identifier without space, for example 123abc.
ATTACHED_ALIAS:

// The following two tokens will be converted into INTEGER_LITERAL by the
// lookahead_transformer, and the parser should not use them directly.
DECIMAL_INTEGER_LITERAL: /{decimal_digits}/
HEX_INTEGER_LITERAL: /{hex_integer}/

// Represents an exponent part without a sign used in a float literal, for
// example the "e10" in "1.23e10". It will gets fused into a floating point
// literal or becomes an identifier by the lookahead_transformer, and the parser
// should not use it directly.
EXP_IN_FLOAT_NO_SIGN: /{exponent_without_sign}/
// Represents the exponent part "E" used in a float literal, for example the "e"
// in "1.23e+10". It will gets fused into a floating point literal or becomes an
// identifier by the lookahead_transformer, and the parser should not use it
// directly.
"e" (STANDALONE_EXPONENT_SIGN): /e/

// The * in the braced constructor that represents "update many" in the UPDATE
// constructor.
UPDATE_MANY_STAR:

// Non-reserved keywords.  These can also be used as identifiers.
// These must all be listed explicitly in the "keyword_as_identifier" rule
// below. Do NOT include keywords in this list that are conditionally generated.
// They go in a separate list below this one.
//
// This sentinel allocates an integer smaller than all the values used for
// reserved keywords. Together with SENTINEL RESERVED KW END, a simple integer
// comparison can efficiently identify a token as a reserved keyword. This token
// is not produced by the lexer.
SENTINEL_NONRESERVED_KW_START {absl::string_view}:
"ABORT" (KW_ABORT) {absl::string_view}: /abort/
"ACCESS" (KW_ACCESS) {absl::string_view}: /access/
"ACTION" (KW_ACTION) {absl::string_view}: /action/
"ACYCLIC" (KW_ACYCLIC) {absl::string_view}: /acyclic/
"ADD" (KW_ADD) {absl::string_view}: /add/
"AFTER" (KW_AFTER) {absl::string_view}: /after/
"AI" (KW_AI) {absl::string_view}: /ai/
"AGGREGATE" (KW_AGGREGATE) {absl::string_view}: /aggregate/
KW_ALIGN_NONRESERVED {absl::string_view}: /align/
"ALTER" (KW_ALTER) {absl::string_view}: /alter/
"ALWAYS" (KW_ALWAYS) {absl::string_view}: /always/
"ANALYZE" (KW_ANALYZE) {absl::string_view}: /analyze/
"APPROX" (KW_APPROX) {absl::string_view}: /approx/
"ARE" (KW_ARE) {absl::string_view}: /are/
"ASCENDING" (KW_ASCENDING) {absl::string_view}: /ascending/
"ASSERT" (KW_ASSERT) {absl::string_view}: /assert/
"AUTO" (KW_AUTO) {absl::string_view}: /auto/
"BATCH" (KW_BATCH) {absl::string_view}: /batch/
"BEGIN" (KW_BEGIN) {absl::string_view}: /begin/
"BIGDECIMAL" (KW_BIGDECIMAL) {absl::string_view}: /bigdecimal/
"BIGNUMERIC" (KW_BIGNUMERIC) {absl::string_view}: /bignumeric/
"BACKWARDS" (KW_BACKWARDS) {absl::string_view}: /backwards/
"BREAK" (KW_BREAK) {absl::string_view}: /break/
"CALL" (KW_CALL) {absl::string_view}: /call/
"CASCADE" (KW_CASCADE) {absl::string_view}: /cascade/
"CHECK" (KW_CHECK) {absl::string_view}: /check/
"CHEAPEST" (KW_CHEAPEST) {absl::string_view}: /cheapest/
"CLAMPED" (KW_CLAMPED) {absl::string_view}: /clamped/
"CLONE" (KW_CLONE) {absl::string_view}: /clone/
"COPY" (KW_COPY) {absl::string_view}: /copy/
"CLUSTER" (KW_CLUSTER) {absl::string_view}: /cluster/
"COLUMN" (KW_COLUMN) {absl::string_view}: /column/
"COLUMNS" (KW_COLUMNS) {absl::string_view}: /columns/
"COMMIT" (KW_COMMIT) {absl::string_view}: /commit/
"CONDITION" (KW_CONDITION) {absl::string_view}: /condition/
"CONFLICT" (KW_CONFLICT) {absl::string_view}: /conflict/
"CONNECTION" (KW_CONNECTION) {absl::string_view}: /connection/
"CONTINUE" (KW_CONTINUE) {absl::string_view}: /continue/
"CONSTANT" (KW_CONSTANT) {absl::string_view}: /constant/
"CONSTRAINT" (KW_CONSTRAINT) {absl::string_view}: /constraint/
"COST" (KW_COST) {absl::string_view}: /cost/
"CYCLE" (KW_CYCLE) {absl::string_view}: /cycle/
"DATA" (KW_DATA) {absl::string_view}: /data/
"DATA_POLICY" (KW_DATA_POLICY) {absl::string_view}: /data_policy/
"DATABASE" (KW_DATABASE) {absl::string_view}: /database/
"DATE" (KW_DATE) {absl::string_view}: /date/
"DATETIME" (KW_DATETIME) {absl::string_view}: /datetime/
"DECIMAL" (KW_DECIMAL) {absl::string_view}: /decimal/
"DECLARE" (KW_DECLARE) {absl::string_view}: /declare/
"DEFINER" (KW_DEFINER) {absl::string_view}: /definer/
"DELETE" (KW_DELETE) {absl::string_view}: /delete/
"DELETION" (KW_DELETION) {absl::string_view}: /deletion/
"DEPTH" (KW_DEPTH) {absl::string_view}: /depth/
"DESCENDING" (KW_DESCENDING) {absl::string_view}: /descending/
"DESCRIBE" (KW_DESCRIBE) {absl::string_view}: /describe/
"DESCRIPTOR" (KW_DESCRIPTOR) {absl::string_view}: /descriptor/
"DESTINATION" (KW_DESTINATION) {absl::string_view}: /destination/
"DETACH" (KW_DETACH) {absl::string_view}: /detach/
"DETERMINISTIC" (KW_DETERMINISTIC) {absl::string_view}: /deterministic/
"DO" (KW_DO) {absl::string_view}: /do/
"DROP" (KW_DROP) {absl::string_view}: /drop/
"DYNAMIC" (KW_DYNAMIC) {absl::string_view}: /dynamic/
"EDGE" (KW_EDGE) {absl::string_view}: /edge/
"ELSEIF" (KW_ELSEIF) {absl::string_view}: /elseif/
"ENFORCED" (KW_ENFORCED) {absl::string_view}: /enforced/
"EPOCH" (KW_EPOCH) {absl::string_view}: /epoch/
"EXECUTE" (KW_EXECUTE) {absl::string_view}: /execute/
"EXPLAIN" (KW_EXPLAIN) {absl::string_view}: /explain/
"EXPORT" (KW_EXPORT) {absl::string_view}: /export/
"EXTEND" (KW_EXTEND) {absl::string_view}: /extend/
"EXTERNAL" (KW_EXTERNAL) {absl::string_view}: /external/
"FILES" (KW_FILES) {absl::string_view}: /files/
"FINISH" (KW_FINISH) {absl::string_view}: /finish/
"FILTER" (KW_FILTER) {absl::string_view}: /filter/
"FILL" (KW_FILL) {absl::string_view}: /fill/
"FIRST" (KW_FIRST) {absl::string_view}: /first/
"FOREIGN" (KW_FOREIGN) {absl::string_view}: /foreign/
"FORK" (KW_FORK) {absl::string_view}: /fork/
"FORMAT" (KW_FORMAT) {absl::string_view}: /format/
"FORWARDS" (KW_FORWARDS) {absl::string_view}: /forwards/
"FUNCTION" (KW_FUNCTION) {absl::string_view}: /function/
"GENERATED" (KW_GENERATED) {absl::string_view}: /generated/
"GRAPH" (KW_GRAPH) {absl::string_view}: /graph/
KW_GRAPH_TABLE_NONRESERVED {absl::string_view}:  /graph_table/
"GRANT" (KW_GRANT) {absl::string_view}: /grant/
"GROUP_ROWS" (KW_GROUP_ROWS) {absl::string_view}: /group_rows/
"HIDDEN" (KW_HIDDEN) {absl::string_view}: /hidden/
"IDENTITY" (KW_IDENTITY) {absl::string_view}: /identity/
"IMMEDIATE" (KW_IMMEDIATE) {absl::string_view}: /immediate/
"IMMUTABLE" (KW_IMMUTABLE) {absl::string_view}: /immutable/
"IMPORT" (KW_IMPORT) {absl::string_view}: /import/
"INCLUDE" (KW_INCLUDE) {absl::string_view}: /include/
"INCREMENT" (KW_INCREMENT) {absl::string_view}: /increment/
"INDEX" (KW_INDEX) {absl::string_view}: /index/
"INOUT" (KW_INOUT) {absl::string_view}: /inout/
"INPUT" (KW_INPUT) {absl::string_view}: /input/
"INSERT" (KW_INSERT) {absl::string_view}: /insert/
"INVOKER" (KW_INVOKER) {absl::string_view}: /invoker/
"ITERATE" (KW_ITERATE) {absl::string_view}: /iterate/
"ISOLATION" (KW_ISOLATION) {absl::string_view}: /isolation/
"JSON" (KW_JSON) {absl::string_view}: /json/
"KEY" (KW_KEY) {absl::string_view}: /key/
"LABEL" (KW_LABEL) {absl::string_view}: /label/
"LABELED" (KW_LABELED) {absl::string_view}: /labeled/
"LANGUAGE" (KW_LANGUAGE) {absl::string_view}: /language/
"LAST" (KW_LAST) {absl::string_view}: /last/
"LEAVE" (KW_LEAVE) {absl::string_view}: /leave/
"LET" (KW_LET) {absl::string_view}: /let/
"LEVEL" (KW_LEVEL) {absl::string_view}: /level/
"LIVE" (KW_LIVE) {absl::string_view}: /live/
"LOAD" (KW_LOAD) {absl::string_view}: /load/
"LOCALITY" (KW_LOCALITY) {absl::string_view}: /locality/
"LOG" (KW_LOG) {absl::string_view}: /log/
"LOOP" (KW_LOOP) {absl::string_view}: /loop/
"MACRO" (KW_MACRO) {absl::string_view}: /macro/
"MAP" (KW_MAP) {absl::string_view}: /map/
"MATCH" (KW_MATCH) {absl::string_view}: /match/
KW_MATCH_RECOGNIZE_NONRESERVED {absl::string_view}: /match_recognize/
"MATCHED" (KW_MATCHED) {absl::string_view}: /matched/
"MATERIALIZED" (KW_MATERIALIZED) {absl::string_view}: /materialized/
"MAX" (KW_MAX) {absl::string_view}: /max/
"MAXVALUE" (KW_MAXVALUE) {absl::string_view}: /maxvalue/
"MEASURES" (KW_MEASURES) {absl::string_view}: /measures/
"MESSAGE" (KW_MESSAGE) {absl::string_view}: /message/
"METADATA" (KW_METADATA) {absl::string_view}: /metadata/
"METRICS" (KW_METRICS) {absl::string_view}: /metrics/
"MIN" (KW_MIN) {absl::string_view}: /min/
"MINVALUE" (KW_MINVALUE) {absl::string_view}: /minvalue/
"MODEL" (KW_MODEL) {absl::string_view}: /model/
"MODULE" (KW_MODULE) {absl::string_view}: /module/
"NAME" (KW_NAME) {absl::string_view}: /name/
"NEXT" (KW_NEXT) {absl::string_view}: /next/
"NODE" (KW_NODE) {absl::string_view}: /node/
"NODETACH" (KW_NODETACH) {absl::string_view}: /nodetach/
"NOTHING" (KW_NOTHING) {absl::string_view}: /nothing/
"NUMERIC" (KW_NUMERIC) {absl::string_view}: /numeric/
"OFFSET" (KW_OFFSET) {absl::string_view}: /offset/
"ORIGIN" (KW_ORIGIN) {absl::string_view}: /origin/
"ONLY" (KW_ONLY) {absl::string_view}: /only/
"OPTIONAL" (KW_OPTIONAL) {absl::string_view}: /optional/
"OPTIONS" (KW_OPTIONS) {absl::string_view}: /options/
"OUT" (KW_OUT) {absl::string_view}: /out/
"OUTPUT" (KW_OUTPUT) {absl::string_view}: /output/
"OVERWRITE" (KW_OVERWRITE) {absl::string_view}: /overwrite/
"PARTITIONS" (KW_PARTITIONS) {absl::string_view}: /partitions/
"PAST" (KW_PAST) {absl::string_view}: /past/
"PATTERN" (KW_PATTERN) {absl::string_view}: /pattern/
"PATH" (KW_PATH) {absl::string_view}: /path/
"PATHS" (KW_PATHS) {absl::string_view}: /paths/
"PER" (KW_PER) {absl::string_view}: /per/
"PERIOD" (KW_PERIOD) {absl::string_view}: /period/
"PERCENT" (KW_PERCENT) {absl::string_view}: /percent/
"PIVOT" (KW_PIVOT) {absl::string_view}: /pivot/
"POLICIES" (KW_POLICIES) {absl::string_view}: /policies/
"POLICY" (KW_POLICY) {absl::string_view}: /policy/
"PRIMARY" (KW_PRIMARY) {absl::string_view}: /primary/
"PRIVATE" (KW_PRIVATE) {absl::string_view}: /private/
"PRIVILEGE" (KW_PRIVILEGE) {absl::string_view}: /privilege/
"PRIVILEGES" (KW_PRIVILEGES) {absl::string_view}: /privileges/
"PROCEDURE" (KW_PROCEDURE) {absl::string_view}: /procedure/
"PROJECT" (KW_PROJECT) {absl::string_view}: /project/
"PROPERTIES" (KW_PROPERTIES) {absl::string_view}: /properties/
"PROPERTY" (KW_PROPERTY) {absl::string_view}: /property/
"PUBLIC" (KW_PUBLIC) {absl::string_view}: /public/
KW_QUALIFY_NONRESERVED {absl::string_view}: /qualify/
"RAISE" (KW_RAISE) {absl::string_view}: /raise/
"READ" (KW_READ) {absl::string_view}: /read/
"REBUILD" (KW_REBUILD) {absl::string_view}: /rebuild/
"REFERENCES" (KW_REFERENCES) {absl::string_view}: /references/
"REMOTE" (KW_REMOTE) {absl::string_view}: /remote/
"REMOVE" (KW_REMOVE) {absl::string_view}: /remove/
"RENAME" (KW_RENAME) {absl::string_view}: /rename/
"REPEAT" (KW_REPEAT) {absl::string_view}: /repeat/
"REPEATABLE" (KW_REPEATABLE) {absl::string_view}: /repeatable/
"REPLACE" (KW_REPLACE) {absl::string_view}: /replace/
"REPLACE_FIELDS" (KW_REPLACE_FIELDS) {absl::string_view}: /replace_fields/
"REPLICA" (KW_REPLICA) {absl::string_view}: /replica/
"REPORT" (KW_REPORT) {absl::string_view}: /report/
"RESTRICT" (KW_RESTRICT) {absl::string_view}: /restrict/
"RESTRICTION" (KW_RESTRICTION) {absl::string_view}: /restriction/
"RETURN" (KW_RETURN) {absl::string_view}: /return/
"RETURNS" (KW_RETURNS) {absl::string_view}: /returns/
"REVOKE" (KW_REVOKE) {absl::string_view}: /revoke/
"ROLLBACK" (KW_ROLLBACK) {absl::string_view}: /rollback/
"ROW" (KW_ROW) {absl::string_view}: /row/
"RUN" (KW_RUN) {absl::string_view}: /run/
"SAFE_CAST" (KW_SAFE_CAST) {absl::string_view}: /safe_cast/
"SCHEMA" (KW_SCHEMA) {absl::string_view}: /schema/
"SEARCH" (KW_SEARCH) {absl::string_view}: /search/
"SECURITY" (KW_SECURITY) {absl::string_view}: /security/
"SEQUENCE" (KW_SEQUENCE) {absl::string_view}: /sequence/
"SETS" (KW_SETS) {absl::string_view}: /sets/
"SHORTEST" (KW_SHORTEST) {absl::string_view}: /shortest/
"SHOW" (KW_SHOW) {absl::string_view}: /show/
"SIMPLE" (KW_SIMPLE) {absl::string_view}: /simple/
"SKIP" (KW_SKIP) {absl::string_view}: /skip/
"SNAPSHOT" (KW_SNAPSHOT) {absl::string_view}: /snapshot/
"SOURCE" (KW_SOURCE) {absl::string_view}: /source/
"SQL" (KW_SQL) {absl::string_view}: /sql/
"STABLE" (KW_STABLE) {absl::string_view}: /stable/
"START" (KW_START) {absl::string_view}: /start/
"STATIC_DESCRIBE" (KW_STATIC_DESCRIBE) {absl::string_view}: /static_describe/
"STORED" (KW_STORED) {absl::string_view}: /stored/
"STORING" (KW_STORING) {absl::string_view}: /storing/
"SYSTEM" (KW_SYSTEM) {absl::string_view}: /system/
"SYSTEM_TIME" (KW_SYSTEM_TIME) {absl::string_view}: /system_time/
"TABLE" (KW_TABLE) {absl::string_view}: /table/
"TABLES" (KW_TABLES) {absl::string_view}: /tables/
"TARGET" (KW_TARGET) {absl::string_view}: /target/
"TRAIL" (KW_TRAIL) {absl::string_view}: /trail/
"TRANSFORM" (KW_TRANSFORM) {absl::string_view}: /transform/
"TEMP" (KW_TEMP) {absl::string_view}: /temp/
"TEMPORARY" (KW_TEMPORARY) {absl::string_view}: /temporary/
"TEE" (KW_TEE) {absl::string_view}: /tee/
"TIME" (KW_TIME) {absl::string_view}: /time/
"TIMESTAMP" (KW_TIMESTAMP) {absl::string_view}: /timestamp/
"TRANSACTION" (KW_TRANSACTION) {absl::string_view}: /transaction/
"TRUNCATE" (KW_TRUNCATE) {absl::string_view}: /truncate/
"TYPE" (KW_TYPE) {absl::string_view}: /type/
"TYPES" (KW_TYPES) {absl::string_view}: /types/
"UNDROP" (KW_UNDROP) {absl::string_view}: /undrop/
"UNIQUE" (KW_UNIQUE) {absl::string_view}: /unique/
"UNKNOWN" (KW_UNKNOWN) {absl::string_view}: /unknown/
"UNPIVOT" (KW_UNPIVOT) {absl::string_view}: /unpivot/
"UNTIL" (KW_UNTIL) {absl::string_view}: /until/
"UPDATE" (KW_UPDATE) {absl::string_view}: /update/
"VALUE" (KW_VALUE) {absl::string_view}: /value/
"VALUES" (KW_VALUES) {absl::string_view}: /values/
"VARIANT" (KW_VARIANT) {absl::string_view}: /variant/
"VERSION" (KW_VERSION) {absl::string_view}: /version/
"VECTOR" (KW_VECTOR) {absl::string_view}: /vector/
"VOLATILE" (KW_VOLATILE) {absl::string_view}: /volatile/
"VIEW" (KW_VIEW) {absl::string_view}: /view/
"VIEWS" (KW_VIEWS) {absl::string_view}: /views/
"WALK" (KW_WALK) {absl::string_view}: /walk/
"WEIGHT" (KW_WEIGHT) {absl::string_view}: /weight/
"WHILE" (KW_WHILE) {absl::string_view}: /while/
"WRITE" (KW_WRITE) {absl::string_view}: /write/
"YIELD" (KW_YIELD) {absl::string_view}: /yield/
"ZONE" (KW_ZONE) {absl::string_view}: /zone/
"EXCEPTION" (KW_EXCEPTION) {absl::string_view}: /exception/
"ERROR" (KW_ERROR) {absl::string_view}: /error/
"CORRESPONDING" (KW_CORRESPONDING) {absl::string_view}: /corresponding/
"STRICT" (KW_STRICT) {absl::string_view}: /strict/

// Spanner-specific keywords
"INTERLEAVE" (KW_INTERLEAVE) {absl::string_view}: /interleave/
"NULL_FILTERED" (KW_NULL_FILTERED) {absl::string_view}: /null_filtered/
"PARENT" (KW_PARENT) {absl::string_view}: /parent/

SENTINEL_NONRESERVED_KW_END {absl::string_view}:

// This is not a keyword token. It represents all identifiers that are
// CURRENT_* functions for date/time.
KW_CURRENT_DATETIME_FUNCTION:

/* Whitespace and EOI rule.

  This rule eats leading whitespace but not comments. This makes the EOI
  location reported to the parser skip the trailing whitespace, which results
  in better errors for unexpected end of input. But it doesn't skip trailing
  comments.
*/
whitespace_no_comments: /{whitespace_no_comments}/ (space)

eoi: /{eoi}/ {
  /* The location of EOI is always [N, N), where N is the length of the input.
  */
  token_offset_ = offset_;
  return Token::EOI;
}

/* Catchall rule. */
catch_all: /./ -100 {
  SetOverrideError(
        LastTokenLocationWithStartOffset(),
        absl::StrFormat(R"(Syntax error: Illegal input character "%s")",
                        absl::CEscape(Text().substr(0, 1))));
}

:: parser

// AMBIGUOUS CASES
// ===============
//
// AMBIGUOUS CASE 1: SAFE_CAST(...)
// --------------------------------
// The SAFE_CAST keyword is non-reserved and can be used as an identifier. This
// causes one shift/reduce conflict between keyword_as_identifier and the rule
// that starts with "SAFE_CAST" "(". It is resolved in favor of the SAFE_CAST(
// rule, which is the desired behavior.
//
//
// AMBIGUOUS CASE 2: CREATE TABLE CONSTRAINTS
// ------------------------------------------
// The CREATE TABLE rules for the PRIMARY KEY and FOREIGN KEY constraints have
// 2 shift/reduce conflicts, one for each constraint. PRIMARY and FOREIGN can
// be used as keywords for constraint definitions and as identifiers for column
// names. Bison can either shift the PRIMARY or FOREIGN keywords and use them
// for constraint definitions, or it can reduce them as identifiers and use
// them for column definitions. By default Bison shifts them. If the next token
// is KEY, Bison proceeds to reduce table_constraint_definition; otherwise, it
// reduces PRIMARY or FOREIGN as identifier and proceeds to reduce
// table_column_definition. Note that this grammar reports a syntax error when
// using PRIMARY KEY or FOREIGN KEY as column definition name and type pairs.
//
// AMBIGUOUS CASE 3: REPLACE_FIELDS(...)
// --------------------------------
// The REPLACE_FIELDS keyword is non-reserved and can be used as an identifier.
// This causes a shift/reduce conflict between keyword_as_identifier and the
// rule that starts with "REPLACE_FIELDS" "(". It is resolved in favor of the
// REPLACE_FIELDS( rule, which is the desired behavior.
//
// AMBIGUOUS CASE 4: Procedure parameter list in CREATE PROCEDURE
// -------------------------------------------------------------
// With rule procedure_parameter being:
// [<mode>] <identifier> <type>
// Optional <mode> can be non-reserved word OUT or INOUT, which can also be
// used as <identifier>. This causes 4 shift/reduce conflicts:
//   ( OUT
//   ( INOUT
//   , OUT
//   , INOUT
// By default, Bison chooses to "shift" and always treat OUT/INOUT as <mode>.
// In order to use OUT/INOUT as identifier, it needs to be escaped with
// backticks.
//
// AMBIGUOUS CASE 5: CREATE TABLE GENERATED
// -------------------------------------------------------------
// The GENERATED keyword is non-reserved, so when a generated column is defined
// with "<name> [<type>] GENERATED AS ()", we have a shift/reduce conflict, not
// knowing whether the word GENERATED is an identifier from <type> or the
// keyword GENERATED because <type> is missing. By default, Bison chooses
// "shift", treating GENERATED as a keyword. To use it as an identifier, it
// needs to be escaped with backticks.
//
// AMBIGUOUS CASE 6: DESCRIPTOR(...)
// --------------------------------
// The DESCRIPTOR keyword is non-reserved and can be used as an identifier. This
// causes one shift/reduce conflict between keyword_as_identifier and the rule
// that starts with "DESCRIPTOR" "(". It is resolved in favor of DESCRIPTOR(
// rule, which is the desired behavior.
//
// AMBIGUOUS CASE 7: ANALYZE OPTIONS(...)
// --------------------------------
// The OPTIONS keyword is non-reserved and can be used as an identifier.
// This causes a shift/reduce conflict between keyword_as_identifier and the
// rule that starts with "ANALYZE"  "OPTIONS" "(". It is resolved in favor of
// the OPTIONS( rule, which is the desired behavior.
//
// AMBIGUOUS CASE 8: SELECT * FROM T QUALIFY
// --------------------------------
// The QUALIFY keyword is non-reserved and can be used as an identifier.
// This causes a shift/reduce conflict between keyword_as_identifier and the
// rule that starts with "QUALIFY". It is resolved in favor of the QUALIFY rule,
// which is the desired behavior. Currently this is only used to report
// error messages to user when QUALIFY clause is used without
// WHERE/GROUP BY/HAVING.
//
// AMBIGUOUS CASE 9: ALTER COLUMN
// --------------------------------
// Spanner DDL compatibility extensions provide support for Spanner flavor of
// ALTER COLUMN action, which expects full column definition instead of
// sub-action. Column type identifier in this definition causes 2 shift/reduce
// conflicts with
//   ALTER COLUMN... DROP DEFAULT
//   ALTER COLUMN... DROP NOT NULL actions
// In both cases when encountering DROP, bison might either choose to shift
// (e.g. interpret DROP as keyword and proceed with one of the 2 rules above),
// or reduce DROP as type identifier in Spanner-specific rule. Bison chooses to
// shift, which is a desired behavior.
//
// AMBIGUOUS CASE 10: INPUT_ARG_TYPE CLAMPED
// ----------------------------------
//
// Catalog references use keywords (MODEL, SEQUENCE, TABLE, etc.) that are
// non-reserved and can be used as identifiers. INPUT_ARG_TYPE below can be
// substituted with any of these keywords.
//
// MyFunction(INPUT_ARG_TYPE clamped). Resolve to a function call passing an
// `INPUT_ARG_TYPE input` argument.
//
// MyFunction(input_arg_type clamped between x and y)
// Resolve to a function call passing a column 'input_arg_type' modified
// with "clamped between x and y".
//
// Bison favors reducing the 2nd form to an error, so we add a lexer rule to
// force INPUT_ARG_TYPE followed by clamped to resolve to an identifier.
// So bison still thinks there is a conflict but the lexer
// will _never_ produce:
// ... KW_INPUT_ARG_TYPE KW_CLAMPED ...
// it instead produces
// ... IDENTIFIER KW_CLAMPED
// Which will resolve toward the second form
// (input_arg_type clamped between x and y) correctly,
// and the first form (input_arg_type clamped) will result in an error.
//
// In other contexts, CLAMPED will also act as an identifier via the
// keyword_as_identifier rule.
//
// If the user wants to reference a catalog entity called 'clamped', they must
// identifier quote it (ENTITY `clamped`);
//
// AMBIGUOUS CASE 11: WITH OFFSET in <graph_with_operator>
// ----------------------------------
// In graph linear queries, there is a true ambiguity when using WITH following
// FOR <identifier> IN <expression>, which allows a trailing WITH OFFSET.
// KW_WITH can also be interpreted as the beginning of an immediately following
// <graph_with_operator>. The parser prefers to shfit by default, so
// that KW_WITH in this context is always interpreted as the beginning of
// WITH OFFSET [AS <alias>], which is the desired behavior.
//
// AMBIGUOUS CASE 12: WITH WEIGHT in <graph_sample_operator>
// ----------------------------------
// Similar to the above case, there is a true ambiguity when using WITH
// following the TABLESAMPLE clause in graph linear queries, which allows
// a trailing WITH WEIGHT [AS <alias>].
// The parser prefers to shift by default, so
// that KW_WITH in this context is always interpreted as the beginning of
// WITH WEIGHT [AS <alias>], which is the desired behavior.
//
// AMBIGUOUS CASE 13: Pipe INSERT
// ------------------------------
// The INSERT syntax has many optional modifiers.  Two of them come after the
// embedded (unparenthesized) query in an INSERT statement.
// If that embedded query ends with a pipe INSERT, trailing modifiers
// ASSERT_ROWS_MODIFIED or THEN RETURN are ambiguous, and could bind to
// either the inner or outer INSERT.
//
// Example:
//    INSERT INTO t1
//    FROM t2
//    |> INSERT INTO t3
//       ASSERT_ROWS_MODIFIED 100   # ambiguous dangling clause #1
//       THEN RETURN *              # ambiguous dangling clause #2
//
// Allowing the conflict makes these clauses bind to the inner pipe operator.
// This actually doesn't matter, since all of these statements (parsed either way)
// will be rejected at analysis time, since pipe INSERT is only allowed in
// query statements, not in DML statements like INSERT.
//
// AMBIGUOUS CASE 14: COST in <graph_element_pattern_filler>
// ----------------------------------
//  graph_element_pattern_filler:
//    hint?  graph_identifier-opt[elem_name] is_label_expression[label]?
//    graph_property_specification[prop_spec]?  where_clause[where]?
//    ("COST" expression[cost])?
//
// 'COST' is a non-reserved keyword that can be an identifier. There is a
// grammatical ambiguity for inputs such as:
//
// 1)  MATCH (COST ● COST
// 2)  MATCH (COST ● {
//
// At ● in #1, the parser must decide between:
// * Make the first 'COST' `elem_name`. Then the second 'COST' is the keyword
//   introducing `cost`.
// * Make the first 'COST' the keyword introducing `cost`. That means the second
//   'COST' is the first token of the cost expression.
//
// At ● in #2, the parser must decide between:
// * Make 'COST' `elem_name`. Then '{' opens prop_spec.
// * Make 'COST' the keyword introducing `cost`. Then '{' is the first token of
//   the cost expression.
//
// Case #1 is a limitation of the single token lookahead, but #2 is a true
// ambiguity.
//
// By using the _opt suffix operator on the elem_name rather than ? we force an
// artificial shift-reduce conflict for the first 'COST' token. The artifical
// conflict resolves to shift, which is the backward compatible choice. That
// conflict ends up being counted 5 times by Textmapper.
//
// The five cases give the following example inputs:
//  * MATCH ( ● COST
//  * MATCH ( hint ● COST
//  * MATCH - [ ● COST
//  * MATCH - [ hint ● COST
//  * MATCH < - [ ● COST
//
// AMBIGUOUS CASE 15: CALL PER()
// ------------------------------
// The PER keyword is non-reserved and can be used as an identifier.
// When the parser sees `CALL PER(`, it can either assume this is the PER clause
// and continue shifting, or it can assume this is already the tvf() (which is
// called "per", reducing the PER() and ON() clauses as if they do not exist.
// Allowing the resolution in favor of "shift" favors the PER clause.
// There are 2 instances because graph subquery has a different entry point from
// the main graph query.
//
// AMBIGUOUS CASE 16: INTERVAL expression identifier TO
// ------------------------------
// Usage of `interval_expression` in `within_range_clause` introduces
// ambiguity in parsing `interval_expression` when `TO` follows
// `INTERVAL expression identifier`. The parser can interpret `TO` as
// introducing the end of datetime part, e.g. `INTERVAL '2-11' YEAR TO MONTH`,
// or as introducing the upper bound of `within_range_clause`,
// e.g. `WITHIN (RANGE FROM INTERVAL 1 DAY TO ...)` where `INTERVAL 1 DAY`
// is the lower bound.
// The conflict is resolved by shifting `TO` to prefer parsing
// `INTERVAL ... TO ...`, which is the only option available based on how the
// grammar is currently structured. This is desired behavior for interval
// expression bound in `WITHIN (RANGE FROM ... TO ...)` as it must be followed
// by `PRECEDING` or `FOLLOWING`, e.g. `WITHIN (RANGE FROM INTERVAL 1 DAY
// PRECEDING TO ...)`. This is undesired behavior for timestamp expression bound
// in `WITHIN (RANGE FROM ... TO ...)` when it has interval expression as right
// factor, e.g `WITHIN (RANGE FROM CURRENT_TIMESTAMP() - INTERVAL 1 DAY TO ...)`
// and will require it to be parenthesized.
//
// AMBIGUOUS CASE 17: VALUE { ... }
// ------------------------------
// The VALUE keyword is non-reserved and can be used as an identifier.
// When the parser sees `VALUE {`, it can either:
//   * Reduce VALUE as a unit length path expression such that the open brace
//     is the start of a braced constructor suffix.
//   * Shift VALUE as the keyword opening a GQL braced subquery expression.
// This case may be solvable by pulling the braced VALUE subquery up into the
// primary non-terminals for expressions and parsing "VALUE" as an expression.
// Doing so naïvely introduces more conflicts because of the hint in
// the VALUE subquery, but they are potentially resolvable with careful
// adjustment.
//
// AMBIGUOUS CASE 19: COLUMNS(...)
// --------------------------------
// The COLUMNS keyword is non-reserved and can be used as an identifier. This
// causes one shift/reduce conflict between `keyword_as_identifier` and
// `column_list_spec_constructor`, because it starts with "COLUMNS" "(".
// It is resolved in favor of the column_list_spec_constructor, which is the
// desired behavior.
//
// AMBIGUOUS CASE 20: FUNCTION and MAP templated types
// ---------------------------------------------------
// The FUNCTION and MAP keywords are non-reserved and can be used as
// identifiers. When a type name is the suffix of an expression, such as happens
// in the `::` operator, the `<` following `MAP` or `FUNCTION` could either be
// opening a type template block (shift case) or be a less-than operator (reduce
// the keyword as identifier case). Textmapper's default resolution is for
// shift, which is the desired resolution in this case.
//
// AMBIGUOUS CASE 21: type_name followed by "."
// --------------------------------------------------------------
// When a type name is the suffix of an expression, such as happens in the `::`
// operator, the `.` following a complete path expression could either be
// continuing the type name (the shift case) or starting a field access on the
// result of the `::` operator (reducing the path expression as a type name).
// Textmapper's default resolution is for shift, which is the desired resolution
// in this case.
//
// AMBIGUOUS CASE 22: TABLE(...)
// -----------------------------
// The TABLE keyword is non-reserved and can be used as an identifier. With
// parenthesized TABLE(...) syntax in `table_clause_no_keyword`, "TABLE"
// followed by "(" causes one shift/reduce conflict between
// `setof_NonReservedKeyword` (for a function call `table(...)`) and
// `table_clause_unreserved`. It is resolved in favor of
// `table_clause_unreserved` (shift), which is the desired behavior.
//
// Total expected shift/reduce conflicts as described above:
//   1: SAFE CAST
//   2: CREATE TABLE CONSTRAINTS
//   1: REPLACE FIELDS
//   4: CREATE PROCEDURE
//   1: CREATE TABLE GENERATED
//   1: DESCRIPTOR
//   1: COLUMNS
//   1: ANALYZE
//   5: QUALIFY
//   2: ALTER COLUMN
//   1: SUM(SEQUENCE CLAMPED BETWEEN x and y)
//   1: SUM(MODEL CLAMPED BETWEEN x and y)
//   1: WITH OFFSET
//   1: WITH WEIGHT
//   2: Pipe INSERT
//   5: COST
//   2: CALL PER()
//   1: INTERVAL expression identifier TO
//   1: VALUE { ... }
//   1: Graph SET
//   2: FUNCTION and MAP templated types
//   1: type_name followed by "."
//   1: TABLE
%expect 41;

// Precedence for operator tokens. The operator precedence is defined from
// lowest to highest by the order of the declarations here, with tokens
// specified in the same declaration having the same precedence.
//
// Precedences are a total order, so resolving any conflict using precedence has
// non-local effects. Only use precedences that are widely globally accepted,
// like multiplication binding tighter than addition.
//
// The fake DOUBLE_AT_PRECEDENCE symbol is introduced to resolve a shift/reduce
// conflict in the system_variable_expression rule. A potentially ambiguous
// input is "@@a.b". Without modifying the rule's precedence, this could be
// parsed as a system variable named "a" of type STRUCT or as a system variable
// named "a.b" (the GoogleSQL language chooses the latter).
%left "OR";
%left "AND";
%nonassoc UNARY_NOT_PRECEDENCE;
%nonassoc "=" "<>" ">" "<" ">=" "<=" "!=" "LIKE" "IN" "DISTINCT" "BETWEEN" "IS" "NOT_SPECIAL" "+=" "-=" EDGE_ENDPOINT_PRECEDENCE;
%left "|";
%left "^";
%left "&";
%left "<<" ">>";
%left "+" "-";
%left "||";
%left "*" "/";
%nonassoc UNARY_PRECEDENCE;      // For all unary operators
%left "::" "?::";
%nonassoc DOUBLE_AT_PRECEDENCE;  // Needs to appear before "."

// The RUN statement in `next_statement_kind_without_hint` experiences a
// shift/reduce conflict when it has consumed a "complete" path expression and
// has a lookahead of either "." or "(".
//
// The expanded rules for the "(" case look like this:
//    | "RUN" path_expression      // This rule wants to reduce on lookahead "("
//    | "RUN" path_expression "("  // This rule wants to shift on lookahead "("
//
// Textmapper's default resolution rules compare the precedence of the rule
// that wants to reduce with the precedence of the lookahead token. A rule's
// precedence is taken from the precedence of the last terminal in the rule. In
// this case that is the "RUN" keyword. "RUN" has no specified precdence, so
// neither does the rule.
//
// To enable the conflict resolution logic we need the rule to have a precedence
// level. That means either giving a precedence level to the "RUN" keyword or
// giving a precedence level to the rule explicitly using %prec. Generally we
// want to keep precedences as local as possible, so we choose to give the rule
// an explicit precedence.
//
// We want a shift resolution. Textmapper's rules state that shift is chosen "if
// the lookahead token has higher precedence than the rule". Thus we add the
// RUN_PRECEDENCE token with precedence lower than the "(" token to serve this
// purpose.
//
// With the explicit precedence the conflict is resolved in favor of shift and
// the expanded rules look like this:
//    | "RUN" path_expression %prec RUN_PRECEDENCE
//    | "RUN" path_expression "("
%left RUN_PRECEDENCE;

// The symbols listed in this declaration are used to start suffix expressions.
// These tokens will be the lookahead token in various cases where the parser
// decides between reducing unparenthesized_expression_higher_prec_than_and or
// shifting a token to start another expression as a suffix of the just parsed
// expression. We have %prec levels like UNARY_PRECIDENCE, UNARY_NOT_PRECEDENCE,
// and EDGE_ENDPOINT_PRECEDENCE assigned to a bunch of rules that involve suffix
// expressions, but Textmapper requires both the rule and the lookahead token to
// have a precedence level to enable precedence based conflict resolution, hence
// the requirement to give precedence to "(", "[", "{", and ".". These operators
// share the highest precedence since they just need to have a precedence level
// to enable conflict resolution.
%left PRIMARY_PRECEDENCE "(" "[" "{" ".";

// The set of tokens that can start an expression.
%generate ExpressionFirst = set(first expression);

// The set of tokens that can start a graph operation block.
%generate GraphOperationBlockFirst =
    set(first graph_operation_block);

// The set of tokens that can follow "TABLE FUNCTION" as a schema object. In
// some of the no-eoi rules we need one more token after
// KW_FUNCTION_IN_TABLE_FUNCTION to prevent a shift/reduce conflict between
// "FUNCTION" being optional and the parse ending.
// * "IF" occurs because of 'CREATE TABLE FUNCTION IF NOT EXISTS ...'
// * path_expression occurs because of 'CREATE TABLE FUNCTION my.namespace...'
%generate FollowsTableFunction = set("IF" | first path_expression);

// The set of tokens that can follow the optional TYPE keyword in
// "CREATE [OR REPLACE] PROPERTY GRAPH [TYPE] ...". In the no-eoi
// next_statement_kind rule we need one more token after the optional
// KW_TYPE_IN_PROPERTY_GRAPH_TYPE to disambiguate PROPERTY GRAPH from
// PROPERTY GRAPH TYPE. Only the leading token of the graph name matters here,
// so this is the FIRST set of `identifier` plus "IF":
// * "IF" occurs because of 'CREATE PROPERTY GRAPH [TYPE] IF NOT EXISTS ...';
//   "IF" is reserved, so it is not part of `first identifier`.
// * `identifier` occurs because of 'CREATE PROPERTY GRAPH [TYPE] my.name...';
//   a path_expression begins with an identifier, so its first token is enough.
%generate FollowsGraph = set("IF" | first identifier);

// The set of tokens that can follow a column schema definition. The column
// schema includes a list of attributes wihtout a separator, so this is both
// the token that start each of those attributes and also the tokens that can
// follow the last attribute (if any).
//
// This is used to resolve ambiguity betwen the type name and the first
// attribute in the case that the column type is not specified, but inferred
// from context.
%generate TableColumnSchemaFollow = set(follow table_column_schema);

// The list of non-reserved keywords which are reserved as function names in
// `WITH identifier() AS ...` syntax.
// This is needed to resolve a reduce/reduce conflict that arises for input like
// `INSERT INTO table_name WITH TIMESTAMP ● ( ...`.
%generate WithFunctionNameReserved = set("TIMESTAMP" | "VERSION");

%generate WithFunctionNameNonReserved =
    set(NonReservedKeyword & ~WithFunctionNameReserved);

%input next_script_statement no-eoi,
       next_statement no-eoi,
       next_statement_kind no-eoi,
       script,
       sql_statement,
       standalone_expression,
       standalone_type;

semicolon_or_eoi:
    ";"
  | eoi
;

sql_statement:
    // Using `semicolon_or_eoi` here improves the error message.
    // TODO: Use semicolon_or_eoi rather than ";"?
    unterminated_sql_statement[stmt] ";"?
    {
      *ast_node_result = $stmt;
    }
;

next_script_statement:
    unterminated_statement[stmt] semicolon_or_eoi[stmt_end]
    {
      *statement_end_byte_offset = @stmt_end.end().GetByteOffset();
      *ast_node_result = $stmt;
    }
;

next_statement:
    unterminated_sql_statement[stmt] semicolon_or_eoi[stmt_end]
    {
      *statement_end_byte_offset = @stmt_end.end().GetByteOffset();
      *ast_node_result = $stmt;
    }
;

standalone_expression:
    expression
    {
      *ast_node_result = $expression;
    }
;

standalone_type:
    type
    {
      *ast_node_result = $type;
    }
;

// This rule exists to run an action before parsing a statement irrespective
// of whether or not the statement is a sql statement or a script statement.
// This shape is recommended in the Bison manual.
// See https://www.gnu.org/software/bison/manual/bison.html#Midrule-Conflicts
//
// We override the lookback of BEGIN here, and not locally in begin_end_block
// to avoid shift/reduce conflicts with the BEGIN TRANSACTION statement. The
// alternative lookback in this case meerly asserts that BEGIN is a keyword
// at the beginning of a statement, which is true both for being end blocks
// and for BEGIN TRANSACTION.
pre_statement:
    %empty
    {
      OVERRIDE_NEXT_TOKEN_LOOKBACK(KW_BEGIN, LB_BEGIN_AT_STATEMENT_START);
    }
;

unterminated_statement {ASTNode*}:
    pre_statement unterminated_sql_statement[stmt]
    {
      @$ = @stmt;
      $$ = $stmt;
    }
  | pre_statement unterminated_script_statement[stmt]
    {
      @$ = @stmt;
      $$ = $stmt;
    }
;

statement_level_hint {ASTNode*}:
    hint
    {
      OVERRIDE_CURRENT_TOKEN_LOOKBACK(@hint, LB_END_OF_STATEMENT_LEVEL_HINT);
      $$ = $hint;
    }
;

unterminated_sql_statement {ASTNode*}:
    sql_statement_body
  | statement_level_hint[hint] sql_statement_body
    {
      $$ = MakeNode<ASTHintedStatement>(@$, $hint, $sql_statement_body);
    }
  | "DEFINE"[kw_define] macro_visibility? "MACRO"
    {
      // If macros are disabled and the system is accepting macro definitions
      // it typically indicates a misconfiguration in the way parser is setup.
      GOOGLESQL_RETURN_IF_ERROR(ValidateMacroSupport(@kw_define));
      // Rule to capture wrong usage, where DEFINE MACRO is resulting from
      // expanding other macros, instead of being original user input.
      return MakeSyntaxError(
        @kw_define,
        "Syntax error: DEFINE MACRO statements cannot be composed from other "
        "expansions");
    }
  | statement_level_hint[hint] "DEFINE"[kw_define] macro_visibility? "MACRO"
    {
      // If macros are disabled and the system is accepting macro definitions
      // it typically indicates a misconfiguration in the way parser is setup.
      GOOGLESQL_RETURN_IF_ERROR(ValidateMacroSupport(@kw_define));
      return MakeSyntaxError(
        @hint, "Hints are not allowed on DEFINE MACRO statements.");
    }
  | statement_level_hint[hint] KW_DEFINE_FOR_MACROS macro_visibility? "MACRO"
    {
      // This is here for extra future-proofing. We should never hit this
      // codepath, because the expander should only generate
      // KW_DEFINE_FOR_MACROS when it's the first token in the statement,
      // ignoring comments.
      ABSL_DLOG(FATAL) << "KW_DEFINE_FOR_MACROS should only appear as the "
                      "first token in a statement.";
      return MakeSyntaxError(
        @hint, "Hints are not allowed on DEFINE MACRO statements");
    }
;

unterminated_unlabeled_script_statement {ASTNode*}:
    begin_end_block
  | while_statement
  | loop_statement
  | repeat_statement
  | for_in_statement
;

unterminated_script_statement {ASTNode*}:
    if_statement
  | case_statement
  | variable_declaration
  | break_statement
  | continue_statement
  | return_statement
  | raise_statement
  | unterminated_unlabeled_script_statement
  | label ":"
      // We override the lookback of BEGIN here, and not locally in
      // begin_end_block to avoid shift/reduce conflicts with the
      // BEGIN TRANSACTION statement in the higher level unterminated_statement
      // rule. In this case we provide the LB_OPEN_STATEMENT_BLOCK lookback
      // because only BEGIN of a statement block can follow a label. If
      // BEGIN TRANSACTION is changed to accept a label, then we want to change
      // this lookback to `LB_BEGIN_AT_STATEMENT_START`.
    {
      OVERRIDE_NEXT_TOKEN_LOOKBACK(KW_BEGIN, LB_OPEN_STATEMENT_BLOCK);
    }
    unterminated_unlabeled_script_statement[stmt] opt_identifier[end_label]
    {
      GOOGLESQL_RETURN_IF_ERROR(ValidateLabelSupport($label, @label));
      if ($end_label != nullptr &&
          !$end_label->GetAsIdString().CaseEquals($label->GetAsIdString())) {
        return MakeSyntaxError(
            @end_label,
            absl::StrCat("Mismatched end label; expected ",
                          $label->GetAsStringView(), ", got ",
                          $end_label->GetAsStringView()));
      }
      auto label = MakeNode<ASTLabel>(@label, $label);
      ExtendNodeLeft($stmt, label);
      $$ = WithLocation($stmt, @$);
    }
;

sql_statement_body {ASTNode*}:
    sql_statement_body_no_pipe_suffix[stmt]
  | sql_statement_body_maybe_pipe_suffix[stmt] pipe_and_pipe_operator*[pipe_ops]
    {
      if ($pipe_ops.empty()) {
        $$ = $stmt;
      } else if(language_options.LanguageFeatureEnabled(
                    FEATURE_STATEMENT_WITH_PIPE_OPERATORS)) {
        auto pipe_suffix =
            MakeNode<ASTSubpipelineStatement>(@pipe_ops,
                MakeNode<ASTSubpipeline>(@pipe_ops, $pipe_ops));
        $$ = MakeNode<ASTStatementWithPipeOperators>(@$, $stmt, pipe_suffix);
      } else {
        return MakeSyntaxError(@pipe_ops,
                   "Pipe operators are not supported on this statement");
      }
    }
  ;

sql_statement_body_no_pipe_suffix {ASTNode*}:
  # These statements can include a query as a suffix so they can't take more
  # pipe operators as an ASTStatementWithPipeOperators.
    query_statement
  | subpipeline_statement
  | create_external_table_function_statement
  | create_external_table_statement
  | create_model_statement
  | create_table_function_statement
  | create_table_statement
  | create_live_table_statement
  | create_view_statement
  | explain_statement
  | export_data_statement
  | export_model_statement
  #
  # These statements can't possibly return a table, so accepting a pipe suffix
  # doesn't make sense on them.  Anything that *might* return a table
  # should go in `sql_statement_body_maybe_pipe_suffix` below.
  #
  | alter_statement
  | begin_statement
  | analyze_statement
  | assert_statement
  | aux_load_data_statement
  | clone_data_statement
  | merge_statement
  | truncate_statement
  | set_statement
  | commit_statement
  | start_batch_statement
  | run_batch_statement
  | abort_batch_statement
  | create_constant_statement
  | create_connection_statement
  | create_database_statement
  | create_function_statement
  | create_procedure_statement
  | create_index_statement
  | create_privilege_restriction_statement
  | create_row_access_policy_statement
  | create_data_policy_statement
  | create_sequence_statement
  | create_locality_group_statement
  | create_property_graph_statement
  | create_property_graph_type_statement
  | create_schema_statement
  | create_external_schema_statement
  | create_snapshot_statement
  | create_entity_statement
  | define_macro_statement
  | define_table_statement
  | export_metadata_statement
  | grant_statement
  | rename_statement
  | revoke_statement
  | rollback_statement
  | drop_all_row_access_policies_statement
  | drop_statement
  | import_statement
  | module_statement
  | undrop_statement
  # This should allow pipe suffixes but is currently excluded because this
  # resolves as a ResolvedQueryStmt, so having an unresolved pipe suffix
  # confuses the SQLBuilder and some analyzer tests.
  | gql_statement
  # This should allow pipe suffixes on DML statements with THEN RETURN but
  # it's excluded for now because that causes parser conflicts.
  | dml_statement
;

# This includes all statements which could potentially return a table, and
# therefore allow pipe operator suffixes using ASTStatementWithPipeOperators
# if FEATURE_STATEMENT_WITH_PIPE_OPERATORS is enabled.
#
# The statements are only valid when they return return exactly one output
# table, but this isn't determined at parse time.
# This must be checked later by whatever executes the statement, giving an
# error if the initial statement does not return exactly one table.
sql_statement_body_maybe_pipe_suffix {ASTNode*}:
  # Keep in sync with `validator.cc` code checking which statements are allowed.
    call_statement
  | describe_statement
  | execute_immediate
  | run_statement
  | show_statement
;

run_statement_arg {ASTNamedArgument*}: identifier[name] ("=" | "=>") string_literal[expr]
    {
      $$ = MakeNode<ASTNamedArgument>(@$, $name, $expr);
    }
;

run_statement_arg_list {std::vector<ASTNamedArgument*>}: (run_statement_arg separator ",")+[args] ","?
    {
      $$ = $args;
    }
;

run_statement {ASTNode*}: "RUN" string_literal[path] ("(" (run_statement_arg_list)? ")" )?
    {
      if(!language_options.LanguageFeatureEnabled(FEATURE_RUN_STATEMENT)) {
        return MakeSyntaxError(@1, "RUN statement is not supported.");
      }
      $$ = MakeNode<ASTRunStatement>(@$, $path, $run_statement_arg_list);
    }
  | "RUN" path_expression[path] "(" run_statement_arg_list? ")"
    {
      if(!language_options.LanguageFeatureEnabled(FEATURE_RUN_STATEMENT)) {
        return MakeSyntaxError(@1, "RUN statement is not supported.");
      }
      $$ = MakeNode<ASTRunStatement>(@$, $path, $run_statement_arg_list);
    }
;

define_macro_statement {ASTNode*}:
    // Use a special version of KW_DEFINE which indicates that this macro
    // definition was "original" (i.e., not expanded from other macros), and
    // is top-level (i.e., not nested under other statements or blocks like IF).
    KW_DEFINE_FOR_MACROS macro_visibility[visibility]? "MACRO" identifier[name]
    set(~(";" | eoi | invalid_token))*[body]
    {
      auto* body = MakeNode<ASTMacroBody>(@body);
      auto* node = MakeNode<ASTDefineMacroStatement>(@$, $name, body);
      if ($visibility.has_value()) {
        node->set_visibility(*$visibility);
      }
      $$ = node;
    }
;

macro_visibility {ASTDefineMacroStatement::MacroVisibility}:
    "PUBLIC"
    {
      $$ = ASTDefineMacroStatement::PUBLIC;
    }
  | "PRIVATE"
    {
      $$ = ASTDefineMacroStatement::PRIVATE;
    }
;

query_statement {ASTNode*}:
    query
    {
      $$ = MakeNode<ASTQueryStatement>(@$, $1);
    }
;

subpipeline_statement {ASTNode*}:
    subpipeline_no_parens
    {
      $$ = MakeNode<ASTSubpipelineStatement>(@$, $1);
    }
;

alter_action {ASTNode*}:
    "SET" "OPTIONS" options_list
    {
      $$ = MakeNode<ASTSetOptionsAction>(@$, $3);
    }
  | "SET" "AS" generic_entity_body[body]
      // See (broken link)
    {
      $$ = MakeNode<ASTSetAsAction>(@$, $body);
    }
  | "ADD" table_constraint_spec
    {
      $$ = MakeNode<ASTAddConstraintAction>(@$, $2);
    }
  | "ADD" primary_key_spec
    {
      $$ = MakeNode<ASTAddConstraintAction>(@$, $2);
    }
  | "ADD" "CONSTRAINT" opt_if_not_exists identifier[name]
        primary_key_or_table_constraint_spec[constraint]
    {
      ExtendNodeRight($constraint, @constraint.end(), $name);
      WithStartLocation($constraint, @name.start());
      auto* node = MakeNode<ASTAddConstraintAction>(@$, $constraint);
      node->set_is_if_not_exists($3);
      $$ = node;
    }
  | "DROP" "CONSTRAINT" opt_if_exists identifier
    {
      auto* node =
          MakeNode<ASTDropConstraintAction>(@$, $4);
      node->set_is_if_exists($3);
      $$ = node;
    }
  | "DROP" "PRIMARY" "KEY" opt_if_exists
    {
      auto* node = MakeNode<ASTDropPrimaryKeyAction>(@$);
      node->set_is_if_exists($4);
      $$ = node;
    }
  | "ALTER" "CONSTRAINT" opt_if_exists identifier constraint_enforcement
    {
      auto* node =
          MakeNode<ASTAlterConstraintEnforcementAction>(@$, $4);
      node->set_is_if_exists($3);
      node->set_is_enforced($5);
      $$ = node;
    }
  | "ALTER" "CONSTRAINT" opt_if_exists identifier "SET" "OPTIONS" options_list
    {
      auto* node =
          MakeNode<ASTAlterConstraintSetOptionsAction>(@$, $4, $7);
      node->set_is_if_exists($3);
      $$ = node;
    }
  | "ADD" "COLUMN" if_not_exists? table_column_definition[definition]
        column_position[psn]? ("FILL" "USING" expression[fill])?
    {
      auto* node = MakeNode<ASTAddColumnAction>(@$, $definition, $psn, $fill);
      node->set_is_if_not_exists(@if_not_exists.has_value());
      $$ = node;
    }
  | "ADD" "STORED" "COLUMN" if_not_exists? identifier[column]
    {
      auto* node = MakeNode<ASTAddStoredColumnAction>(@$, $column);
      node->set_is_if_not_exists(@if_not_exists.has_value());
      $$ = node;
    }
  | "DROP" "STORED" "COLUMN" opt_if_exists identifier[column]
    {
      auto* node = MakeNode<ASTDropStoredColumnAction>(@$, $column);
      node->set_is_if_exists($opt_if_exists);
      $$ = node;
    }
  | "DROP" "COLUMN" opt_if_exists identifier
    {
      auto* node = MakeNode<ASTDropColumnAction>(@$, $4);
      node->set_is_if_exists($3);
      $$ = node;
    }
  | "RENAME" "COLUMN" opt_if_exists identifier "TO" identifier
    {
      auto* node = MakeNode<ASTRenameColumnAction>(@$, $4, $6);
      node->set_is_if_exists($3);
      $$ = node;
    }
  | "ALTER" "COLUMN" opt_if_exists identifier "SET" "DATA" "TYPE"
          field_schema
    {
      auto* node = MakeNode<ASTAlterColumnTypeAction>(@$, $4, $8);
      node->set_is_if_exists($3);
      $$ = node;
    }
  | "ALTER" "COLUMN" opt_if_exists identifier "SET" "OPTIONS" options_list
    {
      auto* node = MakeNode<ASTAlterColumnOptionsAction>(@$, $4, $7);
      node->set_is_if_exists($3);
      $$ = node;
    }
  | "ALTER" "COLUMN" opt_if_exists identifier "SET" "DEFAULT" expression
    {
      auto* node = MakeNode<ASTAlterColumnSetDefaultAction>(@$, $4, $7);
      node->set_is_if_exists($3);
      $$ = node;
    }
  | "ALTER" "COLUMN" opt_if_exists identifier "DROP" "DEFAULT"
    {
      auto* node = MakeNode<ASTAlterColumnDropDefaultAction>(@$, $4);
      node->set_is_if_exists($3);
      $$ = node;
    }
  | "ALTER" "COLUMN" opt_if_exists identifier "DROP" "NOT" "NULL"
    {
      auto* node = MakeNode<ASTAlterColumnDropNotNullAction>(@$, $4);
      node->set_is_if_exists($3);
      $$ = node;
    }
  | "ALTER" "COLUMN" opt_if_exists identifier "SET" "GENERATED"
          generated_column_info_for_alter_column_action[gen_col_info]
    {
      auto* node = MakeNode<ASTAlterColumnSetGeneratedAction>(
          @$, $identifier, $gen_col_info);
      node->set_is_if_exists($opt_if_exists);
      $$ = node;
    }
  | "ALTER" "COLUMN" opt_if_exists identifier "DROP" "GENERATED"
    {
      auto* node = MakeNode<ASTAlterColumnDropGeneratedAction>(@$, $identifier);
      node->set_is_if_exists($opt_if_exists);
      $$ = node;
    }
  | "RENAME" "TO" path_expression
    {
      $$ = MakeNode<ASTRenameToClause>(@$, $3);
    }
  | "SET" "DEFAULT" collate_clause
    {
      $$ = MakeNode<ASTSetCollateClause>(@$, $3);
    }
  | "ADD" "ROW" "DELETION" "POLICY" opt_if_not_exists "(" expression ")"
    {
      if (!language_options.LanguageFeatureEnabled(FEATURE_TTL)) {
        return MakeSyntaxError(@2,
            "ADD ROW DELETION POLICY clause is not supported.");
      }
      auto* node = MakeNode<ASTAddTtlAction>(@$, $7);
      node->set_is_if_not_exists($5);
      $$ = node;
    }
  | "REPLACE" "ROW" "DELETION" "POLICY" opt_if_exists "(" expression ")"
    {
      if (!language_options.LanguageFeatureEnabled(FEATURE_TTL)) {
        return MakeSyntaxError(@2,
            "REPLACE ROW DELETION POLICY clause is not supported.");
      }
      auto* node = MakeNode<ASTReplaceTtlAction>(@$, $7);
      node->set_is_if_exists($5);
      $$ = node;
    }
  | "DROP" "ROW" "DELETION" "POLICY" opt_if_exists
    {
      if (!language_options.LanguageFeatureEnabled(FEATURE_TTL)) {
        return MakeSyntaxError(@2,
            "DROP ROW DELETION POLICY clause is not supported.");
      }
      auto* node = MakeNode<ASTDropTtlAction>(@$);
      node->set_is_if_exists($5);
      $$ = node;
    }
  | "ALTER" generic_sub_entity_type opt_if_exists identifier alter_action
    {
      auto* node = MakeNode<ASTAlterSubEntityAction>(@$, $2, $4, $5);
      node->set_is_if_exists($3);
      $$ = node;
    }
  | "ADD" generic_sub_entity_type opt_if_not_exists identifier
      opt_options_list
    {
      auto* node = MakeNode<ASTAddSubEntityAction>(@$, $2, $4, $5);
      node->set_is_if_not_exists($3);
      $$ = node;
    }
  | "DROP" generic_sub_entity_type opt_if_exists identifier
    {
      auto* node = MakeNode<ASTDropSubEntityAction>(@$, $2, $4);
      node->set_is_if_exists($3);
      $$ = node;
    }
  | "REBUILD"
    {
      $$ = MakeNode<ASTRebuildAction>(@$);
    }
  | spanner_alter_column_action
  | spanner_set_on_delete_action
;

alter_action_list {ASTNode*}:
    (alter_action separator ",")+[actions]
    {
      $$ = MakeNode<ASTAlterActionList>(@$, $actions);
    }
;

// This is split up from the other ALTER actions since the alter actions for
// PRIVILEGE RESTRICTION are only used by PRIVILEGE RESTRICTION at the moment.
privilege_restriction_alter_action {ASTNode*}:
    restrict_to_clause
  | "ADD" opt_if_not_exists possibly_empty_grantee_list
    {
      auto* node = MakeNode<ASTAddToRestricteeListClause>(@$, $3);
      node->set_is_if_not_exists($2);
      $$ = node;
    }
  | "REMOVE" opt_if_exists possibly_empty_grantee_list
    {
      auto* node = MakeNode<ASTRemoveFromRestricteeListClause>(@$, $3);
      node->set_is_if_exists($2);
      $$ = node;
    }
;

// This is split up from the other ALTER actions since the alter actions for
// ROW ACCESS POLICY are only used by ROW ACCESS POLICY at the moment.
row_access_policy_alter_action {ASTNode*}:
    grant_to_clause
  | "FILTER" "USING" "(" expression ")"
    {
      ASTFilterUsingClause* node = MakeNode<ASTFilterUsingClause>(@$, $4);
      node->set_has_filter_keyword(true);
      $$ = node;
    }
  | "REVOKE" "FROM" "(" grantee_list ")"
    {
      $$ = MakeNode<ASTRevokeFromClause>(@$, $4);
    }
  | "REVOKE" "FROM" "ALL"
    {
      ASTRevokeFromClause* node = MakeNode<ASTRevokeFromClause>(@$);
      node->set_is_revoke_from_all(true);
      $$ = node;
    }
  | "RENAME" "TO" identifier
    {
      ASTPathExpression* id = MakeNode<ASTPathExpression>(@3, $3);
      $$ = MakeNode<ASTRenameToClause>(@$, id);
    }
;

// Note - this excludes the following objects:
// - ROW ACCESS POLICY for tactical reasons, since the production rules for
//   ALTER and DROP require very different syntax for ROW ACCESS POLICY as
//   compared to other object kinds.  So we do not want to match
//   ROW ACCESS POLICY here.
// - TABLE, TABLE FUNCTION, and SNAPSHOT TABLE since we use different production
//   for table path expressions (one which may contain dashes).
// - SEARCH INDEX since the DROP SEARCH INDEX has an optional ON <table> clause.
// - VECTOR INDEX since the DROP VECTOR INDEX has an optional ON <table> clause.
schema_object_kind {SchemaObjectKind}:
    "AGGREGATE" "FUNCTION"
    {
      $$ = SchemaObjectKind::kAggregateFunction;
    }
  | "APPROX" "VIEW"
    {
      $$ = SchemaObjectKind::kApproxView;
    }
  | "CONNECTION"
    {
      $$ = SchemaObjectKind::kConnection;
    }
  | "CONSTANT"
    {
      $$ = SchemaObjectKind::kConstant;
    }
  | "DATABASE"
    {
      $$ = SchemaObjectKind::kDatabase;
    }
  | "EXTERNAL" "TABLE" .TableFunction KW_FUNCTION_IN_TABLE_FUNCTION[fn]?
    {
      if (@fn.has_value()) {
        return MakeSyntaxError(@1, "EXTERNAL TABLE FUNCTION is not supported");
      } else {
        $$ = SchemaObjectKind::kExternalTable;
      }
    }
  | "EXTERNAL" "SCHEMA"
    {
      $$ = SchemaObjectKind::kExternalSchema;
    }
  | "FUNCTION"
    {
      $$ = SchemaObjectKind::kFunction;
    }
  | index_type? "INDEX"
    {
      switch ($index_type.value_or(IndexType::INDEX_DEFAULT)) {
        case IndexType::INDEX_DEFAULT:
          $$ = SchemaObjectKind::kIndex;
          break;
        case IndexType::INDEX_SEARCH:
          $$ = SchemaObjectKind::kSearchIndex;
          break;
        case IndexType::INDEX_VECTOR:
          $$ = SchemaObjectKind::kVectorIndex;
          break;
        case IndexType::INDEX_AI:
          $$ = SchemaObjectKind::kAiIndex;
          break;
      }
    }
  | "LIVE" "TABLE"
    {
      $$ = SchemaObjectKind::kLiveTable;
    }
  | "MATERIALIZED" "VIEW"
    {
      $$ = SchemaObjectKind::kMaterializedView;
    }
  | "MODEL"
    {
      $$ = SchemaObjectKind::kModel;
    }
  | "PROCEDURE"
    {
      $$ = SchemaObjectKind::kProcedure;
    }
  | "SCHEMA"
    {
      $$ = SchemaObjectKind::kSchema;
    }
  | "SEQUENCE"
    {
      $$ = SchemaObjectKind::kSequence;
    }
  | "VIEW"
    {
      $$ = SchemaObjectKind::kView;
    }
  | "PROPERTY" "GRAPH" .PropertyGraphType
    {
      $$ = SchemaObjectKind::kPropertyGraph;
    }
  | "PROPERTY" "GRAPH" .PropertyGraphType KW_TYPE_IN_PROPERTY_GRAPH_TYPE
    {
      $$ = SchemaObjectKind::kPropertyGraphType;
    }
;

alter_statement {ASTNode*}:
    "ALTER" "TABLE" .TableFunction KW_FUNCTION_IN_TABLE_FUNCTION[fn]?
      if_exists? maybe_dashed_path_expression[name] alter_action_list[actions]
    {
      if (@fn.has_value()) {
        return MakeSyntaxError(@2, "ALTER TABLE FUNCTION is not supported");
      }
      auto* node = MakeNode<ASTAlterTableStatement>(@$, $name, $actions);
      node->set_is_if_exists(@if_exists.has_value());
      $$ = node;
    }
  | "ALTER" schema_object_kind[kind] if_exists? path_expression[name]
      ("ON" path_expression[on])? alter_action_list[actions]
    {
      ASTAlterStatementBase* node = nullptr;
      switch ($kind) {
        case SchemaObjectKind::kApproxView:
          node = MakeNode<ASTAlterApproxViewStatement>(@$);
          break;
        case SchemaObjectKind::kConnection:
          node = MakeNode<ASTAlterConnectionStatement>(@$);
          break;
        case SchemaObjectKind::kDatabase:
          node = MakeNode<ASTAlterDatabaseStatement>(@$);
          break;
        case SchemaObjectKind::kSchema:
          node = MakeNode<ASTAlterSchemaStatement>(@$);
          break;
        case SchemaObjectKind::kExternalSchema:
          node = MakeNode<ASTAlterExternalSchemaStatement>(@$);
          break;
        case SchemaObjectKind::kView:
          node = MakeNode<ASTAlterViewStatement>(@$);
          break;
        case SchemaObjectKind::kMaterializedView:
          node = MakeNode<ASTAlterMaterializedViewStatement>(@$);
          break;
        case SchemaObjectKind::kModel:
          node = MakeNode<ASTAlterModelStatement>(@$);
          break;
        case SchemaObjectKind::kSequence:
          node = MakeNode<ASTAlterSequenceStatement>(@$);
          break;
        case SchemaObjectKind::kSearchIndex: {
          auto* alter =
              MakeNode<ASTAlterIndexStatement>(@$, $name, $on, $actions);
          alter->set_index_type(ASTAlterIndexStatement::INDEX_SEARCH);
          alter->set_is_if_exists(@if_exists.has_value());
          $$ = alter;
          return absl::OkStatus();
        }
        case SchemaObjectKind::kVectorIndex: {
          auto* alter =
              MakeNode<ASTAlterIndexStatement>(@$, $name, $on, $actions);
          alter->set_index_type(ASTAlterIndexStatement::INDEX_VECTOR);
          alter->set_is_if_exists(@if_exists.has_value());
          $$ = alter;
          return absl::OkStatus();
        }
        case SchemaObjectKind::kAiIndex: {
          auto* alter =
              MakeNode<ASTAlterIndexStatement>(@$, $name, $on, $actions);
          alter->set_index_type(ASTAlterIndexStatement::INDEX_AI);
          alter->set_is_if_exists(@if_exists.has_value());
          $$ = alter;
          return absl::OkStatus();
        }
        default: {
          absl::string_view kind_name = SchemaObjectKindToName($kind);
          return MakeSyntaxError(
              @kind, absl::StrCat("ALTER ", absl::AsciiStrToUpper(kind_name),
                                  " is not supported"));
        }
      }
      if ($on.has_value()) {
        absl::string_view kind_name = SchemaObjectKindToName($kind);
        return MakeSyntaxError(
            @on, absl::StrCat("ALTER ", absl::AsciiStrToUpper(kind_name),
                              " does not support the ON clause"));
      }
      node->set_is_if_exists(@if_exists.has_value());
      $$ = ExtendNodeRight(node, $name, $actions);
    }
  | "ALTER" generic_entity_type opt_if_exists path_expression
      alter_action_list
    {
      auto* node = MakeNode<ASTAlterEntityStatement>(@$, $2, $4, $5);
      node->set_is_if_exists($3);
      $$ = node;
    }
  | "ALTER" generic_entity_type opt_if_exists alter_action_list
    {
      auto* node = MakeNode<ASTAlterEntityStatement>(@$, $2, $4);
      node->set_is_if_exists($3);
      $$ = node;
    }
  | "ALTER" "PRIVILEGE" "RESTRICTION" opt_if_exists
      "ON" privilege_list "ON" identifier path_expression
      (privilege_restriction_alter_action separator ",")+[actions]
    {
      auto* actions_list = MakeNode<ASTAlterActionList>(@actions, $actions);
      auto* alter_privilege_restriction = MakeNode<ASTAlterPrivilegeRestrictionStatement>(@$, $6, $8, $9, actions_list);
      alter_privilege_restriction->set_is_if_exists($4);
      $$ = alter_privilege_restriction;
    }
  | "ALTER" "ROW" "ACCESS" "POLICY" opt_if_exists identifier "ON"
      path_expression (row_access_policy_alter_action separator ",")+[actions]
    {
      auto* actions_list = MakeNode<ASTAlterActionList>(@actions, $actions);
      ASTAlterRowAccessPolicyStatement* node = MakeNode<ASTAlterRowAccessPolicyStatement>(@$, $6, $8, actions_list);
      node->set_is_if_exists($5);
      $$ = node;
    }
  | "ALTER" "ALL" "ROW" "ACCESS" "POLICIES" "ON" path_expression
      row_access_policy_alter_action
    {
      $$ = MakeNode<ASTAlterAllRowAccessPoliciesStatement>(@$, $7, $8);
    }
  | alter_data_policy_statement
;

// Uses table_element_list to reduce redundancy.
// However, constraints clauses are not allowed.
input_output_clause {ASTInputOutputClause*}:
    "INPUT" table_element_list[input] "OUTPUT" table_element_list[output]
    {
      auto* input = $input->GetAsOrDie<ASTTableElementList>();
      if (input->HasConstraints()) {
        return MakeSyntaxError(@input,
              "Syntax error: Element list contains unexpected constraint");
      }
      auto* output = $output->GetAsOrDie<ASTTableElementList>();
      if (output->HasConstraints()) {
        return MakeSyntaxError(@output,
              "Syntax error: Element list contains unexpected constraint");
      }
      $$ = MakeNode<ASTInputOutputClause>(@$, $input, $output);
    }
;

transform_clause {ASTTransformClause*}:
    "TRANSFORM" "(" select_list ")"
    {
      $$ = MakeNode<ASTTransformClause>(@$, $select_list);
    }
;

assert_statement {ASTNode*}:
    "ASSERT" expression ("AS" string_literal[description])?
    {
      $$ = MakeNode<ASTAssertStatement>(@$, $expression, $description);
    }
;

analyze_statement {ASTNode*}:
    "ANALYZE" options-opt (table_and_column_info separator ",")*[info]
    {
      auto info_list = MakeListNode<ASTTableAndColumnInfoList>(@info, $info);
      $$ = MakeNode<ASTAnalyzeStatement>(@$, $options, info_list);
    }
;

table_and_column_info {ASTNode*}:
    maybe_dashed_path_expression opt_column_list
    {
      $$ = MakeNode<ASTTableAndColumnInfo>(@$, $1, $2);
    }
;

transaction_mode {ASTNode*}:
    "READ" "ONLY"
    {
      auto* node = MakeNode<ASTTransactionReadWriteMode>(@$);
      node->set_mode(ASTTransactionReadWriteMode::READ_ONLY);
      $$ = node;
    }
  | "READ" "WRITE"
    {
      auto* node = MakeNode<ASTTransactionReadWriteMode>(@$);
      node->set_mode(ASTTransactionReadWriteMode::READ_WRITE);
      $$ = node;
    }
  | "ISOLATION" "LEVEL" identifier
    {
      $$ = MakeNode<ASTTransactionIsolationLevel>(@$, $3);
    }
  | "ISOLATION" "LEVEL" identifier identifier
    {
      $$ = MakeNode<ASTTransactionIsolationLevel>(@$, $3, $4);
    }
;

begin_statement {ASTNode*}:
    ("START" "TRANSACTION" | "BEGIN" "TRANSACTION"?) (transaction_mode separator ",")*[modes]
    {
      $$ = MakeNode<ASTBeginStatement>(
          @$, MakeListNode<ASTTransactionModeList>(@modes, $modes));
    }
;

set_statement {ASTNode*}:
    "SET" "TRANSACTION" (transaction_mode separator ",")+[modes]
    {
      $$ = MakeNode<ASTSetTransactionStatement>(
          @$, MakeListNode<ASTTransactionModeList>(@modes, $modes));
    }
  | "SET" identifier "=" expression
    {
      $$ = MakeNode<ASTSingleAssignment>(@$, $2, $4);
    }
  | "SET" named_parameter_expression "=" expression
    {
      $$ = MakeNode<ASTParameterAssignment>(@$, $2, $4);
    }
  | "SET" system_variable_expression "=" expression
    {
      $$ = MakeNode<ASTSystemVariableAssignment>(@$, $2, $4);
    }
  | "SET" "(" identifier_list ")" "=" expression
    {
      $$ = MakeNode<ASTAssignmentFromStruct>(@$, $3, $6);
    }
  | "SET" "(" ")"
    {
      // Provide improved error message for an empty variable list.
      return MakeSyntaxError(@3,
        "Parenthesized SET statement requires a variable list");
    }
  | "SET" identifier "," identifier_list "="
    {
      // Provide improved error message for missing parentheses around a
      // list of multiple variables.
      return MakeSyntaxError(@2,
        "Using SET with multiple variables requires parentheses around the "
        "variable list");
    }
;

commit_statement {ASTNode*}:
    "COMMIT" "TRANSACTION"?
    {
      $$ = MakeNode<ASTCommitStatement>(@$);
    }
;

rollback_statement {ASTNode*}:
    "ROLLBACK" "TRANSACTION"?
    {
      $$ = MakeNode<ASTRollbackStatement>(@$);
    }
;

start_batch_statement {ASTNode*}:
    "START" "BATCH" opt_identifier
    {
      $$ = MakeNode<ASTStartBatchStatement>(@$, $3);
    }
;

run_batch_statement {ASTNode*}:
    "RUN" "BATCH"
    {
      $$ = MakeNode<ASTRunBatchStatement>(@$);
    }
;

abort_batch_statement {ASTNode*}:
    "ABORT" "BATCH"
    {
      $$ = MakeNode<ASTAbortBatchStatement>(@$);
    }
;

create_constant_statement {ASTNode*}:
    "CREATE" opt_or_replace opt_create_scope "CONSTANT" opt_if_not_exists
    path_expression "=" expression
    {
      auto* create = MakeNode<ASTCreateConstantStatement>(@$, $6, $8);
      create->set_is_or_replace($2);
      create->set_scope($3);
      create->set_is_if_not_exists($5);
      $$ = create;
    }
;

create_database_statement {ASTNode*}:
    "CREATE" "DATABASE" path_expression opt_options_list
    {
      $$ = MakeNode<ASTCreateDatabaseStatement>(@$, $3, $4);
    }
;

unordered_options_body {std::pair<ASTNode*, ASTNode*>}:
    options opt_as_sql_function_body_or_string[body]
    {
      $$ = std::make_pair($options, $body);
    }
  | as_sql_function_body_or_string[body] opt_options_list[options]
    {
      if ($options != nullptr) {
        GOOGLESQL_RETURN_IF_ERROR(warning_sink.AddWarning(
            DeprecationWarning::LEGACY_FUNCTION_OPTIONS_PLACEMENT,
            MakeSqlErrorAtStart(@options)
                << "The preferred style places the OPTIONS clause before the "
                << "function body."));
      }
      $$ = std::make_pair($options, $body);
    }
;

create_function_statement {ASTNode*}:
    // The preferred style is LANGUAGE OPTIONS BODY but LANGUAGE BODY OPTIONS
    // is allowed for backwards compatibility (with a deprecation warning).
    "CREATE" opt_or_replace opt_create_scope "AGGREGATE"[ag]? "FUNCTION"
    opt_if_not_exists function_declaration
    ("RETURNS" (type | templated_parameter_type))?
    sql_security? determinism_level?
    language_or_remote_with_connection[language]?
    unordered_options_body[uob]?
    {
      if (@templated_parameter_type.has_value()) {
        return MakeSyntaxError(
            *@templated_parameter_type, "Syntax error: "
            "Templated types are not allowed in the RETURNS clause");
      }
      auto [options, body] =
          $uob.has_value() ? *$uob : std::make_pair(nullptr, nullptr);
      auto [language, is_remote, connection] =
          $language.has_value() ? *$language : std::make_tuple(nullptr, false, nullptr);
      auto* create = MakeNode<ASTCreateFunctionStatement>(@$,
          $function_declaration, $type, language, connection, body, options);
      create->set_is_or_replace($opt_or_replace);
      create->set_scope($opt_create_scope);
      create->set_is_aggregate(@ag.has_value());
      create->set_is_if_not_exists($opt_if_not_exists);
      create->set_sql_security($sql_security.value_or(ASTCreateStatement::SQL_SECURITY_UNSPECIFIED));
      create->set_determinism_level($determinism_level.value_or(ASTCreateFunctionStmtBase::DETERMINISM_UNSPECIFIED));
      create->set_is_remote(is_remote);
      $$ = create;
    }
;

function_declaration {ASTNode*}:
    path_expression function_parameters
    {
      $$ = MakeNode<ASTFunctionDeclaration>(@$, $1, $2);
    }
;

function_parameter {ASTFunctionParameter*}:
    identifier[name]? type_or_tvf_schema[type] ("AS"[alias_as] identifier[alias])?
    opt_default_expression[default] ("NOT" "AGGREGATE"[not_aggregate])?
    {
      if ($default != nullptr && !$name.has_value()) {
        return MakeSyntaxError(@default, "Syntax error: Default expression is only allowed for named parameters");
      }
      ASTAlias* alias = nullptr;
      if ($alias.has_value()) {
        alias = WithStartLocation(MakeNode<ASTAlias>(*@alias, $alias), *@alias_as);
      }
      $$ = MakeNode<ASTFunctionParameter>(@$, $name, $type, alias, $default);
      $$->set_is_not_aggregate(@not_aggregate.has_value());
    }
;

function_parameters {ASTNode*}:
    "(" (function_parameter separator ",")*[params] ")"
    {
      $$ = MakeNode<ASTFunctionParameters>(@$, $params);
    }
;

create_procedure_statement {ASTNode*}:
    "CREATE" opt_or_replace opt_create_scope "PROCEDURE" opt_if_not_exists
    path_expression[name] procedure_parameters[params]
    ("EXTERNAL" "SECURITY" ("INVOKER"[invoker] | "DEFINER"[definer]))?
    with_connection_clause[conn]? options?
    (begin_end_block[begin_end] | "LANGUAGE"[lang] identifier[lang_name] ("AS" string_literal[code])?)
    {
      if (@lang.has_value()) {
        WithStartLocation(*$lang_name, *@lang);
      }
      ASTScript* body = nullptr;
      if ($begin_end.has_value()) {
        auto* stmt_list = MakeNode<ASTStatementList>(*@begin_end, $begin_end);
        body = MakeNode<ASTScript>(*@begin_end, stmt_list);
      }
      auto* create =
          MakeNode<ASTCreateProcedureStatement>(
              @$, $name, $params, $options, body, $conn, $lang_name, $code);
      create->set_is_or_replace($opt_or_replace);
      create->set_scope($opt_create_scope);
      create->set_is_if_not_exists($opt_if_not_exists);
      if (@invoker.has_value()) {
        create->set_external_security(ASTCreateStatement::SQL_SECURITY_INVOKER);
      } else if (@definer.has_value()) {
        create->set_external_security(ASTCreateStatement::SQL_SECURITY_DEFINER);
      } else {
        create->set_external_security(ASTCreateStatement::SQL_SECURITY_UNSPECIFIED);
      }
      $$ = create;
    }
;

procedure_parameters {ASTNode*}:
    "(" (procedure_parameter separator ",")*[params] ")"
    {
      $$ = MakeNode<ASTFunctionParameters>(@$, $params);
    }
;

procedure_parameter {ASTFunctionParameter*}:
    procedure_parameter_mode-opt[mode] identifier (type_or_tvf_schema[type] | ")"[paren] | ","[comma])
    {
      if (!$type.has_value()) {
        // There may be 3 cases causing this error:
        // 1. OUT int32 where mode is empty and intended identifier name is
        //    "OUT"
        // 2. OUT int32 where mode is OUT and identifier is missing
        // 3. OUT param_a where type is missing
        return MakeSyntaxError(@paren.has_value() ? *@paren : *@comma,
                               "Syntax error: Unexpected end of parameter."
                               " Parameters should be in the format "
                               "[<parameter mode>] <parameter name> <type>. "
                               "If IN/OUT/INOUT is intended to be the name of "
                               "a parameter, it must be escaped with backticks"
                              );
      }
      $$ = MakeNode<ASTFunctionParameter>(@$, $identifier, $type);
      $$->set_procedure_parameter_mode($mode.value_or(
          ASTFunctionParameter::ProcedureParameterMode::NOT_SET));
    }
;

procedure_parameter_mode {ASTFunctionParameter::ProcedureParameterMode}:
    "IN"
    {
      $$ = ASTFunctionParameter::ProcedureParameterMode::IN;
    }
  | "OUT"
    {
      $$ = ASTFunctionParameter::ProcedureParameterMode::OUT;
    }
  | "INOUT"
    {
      $$ = ASTFunctionParameter::ProcedureParameterMode::INOUT;
    }
;

returns {ASTNode*}:
    "RETURNS" type_or_tvf_schema[type]
    {
      if ($type->node_kind() == AST_TEMPLATED_PARAMETER_TYPE) {
        // TODO: Note that the official design supports this
        // feature. A reasonable use-case is named templated types here: e.g.
        // CREATE FUNCTION f(arg ANY TYPE T) RETURNS T AS ...
        return MakeSyntaxError(@type, "Syntax error: Templated types are not "
                                      "allowed in the RETURNS clause");
      }
      $$ = $type;
    }
;

determinism_level {ASTCreateFunctionStmtBase::DeterminismLevel}:
    "DETERMINISTIC"
    {
      $$ = ASTCreateFunctionStmtBase::DETERMINISTIC;
    }
  | "NOT" "DETERMINISTIC"
    {
      $$ = ASTCreateFunctionStmtBase::NOT_DETERMINISTIC;
    }
  | "IMMUTABLE"
    {
      $$ = ASTCreateFunctionStmtBase::IMMUTABLE;
    }
  | "STABLE"
    {
      $$ = ASTCreateFunctionStmtBase::STABLE;
    }
  | "VOLATILE"
    {
      $$ = ASTCreateFunctionStmtBase::VOLATILE;
    }
;

language {ASTIdentifier*}:
    "LANGUAGE" identifier
    {
      $$ = $identifier;
    }
;

remote_with_connection_clause {std::pair<bool, ASTWithConnectionClause*>}:
    "REMOTE"[remote] with_connection_clause?
    {
      if (!language_options.LanguageFeatureEnabled(FEATURE_REMOTE_FUNCTION)) {
        return MakeSyntaxError(@remote, "Keyword REMOTE is not supported");
      }
      $$ = std::make_pair(true, $with_connection_clause.value_or(nullptr));
    }
  | with_connection_clause
    {
      if (!language_options.LanguageFeatureEnabled(FEATURE_REMOTE_FUNCTION) &&
          !language_options.LanguageFeatureEnabled(
                FEATURE_CREATE_FUNCTION_LANGUAGE_WITH_CONNECTION)) {
        return MakeSyntaxError(@$, "WITH CONNECTION clause is not supported");
      }
      $$ = std::make_pair(false, $with_connection_clause);
    }
;

model_remote_with_connection_clause {std::pair<bool, ASTWithConnectionClause*>}:
    "REMOTE"[remote] with_connection_clause?
    {
      $$ = std::make_pair(true, $with_connection_clause.value_or(nullptr));
    }
  | with_connection_clause
    {
      $$ = std::make_pair(false, $with_connection_clause);
    }
;

language_or_remote_with_connection {std::tuple<ASTIdentifier*, bool, ASTWithConnectionClause*>}:
    "LANGUAGE" identifier remote_with_connection_clause[connection]?
    {
      auto [is_remote, connection] =
          $connection.value_or(std::make_pair(false, nullptr));
      $$ = std::make_tuple($identifier, is_remote, connection);
    }
  | remote_with_connection_clause language?
    {
      auto [is_remote, connection] = $remote_with_connection_clause;
      ASTIdentifier* language = $language.value_or(nullptr);
      $$ = std::make_tuple(language, is_remote, connection);
    }
;

sql_security {ASTCreateStatement::SqlSecurity}:
    "SQL" "SECURITY" ("INVOKER"[invoker] | "DEFINER")
    {
      $$ = @invoker.has_value()
         ? ASTCreateStatement::SQL_SECURITY_INVOKER
         : ASTCreateStatement::SQL_SECURITY_DEFINER;
    }
;

as_sql_function_body_or_string {ASTNode*}:
    "AS" sql_function_body
    {
      // Queries may be defined in sql function bodies with lock modes. To
      // avoid unknowingly acquiring locks when executing these queries, we
      // return a syntax error.
      if (HasLockMode($2)) {
        return MakeSyntaxError(@2,
            "Syntax error: Unexpected lock mode in function body query");
      }
      $$ = $2;
    }
  | "AS" string_literal
    {
      $$ = $2;
    }
;

opt_as_sql_function_body_or_string {ASTNode*}:
    as_sql_function_body_or_string?
    {
      $$ = $as_sql_function_body_or_string.value_or(nullptr);
    }
;


path_expression_or_string {ASTNode*}:
    path_expression
  | string_literal
;

path_expression_or_default {ASTExpression*}:
    path_expression
  | "DEFAULT"
    {
      $$ = MakeNode<ASTDefaultLiteral>(@$);
    }
;

sql_function_body {ASTNode*}:
    "(" expression ")"
    {
      $$ = MakeNode<ASTSqlFunctionBody>(@$, $2);
    }
  | "(" "SELECT"
    {
      return MakeSyntaxError(
        @2,
        "The body of each CREATE FUNCTION statement is an expression, not a "
        "query; to use a query as an expression, the query must be wrapped "
        "with additional parentheses to make it a scalar subquery expression");
    }
;

// Parens are required for statements where this clause can be one of many
// actions, so that it's unambiguous where the restrictee list ends and the next
// action begins.
restrict_to_clause {ASTNode*}:
    "RESTRICT" "TO" possibly_empty_grantee_list
    {
      ASTRestrictToClause* node =
          MakeNode<ASTRestrictToClause>(@$, $3);
      $$ = node;
    }
;

grant_to_clause {ASTNode*}:
    "GRANT" "TO" "(" grantee_list ")"
    {
      ASTGrantToClause* grant_to = MakeNode<ASTGrantToClause>(@$, $4);
      grant_to->set_has_grant_keyword_and_parens(true);
      $$ = grant_to;
    }
;

create_row_access_policy_grant_to_clause {ASTNode*}:
    grant_to_clause
  | "TO" grantee_list
    {
      ASTGrantToClause* grant_to = MakeNode<ASTGrantToClause>(@$, $2);
      grant_to->set_has_grant_keyword_and_parens(false);
      $$ = grant_to;
    }
;

filter_using_clause {ASTNode*}:
    "FILTER"[filter]? "USING" "(" expression ")"
    {
      ASTFilterUsingClause* filter_using =
          MakeNode<ASTFilterUsingClause>(@$, $expression);
      filter_using->set_has_filter_keyword(@filter.has_value());
      $$ = filter_using;
    }
;

create_privilege_restriction_statement {ASTNode*}:
    "CREATE" opt_or_replace "PRIVILEGE" "RESTRICTION" opt_if_not_exists
    "ON" privilege_list "ON" identifier path_expression
    restrict_to_clause?
    {
      ASTCreatePrivilegeRestrictionStatement* node =
          MakeNode<ASTCreatePrivilegeRestrictionStatement>(@$,
                    $privilege_list, $identifier, $path_expression,
                    $restrict_to_clause);
      node->set_is_or_replace($opt_or_replace);
      node->set_is_if_not_exists($opt_if_not_exists);
      $$ = node;
    }
;

create_row_access_policy_statement {ASTNode*}:
    "CREATE" opt_or_replace[or_replace] "ROW" opt_access[access]
        "POLICY" opt_if_not_exists[if_not_exists] opt_identifier[id] "ON"
        path_expression
        create_row_access_policy_grant_to_clause?
        filter_using_clause
    {
      ASTPathExpression* opt_path_expression =
          $id == nullptr ? nullptr : MakeNode<ASTPathExpression>(@id, $id);
      ASTCreateRowAccessPolicyStatement* create =
          MakeNode<ASTCreateRowAccessPolicyStatement>(@$,
                    $path_expression, $create_row_access_policy_grant_to_clause,
                    $filter_using_clause, opt_path_expression);
      create->set_is_or_replace($or_replace);
      create->set_is_if_not_exists($if_not_exists);
      create->set_has_access_keyword($access);
      $$ = create;
    }
;

create_data_policy_statement {ASTNode*}:
    "CREATE" opt_or_replace[or_replace] "DATA_POLICY" opt_if_not_exists[if_not_exists]
    path_expression[name]
    opt_options_list[options]
    opt_generic_entity_body
    with_condition_clause?
    {
      if (language_options.LanguageFeatureEnabled(FEATURE_TAG_DATA_POLICY_DDL)) {
        if ($opt_generic_entity_body != nullptr) {
          return MakeSyntaxError(@opt_generic_entity_body, "AS clause is not supported for CREATE DATA_POLICY");
        }
        auto* node = MakeNode<ASTCreateDataPolicyStatement>(@$, $name, $options, $with_condition_clause);
        node->set_is_or_replace($or_replace);
        node->set_is_if_not_exists($if_not_exists);
        $$ = node;
      } else {
        if ($with_condition_clause.value_or(nullptr) != nullptr) {
          return MakeSyntaxError(@with_condition_clause, "WITH CONDITION is not supported.");
        }
        auto* entity_type = node_factory.MakeIdentifier(@3, "DATA_POLICY");
        auto* node = MakeNode<ASTCreateEntityStatement>(@$,
              entity_type,
              $name,
              $options,
              $opt_generic_entity_body
            );
        node->set_is_or_replace($or_replace);
        node->set_is_if_not_exists($if_not_exists);
        $$ = node;
      }
    }
;

with_condition_clause {ASTExpression*}:
    "WITH" "CONDITION" "(" expression ")"
    {
      $$ = $expression;
    }
;

alter_data_policy_statement {ASTNode*}:
    "ALTER" "DATA_POLICY" opt_if_exists[if_exists] path_expression[name]
    data_policy_alter_action_list[actions]
    {
      if (language_options.LanguageFeatureEnabled(FEATURE_TAG_DATA_POLICY_DDL)) {
        auto* node = MakeNode<ASTAlterDataPolicyStatement>(@$, $name, $actions);
        node->set_is_if_exists($if_exists);
        $$ = node;
      } else {
        auto* entity_type = node_factory.MakeIdentifier(@2, "DATA_POLICY");
        auto* node = MakeNode<ASTAlterEntityStatement>(@$, entity_type, $name, $actions);
        node->set_is_if_exists($if_exists);
        $$ = node;
      }
    }
;

data_policy_alter_action_list {ASTNode*}:
    data_policy_alter_action
    {
      $$ = MakeNode<ASTAlterActionList>(@$, $1);
    }
  | data_policy_alter_action_list "," data_policy_alter_action
    {
      $$ = ExtendNodeRight($1, $3);
    }
;

data_policy_alter_action {ASTNode*}:
    "SET" "CONDITION" "(" expression ")"
    {
      $$ = MakeNode<ASTSetConditionAction>(@$, $expression);
    }
  | "SET" "OPTIONS" options_list
    {
      $$ = MakeNode<ASTSetOptionsAction>(@$, $options_list);
    }
;

with_partition_columns_clause {ASTNode*}:
    "WITH" "PARTITION" "COLUMNS" opt_table_element_list
    {
      ASTWithPartitionColumnsClause* with_partition_columns =
          MakeNode<ASTWithPartitionColumnsClause>(@$, $4);
      $$ = with_partition_columns;
    }
;

with_connection_clause {ASTWithConnectionClause*}:
    "WITH" connection_clause
    {
      $$ = MakeNode<ASTWithConnectionClause>(@$, $2);
    }
;

create_external_table_statement {ASTNode*}:
    "CREATE" opt_or_replace[or_replace] opt_create_scope[create_scope]
    "EXTERNAL" "TABLE" .TableFunction opt_if_not_exists[if_not_exists]
    maybe_dashed_path_expression table_element_list? like_path_expression?
    default_collate_clause? with_partition_columns_clause?
    with_connection_clause? options
    {
      auto* create = MakeNode<ASTCreateExternalTableStatement>(@$,
          $maybe_dashed_path_expression, $table_element_list,
          $like_path_expression, $default_collate_clause,
          $with_partition_columns_clause, $with_connection_clause, $options);
      create->set_is_or_replace($or_replace);
      create->set_scope($create_scope);
      create->set_is_if_not_exists($if_not_exists);
      $$ = create;
    }
;

create_external_table_function_statement {ASTNode*}:
    "CREATE" opt_or_replace opt_create_scope "EXTERNAL"[ex] "TABLE"
    .TableFunction KW_FUNCTION_IN_TABLE_FUNCTION
    {
      return MakeSyntaxError(
          @ex, "Syntax error: CREATE EXTERNAL TABLE FUNCTION is not supported");
    }
;

// "PARTITION BY" and "INTERLEAVE IN" clauses conflict because "PARTITION BY"
// uses Token::COMMA is used to separate the partition columns, while
// "INTERLEAVE IN" starts with Token::COMMA. One token of LA is insufficent to
// decide if ", INTERLEAVE" starts another partition by expression or starts the
// INTERLEAVE IN clause. To work around the conflict,
// create_index_statement_suffix does not allow partition by and interleave in
// to be present at the same time.
create_index_statement_suffix {CreateIndexStatementSuffix}:
    partition_by_clause_prefix_no_hint with_connection_clause[conn]? options?
    {
      $$ = CreateIndexStatementSuffix{
            .partition_by = $partition_by_clause_prefix_no_hint,
            .with_connection_clause = $conn.value_or(nullptr),
            .options_list = $options.value_or(nullptr)};
    }
  | with_connection_clause[conn] options?
    {
      $$ = CreateIndexStatementSuffix{
            .with_connection_clause = $with_connection_clause,
            .options_list = $options.value_or(nullptr)};
    }
  | options? spanner_index_interleave_clause[interleave]
    {
      $$ = CreateIndexStatementSuffix{
            .options_list = $options.value_or(nullptr),
            .interleave_in = $interleave};
    }
  | options
    {
      $$ = CreateIndexStatementSuffix{.options_list = $options};
    }
;

create_index_statement {ASTCreateIndexStatement*}:
    "CREATE" opt_or_replace "UNIQUE"[unique]? "NULL_FILTERED"[null_filtered]?
    index_type? "INDEX" if_not_exists? path_expression[name]
    "ON" path_expression[target] as_alias-opt[table_alias]
    index_unnest_expression_list-opt[unnest]
    index_order_by_and_options[order_by] index_storing_list[storing]?
    create_index_statement_suffix[suffix]?
    {
      if (@null_filtered.has_value() &&
          !language_options.LanguageFeatureEnabled(FEATURE_SPANNER_LEGACY_DDL)) {
        return MakeSyntaxError(
            @null_filtered, "null_filtered is not a supported object type");
      }
      ASTPartitionBy* partition_by = nullptr;
      ASTWithConnectionClause* with_connection_clause = nullptr;
      ASTOptionsList* options_list = nullptr;
      ASTSpannerInterleaveClause* interleave_in = nullptr;
      if ($suffix.has_value()) {
        partition_by = $suffix->partition_by;
        with_connection_clause = $suffix->with_connection_clause;
        options_list = $suffix->options_list;
        interleave_in = $suffix->interleave_in;
      }
      $$ = MakeNode<ASTCreateIndexStatement>(@$,
            $name, $target, $table_alias, $unnest, $order_by, $storing,
            partition_by, with_connection_clause, options_list, interleave_in);
      $$->set_is_or_replace($opt_or_replace);
      $$->set_is_unique(@unique.has_value());
      $$->set_is_if_not_exists(@if_not_exists.has_value());
      $$->set_spanner_is_null_filtered(@null_filtered.has_value());
      switch ($index_type.value_or(IndexType::INDEX_DEFAULT)) {
        case IndexType::INDEX_DEFAULT:
          break;
        case IndexType::INDEX_SEARCH:
          $$->set_is_search(true);
          break;
        case IndexType::INDEX_VECTOR:
          $$->set_is_vector(true);
          break;
        case IndexType::INDEX_AI:
          $$->set_is_ai(true);
          break;
      }
    }
;

braced_graph_subquery {ASTQuery*}:
    "{" graph_operation_block[ops] "}"
    {
      $$ = MakeGraphSubquery($ops, /*graph=*/nullptr, node_factory, @$);
    }
  | "{" "GRAPH" path_expression[graph] graph_operation_block[ops] "}"
    {
      $$ = MakeGraphSubquery($ops, $graph, node_factory, @$);
    }
;

exists_graph_pattern_subquery {ASTExpressionSubquery*}:
    "EXISTS" hint? "{" graph_pattern "}"
    {
      $$ = MakeGqlExistsGraphPatternSubquery(
            $graph_pattern, /*graph=*/nullptr, $hint.value_or(nullptr), node_factory, @$);
    }
  | "EXISTS" hint? "{" "GRAPH" path_expression[graph] graph_pattern "}"
    {
      $$ = MakeGqlExistsGraphPatternSubquery(
            $graph_pattern, $graph, $hint.value_or(nullptr), node_factory, @$);
    }
;

exists_linear_ops_subquery {ASTExpressionSubquery*}:
    "EXISTS" hint? "{" graph_linear_operator_list[ops] "}"
    {
      $$ = MakeGqlExistsLinearOpsSubquery(
          $ops, /*graph=*/nullptr, $hint.value_or(nullptr), node_factory, @$);
    }
  | "EXISTS" hint? "{" "GRAPH" path_expression[graph] graph_linear_operator_list[ops] "}"
    {
      $$ = MakeGqlExistsLinearOpsSubquery(
          $ops, $graph, $hint.value_or(nullptr), node_factory, @$);
    }
;

exists_graph_subquery {ASTExpressionSubquery*}:
    exists_graph_pattern_subquery
  | exists_linear_ops_subquery
  | "EXISTS" hint? braced_graph_subquery[graph_query]
    {
      $$ = MakeNode<ASTExpressionSubquery>(@$, $hint, $graph_query);
      $$->set_modifier(ASTExpressionSubquery::EXISTS);
    }
;

create_property_graph_statement {ASTNode*}:
    "CREATE" opt_or_replace opt_create_scope "PROPERTY" "GRAPH" .PropertyGraphType
    opt_if_not_exists path_expression
    node_table_clause? edge_table_clause? with_connection_clause? opt_options_list
    {
      ASTCreateStatement* create =
            MakeNode<ASTCreatePropertyGraphStatement>(@$,
              $path_expression,
              $node_table_clause,
              $edge_table_clause,
              $with_connection_clause,
              $opt_options_list
            );
        create->set_is_or_replace($opt_or_replace);
        create->set_is_if_not_exists($opt_if_not_exists);
        create->set_scope($opt_create_scope);
        $$ = create;
    }
;

create_property_graph_type_statement {ASTNode*}:
    "CREATE" opt_or_replace opt_create_scope "PROPERTY" "GRAPH" .PropertyGraphType
    KW_TYPE_IN_PROPERTY_GRAPH_TYPE
    opt_if_not_exists path_expression
    "NODE" "TYPES" element_type_list edge_type_clause? opt_options_list
    {
      ASTCreateStatement* create =
            MakeNode<ASTCreatePropertyGraphTypeStatement>(@$,
              $path_expression,
              $element_type_list,
              $edge_type_clause,
              $opt_options_list
            );
        create->set_is_or_replace($opt_or_replace);
        create->set_is_if_not_exists($opt_if_not_exists);
        create->set_scope($opt_create_scope);
        $$ = create;
    }
;

element_type_list {ASTNode*}:
    "(" ")"
    {
      $$ = MakeNode<ASTGraphElementTypeList>(@$);
    }
  | "(" (element_type_definition separator ",")+[defs] ","? ")"
    {
      $$ = MakeNode<ASTGraphElementTypeList>(@$, $defs);
    }
;

edge_type_clause {ASTNode*}:
    "EDGE" "TYPES" element_type_list
    {
      $$ = WithEndLocation($element_type_list, @$);
    }
;

element_type_definition {ASTNode*}:
    identifier
    node_type_source_clause? node_type_dest_clause?
    opt_options_list[default_label_options]
    element_type_properties_clause?
    {
      $$ = MakeNode<ASTGraphElementType>(@$,
          $identifier,
          $node_type_source_clause,
          $node_type_dest_clause,
          $element_type_properties_clause,
          $default_label_options
        );
    }
;

node_type_source_clause {ASTNode*}:
    "FROM" identifier
    {
      auto* node_ref = MakeNode<ASTGraphNodeTypeReference>(@$, $identifier);
      node_ref->set_node_reference_type(ASTGraphNodeTypeReference::SOURCE);
      $$ = node_ref;
    }
;

node_type_dest_clause {ASTNode*}:
    "TO" identifier
    {
      auto* node_ref = MakeNode<ASTGraphNodeTypeReference>(@$, $identifier);
      node_ref->set_node_reference_type(ASTGraphNodeTypeReference::DESTINATION);
      $$ = node_ref;
    }
;

element_type_properties_clause {ASTNode*}:
    "PROPERTIES" "(" (graph_property_declaration separator ",")+[decls] ")"
    {
      $$ = MakeNode<ASTGraphPropertyDeclarationList>(@$, $decls);
    }
;

graph_property_declaration {ASTNode*}:
    identifier type opt_options_list
    {
      $$ = MakeNode<ASTGraphPropertyDeclaration>(@$,
          $identifier, $type, $opt_options_list);
    }
;

element_table_list {ASTNode*}:
    "(" (element_table_definition separator ",")+[defs] ","? ")"
    {
      $$ = MakeNode<ASTGraphElementTableList>(@$, $defs);
    }
;

inlined_edge_direction {ASTGraphEdgePatternEnums::EdgeOrientation}:
    "FORWARDS"  { $$ = ASTGraphEdgePatternEnums::RIGHT; }
  | "BACKWARDS" { $$ = ASTGraphEdgePatternEnums::LEFT; }
;

inlined_edge_definition {ASTGraphInlinedEdgeDefinition*}:
    "EDGE" as_alias_with_required_as[name]
    "JOIN" "KEY" column_list[join_keys] "REFERENCES" identifier[ref_table]
    opt_column_list[dest_element_table_columns]
    inlined_edge_direction[direction]?
    opt_options_list[default_label_options]
    opt_inlined_edge_label_and_properties_clause[label_and_properties]
    dynamic_label_and_properties?
    {
       auto direction = $direction.has_value() ? $direction.value() : ASTGraphEdgePatternEnums::RIGHT;
       $$ = MakeNode<ASTGraphInlinedEdgeDefinition>(@$,
           $name,
           $join_keys,
           $ref_table,
           $dest_element_table_columns,
           $label_and_properties,
           $dynamic_label_and_properties.has_value() ?
               $dynamic_label_and_properties->dynamic_label : nullptr,
           $dynamic_label_and_properties.has_value() ?
               $dynamic_label_and_properties->dynamic_properties : nullptr,
           $default_label_options
       );
       $$->set_direction(static_cast<ASTGraphEdgePattern::EdgeOrientation>(direction));
    }
;

inlined_edge_definitions {std::vector<ASTGraphInlinedEdgeDefinition*>}:
    (inlined_edge_definition)+[defs]
    {
      $$ = $defs;
    }
;

element_table_definition {ASTNode*}:
    path_expression as_alias_with_required_as?
    key_clause?
    source_node_table_clause? dest_node_table_clause?
    opt_options_list[default_label_options]
    opt_label_and_properties_clause
    dynamic_label_and_properties?
    inlined_edge_definitions?
    {
      $$ = MakeNode<ASTGraphElementTable>(@$,
          $path_expression,
          $as_alias_with_required_as,
          $key_clause,
          $source_node_table_clause,
          $dest_node_table_clause,
          $opt_label_and_properties_clause,
          $dynamic_label_and_properties.has_value() ?
              $dynamic_label_and_properties->dynamic_label : nullptr,
          $dynamic_label_and_properties.has_value() ?
              $dynamic_label_and_properties->dynamic_properties : nullptr,
          $default_label_options,
          $inlined_edge_definitions
        );
    }
;

key_clause {ASTNode*}:
    "KEY" column_list
    {
      $$ = $2;
    }
;

source_node_table_clause {ASTNode*}:
    "SOURCE" "KEY" column_list "REFERENCES" identifier opt_column_list
    {
      auto* node_ref = MakeNode<ASTGraphNodeTableReference>(@$,
        $identifier, $column_list, $opt_column_list);
      node_ref->set_node_reference_type(ASTGraphNodeTableReference::SOURCE);
      $$ = node_ref;
    }
;

dest_node_table_clause {ASTNode*}:
    "DESTINATION" "KEY" column_list "REFERENCES" identifier opt_column_list
    {
      auto* node_ref = MakeNode<ASTGraphNodeTableReference>(@$,
        $identifier, $column_list, $opt_column_list);
      node_ref->set_node_reference_type(ASTGraphNodeTableReference::DESTINATION);
      $$ = node_ref;
    }
;

node_table_clause {ASTNode*}:
    "NODE" "TABLES" element_table_list
    {
      $$ = WithEndLocation($3, @$);
    }
;

edge_table_clause {ASTNode*}:
    "EDGE" "TABLES" element_table_list
    {
      $3->GetAsOrDie<ASTGraphElementTableList>()->set_is_edge_table_list(true);
      $$ = WithEndLocation($3, @$);
    }
;

opt_label_and_properties_clause {ASTNode*}:
    %empty
    {
      // Implicit DEFAULT LABEL PROPERTIES [ARE] ALL COLUMNS
      auto* properties = MakeNode<ASTGraphProperties>(@$);
      properties->set_no_properties(false);
      $$ = MakeGraphElementLabelAndPropertiesListImplicitDefaultLabel(
        node_factory, /*properties=*/properties, @$);
    }
  | non_empty_label_and_properties_clause
;

non_empty_label_and_properties_clause {ASTNode*}:
    properties_clause[properties]
    {
      // Implicit DEFAULT LABEL PROPERTIES ...
      $$ = MakeGraphElementLabelAndPropertiesListImplicitDefaultLabel(
        node_factory, /*properties=*/$properties, @$);
    }
  | label_and_properties+[label_and_properties_list]
    {
      $$ = MakeNode<ASTGraphElementLabelAndPropertiesList>(@$,
            $label_and_properties_list);
    }
;

// Same as `opt_label_and_properties_clause`, except that an omitted clause
// defaults to NO PROPERTIES instead of PROPERTIES ALL COLUMNS. An inlined edge
// shares the host node table, so defaulting to ALL COLUMNS would copy the
// node's columns onto the edge, which is rarely intended. See
// (broken link):graph-inlined-edge.
opt_inlined_edge_label_and_properties_clause {ASTNode*}:
    %empty
    {
      // Implicit DEFAULT LABEL NO PROPERTIES
      auto* properties = MakeNode<ASTGraphProperties>(@$);
      properties->set_no_properties(true);
      $$ = MakeGraphElementLabelAndPropertiesListImplicitDefaultLabel(
        node_factory, /*properties=*/properties, @$);
    }
  | non_empty_label_and_properties_clause
;

label_and_properties {ASTNode*}:
    ("DEFAULT" "LABEL" opt_options_list | "LABEL" identifier) properties_clause?
    {
      if ($properties_clause.has_value()) {
        $$ = MakeNode<ASTGraphElementLabelAndProperties>(@$, $identifier,
            $opt_options_list,
            $properties_clause);
      } else {
        auto* properties = MakeNode<ASTGraphProperties>(
          ParseLocationRange(@$.end(), @$.end()));
        properties->set_no_properties(false);
        $$ = MakeNode<ASTGraphElementLabelAndProperties>(@$, $identifier,
            $opt_options_list,
            properties);
      }
    }
;

properties_clause {ASTGraphProperties*}:
    "NO" "PROPERTIES"
    {
      auto* properties = MakeNode<ASTGraphProperties>(@$);
      properties->set_no_properties(true);
      $$ = properties;
    }
  | properties_all_columns except_column_list?
    {
      auto* properties = MakeNode<ASTGraphProperties>(@$,
          $except_column_list);
      properties->set_no_properties(false);
      $$ = properties;
    }
  | "PROPERTIES" "(" derived_property_list ")"
    {
      auto* properties = MakeNode<ASTGraphProperties>(@$,
          $derived_property_list);
      properties->set_no_properties(false);
      $$ = properties;
    }
;

properties_all_columns:
    "PROPERTIES" "ALL" "COLUMNS"
  | "PROPERTIES" "ARE" "ALL" "COLUMNS"
;

except_column_list {ASTNode*}:
    "EXCEPT" column_list
    {
      $$ = $column_list;
    }
;

derived_property_list {ASTNode*}:
    (derived_property separator ",")+[derived_properties]
    {
      $$ = MakeNode<ASTGraphDerivedPropertyList>(@$, $derived_properties);
    }
;

derived_property {ASTNode*}:
    expression as_alias_with_required_as[alias]? opt_options_list[options]
    {
      $$ = MakeNode<ASTGraphDerivedProperty>(@$, $expression, $alias, $options);
    }
;

dynamic_label_or_properties {GraphDynamicLabelProperties}:
    "DYNAMIC" "LABEL" "(" dynamic_label ")"
    {
      $$ = {.dynamic_label = $dynamic_label};
    }
  | "DYNAMIC" "PROPERTIES" "(" dynamic_property ")"
    {
      $$ = {.dynamic_properties = $dynamic_property};
    }
;

static_label_or_properties_keywords:
    "NO" "PROPERTIES"
  | "DEFAULT" "LABEL"
  | "LABEL"
  | "PROPERTIES"
;

dynamic_label_and_properties {GraphDynamicLabelProperties}:
    dynamic_label_or_properties
  | dynamic_label_and_properties dynamic_label_or_properties
    {
      auto merged = MergeDynamicLabelProperties(
          $dynamic_label_and_properties, $dynamic_label_or_properties);
      if (merged.ok()) {
        $$ = *merged;
      } else {
        return MakeSyntaxError(@dynamic_label_or_properties,
          merged.status().message());
      }
    }
  | dynamic_label_or_properties static_label_or_properties_keywords
    {
      return MakeSyntaxError(
          @static_label_or_properties_keywords,
          "Syntax error: LABEL or PROPERTIES clause should be moved before "
          "DYNAMIC LABEL/PROPERTIES clause");
    }
;

dynamic_label {ASTNode*}:
    expression
    {
      $$ = MakeNode<ASTGraphDynamicLabel>(@$, $expression);
    }
;

dynamic_property {ASTNode*}:
    expression
    {
      $$ = MakeNode<ASTGraphDynamicProperties>(@$, $expression);
    }
;

create_schema_statement {ASTNode*}:
    "CREATE" opt_or_replace "SCHEMA" opt_if_not_exists path_expression
    opt_default_collate_clause opt_options_list
    {
      auto* create = MakeNode<ASTCreateSchemaStatement>(@$, $5, $6, $7);
      create->set_is_or_replace($2);
      create->set_is_if_not_exists($4);
      $$ = create;
    }
;

create_locality_group_statement {ASTNode*}:
    "CREATE" "LOCALITY" "GROUP" path_expression opt_options_list
    {
      if (!language_options.LanguageFeatureEnabled(
          FEATURE_CREATE_LOCALITY_GROUP)) {
        return MakeSyntaxError(@2, "CREATE LOCALITY GROUP is not supported");
      }
      $$ = MakeNode<ASTCreateLocalityGroupStatement>(@$, $4, $5);
    }
;

create_external_schema_statement {ASTNode*}:
    "CREATE" opt_or_replace opt_create_scope "EXTERNAL" "SCHEMA" opt_if_not_exists path_expression
    opt_with_connection_clause options
    {
      auto* create = MakeNode<ASTCreateExternalSchemaStatement>(@$, $7, $8, $9);
      create->set_is_or_replace($2);
      create->set_scope($3);
      create->set_is_if_not_exists($6);
      $$ = create;
    }
;

create_connection_statement {ASTNode*}:
    "CREATE" opt_or_replace "CONNECTION" opt_if_not_exists path_expression
    opt_options_list
    {
      auto* create = MakeNode<ASTCreateConnectionStatement>(@$, $path_expression, $opt_options_list);
      create->set_is_or_replace($2);
      create->set_is_if_not_exists($4);
      $$ = create;
    }
;

undrop_statement {ASTNode*}:
    "UNDROP" schema_object_kind opt_if_not_exists path_expression
    for_version_clause? opt_options_list
    {
      if ($schema_object_kind != SchemaObjectKind::kSchema) {
        return MakeSyntaxError(
          @schema_object_kind,
          absl::StrCat(
            "UNDROP ",
            absl::AsciiStrToUpper(
              SchemaObjectKindToName($schema_object_kind)),
            " is not supported"));
      }
      auto* undrop = MakeNode<ASTUndropStatement>(@$, $path_expression, $for_version_clause, $opt_options_list);
      undrop->set_schema_object_kind($schema_object_kind);
      undrop->set_is_if_not_exists($opt_if_not_exists);
      $$ = undrop;
    }
;

create_snapshot_statement {ASTNode*}:
    "CREATE" opt_or_replace "SNAPSHOT" "TABLE" opt_if_not_exists maybe_dashed_path_expression
     "CLONE" clone_data_source opt_options_list
    {
      auto* create = MakeNode<ASTCreateSnapshotTableStatement>(@$, $6, $8, $9);
      create->set_is_if_not_exists($5);
      create->set_is_or_replace($2);
      $$ = create;
    }
  | "CREATE" opt_or_replace "SNAPSHOT" schema_object_kind opt_if_not_exists maybe_dashed_path_expression
      "CLONE" clone_data_source opt_options_list
    {
      if (!SchemaObjectAllowedForSnapshot($schema_object_kind)) {
        return MakeSyntaxError(
          @schema_object_kind,
          absl::StrCat(
            "CREATE SNAPSHOT ",
            absl::AsciiStrToUpper(
              SchemaObjectKindToName($schema_object_kind)),
            " is not supported"));
      }
      auto* create = MakeNode<ASTCreateSnapshotStatement>(@$, $6, $8, $9);
      create->set_schema_object_kind($schema_object_kind);
      create->set_is_if_not_exists($5);
      create->set_is_or_replace($2);
      $$ = create;
    }
;

tvf_declaration_option {ASTNode*}:
    language { $$ = $1; }
  | with_connection_clause { $$ = $1; }
  | options { $$ = $1; }
;

tvf_declaration_options {googlesql::parser::internal::TVFDeclarationOptions}:
    (tvf_declaration_option)+[list]
    {
      auto options_or = googlesql::parser::internal::ProcessTVFDeclarationOptions($list);
      if (!options_or.ok()) {
        return options_or.status();
      }
      $$ = std::move(options_or).value();
    }
;

create_table_function_statement {ASTNode*}:
    // The preferred style is LANGUAGE OPTIONS but OPTIONS LANGUAGE is allowed
    // for backwards compatibility (no deprecation warning YET).
    "CREATE" opt_or_replace opt_create_scope "TABLE" .TableFunction KW_FUNCTION_IN_TABLE_FUNCTION
        opt_if_not_exists path_expression opt_function_parameters returns?
        sql_security? tvf_declaration_options[ulo]? opt_as_query_or_string[body]
    {
      if ($opt_function_parameters == nullptr) {
        // Missing function argument list.
        return MakeSyntaxError(@opt_function_parameters,
                              "Syntax error: Expected (");
      }
      if ($returns.has_value() && !(*$returns)->Is<ASTTVFSchema>()) {
        return MakeSyntaxError(*@returns,
                               "Syntax error: Expected keyword TABLE");
      }
      // Build the create table function statement.
      auto* fn_decl = MakeNode<ASTFunctionDeclaration>(
          MakeLocationRange(@path_expression, @opt_function_parameters),
          $path_expression, $opt_function_parameters);

      ASTIdentifier* language = nullptr;
      ASTWithConnectionClause* connection = nullptr;
      ASTOptionsList* options = nullptr;
      if ($ulo.has_value()) {
        language = $ulo->language;
        connection = $ulo->connection;
        options = $ulo->options;
      }

      auto* create = MakeNode<ASTCreateTableFunctionStatement>(
          @$, fn_decl, $returns, options, language, connection, $body);
      create->set_is_or_replace($opt_or_replace);
      create->set_scope($opt_create_scope);
      create->set_is_if_not_exists($opt_if_not_exists);
      create->set_sql_security(
          $sql_security.value_or(ASTCreateStatement::SQL_SECURITY_UNSPECIFIED));
      $$ = create;
    }
;

create_table_statement_prefix {ASTNode*}:
    "CREATE" opt_or_replace opt_create_scope "TABLE" .TableFunction
    opt_if_not_exists maybe_dashed_path_expression opt_table_element_list
    opt_spanner_table_options opt_like_path_expression opt_clone_table
    opt_copy_table opt_default_collate_clause opt_partition_by_clause_no_hint
    opt_cluster_by_clause_no_hint opt_ttl_clause opt_with_connection_clause
    opt_options_list
    {
      ASTCreateStatement* create =
          MakeNode<ASTCreateTableStatement>(@$,
            $maybe_dashed_path_expression,
            $opt_table_element_list,
            $opt_like_path_expression,
            $opt_spanner_table_options,
            $opt_clone_table,
            $opt_copy_table,
            $opt_default_collate_clause,
            $opt_partition_by_clause_no_hint,
            $opt_cluster_by_clause_no_hint,
            $opt_ttl_clause,
            $opt_with_connection_clause,
            $opt_options_list
          );
      create->set_is_or_replace($opt_or_replace);
      create->set_scope($opt_create_scope);
      create->set_is_if_not_exists($opt_if_not_exists);
      $$ = create;
    }
;

create_table_statement {ASTNode*}:
    create_table_statement_prefix[prefix] opt_as_query
    {
      $$ = ExtendNodeRight($prefix, @$.end(), $opt_as_query);
    }
;

create_live_table_statement {ASTNode*}:
    "CREATE" opt_or_replace opt_create_scope "LIVE" "TABLE" opt_if_not_exists
    maybe_dashed_path_expression opt_table_element_list
    opt_default_collate_clause opt_partition_by_clause_no_hint
    opt_cluster_by_clause_no_hint
    opt_with_connection_clause
    opt_options_list opt_as_query
    {
      if ($opt_create_scope != ASTCreateStatement::DEFAULT_SCOPE) {
        return MakeSyntaxError(@opt_create_scope,
            "Syntax error: TEMP, TEMPORARY, PUBLIC, or PRIVATE modifiers are not supported for LIVE TABLE");
      }
      auto* create = MakeNode<ASTCreateLiveTableStatement>(
          @$, $maybe_dashed_path_expression, $opt_table_element_list,
          $opt_default_collate_clause,
          $opt_partition_by_clause_no_hint, $opt_cluster_by_clause_no_hint,
          $opt_with_connection_clause,
          $opt_options_list, $opt_as_query);
      create->set_is_or_replace($opt_or_replace);
      create->set_scope($opt_create_scope);
      create->set_is_if_not_exists($opt_if_not_exists);
      $$ = create;
    }
;


append_or_overwrite {ASTAuxLoadDataStatement::InsertionMode}:
    "INTO"
    {
      // INTO to mean append, which is consistent with INSERT INTO
      $$ = ASTAuxLoadDataStatement::InsertionMode::APPEND;
    }
  | "OVERWRITE"
    {
      $$ = ASTAuxLoadDataStatement::InsertionMode::OVERWRITE;
    }
;

aux_load_data_from_files_options_list {ASTNode*}:
    "FROM" "FILES" options_list
    {
      $$ = MakeNode<ASTAuxLoadDataFromFilesOptionsList>(@$, $3);
    }
;

load_data_partitions_clause {ASTNode*}:
    "OVERWRITE"[overwrite]? "PARTITIONS" "(" expression ")"
    {
      if (!language_options.LanguageFeatureEnabled(
        FEATURE_LOAD_DATA_PARTITIONS)) {
          return MakeSyntaxError(
            @2,
            "LOAD DATA statement with PARTITIONS is not supported");
      }
      ASTAuxLoadDataPartitionsClause* partitions_clause =
          MakeNode<ASTAuxLoadDataPartitionsClause>(@$, $expression);
      partitions_clause->set_is_overwrite(@overwrite.has_value());
      $$ = partitions_clause;
    }
;

maybe_dashed_path_expression_with_scope {PathExpressionWithScope}:
    "TEMP" "TABLE" maybe_dashed_path_expression
    {
      $$.maybe_dashed_path_expression = $3->GetAsOrDie<ASTExpression>();
      $$.is_temp_table = true;
    }
  | "TEMPORARY" "TABLE" maybe_dashed_path_expression
    {
      $$.maybe_dashed_path_expression = $3->GetAsOrDie<ASTExpression>();
      $$.is_temp_table = true;
    }
  | maybe_dashed_path_expression
    {
      $$.maybe_dashed_path_expression = $1->GetAsOrDie<ASTExpression>();
      $$.is_temp_table = false;
    }
;

aux_load_data_statement {ASTNode*}:
    "LOAD" "DATA" append_or_overwrite
    maybe_dashed_path_expression_with_scope[name]
    {
      OVERRIDE_NEXT_TOKEN_LOOKBACK(LPAREN, LB_LPAREN_NOT_OPEN_SUBQUERY);
    }
    table_element_list-opt[table_elements]
    load_data_partitions_clause[partitions]?
    collate_clause[collate]?
    partition_by_clause_prefix_no_hint[partition_by]?
    cluster_by_clause_no_hint[cluster_by]?
    options? aux_load_data_from_files_options_list[from_files_options]
    with_partition_columns_clause? with_connection_clause?
    {
      ASTAuxLoadDataStatement* statement =
          MakeNode<ASTAuxLoadDataStatement>(@$,
              $name.maybe_dashed_path_expression,
              $table_elements, $partitions, $collate, $partition_by,
              $cluster_by, $options, $from_files_options,
              $with_partition_columns_clause, $with_connection_clause);
      statement->set_insertion_mode($append_or_overwrite);
      if (!language_options.LanguageFeatureEnabled(FEATURE_LOAD_DATA_TEMP_TABLE)
          && $name.is_temp_table) {
          return MakeSyntaxError(
              @name,
              "LOAD DATA statement with TEMP TABLE is not supported");
      }
      statement->set_is_temp_table($name.is_temp_table);
      $$ = statement;
    }
;

generic_entity_type_unchecked {ASTIdentifier*}:
    IDENTIFIER
    {
      // It is by design that we don't want to support backtick quoted
      // entity type. Backtick is kept as part of entity type name, and will
      // be rejected by engine later.
      $$ = node_factory.MakeIdentifier(@1, $1);
    }
  | "PROJECT"
    {
      $$ = node_factory.MakeIdentifier(@1, $1);
    }
;

generic_entity_type {ASTNode*}:
    generic_entity_type_unchecked
    {
      std::string entity_type($1->GetAsStringView());
      if (!language_options.GenericEntityTypeSupported(entity_type)) {
        return MakeSyntaxError(@1, absl::StrCat(
                              entity_type, " is not a supported object type"));
      }
      $$ = $1;
    }
;

// This rule can't use the normal `identifier` production, because that includes
// `keyword_as_identifier`, which includes all non-reserved keywords.
// Including the non-reserved keywords causes many ambiguities with non-generic
// DDL rules - e.g. ADD COLUMN.
//
// Any non-reserved keywords that need to work as generic DDL object types need
// to be included here explicitly.
sub_entity_type_identifier {ASTIdentifier*}:
    IDENTIFIER
    {
      $$ = node_factory.MakeIdentifier(@1, $1);
    }
  | "REPLICA"
    {
      $$ = node_factory.MakeIdentifier(@1, $1);
    }
;

generic_sub_entity_type {ASTNode*}:
    sub_entity_type_identifier
    {
      if (!language_options.GenericSubEntityTypeSupported($1->GetAsString())) {
        return MakeSyntaxError(
          @1, absl::StrCat(ToIdentifierLiteral($1->GetAsString()),
                           " is not a supported nested object type"));
      }
      $$ = $1;
    }
;

generic_entity_body {ASTNode*}:
    json_literal
  | string_literal
;

opt_generic_entity_body {ASTNode*}:
    "AS" generic_entity_body
    {
      $$ = $2;
    }
  | %empty
    {
      $$ = nullptr;
    }
;

create_entity_statement {ASTNode*}:
    "CREATE" opt_or_replace opt_create_scope generic_entity_type opt_if_not_exists
    path_expression opt_options_list opt_generic_entity_body
    {
      auto* node = MakeNode<ASTCreateEntityStatement>(@$,
            $generic_entity_type,
            $path_expression,
            $opt_options_list,
            $opt_generic_entity_body
          );
      node->set_is_or_replace($opt_or_replace);
      node->set_scope($opt_create_scope);
      node->set_is_if_not_exists($opt_if_not_exists);
      $$ = node;
    }
;

create_model_statement {ASTNode*}:
    "CREATE" opt_or_replace opt_create_scope "MODEL" opt_if_not_exists
    path_expression input_output_clause? transform_clause?
    model_remote_with_connection_clause[remote]? options?
    as_query_or_aliased_query_list[queries]?
    {
      auto [is_remote, connection] =
          $remote.has_value() ? *$remote : std::make_pair(false, nullptr);
      auto* node = MakeNode<ASTCreateModelStatement>(
          @$, $path_expression, $input_output_clause, $transform_clause,
          connection, $options, $queries);
      node->set_is_or_replace($opt_or_replace);
      node->set_scope($opt_create_scope);
      node->set_is_if_not_exists($opt_if_not_exists);
      node->set_is_remote(is_remote);
      $$ = node;
    }
;

opt_table_element_list {ASTNode*}:
    table_element_list
  | %empty
    {
      $$ = nullptr;
    }
;

table_element_list {ASTNode*}:
    table_element_list_prefix ")"
    {
      $$ = WithEndLocation($1, @$);
    }
  | "(" ")"
    {
      if (!language_options.LanguageFeatureEnabled(
          FEATURE_SPANNER_LEGACY_DDL)) {
        return MakeSyntaxError(@2, "A table must define at least one column.");
      }
      $$ = MakeNode<ASTTableElementList>(@$);
    }
;

table_element_list_prefix {ASTNode*}:
    "(" table_element
    {
      $$ = MakeNode<ASTTableElementList>(@$, $2);
    }
  | table_element_list_prefix "," table_element
    {
      $$ = ExtendNodeRight($1, $3);
    }
    # This is buggy because it allows an arbitrary number of commas between
    # table elements. Some customers depend on this bug.
  | table_element_list_prefix ","
    {
      $$ = WithEndLocation($1, @$);
    }
;

// The table_element grammar includes 2 shift/reduce conflicts in its
// table_constraint_definition rule. See the file header comment for
// AMBIGUOUS CASE ... CREATE TABLE CONSTRAINTS.
//
// The table elements for the CREATE TABLE statement include a mix of column
// definitions and constraint definitions (such as foreign key, primary key,
// or check constraint). Most keywords in these definitions are
// context-sensitive and may also be used as identifiers.
//
// A number of strategies are used to disambiguate the grammar. Definitions
// starting with constraint keywords and tokens, such as "PRIMARY" "KEY",
// "FOREIGN" "KEY", and "CHECK" "(", are parsed as table constraints.
// Also, definitions with a reserved keyword, such as ARRAY, for the second
// token are unambiguously parsed as column definitions.
//
// Definitions prefixed with two identifiers are potentially either a column
// definition or a named constraint (e.g. CONSTRAINT name FOREIGN KEY). We
// cannot use 'CONSTRAINT identifier' in the grammar without an explosion of
// Bison shift/reduce conflicts with 'identifier identifier' in the column
// definition rules. Instead, constraint names are parsed as
// 'identifier identifier', manually checking if the first identifier is
// "CONSTRAINT".
//
// Lastly, the third token of a table element definition is always a reserved
// keyword (reserved_keyword_rule), a non-reserved keyword
// (keyword_as_identifier), or the "." in a path expression. The third token is
// never an IDENTIFIER. This enables the grammar to unambiguously distinguish
// between named foreign key constraints and column definition attributes. The
// only requirement is that the third component of all table element rules,
// direct and indirect, is a keyword or symbol (i.e., a string literal, such as
// "HIDDEN", "FOREIGN" or ".").
//
table_element {ASTNode*}:
    table_column_definition
  | table_constraint_definition
;

table_column_definition {ASTNode*}:
    identifier[name] .lr0 .AfterColumnName
    ( (table_column_schema[schema] | GUIDE_EMPTY_COLUMN_TYPE)
      column_attributes? options?
    )[full_schema]
    {
      ASTNode* schema = nullptr;
      if (@schema.has_value()) {
        schema =
            ExtendNodeRight(*$schema, @$.end(), $column_attributes, $options);
      } else {
        schema = MakeNode<ASTInferredTypeColumnSchema>(
            @full_schema, $column_attributes, $options);
      }
      $$ = MakeNode<ASTColumnDefinition>(@$, $name, schema);
    }
;

table_column_schema {ASTNode*}:
    column_schema_inner opt_collate_clause opt_column_info
    {
      $$ = ExtendNodeRight($1, $2, $3.generated_column_info,
                             $3.default_expression);
    }
  | generated_column_info
    {
      $$ = MakeNode<ASTInferredTypeColumnSchema>(@$, $1);
    }
;

simple_column_schema_inner {ASTNode*}:
    path_expression
    {
      $$ = MakeNode<ASTSimpleColumnSchema>(@$, $1);
    }
    // Unlike other type names, 'INTERVAL' is a reserved keyword.
  | "INTERVAL"
    {
      auto* id = node_factory.MakeIdentifier(@1, $1);
      auto* path_expression = MakeNode<ASTPathExpression>(@$, id);
      $$ = MakeNode<ASTSimpleColumnSchema>(@$, path_expression);
    }
;

array_column_schema_inner {ASTNode*}:
    "ARRAY" "<" .OpenTypeTemplate field_schema template_type_close
    {
      $$ = MakeNode<ASTArrayColumnSchema>(@$, $3);
    }
;

range_column_schema_inner {ASTNode*}:
    "RANGE" "<" .OpenTypeTemplate field_schema template_type_close
    {
      $$ = MakeNode<ASTRangeColumnSchema>(@$, $3);
    }
;

map_column_schema_inner {ASTNode*}:
    "MAP" "<" .OpenTypeTemplate field_schema[key] "," field_schema[value] template_type_close
    {
      $$ = MakeNode<ASTMapColumnSchema>(@$, $key, $value);
    }
;

struct_column_field {ASTNode*}:
    // Unnamed fields cannot have OPTIONS annotation, because OPTIONS is not
    // a reserved keyword. More specifically, both
    //  field_schema
    // and
    //  column_schema_inner "OPTIONS" options_list
    // will result in conflict; even if we increase %expect, the parser favors
    // the last rule and fails when it encounters "(" after "OPTIONS".
    //
    // We could replace this rule with
    //   column_schema_inner
    //   | column_schema_inner not_null_column_attribute opt_options_list
    // without conflict, but it would be inconsistent to allow
    // STRUCT<INT64 NOT NULL OPTIONS()> while disallowing
    // STRUCT<INT64 OPTIONS()>.
    //
    // For a similar reason, the only supported field attribute is NOT NULL,
    // which have reserved keywords.
    column_schema_inner opt_collate_clause opt_field_attributes
    {
      auto* schema = ExtendNodeRight($1, $2, $3);
      $$ = MakeNode<ASTStructColumnField>(@$, schema);
    }
  | identifier field_schema
    {
      $$ = MakeNode<ASTStructColumnField>(@$, $1, $2);
    }
;
struct_column_schema_inner {ASTNode*}:
    "STRUCT" "<" .OpenTypeTemplate (struct_column_field separator ",")*[fields] template_type_close
    {
      $$ = MakeNode<ASTStructColumnSchema>(@$, $fields);
    }
;

raw_column_schema_inner {ASTNode*}:
    simple_column_schema_inner
  | array_column_schema_inner
  | struct_column_schema_inner
  | range_column_schema_inner
  | map_column_schema_inner
;

column_schema_inner {ASTNode*}:
    raw_column_schema_inner[inner] opt_type_parameters[params]
    {
      $$ = ExtendNodeRight($inner, @$.end(), $params);
    }
;

generated_mode {ASTGeneratedColumnInfo::GeneratedMode}:
    "GENERATED" "AS"
    {
      $$ = ASTGeneratedColumnInfo::GeneratedMode::ALWAYS;
    }
  | "GENERATED" "ALWAYS" "AS"
    {
      $$ = ASTGeneratedColumnInfo::GeneratedMode::ALWAYS;
    }
  | "GENERATED" "BY" "DEFAULT" "AS"
    {
      $$ = ASTGeneratedColumnInfo::GeneratedMode::BY_DEFAULT;
    }
  | "AS"
    {
      $$ = ASTGeneratedColumnInfo::GeneratedMode::ALWAYS;
    }
;

stored_mode {ASTGeneratedColumnInfo::StoredMode}:
    "STORED" "VOLATILE"
    {
      $$ = ASTGeneratedColumnInfo::StoredMode::STORED_VOLATILE;
    }
  | "STORED"
    {
      $$ = ASTGeneratedColumnInfo::StoredMode::STORED;
    }
  | %empty
    {
      $$ = ASTGeneratedColumnInfo::StoredMode::NON_STORED;
    }
;

signed_numerical_literal {ASTExpression*}:
    integer_literal
  | numeric_literal
  | bignumeric_literal
  | floating_point_literal
  | "-" integer_literal[literal]
    {
      auto* expression = MakeNode<ASTUnaryExpression>(@$, $literal);
      expression->set_op(ASTUnaryExpression::MINUS);
      $$ = expression;
    }
  | "-" floating_point_literal[literal]
    {
      auto* expression = MakeNode<ASTUnaryExpression>(@$, $literal);
      expression->set_op(ASTUnaryExpression::MINUS);
      $$ = expression;
    }
;

opt_start_with {ASTNode*}:
    "START" "WITH" signed_numerical_literal[literal]
    {
      $$ = MakeNode<ASTIdentityColumnStartWith>(@$, $literal);
    }
  | %empty
    {
      $$ = nullptr;
    }
;

opt_increment_by {ASTNode*}:
    "INCREMENT" "BY" signed_numerical_literal[literal]
    {
      $$ = MakeNode<ASTIdentityColumnIncrementBy>(@$, $literal);
    }
  | %empty
    {
      $$ = nullptr;
    }
;

opt_maxvalue {ASTNode*}:
    "MAXVALUE" signed_numerical_literal[literal]
    {
      $$ = MakeNode<ASTIdentityColumnMaxValue>(@$, $literal);
    }
  | %empty
    {
      $$ = nullptr;
    }
;

opt_minvalue {ASTNode*}:
    "MINVALUE" signed_numerical_literal[literal]
    {
      $$ = MakeNode<ASTIdentityColumnMinValue>(@$, $literal);
    }
  | %empty
    {
      $$ = nullptr;
    }
;

opt_cycle {bool}:
    "CYCLE"
    {
      $$ = true;
    }
  | "NO" "CYCLE"
    {
      $$ = false;
    }
  | %empty
    {
      $$ = false;
    }
;

identity_column_info {ASTNode*}:
    "IDENTITY" "(" opt_start_with[start] opt_increment_by[increment]
  opt_maxvalue[max] opt_minvalue[min] opt_cycle[cycle] ")"
    {
      auto* identity_column =
        MakeNode<ASTIdentityColumnInfo>(@$, $start, $increment, $max, $min);
      identity_column->set_cycling_enabled($cycle);
      $$ = identity_column;
    }
;

generated_column_info {ASTNode*}:
    generated_mode "(" expression ")" stored_mode
    {
      auto* column = MakeNode<ASTGeneratedColumnInfo>(@$, $expression);
      column->set_stored_mode($stored_mode);
      column->set_generated_mode($generated_mode);
      $$ = column;
    }
  | generated_mode identity_column_info
    {
      auto* column = MakeNode<ASTGeneratedColumnInfo>(@$, $2);
      column->set_generated_mode($generated_mode);
      $$ = column;
    }
;

generated_mode_for_alter_column_action {ASTGeneratedColumnInfo::GeneratedMode}:
    "ALWAYS"? "AS"
    {
      $$ = ASTGeneratedColumnInfo::GeneratedMode::ALWAYS;
    }
  | "BY" "DEFAULT" "AS"
    {
      $$ = ASTGeneratedColumnInfo::GeneratedMode::BY_DEFAULT;
    }
;

generated_column_info_for_alter_column_action {ASTNode*}:
    generated_mode_for_alter_column_action[gen_mode] identity_column_info[ic_info]
    {
      auto* column = MakeNode<ASTGeneratedColumnInfo>(@$, $ic_info);
      column->set_generated_mode($gen_mode);
      $$ = column;
    }
;

default_column_info {ASTNode*}:
    "DEFAULT" expression
    {
      if (language_options.LanguageFeatureEnabled(
             FEATURE_COLUMN_DEFAULT_VALUE)) {
        $$ = $2;
      } else {
        return MakeSyntaxError(@2, "Column DEFAULT value is not supported.");
      }
    }
;

opt_column_info {GeneratedOrDefaultColumnInfo}:
    generated_column_info[gen_info] default_column_info[default_info]
    {
      if ($default_info != nullptr) {
        return MakeSyntaxError(@default_info, "Syntax error: \"DEFAULT\" and "
            "\"[GENERATED [ALWAYS | BY DEFAULT]] AS\" clauses must not be both "
            "provided for the column");
      }
      $$.generated_column_info =
          static_cast<ASTGeneratedColumnInfo*>($gen_info);
      $$.default_expression = nullptr;
    }
  | generated_column_info[gen_info]
    {
      $$.generated_column_info = nullptr;
      $$.default_expression = static_cast<ASTExpression*>($gen_info);
    }
  | default_column_info[default_info] generated_column_info[gen_info]
    {
      if ($2 != nullptr) {
        return MakeSyntaxError(@gen_info, "Syntax error: \"DEFAULT\" and "
            "\"[GENERATED [ALWAYS | BY DEFAULT]] AS\" clauses must not be both "
            "provided for the column");
      }
      $$.generated_column_info = nullptr;
      $$.default_expression = static_cast<ASTExpression*>($default_info);
    }
  | default_column_info[default_info]
    {
      $$.generated_column_info = nullptr;
      $$.default_expression = static_cast<ASTExpression*>($default_info);
    }
  | %empty
    {
      $$.generated_column_info = nullptr;
      $$.default_expression = nullptr;
    }
;

field_schema {ASTNode*}:
    column_schema_inner opt_collate_clause opt_field_attributes opt_options_list
    {
      $$ = ExtendNodeRight($1, $2, $3, $4);
    }
;

primary_key_column_attribute {ASTNode*}:
    "PRIMARY" "KEY"
    {
      $$ = MakeNode<ASTPrimaryKeyColumnAttribute>(@$);
    }
;

foreign_key_column_attribute {ASTNode*}:
    opt_constraint_identity foreign_key_reference
    {
      auto* node = MakeNode<ASTForeignKeyColumnAttribute>(@$, $1, $2);
      $$ = WithStartLocation(node, FirstNonEmptyLocation(@1, @2));
    }
;

hidden_column_attribute {ASTNode*}:
    "HIDDEN"
    {
      $$ = MakeNode<ASTHiddenColumnAttribute>(@$);
    }
;

not_null_column_attribute {ASTNode*}:
    "NOT" "NULL"
    {
      $$ = MakeNode<ASTNotNullColumnAttribute>(@$);
    }
;

column_attribute {ASTNode*}:
    primary_key_column_attribute
  | foreign_key_column_attribute
  | hidden_column_attribute
  | not_null_column_attribute
;

// Conceptually, a foreign key column reference is defined by this rule:
//
//   opt_constraint_identity foreign_key_reference opt_constraint_enforcement
//
// However, the trailing opt_constraint_enforcement leads to a potential syntax
// error for a valid column definition:
//
//   a INT64 REFERENCES t (a) NOT NULL
//
// If foreign_key_reference included opt_constraint_enforcement, Bison's
// bottom-up evaluation would want to bind NOT to ENFORCED. For the example
// above, it would fail on NULL with a syntax error.
//
// The workaround is to hoist NOT ENFORCED to the same level in the grammar as
// NOT NULL. This forces Bison to defer shift/reduce decisions for NOT until it
// evaluates the next token, either ENFORCED or NULL.
column_attributes {ASTNode*}:
    column_attribute
    {
      $$ = MakeNode<ASTColumnAttributeList>(@$, $1);
    }
  | column_attributes column_attribute
    {
      $$ = ExtendNodeRight($1, $2);
    }
  | column_attributes constraint_enforcement
    {
      auto* last = $1->mutable_child($1->num_children() - 1);
      if (last->node_kind() != AST_FOREIGN_KEY_COLUMN_ATTRIBUTE
        && last->node_kind() != AST_PRIMARY_KEY_COLUMN_ATTRIBUTE) {
        return MakeSyntaxError(@2,
            "Syntax error: Unexpected constraint enforcement clause");
      }
      // Update the node's location to include constraint_enforcement.
      last = WithEndLocation(last, @$);
      if (last->node_kind() == AST_FOREIGN_KEY_COLUMN_ATTRIBUTE) {
        int index = last->find_child_index(AST_FOREIGN_KEY_REFERENCE);
        if (index == -1) {
          return MakeSyntaxError(@2,
              "Internal Error: Expected foreign key reference");
        }
        ASTForeignKeyReference* reference =
            last->mutable_child(index)
                ->GetAsOrDie<ASTForeignKeyReference>();
        reference->set_enforced($2);
      } else {
        ASTPrimaryKeyColumnAttribute* primary_key =
            last->GetAsOrDie<ASTPrimaryKeyColumnAttribute>();
        primary_key->set_enforced($2);
      }
      $$ = WithEndLocation($1, @$);
    }
;

opt_field_attributes {ASTNode*}:
    not_null_column_attribute
    {
      $$ = MakeNode<ASTColumnAttributeList>(@$, $1);
    }
  | %empty
    {
      $$ = nullptr;
    }
;

column_position {ASTNode*}:
    "PRECEDING" identifier
    {
      auto* pos = MakeNode<ASTColumnPosition>(@$, $2);
      pos->set_type(ASTColumnPosition::PRECEDING);
      $$ = pos;
    }
  | "FOLLOWING" identifier
    {
      auto* pos = MakeNode<ASTColumnPosition>(@$, $2);
      pos->set_type(ASTColumnPosition::FOLLOWING);
      $$ = pos;
    }
;

table_constraint_spec {ASTNode*}:
    "CHECK" "(" expression ")" opt_constraint_enforcement opt_options_list
    {
      auto* node = MakeNode<ASTCheckConstraint>(@$, $3, $6);
      node->set_is_enforced($5);
      $$ = node;
    }
  | "FOREIGN" "KEY" column_list foreign_key_reference
        opt_constraint_enforcement opt_options_list
    {
      ASTForeignKeyReference* foreign_key_ref = $4;
      foreign_key_ref->set_enforced($5);
      $$ = MakeNode<ASTForeignKey>(@$, $3, $4, $6);
    }
;

primary_key_element {ASTNode*}:
    identifier opt_asc_or_desc opt_null_order
    {
      if (!language_options.LanguageFeatureEnabled(
            FEATURE_ORDERED_PRIMARY_KEYS)) {
        if ($opt_asc_or_desc != ASTOrderingExpression::UNSPECIFIED
            || $opt_null_order != nullptr) {
          return MakeSyntaxError(@2,
            "Ordering for primary keys is not supported");
        }
      }
      auto* node = MakeNode<ASTPrimaryKeyElement>(@$,
        $identifier,
        $opt_null_order
      );
      node->set_ordering_spec($opt_asc_or_desc);
      $$ = node;
    }
;

primary_key_element_list_prefix {ASTNode*}:
    "(" primary_key_element
    {
      $$ = MakeNode<ASTPrimaryKeyElementList>(@$, $2);
    }
  | primary_key_element_list_prefix "," primary_key_element
    {
      $$ = ExtendNodeRight($1, $3);
    }
;

primary_key_element_list {ASTNode*}:
    primary_key_element_list_prefix ")"
    {
      $$ = WithEndLocation($1, @$);
    }
  | "(" ")"
    {
      $$ = nullptr;
    }
;

primary_key_spec {ASTNode*}:
    "PRIMARY" "KEY" primary_key_element_list opt_constraint_enforcement
  opt_options_list
    {
      ASTPrimaryKey* node = MakeNode<ASTPrimaryKey>(@$, $3, $5);
      node->set_enforced($4);
      $$ = node;
    }
;

primary_key_or_table_constraint_spec {ASTNode*}:
    primary_key_spec
  | table_constraint_spec
;

// This rule produces 2 shift/reduce conflicts and requires manual parsing of
// named constraints. See table_element for details.
table_constraint_definition {ASTNode*}:
    primary_key_spec
  | table_constraint_spec
  | identifier .lr0 .AfterColumnName identifier[name] table_constraint_spec
    {
      auto* node = $3;
      absl::string_view constraint = $1->GetAsStringView();
      if (!googlesql_base::CaseEqual(constraint, "CONSTRAINT")) {
        if (node->node_kind() == AST_CHECK_CONSTRAINT) {
          return MakeSyntaxError(
            @1,
            "Syntax error: Expected CONSTRAINT for check constraint "
            "definition. Check constraints on columns are not supported. "
            "Define check constraints as table elements instead");
        } else if (node->node_kind() == AST_FOREIGN_KEY) {
          return MakeSyntaxError(@1,
            "Syntax error: Expected CONSTRAINT for foreign key definition");
        } else {
          return MakeSyntaxError(@$,
            "Syntax error: Unkown table constraint type");
        }
      }
      $$ = WithLocation(ExtendNodeRight(node, $name), @$);
    }
;

// Foreign key enforcement is parsed separately in order to avoid ambiguities
// in the grammar. See column_attributes for details.
foreign_key_reference {ASTForeignKeyReference*}:
    "REFERENCES" path_expression column_list opt_foreign_key_match
        opt_foreign_key_actions
    {
      auto* reference = MakeNode<ASTForeignKeyReference>(@$, $2, $3, $5);
      reference->set_match($4);
      $$ = reference;
    }
;

opt_foreign_key_match {ASTForeignKeyReference::Match}:
    "MATCH" foreign_key_match_mode
    {
      $$ = $2;
    }
  | %empty
    {
      $$ = ASTForeignKeyReference::SIMPLE;
    }
;

foreign_key_match_mode {ASTForeignKeyReference::Match}:
    "SIMPLE"
    {
      $$ = ASTForeignKeyReference::SIMPLE;
    }
  | "FULL"
    {
      $$ = ASTForeignKeyReference::FULL;
    }
  | "NOT_SPECIAL" "DISTINCT"
    {
      $$ = ASTForeignKeyReference::NOT_DISTINCT;
    }
;

opt_foreign_key_actions {ASTNode*}:
    foreign_key_on_update opt_foreign_key_on_delete
    {
      auto* actions = MakeNode<ASTForeignKeyActions>(@$);
      actions->set_update_action($1);
      actions->set_delete_action($2);
      $$ = actions;
    }
  | foreign_key_on_delete opt_foreign_key_on_update
    {
      auto* actions = MakeNode<ASTForeignKeyActions>(@$);
      actions->set_delete_action($1);
      actions->set_update_action($2);
      $$ = actions;
    }
  | %empty
    {
      $$ = MakeNode<ASTForeignKeyActions>(@$);
    }
;

opt_foreign_key_on_update {ASTForeignKeyActions::Action}:
    foreign_key_on_update
  | %empty
    {
      $$ = ASTForeignKeyActions::NO_ACTION;
    }
;

opt_foreign_key_on_delete {ASTForeignKeyActions::Action}:
    foreign_key_on_delete
  | %empty
    {
      $$ = ASTForeignKeyActions::NO_ACTION;
    }
;

foreign_key_on_update {ASTForeignKeyActions::Action}:
    "ON" "UPDATE" foreign_key_action
    {
      $$ = $3;
    }
;

foreign_key_on_delete {ASTForeignKeyActions::Action}:
    "ON" "DELETE" foreign_key_action
    {
      $$ = $3;
    }
;

foreign_key_action {ASTForeignKeyActions::Action}:
    "NO" "ACTION"
    {
      $$ = ASTForeignKeyActions::NO_ACTION;
    }
  | "RESTRICT"
    {
      $$ = ASTForeignKeyActions::RESTRICT;
    }
  | "CASCADE"
    {
      $$ = ASTForeignKeyActions::CASCADE;
    }
  | "SET" "NULL"
    {
      $$ = ASTForeignKeyActions::SET_NULL;
    }
;

opt_constraint_identity {ASTNode*}:
    "CONSTRAINT" identifier
    {
      $$ = $2;
    }
  | %empty
    {
      $$ = nullptr;
    }
;

opt_constraint_enforcement {bool}:
    constraint_enforcement
  | %empty
    {
      $$ = true;
    }
;

constraint_enforcement {bool}:
    "ENFORCED"
    {
      $$ = true;
    }
  | "NOT" "ENFORCED"
    {
      $$ = false;
    }
;

tvf_schema_column {ASTNode*}:
    identifier type
    {
      $$ = MakeNode<ASTTVFSchemaColumn>(@$, $1, $2);
    }
  | type
    {
      $$ = MakeNode<ASTTVFSchemaColumn>(@$, $1);
    }
;

tvf_schema {ASTNode*}:
    "TABLE" "<" .OpenTypeTemplate (tvf_schema_column separator ",")+[columns] template_type_close
    {
      $$ = MakeNode<ASTTVFSchema>(@$, $columns);
    }
;

create_view_statement {ASTNode*}:
    "CREATE" opt_or_replace opt_create_scope "RECURSIVE"[recursive]? "VIEW"
    opt_if_not_exists maybe_dashed_path_expression[name] column_with_options_list[cols]?
    sql_security? options? as_query
    {
      auto* create =
          MakeNode<ASTCreateViewStatement>(@$, $name, $cols, $options, $as_query);
      create->set_is_or_replace($opt_or_replace);
      create->set_scope($opt_create_scope);
      create->set_recursive(@recursive.has_value());
      create->set_is_if_not_exists($opt_if_not_exists);
      create->set_sql_security(
          $sql_security.value_or(ASTCreateStatement::SQL_SECURITY_UNSPECIFIED));
      $$ = create;
    }
  | "CREATE" opt_or_replace "MATERIALIZED" "RECURSIVE"[recursive]? "VIEW"
    opt_if_not_exists maybe_dashed_path_expression[name] column_with_options_list[cols]?
    sql_security? opt_partition_by_clause_no_hint[partition]
    cluster_by_clause_no_hint[cluster]? options? as_before_query
    (query | "REPLICA" "OF" maybe_dashed_path_expression[replica_source])
    {
      // Queries in DDL statements shouldn't have lock mode clauses to avoid
      // unintentionally acquiring locks when executing these queries.
      if ($query.has_value() && HasLockMode(*$query)) {
        return MakeSyntaxError(*@query,
            "Syntax error: Unexpected lock mode in query");
      }
      auto* create = MakeNode<ASTCreateMaterializedViewStatement>(
          @$, $name, $cols, $partition, $cluster, $options, $query,
          $replica_source);
      create->set_is_or_replace($opt_or_replace);
      create->set_recursive(@recursive.has_value());
      create->set_scope(ASTCreateStatement::DEFAULT_SCOPE);
      create->set_is_if_not_exists($opt_if_not_exists);
      create->set_sql_security(
          $sql_security.value_or(ASTCreateStatement::SQL_SECURITY_UNSPECIFIED));
      $$ = create;
    }
  | "CREATE" opt_or_replace "APPROX" "RECURSIVE"[recursive]? "VIEW"
    opt_if_not_exists maybe_dashed_path_expression[name] column_with_options_list[cols]?
    sql_security? options? as_query
    {
      auto* create = MakeNode<ASTCreateApproxViewStatement>(
          @$, $name, $cols, $options, $as_query);
      create->set_is_or_replace($opt_or_replace);
      create->set_scope(ASTCreateStatement::DEFAULT_SCOPE);
      create->set_recursive(@recursive.has_value());
      create->set_is_if_not_exists($opt_if_not_exists);
      create->set_sql_security(
          $sql_security.value_or(ASTCreateStatement::SQL_SECURITY_UNSPECIFIED));
      $$ = create;
    }
;

as_before_query {ASTNode*}:
    {
      OVERRIDE_NEXT_TOKEN_LOOKBACK(KW_AS, LB_AS_BEFORE_QUERY);
    }
    "AS"
;

as_query {ASTNode*}:
    as_before_query query_after_as
    {
      // as_query is used in DDL statements and these should not have lock mode
      // clauses to avoid unknowingly acquiring locks when executing these
      // queries.
      if (HasLockMode($2)) {
        return MakeSyntaxError(@2,
            "Syntax error: Unexpected lock mode in query");
      }
      $$ = $2;
    }
;

opt_as_query {ASTNode*}:
    as_query
  | %empty
    {
      $$ = nullptr;
    }
;

as_query_or_string {ASTNode*}:
    as_query
  | as_before_query string_literal
    {
      $$ = $2;
    }
;

opt_as_query_or_string {ASTNode*}:
    as_query_or_string?
    {
      $$ = $as_query_or_string.value_or(nullptr);
    }
;

as_query_or_aliased_query_list {ASTNode*}:
    as_query
  | as_before_query "(" (aliased_query separator ",")+[queries] ")"
    {
      $$ = MakeNode<ASTAliasedQueryList>(@queries, $queries);
    }
;

if_not_exists:
    "IF" "NOT" "EXISTS"
;

opt_if_not_exists {bool}:
    if_not_exists?
    {
      $$ = @if_not_exists.has_value();
    }
;

describe_statement {ASTNode*}:
    describe_keyword describe_info
    {
      $$ = WithStartLocation($2, @$);
    }
;

describe_info {ASTNode*}:
    identifier maybe_slashed_or_dashed_path_expression from_path_expression?
    {
      $$ = MakeNode<ASTDescribeStatement>(@$, $1, $2, $3);
    }
  | maybe_slashed_or_dashed_path_expression from_path_expression?
    {
      $$ = MakeNode<ASTDescribeStatement>(@$, $1, $2);
    }
;

from_path_expression {ASTExpression*}:
    "FROM" maybe_slashed_or_dashed_path_expression
    {
      $$ = $2;
    }
;

explain_statement {ASTNode*}:
    "EXPLAIN" unterminated_sql_statement
    {
      $$ = MakeNode<ASTExplainStatement>(@$, $2);
    }
;

export_data_no_query {ASTNode*}:
    "EXPORT" "DATA" opt_with_connection_clause opt_options_list
    {
      $$ = MakeNode<ASTExportDataStatement>(@$,
               $opt_with_connection_clause, $opt_options_list);
    }
;

export_data_statement {ASTNode*}:
    export_data_no_query as_query
    {
      // WithEndLocation is needed because as_query won't include the
      // closing paren on a parenthesized query.
      $$ = ExtendNodeRight($export_data_no_query, @$.end(), $as_query);
    }
;

export_model_statement {ASTNode*}:
    "EXPORT" "MODEL" path_expression opt_with_connection_clause opt_options_list
    {
      $$ = MakeNode<ASTExportModelStatement>(@$, $3, $4, $5);
    }
;

export_metadata_statement {ASTNode*}:
    "EXPORT" "TABLE" "FUNCTION"[fn]? "METADATA" "FROM"
    maybe_dashed_path_expression[name] opt_with_connection_clause[conn] options?
    {
      if (@fn.has_value()) {
        return MakeSyntaxError(@2,
            "EXPORT TABLE FUNCTION METADATA is not supported");
      }
      auto* export_metadata =
          MakeNode<ASTExportMetadataStatement>(@$, $name, $conn, $options);
      export_metadata->set_schema_object_kind(SchemaObjectKind::kTable);
      $$ = export_metadata;
    }
;

grant_statement {ASTNode*}:
    "GRANT" privileges "ON" identifier path_expression "TO" grantee_list
    {
      $$ = MakeNode<ASTGrantStatement>(@$, $2, $4, $5, $7);
    }
  | "GRANT" privileges "ON" identifier identifier path_expression
        "TO" grantee_list
    {
      $$ = MakeNode<ASTGrantStatement>(@$, $2, $4, $5, $6, $8);
    }
  | "GRANT" privileges "ON" path_expression "TO" grantee_list
    {
      $$ = MakeNode<ASTGrantStatement>(@$, $2, $4, $6);
    }
;

revoke_statement {ASTNode*}:
    "REVOKE" privileges "ON" identifier path_expression "FROM" grantee_list
    {
      $$ = MakeNode<ASTRevokeStatement>(@$, $2, $4, $5, $7);
    }
  | "REVOKE" privileges "ON" identifier identifier path_expression
        "FROM" grantee_list
    {
      $$ = MakeNode<ASTRevokeStatement>(@$, $2, $4, $5, $6, $8);
    }
  | "REVOKE" privileges "ON" path_expression "FROM" grantee_list
    {
      $$ = MakeNode<ASTRevokeStatement>(@$, $2, $4, $6);
    }
;

privileges {ASTNode*}:
    "ALL" "PRIVILEGES"?
    {
      $$ = MakeNode<ASTPrivileges>(@$);
    }
  | privilege_list
;

privilege_list {ASTNode*}:
    (privilege separator ",")+[privilege_list]
    {
      $$ = MakeNode<ASTPrivileges>(@$, $privilege_list);
    }
;

privilege {ASTNode*}:
    privilege_name opt_path_expression_list_with_parens
    {
      $$ = MakeNode<ASTPrivilege>(@$, $1, $2);
    }
;

privilege_name {ASTNode*}:
    identifier
  | "SELECT"
    {
      // The SELECT keyword is allowed to be a privilege name.
      $$ = node_factory.MakeIdentifier(@1, $1);
    }
;

rename_statement {ASTNode*}:
    "RENAME" identifier path_expression "TO" path_expression
    {
      $$ = MakeNode<ASTRenameStatement>(@$, $2, $3, $5);
    }
;

import_statement {ASTNode*}:
    "IMPORT" import_type path_expression_or_string
    opt_as_or_into_alias opt_options_list
    {
      auto* import = MakeNode<ASTImportStatement>(@$, $3, $4, $5);
      switch ($2) {
        case ImportType::kModule:
          import->set_import_kind(ASTImportStatement::MODULE);
          break;
        case ImportType::kProto:
          import->set_import_kind(ASTImportStatement::PROTO);
          break;
      }
      $$ = import;
    }
;

module_statement {ASTNode*}:
    "MODULE" path_expression opt_options_list
    {
      $$ = MakeNode<ASTModuleStatement>(@$, $2, $3);
    }
;


column_ordering_and_options_expr {ASTNode*}:
    expression opt_collate_clause opt_asc_or_desc opt_null_order opt_options_list
    {
      auto* ordering_expr =
          MakeNode<ASTOrderingExpression>(@$,
            $expression,
            $opt_collate_clause,
            $opt_null_order,
            $opt_options_list
          );
      ordering_expr->set_ordering_spec($3);
      $$ = ordering_expr;
    }
;

index_order_by_and_options_prefix {ASTNode*}:
    "(" column_ordering_and_options_expr
    {
      $$ = MakeNode<ASTIndexItemList>(@$, $2);
    }
  | index_order_by_and_options_prefix "," column_ordering_and_options_expr
    {
      $$ = ExtendNodeRight($1, $3);
    }
;

all_column_column_options {ASTNode*}:
    index_order_by_and_options_prefix ")"
;

with_column_options {ASTNode*}:
    "WITH" "COLUMN" "OPTIONS" all_column_column_options
    {
      $$ = $4;
    }
;

index_all_columns {ASTNode*}:
    "(" "ALL" "COLUMNS" with_column_options? ")"
    {
      auto* all_columns = MakeNode<ASTIndexAllColumns>(@$, $4);
      all_columns->set_image("ALL COLUMNS");
      auto* ordering_expr =
          MakeNode<ASTOrderingExpression>(@$,
                    all_columns);
      ordering_expr->set_ordering_spec(
                              ASTOrderingExpression::UNSPECIFIED);
      $$ = MakeNode<ASTIndexItemList>(@$, ordering_expr);
    }
;

index_auto_columns {ASTNode*}:
    "(" "AUTO" "COLUMNS" ")"
    {
      auto* auto_columns = MakeNode<ASTIndexAutoColumns>(@$);
      auto_columns->set_image("AUTO COLUMNS");
      auto* ordering_expr =
          MakeNode<ASTOrderingExpression>(@$,
                    auto_columns);
      ordering_expr->set_ordering_spec(
                              ASTOrderingExpression::UNSPECIFIED);
      $$ = MakeNode<ASTIndexItemList>(@$, ordering_expr);
    }
;

index_order_by_and_options {ASTNode*}:
    index_order_by_and_options_prefix ")"
    {
      $$ = WithEndLocation($1, @$);
    }
  | index_all_columns
    {
      $$ = WithEndLocation($1, @$);
    }
  | index_auto_columns
    {
      $$ = WithEndLocation($1, @$);
    }
;

index_unnest_expression_list {ASTNode*}:
    (unnest_expression_with_opt_alias_and_offset)+[index_unnest_expression_list]
    {
      $$ = MakeNode<ASTIndexUnnestExpressionList>(@$, $index_unnest_expression_list);
    }
;

index_storing_list {ASTNode*}:
    "STORING" "("[lparen] (expression separator ",")+[index_storing_list] ")"
    {
      $$ = WithStartLocation(MakeNode<ASTIndexStoringExpressionList>(@$, $index_storing_list), @lparen);
    }
;

column_list {ASTNode*}:
    "(" (identifier separator ",")+[column_list]
    {
      OVERRIDE_NEXT_TOKEN_LOOKBACK(RPAREN, LB_CLOSE_COLUMN_LIST);
    }
    ")"
    {
      $$ = MakeNode<ASTColumnList>(@$, $column_list);
    }
;

opt_column_list {ASTNode*}:
    column_list?
    {
      $$ = $column_list.value_or(nullptr);
    }
;

column_with_options {ASTNode*}:
    identifier opt_options_list
    {
      $$ = MakeNode<ASTColumnWithOptions>(@$, $1, $2);
    }
;

column_with_options_list {ASTNode*}:
    "(" (column_with_options separator ",")+[column_with_options_list] ")"
    {
      $$ = MakeNode<ASTColumnWithOptionsList>(@$, $column_with_options_list);
    }
;

grantee_list {ASTNode*}:
    (string_literal_or_parameter separator ",")+[grantee_list]
    {
      $$ = MakeNode<ASTGranteeList>(@$, $grantee_list);
    }
;

possibly_empty_grantee_list {ASTNode*}:
    "(" grantee_list ")"
    {
      $$ = WithLocation($grantee_list, @$);
    }
  | "(" ")"
    {
      $$ = MakeNode<ASTGranteeList>(@$);
    }
;

show_statement {ASTNode*}:
    "SHOW" show_target from_path_expression? like_string_literal?
    {
      $$ = MakeNode<ASTShowStatement>(@$, $2, $3, $4);
    }
;

show_target {ASTIdentifier*}:
    "MATERIALIZED" "VIEWS"
    {
      $$ = node_factory.MakeIdentifier(@$, "MATERIALIZED VIEWS");
    }
  | identifier
;

like_string_literal {ASTNode*}:
    "LIKE" string_literal
    {
      $$ = $2;
    }
;

like_path_expression {ASTNode*}:
    "LIKE" maybe_dashed_path_expression
    {
      $$ = $2;
    }
;

opt_like_path_expression {ASTNode*}:
    like_path_expression?
    {
      $$ = $like_path_expression.value_or(nullptr);
    }
;

clone_table {ASTNode*}:
    "CLONE" clone_data_source
    {
      $$ = $2;
    }
;

opt_clone_table {ASTNode*}:
    clone_table?
    {
      $$ = $clone_table.value_or(nullptr);
    }
;

copy_table {ASTNode*}:
    "COPY" copy_data_source
    {
      $$ = $2;
    }
;

opt_copy_table {ASTNode*}:
    copy_table?
    {
      $$ = $copy_table.value_or(nullptr);
    }
;

all_or_distinct {ASTSetOperationAllOrDistinct*}:
    "ALL"
    {
      $$ = MakeNode<ASTSetOperationAllOrDistinct>(@$);
      $$->set_value(ASTSetOperation::ALL);
    }
  | "DISTINCT"
    {
      $$ = MakeNode<ASTSetOperationAllOrDistinct>(@$);
      $$->set_value(ASTSetOperation::DISTINCT);
    }
;

distinct:
    "DISTINCT"
;

opt_distinct {bool}:
    distinct?
    {
      $$ = @distinct.has_value();
    }
;

// Returns the token for a set operation as expected by
// ASTSetOperation::op_type().
query_set_operation_type {ASTSetOperationType*}:
    "UNION"
    {
      $$ = MakeNode<ASTSetOperationType>(@$);
      $$->set_value(ASTSetOperation::UNION);
    }
  | KW_EXCEPT_IN_SET_OP
    {
      $$ = MakeNode<ASTSetOperationType>(@$);
      $$->set_value(ASTSetOperation::EXCEPT);
    }
  | "INTERSECT"
    {
      $$ = MakeNode<ASTSetOperationType>(@$);
      $$->set_value(ASTSetOperation::INTERSECT);
    }
;

query_primary_or_set_operation {ASTNode*}:
    query_primary
  | query_set_operation
;

parenthesized_query {ASTQuery*}:
    "(" query ")"
    {
      // We do not call $query->set_parenthesized(true) because typically the
      // calling rule expects parentheses and will already insert one pair
      // when unparsing.
      // We also don't call WithLocation to set the location on the ASTQuery
      // to include the parentheses.  Most callers of this rule want the
      // ASTQuery location to point at the query inside the parentheses.
      $$ = $query;
    }
;

select_or_from_keyword:
    "SELECT"
  | "FROM"
;

// These rules are for generating errors for unexpected clauses after FROM
// queries.  The returned string constant is the name for the error message.
bad_keyword_after_from_query {const char*}:
    "WHERE"
    {
      $$ = "WHERE";
    }
  | "SELECT"
    {
      $$ = "SELECT";
    }
  | "GROUP"
    {
      $$ = "GROUP BY";
    }
;

// These produce a different error that says parentheses are also allowed.
bad_keyword_after_from_query_allows_parens {const char*}:
    "ORDER"
    {
      $$ = "ORDER BY";
    }
  | "UNION"
    {
      $$ = "UNION";
    }
  | "INTERSECT"
    {
      $$ = "INTERSECT";
    }
  | KW_EXCEPT_IN_SET_OP
    {
      $$ = "EXCEPT";
    }
  | "LIMIT"
    {
      $$ = "LIMIT";
    }
;

query_without_pipe_operators {ASTQuery*}:
    // We don't use an opt_with_clause for the first element because it causes
    // shift/reduce conflicts.
    with_clause query_primary_or_set_operation[query_primary]
      opt_order_by_clause[order_by]
      opt_limit_offset_clause[offset]
      opt_lock_mode_clause[lock_mode]
    {
      auto* node = MakeNode<ASTQuery>(@$,
           $with_clause, $query_primary, $order_by, $offset, $lock_mode);
      $$ = node;
    }
  | with_clause_with_trailing_comma select_or_from_keyword
    {
      // TODO: Consider pointing the error location at the comma
      // instead of at the SELECT.
      return MakeSyntaxError(@2,
                            "Syntax error: Trailing comma after the WITH "
                            "clause before the main query is not allowed");
    }
  | with_clause "|>"
    {
      if (!language_options.LanguageFeatureEnabled(
                FEATURE_PIPES)) {
        return MakeSyntaxError(@2, "Syntax error: Unexpected |");
      } else {
        return MakeSyntaxError(@2,
                              "Syntax error: A pipe operator cannot follow "
                              "the WITH clause before the main query; The "
                              "main query usually starts with SELECT or "
                              "FROM here");
      }
    }
  | query_primary_or_set_operation[query_primary]
      opt_order_by_clause[order_by]
      opt_limit_offset_clause[offset]
      opt_lock_mode_clause[lock_mode]
    {
      ASTQuery* query = $query_primary->GetAsOrNull<ASTQuery>();
      if (query != nullptr && !query->parenthesized()) {
        $$ = ExtendNodeRight(query, $order_by, $offset, $lock_mode);
      } else if (query != nullptr && $order_by == nullptr
                  && $offset == nullptr && $lock_mode == nullptr) {
        // This means it is a query originally and there are no other clauses.
        // So then wrapping it is semantically useless.
        $$ = query;
      } else {
        $$ = MakeNode<ASTQuery>(@$,
                        $query_primary, $order_by, $offset, $lock_mode);
      }
    }
    // Support FROM queries, which just have a standalone FROM clause and
    // no other clauses (other than pipe operators).
    // FROM queries also cannot be followed with a LIMIT, ORDER BY, or set
    // operations (which would be allowed if this was attached in
    // query_primary rather than here).
  | opt_with_clause from_clause
      opt_lock_mode_clause
    {
      if (!language_options.LanguageFeatureEnabled(FEATURE_PIPES)) {
        return MakeSyntaxError(@2, "Syntax error: Unexpected FROM");
      }
      ASTFromQuery* from_query = MakeNode<ASTFromQuery>(@2, $2);
      $$ = MakeNode<ASTQuery>(@$, $1, from_query, $3);
    }
  | opt_with_clause from_clause bad_keyword_after_from_query
    {
      if (!language_options.LanguageFeatureEnabled(FEATURE_PIPES)) {
        return MakeSyntaxError(@2, "Syntax error: Unexpected FROM");
      }
      const absl::string_view keyword = $3;
      return MakeSyntaxError(@3, absl::StrCat(
          "Syntax error: ", keyword, " not supported after FROM query; "
          "Consider using pipe operator `|> ",
          keyword == "GROUP BY" ? "AGGREGATE" : keyword, "`"));
    }
  | opt_with_clause from_clause bad_keyword_after_from_query_allows_parens
    {
      if (!language_options.LanguageFeatureEnabled(FEATURE_PIPES)) {
        return MakeSyntaxError(@2, "Syntax error: Unexpected FROM");
      }
      const absl::string_view keyword = $3;
      return MakeSyntaxError(@3, absl::StrCat(
          "Syntax error: ", keyword, " not supported after FROM query; "
          "Consider using pipe operator `|> ", keyword,
          "` or parentheses around the FROM query"));
    }
;

query {ASTQuery*}:
    query_without_pipe_operators
  | query pipe_and_pipe_operator[pipe_op]
    {
      ASTQuery* query = $1;
      if (query->parenthesized()) {
        // When we have a pipe operator following a parenthesized query, rather
        // than just adding it on, we created a nested query expression, so
        // we get a better representation of how the pipes bind.
        // We set is_nested for the Unparser, and unset parenthesized to avoid
        // printing double-parentheses.
        query->set_is_nested(true);
        query->set_parenthesized(false);
        $$ = MakeNode<ASTQuery>(@$, query, $pipe_op);
      } else {
        $$ = ExtendNodeRight(query, $pipe_op);
      }
    }
;

query_after_as {ASTQuery*}:
    query
  | gql_query
;

# For helpful errors if a user writes a subquery where a subpipeline was expected.
subpipeline_bad_prefix_subquery:
    "SELECT"
  | "FROM"
  | "WITH"
;

# "Expected |>" errors are suppressed to avoid suggesting pipes everywhere,
# but we still want an error like that in subpipelines if a user writes
# pipe operators directly without writing |>.
subpipeline_bad_prefix_not_subquery:
    # This catches some common reserved keyword pipe operators plus any
    # non-reserved keyword operators.
    # "SELECT" isn't here because it's in `subpipeline_bad_prefix_subquery` above.
    identifier
  | "WHERE"
  | "LIMIT"
  | "JOIN"
  | "ORDER"
  | "GROUP"
  | "("
;

subpipeline_prefix_invalid {ASTNode*}:
    "(" subpipeline_bad_prefix_subquery
    {
      return MakeSyntaxError(@subpipeline_bad_prefix_subquery,
            "Syntax error: Expected subpipeline starting with |>, "
            "not a subquery");
    }
  | "(" subpipeline_bad_prefix_not_subquery
    {
      return MakeSyntaxError(@subpipeline_bad_prefix_not_subquery,
            "Syntax error: Expected subpipeline starting with |>");
    }
;

# Subpipeline without parens.  There must be at least one pipe operator.
subpipeline_no_parens {ASTSubpipeline*}:
  pipe_and_pipe_operator+[pipe_ops]
    {
      $$ = MakeNode<ASTSubpipeline>(@$, $pipe_ops);
    }
;

# Subpipeline with parens.  Empty subpipelines with zero operators are allowed.
subpipeline_with_parens {ASTNode*}:
    "(" pipe_and_pipe_operator*[pipe_ops] ")"
    {
      ASTSubpipeline* ast_node = MakeNode<ASTSubpipeline>(@$, $pipe_ops);
      ast_node->set_parenthesized(true);
      $$ = ast_node;
    }
;

# This is a subpipeline including parentheses.
subpipeline {ASTNode*}:
    subpipeline_with_parens
  | subpipeline_prefix_invalid
;

opt_subpipeline {ASTNode*}:
    subpipeline
  | %empty
    {
      $$ = nullptr;
    }
;

pipe_and_pipe_operator {ASTPipeOperator*}:
    "|>" pipe_operator
    {
      // Adjust the location on the operator node to include the pipe symbol.
        $$ = WithStartLocation($pipe_operator, @1);
    }
;

pipe_operator {ASTPipeOperator*}:
    pipe_where
  | pipe_select
  | pipe_extend
  | pipe_rename
  | pipe_aggregate
  | pipe_group_by
  | pipe_limit_offset
  | pipe_set_operation
  | pipe_recursive_union
  | pipe_order_by
  | pipe_join
  | pipe_call
  | pipe_window
  | pipe_distinct
  | pipe_finish
  | pipe_tablesample
  | pipe_as
  | pipe_describe
  | pipe_static_describe
  | pipe_assert
  | pipe_log
  | pipe_drop
  | pipe_set
  | pipe_pivot
  | pipe_unpivot
  | pipe_if
  | pipe_elseif
  | pipe_else
  | pipe_fork
  | pipe_tee
  | pipe_with
  | pipe_export_data
  | pipe_create_table
  | pipe_insert
  | pipe_match_recognize
  | pipe_align_operator
;

pipe_where {ASTPipeOperator*}:
    where_clause
    {
      $$ = MakeNode<ASTPipeWhere>(@$, $1);
    }
;

pipe_select {ASTPipeOperator*}:
    select_clause opt_window_clause_with_trailing_comma[window]
    {
      ASTSelect* select = ExtendNodeRight($select_clause, $window);
      $$ = MakeNode<ASTPipeSelect>(@$, select);
    }
;

pipe_limit_offset {ASTPipeOperator*}:
    limit_offset_clause
    {
      $$ = MakeNode<ASTPipeLimitOffset>(@$, $1);
    }
;

pipe_order_by {ASTPipeOperator*}:
    order_by_clause_with_opt_comma
    {
      $$ = MakeNode<ASTPipeOrderBy>(@$, $1);
    }
;

pipe_extend {ASTPipeOperator*}:
    "EXTEND" pipe_selection_item_list
             opt_window_clause_with_trailing_comma[window]
    {
      // Pipe EXTEND is represented as an ASTSelect inside an
      // ASTPipeExtend.  This allows more resolver code sharing.
      ASTSelect* select =
          MakeNode<ASTSelect>(@$, $pipe_selection_item_list, $window);
      $$ = MakeNode<ASTPipeExtend>(@$, select);
    }
;

pipe_selection_item {ASTNode*}:
    select_column_expr
  | select_column_dot_star
;

// This adds optional selection_item_order suffixes on the expression cases.
// Dot-star cases are also supported, without order suffixes.
pipe_selection_item_with_order {ASTNode*}:
    select_column_expr opt_selection_item_order
    {
      $$ = ExtendNodeRight($select_column_expr, $opt_selection_item_order);
    }
  | select_column_dot_star
;

// This is the selection list used for most pipe operators.
// It resolves to an ASTSelectList.
pipe_selection_item_list {ASTNode*}:
    (pipe_selection_item separator ",")+[items] ","?
    {
      $$ = MakeNode<ASTSelectList>(@$, $items);
    }
;

// This extends pipe_selection_item_list to support
// ASTGroupingItemOrder suffixes.
pipe_selection_item_list_with_order {ASTNode*}:
    (pipe_selection_item_with_order separator ",")+[items] ","?
    {
      $$ = MakeNode<ASTSelectList>(@$, $items);
    }
;

pipe_selection_item_list_with_order_or_empty {ASTNode*}:
    pipe_selection_item_list_with_order
  | %empty
    {
      $$ = MakeNode<ASTSelectList>(@$);
    }
;

pipe_rename_item {ASTPipeOperator*}:
    identifier[old] "AS"? identifier[new]
    {
      $$ = MakeNode<ASTPipeRenameItem>(@$, $old, $new);
    }
  | identifier "."
    {
      return MakeSyntaxError(
            @1,
            "Syntax error: Pipe RENAME can only rename columns by name alone; "
            "Renaming columns under table aliases or fields under paths is not "
            "supported");
    }
;

pipe_rename {ASTPipeOperator*}:
    "RENAME" (pipe_rename_item separator ",")+[items] ","?
    {
      $$ = MakeNode<ASTPipeRename>(@items, $items);
    }
;

pipe_aggregate {ASTPipeOperator*}:
    "AGGREGATE" opt_with_modifier[with_modifier]
                pipe_selection_item_list_with_order_or_empty[agg_list]
                opt_group_by_clause_in_pipe[group_by]
    {
      // Pipe AGGREGATE is represented as an ASTSelect inside an
      // ASTPipeAggregate.  This allows more resolver code sharing.
      ASTSelect* select = MakeNode<ASTSelect>(@$, $agg_list, $group_by);
      $$ = MakeNode<ASTPipeAggregate>(@$, $with_modifier, select);
    }
;

// |> GROUP BY is an error - likely because of an unwanted pipe symbol.
pipe_group_by {ASTPipeOperator*}:
    "GROUP"
    {
      return MakeSyntaxError(
            @1,
            "Syntax error: GROUP BY should be part of a pipe AGGREGATE "
            "operator, without a leading pipe symbol");
    }
;

pipe_set_operation_base {ASTPipeOperator*}:
    set_operation_metadata parenthesized_query
    {
      $2->set_parenthesized(true);
      $$ = MakeNode<ASTPipeSetOperation>(@$, $1, $2);
    }
  | set_operation_metadata table_clause
    {
      $$ = MakeNode<ASTPipeSetOperation>(@$, $1, $2);
    }
  | pipe_set_operation_base "," parenthesized_query
    {
      $3->set_parenthesized(true);
      $$ = ExtendNodeRight($1, $3);
    }
  | pipe_set_operation_base "," table_clause
    {
      $$ = ExtendNodeRight($1, $3);
    }
;

pipe_set_operation {ASTPipeOperator*}:
    pipe_set_operation_base ","?
;

subquery_or_subpipeline {ASTNode*}:
    # The <subpineline> rule matches inputs that look like subqueries so it can
    # give a better error on unexpected subqueries, but in the
    # <subquery_or_subpipeline> rule, it makes subqueries ambiguous. So we use
    # the rule that only matches valid subpipelines here.
    subpipeline_with_parens
  | parenthesized_query[subquery]
    {
      $subquery->set_parenthesized(true);
      // Fix the location of the subquery to include the parentheses.
      $$ = WithLocation($subquery, @$);
    }
;

pipe_recursive_union_no_alias {ASTPipeOperator*}:
    "RECURSIVE" set_operation_metadata opt_recursion_depth_modifier[depth]
    subquery_or_subpipeline
    {
      $$ = MakeNode<ASTPipeRecursiveUnion>(@$,
           $set_operation_metadata, $depth, $subquery_or_subpipeline);
    }
;

pipe_recursive_union {ASTPipeOperator*}:
    pipe_recursive_union_no_alias as_alias_with_required_as[alias]?
    {
      $$ = ExtendNodeRight($1, $alias);
    }
  | pipe_recursive_union_no_alias[base] identifier[invalid_alias]
    {
      return MakeSyntaxError(
            @invalid_alias,
            "Syntax error: The keyword \"AS\" is required before the alias for "
            "pipe RECURSIVE UNION");
    }
;

pipe_join {ASTPipeOperator*}:
    opt_natural join_type join_hint "JOIN" hint? table_primary
    opt_on_or_using_clause
    {
      // Pipe JOIN has no LHS, so we use this placeholder in the ASTJoin.
      ASTPipeJoinLhsPlaceholder* join_lhs =
          MakeNode<ASTPipeJoinLhsPlaceholder>(@$);

      // JoinRuleAction expects a list of clauses, but this grammar rule only
      // accepts one.  Wrap it into a list.
      ASTOnOrUsingClauseList* clause_list = nullptr;
      if ($7 != nullptr) {
        clause_list = MakeNode<ASTOnOrUsingClauseList>(@$, $7);
      }

      // Our main code for constructing ASTJoin is in JoinRuleAction.
      // In other places, it handles complex cases of chains of joins with
      // repeated join clauses.  Here, we always have just one JOIN,
      // so it just constructs a single ASTJoin.
      ErrorInfo error_info;
      auto *join_location =
          MakeNode<ASTLocation>(NonEmptyRangeLocation(@1, @2, @3, @4));
      ASTNode* join = JoinRuleAction(
          @1, @$, join_lhs, $1, $2, $3, $hint.value_or(nullptr), $6,
          clause_list, join_location, node_factory, &error_info);
      if (join == nullptr) {
        return MakeSyntaxError(error_info.location, error_info.message);
      }

      $$ = MakeNode<ASTPipeJoin>(@$, join);
    }
;

pipe_call {ASTPipeOperator*}:
    "CALL" tvf as_alias[alias]? opt_partition_by_clause opt_order_by_clause
    {
      $$ = MakeNode<ASTPipeCall>(@$, ExtendNodeRight($tvf, $alias),
                                 $opt_partition_by_clause,
                                 $opt_order_by_clause);
    }
;

pipe_window {ASTPipeOperator*}:
    "WINDOW" pipe_selection_item_list
    {
      // Pipe WINDOW is represented as an ASTSelect inside an
      // ASTPipeWindow.  This allows more resolver code sharing.
      ASTSelect* select = MakeNode<ASTSelect>(@$, $2);
      $$ = MakeNode<ASTPipeWindow>(@$, select);
    }
;

pipe_distinct {ASTPipeOperator*}:
    "DISTINCT"
    {
      $$ = MakeNode<ASTPipeDistinct>(@$);
    }
;

pipe_finish {ASTPipeOperator*}:
    "FINISH" hint?
    {
      $$ = MakeNode<ASTPipeFinish>(@$, $hint);
    }
;

pipe_tablesample {ASTPipeOperator*}:
    sample_clause
    {
      $$ = MakeNode<ASTPipeTablesample>(@$, $1);
    }
;

pipe_as {ASTPipeOperator*}:
    "AS" identifier
    {
      auto* alias = MakeNode<ASTAlias>(@2, $2);
      $$ = MakeNode<ASTPipeAs>(@$, alias);
    }
;

pipe_describe {ASTPipeOperator*}:
    "DESCRIBE"
    {
      $$ = MakeNode<ASTPipeDescribe>(@$);
    }
;

pipe_static_describe {ASTPipeOperator*}:
    "STATIC_DESCRIBE"
    {
      $$ = MakeNode<ASTPipeStaticDescribe>(@$);
    }
;

pipe_assert_base {ASTPipeOperator*}:
    "ASSERT" expression
    {
      $$ = MakeNode<ASTPipeAssert>(@$, $2);
    }
  | pipe_assert_base "," expression
    {
      $$ = ExtendNodeRight($1, $3);
    }
;

pipe_assert {ASTPipeOperator*}:
    pipe_assert_base ","?
    {
      $$ = WithEndLocation($1, @$);
    }
;

pipe_log {ASTPipeOperator*}:
    "LOG" hint? opt_subpipeline
    {
      $$ = MakeNode<ASTPipeLog>(@$, $hint, $opt_subpipeline);
    }
;

// This is the same as identifier, but gives a custom error if the
// items are paths rather than just identifiers.
pipe_drop_item {ASTNode*}:
    identifier
  | identifier "."
    {
      {
        return MakeSyntaxError(
            @1,
            "Syntax error: Pipe DROP can only drop columns by name alone; "
            "Dropping columns under table aliases or fields under paths is not "
            "supported");
      }
    }
  | unpack_expression
    {
      $$ = $unpack_expression;
    }
;


pipe_drop {ASTPipeOperator*}:
    "DROP" (pipe_drop_item separator ",")+[items] ","?
    {
      auto* items = MakeNode<ASTExpressionList>(@items, $items);
      $$ = MakeNode<ASTPipeDrop>(@$, items);
    }
;

pipe_set_item {ASTNode*}:
    identifier "=" expression
    {
      $$ = MakeNode<ASTPipeSetItem>(@$, $1, $3);
    }
  | identifier "."
    {
      {
        return MakeSyntaxError(
            @1,
            "Syntax error: Pipe SET can only update columns by column name "
            "alone; Setting columns under table aliases or fields under "
            "paths is not supported");
      }
    }
;

pipe_set {ASTPipeOperator*}:
    "SET" (pipe_set_item separator ",")+[items] ","?
    {
      $$ = MakeNode<ASTPipeSet>(@$, $items);
    }
;

pipe_pivot {ASTPipeOperator*}:
    pivot_clause as_alias?
    {
      // The alias is parsed separately from pivot_clause but needs to be
      // added into that AST node.
      $$ = MakeNode<ASTPipePivot>(@$, ExtendNodeRight($1, $2));
    }
;

pipe_unpivot {ASTPipeOperator*}:
    unpivot_clause as_alias?
    {
      // The alias is parsed separately from unpivot_clause but needs to be
      // added into that AST node.
      $$ = MakeNode<ASTPipeUnpivot>(@$, ExtendNodeRight($1, $2));
    }
;

pipe_if {ASTPipeOperator*}:
    pipe_if_prefix
  | pipe_if_prefix "ELSE" subpipeline
    {
      $$ = ExtendNodeRight($pipe_if_prefix, $subpipeline);
    }
;

pipe_if_prefix {ASTPipeOperator*}:
    "IF" hint? expression[condition] "THEN" subpipeline
    {
      auto* if_case = MakeNode<ASTPipeIfCase>(@$, $condition, $subpipeline);
      $$ = MakeNode<ASTPipeIf>(@$, $hint, if_case);
    }
  | pipe_if_prefix[prefix] pipe_if_elseif[elseif]
    {
      $$ = ExtendNodeRight($prefix, $elseif);
    }
;

pipe_if_elseif {ASTNode*}:
    "ELSEIF" expression[condition] "THEN" subpipeline
    {
      $$ = MakeNode<ASTPipeIfCase>(@$, $condition, $subpipeline);
    }
  | "ELSE" "IF"
    {
      return MakeSyntaxError(@1,
                           "Syntax error: Unexpected ELSE IF; Expected ELSEIF");
    }
;

// |> ELSEIF is an error - likely because of an unwanted pipe symbol.
pipe_elseif {ASTPipeOperator*}:
    "ELSEIF"
    {
      return MakeSyntaxError(
            @1,
            "Syntax error: ELSEIF should be part of a pipe IF, "
            "without a leading pipe symbol");
    }
;

// |> ELSE is an error - likely because of an unwanted pipe symbol.
pipe_else {ASTPipeOperator*}:
    "ELSE"
    {
      return MakeSyntaxError(
            @1,
            "Syntax error: ELSE should be part of a pipe IF, "
            "without a leading pipe symbol");
    }
;

pipe_fork {ASTPipeOperator*}:
    pipe_fork_impl ","?
    {
      $$ = WithEndLocation($pipe_fork_impl, @$);
    }
;

pipe_fork_impl {ASTPipeOperator*}:
    "FORK" hint? subpipeline
    {
      $$ = MakeNode<ASTPipeFork>(@$, $hint, $subpipeline);
    }
  | pipe_fork_impl "," subpipeline
    {
      $$ = ExtendNodeRight($pipe_fork_impl, $subpipeline);
    }
;

pipe_tee {ASTPipeOperator*}:
    "TEE" hint?
    {
      $$ = MakeNode<ASTPipeTee>(@$, $hint);
    }
  | pipe_tee_impl ","?
    {
      $$ = WithEndLocation($pipe_tee_impl, @$);
    }
;

pipe_tee_impl {ASTPipeOperator*}:
    "TEE" hint? subpipeline
    {
      $$ = MakeNode<ASTPipeTee>(@$, $hint, $subpipeline);
    }
  | pipe_tee_impl "," subpipeline
    {
      $$ = ExtendNodeRight($pipe_tee_impl, $subpipeline);
    }
;

pipe_with {ASTPipeOperator*}:
    with_clause ","?
    {
      $$ = MakeNode<ASTPipeWith>(@$, $with_clause);
    }
;

pipe_export_data {ASTPipeOperator*}:
    export_data_no_query
    {
      $$ = MakeNode<ASTPipeExportData>(@$, $export_data_no_query);
    }
  | export_data_no_query "AS"[as]
    {
      return MakeSyntaxError(
          @as, "Syntax error: AS query is not allowed on pipe EXPORT DATA");
    }
;

pipe_create_table {ASTPipeOperator*}:
    create_table_statement_prefix
    {
      $$ = MakeNode<ASTPipeCreateTable>(@$, $create_table_statement_prefix);
    }
  | create_table_statement_prefix "AS"[as]
    {
      return MakeSyntaxError(
          @as, "Syntax error: AS query is not allowed on pipe CREATE TABLE");
    }
;

pipe_insert {ASTPipeOperator*}:
    insert_statement_in_pipe
    {
      $$ = MakeNode<ASTPipeInsert>(@$, $insert_statement_in_pipe);
    }
;

pipe_match_recognize {ASTPipeOperator*}:
    match_recognize_clause
    {
      $$ = MakeNode<ASTPipeMatchRecognize>(@$, $match_recognize_clause);
    }
;

pipe_align_operator {ASTPipeOperator*}:
    align_operator
    {
      $$ = MakeNode<ASTPipeAlignOperator>(@$, $align_operator);
    }
;

opt_corresponding_outer_mode {ASTSetOperationColumnPropagationMode*}:
    (KW_FULL_IN_SET_OP | KW_FULL_IN_SET_OP "OUTER" | "OUTER")
    {
      $$ = MakeNode<ASTSetOperationColumnPropagationMode>(@$);
      $$->set_value(ASTSetOperation::FULL);
    }
  | KW_INNER_IN_SET_OP
    {
      $$ = MakeNode<ASTSetOperationColumnPropagationMode>(@$);
      $$->set_value(ASTSetOperation::INNER);
    }
  | KW_LEFT_IN_SET_OP "OUTER"?
    {
      $$ = MakeNode<ASTSetOperationColumnPropagationMode>(@$);
      $$->set_value(ASTSetOperation::LEFT);
    }
  | %empty
    {
      $$ = nullptr;
    }
;

opt_strict {ASTSetOperationColumnPropagationMode*}:
    "STRICT"
    {
      $$ = MakeNode<ASTSetOperationColumnPropagationMode>(@$);
        $$->set_value(ASTSetOperation::STRICT);
    }
  | %empty
    {
      $$ = nullptr;
    }
;

identifier_or_unpack_expression {ASTExpression*}:
    identifier
    {
      $$ = $identifier;
    }
  | unpack_expression
    {
      $$ = $unpack_expression;
    }
;

identifier_or_unpack_list {ASTExpressionList*}:
     (identifier_or_unpack_expression separator ",")+[list]
    {
      $$ = MakeNode<ASTExpressionList>(@$, $list);
    }
;

opt_column_match_suffix_column_list {ASTExpressionList*}:
    identifier_or_unpack_list[column_list]
    {
      $$ = $column_list;
      OVERRIDE_NEXT_TOKEN_LOOKBACK(RPAREN, LB_CLOSE_COLUMN_LIST);
    }
;

opt_column_match_suffix_column_list_with_parens {ASTExpressionList*}:
    "(" opt_column_match_suffix_column_list[column_list] ")"
    {
      $$ = WithStartLocation(WithEndLocation($column_list, @$), @1);
    }
;

opt_column_match_suffix {ColumnMatchSuffix}:
    "CORRESPONDING"
    {
      auto* mode = MakeNode<ASTSetOperationColumnMatchMode>(@$);
      mode->set_value(ASTSetOperation::CORRESPONDING);
      $$.column_match_mode = mode;
      $$.column_list = nullptr;
    }
  | "CORRESPONDING" "BY" opt_column_match_suffix_column_list_with_parens[column_list]
    {
      auto* mode = MakeNode<ASTSetOperationColumnMatchMode>(MakeLocationRange(@1, @2));
      mode->set_value(ASTSetOperation::CORRESPONDING_BY);
      $$.column_match_mode = mode;
      $$.column_list = $column_list;
    }
  | "BY" "NAME"
    {
      auto* mode = MakeNode<ASTSetOperationColumnMatchMode>(@$);
      mode->set_value(ASTSetOperation::BY_NAME);
      $$.column_match_mode = mode;
      $$.column_list = nullptr;
    }
  | "BY" "NAME" "ON" opt_column_match_suffix_column_list_with_parens[column_list]
    {
      auto* mode = MakeNode<ASTSetOperationColumnMatchMode>(MakeLocationRange(@1, @3));
      mode->set_value(ASTSetOperation::BY_NAME_ON);
      $$.column_match_mode = mode;
      $$.column_list = $column_list;
    }
  | %empty
    {
      $$.column_match_mode = nullptr;
      $$.column_list = nullptr;
    }
;

// This rule allows combining multiple query_primaries with set operations
// as long as all the set operations are identical. It is written to allow
// different set operations grammatically, but it generates an error if
// the set operations in an unparenthesized sequence are different.
// We have no precedence rules for associativity between different set
// operations but parentheses are supported to disambiguate.
query_set_operation_prefix {ASTSetOperation*}:
    query_primary[left_query] set_operation_metadata[set_op] query_primary[right_query]
    {
      auto* metadata_list =
          MakeNode<ASTSetOperationMetadataList>(@set_op, $set_op);
      $$ = MakeNode<ASTSetOperation>(@$,
                    metadata_list, $left_query, $right_query);
    }
  | query_set_operation_prefix[prefix] set_operation_metadata query_primary
    {
      ExtendNodeRight($prefix->mutable_child(0), $set_operation_metadata);
      $$ = ExtendNodeRight($prefix, $query_primary);
    }
  | query_primary set_operation_metadata "FROM"
    {
      if (!language_options.LanguageFeatureEnabled(FEATURE_PIPES)) {
        return MakeSyntaxError(@3, "Syntax error: Unexpected FROM");
      }
      return MakeSyntaxError(@3, absl::StrCat(
          "Syntax error: Unexpected FROM; "
          "FROM queries following a set operation must be parenthesized"));
    }
  | query_set_operation_prefix set_operation_metadata "FROM"
    {
      if (!language_options.LanguageFeatureEnabled(FEATURE_PIPES)) {
        return MakeSyntaxError(@3, "Syntax error: Unexpected FROM");
      }
      return MakeSyntaxError(@3, absl::StrCat(
          "Syntax error: Unexpected FROM; "
          "FROM queries following a set operation must be parenthesized"));
    }
;

set_operation_metadata {ASTNode*}:
    opt_corresponding_outer_mode query_set_operation_type hint?
    {
      OVERRIDE_NEXT_TOKEN_LOOKBACK(KW_ALL, LB_SET_OP_QUANTIFIER);
      OVERRIDE_NEXT_TOKEN_LOOKBACK(KW_DISTINCT, LB_SET_OP_QUANTIFIER);
    }
    all_or_distinct opt_strict opt_column_match_suffix
    {
      if ($opt_corresponding_outer_mode != nullptr && $opt_strict != nullptr) {
        return MakeSyntaxError(@opt_strict,
                              "Syntax error: STRICT cannot be used with outer "
                              "mode in set operations");
      }
      ASTSetOperationColumnMatchMode* match_mode =
          $opt_column_match_suffix.column_match_mode;
      if ($opt_strict != nullptr && match_mode != nullptr &&
          (match_mode->value() == ASTSetOperation::BY_NAME ||
            match_mode->value() == ASTSetOperation::BY_NAME_ON)) {
        return MakeSyntaxError(@opt_strict,
                              "Syntax error: STRICT cannot be used with BY NAME "
                              "in set operations");
      }
      ASTSetOperationColumnPropagationMode* column_propagation_mode =
          $opt_strict == nullptr ? $opt_corresponding_outer_mode : $opt_strict;
      $$ = MakeNode<ASTSetOperationMetadata>(@$,
                $query_set_operation_type, $all_or_distinct, $hint,
                $opt_column_match_suffix.column_match_mode,
                column_propagation_mode, $opt_column_match_suffix.column_list);
    }
;

query_set_operation {ASTNode*}:
    query_set_operation_prefix
    {
      $$ = WithEndLocation($1, @$);
    }
;

query_primary {ASTNode*}:
    select
  | table_clause_reserved[table]
    {
      $$ = MakeNode<ASTQuery>(@$, $table);
    }
  | parenthesized_query[query] as_alias_with_required_as[alias]?
    {
      if ($alias.has_value()) {
        if (!language_options.LanguageFeatureEnabled(
                FEATURE_PIPES)) {
          return MakeSyntaxError(
              @alias.value(), "Syntax error: Alias not allowed on parenthesized "
                      "outer query");
        }
        $$ = MakeNode<ASTAliasedQueryExpression>(@$, $query, $alias);
      } else {
        $query->set_parenthesized(true);
        $$ = $query;
      }
    }
;

// This makes an ASTSelect with none of the clauses after SELECT filled in.
select_clause {ASTSelect*}:
    "SELECT" hint?
    opt_with_modifier
    opt_all_or_distinct
    opt_select_as_clause select_list
    {
      auto* select = MakeNode<ASTSelect>(@$, $hint, $3, $5, $6);
      select->set_distinct($4 == AllOrDistinctKeyword::kDistinct);
      $$ = select;
    }
  | "SELECT" hint?
      opt_with_modifier
      opt_all_or_distinct
      opt_select_as_clause "FROM"
    {
      return MakeSyntaxError(
            @6,
            "Syntax error: SELECT list must not be empty");
    }
;

select {ASTNode*}:
    select_clause opt_from_clause opt_clauses_following_from
    {
      ASTSelect* select = static_cast<ASTSelect*>($1);
      $$ = ExtendNodeRight(select, $2, $3.where, $3.group_by,
                           $3.having, $3.qualify, $3.window);
    }
;

pre_with_modifier:
    %empty
    {
      OVERRIDE_NEXT_TOKEN_LOOKBACK(KW_WITH, LB_WITH_IN_WITH_OPTIONS);
    }
;

opt_with_modifier {ASTWithModifier*}:
    pre_with_modifier "WITH"[with] identifier
    {
      $$ = MakeNode<ASTWithModifier>(@$, $identifier);
    }
  | pre_with_modifier "WITH"[with] identifier KW_OPTIONS_IN_WITH_OPTIONS options_list
    {
      $$ = MakeNode<ASTWithModifier>(@$, $identifier, $options_list);
    }
  | pre_with_modifier
    {
      $$ = nullptr;
    }
;

// AS STRUCT, AS VALUE, or AS <path expression>. This needs some special
// handling because VALUE is a valid path expression.
opt_select_as_clause {ASTNode*}:
    "AS" "STRUCT"
    {
      auto* select_as = MakeNode<ASTSelectAs>(@$);
      select_as->set_as_mode(ASTSelectAs::STRUCT);
      $$ = select_as;
    }
  | "AS" path_expression
    {
      // "VALUE" is a valid identifier, so it can be a valid path expression.
      // But AS VALUE has a special meaning as a SELECT statement mode. We
      // handle it here, but only when VALUE is used without backquotes. With
      // backquotes the `VALUE` is treated like a regular path expression.
      bool is_value = false;
      if ($2->num_children() == 1) {
        GOOGLESQL_RET_CHECK($2->child(0)->Is<ASTIdentifier>());
        auto* identifier = $2->child(0)->GetAsOrDie<ASTIdentifier>();
        if (!identifier->is_quoted() &&
            googlesql_base::CaseEqual(identifier->GetAsStringView(), "VALUE")) {
          auto* select_as = MakeNode<ASTSelectAs>(@$);
          select_as->set_as_mode(ASTSelectAs::VALUE);
          $$ = select_as;
          is_value = true;
        }
      }
      if (!is_value) {
        auto* select_as = MakeNode<ASTSelectAs>(@$, $2);
        select_as->set_as_mode(ASTSelectAs::TYPE_NAME);
        $$ = select_as;
      }
    }
  | %empty
    {
      $$ = nullptr;
    }
;

extra_identifier_in_hints_name {absl::string_view}:
    "HASH"
  | "PROTO"
  | "PARTITION"
;

identifier_in_hints {ASTIdentifier*}:
    identifier
  | extra_identifier_in_hints_name
    {
      $$ = node_factory.MakeIdentifier(@1, $1);
    }
;

hint_entry {ASTNode*}:
    identifier_in_hints "=" expression
    {
      $$ = MakeNode<ASTHintEntry>(@$, $1, $3);
    }
  | identifier_in_hints "." identifier_in_hints "=" expression
    {
      $$ = MakeNode<ASTHintEntry>(@$, $1, $3, $5);
    }
;

hint_with_body_prefix {ASTNode*}:
    OPEN_INTEGER_PREFIX_HINT integer_literal KW_OPEN_HINT "{" hint_entry[entry]
    {
      $$ = MakeNode<ASTHint>(@$, $2, $entry);
    }
  | KW_OPEN_HINT "{" hint_entry[entry]
    {
      $$ = MakeNode<ASTHint>(@$, $entry);
    }
  | hint_with_body_prefix "," hint_entry
    {
      $$ = ExtendNodeRight($1, $3);
    }
;

hint_with_body {ASTNode*}:
    hint_with_body_prefix "}"
    {
      $$ = WithEndLocation($1, @$);
    }
;

// We can have "@<int>", "@<int> @{hint_body}", or "@{hint_body}". The case
// where both @<int> and @{hint_body} are present is covered by
// hint_with_body_prefix.
hint {ASTNode*}:
    KW_OPEN_INTEGER_HINT integer_literal
    {
      GOOGLESQL_RET_CHECK(PARSER_LA_IS_EMPTY())
          << "Expected parser lookahead to be empty following hint.";
      $$ = MakeNode<ASTHint>(@$, $integer_literal);
    }
  | hint_with_body
    {
      GOOGLESQL_RET_CHECK(PARSER_LA_IS_EMPTY())
          << "Expected parser lookahead to be empty following hint.";
      $$ = $hint_with_body;
    }
;

// This returns an AllOrDistinctKeyword to indicate what was present.
opt_all_or_distinct {AllOrDistinctKeyword}:
    "ALL"
    {
      $$ = AllOrDistinctKeyword::kAll;
    }
  | "DISTINCT"
    {
      $$ = AllOrDistinctKeyword::kDistinct;
    }
  | %empty
    {
      $$ = AllOrDistinctKeyword::kNone;
    }
;

select_list {ASTNode*}:
    (select_column separator ",")+[columns] ","?
    {
      $$ = MakeNode<ASTSelectList>(@$, $columns);
    }
;

star_except_list {ASTNode*}:
    "EXCEPT" "(" identifier_or_unpack_list ")"
    {
      $$ = MakeNode<ASTStarExceptList>(@$, WithStartLocation(WithEndLocation($3, @$), @2));
    }
;

star_replace_item {ASTNode*}:
    expression "AS" identifier
    {
      $$ = MakeNode<ASTStarReplaceItem>(@$, $1, $3);
    }
;

star_modifiers {ASTNode*}:
    star_except_list ("REPLACE" "(" (star_replace_item separator ",")+[items] ")")?
    {
      $$ = MakeNode<ASTStarModifiers>(@$, $star_except_list, $items);
    }
  | "REPLACE" "(" (star_replace_item separator ",")+[items] ")"
    {
      $$ = MakeNode<ASTStarModifiers>(@$, $items);
    }
;

select_column {ASTNode*}:
    select_column_expr
  | select_column_dot_star
  | select_column_star
;

// These are the ASTSelectColumn cases for `expression [[AS] alias]`.
select_column_expr {ASTNode*}:
    expression
    {
      $$ = MakeNode<ASTSelectColumn>(@$, $1);
    }
  | select_column_expr_with_as_alias
  | expression[e] identifier[alias]
    {
      auto* alias = MakeNode<ASTAlias>(@alias, $alias);
      $$ = MakeNode<ASTSelectColumn>(@$, $e, alias);
    }
  | expression ATTACHED_ALIAS[token]
    {
      // This special token is for cases like 10K or 24h where we assume the
      // user wanted to write `10 AS K` or `24 AS h` but forgot the space
      // between the literal and the alias.
      return MakeSyntaxError(
          @token, "Syntax error: Missing whitespace between literal and alias");
    }
;

select_column_expr_with_as_alias {ASTNode*}:
    expression[expr] "AS"[as] identifier[alias]
    {
      auto* alias = MakeNode<ASTAlias>(MakeLocationRange(@as, @$), $alias);
      $$ = MakeNode<ASTSelectColumn>(@$, $expr, alias);
    }
;

// These are the ASTSelectColumn cases for `expression.*`, plus optional
// EXCEPT/REPLACE modifiers.
select_column_dot_star {ASTNode*}:
    // Bison uses the precedence of the last terminal in the rule, but this is a
    // post-fix expression, and it really should have the precedence of ".", not
    // "*".
    expression_higher_prec_than_and[expr] "." "*" %prec "."
    {
      auto* dot_star = MakeNode<ASTDotStar>(@$, $expr);
      $$ = MakeNode<ASTSelectColumn>(@$, dot_star);
    }
    // Bison uses the precedence of the last terminal in the rule, but this is a
    // post-fix expression, and it really should have the precedence of ".", not
    // "*".
  | expression_higher_prec_than_and "." "*" star_modifiers[modifiers] %prec "."
    {
      auto* dot_star_with_modifiers =
          MakeNode<ASTDotStarWithModifiers>(@$, $1, $modifiers);
      $$ = MakeNode<ASTSelectColumn>(@$, dot_star_with_modifiers);
    }
;

// These are the ASTSelectColumn cases for `*`, plus optional
// EXCEPT/REPLACE modifiers.
select_column_star {ASTNode*}:
    "*"
    {
      auto* star = MakeNode<ASTStar>(@$);
      star->set_image("*");
      $$ = MakeNode<ASTSelectColumn>(@$, star);
    }
  | "*" star_modifiers
    {
      auto* star_with_modifiers = MakeNode<ASTStarWithModifiers>(@$, $2);
      $$ = MakeNode<ASTSelectColumn>(@$, star_with_modifiers);
    }
;

as_alias {ASTAlias*}:
    "AS"? identifier
    {
      $$ = MakeNode<ASTAlias>(@$, $2);
    }
;

as_alias_with_required_as {ASTAlias*}:
    "AS" identifier
    {
      $$ = MakeNode<ASTAlias>(@$, $2);
    }
;

opt_as_or_into_alias {ASTNode*}:
    "AS" identifier
    {
      $$ = MakeNode<ASTAlias>(@$, $2);
    }
  | "INTO" identifier
    {
      $$ = MakeNode<ASTIntoAlias>(@$, $2);
    }
  | %empty
    {
      $$ = nullptr;
    }
;

// Returns true for "NATURAL", false for not-natural.
opt_natural {bool}:
    "NATURAL"
    {
      $$ = true;
    }
  | %empty
    {
      $$ = false;
    }
;

opt_int_literal_or_parameter {ASTExpression*}:
    int_literal_or_parameter
  | %empty
    {
      $$ = nullptr;
    }
;

int_literal_or_parameter {ASTExpression*}:
    integer_literal
  | parameter_expression
  | system_variable_expression
;

cast_int_literal_or_parameter {ASTExpression*}:
    "CAST" "(" int_literal_or_parameter "AS" type opt_format ")"
    {
      $$ = MakeNode<ASTCastExpression>(@$, $3, $5, $6);
    }
;

// TODO: If we update the literal productions to include
// CASTed literals, then we should update this.
possibly_cast_int_literal_or_parameter {ASTExpression*}:
    cast_int_literal_or_parameter
  | int_literal_or_parameter
;

repeatable_clause {ASTNode*}:
    "REPEATABLE" "(" possibly_cast_int_literal_or_parameter ")"
    {
      $$ = MakeNode<ASTRepeatableClause>(@$, $3);
    }
;

sample_size_value {ASTExpression*}:
    possibly_cast_int_literal_or_parameter
  | floating_point_literal
;

// Returns the TABLESAMPLE size unit as expected by ASTSampleClause::set_unit().
sample_size_unit {ASTSampleSize::Unit}:
    "ROWS"
    {
      $$ = ASTSampleSize::ROWS;
    }
  | "PERCENT"
    {
      $$ = ASTSampleSize::PERCENT;
    }
;

sample_size {ASTNode*}:
    sample_size_value sample_size_unit opt_partition_by_clause_no_hint
    {
      auto* sample_size = MakeNode<ASTSampleSize>(@$, $1, $3);
      sample_size->set_unit($2);
      $$ = sample_size;
    }
;

opt_repeatable_clause {ASTNode*}:
    repeatable_clause
  | %empty
    {
      $$ = nullptr;
    }
;

// It doesn't appear to be possible to consolidate the rules without introducing
// a shift/reduce or a reduce/reduce conflict related to REPEATABLE.
opt_sample_clause_suffix {ASTNode*}:
    repeatable_clause
    {
      $$ = MakeNode<ASTSampleSuffix>(@$, $1);
    }
  | "WITH" "WEIGHT" opt_repeatable_clause
    {
      auto* with_weight = MakeNode<ASTWithWeight>(@$);
      $$ = MakeNode<ASTSampleSuffix>(@$, with_weight, $3);
    }
  | "WITH" "WEIGHT" identifier opt_repeatable_clause
    {
      auto* alias = MakeNode<ASTAlias>(@3, $3);
      auto* with_weight = MakeNode<ASTWithWeight>(@$, alias);
      $$ = MakeNode<ASTSampleSuffix>(@$, with_weight, $4);
    }
  | "WITH" "WEIGHT" "AS" identifier opt_repeatable_clause
    {
      auto* alias = MakeNode<ASTAlias>(MakeLocationRange(@3, @4), $4);
      auto* with_weight = MakeNode<ASTWithWeight>(@$, alias);
      $$ = MakeNode<ASTSampleSuffix>(@$, with_weight, $5);
    }
  | %empty
    {
      $$ = nullptr;
    }
;

// Similar to the usual opt_sample_clause_suffix, but requires the AS alias
// in WITH WEIGHT AS <alias>, to avoi the ambiguity of whether the identifier is
// an alias or the beginning of the next garph operator.
opt_graph_sample_clause_suffix {ASTNode*}:
    repeatable_clause
    {
      $$ = MakeNode<ASTSampleSuffix>(@$, $1);
    }
  | "WITH" "WEIGHT" opt_repeatable_clause
    {
      auto* with_weight = MakeNode<ASTWithWeight>(@$);
      $$ = MakeNode<ASTSampleSuffix>(@$, with_weight, $3);
    }
  | "WITH" "WEIGHT" "AS" identifier opt_repeatable_clause
    {
      auto* alias = MakeNode<ASTAlias>(MakeLocationRange(@3, @4), $4);
      auto* with_weight = MakeNode<ASTWithWeight>(@$, alias);
      $$ = MakeNode<ASTSampleSuffix>(@$, with_weight, $5);
    }
  | %empty
    {
      $$ = nullptr;
    }
;

sample_clause {ASTSampleClause*}:
    "TABLESAMPLE" identifier "(" sample_size ")" opt_sample_clause_suffix
    {
      $$ = MakeNode<ASTSampleClause>(@$, $2, $4, $6);
    }
;

graph_sample_clause {ASTSampleClause*}:
    "TABLESAMPLE" identifier "(" sample_size ")" opt_graph_sample_clause_suffix
    {
      $$ = MakeNode<ASTSampleClause>(@$, $2, $4, $6);
    }
;

pivot_expression {ASTNode*}:
    expression as_alias?
    {
      $$ = MakeNode<ASTPivotExpression>(@$, $1, $2);
    }
;

pivot_value {ASTNode*}:
    expression as_alias?
    {
      $$ = MakeNode<ASTPivotValue>(@$, $1, $2);
    }
;

pivot_clause {ASTPivotClause*}:
    "PIVOT" "(" (pivot_expression separator ",")+[exprs]
    "FOR" expression_higher_prec_than_and[for]
    "IN" "(" (pivot_value separator ",")+[values] ")" ")"
    {
      auto* exprs = MakeNode<ASTPivotExpressionList>(@exprs, $exprs);
      auto* values = MakeNode<ASTPivotValueList>(@values, $values);
      $$ = MakeNode<ASTPivotClause>(@$, exprs, $for, values);
    }
;

opt_as_string_or_integer {ASTNode*}:
    "AS"? string_literal
    {
      $$ = MakeNode<ASTUnpivotInItemLabel>(@$, $2);
    }
  | "AS"? integer_literal
    {
      $$ = MakeNode<ASTUnpivotInItemLabel>(@$, $2);
    }
  | %empty
    {
      $$ = nullptr;
    }
;

unpivot_in_column_item {ASTExpression*}:
    path_expression
    {
      $$ = $1;
    }
  | unpack_expression
    {
      $$ = $1;
    }
;

unpivot_in_columm_list {ASTExpressionList*}:
    (unpivot_in_column_item separator ",")+[list]
    {
      $$ = MakeNode<ASTExpressionList>(@$, $list);
    }
;

unpivot_in_columm_list_with_opt_parens {ASTExpressionList*}:
    "(" unpivot_in_columm_list ")"
    {
      $$ = $2;
      $$->set_parenthesized(true);
    }
  | path_expression
    {
      $$ = MakeNode<ASTExpressionList>(@$, $1);
    }
;

path_expression_list_with_parens {ASTNode*}:
    "(" (path_expression separator ",")+[paths] ")"
    {
      $$ = MakeNode<ASTPathExpressionList>(@$, $paths);
    }
;

opt_path_expression_list_with_parens {ASTNode*}:
    path_expression_list_with_parens
  | %empty
    {
      $$ = nullptr;
    }
;

unpivot_in_item {ASTNode*}:
    unpivot_in_columm_list_with_opt_parens opt_as_string_or_integer
    {
      $$ = MakeNode<ASTUnpivotInItem>(@$, $1, $2);
    }
  | unpack_expression
    {
      $$ = MakeNode<ASTUnpivotInItem>(@$, MakeNode<ASTExpressionList>(@$, $1));
    }
;

unpivot_in_item_list_prefix {ASTNode*}:
    "(" unpivot_in_item
    {
      $$ = MakeNode<ASTUnpivotInItemList>(@$, $2);
    }
  | unpivot_in_item_list_prefix "," unpivot_in_item
    {
      $$ = ExtendNodeRight($1, $3);
    }
;

unpivot_in_item_list {ASTNode*}:
    unpivot_in_item_list_prefix ")"
    {
      $$ = WithEndLocation($1, @$);
    }
;

unpivot_value_columm_list_with_opt_parens {ASTNode*}:
    "(" unpivot_in_columm_list ")"
    {
      $$ = $2;
    }
  | path_expression
    {
      $$ = MakeNode<ASTExpressionList>(@$, $1);
    }
;

opt_unpivot_nulls_filter {ASTUnpivotClause::NullFilter}:
    "EXCLUDE" "NULLS"
    {
      $$ = ASTUnpivotClause::kExclude;
    }
  | "INCLUDE" "NULLS"
    {
      $$ = ASTUnpivotClause::kInclude;
    }
  | %empty
    {
      $$ = ASTUnpivotClause::kUnspecified;
    }
;

unpivot_clause {ASTUnpivotClause*}:
    "UNPIVOT" opt_unpivot_nulls_filter "("
   unpivot_value_columm_list_with_opt_parens
   "FOR" path_expression "IN" unpivot_in_item_list ")"
    {
      auto* unpivot_clause = MakeNode<ASTUnpivotClause>(@$, $4, $6, $8);
      unpivot_clause->set_null_filter($2);
      $$ = unpivot_clause;
    }
;

pivot_or_unpivot_clause {ASTPostfixTableOperator*}:
  pivot_clause as_alias[alias]?
    {
      $$ = ExtendNodeRight($pivot_clause, $alias);
    }
  | unpivot_clause as_alias[alias]?
    {
      $$ = ExtendNodeRight($unpivot_clause, $alias);
    }
  | qualify_clause_nonreserved[qualify]
    {
      return MakeSyntaxError(@qualify,
                             "QUALIFY clause must be used in conjunction with "
                             "WHERE or GROUP BY or HAVING clause");
    }
;

match_recognize_clause {ASTPostfixTableOperator*}:
    KW_MATCH_RECOGNIZE_RESERVED "("
    opt_partition_by_clause[partition_by]
    order_by_clause[order_by]
    measures_clause[measures]
    opt_after_match_skip_clause[after_match_skip_clause]
    "PATTERN" "(" row_pattern_expr ")"
    "DEFINE" with_expression_variable_prefix[definitions]
    opt_options_list[options]
    ")" as_alias[alias]?
    {
      $$ = MakeNode<ASTMatchRecognizeClause>(@$,
                       $options, $partition_by, $order_by, $measures,
                       $after_match_skip_clause, $row_pattern_expr,
                       $definitions, $alias);
    }
;

within_clause_bound_expression {ASTAlignWithinBoundExpr*}:
    expression
    {
      $$ = MakeNode<ASTAlignWithinBoundExpr>(@$, $expression);
      $$->set_bound_type(ASTAlignWithinBoundExpr::TIMESTAMP);
    }
  | expression preceding_or_following
    {
      $$ = MakeNode<ASTAlignWithinBoundExpr>(@$, $expression);
      $$->set_bound_type(($preceding_or_following == PrecedingOrFollowingKeyword::kPreceding)
          ? ASTAlignWithinBoundExpr::INTERVAL_PRECEDING
          : ASTAlignWithinBoundExpr::INTERVAL_FOLLOWING);
    }
  | expression "PERIOD" preceding_or_following
    {
      $$ = MakeNode<ASTAlignWithinBoundExpr>(@$, $expression);
      $$->set_bound_type(($preceding_or_following == PrecedingOrFollowingKeyword::kPreceding)
              ? ASTAlignWithinBoundExpr::PERIOD_PRECEDING
              : ASTAlignWithinBoundExpr::PERIOD_FOLLOWING);
    }
  | "UNBOUNDED" preceding_or_following
    {
      $$ = MakeNode<ASTAlignWithinBoundExpr>(@$);
      $$->set_bound_type(($2 == PrecedingOrFollowingKeyword::kPreceding)
          ? ASTAlignWithinBoundExpr::UNBOUNDED_PRECEDING
          : ASTAlignWithinBoundExpr::UNBOUNDED_FOLLOWING);
    }
;

within_range_clause {ASTAlignWithinClause*}:
    "RANGE" "FROM" within_clause_bound_expression[low] "TO" within_clause_bound_expression[high]
    {
       $$ = MakeNode<ASTAlignWithinClause>(@$);
       $$->set_bounds($low, $high);
    }
  | "RANGE" "FROM" within_clause_bound_expression[low]
    {
      $$ = MakeNode<ASTAlignWithinClause>(@$);
      $$->set_bounds($low, nullptr);
    }
  | "RANGE" "TO" within_clause_bound_expression[high]
    {
      $$ = MakeNode<ASTAlignWithinClause>(@$);
      $$->set_bounds(nullptr, $high);
    }
  | expression "PERIOD" preceding_or_following?
    {
      auto* bound = MakeNode<ASTAlignWithinBoundExpr>(@$, $expression);
      if($preceding_or_following.value_or(PrecedingOrFollowingKeyword::kPreceding) ==
          PrecedingOrFollowingKeyword::kPreceding) {
        bound->set_bound_type(ASTAlignWithinBoundExpr::PERIOD_PRECEDING);
        $$ = MakeNode<ASTAlignWithinClause>(@$);
        $$->set_bounds(bound, nullptr);
      }
      else {
        bound->set_bound_type(ASTAlignWithinBoundExpr::PERIOD_FOLLOWING);
        $$ = MakeNode<ASTAlignWithinClause>(@$);
        $$->set_bounds(nullptr, bound);
      }
    }
  | expression preceding_or_following?
    {
      auto* bound = MakeNode<ASTAlignWithinBoundExpr>(@$, $expression);
      if($preceding_or_following.value_or(PrecedingOrFollowingKeyword::kPreceding) ==
          PrecedingOrFollowingKeyword::kPreceding) {
        bound->set_bound_type(ASTAlignWithinBoundExpr::INTERVAL_PRECEDING);
        $$ = MakeNode<ASTAlignWithinClause>(@$);
        $$->set_bounds(bound, nullptr);
      }
      else {
        bound->set_bound_type(ASTAlignWithinBoundExpr::INTERVAL_FOLLOWING);
        $$ = MakeNode<ASTAlignWithinClause>(@$);
        $$->set_bounds(nullptr, bound);
      }
    }
  | "UNBOUNDED" preceding_or_following
    {
      auto* bound = MakeNode<ASTAlignWithinBoundExpr>(@$);
      if($preceding_or_following == PrecedingOrFollowingKeyword::kPreceding) {
        bound->set_bound_type(ASTAlignWithinBoundExpr::UNBOUNDED_PRECEDING);
        $$ = MakeNode<ASTAlignWithinClause>(@$);
        $$->set_bounds(bound, nullptr);
      }
      else {
        bound->set_bound_type(ASTAlignWithinBoundExpr::UNBOUNDED_FOLLOWING);
        $$ = MakeNode<ASTAlignWithinClause>(@$);
        $$->set_bounds(nullptr, bound);
      }
    }
;

within_clause {ASTAlignWithinClause*}:
    "WITHIN" "(" within_range_clause ")"
    {
        $$ = WithLocation($within_range_clause, @$);

    }
  |  "WITHIN" "("[lparen] ")"[rparen]
    {
       auto* low = MakeNode<ASTAlignWithinBoundExpr>(
        ParseLocationRange(@lparen.end(), @lparen.end()));
       low->set_bound_type(ASTAlignWithinBoundExpr::UNBOUNDED_PRECEDING);
       auto* high = MakeNode<ASTAlignWithinBoundExpr>(
        ParseLocationRange(@rparen.start(), @rparen.start()));
       high->set_bound_type(ASTAlignWithinBoundExpr::UNBOUNDED_FOLLOWING);
       $$ = MakeNode<ASTAlignWithinClause>(@$);
       $$->set_bounds(low, high);
    }
;


metrics_clause {ASTMetricsClause*}:
    "METRICS" (expression_with_alias separator ",")+[exprs]
    {
      $$ = MakeNode<ASTMetricsClause>(@$, $exprs);
    }
;

origin_expression {ASTAlignOriginExpression*}:
    "ORIGIN" expression[expr]
  {
    // "EPOCH" is parsed as an expression (path expression) instead of having
    // a parser rule which introduces a reduce/reduce conflict.
    if ($expr->node_kind() == AST_PATH_EXPRESSION &&
          ! $expr->parenthesized() &&
          $expr->num_children() == 1) {
      auto identifier = $expr->child(0)->GetAs<ASTIdentifier>();
      if(googlesql_base::CaseEqual(identifier->GetAsStringView(), "EPOCH") &&
          ! identifier->is_quoted()) {
        $$ = MakeNode<ASTAlignOriginExpression>(@$);
        $$->set_is_epoch(true);
      }
      else {
        $$ = MakeNode<ASTAlignOriginExpression>(@$, $expr);
      }
    } else {
      $$ = MakeNode<ASTAlignOriginExpression>(@$, $expr);
    }
  }
;

align_operator {ASTPostfixTableOperator*}:
    KW_ALIGN_RESERVED "("
    ("TIMESTAMP" expression_with_opt_alias[timestamp])?
    "PERIOD" expression[period]
    origin_expression[origin]?
    ("OUTPUT" "WITHIN" within_range_clause[within])?
    partition_by_clause_with_opt_alias[partition_by]?
    metrics_clause[metrics]?
    ")" as_alias[alias]?
    {
      $$ = MakeNode<ASTAlignOperator>(@$,
                      $timestamp, $period, $origin,
                      $within, $partition_by, $metrics, $alias);
    }
;

measures_clause {ASTNode*}:
    "MEASURES" (select_column_expr_with_as_alias separator ",")+[measures]
    {
      $$ = MakeNode<ASTSelectList>(@measures, $measures);
    }
;

opt_after_match_skip_clause {ASTNode*}:
    %empty
    {
      $$ = nullptr;
    }
  | "AFTER" "MATCH" "SKIP" skip_to_target
    {
      $$ = $skip_to_target;
    }
;

skip_to_target {ASTNode*}:
    "PAST" "LAST" "ROW"
    {
      auto* skip_clause = MakeNode<ASTAfterMatchSkipClause>(@$);
      skip_clause->set_target_type(ASTAfterMatchSkipClause::PAST_LAST_ROW);
      $$ = skip_clause;
    }
  | "TO" "NEXT" "ROW"
    {
      auto* skip_clause = MakeNode<ASTAfterMatchSkipClause>(@$);
      skip_clause->set_target_type(ASTAfterMatchSkipClause::TO_NEXT_ROW);
      $$ = skip_clause;
    }
;

row_pattern_expr {ASTRowPatternExpression*}:
    row_pattern_concatenation_or_empty
  | row_pattern_expr[alt] "|" row_pattern_concatenation_or_empty[e]
    {
      $$ = MakeOrCombineRowPatternOperation(
          ASTRowPatternOperation::ALTERNATE, node_factory, @$, $alt, $e);
    }
  | row_pattern_expr[alt] "||"[double_pipe] row_pattern_concatenation_or_empty[e]
    {
      // Special case for the concat `||` operator since it's treated as a
      // single token. Create an empty pattern in the middle, with its point
      // location in the middle of the `||`.
      auto middle_loc = @double_pipe.start();
      middle_loc.IncrementByteOffset(1);
      ParseLocationRange middle_range(middle_loc, middle_loc);

      auto* middle_empty = MakeNode<ASTEmptyRowPattern>(middle_range);
      auto* op = MakeOrCombineRowPatternOperation(
                  ASTRowPatternOperation::ALTERNATE, node_factory, @$,
                  $alt, middle_empty);

      $$ = MakeOrCombineRowPatternOperation(
          ASTRowPatternOperation::ALTERNATE, node_factory, @$, op, $e);
    }
;

row_pattern_concatenation_or_empty {ASTRowPatternExpression*}:
    row_pattern_concatenation
  | %empty
    {
      // Unparenthesized empty pattern can only appear at the top-level or in
      // alternations, e.g. "(a|)".
      $$ = MakeNode<ASTEmptyRowPattern>(@$);
    }
;

row_pattern_concatenation {ASTRowPatternExpression*}:
    row_pattern_factor
  | row_pattern_concatenation[sequence] row_pattern_factor[e]
    {
      $$ = MakeOrCombineRowPatternOperation(
          ASTRowPatternOperation::CONCAT, node_factory, @$, $sequence, $e);
    }
;

row_pattern_factor {ASTRowPatternExpression*}:
    row_pattern_primary
  | row_pattern_anchor
  | quantified_row_pattern
;

row_pattern_anchor {ASTRowPatternExpression*}:
    "^"
    {
      auto* anchor = MakeNode<ASTRowPatternAnchor>(@$);
      anchor->set_anchor(ASTRowPatternAnchor::START);
      $$ = anchor;
    }
  | "$"
    {
      auto* anchor = MakeNode<ASTRowPatternAnchor>(@$);
      anchor->set_anchor(ASTRowPatternAnchor::END);
      $$ = anchor;
    }
;

quantified_row_pattern {ASTRowPatternExpression*}:
    row_pattern_primary[primary] row_pattern_quantifier[quantifier]
    {
      $$ = MakeNode<ASTRowPatternQuantification>(@$, $primary, $quantifier);
    }
;

row_pattern_primary {ASTRowPatternExpression*}:
    identifier
    {
      $$ = MakeNode<ASTRowPatternVariable>(@$, $1);
    }
  | "(" row_pattern_expr[e] ")"
    {
      $e->set_parenthesized(true);
      // Don't include the location in the parentheses. Semantic error
      // messages about this expression should point at the start of the
      // expression, not at the opening parentheses.
      $$ = $e;
    }
;

row_pattern_quantifier {ASTQuantifier*}:
    potentially_reluctant_quantifier[q]
  | potentially_reluctant_quantifier[q] "?"
    {
      $$ = WithEndLocation($q, @$);
      $$->set_is_reluctant(true);
    }
  | "{" int_literal_or_parameter[count] "}"
    {
      // {n} is never reluctant because it matches exactly `n` times regardless.
      $$ = MakeNode<ASTFixedQuantifier>(@$, $count);
    }
;

plus_or_star_quantifier {ASTSymbolQuantifier*}:
    "*"
    {
      $$ = MakeNode<ASTSymbolQuantifier>(@$);
      $$->set_symbol(ASTSymbolQuantifier::STAR);
    }
  | "+"
    {
      $$ = MakeNode<ASTSymbolQuantifier>(@$);
      $$->set_symbol(ASTSymbolQuantifier::PLUS);
    }
;

// Quantifiers that may be marked as reluctant (by adding an extra '?')
potentially_reluctant_quantifier {ASTQuantifier*}:
    "?"
    {
      auto* quantifier = MakeNode<ASTSymbolQuantifier>(@$);
      quantifier->set_symbol(ASTSymbolQuantifier::QUESTION_MARK);
      $$ = quantifier;
    }
  | plus_or_star_quantifier
  | "{" opt_int_literal_or_parameter[lower] ","
    opt_int_literal_or_parameter[upper] "}"
    {
      auto* lower_bound = MakeNode<ASTQuantifierBound>(@$, $lower);
      auto* upper_bound = MakeNode<ASTQuantifierBound>(@$, $upper);
      $$ = MakeNode<ASTBoundedQuantifier>(@$, lower_bound, upper_bound );
    }
;

table_subquery {ASTTableSubquery*}:
    parenthesized_query[query] as_alias[alias]? pivot_or_unpivot_clause[pivot]?
    {
      $query->set_is_pivot_input(
          $pivot.has_value() && (*$pivot)->Is<ASTPivotClause>());
      $query->set_is_nested(true);
      // As we set is_nested true, if parenthesized is also true, then
      // we print two sets of brackets in very disorderly way.
      // So set parenthesized to false.
      $query->set_parenthesized(false);
      $$ = MakeNode<ASTTableSubquery>(@$, $query, $alias, $pivot);
    }
;

table_clause_target {ASTNode*}:
    path_expression
  | tvf as_alias[alias]? pivot_or_unpivot_clause[pivot]?
    {
      $$ = ExtendNodeRight($tvf, $alias, $pivot);
    }
;

table_clause_target_parenthesized {ASTNode*}:
    table_clause_target
  | parenthesized_query
;

// TODO: b/568351814 - Accept a STRING literal as a TABLE clause target, e.g.
// TABLE 'path', tvf(TABLE 'path') and
// tvf(TABLE ('path' PARTITION BY x ORDER BY y)), and as a DESCRIBE target (see
// describe_info) in a follow-up CL. See
// (broken link).
// TODO: b/568351814 - Decide how STRING literal table references interact with
// table versioning, e.g. the AT clause in version-aware DML.
table_clause_no_keyword {ASTTableClause*}:
    table_clause_target
    {
      $$ = MakeNode<ASTTableClause>(@$, $table_clause_target);
    }
  | "(" table_clause_target_parenthesized opt_partition_by_clause opt_order_by_clause ")"
    {
      $$ = MakeNode<ASTTableClause>(
          @$, $table_clause_target_parenthesized, $opt_partition_by_clause,
          $opt_order_by_clause);
    }
;

table_clause_reserved {ASTNode*}:
    KW_TABLE_FOR_TABLE_CLAUSE table_clause_no_keyword
    {
      $$ = WithStartLocation($table_clause_no_keyword, @1);
    }
;

table_clause_unreserved {ASTNode*}:
    "TABLE" table_clause_no_keyword
    {
      $$ = WithStartLocation($table_clause_no_keyword, @1);
    }
;

table_clause {ASTNode*}:
    table_clause_reserved
  | table_clause_unreserved
;

model_clause {ASTNode*}:
    "MODEL" path_expression
    {
      $$ = MakeNode<ASTModelClause>(@$, $2);
    }
;

connection_clause {ASTNode*}:
    "CONNECTION" path_expression_or_default
    {
      $$ = MakeNode<ASTConnectionClause>(@$, $2);
    }
  | "CONNECTION" "(" (connection_kv_entry separator ",")+[kv_pairs] ")"
    {
      $$ = MakeNode<ASTConnectionClause>(@$, $kv_pairs);
    }
;

connection_kv_entry {ASTConnectionKeyValuePair*}:
    identifier[key] "=" expression[value]
    {
      $$ = MakeNode<ASTConnectionKeyValuePair>(@$, $key, $value);
    }
;

graph_clause {ASTNode*}:
    "GRAPH" path_expression
    {
      $$ = MakeNode<ASTGraphClause>(@$, $2);
    }
;

descriptor_column {ASTNode*}:
    identifier
    {
      $$ = MakeNode<ASTDescriptorColumn>(@$, $1);
    }
;

descriptor_argument {ASTNode*}:
    "DESCRIPTOR" "(" (descriptor_column separator ",")+[columns] ")"
    {
      auto* columns = MakeNode<ASTDescriptorColumnList>(@columns, $columns);
      $$ = MakeNode<ASTDescriptor>(@$, columns);
    }
;

input_table_argument {ASTNode*}:
    "INPUT" "TABLE"
    {
      $$ = MakeNode<ASTInputTableArgument>(@$);
    }
;

named_non_expr_argument {ASTNamedArgument*}:
    identifier_in_named_arguments[name] "=>" table_clause
    {
      auto* query = MakeNode<ASTQuery>(@3, $table_clause);
      auto* expr_subquery = MakeNode<ASTExpressionSubquery>(@3, query);
      $$ = MakeNode<ASTNamedArgument>(@$, $name, expr_subquery);
    }
;

column_list_spec {ASTExpression*}:
    "COLUMNS" "(" expression ")"
    {
      $$ = MakeNode<ASTColumnListSpec>(@$, $3);
    }
;

tvf_argument {ASTNode*}:
    expression
    {
      $$ = MakeNode<ASTTVFArgument>(@$, $1);
    }
  | descriptor_argument
    {
      $$ = MakeNode<ASTTVFArgument>(@$, $1);
    }
  | column_list_spec
    {
      $$ = MakeNode<ASTTVFArgument>(@$, $1);
    }
  | table_clause
    {
      $$ = MakeNode<ASTTVFArgument>(@$, $1);
    }
  | model_clause
    {
      $$ = MakeNode<ASTTVFArgument>(@$, $1);
    }
  | connection_clause
    {
      $$ = MakeNode<ASTTVFArgument>(@$, $1);
    }
  | graph_clause
    {
      $$ = MakeNode<ASTTVFArgument>(@$, $1);
    }
  | named_argument
    {
      $$ = MakeNode<ASTTVFArgument>(@$, $1);
    }
  | named_non_expr_argument
    {
      $$ = MakeNode<ASTTVFArgument>(@$, $1);
    }
  | input_table_argument
    {
      $$ = MakeNode<ASTTVFArgument>(@$, $1);
    }
  | "(" model_clause ")"
    {
      return MakeSyntaxError(
            @1,
            "Syntax error: Model arguments for table-valued function "
            "calls written as \"MODEL path\" must not be enclosed in "
            "parentheses. To fix this, replace (MODEL path) with MODEL path");
    }
  | "(" connection_clause ")"
    {
      return MakeSyntaxError(
            @1,
            "Syntax error: Connection arguments for table-valued function "
            "calls written as \"CONNECTION path\" must not be enclosed in "
            "parentheses. To fix this, replace (CONNECTION path) with "
            "CONNECTION path");
    }
  | "(" named_argument ")"
    {
      return MakeSyntaxError(
            @1,
            "Syntax error: Named arguments for table-valued function "
            "calls written as \"name => value\" must not be enclosed in "
            "parentheses. To fix this, replace (name => value) with "
            "name => value");
    }
  | "SELECT"
    {
      return MakeSyntaxError(
            @1,
            "Syntax error: Each subquery argument for table-valued function "
            "calls must be enclosed in parentheses. To fix this, replace "
            "SELECT... with (SELECT...)");
    }
  | "WITH"
    {
      return MakeSyntaxError(
            @1,
            "Syntax error: Each subquery argument for table-valued function "
            "calls must be enclosed in parentheses. To fix this, replace "
            "WITH... with (WITH...)");
    }
;

tvf {ASTTVF*}:
    (path_expression | "IF"[if]) "(" (tvf_argument separator ",")*[args] ")" hint?
    {
      auto* name = $path_expression.value_or(nullptr);
      if (@if.has_value()) {
        auto* id = node_factory.MakeIdentifier(*@if, *$if);
        name = MakeNode<ASTPathExpression>(*@if, id);
      }
      $$ = MakeNode<ASTTVF>(@$, name, $args, $hint);
    }
;

table_path_expression_base {ASTNode*}:
    unnest_expression
  | maybe_slashed_or_dashed_path_expression
    // A STRING literal used as a table reference, e.g. FROM 'path'.
  | string_literal
  | path_expression "["
    {
      return MakeSyntaxError(
            @2,
            "Syntax error: Array element access is not allowed in the FROM "
            "clause without UNNEST; Use UNNEST(<expression>)");
    }
  | path_expression "." "("
    {
      return MakeSyntaxError(
            @3,
            "Syntax error: Generalized field access is not allowed in the FROM "
            "clause without UNNEST; Use UNNEST(<expression>)");
    }
  | unnest_expression "["
    {
      return MakeSyntaxError(
            @2,
            "Syntax error: Array element access is not allowed in the FROM "
            "clause without UNNEST; Use UNNEST(<expression>)");
    }
  | unnest_expression "." "("
    {
      return MakeSyntaxError(
            @3,
            "Syntax error: Generalized field access is not allowed in the FROM "
            "clause without UNNEST; Use UNNEST(<expression>)");
    }
;

table_path_expression {ASTTableExpression*}:
    table_path_expression_base[path] hint? as_alias[alias]?
    (with_offset[offset] as_alias[offset_alias]?)?
    pivot_or_unpivot_clause[pivot]? for_version_clause[time]?
    {
      if ($offset.has_value()) {
        ExtendNodeRight(*$offset, $offset_alias);
      }
      if ($time.has_value() && $pivot.has_value()) {
        return MakeSyntaxError(
            *@time,
            absl::StrCat("Syntax error: ",
                          (*$pivot)->Is<ASTPivotClause>() ? "PIVOT" : "UNPIVOT",
                          " and FOR SYSTEM TIME AS OF may not be combined"));
      }
      $$ = MakeNode<ASTTablePathExpression>(@$, $path, $hint, $alias, $offset,
                                            $time, $pivot);
    }
;

table_primary {ASTTableExpression*}:
    "LATERAL"[lat]? tvf as_alias[alias]? pivot_or_unpivot_clause[pivot]?
    {
      $tvf->set_is_lateral(@lat.has_value());
      $$ = WithStartLocation(ExtendNodeRight($tvf, $as_alias, $pivot), @$);
    }
  | table_path_expression
  | "(" join ")"
    {
      ErrorInfo error_info;
      auto node = TransformJoinExpression(
        $join, node_factory, &error_info);
      if (node == nullptr) {
        return MakeSyntaxError(error_info.location, error_info.message);
      }

      $$ = MakeNode<ASTParenthesizedJoin>(@$, node);
    }
  | table_subquery
  | "LATERAL" table_subquery
    {
      $table_subquery->set_is_lateral(true);
      $$ = WithStartLocation($table_subquery, @$);
    }
  | graph_table_query
  | "GROUP" "ROWS" as_alias[alias]? pivot_or_unpivot_clause[pivot]?
    {
      auto* gr = MakeNode<ASTGroupRows>(@$, $alias.value_or(nullptr));
      $$ = WithStartLocation(ExtendNodeRight(gr, $pivot), @$);
    }
    // Postfix operators. Note that PIVOT/UNPIVOT are lumped together with each
    // rule because they're entangled with alias to work around the fact that
    // PIVOT and UNPIVOT are not reserved keywords.
    // Ideally they should be listed here.
  | table_primary[table] match_recognize_clause
    {
      $$ = ExtendNodeRight($table, $match_recognize_clause);
    }
  | table_primary[table] sample_clause
    {
      $$ = ExtendNodeRight($table, $sample_clause);
    }
  | table_primary[table] align_operator
    {
      $$ = ExtendNodeRight($table, $align_operator);
    }
;

gql_query {ASTQuery*}:
    "GRAPH" path_expression[graph] graph_operation_block[operation_block]
    {
      auto* graph_table = MakeNode<ASTGraphTableQuery>(@$,
                                    $graph, $operation_block);
      auto* graph_query = MakeNode<ASTGqlQuery>(@$, graph_table);
      graph_query->set_is_subquery(false);
      $$ = MakeNode<ASTQuery>(@$, graph_query);
    }
;

gql_statement {ASTNode*}:
    gql_query
    {
      $$ = MakeNode<ASTQueryStatement>(@$, $gql_query);
    }
;

graph_table_query {ASTTableExpression*}:
    KW_GRAPH_TABLE_RESERVED "("
      path_expression[graph]
      graph_match_operator[match]
      graph_shape_clause[shape]
      ")"
      as_alias[alias]?
    {
      $$ = MakeNode<ASTGraphTableQuery>(@$, $graph, $match, $shape, $alias);
    }
  | KW_GRAPH_TABLE_RESERVED "("
      path_expression[graph]
      graph_operation_block[operation_block]
      ")"
      as_alias[alias]?
    {
      $$ = MakeNode<ASTGraphTableQuery>(@$, $graph, $operation_block, $alias);
    }
;

graph_shape_clause {ASTNode*}:
    "COLUMNS" "(" select_list ")"
    {
      $$ = MakeNode<ASTSelect>(@$, $3);
    }
  | %empty
    {
      $$ = nullptr;
    }
;

graph_return_item {ASTNode*}:
    expression
    {
      $$ = MakeNode<ASTSelectColumn>(@$, $1);
    }
  | expression "AS" identifier
    {
      auto* alias = MakeNode<ASTAlias>(MakeLocationRange(@2, @3), $3);
      $$ = MakeNode<ASTSelectColumn>(@$, $1, alias);
    }
  | "*"
    {
      auto* star = MakeNode<ASTStar>(@$);
      star->set_image("*");
      $$ = MakeNode<ASTSelectColumn>(@$, star);
    }
;

graph_return_item_list {ASTNode*}:
    (graph_return_item separator ",")+[items]
    {
      $$ = MakeNode<ASTSelectList>(@$, $items);
    }
;

graph_return_operator {ASTNode*}:
    "RETURN" hint?
           opt_all_or_distinct[distinct]
           graph_return_item_list[return_list]
           opt_group_by_clause[group_by]
           opt_graph_order_by_clause[order_by]
           graph_offset_clause[page_offset]?
           graph_limit_clause[page_limit]?
    {
      auto* select = MakeNode<ASTSelect>(@return_list, $return_list);
      if ($distinct != AllOrDistinctKeyword::kNone) {
        WithStartLocation(select, @distinct);
      }
      // Using extend node as a convenient way to set the location correctly.
      ExtendNodeRight(ExtendNodeLeft(select, $hint), $group_by);
      select->set_distinct($distinct == AllOrDistinctKeyword::kDistinct);
      ASTGqlPage* page = nullptr;
      if ($page_offset.has_value() || $page_limit.has_value()) {
        ParseLocationRange page_location =
            @page_offset.has_value() && @page_limit.has_value()
            ? MakeLocationRange(*@page_offset, *@page_limit)
            : (@page_offset.has_value() ? *@page_offset : *@page_limit);
        page = MakeNode<ASTGqlPage>(page_location, $page_offset, $page_limit);
      }
      ASTGqlOrderByAndPage* order_by_and_page = nullptr;
      if ($order_by != nullptr || page != nullptr) {
         order_by_and_page =
             MakeNode<ASTGqlOrderByAndPage>(MakeLocationRange(@order_by, @$),
                                            $order_by, page);
      }
      $$ = MakeNode<ASTGqlReturn>(@$, select, order_by_and_page);
    }
;

opt_graph_asc_or_desc {ASTOrderingExpression::OrderingSpec}:
    opt_asc_or_desc
  | "ASCENDING"
    {
      $$ = ASTOrderingExpression::ASC;
    }
  | "DESCENDING"
    {
      $$ = ASTOrderingExpression::DESC;
    }
;

graph_ordering_expression {ASTNode*}:
    expression[expr] opt_collate_clause[collate] opt_graph_asc_or_desc[ordering]
    opt_null_order[null_order]
    {
      auto* ordering_expr =
          MakeNode<ASTOrderingExpression>(@$, $expr, $collate, $null_order);
      ordering_expr->set_ordering_spec($ordering);
      $$ = ordering_expr;
    }
;

graph_order_by_clause {ASTNode*}:
    "ORDER" hint? "BY" (graph_ordering_expression separator ",")+[exprs]
    {
      $$ = MakeNode<ASTOrderBy>(@$, $hint, $exprs);
    }
;

opt_graph_order_by_clause {ASTNode*}:
    graph_order_by_clause[order_by]
  | %empty
    {
      $$ = nullptr;
    }
;

graph_order_by_operator {ASTNode*}:
    graph_order_by_clause[order_by]
    {
      $$ = MakeNode<ASTGqlOrderByAndPage>(@$, $order_by);
    }
;

graph_limit_clause {ASTGqlPageLimit*}:
    "LIMIT" possibly_cast_int_literal_or_parameter[limit]
    {
      $$ = MakeNode<ASTGqlPageLimit>(@$, $limit);
    }
;

graph_offset_clause {ASTGqlPageOffset*}:
    ("OFFSET" | "SKIP") possibly_cast_int_literal_or_parameter[offset]
    {
      $$ = MakeNode<ASTGqlPageOffset>(@$, $offset);
    }
;

graph_match_operator {ASTNode*}:
    "MATCH" hint? graph_pattern[pattern]
    {
      $$ = MakeNode<ASTGqlMatch>(@$, $pattern, $hint);
    }
;

graph_optional_match_operator {ASTNode*}:
    "OPTIONAL" "MATCH" hint? graph_pattern
    {
      auto* match = MakeNode<ASTGqlMatch>(@$, $graph_pattern, $hint);
      match->set_optional(true);
      $$ = match;
    }
;

graph_let_operator {ASTNode*}:
    "LET" (graph_let_variable_definition separator ",")+[defs]
    {
      auto* defs = MakeNode<ASTGqlLetVariableDefinitionList>(@defs, $defs);
      $$ = MakeNode<ASTGqlLet>(@$, defs);
    }
;

graph_let_variable_definition {ASTNode*}:
    identifier "=" expression
    {
      $$ = MakeNode<ASTGqlLetVariableDefinition>(@$, $1, $3);
    }
;

graph_filter_operator {ASTNode*}:
    "FILTER" where_clause
    {
      $$ = MakeNode<ASTGqlFilter>(@$, $2);
    }
  | "FILTER" expression
    {
      auto* filter_where_clause = MakeNode<ASTWhereClause>(@$, $2);
      $$ = MakeNode<ASTGqlFilter>(@$, filter_where_clause);
    }
;

graph_with_operator {ASTNode*}:
    "WITH" opt_all_or_distinct[distinct] hint?
         graph_return_item_list[return_list] opt_group_by_clause[group_by]
    {
      auto* select = MakeNode<ASTSelect>(@return_list, $return_list);
      // Using extend node as a convenient way to set the location correctly.
      ExtendNodeRight(ExtendNodeLeft(select, $hint), $group_by);
      select->set_distinct($distinct == AllOrDistinctKeyword::kDistinct);
      if ($distinct != AllOrDistinctKeyword::kNone) {
        WithStartLocation(select, @distinct);
      }
      $$ = MakeNode<ASTGqlWith>(@$, select);
    }
;

graph_for_operator {ASTNode*}:
    "FOR" identifier
    {
      OVERRIDE_NEXT_TOKEN_LOOKBACK(KW_IN, LB_GRAPH_FOR_IN);
    }
    "IN" expression (with_offset[offset] as_alias_with_required_as[alias]?)?
    {
      if ($offset.has_value()) {
        ExtendNodeRight(*$offset, $alias);
      }
      $$ = MakeNode<ASTGqlFor>(@$, $identifier, $expression, $offset);
    }
;

graph_sample_operator {ASTNode*}:
    graph_sample_clause
    {
      $$ = MakeNode<ASTGqlSample>(@$, $graph_sample_clause);
    }
;

graph_call_operator {ASTGqlCallBase*}:
    graph_call_operator_core
  | "OPTIONAL"[kw_optional] graph_call_operator_core[call]
    {
      auto* optional_call = WithStartLocation($call, @kw_optional.start());
      optional_call->set_optional(true);
      $$ = optional_call;
    }
;

graph_call_operator_core {ASTGqlCallBase*}:
    "CALL" opt_per_clause[per] tvf opt_yield_clause[yield]
    {
      $$ = MakeNode<ASTGqlNamedCall>(@$, $tvf, $yield, $per);
      $$->set_is_partitioning($per != nullptr);
    }
  | "CALL" opt_per_clause[per] braced_graph_subquery[subquery]
    {
      $$ = MakeNode<ASTGqlInlineSubqueryCall>(@$, $subquery, $per);
      $$->set_is_partitioning($per != nullptr);
    }
  | "CALL" parenthesized_identifier_list[list] braced_graph_subquery[subquery]
    {
      $$ = MakeNode<ASTGqlInlineSubqueryCall>(@$, $subquery, $list);
      $$->set_is_partitioning(false);
    }
;

parenthesized_identifier_list {ASTNode*}:
    "(" identifier_list ")"
    {
      $$ = WithLocation($identifier_list, @$);
    }
  | "(" ")"
    {
      $$ = MakeNode<ASTIdentifierList>(@$);
    }
;

opt_per_clause {ASTNode*}:
    "PER" parenthesized_identifier_list[list]
    {
      $$ = $list;
    }
  | %empty
    {
      $$ = nullptr;
    }
;

opt_yield_clause {ASTNode*}:
    "YIELD" (yield_item separator ",")+[items]
    {
      $$ = MakeNode<ASTYieldItemList>(@$, $items);
    }
  | %empty
    {
      $$ = nullptr;
    }
;

yield_item {ASTNode*}:
    identifier[expr] as_alias_with_required_as[alias]?
    {
      $$ = MakeNode<ASTExpressionWithOptAlias>(@$, $expr, $alias);
    }
;

// GQL OrderByAndPage statements are initially parsed into three separate nodes,
// one each for ORDER BY, OFFSET, and LIMIT to avoid grammatical conflicts. We
// combine the OFFSET and LIMIT nodes in the reduce action of
// `graph_linear_operator_list` below. The small state maching in that reduce
// action could be extended to include ORDER BY as well into the same
// OrderByAndPage node if desired.
graph_linear_op {ASTNode*}:
    graph_match_operator
  | graph_optional_match_operator
  | graph_let_operator
  | graph_filter_operator
  | graph_order_by_operator
  | graph_offset_clause
  | graph_limit_clause
  | graph_with_operator
  | graph_for_operator
  | graph_sample_operator
  | graph_call_operator[call]
    {
      $$ = $call->GetAsOrDie<ASTNode>();
    }
;

graph_linear_operator_list {ASTGqlOperatorList*}:
    graph_linear_op+[ops]
    {
      std::vector<ASTNode*> ops;
      ops.reserve(ops.size());
      // The following loop is a tiny state machine that gathers OFFSET clauses
      // and following LIMIT clause into a single ASTGqlOrderByAndPage node.
      bool previous_was_offset = false;
      ASTGqlPage* page = nullptr;
      for (int i = 0; i < $ops.size(); ++i) {
        ASTNode* this_op = $ops.at(i);
        if (previous_was_offset && this_op->Is<ASTGqlPageLimit>()) {
          GOOGLESQL_RET_CHECK(page != nullptr);
          ExtendNodeRight(page, this_op);
          WithEndLocation(ops.back(), this_op->end_location());
          page = nullptr;
          previous_was_offset = false;
          continue;
        }
        previous_was_offset = this_op->Is<ASTGqlPageOffset>();
        if (this_op->Is<ASTGqlPageLimit>() || this_op->Is<ASTGqlPageOffset>()) {
          page = MakeNode<ASTGqlPage>(this_op->location(), this_op);
          this_op = MakeNode<ASTGqlOrderByAndPage>(this_op->location(), page);
        }
        ops.push_back(this_op);
      }
      $$ = MakeNode<ASTGqlOperatorList>(@$, ops);
    }
;

graph_linear_query_operation {ASTNode*}:
    graph_return_operator
    {
      $$ = MakeNode<ASTGqlOperatorList>(@$, $1);
    }
  | graph_linear_operator_list graph_return_operator
    {
      $$ = ExtendNodeRight($1, $2);
    }
;

graph_set_operation_metadata {ASTNode*}:
    opt_corresponding_outer_mode query_set_operation_type all_or_distinct
    {
      $$ = MakeNode<ASTSetOperationMetadata>(@$,
                     $query_set_operation_type, $all_or_distinct,
                     $opt_corresponding_outer_mode
                     );
    }
;

graph_composite_query_prefix {ASTNode*}:
    graph_linear_query_operation[left]
    graph_set_operation_metadata[set_op]
    graph_linear_query_operation[right]
    {
      auto* metadata_list = MakeNode<ASTSetOperationMetadataList>(@set_op, $set_op);
      $$ = MakeNode<ASTGqlSetOperation>(@$, metadata_list, $left, $right);
    }
  | graph_composite_query_prefix[prefix]
    graph_set_operation_metadata[set_op]
    graph_linear_query_operation[right]
    {
      ExtendNodeRight($prefix->mutable_child(0), $set_op);
      $$ = ExtendNodeRight($prefix, $right);
    }
;

graph_insert_element_pattern_filler {ASTGraphInsertElementPatternFiller*}:
    opt_graph_element_identifier[elem_name]
    is_label_expression[label_spec]?
    opt_graph_property_specification[prop_spec]
    {
      $$ = MakeNode<ASTGraphInsertElementPatternFiller>(
          @$, $elem_name, $label_spec, $prop_spec);
    }
;

graph_insert_edge_pattern {ASTGraphInsertEdgePattern*}:
    "<" "-" "[" graph_insert_element_pattern_filler[filler] "]" "-"
    {
      $$ = MakeNode<ASTGraphInsertEdgePattern>(@$, $filler);
      $$->set_orientation(ASTGraphEdgePattern::LEFT);
    }
  | "-" "[" graph_insert_element_pattern_filler[filler] "]" "->"
    {
      $$ = MakeNode<ASTGraphInsertEdgePattern>(@$, $filler);
      $$->set_orientation(ASTGraphEdgePattern::RIGHT);
    }
;

graph_insert_node_pattern {ASTGraphInsertNodePattern*}:
    "(" graph_insert_element_pattern_filler[filler] ")"
    {
      $$ = MakeNode<ASTGraphInsertNodePattern>(@$, $filler);
    }
;

graph_insert_path_pattern {ASTGraphInsertPathPattern*}:
    graph_insert_node_pattern
    {
      $$ = MakeNode<ASTGraphInsertPathPattern>(@$, $1);
    }
  | graph_insert_path_pattern graph_insert_edge_pattern graph_insert_node_pattern
    {
      $$ = ExtendNodeRight(ExtendNodeRight($1, $2), $3);
    }
;

graph_insert_operator {ASTGqlInsert*}:
    "INSERT" (graph_insert_path_pattern separator ",")+[patterns]
    {
      $$ = MakeNode<ASTGqlInsert>(@$, $patterns);
    }
;

graph_dml_operator {ASTNode*}:
    graph_insert_operator
  | graph_set_operator
  | graph_remove_operator
  | graph_delete_operator
;

graph_set_operator {ASTGqlSet*}:
    "SET" (graph_set_item separator ",")+[items]
    {
      $$ = MakeNode<ASTGqlSet>(@$, $items);
    }
;

graph_set_item {ASTGqlSetItem*}:
    graph_set_property_item
  | graph_set_all_properties_item
  | graph_set_label_item
;

graph_set_property_item {ASTGqlSetPropertyItem*}:
    path_expression[path] "=" expression[val]
    {
      $$ = MakeNode<ASTGqlSetPropertyItem>(@$, $path, $val);
    }
;

graph_set_all_properties_item {ASTGqlSetAllPropertiesItem*}:
    identifier[elem] "=" graph_property_specification[spec]
    {
      $$ = MakeNode<ASTGqlSetAllPropertiesItem>(
          @$, $elem, $spec->GetAsOrDie<ASTGraphPropertySpecification>());
    }
    | identifier[elem] "=" "{" "}"
    {
      $$ = MakeNode<ASTGqlSetAllPropertiesItem>(@$, $elem);
    }
;

graph_set_label_item {ASTGqlSetLabelItem*}:
    identifier[elem] ("IS" | ":") identifier[label]
    {
      $$ = MakeNode<ASTGqlSetLabelItem>(@$, $elem, $label);
    }
;

graph_remove_operator {ASTGqlRemove*}:
    "REMOVE" (graph_remove_item separator ",")+[items]
    {
      $$ = MakeNode<ASTGqlRemove>(@$, $items);
    }
;

graph_remove_item {ASTGqlRemoveItem*}:
    graph_remove_property_item
  | graph_remove_label_item
;

graph_remove_property_item {ASTGqlRemovePropertyItem*}:
    path_expression[path]
    {
      $$ = MakeNode<ASTGqlRemovePropertyItem>(@$, $path);
    }
;

graph_remove_label_item {ASTGqlRemoveLabelItem*}:
    identifier[elem] ("IS" | ":") identifier[label]
    {
      $$ = MakeNode<ASTGqlRemoveLabelItem>(@$, $elem, $label);
    }
;

graph_delete_operator {ASTGqlDelete*}:
    "NODETACH"? "DELETE" (graph_delete_item separator ",")+[items]
    {
      $$ = MakeNode<ASTGqlDelete>(@$, $items);
      $$->set_is_detach_mode(false);
    }
  | "DETACH" "DELETE" (graph_delete_item separator ",")+[items]
    {
      $$ = MakeNode<ASTGqlDelete>(@$, $items);
      $$->set_is_detach_mode(true);
    }
;

graph_delete_item {ASTGqlDeleteItem*}:
    identifier[elem]
    {
      $$ = MakeNode<ASTGqlDeleteItem>(@$, $elem);
    }
;

graph_dml_linear_operation {ASTNode*}:
    graph_dml_operator graph_return_operator?
    {
      if ($2 == nullptr) {
        $$ = MakeNode<ASTGqlOperatorList>(@$, $1);
      } else {
        $$ = MakeNode<ASTGqlOperatorList>(@$, $1, $2);
      }
    }
  | graph_linear_operator_list graph_dml_operator graph_return_operator?
    {
      auto* list = ExtendNodeRight($1, $2);
      if ($3 != nullptr) {
        ExtendNodeRight(list, $3);
      }
      $$ = list;
    }
;

graph_composite_query_block {ASTNode*}:
    graph_linear_query_operation
  | graph_composite_query_prefix
;

graph_statement_block {ASTNode*}:
    graph_composite_query_block
  | graph_dml_linear_operation
;

graph_operation_block {ASTGqlOperatorList*}:
    graph_statement_block
    {
      // Top-level list
      $$ = MakeNode<ASTGqlOperatorList>(@$, $1);
    }
  | graph_operation_block "NEXT" graph_statement_block
    {
      $$ = ExtendNodeRight($1, $3);
    }
;

graph_pattern {ASTGraphPattern*}:
    graph_path_pattern_list[path_list]
    opt_where_clause[where_clause]
    {
      $$ = WithLocation(ExtendNodeRight($path_list, $where_clause
      ), @$);
    }
;

graph_path_pattern_list {ASTGraphPattern*}:
    graph_path_pattern[pattern]
    {
      $$ = MakeNode<ASTGraphPattern>(@pattern, $pattern);
    }
  | graph_path_pattern_list[path_list] "," hint? graph_path_pattern[pattern]
    {
      if ($hint != nullptr) {
        ExtendNodeLeft($pattern, $hint);
      }
      $$ = ExtendNodeRight($path_list, $pattern);
    }
;

graph_search_prefix {ASTGraphPathSearchPrefix*}:
    "ANY"
    {
      $$ = MakeNode<ASTGraphPathSearchPrefix>(@$);
      $$->set_type(ASTGraphPathSearchPrefix::PathSearchPrefixType::ANY);
    }
  | "ANY" "SHORTEST"
    {
      $$ = MakeNode<ASTGraphPathSearchPrefix>(@$);
      $$->set_type(ASTGraphPathSearchPrefix::PathSearchPrefixType::SHORTEST);
    }
  | "ANY" "CHEAPEST"
    {
      $$ = MakeNode<ASTGraphPathSearchPrefix>(@$);
      $$->set_type(ASTGraphPathSearchPrefix::PathSearchPrefixType::CHEAPEST);
    }
  | "ANY" int_literal_or_parameter[k]
    {
      auto* k = MakeNode<ASTGraphPathSearchPrefixCount>(@$, $k);
      $$ = MakeNode<ASTGraphPathSearchPrefix>(@$, k);
      $$->set_type(ASTGraphPathSearchPrefix::PathSearchPrefixType::ANY);
    }
  | "SHORTEST" int_literal_or_parameter[k]
    {
      auto* k = MakeNode<ASTGraphPathSearchPrefixCount>(@$, $k);
      $$ = MakeNode<ASTGraphPathSearchPrefix>(@$, k);
      $$->set_type(ASTGraphPathSearchPrefix::PathSearchPrefixType::SHORTEST);
    }
  | "CHEAPEST" int_literal_or_parameter[k]
    {
      auto* k = MakeNode<ASTGraphPathSearchPrefixCount>(@$, $k);
      $$ = MakeNode<ASTGraphPathSearchPrefix>(@$, k);
      $$->set_type(ASTGraphPathSearchPrefix::PathSearchPrefixType::CHEAPEST);
    }
  | "ALL"
    {
      $$ = MakeNode<ASTGraphPathSearchPrefix>(@$);
      $$->set_type(ASTGraphPathSearchPrefix::PathSearchPrefixType::ALL);
    }
  | "ALL" "SHORTEST"
    {
      $$ = MakeNode<ASTGraphPathSearchPrefix>(@$);
      $$->set_type(ASTGraphPathSearchPrefix::PathSearchPrefixType::ALL_SHORTEST);
    }
  | "ALL" "CHEAPEST"
    {
      $$ = MakeNode<ASTGraphPathSearchPrefix>(@$);
      $$->set_type(ASTGraphPathSearchPrefix::PathSearchPrefixType::ALL_CHEAPEST);
    }
;

graph_path_mode {ASTGraphPathMode*}:
    "WALK"
    {
      $$ = MakeNode<ASTGraphPathMode>(@$);
      $$->set_path_mode(ASTGraphPathMode::WALK);
    }
  | "TRAIL"
    {
      $$ = MakeNode<ASTGraphPathMode>(@$);
      $$->set_path_mode(ASTGraphPathMode::TRAIL);
    }
  | "SIMPLE"
    {
      $$ = MakeNode<ASTGraphPathMode>(@$);
      $$->set_path_mode(ASTGraphPathMode::SIMPLE);
    }
  | "ACYCLIC"
    {
      $$ = MakeNode<ASTGraphPathMode>(@$);
      $$->set_path_mode(ASTGraphPathMode::ACYCLIC);
    }
;

graph_path_pattern {ASTGraphPathPattern*}:
    (graph_identifier[assignment] "=")? graph_search_prefix[search]?
  graph_path_mode[mode]? ("PATH" | "PATHS")? graph_path_pattern_expr[pattern]
    {
      $$ = $pattern;
      if (!$assignment.has_value() && !$search.has_value() && !$mode.has_value()) {
        return absl::OkStatus();
      }
      if ($pattern->parenthesized()) {
        // If the path pattern is parenthesized, we have already stripped the
        // ASTGraphPathPattern that would include it to avoid unnecessary nested
        // ASTGraphPathPattern nodes. However, the wrapper node is necessary
        // when a top-level path mode is present.
        $$ = MakeNode<ASTGraphPathPattern>(@$, $pattern);
      }
      ExtendNodeLeft($$, $assignment, $search, $mode);
    }
;

graph_path_pattern_expr {ASTGraphPathPattern*}:
    graph_path_factor
    {
      // if $1 is a subpath, return itself; Otherwise it's an element scan,
      // wrap it with a path. Not wrapping subpath because it's only necessary
      // to wrap it in the branch below.
      $$ = $1->Is<ASTGraphPathPattern>()
                ? $1->GetAsOrDie<ASTGraphPathPattern>()
                : MakeNode<ASTGraphPathPattern>(@$, $1);
    }
  | graph_path_pattern_expr hint? graph_path_factor
    {
      // Wrap $1 if it's a subpath (a base case from the above branch), in order
      // to extend the wrapper to $3's location. No need to wrap if its already
      // a path concatenation.
      auto* path =
          !$1->parenthesized() ? $1 : MakeNode<ASTGraphPathPattern>(@$, $1);
      auto* next_element = $3;
      if ($hint.has_value()) {
        ASTGraphPathBase* last_element =
            path->mutable_child(path->num_children() - 1)
                ->GetAsOrDie<ASTGraphPathBase>();
        // Hints in between two edges are ambiguous.
        if (last_element->Is<ASTGraphEdgePattern>() &&
            next_element->Is<ASTGraphEdgePattern>()) {
          return MakeSyntaxError(@hint,
                              absl::StrCat(
                                "Hint cannot be used in between two ",
                                last_element->GetNodeKindString(), "s"));
        }
        // If either element is an edge, attach the hint to the edge.
        if (next_element->Is<ASTGraphEdgePattern>()) {
          auto* lhs_hint = MakeNode<ASTGraphLhsHint>(*@hint, $hint);
          ExtendNodeLeft(next_element, lhs_hint);
        } else if (last_element->Is<ASTGraphEdgePattern>()) {
          auto* rhs_hint = MakeNode<ASTGraphRhsHint>(*@hint, $hint);
          // TODO: b/376552156 - Add rhs_hint to back and remove this location reset.
          auto start = last_element->start_location();
          ExtendNodeLeft(last_element, start, rhs_hint);
          last_element->set_end_location(rhs_hint->end_location());

          // Each path pattern can have only up to 1 LHS and RHS hint each.
          // If both exist, swap them here so that they're in the order expected
          // by the tree parser.
          int lhs_idx = last_element->find_child_index(
              AST_GRAPH_LHS_HINT);
          int rhs_idx = last_element->find_child_index(
              AST_GRAPH_RHS_HINT);

          if (lhs_idx >=0 && last_element->SwapChildren(
              lhs_idx, rhs_idx) != true) {
            // Failed to swap LHS and RHS hint even though both exist.
            return MakeSyntaxError(
                *@hint, "Unable to parse hints in the right order");
          }
        } else {
          // Currently ASTGraphNodePattern does not support attaching hints. So
          // wrap node patterns in path patterns and attach hints to the path
          // patterns.
          if (next_element->Is<ASTGraphNodePattern>()) {
            next_element = MakeNode<ASTGraphPathPattern>(@$, next_element);
          }
          ExtendNodeLeft(next_element, $hint);
        }
      }
      $$ = ExtendNodeRight(path, @$.end(), next_element);
    }
;

graph_path_factor {ASTGraphPathBase*}:
    graph_path_primary
  | graph_quantified_path_primary
;

graph_path_primary {ASTGraphPathBase*}:
    graph_element_pattern
    {
      $$ = $1->GetAsOrDie<ASTGraphElementPattern>();
    }
  | graph_parenthesized_path_pattern
    {
      $$ = $1->GetAsOrDie<ASTGraphPathPattern>();
    }
;

graph_parenthesized_path_pattern {ASTGraphPathPattern*}:
    "(" hint? graph_path_pattern[pattern] opt_where_clause[where] ")"
    {
      if ($hint.has_value()) {
        return MakeSyntaxError(@hint,
            "Hint cannot be used at beginning of path pattern");
      }
      if ($where != nullptr) {
        ASTGraphPathPattern* return_pattern = $pattern;
        if ($pattern->parenthesized()) {
          // Add extra layer of path for parentheses if a WHERE clause exists.
          return_pattern = MakeNode<ASTGraphPathPattern>(@$, $pattern);
        } else {
          WithLocation($pattern, @$);
        }
        return_pattern->set_parenthesized(true);
        // TODO: b/376552156 - Add $where to back.
        $$ = ExtendNodeLeft(return_pattern, @$.start(), $where);
      } else {
        // Add parentheses in place.
        $pattern->set_parenthesized(true);
        $$ = WithLocation($pattern, @$);
      }
    }
;

graph_quantifier {ASTQuantifier*}:
    "{"[opening_brace] opt_int_literal_or_parameter[lower_bound] ","
          opt_int_literal_or_parameter[upper_bound] "}"
    {
      // Add the optional lower bound and the upper bound.
      auto* lower =
          MakeNode<ASTQuantifierBound>(@lower_bound, $lower_bound);
      auto* upper = MakeNode<ASTQuantifierBound>(@upper_bound, $upper_bound);
      $$ = MakeNode<ASTBoundedQuantifier>(MakeLocationRange(@opening_brace, @$),
                    lower, upper);
    }
  | "{" int_literal_or_parameter[fixed_bound] "}"
    {
      $$ = MakeNode<ASTFixedQuantifier>(@fixed_bound, $fixed_bound);
    }
  | plus_or_star_quantifier[quantifier]
;

graph_quantified_path_primary {ASTGraphPathBase*}:
    graph_path_primary[path_primary] graph_quantifier[quantifier]
    {
      if ($path_primary->node_kind() == AST_GRAPH_NODE_PATTERN) {
        return MakeSyntaxError(@path_primary,
            "Quantifier cannot be used on a node pattern");
      }

      ASTGraphPathBase* quantifier_container = $path_primary;
      if ($path_primary->node_kind() == AST_GRAPH_EDGE_PATTERN) {
        quantifier_container =
            MakeNode<ASTGraphPathPattern>(@$, $path_primary);
        quantifier_container->GetAsOrDie<ASTGraphPathPattern>()->
            set_parenthesized(true);
      }

      ExtendNodeLeft(quantifier_container, @$.start(), $quantifier);
      // TODO: b/376552156 - Add quantifier to back and remove this location reset.
      $$ = WithLocation(quantifier_container, @$);
    }
;

graph_element_pattern {ASTGraphElementPattern*}:
    graph_node_pattern
  | graph_edge_pattern
;

%generate GraphPathReserved =
    set("SIMPLE" | "ACYCLIC" | "PATH" | "PATHS" | "TRAIL" | "WALK");

%generate GraphPathNonReserved =
    set(NonReservedKeyword & ~GraphPathReserved);

graph_identifier {ASTIdentifier*}:
    token_identifier
  |  set(GraphPathNonReserved)[kw]
    {
      GOOGLESQL_ASSIGN_OR_RETURN($$, MakeIdentifierFromKeyword(@kw, $kw));
    }
;

opt_graph_element_identifier {ASTIdentifier*}:
    graph_identifier
  | %empty
    {
      $$ = nullptr;
    }
;

opt_graph_cost {ASTNode*}:
    "COST" expression
    {
      $$ = $expression;
    }
  | %empty
    {
      $$ = nullptr;
    }
;

graph_element_pattern_filler {ASTGraphElementPatternFiller*}:
    hint? opt_graph_element_identifier[elem_name]
    is_label_expression[label]? opt_graph_property_specification[prop_spec]
    opt_where_clause[where] opt_graph_cost[cost]
    {
      if ($where != nullptr && $prop_spec != nullptr) {
        return MakeSyntaxError(@where, "WHERE clause cannot be used together with property specification");
      }
      $$ = MakeNode<ASTGraphElementPatternFiller>(
          @$, $elem_name, $label, $prop_spec, $where, $hint, $cost);
    }
;

graph_property_specification {ASTNode*}:
    graph_property_specification_prefix[prefix] "}"
    {
      $$ = WithEndLocation($prefix, @$);
    }
;

opt_graph_property_specification {ASTNode*}:
    graph_property_specification
  | %empty
    {
      $$ = nullptr;
    }
;

graph_property_specification_prefix {ASTNode*}:
    "{" (graph_property_name_and_value separator ",")+[properties]
    {
      $$ = MakeNode<ASTGraphPropertySpecification>(@$, $properties);
    }
;

graph_property_name_and_value {ASTNode*}:
    identifier[id] ":" expression[expr]
    {
      $$ = MakeNode<ASTGraphPropertyNameAndValue>(@$, $id, $expr);
    }
;

graph_node_pattern {ASTGraphElementPattern*}:
    "(" graph_element_pattern_filler ")"
    {
      $$ = MakeNode<ASTGraphNodePattern>(@$, $2);
    }
;

// Graph edge pattern delimiters are implemented as multi-tokens (with no
// whitespace in between) to disambiguate with cases like: "a[0]-3", "a<-3".
graph_edge_pattern {ASTGraphElementPattern*}:
    // Full edge patterns.
    "-" "[" graph_element_pattern_filler "]" "-"
    {
      GOOGLESQL_RETURN_IF_ERROR(ValidateNoWhitespace($1, @1, $2, @2));
      GOOGLESQL_RETURN_IF_ERROR(ValidateNoWhitespace($4, @4, $5, @5));
      $$ = MakeGraphEdgePattern(node_factory, $3,
                                ASTGraphEdgePattern::ANY, @$);
    }
  | "<" "-" "[" graph_element_pattern_filler "]" "-"
    {
      GOOGLESQL_RETURN_IF_ERROR(ValidateNoWhitespace($1, @1, $2, @2));
      GOOGLESQL_RETURN_IF_ERROR(ValidateNoWhitespace($2, @2, $3, @3));
      GOOGLESQL_RETURN_IF_ERROR(ValidateNoWhitespace($5, @5, $6, @6));
      $$ = MakeGraphEdgePattern(node_factory, $4,
                                ASTGraphEdgePattern::LEFT, @$);
    }
  | "-" "[" graph_element_pattern_filler "]" "->"
    {
      GOOGLESQL_RETURN_IF_ERROR(ValidateNoWhitespace($1, @1, $2, @2));
      GOOGLESQL_RETURN_IF_ERROR(ValidateNoWhitespace($4, @4, $5, @5));
      $$ = MakeGraphEdgePattern(node_factory, $3,
                                ASTGraphEdgePattern::RIGHT, @$);
    }
    // Abbreviated edge patterns.
  | "-"
    {
      $$ = MakeGraphEdgePattern(node_factory, nullptr,
                                ASTGraphEdgePattern::ANY, @$);
    }
  | "<" "-"
    {
      GOOGLESQL_RETURN_IF_ERROR(ValidateNoWhitespace($1, @1, $2, @2));
      $$ = MakeGraphEdgePattern(node_factory, nullptr,
                                ASTGraphEdgePattern::LEFT, @$);
    }
  | "->"
    {
      $$ = MakeGraphEdgePattern(node_factory, nullptr,
                                ASTGraphEdgePattern::RIGHT, @$);
    }
;

is_label_expression {ASTNode*}:
    ("IS" | ":") label_expression[expr]
    {
      $$ = MakeNode<ASTGraphLabelFilter>(@$, $expr);
    }
;

label_expression {ASTGraphLabelExpression*}:
    label_primary
  | label_expression "&" label_expression
    {
      $$ = MakeOrCombineGraphLabelOperation(
          ASTGraphLabelOperation::AND, node_factory, @$, $1, $3);
    }
  | label_expression "|" label_expression
    {
      $$ = MakeOrCombineGraphLabelOperation(
          ASTGraphLabelOperation::OR, node_factory, @$, $1, $3);
    }
  | "!" label_expression %prec UNARY_PRECEDENCE
    {
      auto* not_expr = MakeNode<ASTGraphLabelOperation>(@$, $2);
      not_expr->set_op_type(ASTGraphLabelOperation::NOT);
      $$ = not_expr;
    }
;

label_primary {ASTGraphLabelExpression*}:
    identifier
    {
      $$ = MakeNode<ASTGraphElementLabel>(@$, $1);
    }
  | "%"
    {
      $$ = MakeNode<ASTGraphWildcardLabel>(@$);
    }
  | parenthesized_label_expression
;

parenthesized_label_expression {ASTGraphLabelExpression*}:
    "(" label_expression ")"
    {
      $2->set_parenthesized(true);
      // Don't include the location in the parentheses. Semantic error
      // messages about this expression should point at the start of the
      // expression, not at the opening parentheses.
      $$ = $2;
    }
;

unpack_expression {ASTExpression*}:
  "**" column_list_spec[arg] %prec UNARY_PRECEDENCE
    {
      $$ = MakeNode<ASTUnpackExpression>(@$, $arg);
    }
  | "**" "(" column_list_spec[arg] ")" %prec UNARY_PRECEDENCE
    {
      $arg->set_parenthesized(true);
      $$ = MakeNode<ASTUnpackExpression>(@$, $arg);
    }
  | "**" expression_higher_prec_than_and[expr] %prec UNARY_PRECEDENCE
    {
      $$ = MakeNode<ASTUnpackExpression>(@$, $expr);
    }
;

graph_expression {ASTExpression*}:
    expression_higher_prec_than_and edge_source_endpoint_operator expression_higher_prec_than_and %prec EDGE_ENDPOINT_PRECEDENCE
    {
      auto* binary_expression = MakeNode<ASTBinaryExpression>(@$, $1, $3);
      binary_expression->set_is_not($2 == NotKeywordPresence::kPresent);
      binary_expression->set_op(ASTBinaryExpression::IS_SOURCE_NODE);
      $$ = binary_expression;
    }
  | expression_higher_prec_than_and edge_dest_endpoint_operator expression_higher_prec_than_and %prec EDGE_ENDPOINT_PRECEDENCE
    {
      auto* binary_expression = MakeNode<ASTBinaryExpression>(@$, $1, $3);
      binary_expression->set_is_not($2 == NotKeywordPresence::kPresent);
      binary_expression->set_op(ASTBinaryExpression::IS_DEST_NODE);
      $$ = binary_expression;
    }
  | expression_higher_prec_than_and[expr] is_labeled_operator[op] label_expression[labelexpr] %prec EDGE_ENDPOINT_PRECEDENCE
    {
      auto* is_labeled_predicate =
          MakeNode<ASTGraphIsLabeledPredicate>(@$, $expr, $labelexpr);
      is_labeled_predicate->set_is_not($2 == NotKeywordPresence::kPresent);
      $$ = is_labeled_predicate;
    }
  | expression_higher_prec_than_and[expr] in_operator[op] braced_graph_subquery[q] %prec "IN"
    {
      // Bison allows some cases like IN on the left hand side because it's
      // not ambiguous. The language doesn't allow this.
      if (!$expr->IsAllowedInComparison()) {
        return MakeSyntaxError(@op,
                              "Syntax error: Expression to the left of IN "
                              "must be parenthesized");
      }
      ASTLocation* in_location = MakeNode<ASTLocation>(@op);
      ASTInExpression* in_expression =
          MakeNode<ASTInExpression>(@$, $expr, in_location, $q);
      in_expression->set_is_not($op == NotKeywordPresence::kPresent);
      $$ = in_expression;
    }
;

edge_source_endpoint_operator {NotKeywordPresence}:
    "IS" "SOURCE"
    {
      $$ = NotKeywordPresence::kAbsent;
    }
    %prec EDGE_ENDPOINT_PRECEDENCE
  | "IS" "SOURCE" "OF"
    {
      $$ = NotKeywordPresence::kAbsent;
    }
    %prec EDGE_ENDPOINT_PRECEDENCE
  | "IS" "NOT" "SOURCE"
    {
      @$ = @3;  // Error messages should point at the "SOURCE".
        $$ = NotKeywordPresence::kPresent;
    }
    %prec EDGE_ENDPOINT_PRECEDENCE
  | "IS" "NOT" "SOURCE" "OF"
    {
      @$ = @3;  // Error messages should point at the "SOURCE".
        $$ = NotKeywordPresence::kPresent;
    }
    %prec EDGE_ENDPOINT_PRECEDENCE
;

edge_dest_endpoint_operator {NotKeywordPresence}:
    "IS" "DESTINATION"
    {
      $$ = NotKeywordPresence::kAbsent;
    }
    %prec EDGE_ENDPOINT_PRECEDENCE
  | "IS" "DESTINATION" "OF"
    {
      $$ = NotKeywordPresence::kAbsent;
    }
    %prec EDGE_ENDPOINT_PRECEDENCE
  | "IS" "NOT" "DESTINATION"
    {
      @$ = @3;  // Error messages should point at the "DESTINATION".
      $$ = NotKeywordPresence::kPresent;
    }
    %prec EDGE_ENDPOINT_PRECEDENCE
  | "IS" "NOT" "DESTINATION" "OF"
    {
      @$ = @3;  // Error messages should point at the "DESTINATION".
      $$ = NotKeywordPresence::kPresent;
    }
    %prec EDGE_ENDPOINT_PRECEDENCE
;

is_labeled_operator {NotKeywordPresence}:
    "IS" "LABELED"
    {
      $$ = NotKeywordPresence::kAbsent;
    }
    %prec EDGE_ENDPOINT_PRECEDENCE
  | "IS" "NOT" "LABELED"[labeled]
    {
      @$ = @labeled;
      $$ = NotKeywordPresence::kPresent;
    }
    %prec EDGE_ENDPOINT_PRECEDENCE
;

for_version_clause {ASTForVersionClause*}:
    "FOR" ("SYSTEM" "TIME" | "SYSTEM_TIME") "AS" "OF" expression[time]
    {
      $$ = MakeNode<ASTForVersionClause>(@$, $time);
    }
;

on_clause {ASTNode*}:
    "ON" expression
    {
      $$ = MakeNode<ASTOnClause>(@$, $2);
    }
;

using_clause {ASTNode*}:
    "USING" "(" (identifier separator ",")+[ids] ")"
    {
      $$ = MakeNode<ASTUsingClause>(@$, $ids);
    }
;

on_or_using_clause_lists {ASTNode*}:
    on_or_using_clause+[list]
    {
      if ($list.size() > 1) {
        if (!language_options.LanguageFeatureEnabled(
              FEATURE_ALLOW_CONSECUTIVE_ON)) {
          return MakeSyntaxError(
            $list[1]->location(),
            absl::StrCat(
                "Syntax error: Expected end of input but got keyword ",
                ($list[1]->node_kind() == AST_ON_CLAUSE
                      ? "ON" : "USING")));
        }
      }
      $$ = MakeNode<ASTOnOrUsingClauseList>(@list, $list);
    }
;

on_or_using_clause {ASTNode*}:
    on_clause
  | using_clause
;

opt_on_or_using_clause {ASTNode*}:
    on_or_using_clause?
    {
      $$ = $on_or_using_clause.value_or(nullptr);
    }
;

// Returns the join type id. Returns 0 to indicate "just a join".
join_type_base {ASTJoin::JoinType}:
    "CROSS"
    {
      $$ = ASTJoin::CROSS;
    }
  | "FULL" "OUTER"?
    {
      $$ = ASTJoin::FULL;
    }
  | "INNER"
    {
      $$ = ASTJoin::INNER;
    }
  | "LEFT" "OUTER"?
    {
      $$ = ASTJoin::LEFT;
    }
  | "RIGHT" "OUTER"?
    {
      $$ = ASTJoin::RIGHT;
    }
;

join_type {ASTJoin::JoinType}:
    join_type_base?
    {
      $$ = $join_type_base.value_or(ASTJoin::DEFAULT_JOIN_TYPE);
    }
;

// Return the join hint token as expected by ASTJoin::set_join_hint().
join_hint_base {ASTJoin::JoinHint}:
    "HASH"
    {
      $$ = ASTJoin::HASH;
    }
  | "LOOKUP"
    {
      $$ = ASTJoin::LOOKUP;
    }
;

join_hint {ASTJoin::JoinHint}:
    join_hint_base?
    {
      $$ = $join_hint_base.value_or(ASTJoin::NO_JOIN_HINT);
    }
;

join_input {ASTTableExpression*}:
    join
  | table_primary
;

// This is only used for parenthesized joins. Unparenthesized joins in the FROM
// clause are directly covered in from_clause_contents. These rules are separate
// because the FROM clause also allows comma joins, while parenthesized joins do
// not.
// Note that if there are consecutive ON/USING clauses, then this ASTJoin tree
// must be processed by TransformJoinExpression in the rule table_primary before
// the final AST is returned.
join {ASTTableExpression*}:
    join_input[input] opt_natural[natural] join_type[type] join_hint
    "JOIN"[join] hint? table_primary[table]
    on_or_using_clause_lists[on_or_using_list]?
    {
      ErrorInfo error_info;
      auto * join_location =
          MakeNode<ASTLocation>(NonEmptyRangeLocation(@natural, @type,
                                                      @join_hint, @join));
      auto node = JoinRuleAction(
          @input, @$,
          $input, $natural, $type, $join_hint, $hint.value_or(nullptr), $table,
          $on_or_using_list.value_or(nullptr), join_location, node_factory,
          &error_info);
      if (node == nullptr) {
        return MakeSyntaxError(error_info.location, error_info.message);
      }

      $$ = node->GetAsOrDie<ASTJoin>();
    }
;

from_clause_contents {ASTNode*}:
    table_primary[table]
  | from_clause_contents[from] "," table_primary[table]
    {
      ErrorInfo error_info;
      auto* comma_location = MakeNode<ASTLocation>(@2);
      auto node = CommaJoinRuleAction(
          @from, @table, $from, $table, comma_location, node_factory,
          &error_info);
      if (node == nullptr) {
        return MakeSyntaxError(error_info.location, error_info.message);
      }

      $$ = node;
    }
  | from_clause_contents[from] opt_natural[natural] join_type[type] join_hint
      "JOIN"[join] hint? table_primary[table]
      on_or_using_clause_lists[on_or_using_list]?
    {
      // Give an error if we have a RIGHT or FULL JOIN following a comma
      // join since our left-to-right binding would violate the standard.
      // See (broken link).
      if (($type == ASTJoin::FULL || $type == ASTJoin::RIGHT) &&
          $from->node_kind() == AST_JOIN) {
        const auto* join_input = $from->GetAsOrDie<ASTJoin>();
        while (true) {
          if (join_input->join_type() == ASTJoin::COMMA) {
            return MakeSyntaxError(
                @type,
                absl::StrCat("Syntax error: ",
                              ($type == ASTJoin::FULL
                                  ? "FULL" : "RIGHT"),
                              " JOIN must be parenthesized when following a "
                              "comma join.  Also, if the preceding comma join "
                              "is a correlated CROSS JOIN that unnests an "
                              "array, then CROSS JOIN syntax must be used in "
                              "place of the comma join"));
          }
          if (join_input->child(0)->node_kind() == AST_JOIN) {
            // Look deeper only if the left input is an unparenthesized join.
            join_input = join_input->child(0)->GetAsOrDie<ASTJoin>();
          } else {
            break;
          }
        }
      }

      ErrorInfo error_info;
      auto* join_location = MakeNode<ASTLocation>(
          NonEmptyRangeLocation(@natural, @type, @join_hint, @join));
      auto node = JoinRuleAction(
          @from, @$,
          $from, $natural, $type, $join_hint, $hint.value_or(nullptr), $table,
          $on_or_using_list.value_or(nullptr), join_location, node_factory,
          &error_info);
      if (node == nullptr) {
        return MakeSyntaxError(error_info.location, error_info.message);
      }

      $$ = node;
    }
  | "@"
    {
      return MakeSyntaxError(
            @1, "Query parameters cannot be used in place of table names");
    }
  | "?"
    {
      return MakeSyntaxError(
            @1, "Query parameters cannot be used in place of table names");
    }
  | "@@"
    {
      return MakeSyntaxError(
            @1, "System variables cannot be used in place of table names");
    }
;

opt_from_clause {ASTNode*}:
    from_clause?
    {
      $$ = $from_clause.value_or(nullptr);
    }
;

from_clause {ASTNode*}:
    "FROM" from_clause_contents
    {
      ErrorInfo error_info;
      auto node = TransformJoinExpression(
        $2, node_factory, &error_info);
      if (node == nullptr) {
        return MakeSyntaxError(error_info.location, error_info.message);
      }

      $$ = MakeNode<ASTFromClause>(@$, node);
    }
;

// The rules opt_clauses_following_from, opt_clauses_following_where and
// opt_clauses_following_group_by exist to constrain QUALIFY clauses to require
// a WHERE, GROUP BY, or HAVING clause when the QUALIFY keyword is nonreserved.
//
// This restriction exists to ensure that there is a clause that starts with a
// reserved keyword (WHERE, GROUP BY or HAVING) between them.
//
// When QUALIFY is enabled as a reserved keyword in the LanguageOptions, the
// requirement for the QUALIFY clause to have WHERE, GROUP BY, or HAVING
// preceding it goes away.
opt_clauses_following_from {ClausesFollowingFrom}:
    where_clause[where] opt_group_by_clause[group_by] opt_having_clause[having]
    qualify_clause[qualify]? window_clause[window]?
    {
      $$ = {$where, $group_by, $having,
            $qualify.value_or(nullptr), $window.value_or(nullptr)};
    }
  | opt_clauses_following_where
    {
      $$ = {/*where=*/nullptr, $1.group_by, $1.having, $1.qualify, $1.window};
    }
;

opt_clauses_following_where {ClausesFollowingFrom}:
    group_by_clause[group_by] opt_having_clause[having]
    qualify_clause[qualify]? window_clause[window]?
    {
      $$ = {/*where=*/nullptr, $group_by, $having, $qualify.value_or(nullptr),
            $window.value_or(nullptr)};
    }
  | opt_clauses_following_group_by
    {
      $$ = {/*where=*/nullptr, /*group_by=*/nullptr, $1.having, $1.qualify,
            $1.window};
    }
;

opt_clauses_following_group_by {ClausesFollowingFrom}:
    having_clause[having] qualify_clause[qualify]? window_clause[window]?
    {
      $$ = {/*where=*/nullptr, /*group_by=*/nullptr, $having,
            $qualify.value_or(nullptr), $window.value_or(nullptr)};
    }
  | qualify_clause_reserved[qualify]? window_clause[window]?
    {
      $$ = {/*where=*/nullptr, /*group_by=*/nullptr, /*having=*/nullptr,
            $qualify.value_or(nullptr), $window.value_or(nullptr)};
    }
;

where_clause {ASTNode*}:
    "WHERE" expression
    {
      $$ = MakeNode<ASTWhereClause>(@$, $2);
    }
;

opt_where_clause {ASTNode*}:
    where_clause?
    {
      $$ = $where_clause.value_or(nullptr);
    }
;

rollup_list {ASTNode*}:
    "ROLLUP" "(" (expression separator ",")+[expression_list] ")"
    {
      $$ = MakeNode<ASTRollup>(@$, $expression_list);
    }
;

cube_list {ASTNode*}:
    "CUBE" "(" (expression separator ",")+[expression_list] ")"
    {
      $$ = MakeNode<ASTCube>(@$, $expression_list);
    }
;

grouping_set {ASTNode*}:
    "(" ")"
    {
      $$ = MakeNode<ASTGroupingSet>(@$);
    }
  | expression
    {
      $$ = MakeNode<ASTGroupingSet>(@$, $1);
    }
  | rollup_list
    {
      $$ = MakeNode<ASTGroupingSet>(@$, $rollup_list);
    }
  | cube_list
    {
      $$ = MakeNode<ASTGroupingSet>(@$, $cube_list);
    }
;


// In selection items, NULLS FIRST/LAST is not allowed without ASC/DESC first.
selection_item_order {ASTNode*}:
    asc_or_desc opt_null_order
    {
      auto* node = MakeNode<ASTGroupingItemOrder>(@$, $opt_null_order);
      node->set_ordering_spec($1);
      $$ = node;
    }
;

opt_selection_item_order {ASTNode*}:
    selection_item_order?
    {
      $$ = $selection_item_order.value_or(nullptr);
    }
;

// In grouping items, NULLS FIRST/LAST is allowed without ASC/DESC first.
opt_grouping_item_order {ASTNode*}:
    opt_selection_item_order
  | null_order
    {
      auto* node = MakeNode<ASTGroupingItemOrder>(@$, $null_order);
      $$ = node;
    }
;

grouping_item_base {ASTNode*}:
    "(" ")"
    {
      auto* grouping_item = MakeNode<ASTGroupingItem>(@$);
      $$ = WithEndLocation(grouping_item, @$);
    }
  | rollup_list
    {
      $$ = MakeNode<ASTGroupingItem>(@$, WithEndLocation($rollup_list, @$));
    }
  | cube_list
    {
      $$ = MakeNode<ASTGroupingItem>(@$, WithEndLocation($cube_list, @$));
    }
  | "GROUPING" "SETS" "(" (grouping_set separator ",")+[grouping_set_list] ")"
    {
      auto* grouping_sets =
          MakeNode<ASTGroupingSetList>(@$, $grouping_set_list);
      $$ = MakeNode<ASTGroupingItem>(@$, WithEndLocation(grouping_sets, @$));
    }
;

// This is the grouping item in standard syntax GROUP BY.
// It doesn't support aliases or ordering suffixes.
grouping_item {ASTNode*}:
    grouping_item_base
  | expression
    {
      $$ = MakeNode<ASTGroupingItem>(@$, $1);
    }
;

// This is the grouping item in pipe AGGREGATE's GROUP BY.
// It supports aliases and ordering suffixes.
grouping_item_in_pipe {ASTNode*}:
    grouping_item_base
  | expression as_alias? opt_grouping_item_order
    {
      $$ = MakeNode<ASTGroupingItem>(@$, $1, $2, $3);
    }
;

group_by_preamble {GroupByPreamble}:
    "GROUP" hint? "BY"
    {
      $$.hint = $hint.value_or(nullptr);
      $$.and_order_by = false;
    }
;

group_by_preamble_in_pipe {GroupByPreamble}:
    "GROUP" hint? ("AND"[and_order] "ORDER")? "BY"
    {
      if (@and_order.has_value() && !language_options.LanguageFeatureEnabled(
                FEATURE_PIPES)) {
        return MakeSyntaxError(
            *@and_order, "Syntax error: Unexpected AND");
      }
      $$.hint = $hint.value_or(nullptr);
      $$.and_order_by = @and_order.has_value();
    }
;

group_by_clause_prefix {ASTNode*}:
    group_by_preamble[preamble] (grouping_item separator ",")+[items]
    {
      $$ = MakeNode<ASTGroupBy>(@$, $preamble.hint, $items);
    }
;

group_by_clause_prefix_in_pipe {ASTNode*}:
    group_by_preamble_in_pipe[preamble] grouping_item_in_pipe[item]
    {
      auto* node = MakeNode<ASTGroupBy>(@$, $preamble.hint, $item);
        node->set_and_order_by($preamble.and_order_by);
        $$ = node;
    }
  | group_by_clause_prefix_in_pipe[prefix] "," grouping_item_in_pipe[item]
    {
      $$ = ExtendNodeRight($prefix, $item);
    }
;

group_by_all {ASTNode*}:
    group_by_preamble[preamble] "ALL"[all]
    {
      auto* group_by_all = MakeNode<ASTGroupByAll>(@all);
      auto* node = MakeNode<ASTGroupBy>(@$, $preamble.hint, group_by_all);
      node->set_and_order_by($preamble.and_order_by);
      $$ = node;
    }
;

group_by_clause {ASTNode*}:
    group_by_all
  | group_by_clause_prefix
;

// This is a GROUP BY clause outside pipe AGGREGATE.
opt_group_by_clause {ASTNode*}:
    group_by_clause?
    {
      $$ = $group_by_clause.value_or(nullptr);
    }
;

// This is a GROUP BY clause in pipe AGGREGATE.
// Its grouping_items support aliases and order suffixes, and it accepts
// a trailing comma.
// This version does not support GROUP BY ALL.
//
// The `..._in_pipe` subrules eventually use `grouping_item_in_pipe`
// instead of `grouping_item`.
//
// Combining those results in a shift/reduce conflict, and also allows
// unwanted modifiers on the non-pipe GROUP BY items.
//
// When TextMapper templated rules are supported, those rules can be merged.
group_by_clause_in_pipe {ASTNode*}:
    group_by_clause_prefix_in_pipe ","?
    {
      $$ = WithEndLocation($1, @$);
    }
;

opt_group_by_clause_in_pipe {ASTNode*}:
    group_by_clause_in_pipe?
    {
      $$ = $group_by_clause_in_pipe.value_or(nullptr);
    }
;

having_clause {ASTNode*}:
    "HAVING" expression
    {
      $$ = MakeNode<ASTHaving>(@$, $2);
    }
;

opt_having_clause {ASTNode*}:
    having_clause?
    {
      $$ = $having_clause.value_or(nullptr);
    }
;

window_definition {ASTNode*}:
    identifier "AS" window_specification
    {
      $$ = MakeNode<ASTWindowDefinition>(@$, $1, $3);
    }
;

window_clause {ASTNode*}:
    "WINDOW" window_definition
    {
      $$ = MakeNode<ASTWindowClause>(@$, $2);
    }
  | window_clause "," window_definition
    {
      $$ = ExtendNodeRight($1, $3);
    }
;

window_clause_with_trailing_comma {ASTNode*}:
    window_clause ","?
    {
      $$ = WithEndLocation($1, @$);
    }
;

opt_window_clause_with_trailing_comma {ASTNode*}:
    window_clause_with_trailing_comma?
    {
      $$ = $window_clause_with_trailing_comma.value_or(nullptr);
    }
;

qualify_clause {ASTNode*}:
    qualify_clause_reserved
  | qualify_clause_nonreserved
;

qualify_clause_reserved {ASTNode*}:
    KW_QUALIFY_RESERVED expression
    {
      if (!language_options.LanguageFeatureEnabled(FEATURE_QUALIFY)) {
        return MakeSyntaxError(@1, "QUALIFY is not supported");
      }
      $$ = MakeNode<ASTQualify>(@$, $2);
    }
;

qualify_clause_nonreserved {ASTNode*}:
    KW_QUALIFY_NONRESERVED expression
    {
      if (!language_options.LanguageFeatureEnabled(FEATURE_QUALIFY)) {
        return MakeSyntaxError(@1, "QUALIFY is not supported");
      }
      $$ = MakeNode<ASTQualify>(@$, $2);
    }
;

limit_offset_clause {ASTNode*}:
    limit_expression
    "OFFSET" expression
    {
      $$ = MakeNode<ASTLimitOffset>(@$, $limit_expression, $expression);
    }
  | limit_expression
    {
      $$ = MakeNode<ASTLimitOffset>(@$, $limit_expression);
    }
  | limit_all "OFFSET" expression
    {
      $$ = MakeNode<ASTLimitOffset>(@$, $limit_all, $expression);
    }
  | limit_all
    {
      $$ = MakeNode<ASTLimitOffset>(@$, $limit_all);
    }
;

limit_expression {ASTNode*}:
    "LIMIT" expression
    {
      $$ = MakeNode<ASTLimit>(@$, $expression);
    }
;

limit_all {ASTNode*}:
    "LIMIT" "ALL"[all]
    {
      auto* all = MakeNode<ASTLimitAll>(@all);
      $$ = MakeNode<ASTLimit>(@$, all);
    }
;

opt_limit_offset_clause {ASTNode*}:
    limit_offset_clause?
    {
      $$ = $limit_offset_clause.value_or(nullptr);
    }
;

lock_mode_clause {ASTLockMode*}:
    KW_FOR_BEFORE_LOCK_MODE "UPDATE"
    {
      if (!language_options.LanguageFeatureEnabled(FEATURE_FOR_UPDATE)) {
        return MakeSyntaxError(@1, "FOR UPDATE is not supported");
      }
      auto* node = MakeNode<ASTLockMode>(@$);
      node->set_strength(ASTLockMode::UPDATE);
      $$ = node;
    }
;

opt_lock_mode_clause {ASTLockMode*}:
    lock_mode_clause?
    {
      $$ = $lock_mode_clause.value_or(nullptr);
    }
;

having_modifier {ASTNode*}:
    "HAVING" ("MAX"[max] | "MIN") expression
    {
      auto* modifier = MakeNode<ASTHavingModifier>(@$, $expression);
      modifier->set_modifier_kind(
          @max.has_value() ? ASTHavingModifier::ModifierKind::MAX
                            : ASTHavingModifier::ModifierKind::MIN);
      $$ = modifier;
    }
;

clamped_between_modifier {ASTNode*}:
    "CLAMPED" "BETWEEN"
    expression_higher_prec_than_and[lower] "AND" expression[upper]
    %prec "BETWEEN"
    {
      $$ = MakeNode<ASTClampedBetweenModifier>(@$, $lower, $upper);
    }
;

with_report_modifier {ASTNode*}:
    "WITH" "REPORT" options_list[report_format]?
    {
      $$ = MakeNode<ASTWithReportModifier>(@$, $report_format);
    }
;

null_handling_modifier {ASTFunctionCall::NullHandlingModifier}:
    (("IGNORE"[ignore] | "RESPECT") "NULLS"[nulls])?
    {
      if (!@nulls.has_value()) {
        $$ = ASTFunctionCall::DEFAULT_NULL_HANDLING;
      } else if (@ignore.has_value()) {
        $$ = ASTFunctionCall::IGNORE_NULLS;
      } else {
        $$ = ASTFunctionCall::RESPECT_NULLS;
      }
    }
;

possibly_unbounded_int_literal_or_parameter {ASTNode*}:
    int_literal_or_parameter
    {
      $$ = MakeNode<ASTIntOrUnbounded>(@$, $1);
    }
  | "UNBOUNDED"
    {
      $$ = MakeNode<ASTIntOrUnbounded>(@$);
    }
;

recursion_depth_modifier {ASTNode*}:
    "WITH" "DEPTH" as_alias_with_required_as[alias]?
    {
      auto empty_location = LocationFromOffset(@$.end());

      // By default, they're unbounded when unspecified.
      auto* lower_bound = MakeNode<ASTIntOrUnbounded>(empty_location);
      auto* upper_bound = MakeNode<ASTIntOrUnbounded>(empty_location);
      $$ = MakeNode<ASTRecursionDepthModifier>(@$,
                      $alias, lower_bound, upper_bound);
    }
    // Bison uses the prec of the last terminal (which is "AND" in this case),
      // so we need to explicitly set to %prec "BETWEEN"
      // TODO: Clean up BETWEEN ... AND ... syntax once
      // we move to TextMapper.
  | "WITH" "DEPTH" as_alias_with_required_as[alias]?
      "BETWEEN" possibly_unbounded_int_literal_or_parameter[lower_bound]
      "AND" possibly_unbounded_int_literal_or_parameter[upper_bound]
      %prec "BETWEEN"
    {
      $$ = MakeNode<ASTRecursionDepthModifier>(@$,
                       $alias, $lower_bound, $upper_bound);
    }
  | "WITH" "DEPTH"[d] as_alias_with_required_as[alias]?
      "MAX" possibly_unbounded_int_literal_or_parameter[upper_bound]
    {
      auto empty_location = LocationFromOffset(@alias.has_value()
                                                   ? @alias.value().end()
                                                   : @d.end());

      // Lower bound is unspecified in this case.
      auto* lower_bound = MakeNode<ASTIntOrUnbounded>(empty_location);
      $$ = MakeNode<ASTRecursionDepthModifier>(@$,
                      $alias, lower_bound, $upper_bound);
    }
;

opt_recursion_depth_modifier {ASTNode*}:
    recursion_depth_modifier?
    {
      $$ = $recursion_depth_modifier.value_or(nullptr);
    }
;

// Represents an aliased query. The lookback of the next token is overridden
// to allow the lookahead transformer to find the end of an aliased query
// (potentially with modifiers). One use case is to support TABLE statements
// after CTE, for example:
//
// WITH RECURSIVE t AS (
//   ...
// ) WITH DEPTH
// TABLE t
aliased_query_with_overridden_next_token_lookback {ASTNode*}:
    "("[lparen] query
    {
      OVERRIDE_NEXT_TOKEN_LOOKBACK(RPAREN, LB_CLOSE_ALIASED_QUERY);
    }
    ")"[rparen]
    {
      // Fix the location of the subquery to include the parentheses.
      $$ = WithLocation($query, @$);
    }
;

aliased_query {ASTAliasedQuery*}:
    identifier "AS" aliased_query_with_overridden_next_token_lookback[query]
    recursion_depth_modifier[modifiers]?
    {
      $$ = MakeNode<ASTAliasedQuery>(
              @$, $identifier, $query, @modifiers.has_value()
                      ? MakeNode<ASTAliasedQueryModifiers>(
                                 @modifiers.value(), $modifiers)
                      : nullptr);
    }
;

// An identifier for the name of a WITH function, such as `my_fn` in
// `WITH my_fn() AS GROUP ROWS`.
with_function_name_identifier {ASTIdentifier*}:
    token_identifier
  |  set(WithFunctionNameNonReserved)[kw]
    {
      GOOGLESQL_ASSIGN_OR_RETURN($$, MakeIdentifierFromKeyword(@kw, $kw));
    }
;

with_clause_entry {ASTWithClauseEntry*}:
  aliased_query
  {
    $$ = MakeNode<ASTWithClauseEntry>(@$, $aliased_query);
  }
| with_function_name_identifier[name] "(" ")" "AS" "GROUP" "ROWS"
  {
    auto aliased_group_rows = MakeNode<ASTAliasedGroupRows>(@$, $name);
    $$ = MakeNode<ASTWithClauseEntry>(@$, aliased_group_rows);
    $$ = WithEndLocation($$, @$);
  }
;

with_clause {ASTWithClause*}:
    "WITH" .PossibleWithTimestamp "RECURSIVE"[recursive]? with_clause_entry
    {
      $$ = MakeNode<ASTWithClause>(@$, $with_clause_entry);
      $$->set_recursive(@recursive.has_value());
    }
  | with_clause "," with_clause_entry
    {
      $$ = ExtendNodeRight($with_clause, $with_clause_entry);
    }
;

opt_with_clause {ASTNode*}:
    with_clause?
    {
      $$ = $with_clause.value_or(nullptr);
    }
;

opt_with_connection_clause {ASTWithConnectionClause*}:
    with_connection_clause?
    {
      $$ = $with_connection_clause.value_or(nullptr);
    }
;

with_clause_with_trailing_comma {ASTNode*}:
    with_clause ","
    {
      $$ = WithEndLocation($1, @$);
    }
;

asc_or_desc {ASTOrderingExpression::OrderingSpec}:
    "ASC"
    {
      $$ = ASTOrderingExpression::ASC;
    }
  | "DESC"
    {
      $$ = ASTOrderingExpression::DESC;
    }
;

opt_asc_or_desc {ASTOrderingExpression::OrderingSpec}:
    asc_or_desc?
    {
      $$ = $asc_or_desc.value_or(ASTOrderingExpression::UNSPECIFIED);
    }
;

null_order {ASTNode*}:
    "NULLS" "FIRST"
    {
      auto* null_order = MakeNode<ASTNullOrder>(@$);
      null_order->set_nulls_first(true);
      $$ = null_order;
    }
  | "NULLS" "LAST"
    {
      auto* null_order = MakeNode<ASTNullOrder>(@$);
      null_order->set_nulls_first(false);
      $$ = null_order;
    }
;

opt_null_order {ASTNode*}:
    null_order?
    {
      $$ = $null_order.value_or(nullptr);
    }
;

string_literal_or_parameter {ASTExpression*}:
    string_literal
  | parameter_expression
  | system_variable_expression
;

collate_clause {ASTNode*}:
    "COLLATE" string_literal_or_parameter
    {
      $$ = MakeNode<ASTCollate>(@$, $2);
    }
;

opt_collate_clause {ASTNode*}:
    collate_clause?
    {
      $$ = $collate_clause.value_or(nullptr);
    }
;

default_collate_clause {ASTNode*}:
    "DEFAULT" collate_clause
    {
      $$ = $collate_clause;
    }
;

opt_default_collate_clause {ASTNode*}:
    default_collate_clause?
    {
      $$ = $default_collate_clause.value_or(nullptr);
    }
;

ordering_expression {ASTNode*}:
    expression collate_clause? opt_asc_or_desc null_order?
    {
      auto* ordering_expr = MakeNode<ASTOrderingExpression>(@$, $1, $2, $4);
      ordering_expr->set_ordering_spec($3);
      $$ = ordering_expr;
    }
;

order_by_clause_prefix {ASTNode*}:
    "ORDER" hint? "BY" ordering_expression
    {
      $$ = MakeNode<ASTOrderBy>(@$, $hint, $4);
    }
  | order_by_clause_prefix "," ordering_expression
    {
      $$ = ExtendNodeRight($1, $3);
    }
;

order_by_clause {ASTNode*}:
    order_by_clause_prefix
    {
      $$ = WithEndLocation($1, @$);
    }
;

order_by_clause_with_opt_comma {ASTNode*}:
    order_by_clause_prefix ","?
    {
      $$ = WithEndLocation($1, @$);
    }
;

opt_order_by_clause {ASTNode*}:
    order_by_clause?
    {
      $$ = $order_by_clause.value_or(nullptr);
    }
;

parenthesized_in_rhs {ASTNode*}:
    parenthesized_query[query]
  | "(" expression_maybe_parenthesized_not_a_query[e] ")"
    {
      // `expression_maybe_parenthesized_not_a_query` is NOT a query.
        $$ = MakeNode<ASTInList>(@e, $e);
    }
  | in_list_two_or_more_prefix ")"
    {
      // Don't include the ")" in the location, to match the JavaCC parser.
      // TODO: Fix that.
      $$ = WithEndLocation($1, @1);
    }
;

parenthesized_anysomeall_list_rhs {ASTNode*}:
    // This rule defines the right-hand side of a quantified comparison
    // expression, when it is enclosed in parentheses,
    // e.g., x LIKE ANY (...), x = ALL (...), x <= SOME (...)
    //
    // The possible forms are:
    // 1. A subquery, e.g., LIKE ANY (SELECT c from t).
    // 2. A single expression, e.g., LIKE ANY ('a').
    // 3. A list of multiple expressions, e.g., LIKE ANY ('a', select('b'), 'c').
    // 4. An extra-parenthesized subquery, e.g., LIKE ANY ((SELECT c from t)).
    //    This is treated as a single scalar expression.

    // Handles a subquery (1) or an extra-parenthesized subquery (4).
    parenthesized_query[query]
      {
        if ($query->parenthesized()) {
          // Case 4: An extra-parenthesized subquery. This is treated as a
          // single expression containing a subquery.
          $query->set_parenthesized(false);
          auto* sub_query = MakeNode<ASTExpressionSubquery>(@query, $query);
          $$ = MakeNode<ASTInList>(@$, sub_query);
        } else {
          // Case 1: A subquery.
          // Note: $query->parenthesized() is false here even though it came
          // from parenthesized_query. The parenthesized_query rule does not
          // consider the outermost parens into the query's is_parenthesized.
          $$ = $query->GetAsOrDie<ASTQuery>();
        }
      }
    // Case 2. Single expression
    | "(" expression_maybe_parenthesized_not_a_query[e] ")"
      {
        // `expression_maybe_parenthesized_not_a_query` is NOT a query.
        $$ = MakeNode<ASTInList>(@e, $e);
      }
    // Case 3. Multiple expressions
    | in_list_two_or_more_prefix ")"
      {
        // Don't include the ")" in the location, to match the JavaCC parser.
        // TODO: Fix that.
        $$ = WithEndLocation($1, @1);
      }
    ;

in_list_two_or_more_prefix {ASTNode*}:
    "(" expression "," expression
    {
      // The JavaCC parser doesn't include the opening "(" in the location
      // for some reason. TODO: Correct this after JavaCC is gone.
      $$ = MakeNode<ASTInList>(MakeLocationRange(@2, @4), $2, $4);
    }
  | in_list_two_or_more_prefix "," expression
    {
      $$ = ExtendNodeRight($1, $3);
    }
;

expression_with_opt_alias {ASTNode*}:
    expression as_alias_with_required_as[opt_alias]?
    {
      $$ = MakeNode<ASTExpressionWithOptAlias>(@$, $expression, $opt_alias);
    }
;

expression_with_alias {ASTExpressionWithAlias*}:
    expression[expr] as_alias[alias]
    {
      $$ = MakeNode<ASTExpressionWithAlias>(@$, $expr, $alias);
    }
  | expression
    {
      return MakeSyntaxError(
          ParseLocationRange(@1.end(), @1.end()),
          "Syntax error: Expected alias");
    }
;

unnest_expression {ASTNode*}:
    "UNNEST" "(" (expression_with_opt_alias separator ",")+[exprs] ("," named_argument[array_zip_mode])? ")"
    {
      $$ = MakeNode<ASTUnnestExpression>(@$, $exprs, $array_zip_mode);
    }
  | "UNNEST" "(" "SELECT"
    {
      return MakeSyntaxError(
          @3,
          "The argument to UNNEST is an expression, not a query; to use a query "
          "as an expression, the query must be wrapped with additional "
          "parentheses to make it a scalar subquery expression");
    }
;

unnest_expression_with_opt_alias_and_offset {ASTNode*}:
    unnest_expression as_alias? with_offset_and_alias?
    {
      $$ = MakeNode<ASTUnnestExpressionWithOptAliasAndOffset>(@$, $1, $2, $3);
    }
;

// This rule returns the JavaCC operator id for the operator.
comparative_operator {ASTBinaryExpression::Op}:
    "="
    {
      $$ = ASTBinaryExpression::EQ;
    }
  | "!="
    {
      $$ = ASTBinaryExpression::NE;
    }
  | "<>"
    {
      $$ = ASTBinaryExpression::NE2;
    }
  | "<"
    {
      $$ = ASTBinaryExpression::LT;
    }
  | "<="
    {
      $$ = ASTBinaryExpression::LE;
    }
  | ">"
    {
      $$ = ASTBinaryExpression::GT;
    }
  | ">="
    {
      $$ = ASTBinaryExpression::GE;
    }
;

additive_operator {ASTBinaryExpression::Op}:
    "+"
    {
      $$ = ASTBinaryExpression::PLUS;
    }
  | "-"
    {
      $$ = ASTBinaryExpression::MINUS;
    }
;

multiplicative_operator {ASTBinaryExpression::Op}:
    "*"
    {
      $$ = ASTBinaryExpression::MULTIPLY;
    }
  | "/"
    {
      $$ = ASTBinaryExpression::DIVIDE;
    }
;

// Returns ShiftOperator to indicate the operator type.
shift_operator {ShiftOperator}:
    "<<"
    {
      $$ = ShiftOperator::kLeft;
    }
  | ">>"
    {
      $$ = ShiftOperator::kRight;
    }
;

// Returns ImportType to indicate the import object type.
import_type {ImportType}:
    "MODULE"
    {
      $$ = ImportType::kModule;
    }
  | "PROTO"
    {
      $$ = ImportType::kProto;
    }
;

// This returns an AnySomeAllOp to indicate what keyword was present.
any_some_all {ASTNode*}:
    "ANY"
    {
      auto* op = MakeNode<ASTAnySomeAllOp>(@$);
      op->set_op(ASTAnySomeAllOp::kAny);
      $$ = op;
    }
  | "SOME"
    {
      auto* op = MakeNode<ASTAnySomeAllOp>(@$);
      op->set_op(ASTAnySomeAllOp::kSome);
      $$ = op;
    }
  | "ALL"
    {
      auto* op = MakeNode<ASTAnySomeAllOp>(@$);
      op->set_op(ASTAnySomeAllOp::kAll);
      $$ = op;
    }
;

// Returns NotKeywordPresence to indicate whether NOT was present.
like_operator {NotKeywordPresence}:
    "LIKE"
    {
      $$ = NotKeywordPresence::kAbsent;
    }
    %prec "LIKE"
  | "NOT_SPECIAL" "LIKE"
    {
      @$ = @2;  // Error messages should point at the "LIKE".
      $$ = NotKeywordPresence::kPresent;
    }
    %prec "LIKE"
;

// Returns NotKeywordPresence to indicate whether NOT was present.
between_operator {NotKeywordPresence}:
    "BETWEEN"
    {
      $$ = NotKeywordPresence::kAbsent;
    }
    %prec "BETWEEN"
  | "NOT_SPECIAL" "BETWEEN"
    {
      @$ = @2;  // Error messages should point at the "BETWEEN".
        $$ = NotKeywordPresence::kPresent;
    }
    %prec "BETWEEN"
;

distinct_operator {NotKeywordPresence}:
    "IS" "DISTINCT" "FROM"
    {
      $$ = NotKeywordPresence::kAbsent;
    }
    %prec "DISTINCT"
  | "IS" "NOT_SPECIAL" "DISTINCT" "FROM"
    {
      @$ = @3;  // Error messages should point at the "DISTINCT".
        $$ = NotKeywordPresence::kPresent;
    }
    %prec "DISTINCT"
;

// Returns NotKeywordPresence to indicate whether NOT was present.
in_operator {NotKeywordPresence}:
    "IN"
    {
      $$ = NotKeywordPresence::kAbsent;
    }
    %prec "IN"
  | "NOT_SPECIAL" "IN"
    {
      @$ = @2;  // Error messages should point at the "IN".
        $$ = NotKeywordPresence::kPresent;
    }
    %prec "IN"
;

// Returns NotKeywordPresence to indicate whether NOT was present.
is_operator {NotKeywordPresence}:
    "IS"
    {
      $$ = NotKeywordPresence::kAbsent;
    }
    %prec "IS"
  | "IS" "NOT"
    {
      $$ = NotKeywordPresence::kPresent;
    }
    %prec "IS"
;

unary_operator {ASTUnaryExpression::Op}:
    "+"
    {
      $$ = ASTUnaryExpression::PLUS;
    }
    %prec UNARY_PRECEDENCE
  | "-"
    {
      $$ = ASTUnaryExpression::MINUS;
    }
    %prec UNARY_PRECEDENCE
  | "~"
    {
      $$ = ASTUnaryExpression::BITWISE_NOT;
    }
    %prec UNARY_PRECEDENCE
;

with_expression_variable {ASTNode*}:
    identifier "AS" expression
    {
      auto* alias = MakeNode<ASTAlias>(MakeLocationRange(@1, @2), $1);
      $$ = MakeNode<ASTSelectColumn>(@$, $3, alias);
    }
;

with_expression_variable_prefix {ASTNode*}:
    with_expression_variable
    {
      $$ = MakeNode<ASTSelectList>(@$, $1);
    }
  | with_expression_variable_prefix "," with_expression_variable
    {
      $$ = ExtendNodeRight($1, $3);
    }
;

with_expression {ASTExpression*}:
    KW_WITH_STARTING_WITH_EXPRESSION "(" with_expression_variable_prefix "," expression ")"
    {
      $$ = MakeNode<ASTWithExpression>(@$, $3, $5);
    }
;

// This top level rule is designed to make it possible to use one token of
// lookahead to decide whether to turn a parenthesized query into an
// expression.
// The lookahead is used to disambiguate between
// - the expression case like ((SELECT 1)) + 1
// - and the set operation query case like ((SELECT 1)) UNION ALL (SELECT 2).
expression {ASTExpression*}:
    expression_higher_prec_than_and
  | and_expression %prec "AND"
  | or_expression %prec "OR"
;

or_expression {ASTExpression*}:
    expression[lhs] "OR" expression[rhs] %prec "OR"
    {
      if ($lhs->node_kind() == AST_OR_EXPR && !$lhs->parenthesized()) {
        // $rhs will not includes its own parenthesis in its location, so we
        // need to override the range in this case.
        $$ = ExtendNodeRight($lhs, @$.end(), $rhs);
      } else {
        $$ = MakeNode<ASTOrExpr>(@$, $lhs, $rhs);
      }
    }
;

and_expression {ASTExpression*}:
    and_expression[lhs] "AND" expression_higher_prec_than_and[rhs] %prec "AND"
    {
      // $rhs will not includes its own parenthesis in its location, so we
      // need to override the range in this case.
      $$ = ExtendNodeRight($lhs, @$.end(), $rhs);
    }
  | expression_higher_prec_than_and[lhs] "AND" expression_higher_prec_than_and[rhs] %prec "AND"
    {
      $$ = MakeNode<ASTAndExpr>(@$, $lhs, $rhs);
    }
;

// Any expression that has a higher precedence than AND. Anything here can
// directly go as a lower bound for a BETWEEN expression (i.e., without needing
// parens), or a parenthesized expression of any form. Anything matching this
// rule can thus serve as a lower bound for BETWEEN.
expression_higher_prec_than_and {ASTExpression*}:
    unparenthesized_expression_higher_prec_than_and
  | parenthesized_expression_not_a_query
  | parenthesized_query[query]
    {
      // As the query ASTExpressionSubquery already has parentheses, set this
      // flag to false to avoid a double nesting like SELECT ((SELECT 1)).
      $query->set_parenthesized(false);
      $$ = MakeNode<ASTExpressionSubquery>(@query, $query);
    }
;

expression_maybe_parenthesized_not_a_query {ASTExpression*}:
    parenthesized_expression_not_a_query
  | unparenthesized_expression_higher_prec_than_and
  | and_expression
  | or_expression
;

// Don't include the location in the parentheses. Semantic error messages about
// this expression should point at the start of the expression, not at the
// opening parentheses.
parenthesized_expression_not_a_query {ASTExpression*}:
    "(" expression_maybe_parenthesized_not_a_query[e] ")"
    {
      $e->set_parenthesized(true);
      $$ = $e;
    }
;


// The most restrictive rule: expression that is not parenthesized, and is not
// `e1 AND e2`. Anything in this rule can directly be a lower bound for a
// BETWEEN expression, without any parens.
unparenthesized_expression_higher_prec_than_and {ASTExpression*}:
    null_literal
  | boolean_literal
  | string_literal
  | bytes_literal
  | integer_literal
  | numeric_literal
  | bignumeric_literal
  | json_literal
  | floating_point_literal
  | date_or_time_literal
  | range_literal
  | parameter_expression
  | system_variable_expression
  | array_constructor
  | new_constructor
  | braced_constructor[ctor]
  | braced_new_constructor
  | struct_braced_constructor
  | map_braced_constructor
  | case_expression
  | cast_expression
  | extract_expression
  | with_expression
  | replace_fields_expression
  | function_call_expression_with_clauses
  | interval_expression
  | identifier[id]
    {
      // The path expression is extended by the "." identifier rule below.
      $$ = MakeNode<ASTPathExpression>(@$, $1);

      // This could be a bare reference to a CURRENT_* date/time function.
      // Those functions can be called without arguments, but they should
      // still be parsed as function calls. We only parse them as such when
      // the identifiers are not backquoted, i.e., when they are used as
      // keywords. The backquoted versions are treated like regular
      // identifiers.
      absl::string_view name = $id->GetAsStringView();
      // Quick check to filter out certain non-matches.
      if (!$id->is_quoted() && googlesql_base::CaseEqual(name.substr(0, 8), "current_")) {
        absl::string_view remainder = name.substr(8);
        if (googlesql_base::CaseEqual(remainder, "time") ||
            googlesql_base::CaseEqual(remainder, "date") ||
            googlesql_base::CaseEqual(remainder, "datetime") ||
            googlesql_base::CaseEqual(remainder, "timestamp")) {
          auto* function_call = MakeNode<ASTFunctionCall>(@$, $$);
          function_call->set_is_current_date_time_without_parentheses(true);
          $$ = function_call;
        }
      }
    }
  | struct_constructor
  | expression_subquery_with_keyword
  | expression_higher_prec_than_and "[" expression "]" %prec PRIMARY_PRECEDENCE
    {
      auto* bracket_loc = MakeNode<ASTLocation>(@2);
      $$ = MakeNode<ASTArrayElement>(@$, $1, bracket_loc, $3);
    }
  | expression_higher_prec_than_and "." "(" path_expression ")" %prec PRIMARY_PRECEDENCE
    {
      $$ = MakeNode<ASTDotGeneralizedField>(@$, $1, $4);
    }
    // This rule accepts `identifier_or_function_name_from_keyword` so
    // chained function calls of reserved names like LEFT work.
    // This also makes dot-identifier with those names parsable.
    // e.g. `("abc").LEFT(3)` or `"abc".LEFT`.
    // `path_expression` rules already accept these, like `LEFT("abc", 3)`.
  | expression_higher_prec_than_and "."
      identifier_or_function_name_from_keyword %prec PRIMARY_PRECEDENCE
    {
      // Note that if "expression" ends with an identifier, then the tokenizer
      // switches to IDENTIFIER_DOT mode before tokenizing $3. That means that
      // "identifier" here allows any non-reserved keyword to be used as an
      // identifier, as well as "identifiers" that start with a digit.

      // We try to build path expressions as long as identifiers are added.
      // As soon as a dotted path contains anything else, we use generalized
      // DotIdentifier.
      if ($1->node_kind() == AST_PATH_EXPRESSION &&
          !$1->parenthesized()) {
        $$ = ExtendNodeRight($1, $3);
      } else {
        $$ = MakeNode<ASTDotIdentifier>(@$, $1, $3);
      }
    }
  | expression_higher_prec_than_and[expr] braced_constructor[ctor] %prec PRIMARY_PRECEDENCE
    {
      if ($expr->node_kind() == AST_FUNCTION_CALL &&
          !$expr->GetAs<ASTFunctionCall>()->parenthesized()) {
        const ASTNode* fn_name = $expr->GetAs<ASTFunctionCall>()->child(0);
        if (fn_name->node_kind() == AST_PATH_EXPRESSION &&
            fn_name->num_children() == 1 &&
            googlesql_base::CaseEqual(
                fn_name->child(0)->GetAs<ASTIdentifier>()->GetAsStringView(),
                "UPDATE") &&  !
                fn_name->child(0)->GetAs<ASTIdentifier>()->is_quoted()) {
          $$ = MakeNode<ASTUpdateConstructor>(@$, $expr, $ctor);
          return absl::OkStatus();
        }
      }
      $$ = MakeNode<ASTBracedConstructorExtendedExpr>(@$, $expr, $ctor);
    }
  | "NOT" expression_higher_prec_than_and %prec UNARY_NOT_PRECEDENCE
    {
      auto* not_expr = MakeNode<ASTUnaryExpression>(@$, $2);
      not_expr->set_op(ASTUnaryExpression::NOT);
      $$ = not_expr;
    }
  | expression_higher_prec_than_and[lhs] like_operator any_some_all
    hint? unnest_expression %prec "LIKE"
    {
      // Explicitly enforce non-associativity. The grammar allows syntax such as
      // "x IN (a,b) LIKE ANY (c,d)" but the language does not.
      if (!$lhs->IsAllowedInComparison()) {
        return MakeSyntaxError(@like_operator,
            "Syntax error: Expression to the left of LIKE "
            "must be parenthesized");
      }
      if ($hint.has_value()) {
        return MakeSyntaxError(@hint,
            "Syntax error: HINTs cannot be specified on "
            "LIKE clause with UNNEST");
      }
      ASTNode* like_location = MakeNode<ASTLocation>(@like_operator);
      auto* like_expression = MakeNode<ASTLikeExpression>(@$,
          $lhs, like_location, $any_some_all, $unnest_expression);
      like_expression->set_is_not(
          $like_operator == NotKeywordPresence::kPresent);
      $$ = like_expression;
    }
  | expression_higher_prec_than_and[lhs] comparative_operator any_some_all
    hint? unnest_expression %prec "="
    {
      // Explicitly enforce non-associativity. The grammar allows syntax such as
      // "x IN (a,b) = ANY (c,d)" but the language does not.
      if (!$lhs->IsAllowedInComparison()) {
        return MakeSyntaxError(@comparative_operator,
            "Syntax error: Expression to the left of the comparison operator "
            "must be parenthesized");
      }
      if ($hint.has_value()) {
        return MakeSyntaxError(@hint,
                              "Syntax error: HINTs cannot be specified on "
                              "ANY/SOME/ALL clause with UNNEST");
      }
      ASTNode* op_location = MakeNode<ASTLocation>(@comparative_operator);
      auto* expression = MakeNode<ASTQuantifiedComparisonExpression>(@$,
          $lhs, op_location, $any_some_all, $unnest_expression);
      expression->set_op($comparative_operator);
      $$ = expression;
    }
  | expression_higher_prec_than_and[lhs] like_operator any_some_all
    hint? parenthesized_anysomeall_list_rhs[rhs] %prec "LIKE"
    {
      // Explicitly enforce non-associativity. The grammar allows syntax such as
      // "x = ANY (a,b) LIKE ANY (c,d)" but the language does not.
      if (!$lhs->IsAllowedInComparison()) {
        return MakeSyntaxError(@like_operator,
            "Syntax error: Expression to the left of LIKE "
            "must be parenthesized");
      }
      // Hints can only be on queries.
      if ($hint.has_value() && $rhs->node_kind() != AST_QUERY) {
        return MakeSyntaxError(@hint,
            "Syntax error: HINTs cannot be specified on "
            "LIKE clause with value list");
      }
      ASTNode* like_location = MakeNode<ASTLocation>(@like_operator);
      ASTLikeExpression* like_expression = nullptr;
      if ($rhs->node_kind() == AST_QUERY) {
        like_expression = MakeNode<ASTLikeExpression>(@$,
            $lhs, like_location, $any_some_all, $hint, $rhs);
      } else {
        like_expression = MakeNode<ASTLikeExpression>(@$,
            $lhs, like_location, $any_some_all, $rhs);
      }
      like_expression->set_is_not(
          $like_operator == NotKeywordPresence::kPresent);
      $$ = like_expression;
    }
  | expression_higher_prec_than_and[lhs] comparative_operator any_some_all
    hint? parenthesized_anysomeall_list_rhs[rhs] %prec "="
    {
      // Explicitly enforce non-associativity. The grammar allows syntax such as
      // "x LIKE ANY (a,b) = ANY (c,d)" but the language does not.
      if (!$lhs->IsAllowedInComparison()) {
        return MakeSyntaxError(@comparative_operator,
            "Syntax error: Expression to the left of the comparison operator "
            "must be parenthesized");
      }
      // Hints can only be on queries.
      if ($hint.has_value() && $rhs->node_kind() != AST_QUERY) {
        return MakeSyntaxError(@hint,
            "Syntax error: HINTs cannot be specified on "
            "ANY/SOME/ALL clause with value list");
      }
      ASTNode* op_location = MakeNode<ASTLocation>(@comparative_operator);
      ASTQuantifiedComparisonExpression* expression = nullptr;
      if ($rhs->node_kind() == AST_QUERY) {
        expression = MakeNode<ASTQuantifiedComparisonExpression>(@$,
            $lhs, op_location, $any_some_all, $hint, $rhs);
      } else {
        expression = MakeNode<ASTQuantifiedComparisonExpression>(@$,
            $lhs, op_location, $any_some_all, $rhs);
      }
      expression->set_op($comparative_operator);
      $$ = expression;
    }
  | expression_higher_prec_than_and like_operator expression_higher_prec_than_and %prec "LIKE"
    {
      GOOGLESQL_RETURN_IF_ERROR(ErrorIfUnparenthesizedNotExpression($3));
      // Bison allows some cases like IN on the left hand side because it's
      // not ambiguous. The language doesn't allow this.
      if (!$1->IsAllowedInComparison()) {
        return MakeSyntaxError(
            @2,
            "Syntax error: "
            "Expression to the left of LIKE must be parenthesized");
      }
      auto* binary_expression =
          MakeNode<ASTBinaryExpression>(@$, $1, $3);
      binary_expression->set_is_not($2 == NotKeywordPresence::kPresent);
      binary_expression->set_op(ASTBinaryExpression::LIKE);
      $$ = binary_expression;
    }
  | expression_higher_prec_than_and distinct_operator expression_higher_prec_than_and %prec "DISTINCT"
    {
      if (!language_options.LanguageFeatureEnabled(FEATURE_IS_DISTINCT)) {
        return MakeSyntaxError(@2, "IS DISTINCT FROM is not supported");
      }
      auto binary_expression =
          MakeNode<ASTBinaryExpression>(@$, $1, $3);
          binary_expression->set_is_not($2 == NotKeywordPresence::kPresent);
          binary_expression->set_op(ASTBinaryExpression::DISTINCT);
      $$ = binary_expression;
    }
  | expression_higher_prec_than_and in_operator hint? unnest_expression %prec "IN"
    {
      if ($hint.has_value()) {
        return MakeSyntaxError(@hint,
                              "Syntax error: HINTs cannot be specified on "
                              "IN clause with UNNEST");
      }
      // Bison allows some cases like IN on the left hand side because it's
      // not ambiguous. The language doesn't allow this.
      if (!$1->IsAllowedInComparison()) {
        return MakeSyntaxError(@2,
                              "Syntax error: Expression to the left of IN "
                              "must be parenthesized");
      }
      ASTLocation* in_location = MakeNode<ASTLocation>(@2);
      auto* in_expression =
          MakeNode<ASTInExpression>(@$, $1, in_location, $4);
      in_expression->set_is_not($2 == NotKeywordPresence::kPresent);
      $$ = in_expression;
    }
  | expression_higher_prec_than_and in_operator hint? parenthesized_in_rhs %prec "IN"
    {
      // Bison allows some cases like IN on the left hand side because it's
      // not ambiguous. The language doesn't allow this.
      if (!$1->IsAllowedInComparison()) {
        return MakeSyntaxError(@2,
                            "Syntax error: Expression to the left of IN "
                            "must be parenthesized");
      }
      ASTInExpression* in_expression = nullptr;
      ASTLocation* in_location = MakeNode<ASTLocation>(@2);
      if ($4->node_kind() == AST_QUERY) {
        in_expression =
            MakeNode<ASTInExpression>(@$, $1, in_location, $3, $4);
      } else {
        if ($hint.has_value()) {
          return MakeSyntaxError(@hint,
                              "Syntax error: HINTs cannot be specified on "
                              "IN clause with value list");
        }
        in_expression =
            MakeNode<ASTInExpression>(@$, $1, in_location, $4);
      }
      in_expression->set_is_not($2 == NotKeywordPresence::kPresent);
      $$ = in_expression;
    }
    // Bison uses the prec of the last terminal (which is "AND" in this case),
    // so we need to explicitly set to %prec "BETWEEN"
  | expression_higher_prec_than_and between_operator
      expression_higher_prec_than_and "AND"
      expression_higher_prec_than_and %prec "BETWEEN"
    {
      // Bison allows some cases like IN on the left hand side because it's
      // not ambiguous. The language doesn't allow this.
      if (!$1->IsAllowedInComparison()) {
        return MakeSyntaxError(@2,
                              "Syntax error: Expression to the left of "
                              "BETWEEN must be parenthesized");
      }
      // Test the middle operand for unparenthesized operators with lower
      // or equal precedence. These cases are unambiguous w.r.t. the
      // operator precedence parsing, but they are disallowed by the SQL
      // standard because it interprets precedence strictly, i.e., it allows
      // no nesting of operators with lower precedence even if it is
      // unambiguous.
      if (!$3->IsAllowedInComparison()) {
        return MakeSyntaxError(@3,
                              "Syntax error: Expression in BETWEEN must be "
                              "parenthesized");
      }
      GOOGLESQL_RETURN_IF_ERROR(ErrorIfUnparenthesizedNotExpression($5));
      auto* between_loc = MakeNode<ASTLocation>(@2);
      auto* between_expression =
          MakeNode<ASTBetweenExpression>(@$, $1, between_loc, $3, $5);
      between_expression->set_is_not($2 == NotKeywordPresence::kPresent);
      $$ = between_expression;
    }
  | expression_higher_prec_than_and between_operator
      expression_higher_prec_than_and[lower] "OR" %prec "BETWEEN"
    {
      // The "OR" operator has lower precedence than "AND". The rule
      // immediately above tries to catch all expressions with lower or
      // equal precedence to "AND" and block them from being the middle
      // argument of "BETWEEN". See the comment on the generic rule above
      // for justification of the error. We need this special rule to catch
      // "OR", and only "OR", because "OR" is the only operator with lower
      // precendence to "AND" that is factored out of
      // `expression_higher_prec_than_and` into its own rule.
      return MakeSyntaxError(@lower,
                            "Syntax error: Expression in BETWEEN must be "
                            "parenthesized");
    }
  | expression_higher_prec_than_and is_operator "UNKNOWN" %prec "IS"
    {
      // The Bison parser allows comparison expressions in the LHS, even
      // though these operators are at the same precedence level and are not
      // associative. Explicitly forbid this.
      if (!$1->IsAllowedInComparison()) {
        return MakeSyntaxError(@2,
                              "Syntax error: Expression to the left of IS "
                              "must be parenthesized");
      }
      auto* unary_expression = MakeNode<ASTUnaryExpression>(@$, $1);
      if ($2 == NotKeywordPresence::kPresent) {
        unary_expression->set_op(ASTUnaryExpression::IS_NOT_UNKNOWN);
      } else {
        unary_expression->set_op(ASTUnaryExpression::IS_UNKNOWN);
      }
      $$ = unary_expression;
    }
  | expression_higher_prec_than_and is_operator null_literal %prec "IS"
    {
      // The Bison parser allows comparison expressions in the LHS, even
      // though these operators are at the same precedence level and are not
      // associative. Explicitly forbid this.
      if (!$1->IsAllowedInComparison()) {
        return MakeSyntaxError(@2,
                              "Syntax error: Expression to the left of IS "
                              "must be parenthesized");
      }
      auto* binary_expression = MakeNode<ASTBinaryExpression>(@$, $1, $3);
      binary_expression->set_is_not($2 == NotKeywordPresence::kPresent);
      binary_expression->set_op(ASTBinaryExpression::IS);
      $$ = binary_expression;
    }
  | expression_higher_prec_than_and is_operator boolean_literal %prec "IS"
    {
      // The Bison parser allows comparison expressions in the LHS, even
      // though these operators are at the same precedence level and are not
      // associative. Explicitly forbid this.
      if (!$1->IsAllowedInComparison()) {
        return MakeSyntaxError(@2,
                              "Syntax error: Expression to the left of IS "
                              "must be parenthesized");
      }
      auto* binary_expression = MakeNode<ASTBinaryExpression>(@$, $1, $3);
      binary_expression->set_is_not($2 == NotKeywordPresence::kPresent);
      binary_expression->set_op(ASTBinaryExpression::IS);
      $$ = binary_expression;
    }
  | expression_higher_prec_than_and comparative_operator expression_higher_prec_than_and %prec "="
    {
      GOOGLESQL_RETURN_IF_ERROR(ErrorIfUnparenthesizedNotExpression($3));
      // Bison allows some cases like IN on the left hand side because it's
      // not ambiguous. The language doesn't allow this.
      if (!$1->IsAllowedInComparison()) {
        return MakeSyntaxError(@2,
                              "Syntax error: Expression to the left of "
                              "comparison must be parenthesized");
      }
      auto* binary_expression = MakeNode<ASTBinaryExpression>(@$, $1, $3);
      binary_expression->set_op($2);
      $$ = binary_expression;
    }
  | expression_higher_prec_than_and "|" expression_higher_prec_than_and
    {
      GOOGLESQL_RETURN_IF_ERROR(ErrorIfUnparenthesizedNotExpression($3));
      auto* binary_expression = MakeNode<ASTBinaryExpression>(@$, $1, $3);
      binary_expression->set_op(ASTBinaryExpression::BITWISE_OR);
      $$ = binary_expression;
    }
  | expression_higher_prec_than_and "^" expression_higher_prec_than_and
    {
      GOOGLESQL_RETURN_IF_ERROR(ErrorIfUnparenthesizedNotExpression($3));
      auto* binary_expression = MakeNode<ASTBinaryExpression>(@$, $1, $3);
      binary_expression->set_op(ASTBinaryExpression::BITWISE_XOR);
      $$ = binary_expression;
    }
  | expression_higher_prec_than_and "&" expression_higher_prec_than_and
    {
      GOOGLESQL_RETURN_IF_ERROR(ErrorIfUnparenthesizedNotExpression($3));
      auto* binary_expression = MakeNode<ASTBinaryExpression>(@$, $1, $3);
      binary_expression->set_op(ASTBinaryExpression::BITWISE_AND);
      $$ = binary_expression;
    }
  | expression_higher_prec_than_and[lhs] "||" expression_higher_prec_than_and[rhs] %prec "||"
    {
      GOOGLESQL_RETURN_IF_ERROR(ErrorIfUnparenthesizedNotExpression($rhs));
      if ($lhs->node_kind() == AST_CONCAT_EXPR && !$lhs->parenthesized()) {
        $$ = ExtendNodeRight($lhs, @$.end(), $rhs);
      } else {
        $$ = MakeNode<ASTConcatExpr>(@$, $lhs, $rhs);
      }
    }
  | expression_higher_prec_than_and shift_operator expression_higher_prec_than_and %prec "<<"
    {
      GOOGLESQL_RETURN_IF_ERROR(ErrorIfUnparenthesizedNotExpression($3));
      auto* operator_location = MakeNode<ASTLocation>(@2);
      auto* binary_expression =
          MakeNode<ASTBitwiseShiftExpression>(@$, $1, operator_location, $3);
      binary_expression->set_is_left_shift($2 == ShiftOperator::kLeft);
      $$ = binary_expression;
    }
  | expression_higher_prec_than_and additive_operator expression_higher_prec_than_and %prec "+"
    {
      GOOGLESQL_RETURN_IF_ERROR(ErrorIfUnparenthesizedNotExpression($3));
      auto* binary_expression = MakeNode<ASTBinaryExpression>(@$, $1, $3);
      binary_expression->set_op($2);
      $$ = binary_expression;
    }
  | expression_higher_prec_than_and multiplicative_operator expression_higher_prec_than_and %prec "*"
    {
      GOOGLESQL_RETURN_IF_ERROR(ErrorIfUnparenthesizedNotExpression($3));
      auto* binary_expression = MakeNode<ASTBinaryExpression>(@$, $1, $3);
      binary_expression->set_op($2);
      $$ = binary_expression;
    }
  | unary_operator expression_higher_prec_than_and %prec UNARY_PRECEDENCE
    {
      GOOGLESQL_RETURN_IF_ERROR(ErrorIfUnparenthesizedNotExpression($2));
      auto* expression = MakeNode<ASTUnaryExpression>(@$, $2);
      expression->set_op($1);
      $$ = expression;
    }
  | graph_expression
  | unpack_expression
;

// Note that the tokenizer will be in "DOT_IDENTIFIER" mode for all identifiers
// after the first dot. This allows path expressions like "foo.201601010" or
// "foo.all" to be written without backquoting, and we don't have to worry about
// this in the parser.
path_expression {ASTPathExpression*}:
    identifier
    {
      $$ = MakeNode<ASTPathExpression>(@$, $1);
    }
  | path_expression[path] "." identifier
    {
      $$ = ExtendNodeRight($path, $identifier);
    }
;

dashed_identifier {SeparatedIdentifierTmpNode*}:
    identifier[id1] "-" identifier[id2]
    {
      // a - b
      if (!@1.IsAdjacentlyFollowedBy(@2) || !@2.IsAdjacentlyFollowedBy(@3)) {
        return MakeSyntaxError(@2, "Syntax error: Unexpected \"-\"");
      }
      if ($id1->is_quoted() || $id2->is_quoted()) {
        return MakeSyntaxError(@2, "Syntax error: Unexpected \"-\"");
      }
      auto out = MakeNode<SeparatedIdentifierTmpNode>(@1);
      out->set_path_parts({{$id1->GetAsStringView(), "-", $id2->GetAsStringView()}});
      $$ = out;
    }
  | dashed_identifier[id1] "-" identifier[id2]
    {
      // a-b - c
      if (!@1.IsAdjacentlyFollowedBy(@2) || !@2.IsAdjacentlyFollowedBy(@3)) {
        return MakeSyntaxError(@2, "Syntax error: Unexpected \"-\"");
      }
      SeparatedIdentifierTmpNode::PathParts prev = $1->release_path_parts();
      if ($id2->is_quoted()) {
        return MakeSyntaxError(@2, "Syntax error: Unexpected \"-\"");
      }
      // Add an extra sub-part to the ending dashed identifier.
      prev.back().push_back("-");
      prev.back().push_back($id2->GetAsStringView());
      auto out = MakeNode<SeparatedIdentifierTmpNode>(@1);
      out->set_path_parts(std::move(prev));
      $$ = out;
    }
  | identifier[id1] "-" INTEGER_LITERAL[id2]
    {
      // a - 5
      if (!@1.IsAdjacentlyFollowedBy(@2) || !@2.IsAdjacentlyFollowedBy(@3)) {
        return MakeSyntaxError(@2, "Syntax error: Unexpected \"-\"");
      }
      if ($id1->is_quoted()) {
        return MakeSyntaxError(@2, "Syntax error: Unexpected \"-\"");
      }
      auto out = MakeNode<SeparatedIdentifierTmpNode>(@1);
      out->set_path_parts({{$id1->GetAsStringView(), "-", $id2}});
      $$ = out;
    }
  | dashed_identifier "-" INTEGER_LITERAL[id2]
    {
      // a-b - 5
      if (!@1.IsAdjacentlyFollowedBy(@2) || !@2.IsAdjacentlyFollowedBy(@3)) {
        return MakeSyntaxError(@2, "Syntax error: Unexpected \"-\"");
      }
      SeparatedIdentifierTmpNode::PathParts prev = $1->release_path_parts();
      prev.back().push_back("-");
      prev.back().push_back($id2);
      auto out = MakeNode<SeparatedIdentifierTmpNode>(@1);
      out->set_path_parts(std::move(prev));
      $$ = out;
    }
  | identifier[id1] "-" FLOATING_POINT_LITERAL[id2] identifier[id3]
    {
      // a - 1. b
      if (!@1.IsAdjacentlyFollowedBy(@2) || !@2.IsAdjacentlyFollowedBy(@3)) {
        return MakeSyntaxError(@2, "Syntax error: Unexpected \"-\"");
      }
      if ($id1->is_quoted()) {
        return MakeSyntaxError(@2, "Syntax error: Unexpected \"-\"");
      }
      auto out = MakeNode<SeparatedIdentifierTmpNode>(@1);
      // Here (and below) we need to handle the case where dot is lex'ed as
      // part of floating number as opposed to path delimiter. To parse it
      // correctly, we push the components separately (as string_view).
      // {{"a", "1"}, "b"}
      out->set_path_parts({{$id1->GetAsStringView(), "-", $id2}, {$id3->GetAsStringView()}});
      $$ = out;
    }
  | dashed_identifier "-" FLOATING_POINT_LITERAL[id1] identifier[id2]
    {
      // a-b - 1. c
      if (!@1.IsAdjacentlyFollowedBy(@2) || !@2.IsAdjacentlyFollowedBy(@3)) {
        return MakeSyntaxError(@2, "Syntax error: Unexpected \"-\"");
      }
      SeparatedIdentifierTmpNode::PathParts prev = $1->release_path_parts();
      // This case is a continuation of an existing dashed_identifier `prev`,
      // followed by what the lexer believes is a floating point literal.
      // here: /*prev=*/={{"a", "b"}}
      // we append "1" to complete the dashed components, followed
      // by the identifier ("c") as {{"c"}}.
      // Thus, we end up with {{"a", "b", "1"}, {"c"}}
      prev.back().push_back("-");
      prev.back().push_back($id1);
      prev.push_back({$id2->GetAsStringView()});
      auto out = MakeNode<SeparatedIdentifierTmpNode>(@1);
      out->set_path_parts(std::move(prev));
      $$ = out;
    }
;

dashed_path_expression {ASTExpression*}:
    dashed_identifier
    {
      absl::StatusOr<std::vector<ASTNode*>> path_parts =
          SeparatedIdentifierTmpNode::BuildPathParts(@1,
            std::move($1->release_path_parts()), node_factory);
      if (!path_parts.ok()) {
        return MakeSyntaxError(@1, std::string(path_parts.status().message()));
      }
      $$ = MakeNode<ASTPathExpression>(@$, *path_parts);
    }
  | dashed_path_expression[path] "." identifier
    {
      $$ = ExtendNodeRight($path, $identifier);
    }
;

maybe_dashed_path_expression {ASTExpression*}:
    path_expression
  | dashed_path_expression
    {
      if (language_options.LanguageFeatureEnabled(
               FEATURE_ALLOW_DASHES_IN_TABLE_NAME)) {
        $$ = $1;
      } else {
        absl::string_view target =
          $1->num_children() > 1
              ? "The dashed identifier part of the table name "
              : "It ";
        return MakeSyntaxError(
            @1,
            absl::StrCat(
                "Syntax error: Table name contains '-' character. ",
                target,
                "needs to be quoted: ",
                ToIdentifierLiteral(
                    $1->child(0)->GetAs<ASTIdentifier>()->GetAsStringView(),
                    false)));
      }
    }
;

maybe_slashed_or_dashed_path_expression {ASTExpression*}:
    maybe_dashed_path_expression
  | slashed_path_expression
    {
      if (language_options.LanguageFeatureEnabled(FEATURE_ALLOW_SLASH_PATHS)) {
        $$ = $1;
      } else {
        absl::string_view target =
            $1->num_children() > 1
                ? "The slashed identifier part of the table name "
                : "It ";
        return MakeSyntaxError(
            @1,
            absl::StrCat(
              "Syntax error: Table name contains '/' character. ",
              target,
              "needs to be quoted: ",
              ToIdentifierLiteral(
                $1->child(0)->GetAs<ASTIdentifier>()->GetAsStringView(),
                false)));
      }
    }
;

slashed_identifier_separator {absl::string_view}:
    "-"
  | "/"
  | ":"
;

// Identifier or integer. SCRIPT_LABEL is also included so that a ":" in a path
// followed by begin/while/loop/repeat/for doesn't trigger the
// script label grammar.
identifier_or_integer {absl::string_view}:
    identifier[id]
    {
      // We have to use the `string_view` member because the other production
      // rules are mere tokens, not identifiers.
      absl::string_view id = $id->GetAsStringView();
      if ($id->is_quoted()) {
        id = node_factory.id_string_pool()
                   ->Make(absl::StrCat("`", id, "`")).ToStringView();
      }
      $$ = id;
    }
  | INTEGER_LITERAL
  | SCRIPT_LABEL
;

// An identifier that starts with a "/" and can contain non-adjacent /:-
// separators.
slashed_identifier {SeparatedIdentifierTmpNode*}:
    "/" identifier_or_integer[id]
    {
      // Return an error if there is embedded whitespace.
      if (!@1.IsAdjacentlyFollowedBy(@2)) {
        return MakeSyntaxError(@1, "Syntax error: Unexpected \"/\"");
      }
      // Return an error if the identifier/literal is quoted.
      if ($id[0] == '`') {
        return MakeSyntaxError(@1, "Syntax error: Unexpected \"/\"");
      }
      auto out = MakeNode<SeparatedIdentifierTmpNode>(@1);
      out->set_path_parts({{"/", $id}});
      $$ = out;
    }
  | slashed_identifier slashed_identifier_separator[sep]
      identifier_or_integer[id]
    {
      // Return an error if there is embedded whitespace.
      if (!@1.IsAdjacentlyFollowedBy(@2) || !@2.IsAdjacentlyFollowedBy(@3)) {
        return MakeSyntaxError(@2,
          absl::StrFormat("Syntax error: Unexpected \"%s\"", $sep));
      }
      // Return an error if the identifier/literal is quoted.
      if ($id[0] == '`') {
        return MakeSyntaxError(@2,
          absl::StrFormat("Syntax error: Unexpected \"%s\"", $sep));
      }
      SeparatedIdentifierTmpNode::PathParts prev = $1->release_path_parts();
      // Add the separator and extra sub-part to the end of the current
      // identifier: {"a", "-", "b"} -> {"a", "-", "b", ":", "c"}
      prev.back().push_back($sep);
      prev.back().push_back($id);
      auto out = MakeNode<SeparatedIdentifierTmpNode>(@1);
      out->set_path_parts(std::move(prev));
      $$ = out;
    }
  | slashed_identifier
      slashed_identifier_separator[sep1]
      FLOATING_POINT_LITERAL[float]
      slashed_identifier_separator[sep2]
      identifier_or_integer[id]
    {
      // This rule handles floating point literals between separator
      // characters (/:-) before the first dot.  The floating point literal
      // can be {1., .1, 1.1, 1e2, 1.e2, .1e2, 1.1e2}.  The only valid form is
      // "1e2".  All forms containing a dot are invalid because the separator
      // characters are not allowed in identifiers after the dot.

      // Return an error if there is embedded whitespace.
      if (!@1.IsAdjacentlyFollowedBy(@2) || !@2.IsAdjacentlyFollowedBy(@3)) {
        return MakeSyntaxError(@2,
          absl::StrFormat("Syntax error: Unexpected \"%s\"", $sep1));
      }
      // Return an error if there is embedded whitespace.
      if (!@3.IsAdjacentlyFollowedBy(@4) || !@4.IsAdjacentlyFollowedBy(@5)) {
        return MakeSyntaxError(@2,
          absl::StrFormat("Syntax error: Unexpected \"%s\"", $sep2));
      }
      // Return an error if the trailing identifier is quoted.
      if ($id[0] == '`') {
        return MakeSyntaxError(@2,
          absl::StrFormat("Syntax error: Unexpected \"%s\"", $sep2));
      }
      // Return an error if the floating point literal contains a dot. Only
      // scientific notation is allowed in this rule.
      if (absl::StrContains($float, '.')) {
        return MakeSyntaxError(@3,
          "Syntax error: Unexpected floating point literal");
      }
      // We are parsing a floating point literal that uses scientific notation
      // in the middle of a slashed path, so just append the text to the
      // existing path. For text: "/a/1e10-b", {"/", "a"} becomes
      // {"/", "a", "/", "1e10". "-", "b"} after matching this rule.
      SeparatedIdentifierTmpNode::PathParts prev = $1->release_path_parts();
      prev.back().push_back($sep1);
      prev.back().push_back($float);
      prev.back().push_back($sep2);
      prev.back().push_back($id);
      auto out = MakeNode<SeparatedIdentifierTmpNode>(@1);
      out->set_path_parts(std::move(prev));
      $$ = out;
    }
;


// A path where the first identifier starts with "/" and can contain
// non-adjacent /:- separators.  Identifiers after the first dot are regular
// identifiers, except they can also start with a digit.
slashed_path_expression {ASTExpression*}:
    slashed_identifier
    {
      // Build the path.
      absl::StatusOr<std::vector<ASTNode*>> path_parts =
          SeparatedIdentifierTmpNode::BuildPathParts(@1,
            std::move($1->release_path_parts()), node_factory);
      if (!path_parts.ok()) {
        return MakeSyntaxError(@1, std::string(path_parts.status().message()));
      }
      $$ = MakeNode<ASTPathExpression>(@$, std::move(path_parts).value());
    }
  | slashed_identifier slashed_identifier_separator[sep]
    FLOATING_POINT_LITERAL[float] identifier[id]
    {
      // This rule handles floating point literals that are preceded by a
      // separator character (/:-). The floating point literal can be
      // {1., .1, 1.1, 1e2, 1.e2, .1e2, 1.1e2}, but the only valid form is a
      // floating point that ends with a dot. The dot is interpreted as the path
      // component separator, and we only allow a regular identifier following
      // the dot. A floating point that starts with a dot is not valid becuase
      // this implies that a dot and separator are adjacent: "-.1". A floating
      // point that has a dot in the middle is not supported because this format
      // is rejected by the tokenizer: "1.5table". A floating point literal that
      // does not contain a dot is not valid because this implies scientific
      // notation was lexed when adjacent to an identifier:
      // "/path/1e10  table". In this case it is not possible to determine if
      // the next token is an alias or part of the next statement.

      absl::string_view id = $id->GetAsStringView();
      // Return an error if there is embedded whitespace.
      if (!@1.IsAdjacentlyFollowedBy(@2) || !@2.IsAdjacentlyFollowedBy(@3)) {
        return MakeSyntaxError(@2,
          absl::StrFormat("Syntax error: Unexpected \"%s\"", $sep));
      }
      // Assert that the raw text of the floating literal ends in a dot since
      // we expect this rule to match at the boundary of a new path component.
      if (!absl::EndsWith($float, ".")) {
        return MakeSyntaxError(@2,absl::StrFormat(
          "Syntax error: Unexpected floating point literal \"%s\" after \"%s\"",
          $float, $sep));
      }
      SeparatedIdentifierTmpNode::PathParts prev =
        $1->release_path_parts();
      // This case is a continuation of an existing slashed_identifier
      // `prev`, followed by what the lexer believes is a floating point
      // literal.
      // here: /*prev=*/={{"a", "-", "b"}}
      // we append "1" to complete the identifier components, followed
      // by the identifier ("c") as {{"c"}}.
      // Thus, we end up with {{"a", "-", "b", "/", "1"}, {"c"}}
      prev.back().push_back($sep);
      prev.back().push_back($float);
      prev.push_back({id});

      // Build the path.
      absl::StatusOr<std::vector<ASTNode*>> path_parts =
        SeparatedIdentifierTmpNode::BuildPathParts(@$,
          std::move(prev), node_factory);
      if (!path_parts.ok()) {
        return MakeSyntaxError(@1, std::string(path_parts.status().message()));
      }
      $$ = MakeNode<ASTPathExpression>(@$, std::move(path_parts).value());
    }
  | slashed_identifier slashed_identifier_separator[sep]
    FLOATING_POINT_LITERAL[float] "." identifier
    {
      // This rule matches a slashed_identifier that terminates in a floating
      // point literal and is followed by the next path component, which must be
      // a regular identifier. The floating point literal can be
      // {1., .1, 1.1, 1e2, 1.e2, .1e2, 1.1e2}, but the only valid form is
      // "1e2".  All forms containing a dot are invalid because this implies
      // that either there are two dots in a row "1.." or the next path
      // component is a number itself, which we do not support (like "1.5.table"
      // and "1.1e10.table"). Note: paths like "/span/global.5.table" are
      // supported because once the lexer sees the first dot it enters
      // DOT_IDENTIFIER mode and lexs the "5" as an identifier rather than
      // producing a ".5" floating point literal token.

      // Return an error if there is embedded whitespace.
      if (!@1.IsAdjacentlyFollowedBy(@2) || !@2.IsAdjacentlyFollowedBy(@3)) {
        return MakeSyntaxError(@2,
          absl::StrFormat("Syntax error: Unexpected \"%s\"", $sep));
      }
      // Reject any floating point literal that contains a dot.
      if (absl::StrContains($float, '.')) {
        return MakeSyntaxError(@3,
          "Syntax error: Unexpected floating point literal");
      }
      // We are parsing a floating point literal that uses scientific notation
      // "1e10" that is followed by a dot and then an identifier. Append the
      // separator and floating point literal to the existing path and then
      // form an ASTPathExpression from the slash path and the trailing
      // identifier.
      SeparatedIdentifierTmpNode::PathParts prev = $1->release_path_parts();
      prev.back().push_back($sep);
      prev.back().push_back($float);

      // Build the slash path.
      absl::StatusOr<std::vector<ASTNode*>> path_parts =
        SeparatedIdentifierTmpNode::BuildPathParts(@$,
          std::move(prev), node_factory);
      if (!path_parts.ok()) {
        return MakeSyntaxError(@1, std::string(path_parts.status().message()));
      }
      // Add the trailing identifier to the path.
      path_parts.value().push_back($5);
      $$ = MakeNode<ASTPathExpression>(@$, std::move(path_parts).value());
    }
  | slashed_path_expression[path] "." identifier
    {
      $$ = ExtendNodeRight($path, $identifier);
    }
;

array_constructor {ASTExpression*}:
    ("ARRAY" | array_type)? "[" (expression separator ",")*[exprs] "]"
    {
      $$ = MakeNode<ASTArrayConstructor>(@$, $array_type, $exprs);
    }
;

range_literal {ASTExpression*}:
    range_type string_literal
    {
      $$ = MakeNode<ASTRangeLiteral>(@$, $1, $2);
    }
;

date_or_time_literal_kind {TypeKind}:
    "DATE"
    {
      $$ = TYPE_DATE;
    }
  | "DATETIME"
    {
      $$ = TYPE_DATETIME;
    }
  | "TIME"
    {
      $$ = TYPE_TIME;
    }
  | "TIMESTAMP"
    {
      $$ = TYPE_TIMESTAMP;
    }
;

date_or_time_literal {ASTExpression*}:
    date_or_time_literal_kind string_literal
    {
      auto* literal = MakeNode<ASTDateOrTimeLiteral>(@$, $2);
      literal->set_type_kind($1);
      $$ = literal;
    }
;

interval_expression {ASTExpression*}:
    "INTERVAL" expression identifier
    {
      $$ = MakeNode<ASTIntervalExpr>(@$, $2, $3);
    }
  | "INTERVAL" expression identifier "TO" identifier
    {
      $$ = MakeNode<ASTIntervalExpr>(@$, $2, $3, $5);
    }
;

parameter_expression {ASTExpression*}:
    named_parameter_expression
  | "?"
    {
      auto* parameter_expr = MakeNode<ASTParameterExpr>(@$);
      // Bison's algorithm guarantees that the "?" productions are reduced in
      // left-to-right order.
      parameter_expr->set_position(++previous_positional_parameter_position_);
      $$ = parameter_expr;
    }
;

named_parameter_expression {ASTExpression*}:
    "@"[at] identifier
    {
      if (!@at.IsAdjacentlyFollowedBy(@identifier)) {
        // TODO: Add a deprecation warning in this case.
      }
      $$ = MakeNode<ASTParameterExpr>(@$, $2);
    }
;

type_name {ASTNode*}:
    path_expression
    {
      $$ = MakeNode<ASTSimpleType>(@$, $1);
    }
    // Unlike other type names, 'INTERVAL' is a reserved keyword.
  | "INTERVAL"
    {
      auto* id = node_factory.MakeIdentifier(@1, $1);
      auto* path_expression = MakeNode<ASTPathExpression>(@$, id);
      $$ = MakeNode<ASTSimpleType>(@$, path_expression);
    }
;

template_type_close:
    ">" .CloseTypeTemplate FORCE_LA1_TOKEN?
;

array_type {ASTNode*}:
    "ARRAY" "<" .OpenTypeTemplate type template_type_close
    {
      $$ = MakeNode<ASTArrayType>(@$, $3);
    }
;

struct_field {ASTNode*}:
    identifier type
    {
      $$ = MakeNode<ASTStructField>(@$, $1, $2);
    }
  | type
    {
      $$ = MakeNode<ASTStructField>(@$, $1);
    }
;

struct_type {ASTNode*}:
    "STRUCT" "<" .OpenTypeTemplate (struct_field separator ",")*[fields] template_type_close
    {
      $$ = MakeNode<ASTStructType>(@$, $fields);
    }
;

range_type {ASTNode*}:
    "RANGE" "<" .OpenTypeTemplate type template_type_close
    {
      $$ = MakeNode<ASTRangeType>(@$, $3);
    }
;

function_type {ASTNode*}:
    "FUNCTION" "<" .OpenTypeTemplate ("(" ")")[parens] "->" type[return_type] template_type_close
    {
      auto empty_arg_list = MakeNode<ASTFunctionTypeArgList>(@parens);
      $$ = MakeNode<ASTFunctionType>(@$, empty_arg_list, $return_type);
    }
  | "FUNCTION" "<" .OpenTypeTemplate type[arg_type] "->" type[return_type] template_type_close
    {
      auto arg_list = MakeNode<ASTFunctionTypeArgList>(@arg_type, $arg_type);
      $$ = MakeNode<ASTFunctionType>(@$, arg_list, $return_type);
    }
  | "FUNCTION" "<" .OpenTypeTemplate "(" (type separator ",")+[types] ")" "->" type[return_type] template_type_close
    {
      // The parse location range for the ArgList is currently too wide. It
      // should not include anything outside the parentheses.
      auto* arg_list = MakeNode<ASTFunctionTypeArgList>(MakeLocationRange(@1, @types), $types);
      $$ = MakeNode<ASTFunctionType>(@$, arg_list, $return_type);
    }
;

map_type {ASTNode*}:
    "MAP" "<" .OpenTypeTemplate type[key_type] "," type[value_type] template_type_close
    {
      $$ = MakeNode<ASTMapType>(@$, $key_type, $value_type);
    }
;


raw_type {ASTNode*}:
    array_type
  | struct_type
  | type_name
  | range_type
  | function_type
  | map_type
;

type_parameter {ASTExpression*}:
    integer_literal
  | boolean_literal
  | string_literal
  | bytes_literal
  | floating_point_literal
  | "MAX"
    {
      $$ = MakeNode<ASTMaxLiteral>(@1);
    }
;

type_parameters_prefix {ASTNode*}:
    "(" type_parameter
    {
      $$ = MakeNode<ASTTypeParameterList>(@$, $2);
    }
  | type_parameters_prefix "," type_parameter
    {
      $$ = ExtendNodeRight($1, $3);
    }
;

type_parameters {ASTNode*}:
    type_parameters_prefix ")"
  | type_parameters_prefix "," ")"
    {
      return MakeSyntaxError(@2,
                             "Syntax error: Trailing comma in type parameter "
                             "list is not allowed.");
    }
;

opt_type_parameters {ASTNode*}:
    type_parameters?
    {
      $$ = $type_parameters.value_or(nullptr);
    }
;

type {ASTNode*}:
    raw_type type_parameters? collate_clause?
    {
      $$ = ExtendNodeRight($1, @$.end(), $2, $3);
    }
;

templated_parameter_kind {ASTTemplatedParameterType::TemplatedTypeKind}:
    "PROTO"
    {
      $$ = ASTTemplatedParameterType::ANY_PROTO;
    }
  | "ENUM"
    {
      $$ = ASTTemplatedParameterType::ANY_ENUM;
    }
  | "STRUCT"
    {
      $$ = ASTTemplatedParameterType::ANY_STRUCT;
    }
  | "ARRAY"
    {
      $$ = ASTTemplatedParameterType::ANY_ARRAY;
    }
  | identifier
    {
      const absl::string_view templated_type_string = $1->GetAsStringView();
      if (googlesql_base::CaseEqual(templated_type_string, "TABLE")) {
        $$ = ASTTemplatedParameterType::ANY_TABLE;
      } else if (googlesql_base::CaseEqual(templated_type_string, "TYPE")) {
        $$ = ASTTemplatedParameterType::ANY_TYPE;
      } else if (googlesql_base::CaseEqual(templated_type_string, "STRING")) {
        $$ = ASTTemplatedParameterType::ANY_STRING;
      } else {
        return MakeSyntaxError(@1,
                              "Syntax error: unexpected ANY template type");
      }
    }
;

templated_parameter_type {ASTNode*}:
    "ANY" templated_parameter_kind
    {
      auto* templated_parameter = MakeNode<ASTTemplatedParameterType>(@$);
      templated_parameter->set_kind($2);
      $$ = templated_parameter;
    }
;

type_or_tvf_schema {ASTNode*}:
    type
  | templated_parameter_type
  | tvf_schema
;

new_constructor_prefix_no_arg {ASTExpression*}:
    "NEW" type_name "("
    {
      $$ = MakeNode<ASTNewConstructor>(@$, $2);
    }
;

new_constructor_arg {ASTNode*}:
    expression
    {
      $$ = MakeNode<ASTNewConstructorArg>(@$, $1);
    }
  | expression "AS" identifier
    {
      $$ = MakeNode<ASTNewConstructorArg>(@$, $1, $3);
    }
  | expression "AS" "(" path_expression ")"
    {
      // Do not parenthesize $4 because it is not really a parenthesized
      // path expression. The parentheses are just part of the syntax here.
      $$ = MakeNode<ASTNewConstructorArg>(@$, $1, $4);
    }
;

new_constructor {ASTExpression*}:
    "NEW" type_name "(" (new_constructor_arg separator ",")+[args] ")"
    {
      $$ = MakeNode<ASTNewConstructor>(@$, $2, $args);
    }
  | new_constructor_prefix_no_arg ")"
    {
      $$ = WithEndLocation($1, @2);
    }
;

braced_constructor_field {ASTBracedConstructorField*}:
    expression[key]
    ((UPDATE_MANY_STAR[star] ":" | "?"[question] ":" | ":") expression[value])?
    {
      // We can't simply ask Is<ASTGeneralizedPathExpression> because that only
      // works for concrete nodes.
      auto is_generalized_path_expression = [](const ASTNode* node) {
        return node->Is<ASTArrayElement>() ||
               node->Is<ASTDotGeneralizedField>() ||
               node->Is<ASTDotIdentifier>() ||
               node->Is<ASTPathExpression>();
      };
      ASTBracedConstructorLhs* key = nullptr;
      ASTBracedConstructorFieldValue* value = nullptr;
      if ($value.has_value()) {
        key = MakeNode<ASTBracedConstructorLhs>(@key, $key);
        value = MakeNode<ASTBracedConstructorFieldValue>(*@value, $value);
        value->set_colon_prefixed(true);
      } else if (!$value.has_value()
                 && $key->Is<ASTBracedConstructorExtendedExpr>()
                 && $key->num_children() > 0
                 && is_generalized_path_expression($key->child(0))) {
        // This case has no colon. It is either a single key that happens to be
        // a braced constructor or it is a key-value pair where the colon has
        // been omitted and the value is a braced constructor. We assume the
        // latter for backward compatibility with textproto format.
        GOOGLESQL_RET_CHECK_EQ($key->num_children(), 2);
        key = MakeNode<ASTBracedConstructorLhs>($key->child(0)->location(),
                                                $key->mutable_child(0));
        value = MakeNode<ASTBracedConstructorFieldValue>(
            $key->child(1)->location(), $key->mutable_child(1));
        value->set_colon_prefixed(false);
      } else {
        // This case has only a key but cannot be the omitted colon case since
        // the key is not a braced constructor like expression. This branch may
        // eventually be useful for a SET-like constructor that only provides
        // keys but not values. For now it's an error. Try to be helpful in case
        // this looks like a dropped colon.
        if ($key->Is<ASTBracedConstructorExtendedExpr>()) {
          GOOGLESQL_RET_CHECK_EQ($key->num_children(), 2);
          return MakeSyntaxError(
              $key->child(1)->location(),
              "Syntax error: Unexpected braced constructor. Did you mean to "
              "use a ':' before the '{'?");
        }
        return MakeSyntaxError(next_symbol_.location,
                              "Syntax error: Invalid braced constructor "
                              "element, expected ':' followed by an expression "
                              "that computes the value");
      }
      auto update_op = @star.has_value()
          ? ASTBracedConstructorLhs::UPDATE_MANY
          : @question.has_value()
              ? ASTBracedConstructorLhs::UPDATE_SINGLE_NO_CREATION
              : ASTBracedConstructorLhs::UPDATE_SINGLE;
      key->set_operation(update_op);
      $$ = MakeNode<ASTBracedConstructorField>(@$, key, value);
    }
;

braced_constructor_field_following_omitted_comma {ASTBracedConstructorField*}:
    generalized_path_expression[key]
    (((UPDATE_MANY_STAR[star] ":" | "?"[question] ":" | ":") expression[value])
     | braced_constructor[sub]
    )
    {
      auto key = MakeNode<ASTBracedConstructorLhs>(@key, $key);
      key->set_operation(
          @star.has_value()
              ? ASTBracedConstructorLhs::UPDATE_MANY
              : @question.has_value()
                  ? ASTBracedConstructorLhs::UPDATE_SINGLE_NO_CREATION
                  : ASTBracedConstructorLhs::UPDATE_SINGLE);
      ASTBracedConstructorFieldValue* value = nullptr;
      if ($value.has_value())  {
        value = MakeNode<ASTBracedConstructorFieldValue>(*@value, $value);
        value->set_colon_prefixed(true);
      } else {
        value = MakeNode<ASTBracedConstructorFieldValue>(*@sub, $sub);
      }
      $$ = MakeNode<ASTBracedConstructorField>(@$, key, value);
    }
;

braced_constructor_field_list_continuation {ASTBracedConstructorField*}:
    "," .lr0 .StartBracedConstructorField braced_constructor_field
    {
      $$ = $braced_constructor_field;
      $$->set_comma_separated(true);
    }
  // The "," may be omitted as in the tradition for text formatted protos.
  // The "key" part of the key-value pair following an omitted comma is
  // restricted to path expressions to avoid gramatical ambiguity between the
  // value preceeding the ommited comma and the key following the omitted comma.
  | braced_constructor_field_following_omitted_comma
;

braced_constructor {ASTBracedConstructor*}:
  "{" .lr0 .StartBracedConstructorField
      ( braced_constructor_field[first]
        (braced_constructor_field_list_continuation)*[rest]
        ("," .lr0 .StartBracedConstructorField )[trailing_comma]?
      )? "}"
    {
      $$ = MakeNode<ASTBracedConstructor>(@$, $first, $rest);
    }
;

braced_new_constructor {ASTExpression*}:
    "NEW" type_name braced_constructor
    {
      $$ = MakeNode<ASTBracedNewConstructor>(@$, $2, $3);
    }
;

struct_braced_constructor {ASTExpression*}:
    struct_type[type] braced_constructor[ctor]
    {
      $$ = MakeNode<ASTStructBracedConstructor>(@$, $type, $ctor);
    }
  | "STRUCT" braced_constructor[ctor]
    {
      $$ = MakeNode<ASTStructBracedConstructor>(@$, $ctor);
    }
;

map_braced_constructor {ASTTypedBracedConstructor*}:
    "NEW" map_type braced_constructor[ctor]
    {
      $$ = MakeNode<ASTTypedBracedConstructor>(@$, $map_type, $ctor);
    }
;

case_no_value_expression_prefix {ASTExpression*}:
    "CASE" "WHEN" expression "THEN" expression
    {
      $$ = MakeNode<ASTCaseNoValueExpression>(@$, $3, $5);
    }
  | case_no_value_expression_prefix "WHEN" expression "THEN" expression
    {
      $$ = ExtendNodeRight($1, $3, $5);
    }
;

case_value_expression_prefix {ASTExpression*}:
    "CASE" expression "WHEN" expression "THEN" expression
    {
      $$ = MakeNode<ASTCaseValueExpression>(@$, $2, $4, $6);
    }
  | case_value_expression_prefix "WHEN" expression "THEN" expression
    {
      $$ = ExtendNodeRight($1, $3, $5);
    }
;

case_expression_prefix {ASTExpression*}:
    case_no_value_expression_prefix
  | case_value_expression_prefix
;

case_expression {ASTExpression*}:
    case_expression_prefix "END"
    {
      $$ = WithEndLocation($1, @$);
    }
  | case_expression_prefix[prefix] "ELSE" expression "END"
    {
      $$ = ExtendNodeRight($prefix, @$.end(), $expression);
    }
;

at_time_zone {ASTExpression*}:
    "AT" "TIME" "ZONE" expression
    {
      $$ = $4;
    }
;

opt_format {ASTNode*}:
    format?
    {
      $$ = $format.value_or(nullptr);
    }
;

format {ASTNode*}:
    "FORMAT" expression at_time_zone?
    {
      $$ = MakeNode<ASTFormatClause>(@$, $2, $3);
    }
;

cast_expression {ASTExpression*}:
    ("CAST" | "SAFE_CAST"[safe]) "(" expression "AS" type format? ")"
    {
      auto* cast = MakeNode<ASTCastExpression>(@$, $expression, $type, $format);
      cast->set_is_safe_cast(@safe.has_value());
      $$ = cast;
    }
  |  expression_higher_prec_than_and[expr] ( "::" | "?::"[safe]) raw_type type_parameters?
    {
      // Disallow "??::" because it is confusing and should instead be written
      // with separating whitespace as "? ?:: <type>".
      if ($expr->node_kind() == AST_PARAMETER_EXPR &&
          $expr->GetAs<ASTParameterExpr>()->position() > 0 &&
          @safe.has_value() &&
          @expr.end().GetByteOffset() == @safe->start().GetByteOffset())
      {
         return MakeSyntaxError(
          @safe,
          "Syntax error: ?:: must be separated by whitespace or comments");
      }
      auto* type = ExtendNodeRight($raw_type, @$.end(), $type_parameters);
      auto* cast = MakeNode<ASTCastExpression>(@$, $expr, type);
      cast->set_is_safe_cast(@safe.has_value());
      cast->set_is_cast_operator(true);
      $$ = cast;
    }
  | "CAST" "(" "SELECT"
    {
      return MakeSyntaxError(
          @3,
          "The argument to CAST is an expression, not a query; to use a query "
          "as an expression, the query must be wrapped with additional "
          "parentheses to make it a scalar subquery expression");
    }
  | "SAFE_CAST" "(" "SELECT"
    {
      return MakeSyntaxError(
          @3,
          "The argument to SAFE_CAST is an expression, not a query; to use a "
          "query as an expression, the query must be wrapped with additional "
          "parentheses to make it a scalar subquery expression");
    }
;

extract_expression_base {ASTExpression*}:
    "EXTRACT" "(" expression "FROM" expression
    {
      $$ = MakeNode<ASTExtractExpression>(@$, $3, $5);
    }
;

extract_expression {ASTExpression*}:
    extract_expression_base ")"
    {
      $$ = WithLocation($1, @$);
    }
  | extract_expression_base[base] "AT" "TIME" "ZONE" expression ")"
    {
      $$ = ExtendNodeRight($base, @$.end(), $expression);
    }
;

replace_fields_arg {ASTNode*}:
    expression "AS" generalized_path_expression
    {
      $$ = MakeNode<ASTReplaceFieldsArg>(@$, $1, $3);
    }
  | expression "AS" generalized_extension_path
    {
      $$ = MakeNode<ASTReplaceFieldsArg>(@$, $1, $3);
    }
;

replace_fields_expression {ASTExpression*}:
    "REPLACE_FIELDS" "(" expression "," (replace_fields_arg separator ",")+[args] ")"
    {
      $$ = MakeNode<ASTReplaceFieldsExpression>(@$, $expression, $args);
    }
;

function_name_from_keyword {ASTNode*}:
    "IF"
    {
      $$ = node_factory.MakeIdentifier(@1, $1);
    }
  | "GROUPING"
    {
      $$ = node_factory.MakeIdentifier(@1, $1);
    }
  | "LEFT"
    {
      $$ = node_factory.MakeIdentifier(@1, $1);
    }
  | "RIGHT"
    {
      $$ = node_factory.MakeIdentifier(@1, $1);
    }
  | "COLLATE"
    {
      $$ = node_factory.MakeIdentifier(@1, $1);
    }
  | "RANGE"
    {
      $$ = node_factory.MakeIdentifier(@1, $1);
    }
;

# Use this when identifiers are expected, and keywords from
# `function_name_from_keyword` are also allowed as identifiers.
# Note that `path_expression` also accepts these keywords because of
# special handling in the lexer for DOT_IDENTIFIER mode.
identifier_or_function_name_from_keyword {ASTNode*}:
    identifier
  | function_name_from_keyword
;

// These rules have "expression" as their first part rather than
// "path_expression". This is needed because the expression parser doesn't
// use the "path_expression" rule, and instead builds path expressions by
// starting with an identifier and then using .identifier followup rules to
// extend the path expression. If we were to use "path_expression" here, then
// the parser becomes ambiguous because it can parse the paths in two different
// ways, one using a sequence of expression parsing rules and one using a
// sequence of path_expression parsing rules. Instead, we use "expression" and
// error out if the expression is anything other than a path expression.
//
// One exception is made for CURRENT_DATE/TIMESTAMP/.. expressions, which are
// converted into function calls immediately when they are seen, even without
// parentheses. We allow them as input parameters so that parentheses can still
// be added to them after they are already parsed as function calls.
function_call_expression_base {ASTFunctionCall*}:
    expression_higher_prec_than_and "(" opt_distinct %prec PRIMARY_PRECEDENCE
    {
      // TODO: Merge this with the other code path. We have to have
      // two separate productions to avoid an empty opt_distinct rule that
      // causes shift/reduce conflicts.
      if ($1->node_kind() == AST_FUNCTION_CALL) {
        auto* function_call = $1->GetAsOrDie<ASTFunctionCall>();
        if (function_call->parenthesized()) {
          return MakeSyntaxError(
              @2,
              "Syntax error: Function call cannot be applied to this "
              "expression. Function calls require a path, e.g. a.b.c()");
        } else if (
            function_call->is_current_date_time_without_parentheses()) {
          // This is a function call like "CURRENT_DATE" without parentheses.
          // Allow parentheses to be added to such a call at most once.
          function_call->set_is_current_date_time_without_parentheses(false);
          function_call->set_distinct($opt_distinct);
          $$ = function_call;
        } else {
          return MakeSyntaxError(
              @2,
              "Syntax error: Double function call parentheses");
        }
      } else if (($1->node_kind() == AST_DOT_IDENTIFIER ||
                  $1->node_kind() == AST_DOT_GENERALIZED_FIELD) &&
                  !$1->parenthesized()) {
        // We have ".abc(" or ".(abc.def)(" and will treat this as a chained
        // function call.
        GOOGLESQL_RET_CHECK_EQ($1->num_children(), 2);

        ASTExpression* base_expr =
            $1->mutable_child(0)->GetAsOrNull<ASTExpression>();
        GOOGLESQL_RET_CHECK(base_expr != nullptr);

        if (!base_expr->parenthesized() &&
            (base_expr->node_kind() == AST_INT_LITERAL ||
              base_expr->node_kind() == AST_FLOAT_LITERAL)) {
          return MakeSyntaxError(@2, "Syntax error: Unexpected \"(\"");
        }

        ASTNode* function_path;
        if ($1->node_kind() == AST_DOT_IDENTIFIER) {
          // For ASTDotIdentifier, make an ASTPathExpression with the name.
          ASTNode* function_name = $1->mutable_child(1);
          function_path =
              MakeNode<ASTPathExpression>(function_name->GetParseLocationRange(),
                        function_name);
        } else {
          // For ASTDotGeneralizedField, use the existing ASTPathExpression.
          GOOGLESQL_RET_CHECK_EQ($1->child(1)->node_kind(), AST_PATH_EXPRESSION);
          function_path = $1->mutable_child(1);
        }

        // The `base_expr` is added as the first `argument`.  Additional
        // `arguments` will be added from the rest of the call.
        auto* function_call = MakeNode<ASTFunctionCall>(@$, function_path, base_expr);
        function_call->set_is_chained_call(true);
        function_call->set_distinct($opt_distinct);
        $$ = function_call;
      } else if (
          $1->node_kind() != AST_PATH_EXPRESSION ||
          $1->GetAsOrDie<ASTPathExpression>()->parenthesized()) {
        return MakeSyntaxError(
            @2,
            "Syntax error: Function call cannot be applied to this "
            "expression. Function calls require a path, e.g. a.b.c()");
      } else {
        auto* function_call = MakeNode<ASTFunctionCall>(@$, $1);
        function_call->set_distinct($opt_distinct);
        $$ = function_call;
      }
    }
  | function_name_from_keyword "(" opt_distinct %prec PRIMARY_PRECEDENCE
    {
      // IF and GROUPING can be function calls, but they are also keywords.
      // Treat them specially, and don't allow DISTINCT etc. since that only
      // applies to aggregate functions.
      auto* path_expression = MakeNode<ASTPathExpression>(@1, $1);
      auto* function_call = MakeNode<ASTFunctionCall>(@$, path_expression);
      function_call->set_distinct($opt_distinct);
      $$ = function_call;
    }
;

function_call_argument {ASTExpression*}:
    expression as_alias_with_required_as?
    {
      // When "AS alias" shows up in a function call argument, we wrap a new
      // node ASTExpressionWithAlias with required alias field to indicate
      // the existence of alias. This approach is taken mainly to avoid
      // backward compatibility break to existing widespread usage of
      // ASTFunctionCall.
      if ($2.has_value()) {
        $$ = MakeNode<ASTExpressionWithAlias>(@$, $1, $2);
      } else {
        $$ = $1;
      }
    }
  | named_argument
  | lambda_argument
  | sequence_arg
  | model_arg
  | function_ref_arg
  | "SELECT"
    {
      return MakeSyntaxError(
        @1,
        "Each function argument is an expression, not a query; to use a "
        "query as an expression, the query must be wrapped with additional "
        "parentheses to make it a scalar subquery expression");
    }
;

sequence_arg {ASTExpression*}:
    "SEQUENCE" path_expression
    {
      $$ = MakeNode<ASTSequenceArg>(@$, $2);
    }
;

model_arg {ASTExpression*}:
    "MODEL" path_expression
    {
      $$ = MakeNode<ASTModelArg>(@$, $2);
    }
;

function_ref_arg {ASTExpression*}:
    "FUNCTION" path_expression
    {
      $$ = MakeNode<ASTFunctionRefArg>(@$, $2);
    }
;

// Reserved keywords that are accepted as unquoted argument names on the
// left-hand side of "=>" in a named argument (e.g., `proto => ...`).
extra_identifier_in_named_arguments_name {absl::string_view}:
    "PROTO"
;

identifier_in_named_arguments {ASTIdentifier*}:
    identifier
  | extra_identifier_in_named_arguments_name
    {
      $$ = node_factory.MakeIdentifier(@1, $1);
    }
;

named_argument {ASTExpression*}:
    identifier_in_named_arguments[name] "=>" expression
    {
      $$ = MakeNode<ASTNamedArgument>(@$, $name, $expression);
    }
  | identifier_in_named_arguments[name] "=>" lambda_argument
    {
      $$ = MakeNode<ASTNamedArgument>(@$, $name, $lambda_argument);
    }
  | identifier_in_named_arguments[name] "=>" function_ref_arg
    {
      $$ = MakeNode<ASTNamedArgument>(@$, $name, $function_ref_arg);
    }
  | identifier_in_named_arguments[name] "=>" input_table_argument
    {
      $$ = MakeNode<ASTNamedArgument>(@$, $name, $input_table_argument);
    }
  | identifier_in_named_arguments[name] "=>" column_list_spec
    {
      $$ = MakeNode<ASTNamedArgument>(@$, $name, $column_list_spec);
    }
  | identifier_in_named_arguments[name] "=>" model_arg
    {
      $$ = MakeNode<ASTNamedArgument>(@$, $name, $model_arg);
    }
  | identifier_in_named_arguments[name] "=>" sequence_arg
    {
      $$ = MakeNode<ASTNamedArgument>(@$, $name, $sequence_arg);
    }
;

lambda_argument {ASTLambda*}:
    lambda_argument_list "->" expression
    {
      $$ = MakeNode<ASTLambda>(@$, $1, $3);
    }
;

// Lambda argument list could be:
//  * one argument without parenthesis, e.g. e.
//  * one argument with parenthesis, e.g. (e).
//  * multiple argument with parenthesis, e.g. (e, i).
// All of the above could be parsed as expression. (e, i) is parsed as struct
// constructor with parenthesis. We use expression rule to cover them all and to
// avoid conflict.
//
// We cannot use an identifier_list rule as that results in conflict with
// expression function argument. For ''(a, b) -> a + b', bison parser was not
// able to decide what to do with the following working stack: ['(', ID('a')]
// and seeing ID('b'), as bison parser won't look ahead to the '->' token.
lambda_argument_list {ASTNode*}:
    expression
    {
      auto expr_kind = $1->node_kind();
      if (expr_kind != AST_STRUCT_CONSTRUCTOR_WITH_PARENS &&
          expr_kind != AST_PATH_EXPRESSION) {
        return MakeSyntaxError(
          @1,
          "Syntax error: Expecting lambda argument list");
      }
      $$ = $1;
    }
  | "(" ")"
    {
      $$ = MakeNode<ASTStructConstructorWithParens>(@$);
    }
;

function_call_expression_with_args_prefix {ASTFunctionCall*}:
    function_call_expression_base function_call_argument
    {
      $$ = ExtendNodeRight($1, $2);
    }
    // The first argument may be a "*" instead of an expression. This is valid
    // for COUNT(*), which has no other arguments
    // and ANON_COUNT(*), which has multiple other arguments.
    // The analyzer must validate the "*" is not used with other functions.
  | function_call_expression_base "*"
    {
      auto* star = MakeNode<ASTStar>(@2);
      star->set_image("*");
      $$ = ExtendNodeRight($1, star);
    }
  | function_call_expression_with_args_prefix "," function_call_argument
    {
      $$ = ExtendNodeRight($1, $3);
    }
;

// A GROUP BY clause optionally followed by a HAVING clause in a function call
// is distinct from the GROUP BY clause in a top-level query, which is
// parsed as a separate `group_by_clause` node.
function_call_expression {ASTFunctionCall*}:
    // Empty argument list.
    function_call_expression_base[call]
    null_handling_modifier
    opt_where_clause
    having_modifier?
    (group_by_clause_prefix[group_by] having_clause[having]?)?
    with_report_modifier?
    order_by_clause?
    opt_limit_offset_clause ")"
    {
      $call->set_null_handling_modifier($null_handling_modifier);
      $$ = ExtendNodeRight($call,
        @$.end(),
        $opt_where_clause,
        $having_modifier,
        $group_by,
        $having,
        $with_report_modifier,
        $order_by_clause,
        $opt_limit_offset_clause);
    }
    // Non-empty argument list.
    // clamped_between_modifier only appears here as it requires at least
    // one argument.  With no arguments, CLAMPED parses as an identifier.
  | function_call_expression_with_args_prefix[call]
      null_handling_modifier
      opt_where_clause
      having_modifier?
      (group_by_clause_prefix[group_by] having_clause[having]?)?
      clamped_between_modifier?
      with_report_modifier?
      order_by_clause?
      opt_limit_offset_clause ")"
    {
      $call->set_null_handling_modifier($null_handling_modifier);
      $$ = ExtendNodeRight($call, @$.end(),
          $opt_where_clause,
          $having_modifier,
          $group_by,
          $having,
          $clamped_between_modifier,
          $with_report_modifier,
          $order_by_clause,
          $opt_limit_offset_clause);
    }
;

opt_identifier {ASTIdentifier*}:
    identifier?
    {
      $$ = $identifier.value_or(nullptr);
    }
;

partition_by_clause {ASTPartitionBy*}:
    "PARTITION" hint? "BY" (expression separator ",")+[exprs]
    {
      $$ = MakeNode<ASTPartitionBy>(@$, $hint, $exprs);
    }
;

opt_partition_by_clause {ASTPartitionBy*}:
    partition_by_clause?
    {
      $$ = $partition_by_clause.value_or(nullptr);
    }
;

partition_by_clause_prefix_no_hint {ASTPartitionBy*}:
    "PARTITION" "BY" (expression separator ",")+[exprs]
    {
      $$ = MakeNode<ASTPartitionBy>(@$, $exprs);
    }
;

opt_partition_by_clause_no_hint {ASTNode*}:
    partition_by_clause_prefix_no_hint[partition_by_clause_no_hint]?
    {
      $$ = $partition_by_clause_no_hint.value_or(nullptr);
    }
;

partition_by_clause_with_opt_alias {ASTNode*}:
    "PARTITION" "BY" (expression_with_opt_alias separator ",")+[exprs]
    {
      $$ = MakeNode<ASTPartitionByWithOptAlias>(@$, $exprs);
    }
;

cluster_by_clause_no_hint {ASTNode*}:
    "CLUSTER" "BY" (expression separator ",")+[exprs]
    {
      $$ = MakeNode<ASTClusterBy>(@$, $exprs);
    }
;

opt_cluster_by_clause_no_hint {ASTNode*}:
    cluster_by_clause_no_hint?
    {
      $$ = $cluster_by_clause_no_hint.value_or(nullptr);
    }
;

ttl_clause {ASTNode*}:
    "ROW" "DELETION" "POLICY" "(" expression ")"
    {
      if (!language_options.LanguageFeatureEnabled(FEATURE_TTL)) {
        return MakeSyntaxError(@1, "ROW DELETION POLICY clause is not supported.");
      }
      $$ = MakeNode<ASTTtlClause>(@$, $5);
    }
;

opt_ttl_clause {ASTNode*}:
    ttl_clause?
    {
      $$ = $ttl_clause.value_or(nullptr);
    }
;

// Returns PrecedingOrFollowingKeyword to indicate which keyword was present.
preceding_or_following {PrecedingOrFollowingKeyword}:
    "PRECEDING"
    {
      $$ = PrecedingOrFollowingKeyword::kPreceding;
    }
  | "FOLLOWING"
    {
      $$ = PrecedingOrFollowingKeyword::kFollowing;
    }
;

window_frame_bound {ASTNode*}:
    "UNBOUNDED" preceding_or_following
    {
      auto* frame = MakeNode<ASTWindowFrameExpr>(@$);
      frame->set_boundary_type(
          ($2 == PrecedingOrFollowingKeyword::kPreceding)
              ? ASTWindowFrameExpr::UNBOUNDED_PRECEDING
              : ASTWindowFrameExpr::UNBOUNDED_FOLLOWING);
      $$ = frame;
    }
  | "CURRENT" "ROW"
    {
      auto* frame = MakeNode<ASTWindowFrameExpr>(@$);
      frame->set_boundary_type(
          ASTWindowFrameExpr::CURRENT_ROW);
      $$ = frame;
    }
  | expression preceding_or_following
    {
      auto* frame = MakeNode<ASTWindowFrameExpr>(@$, $1);
      frame->set_boundary_type(
          ($2 == PrecedingOrFollowingKeyword::kPreceding)
              ? ASTWindowFrameExpr::OFFSET_PRECEDING
              : ASTWindowFrameExpr::OFFSET_FOLLOWING);
      $$ = frame;
    }
;

frame_unit {ASTWindowFrame::FrameUnit}:
    "ROWS"
    {
      $$ = ASTWindowFrame::ROWS;
    }
  | "RANGE"
    {
      $$ = ASTWindowFrame::RANGE;
    }
;

window_frame_clause {ASTWindowFrame*}:
    frame_unit "BETWEEN" window_frame_bound[low] "AND" window_frame_bound[high] %prec "BETWEEN"
    {
      $$ = MakeNode<ASTWindowFrame>(@$, $low, $high);
      $$->set_unit($frame_unit);
    }
  | frame_unit window_frame_bound
    {
      $$ = MakeNode<ASTWindowFrame>(@$, $window_frame_bound);
      $$->set_unit($frame_unit);
    }
;

window_specification {ASTNode*}:
    identifier
    {
      $$ = MakeNode<ASTWindowSpecification>(@$, $identifier);
    }
  | "(" identifier? opt_partition_by_clause order_by_clause?
          window_frame_clause? ")"
    {
      $$ = MakeNode<ASTWindowSpecification>(@$, $identifier,
                                            $opt_partition_by_clause,
                                            $order_by_clause,
                                            $window_frame_clause);
    }
;

function_call_expression_with_clauses {ASTExpression*}:
    function_call_expression[call] hint? over_clause?
    {
      ASTExpression* current_expression = ExtendNodeRight($call, $hint);
      if ($over_clause.has_value()) {
        current_expression = MakeNode<ASTAnalyticFunctionCall>(@$,
            current_expression, $over_clause);
      }
      $$ = current_expression;
    }
  | function_call_expression[call] within_clause[within]
    {
      $$ = MakeNode<ASTEstimatorFunctionCall>(@$, $call, $within);
    }
;

over_clause {ASTNode*}:
    "OVER" window_specification
    {
      $$ = $2;
    }
;

struct_constructor_prefix_with_keyword_no_arg {ASTExpression*}:
    struct_type "("
    {
      $$ = MakeNode<ASTStructConstructorWithKeyword>(@$, $1);
    }
  | "STRUCT" "("
    {
      $$ = MakeNode<ASTStructConstructorWithKeyword>(@$);
    }
;

struct_constructor_arg {ASTNode*}:
    expression as_alias_with_required_as?
    {
      $$ = MakeNode<ASTStructConstructorArg>(@$, $1, $2);
    }
;

struct_constructor_prefix_without_keyword {ASTExpression*}:
    // STRUCTs with no prefix must have at least two expressions, otherwise
    // they're parsed as parenthesized expressions.
    "(" expression "," expression
    {
      $$ = MakeNode<ASTStructConstructorWithParens>(@$, $2, $4);
    }
  | struct_constructor_prefix_without_keyword "," expression
    {
      $$ = ExtendNodeRight($1, $3);
    }
;

struct_constructor {ASTExpression*}:
    struct_constructor_prefix_with_keyword_no_arg[prefix] (struct_constructor_arg separator ",")+[args] ")"
    {
      $$ = ExtendNodeRight($prefix, @$.end(), $args);
    }
  | struct_constructor_prefix_with_keyword_no_arg ")"
    {
      $$ = WithEndLocation($1, @$);
    }
  | struct_constructor_prefix_without_keyword ")"
    {
      $$ = WithEndLocation($1, @$);
    }
;

expression_subquery_with_keyword {ASTExpressionSubquery*}:
    "ARRAY" parenthesized_query[query]
    {
      auto* subquery = MakeNode<ASTExpressionSubquery>(@$, $query);
      subquery->set_modifier(ASTExpressionSubquery::ARRAY);
      $$ = subquery;
    }
  | "EXISTS" hint? parenthesized_query[query]
    {
      auto* subquery = MakeNode<ASTExpressionSubquery>(@$, $hint, $query);
      subquery->set_modifier(ASTExpressionSubquery::EXISTS);
      $$ = subquery;
    }
  | "ARRAY" braced_graph_subquery[graph_query]
    {
      auto* subquery = MakeNode<ASTExpressionSubquery>(@$, $graph_query);
      subquery->set_modifier(ASTExpressionSubquery::ARRAY);
      $$ = subquery;
    }
  | exists_graph_subquery[subquery]
  | "VALUE" hint? braced_graph_subquery[graph_query]
    {
      auto* subquery = MakeNode<ASTExpressionSubquery>(@$, $hint, $graph_query);
      subquery->set_modifier(ASTExpressionSubquery::VALUE);
      $$ = subquery;
    }
;

null_literal {ASTExpression*}:
    "NULL"
    {
      auto* literal = MakeNode<ASTNullLiteral>(@1);
      // TODO: Migrate to absl::string_view or avoid having to
      // set this at all if the client isn't interested.
      literal->set_image(std::string($1));
      $$ = literal;
    }
;

boolean_literal {ASTExpression*}:
    "TRUE"
    {
      auto* literal = MakeNode<ASTBooleanLiteral>(@1);
      literal->set_value(true);
      // TODO: Migrate to absl::string_view or avoid having to
      // set this at all if the client isn't interested.
      literal->set_image(std::string($1));
      $$ = literal;
    }
  | "FALSE"
    {
      auto* literal = MakeNode<ASTBooleanLiteral>(@1);
      literal->set_value(false);
      // TODO: Migrate to absl::string_view or avoid having to
      // set this at all if the client isn't interested.
      literal->set_image(std::string($1));
      $$ = literal;
    }
;

string_literal_component {ASTStringLiteralComponent*}:
    STRING_LITERAL[s]
    {
      std::string str;
      std::string error_string;
      int error_offset;
      const absl::Status parse_status = ParseStringLiteral(
          $s, &str, &error_string, &error_offset);
      if (!parse_status.ok()) {
        auto location = @1;
        location.mutable_start().IncrementByteOffset(error_offset);
        if (!error_string.empty()) {
          return MakeSyntaxError(location,
                                absl::StrCat("Syntax error: ", error_string));
        }
        ABSL_DLOG(FATAL) << "ParseStringLiteral did not return an error string";
        return MakeSyntaxError(location,
                              absl::StrCat("Syntax error: ",
                                          parse_status.message()));
      }

      auto* literal = MakeNode<ASTStringLiteralComponent>(@1);
      literal->set_string_value(std::move(str));
      // TODO: Migrate to absl::string_view or avoid having to
      // set this at all if the client isn't interested.
      literal->set_image(std::string($s));
      $$ = literal;
    }
;

// Can be a concatenation of multiple string literals
string_literal {ASTStringLiteral*}:
    string_literal_component[component]
    {
      $$ = MakeNode<ASTStringLiteral>(@$, $component);
      $$->set_string_value($component->string_value());
    }
  | string_literal[list] string_literal_component[component]
    {
      if (@component.start().GetByteOffset() == @list.end().GetByteOffset()) {
        return MakeSyntaxError(@2, "Syntax error: concatenated string literals must be separated by whitespace or comments");
      }

      $$ = ExtendNodeRight($list, $component);
      // TODO: append the value in place, instead of StrCat()
      // then set().
      $$->set_string_value(
          absl::StrCat($$->string_value(), $component->string_value()));
    }
  | string_literal bytes_literal_component[component]
    {
      // Capture this case to provide a better error message
        return MakeSyntaxError(
            @component,
            "Syntax error: string and bytes literals cannot be concatenated.");
    }
;

bytes_literal_component {ASTBytesLiteralComponent*}:
    BYTES_LITERAL[b]
    {
      std::string bytes;
      std::string error_string;
      int error_offset;
      const absl::Status parse_status = ParseBytesLiteral(
          $b, &bytes, &error_string, &error_offset);
      if (!parse_status.ok()) {
        auto location = @1;
        location.mutable_start().IncrementByteOffset(error_offset);
        if (!error_string.empty()) {
          return MakeSyntaxError(location,
                                 absl::StrCat("Syntax error: ", error_string));
        }
        ABSL_DLOG(FATAL) << "ParseBytesLiteral did not return an error string";
        return MakeSyntaxError(location,
                               absl::StrCat("Syntax error: ",
                                            parse_status.message()));
      }

      // The identifier is parsed *again* in the resolver. The output of the
      // parser maintains the original image.
      // TODO: Fix this wasted work when the JavaCC parser is gone.
      auto* literal = MakeNode<ASTBytesLiteralComponent>(@1);
      literal->set_bytes_value(std::move(bytes));
      // TODO: Migrate to absl::string_view or avoid having to
      // set this at all if the client isn't interested.
      literal->set_image(std::string($b));
      $$ = literal;
    }
;


// Can be a concatenation of multiple string literals
bytes_literal {ASTBytesLiteral*}:
    bytes_literal_component[component]
    {
      $$ = MakeNode<ASTBytesLiteral>(@$, $component);
      $$->set_bytes_value($component->bytes_value());
    }
  | bytes_literal[list] bytes_literal_component[component]
    {
      if (@component.start().GetByteOffset() == @list.end().GetByteOffset()) {
        return MakeSyntaxError(
          @2,
          "Syntax error: concatenated bytes literals must be separated by whitespace or comments");
      }

      $$ = ExtendNodeRight($list, $component);
      // TODO: append the value in place, instead of StrCat()
      // then set().
      $$->set_bytes_value(absl::StrCat($$->bytes_value(), $2->bytes_value()));
    }
  | bytes_literal string_literal_component[component]
    {
      // Capture this case to provide a better error message
      return MakeSyntaxError(@component, "Syntax error: string and bytes literals cannot be concatenated.");
    }
;

integer_literal {ASTExpression*}:
    INTEGER_LITERAL
    {
      auto* literal = MakeNode<ASTIntLiteral>(@1);
      literal->set_image(std::string($1));
      $$ = literal;
    }
;

numeric_literal_prefix:
    "NUMERIC"
  | "DECIMAL"
;

numeric_literal {ASTExpression*}:
    numeric_literal_prefix string_literal
    {
      $$ = MakeNode<ASTNumericLiteral>(@$, $2);
    }
;

bignumeric_literal_prefix:
    "BIGNUMERIC"
  | "BIGDECIMAL"
;

bignumeric_literal {ASTExpression*}:
    bignumeric_literal_prefix string_literal
    {
      $$ = MakeNode<ASTBigNumericLiteral>(@$, $2);
    }
;

json_literal {ASTExpression*}:
    "JSON" string_literal
    {
      $$ = MakeNode<ASTJSONLiteral>(@$, $2);
    }
;

floating_point_literal {ASTExpression*}:
    FLOATING_POINT_LITERAL
    {
      auto* literal = MakeNode<ASTFloatLiteral>(@1);
      literal->set_image(std::string($1));
      $$ = literal;
    }
;

token_identifier {ASTIdentifier*}:
    IDENTIFIER
    {
      const absl::string_view identifier_text($1);
      // The tokenizer rule already validates that the identifier is valid,
      // except for backquoted identifiers.
      if (identifier_text[0] == '`') {
        std::string str;
        std::string error_string;
        int error_offset;
        const absl::Status parse_status =
            ParseGeneralizedIdentifier(
                identifier_text, &str, &error_string, &error_offset);
        if (!parse_status.ok()) {
          auto location = @1;
          location.mutable_start().IncrementByteOffset(error_offset);
          if (!error_string.empty()) {
            return MakeSyntaxError(location,
                                   absl::StrCat("Syntax error: ",
                                                error_string));
          }
          ABSL_DLOG(FATAL) << "ParseIdentifier did not return an error string";
          return MakeSyntaxError(location,
                                 absl::StrCat("Syntax error: ",
                                              parse_status.message()));
        }
        $$ = node_factory.MakeIdentifier(@1, str, /*is_quoted=*/true);
      } else {
        $$ = node_factory.MakeIdentifier(@1, identifier_text, /*is_quoted=*/false);
      }
    }
;

identifier {ASTIdentifier*}:
    token_identifier
  | set(NonReservedKeyword)[kw]
    {
      GOOGLESQL_ASSIGN_OR_RETURN($$, MakeIdentifierFromKeyword(@kw, $kw));
    }
;

label {ASTIdentifier*}:
    SCRIPT_LABEL
    {
      const absl::string_view label_text($1);
      // The tokenizer rule already validates that the identifier is valid and
      // non-empty, except for backquoted identifiers.
      if (label_text[0] == '`') {
        std::string str;
        std::string error_string;
        int error_offset;
        const absl::Status parse_status =
            ParseGeneralizedIdentifier(
                label_text, &str, &error_string, &error_offset);
        if (!parse_status.ok()) {
          auto location = @1;
          location.mutable_start().IncrementByteOffset(error_offset);
          if (!error_string.empty()) {
            return MakeSyntaxError(location,
                                 absl::StrCat("Syntax error: ",
                                              error_string));
          }
          ABSL_DLOG(FATAL) << "ParseIdentifier did not return an error string";
          return MakeSyntaxError(location,
                               absl::StrCat("Syntax error: ",
                                            parse_status.message()));
        }
        $$ = node_factory.MakeIdentifier(@1, str, /*is_quoted=*/true);
      } else {
        $$ = node_factory.MakeIdentifier(@1, label_text);
      }
    }
;

system_variable_expression {ASTExpression*}:
    "@@" path_expression %prec DOUBLE_AT_PRECEDENCE
    {
      if (!@1.IsAdjacentlyFollowedBy(@2)) {
        // TODO: Add a deprecation warning in this case.
      }
      $$ = MakeNode<ASTSystemVariableExpr>(@$, $2);
    }
;

%generate NonReservedKeyword = set(
    "ABORT"
  | "ACCESS"
  | "ACTION"
  | "ACYCLIC"
  | "AGGREGATE"
  | "ADD"
  | "AFTER"
  | "AI"
  | KW_ALIGN_NONRESERVED
  | "ALTER"
  | "ALWAYS"
  | "ANALYZE"
  | "APPROX"
  | "ARE"
  | "ASCENDING"
  | "ASSERT"
  | "AUTO"
  | "BATCH"
  | "BEGIN"
  | "BIGDECIMAL"
  | "BIGNUMERIC"
  | "BACKWARDS"
  | "BREAK"
  | "CALL"
  | "CASCADE"
  | "CHEAPEST"
  | "CHECK"
  | "CLAMPED"
  | "CLONE"
  | "COPY"
  | "COST"
  | "CLUSTER"
  | "COLUMN"
  | "COLUMNS"
  | "COMMIT"
  | "CONDITION"
  | "CONFLICT"
  | "CONNECTION"
  | "CONSTANT"
  | "CONSTRAINT"
  | "CONTINUE"
  | "CORRESPONDING"
  | "CYCLE"
  | "DATA"
  | "DATA_POLICY"
  | "DATABASE"
  | "DATE"
  | "DATETIME"
  | "DECIMAL"
  | "DECLARE"
  | "DEFINER"
  | "DELETE"
  | "DELETION"
  | "DEPTH"
  | "DESCENDING"
  | "DESCRIBE"
  | "DESTINATION"
  | "DETACH"
  | "DETERMINISTIC"
  | "DO"
  | "DROP"
  | "DYNAMIC"
  | "EDGE"
  | "ELSEIF"
  | "ENFORCED"
  | "EPOCH"
  | "ERROR"
  | "EXCEPTION"
  | "EXECUTE"
  | "EXPLAIN"
  | "EXPORT"
  | "EXTEND"
  | "EXTERNAL"
  | "FILES"
  | "FINISH"
  | "FILTER"
  | "FILL"
  | "FIRST"
  | "FOREIGN"
  | "FORK"
  | "FORMAT"
  | "FORWARDS"
  | "FUNCTION"
  | "GENERATED"
  | KW_GRAPH_TABLE_NONRESERVED
  | "GRAPH"
  | "GRANT"
  | "GROUP_ROWS"
  | "HIDDEN"
  | "IDENTITY"
  | "IMMEDIATE"
  | "IMMUTABLE"
  | "IMPORT"
  | "INCLUDE"
  | "INCREMENT"
  | "INDEX"
  | "INOUT"
  | "INPUT"
  | "INSERT"
  | "INVOKER"
  | "ISOLATION"
  | "ITERATE"
  | "JSON"
  | "KEY"
  | "LABEL"
  | "LABELED"
  | "LANGUAGE"
  | "LAST"
  | "LEAVE"
  | "LET"
  | "LEVEL"
  | "LIVE"
  | "LOAD"
  | "LOCALITY"
  | "LOG"
  | "LOOP"
  | "MACRO"
  | "MAP"
  | "MATCH"
  | KW_MATCH_RECOGNIZE_NONRESERVED
  | "MATCHED"
  | "MATERIALIZED"
  | "MAX"
  | "MAXVALUE"
  | "MEASURES"
  | "MESSAGE"
  | "METADATA"
  | "METRICS"
  | "MIN"
  | "MINVALUE"
  | "MODEL"
  | "MODULE"
  | "NAME"
  | "NEXT"
  | "NODE"
  | "NODETACH"
  | "NOTHING"
  | "NUMERIC"
  | "OFFSET"
  | "ONLY"
  | "OPTIONAL"
  | "OPTIONS"
  | "ORIGIN"
  | "OUT"
  | "OUTPUT"
  | "OVERWRITE"
  | "PARTITIONS"
  | "PAST"
  | "PATH"
  | "PATHS"
  | "PATTERN"
  | "PER"
  | "PERCENT"
  | "PERIOD"
  | "PIVOT"
  | "POLICIES"
  | "POLICY"
  | "PRIMARY"
  | "PRIVATE"
  | "PRIVILEGE"
  | "PRIVILEGES"
  | "PROCEDURE"
  | "PROJECT"
  | "PROPERTIES"
  | "PROPERTY"
  | "PUBLIC"
  | KW_QUALIFY_NONRESERVED
  | "RAISE"
  | "READ"
  | "REBUILD"
  | "REFERENCES"
  | "REMOTE"
  | "REMOVE"
  | "RENAME"
  | "REPEAT"
  | "REPEATABLE"
  | "REPLACE"
  | "REPLACE_FIELDS"
  | "REPLICA"
  | "REPORT"
  | "RESTRICT"
  | "RESTRICTION"
  | "RETURNS"
  | "RETURN"
  | "REVOKE"
  | "ROLLBACK"
  | "ROW"
  | "RUN"
  | "SAFE_CAST"
  | "SCHEMA"
  | "SEARCH"
  | "SECURITY"
  | "SEQUENCE"
  | "SETS"
  | "SHORTEST"
  | "SHOW"
  | "SIMPLE"
  | "SKIP"
  | "SNAPSHOT"
  | "SOURCE"
  | "SQL"
  | "STABLE"
  | "START"
  | "STATIC_DESCRIBE"
  | "STORED"
  | "STORING"
  | "STRICT"
  | "SYSTEM"
  | "SYSTEM_TIME"
  | "TABLE"
  | "TABLES"
  | "TARGET"
  | "TEMP"
  | "TEMPORARY"
  | "TEE"
  | "TIME"
  | "TIMESTAMP"
  | "TRAIL"
  | "TRANSACTION"
  | "TRANSFORM"
  | "TRUNCATE"
  | "TYPE"
  | "TYPES"
  | "UNDROP"
  | "UNIQUE"
  | "UNKNOWN"
  | "UNPIVOT"
  | "UNTIL"
  | "UPDATE"
  | "VALUE"
  | "VALUES"
  | "VARIANT"
  | "VERSION"
  | "VECTOR"
  | "VIEW"
  | "VIEWS"
  | "VOLATILE"
  | "WALK"
  | "WEIGHT"
  | "WHILE"
  | "WRITE"
  | "YIELD"
  | "ZONE"
  | "DESCRIPTOR"

    // Spanner-specific keywords
  | "INTERLEAVE"
  | "NULL_FILTERED"
  | "PARENT"
)
;

or_replace:
    "OR" "REPLACE"
;

opt_or_replace {bool}:
    or_replace?
    {
      $$ = @or_replace.has_value();
    }
;

create_scope {ASTCreateStatement::Scope}:
    "TEMP"
    {
      $$ = ASTCreateStatement::TEMPORARY;
    }
  | "TEMPORARY"
    {
      $$ = ASTCreateStatement::TEMPORARY;
    }
  | "PUBLIC"
    {
      $$ = ASTCreateStatement::PUBLIC;
    }
  | "PRIVATE"
    {
      $$ = ASTCreateStatement::PRIVATE;
    }
;

opt_create_scope {ASTCreateStatement::Scope}:
    create_scope?
    {
      $$ = $1.value_or(ASTCreateStatement::DEFAULT_SCOPE);
    }
;

describe_keyword:
    "DESCRIBE"
  | "DESC"
;

options_assignment_operator {ASTOptionsEntry::AssignmentOp}:
    "="
    {
      $$ = ASTOptionsEntry::ASSIGN;
    }
  | "+="
    {
      $$ = ASTOptionsEntry::ADD_ASSIGN;
    }
  | "-="
    {
      $$ = ASTOptionsEntry::SUB_ASSIGN;
    }
;

expression_or_proto {ASTExpression*}:
    "PROTO"
    {
      ASTIdentifier* proto_identifier =
          node_factory.MakeIdentifier(@1, "PROTO");
      $$ = MakeNode<ASTPathExpression>(@$, proto_identifier);
    }
  | expression
;

options_entry {ASTOptionsEntry*}:
    identifier_in_hints[name] options_assignment_operator[op] expression_or_proto[value]
    {
      auto* entry = MakeNode<ASTOptionsEntry>(@$, $name, $value);
      entry->set_assignment_op($op);
      $$ = entry;
    }
  | "FROM"[from] expression[expr]
    {
      if (!language_options.LanguageFeatureEnabled(FEATURE_OPTIONS_FROM_STRUCT)) {
        return MakeSyntaxError(
            @from,
            "Syntax error: OPTIONS (FROM <struct_expression>) is not supported");
      }
      auto* entry = MakeNode<ASTOptionsEntry>(@$, nullptr, $expr);
      entry->set_assignment_op(ASTOptionsEntry::FROM);
      $$ = entry;
    }
;

options_list {ASTOptionsList*}:
    "(" (options_entry separator ",")*[options] ")"
    {
      bool non_from_option_seen = false;
      for (int i = 0; i < $options.size(); ++i) {
        if ($options[i]->assignment_op() == ASTOptionsEntry::FROM) {
          if (non_from_option_seen) {
            return MakeSyntaxError(
                $options[i]->location(),
                "Syntax error: FROM option must appear before all other options");
          }
        } else {
          non_from_option_seen = true;
        }
      }
      $$ = MakeNode<ASTOptionsList>(@$, $options);
    }
;

options {ASTOptionsList*}:
    "OPTIONS" options_list
    {
      $$ = $2;
    }
;

opt_options_list {ASTNode*}:
    options?
    {
      $$ = $1.value_or(nullptr);
    }
;

define_table_statement {ASTNode*}:
    "DEFINE" "TABLE" path_expression options_list
    {
      $$ = MakeNode<ASTDefineTableStatement>(@$, $3, $4);
    }
;

dml_statement {ASTNode*}:
    insert_statement
  | delete_statement
  | update_statement
;

where_expression {ASTNode*}:
    "WHERE" expression
    {
      $$ = $2;
    }
;

assert_rows_modified {ASTNode*}:
    "ASSERT_ROWS_MODIFIED" possibly_cast_int_literal_or_parameter
    {
      $$ = MakeNode<ASTAssertRowsModified>(@$, $2);
    }
;

returning_clause {ASTNode*}:
    "THEN" "RETURN" select_list
    {
      $$ = MakeNode<ASTReturningClause>(@$, $3);
    }
  | "THEN" "RETURN" "WITH" "ACTION" select_list
    {
      ASTIdentifier* default_identifier =
          node_factory.MakeIdentifier(@4, "ACTION");
      auto* action_alias = MakeNode<ASTAlias>(@$, default_identifier);
      $$ = MakeNode<ASTReturningClause>(@$, $5, action_alias);
    }
  | "THEN" "RETURN" "WITH" "ACTION" "AS" identifier select_list
    {
      auto* action_alias = MakeNode<ASTAlias>(@$, $6);
      $$ = MakeNode<ASTReturningClause>(@$, $7, action_alias);
    }
;

conflict_target {ASTNode*}:
    column_list
  | "ON" "UNIQUE" "CONSTRAINT" identifier
    {
      $$ = $identifier;
    }
;

on_conflict_clause {ASTOnConflictClause*}:
    "ON" "CONFLICT" conflict_target? "DO"
    ("NOTHING" | "UPDATE"[update] "SET" (update_item separator ",")+[update_items] where_expression[update_where_expr]?)
    {
      ASTUpdateItemList* update_item_list = nullptr;
      if (@update_items) {
        update_item_list = MakeNode<ASTUpdateItemList>(*@update_items, *$update_items);
      }
      $$ = MakeNode<ASTOnConflictClause>(@$, $conflict_target, update_item_list, $update_where_expr);
      $$->set_conflict_action(@update.has_value() ? ASTOnConflictClause::UPDATE : ASTOnConflictClause::NOTHING);
    }
;

insert_mode {ASTInsertStatement::InsertMode}:
    "OR"? "IGNORE"
    {
      $$ = ASTInsertStatement::IGNORE;
    }
  | (KW_REPLACE_AFTER_INSERT | "OR" "REPLACE")
    {
      $$ = ASTInsertStatement::REPLACE;
    }
  | (KW_UPDATE_AFTER_INSERT | "OR" "UPDATE")
    {
      $$ = ASTInsertStatement::UPDATE;
    }
;

insert_source_and_opt_on_conflict {std::pair<ASTNode*, std::optional<ASTOnConflictClause*>>}:
    # The query must be parenthesized to have an on_conflict_clause. This avoids
    # a conflict with the "ON" clause specifying a join condition.
    query
    {
      $$ = std::make_pair($query, std::nullopt);
    }
  | "(" query ")" on_conflict_clause[on_conflict]
    {
      $$ = std::make_pair($query, $on_conflict);
    }
  | "VALUES" (insert_values_row separator ",")+[rows] on_conflict_clause[on_conflict]?
    {
      auto rows = WithEndLocation(
          MakeNode<ASTInsertValuesRowList>(@$, $rows), @rows);
      $$ = std::make_pair(rows, $on_conflict);
    }
  | table_clause_unreserved[table] on_conflict_clause[on_conflict]?
    {
      $$ = std::make_pair(MakeNode<ASTQuery>(@table, $table), $on_conflict);
    }
;

# The LookaheadTransformer assumes that the sequence < IDENTIFIER '(' > is not
# followed by a query. This non-terminal is used to mark states that are
# exceptions to that assumption, such as the case in the non-pipes INSERT
# statement for `INSERT INTO table_name  ●  (SELECT ...`.
#
# The lowerCamelCase name is chosen to match the typical naming convention for
# Textmapper state markers.
# TODO: b/431288404 - After Textmapper C++ supports a way to access state
#                     marker state, replace this non-term witha state marker.
identifierPreceedsParenthsizedQuery:
    %empty
    {
      OVERRIDE_NEXT_TOKEN_LOOKBACK(LPAREN, LB_PAREN_OPENS_QUERY);
    }
;

version_aware_clause {ASTNode*}:
    temporal_at_clause
  | with_timestamp_clause
;

dml_target_suffix {DmlTargetSuffix}:
    temporal_at_clause as_alias? with_offset_and_alias?
    {
      $$ = DmlTargetSuffix{
          .temporal_at = $temporal_at_clause,
          .alias = $as_alias.value_or(nullptr),
          .with_offset = $with_offset_and_alias.value_or(nullptr)};
    }
  | as_alias? with_timestamp_clause? with_offset_and_alias?
    {
      $$ = DmlTargetSuffix{
          .alias = $as_alias.value_or(nullptr),
          .with_timestamp = $with_timestamp_clause.value_or(nullptr),
          .with_offset = $with_offset_and_alias.value_or(nullptr)};
    }
;

temporal_at_clause {ASTTemporalAt*}:
    "AT" "(" identifier "=>" expression ")"
    {
      $$ = MakeNode<ASTTemporalAt>(@$, $identifier, $expression);
    }
;

with_timestamp_clause {ASTWithTimestamp*}:
    "WITH" .PossibleWithTimestamp "TIMESTAMP"
    {
      $$ = MakeNode<ASTWithTimestamp>(@$);
    }
  | "WITH" .PossibleWithTimestamp KW_TIMESTAMP_IN_WITH_TIMESTAMP_AS_ALIAS
    as_alias_with_required_as
    {
      $$ = MakeNode<ASTWithTimestamp>(@$, $as_alias_with_required_as);
    }
;

insert_column_using_clause {ASTInsertColumnUsingClause*}:
    "USING" "FUNCTION" path_expression ("DEFAULT" expression[default_expr])?
    {
      $$ = MakeNode<ASTInsertColumnUsingClause>(@$, $path_expression, $default_expr);
    }
  | "USING" lambda_argument ("DEFAULT" expression[default_expr])?
    {
      $$ = MakeNode<ASTInsertColumnUsingClause>(@$, $lambda_argument, $default_expr);
    }
;

insert_column_item {ASTInsertColumnItem*}:
    identifier insert_column_using_clause?
    {
      $$ = MakeNode<ASTInsertColumnItem>(@$, $identifier, $insert_column_using_clause);
    }
;

insert_column_list {ASTInsertColumnList*}:
    "(" (insert_column_item separator ",")+[column_list]
    {
      OVERRIDE_NEXT_TOKEN_LOOKBACK(RPAREN, LB_CLOSE_COLUMN_LIST);
    }
    ")"
    {
      $$ = MakeNode<ASTInsertColumnList>(@$, $column_list);
    }
;

insert_statement {ASTInsertStatement*}:
    # LINT.IfChange(insert_statement)
    "INSERT"[kwd] insert_mode[mode]?
    "INTO"? maybe_dashed_generalized_path_expression[target]
    identifierPreceedsParenthsizedQuery
    hint? version_aware_clause?
    insert_column_list[cols]? insert_source_and_opt_on_conflict
    assert_rows_modified[assert]? returning_clause[returning]?
    {
      auto [source, on_conflict] = $insert_source_and_opt_on_conflict;
      $$ = MakeNode<ASTInsertStatement>(@$, $target, $hint,
                                        $version_aware_clause,
                                        $cols, source, on_conflict, $assert,
                                        $returning);
      $$->set_insert_mode($mode.value_or(ASTInsertStatement::DEFAULT_MODE));
      $$->set_operator_keyword_location(@kwd);
    }
    # LINT.ThenChange(:insert_statement_in_pipe)
;

# The pipe form of INSERT has all the same modifiers but has no VALUES or input
# query in the middle because the insert content will come from the pipe input.
#
# The rule is split into 2 parts because otherwise Textmapper overcounts the
# shift/reduce conflicts which spams the debug output. The overcounting occurs
# because Textmapper counts shift/reduce conflicts after expanding syntactic
# sugar, thus counting one per desugared rule instead of once per logical rule.
#
# See explanation above at "AMBIGUOUS CASE ... Pipe INSERT" for details on the
# specific shift/reduce conflict.
insert_statement_in_pipe_prefix {ASTInsertStatement*}:
    # LINT.IfChange(insert_statement_in_pipe)
    "INSERT" insert_mode[mode]?
    "INTO"? maybe_dashed_generalized_path_expression[target]
    hint? version_aware_clause?
    insert_column_list? on_conflict_clause[on_conflict]?
    {
      $$ = MakeNode<ASTInsertStatement>(@$, $target, $hint,
                                        $version_aware_clause,
                                        $insert_column_list,
                                        $on_conflict);
      $$->set_insert_mode($mode.value_or(ASTInsertStatement::DEFAULT_MODE));
      $$->set_operator_keyword_location(@1);
    }
;

insert_statement_in_pipe {ASTInsertStatement*}:
    insert_statement_in_pipe_prefix[stmt] assert_rows_modified-opt[assert]
    returning_clause[returning]?
    {
      $$ = ExtendNodeRight($stmt, $assert, $returning);
    }
    # LINT.ThenChange(:insert_statement)
;

copy_data_source {ASTNode*}:
    maybe_dashed_path_expression for_version_clause? opt_where_clause
    {
      $$ = MakeNode<ASTCopyDataSource>(@$, $1, $for_version_clause, $3);
    }
;

clone_data_source {ASTNode*}:
    maybe_dashed_path_expression for_version_clause? opt_where_clause
    {
      $$ = MakeNode<ASTCloneDataSource>(@$, $1, $for_version_clause, $3);
    }
;

clone_data_statement {ASTNode*}:
    "CLONE" "DATA" "INTO" maybe_dashed_path_expression
    "FROM" (clone_data_source separator "UNION" "ALL")+[clone_data_sources]
    {
      auto* clone_data_sources = MakeNode<ASTCloneDataSourceList>(@clone_data_sources, $clone_data_sources);
      $$ = MakeNode<ASTCloneDataStatement>(@$, $maybe_dashed_path_expression, clone_data_sources);
    }
;

expression_or_default {ASTExpression*}:
    expression
  | "DEFAULT"
    {
      $$ = MakeNode<ASTDefaultLiteral>(@$);
    }
;

insert_values_row {ASTInsertValuesRow*}:
    "(" (expression_or_default separator ",")+[row] ")"
    {
      $$ = MakeNode<ASTInsertValuesRow>(@$, $row);
    }
;

# The prefix is factored off to control the number of expanded rules
delete_statement_prefix {ASTDeleteStatement*}:
    "DELETE"[kwd] "FROM"? maybe_dashed_generalized_path_expression hint?
    dml_target_suffix[suffix]
    {
      $$ = MakeNode<ASTDeleteStatement>(@$,
                                        $maybe_dashed_generalized_path_expression,
                                        $hint, $suffix.temporal_at,
                                        $suffix.alias, $suffix.with_timestamp,
                                        $suffix.with_offset);
      $$->set_operator_keyword_location(@kwd);
    }
;

delete_using_clause {ASTNode*}:
    "USING" from_clause_contents
    {
      ErrorInfo error_info;
      auto node = TransformJoinExpression(
        $2, node_factory, &error_info);
      if (node == nullptr) {
        return MakeSyntaxError(error_info.location, error_info.message);
      }

      $$ = MakeNode<ASTDeleteUsingClause>(@$, node);
    }
;

delete_statement {ASTNode*}:
    delete_statement_prefix delete_using_clause? where_expression? assert_rows_modified?
    returning_clause?
    {
      $$ = ExtendNodeRight($delete_statement_prefix, @$.end(), $delete_using_clause, $where_expression, $assert_rows_modified, $returning_clause);
    }
;

with_offset {ASTWithOffset*}:
    "WITH" .PossibleWithTimestamp "OFFSET"
    {
      $$ = MakeNode<ASTWithOffset>(@$);
    }
;

with_offset_and_alias {ASTWithOffset*}:
    with_offset as_alias?
    {
      $$ = ExtendNodeRight($with_offset, $as_alias);
    }
;

update_statement {ASTNode*}:
    "UPDATE"[kwd] maybe_dashed_generalized_path_expression[path] hint?
    dml_target_suffix[suffix]
    "SET" (update_item separator ",")+[update_item_list]
    opt_from_clause where_expression? assert_rows_modified? returning_clause?
    {
      auto* update_item_list = MakeNode<ASTUpdateItemList>(@update_item_list, $update_item_list);
      $$ = MakeNode<ASTUpdateStatement>(@$, $path, $hint, $suffix.temporal_at,
                                        $suffix.alias, $suffix.with_timestamp,
                                        $suffix.with_offset, update_item_list,
                                        $opt_from_clause, $where_expression,
                                        $assert_rows_modified,
                                        $returning_clause);
      $$->set_operator_keyword_location(@kwd);
    }
;

truncate_statement {ASTNode*}:
    "TRUNCATE" "TABLE" maybe_dashed_path_expression where_expression?
    {
      $$ = MakeNode<ASTTruncateStatement>(@$, $3, $4);
    }
;

nested_dml_statement {ASTNode*}:
    {
      OVERRIDE_NEXT_TOKEN_LOOKBACK(LPAREN, LB_OPEN_NESTED_DML);
    }
    "("[open] dml_statement ")"
    {
      $$ = $dml_statement;
    }
;

// A "generalized path expression" is a path expression that can contain
// generalized field accesses (e.g., "a.b.c.(foo.bar).d.e"). To avoid
// ambiguities in the grammar (particularly with INSERT), a generalized path
// must start with an identifier. The parse trees that result are consistent
// with the similar syntax in the <expression> rule.
generalized_path_expression {ASTExpression*}:
    identifier
    {
      $$ = MakeNode<ASTPathExpression>(@$, $1);
    }
  | generalized_path_expression "." generalized_extension_path
    {
      // Remove the parentheses from generalized_extension_path as they were
      // added to indicate the path corresponds to an extension field in the
      // resolver. It is implied that the path argument of
      // ASTDotGeneralizedField is an extension and thus parentheses are
      // automatically added when this node is unparsed.
      $3->set_parenthesized(false);
      $$ = MakeNode<ASTDotGeneralizedField>(@$, $1, $3);
    }
  | generalized_path_expression[path] "." identifier
    {
      if ($path->node_kind() == AST_PATH_EXPRESSION) {
        $$ = ExtendNodeRight($path, $identifier);
      } else {
        $$ = MakeNode<ASTDotIdentifier>(@$, $path, $3);
      }
    }
  | generalized_path_expression "[" expression "]"
    {
      auto* bracket_loc = MakeNode<ASTLocation>(@2);
      $$ = MakeNode<ASTArrayElement>(@$, $1, bracket_loc, $3);
    }
;

maybe_dashed_generalized_path_expression {ASTExpression*}:
    generalized_path_expression
    // TODO: This is just a regular path expression, not generalized one
    // it doesn't allow extensions or array elements access. It is OK for now,
    // since this production is only used in INSERT INTO and UPDATE statements
    // which don't actually allow extensions or array element access anyway.
    | dashed_path_expression
      {
        if (language_options.LanguageFeatureEnabled(
              FEATURE_ALLOW_DASHES_IN_TABLE_NAME)) {
          $$ = $1;
        } else {
          absl::string_view target =
            $1->num_children() > 1
              ? "The dashed identifier part of the table name "
              : "It ";
          return MakeSyntaxError(
              @1,
              absl::StrCat(
                "Syntax error: Table name contains '-' character. ",
                target,
                "needs to be quoted: ",
                ToIdentifierLiteral(
                  $1->child(0)->GetAs<ASTIdentifier>()->GetAsStringView(),
                  false)));
        }
      }
;

// A "generalized extension path" is similar to a "generalized path expression"
// in that they contain generalized field accesses. The primary difference is
// that a generalized extension path must start with a parenthesized path
// expression, where as a generalized path expression must start with an
// identifier. A generalized extension path allows field accesses of message
// extensions to be parsed.
generalized_extension_path {ASTExpression*}:
    "(" path_expression ")"
    {
      $2->set_parenthesized(true);
      $$ = $2;
    }
  | generalized_extension_path "." "(" path_expression ")"
    {
      $$ = MakeNode<ASTDotGeneralizedField>(@$, $1, $4);
    }
  | generalized_extension_path "." identifier
    {
      $$ = MakeNode<ASTDotIdentifier>(@$, $1, $3);
    }
;

update_set_value {ASTNode*}:
    generalized_path_expression "=" expression_or_default
    {
      $$ = MakeNode<ASTUpdateSetValue>(@$, $1, $3);
    }
;

update_item {ASTNode*}:
    update_set_value
    {
      $$ = MakeNode<ASTUpdateItem>(@$, $1);
    }
  | nested_dml_statement
    {
      $$ = MakeNode<ASTUpdateItem>(@$, $1);
    }
;

merge_insert_value_list_or_source_row {ASTNode*}:
    "VALUES" insert_values_row
    {
      $$ = $2;
    }
  | "ROW"
    {
      $$ = MakeNode<ASTInsertValuesRow>(@$);
    }
;

merge_action {ASTMergeAction*}:
    "INSERT" opt_column_list merge_insert_value_list_or_source_row
    {
      $$ = MakeNode<ASTMergeAction>(@$, $2, $3);
      $$->set_action_type(ASTMergeAction::INSERT);
    }
  | "UPDATE" "SET" (update_item separator ",")+[update_items]
    {
      auto* update_item_list = MakeNode<ASTUpdateItemList>(@update_items, $update_items);
      $$ = MakeNode<ASTMergeAction>(@$, update_item_list);
      $$->set_action_type(ASTMergeAction::UPDATE);
    }
  | "DELETE"
    {
      $$ = MakeNode<ASTMergeAction>(@$);
      $$->set_action_type(ASTMergeAction::DELETE);
    }
;

merge_when_clause {ASTMergeWhenClause*}:
    "WHEN" ("MATCHED"[matched] | "NOT" "MATCHED"[not_matched_by_target] ("BY" "TARGET")? | "NOT" "MATCHED"[not_matched_by_source] "BY" "SOURCE") ("AND" expression)? "THEN" merge_action
    {
      $$ = MakeNode<ASTMergeWhenClause>(@$, $expression, $merge_action);
      ASTMergeWhenClause::MatchType match_type = ASTMergeWhenClause::NOT_SET;
      if (@matched) {
        match_type = ASTMergeWhenClause::MATCHED;
      } else if (@not_matched_by_target) {
        match_type = ASTMergeWhenClause::NOT_MATCHED_BY_TARGET;
      } else if (@not_matched_by_source) {
        match_type = ASTMergeWhenClause::NOT_MATCHED_BY_SOURCE;
      }
      $$->set_match_type(match_type);
    }
;

// TODO: Consider allowing table_primary as merge_source, which
// requires agreement about spec change.
merge_source {ASTTableExpression*}:
    table_path_expression
  | table_subquery
;

merge_statement_prefix {ASTNode*}:
    "MERGE" "INTO"? maybe_dashed_path_expression as_alias?
  "USING" merge_source "ON" expression
    {
      $$ = MakeNode<ASTMergeStatement>(@$, $3, $4, $6, $8);
      $$->set_operator_keyword_location(@1);
    }
;

merge_statement {ASTNode*}:
    merge_statement_prefix merge_when_clause+[merge_when_clause]
    {
      $$ = ExtendNodeRight($1, MakeNode<ASTMergeWhenClauseList>(@merge_when_clause, $merge_when_clause));
    }
;

call_statement {ASTNode*}:
    "CALL" path_expression "(" (tvf_argument separator ",")*[args] ")"
    {
      $$ = MakeNode<ASTCallStatement>(@$, $2, $args);
    }
;

opt_function_parameters {ASTNode*}:
    function_parameters?
    {
      $$ = $function_parameters.value_or(nullptr);
    }
;

/* Returns true if IF EXISTS was specified. */
opt_if_exists {bool}:
    if_exists
    {
      $$ = true;
    }
  | %empty
    {
      $$ = false;
    }
;

if_exists:
    "IF" "EXISTS"
;

/* Returns true if ACCESS was specified. */
access {bool}:
    "ACCESS"
    {
      $$ = true;
    }
;

/* Returns true if ACCESS was specified, false otherwise. */
opt_access {bool}:
    access?
    {
      $$ = $access.value_or(false);
    }
;

// TODO: Make new syntax mandatory.
drop_all_row_access_policies_statement {ASTNode*}:
    "DROP" "ALL" "ROW" access? "POLICIES" "ON" path_expression
    {
      auto* drop_all = MakeNode<ASTDropAllRowAccessPoliciesStatement>(@$, $path_expression);
      drop_all->set_has_access_keyword($access.value_or(false));
      $$ = drop_all;
    }
;

drop_mode {ASTDropStatement::DropMode}:
    "RESTRICT"
    {
      $$ = ASTDropStatement::DropMode::RESTRICT;
    }
  | "CASCADE"
    {
      $$ = ASTDropStatement::DropMode::CASCADE;
    }
;

drop_statement {ASTNode*}:
    "DROP" "PRIVILEGE" "RESTRICTION" opt_if_exists
    "ON" privilege_list "ON" identifier path_expression
    {
      auto* node =
          MakeNode<ASTDropPrivilegeRestrictionStatement>(@$, $6, $8, $9);
      node->set_is_if_exists($4);
      $$ = node;
    }
  | "DROP" "ROW" "ACCESS" "POLICY" if_exists? identifier[name]
      "ON" path_expression[target]
    {
      auto name = MakeNode<ASTPathExpression>(@name, $name);
      // This is a DROP ROW ACCESS POLICY statement.
      auto* drop = MakeNode<ASTDropRowAccessPolicyStatement>(@$, name, $target);
      drop->set_is_if_exists(@if_exists.has_value());
      $$ = drop;
    }
  | "DROP" "TABLE" .TableFunction KW_FUNCTION_IN_TABLE_FUNCTION[fn]?
     if_exists? maybe_dashed_path_expression[path] function_parameters?
    {
      if (@fn.has_value()) {
        // Table functions don't support overloading so this statement doesn't
        // accept any function parameters.
        // (broken link)
        if ($function_parameters.has_value()) {
          return MakeSyntaxError(*@function_parameters,
                                "Syntax error: Parameters are not supported "
                                "for DROP TABLE FUNCTION because table "
                                "functions don't support "
                                "overloading");
        }
        auto* drop = MakeNode<ASTDropTableFunctionStatement>(@$, $path);
        drop->set_is_if_exists(@if_exists.has_value());
        $$ = drop;
      } else {
        // This is a DROP TABLE statement. Table function parameters should
        // not be populated.
        if ($function_parameters.has_value()) {
          return MakeSyntaxError(*@function_parameters,
                                "Syntax error: Unexpected \"(\"");
        }
        auto* drop = MakeNode<ASTDropStatement>(@$, $path);
        drop->set_schema_object_kind(SchemaObjectKind::kTable);
        drop->set_is_if_exists(@if_exists.has_value());
        $$ = drop;
      }
    }
  | "DROP" "SNAPSHOT" "TABLE" opt_if_exists maybe_dashed_path_expression
    {
      auto* drop = MakeNode<ASTDropSnapshotTableStatement>(@$, $5);
      drop->set_is_if_exists($4);
      $$ = drop;
    }
  | "DROP" "DATA_POLICY" opt_if_exists path_expression
    {
      auto* type_ident = node_factory.MakeIdentifier(@2, "DATA_POLICY");
      auto* drop = MakeNode<ASTDropEntityStatement>(@$, type_ident, $4);
      drop->set_is_if_exists($3);
      $$ = drop;
    }
  | "DROP" generic_entity_type opt_if_exists path_expression
    {
      auto* drop = MakeNode<ASTDropEntityStatement>(@$, $2, $4);
      drop->set_is_if_exists($3);
      $$ = drop;
    }
  | "DROP" schema_object_kind[kind] if_exists? path_expression[name]
      function_parameters[params]? ("ON" path_expression[parent_name])[on]? drop_mode?
    {
      if ($kind == SchemaObjectKind::kAggregateFunction) {
        // GoogleSQL does not (yet) support DROP AGGREGATE FUNCTION,
        // though it should as per a recent spec.  Currently, table/aggregate
        // functions are dropped via simple DROP FUNCTION statements.
        return MakeSyntaxError(
            @kind,
            "DROP AGGREGATE FUNCTION is not supported, use DROP FUNCTION");
      }
      if (@params.has_value() && $kind != SchemaObjectKind::kFunction) {
        return MakeSyntaxError(
            *@params,
            "Syntax error: Parameters are only supported for DROP FUNCTION");
      }
      if (@drop_mode.has_value() && $kind != SchemaObjectKind::kSchema) {
        return MakeSyntaxError(
            *@drop_mode,
            absl::StrCat("Syntax error: '",
                         ASTDropStatement::GetSQLForDropMode(*$drop_mode),
                         "' is not supported for DROP ",
                         SchemaObjectKindToName($kind)));
      }
      if (@parent_name.has_value() &&
          $kind != SchemaObjectKind::kIndex &&
          $kind != SchemaObjectKind::kSearchIndex &&
          $kind != SchemaObjectKind::kVectorIndex &&
          $kind != SchemaObjectKind::kAiIndex) {
        absl::string_view kind_name = SchemaObjectKindToName($kind);
        return MakeSyntaxError(
            @on, absl::StrCat("DROP ", absl::AsciiStrToUpper(kind_name),
                              " does not support the ON clause"));
      }
      // This is a DROP <object_type> <object_name> statement.
      switch ($kind) {
        case SchemaObjectKind::kFunction: {
          // If no function parameters are given, then all overloads of the
          // named function will be dropped. Note that "DROP FUNCTION FOO()"
          // will drop the zero-argument overload of foo(), rather than
          // dropping all overloads.
          auto* drop = MakeNode<ASTDropFunctionStatement>(@$, $name, $params);
          drop->set_is_if_exists(@if_exists.has_value());
          $$ = drop;
          return absl::OkStatus();
        }
        case SchemaObjectKind::kMaterializedView: {
          auto* drop = MakeNode<ASTDropMaterializedViewStatement>(@$, $name);
          drop->set_is_if_exists(@if_exists.has_value());
          $$ = drop;
          return absl::OkStatus();
        }
        case SchemaObjectKind::kSearchIndex: {
          auto* drop =
              MakeNode<ASTDropSearchIndexStatement>(@$, $name, $parent_name);
          drop->set_is_if_exists(@if_exists.has_value());
          $$ = drop;
          return absl::OkStatus();
        }
        case SchemaObjectKind::kVectorIndex: {
          auto* drop =
              MakeNode<ASTDropVectorIndexStatement>(@$, $name, $parent_name);
          drop->set_is_if_exists(@if_exists.has_value());
          $$ = drop;
          return absl::OkStatus();
        }
        case SchemaObjectKind::kAiIndex: {
          auto* drop =
              MakeNode<ASTDropAiIndexStatement>(@$, $name, $parent_name);
          drop->set_is_if_exists(@if_exists.has_value());
          $$ = drop;
          return absl::OkStatus();
        }
        case SchemaObjectKind::kIndex:
          if (@parent_name.has_value()) {
            auto* drop =
                MakeNode<ASTDropIndexStatement>(@$, $name, $parent_name);
            drop->set_is_if_exists(@if_exists.has_value());
            $$ = drop;
            return absl::OkStatus();
          }
          // TODO: The DROP INDEX statement without a trailing ON
          // clause produces the old AST node (ASTDropStatement) for backwards
          // compatibility until it is migrated.
          [[fallthrough]];
        default: {
          auto* drop = MakeNode<ASTDropStatement>(@$, $name);
          drop->set_schema_object_kind($kind);
          drop->set_is_if_exists(@if_exists.has_value());
          drop->set_drop_mode($drop_mode.value_or(ASTDropStatement::DropMode::DROP_MODE_UNSPECIFIED));
          $$ = drop;
          return absl::OkStatus();
        }
      }
    }
;

index_type {IndexType}:
    "SEARCH"
    {
      $$ = IndexType::INDEX_SEARCH;
    }
  | "VECTOR"
    {
      $$ = IndexType::INDEX_VECTOR;
    }
  | "AI"
    {
      $$ = IndexType::INDEX_AI;
    }
;

unterminated_non_empty_statement_list {ASTStatementList*}:
    unterminated_statement[stmt]
    {
      if ($stmt->Is<ASTDefineMacroStatement>()) {
        return MakeSyntaxError(
          @stmt,
          "DEFINE MACRO statements cannot be nested under other statements "
          "or blocks.");
      }
      $$ = MakeNode<ASTStatementList>(@$, $stmt);
    }
  | unterminated_non_empty_statement_list[old_list] ";"
      unterminated_statement[new_stmt]
    {
      if ($new_stmt->Is<ASTDefineMacroStatement>()) {
        return MakeSyntaxError(
          @new_stmt,
          "DEFINE MACRO statements cannot be nested under other statements "
          "or blocks.");
      }
      $$ = ExtendNodeRight($old_list, $new_stmt);
    }
;

unterminated_non_empty_top_level_statement_list {ASTStatementList*}:
    unterminated_statement[stmt]
    {
      $$ = MakeNode<ASTStatementList>(@$, $stmt);
    }
  | unterminated_non_empty_top_level_statement_list[old_list] ";"
      unterminated_statement[new_stmt]
    {
      $$ = ExtendNodeRight($old_list, $new_stmt);
    }
;

execute_into_clause {ASTNode*}:
    "INTO" identifier_list
    {
      $$ = MakeNode<ASTExecuteIntoClause>(@$, $identifier_list);
    }
;

execute_using_argument {ASTNode*}:
    expression "AS" identifier
    {
      auto* alias = MakeNode<ASTAlias>(@3, $3);
      $$ = MakeNode<ASTExecuteUsingArgument>(@$, $1, alias);
    }
  | expression
    {
      $$ = MakeNode<ASTExecuteUsingArgument>(@$, $1);
    }
;

execute_using_clause {ASTNode*}:
    "USING" (execute_using_argument separator ",")+[args]
    {
      $$ = MakeNode<ASTExecuteUsingClause>(@args, $args);
    }
;

execute_immediate {ASTNode*}:
    "EXECUTE" "IMMEDIATE" expression execute_into_clause[into]? execute_using_clause[using]?
    {
      $$ = MakeNode<ASTExecuteImmediateStatement>(@$, $expression, $into, $using);
    }
;

script:
    unterminated_non_empty_top_level_statement_list
    {
      $1->set_variable_declarations_allowed(true);
      *ast_node_result = MakeNode<ASTScript>(@$, $1);
    }
  | unterminated_non_empty_top_level_statement_list ";"
    {
      $1->set_variable_declarations_allowed(true);
      *ast_node_result =  MakeNode<ASTScript>(@$, WithEndLocation($1, @$));
    }
  | %empty
    {
      // Resolve to an empty script.
      ASTStatementList* empty_stmt_list = MakeNode<ASTStatementList>(@$);
      *ast_node_result = MakeNode<ASTScript>(@$, empty_stmt_list);
    }
;

statement_list {ASTStatementList*}:
    unterminated_non_empty_statement_list ";"
    {
      $$ = WithEndLocation($1, @$);
    }
  | %empty
    {
      // Resolve to an empty statement list.
      $$ = MakeNode<ASTStatementList>(@$);
    }
;

else_clause {ASTNode*}:
    "ELSE" statement_list
    {
      $$ = $statement_list;
    }
;

elseif_clauses {ASTNode*}:
    "ELSEIF" expression "THEN" statement_list
    {
      ASTElseifClause* elseif_clause = MakeNode<ASTElseifClause>(@$, $2, $4);
      $$ = MakeNode<ASTElseifClauseList>(@$, elseif_clause);
    }
  | elseif_clauses "ELSEIF" expression "THEN" statement_list
    {
      ASTElseifClause* elseif_clause = MakeNode<ASTElseifClause>(MakeLocationRange(@2, @$), $3, $5);
      $$ = ExtendNodeRight($1, elseif_clause);
    }
;

if_statement_unclosed {ASTNode*}:
    "IF" expression "THEN" statement_list[stmts] elseif_clauses[elif]? else_clause[else]?
    {
      $$ = MakeNode<ASTIfStatement>(@$, $expression, $stmts, $elif, $else);
    }
;

if_statement {ASTNode*}:
    if_statement_unclosed
    "END" "IF"
    {
      $$ = WithEndLocation($1, @$);
    }
;

when_then_clauses {ASTNode*}:
    "WHEN" expression "THEN" statement_list
    {
      ASTWhenThenClause* when_then_clause = MakeNode<ASTWhenThenClause>(@$, $2, $4);
      $$ = MakeNode<ASTWhenThenClauseList>(@$, when_then_clause);
    }
  | when_then_clauses "WHEN" expression "THEN" statement_list
    {
      ASTWhenThenClause* when_then_clause = MakeNode<ASTWhenThenClause>(MakeLocationRange(@2, @$), $3, $5);
      $$ = ExtendNodeRight($1, when_then_clause);
    }
;

case_statement {ASTNode*}:
    "CASE" expression? when_then_clauses[when_then] else_clause[else]? "END" "CASE"
    {
      if (!language_options.LanguageFeatureEnabled(FEATURE_CASE_STMT)) {
        return MakeSyntaxError(@1, "Statement CASE...WHEN is not supported");
      }
      $$ = MakeNode<ASTCaseStatement>(@$, $expression, $when_then, $else);
    }
;

begin_end_block {ASTNode*}:
    "BEGIN" statement_list[stmts] exception_handler[handlers]? "END"
    {
      $stmts->set_variable_declarations_allowed(true);
      $$ = MakeNode<ASTBeginEndBlock>(@$, $stmts, $handlers);
    }
;

exception_handler {ASTNode*}:
    "EXCEPTION" "WHEN"[when] "ERROR" "THEN" statement_list
    {
      auto* handler = MakeNode<ASTExceptionHandler>(@$, $statement_list);
      WithStartLocation(handler, @when.start());
      $$ = MakeNode<ASTExceptionHandlerList>(@$, handler);
    }
;

default_expression {ASTNode*}:
  "DEFAULT" column_list_spec
    {
      $$ = $column_list_spec;
    }
  |
    "DEFAULT" expression
    {
      $$ = $expression;
    }
;

opt_default_expression {ASTNode*}:
    default_expression?
    {
      $$ = $default_expression.value_or(nullptr);
    }
;

identifier_list {ASTNode*}:
    (identifier separator ",")+[identifier_list]
    {
      $$ = MakeNode<ASTIdentifierList>(@$, $identifier_list);
    }
;

variable_declaration {ASTNode*}:
    "DECLARE" identifier_list type default_expression?
    {
      $$ = MakeNode<ASTVariableDeclaration>(@$, $identifier_list, $type, $default_expression);
    }
  | "DECLARE" identifier_list "DEFAULT" expression
    {
      $$ = MakeNode<ASTVariableDeclaration>(@$, $identifier_list, $expression);
    }
;

loop_statement {ASTNode*}:
    {
      OVERRIDE_NEXT_TOKEN_LOOKBACK(KW_LOOP, LB_OPEN_STATEMENT_BLOCK);
    }
    "LOOP"[loop] statement_list "END" "LOOP"
    {
      $$ = MakeNode<ASTWhileStatement>(@$, $statement_list);
    }
;

while_statement {ASTNode*}:
    "WHILE" expression
    {
      OVERRIDE_NEXT_TOKEN_LOOKBACK(KW_DO, LB_OPEN_STATEMENT_BLOCK);
    }
    "DO" statement_list "END" "WHILE"
    {
      $$ = MakeNode<ASTWhileStatement>(@$, $expression, $statement_list);
    }
;

until_clause {ASTNode*}:
    "UNTIL" expression
    {
      $$ = MakeNode<ASTUntilClause>(@$, $2);
    }
;

repeat_statement {ASTNode*}:
    {
      OVERRIDE_NEXT_TOKEN_LOOKBACK(KW_REPEAT, LB_OPEN_STATEMENT_BLOCK);
    }
    "REPEAT"[repeat] statement_list until_clause "END" "REPEAT"
    {
      if (!language_options.LanguageFeatureEnabled(FEATURE_REPEAT)) {
        return MakeSyntaxError(@repeat, "REPEAT is not supported");
      }
      $$ = MakeNode<ASTRepeatStatement>(@$, $statement_list, $until_clause);
    }
;

for_in_statement {ASTNode*}:
    "FOR" identifier "IN" parenthesized_query[query]
    {
      OVERRIDE_NEXT_TOKEN_LOOKBACK(KW_DO, LB_OPEN_STATEMENT_BLOCK);
    }
    "DO" statement_list "END" "FOR"
    {
      if (!language_options.LanguageFeatureEnabled(FEATURE_FOR_IN)) {
        return MakeSyntaxError(@1, "FOR...IN is not supported");
      }
      $$ = MakeNode<ASTForInStatement>(@$, $identifier, $query,
                                       $statement_list);
    }
;

break_statement {ASTNode*}:
    "BREAK" opt_identifier
    {
      GOOGLESQL_RETURN_IF_ERROR(ValidateLabelSupport($2, @2));
      ASTBreakStatement* stmt;
      if ($2 == nullptr) {
        stmt = MakeNode<ASTBreakStatement>(@$);
      } else {
        auto label = MakeNode<ASTLabel>(@2, $2);
        stmt = MakeNode<ASTBreakStatement>(@$, label);
      }
      stmt->set_keyword(ASTBreakContinueStatement::BREAK);
      $$ = stmt;
    }
  | "LEAVE" opt_identifier
    {
      GOOGLESQL_RETURN_IF_ERROR(ValidateLabelSupport($2, @2));
      ASTBreakStatement* stmt;
      if ($2 == nullptr) {
        stmt = MakeNode<ASTBreakStatement>(@$);
      } else {
        auto label = MakeNode<ASTLabel>(@2, $2);
        stmt = MakeNode<ASTBreakStatement>(@$, label);
      }
      stmt->set_keyword(ASTBreakContinueStatement::LEAVE);
      $$ = stmt;
    }
;

continue_statement {ASTNode*}:
    "CONTINUE" opt_identifier
    {
      GOOGLESQL_RETURN_IF_ERROR(ValidateLabelSupport($2, @2));
      ASTContinueStatement* stmt;
      if ($2 == nullptr) {
        stmt = MakeNode<ASTContinueStatement>(@$);
      } else {
        auto label = MakeNode<ASTLabel>(@2, $2);
        stmt = MakeNode<ASTContinueStatement>(@$, label);
      }
      stmt->set_keyword(ASTBreakContinueStatement::CONTINUE);
      $$ = stmt;
    }
  | "ITERATE" opt_identifier
    {
      GOOGLESQL_RETURN_IF_ERROR(ValidateLabelSupport($2, @2));
      ASTContinueStatement* stmt;
      if ($2 == nullptr) {
        stmt = MakeNode<ASTContinueStatement>(@$);
      } else {
        auto label = MakeNode<ASTLabel>(@2, $2);
        stmt = MakeNode<ASTContinueStatement>(@$, label);
      }
      stmt->set_keyword(ASTBreakContinueStatement::ITERATE);
      $$ = stmt;
    }
;

// TODO: add expression to RETURN as defined in
// (broken link) section "RETURN Statement".
return_statement {ASTNode*}:
    "RETURN"
    {
      $$ = MakeNode<ASTReturnStatement>(@$);
    }
;

raise_statement {ASTNode*}:
    "RAISE"
    {
      $$ = MakeNode<ASTRaiseStatement>(@$);
    }
  | "RAISE" "USING" "MESSAGE" "=" expression
    {
      $$ = MakeNode<ASTRaiseStatement>(@$, $5);
    }
;

next_statement_kind:
    next_statement_kind_without_hint[kind]
    {
      *ast_node_result = nullptr;
      ast_statement_properties->node_kind = $kind;
    }
  | hint
    {
      OVERRIDE_CURRENT_TOKEN_LOOKBACK(@hint, LB_END_OF_STATEMENT_LEVEL_HINT);
    }
    next_statement_kind_without_hint[kind]
    {
      if ($kind == ASTDefineMacroStatement::kConcreteNodeKind) {
        return MakeSyntaxError(
          @hint, "Hints are not allowed on DEFINE MACRO statements.");
      }
      *ast_node_result = $hint;
      ast_statement_properties->node_kind = $kind;
    }
;

next_statement_kind_parenthesized_select {ASTNodeKind}:
    "(" next_statement_kind_parenthesized_select
    {
      $$ = $2;
    }
  | "SELECT"
    {
      $$ = ASTQueryStatement::kConcreteNodeKind;
    }
  | "WITH"
    {
      $$ = ASTQueryStatement::kConcreteNodeKind;
    }
    // FROM is always treated as indicating the statement is a query, even
    // if the syntax is not enabled.  This is okay because a statement
    // starting with FROM couldn't be anything else, and it'll be reasonable
    // to give errors a statement starting with FROM being an invalid query.
  | "FROM"
    {
      $$ = ASTQueryStatement::kConcreteNodeKind;
    }
  | KW_TABLE_FOR_TABLE_CLAUSE
    {
      $$ = ASTQueryStatement::kConcreteNodeKind;
    }
;

next_statement_kind_table:
    "TABLE" .TableFunction
    {
      // Set statement properties node_kind before finishing parsing, so that
      // in the case of a syntax error after "TABLE", ParseNextStatementKind()
      // still returns ASTCreateTableStatement::kConcreteNodeKind.
      ast_statement_properties->node_kind =
          ASTCreateTableStatement::kConcreteNodeKind;
    }
;

next_statement_kind_create_modifiers {ASTNodeKind}:
    opt_or_replace opt_create_scope
    {
      ast_statement_properties->create_scope = $2;
    }
;

next_statement_kind_without_hint {ASTNodeKind}:
    "EXPLAIN"
    {
      $$ = ASTExplainStatement::kConcreteNodeKind;
    }
  | next_statement_kind_parenthesized_select
  | "DEFINE" "TABLE"
    {
      $$ = ASTDefineTableStatement::kConcreteNodeKind;
    }
  | KW_DEFINE_FOR_MACROS macro_visibility? "MACRO"
    {
      $$ = ASTDefineMacroStatement::kConcreteNodeKind;
    }
  | "EXECUTE" "IMMEDIATE"
    {
      $$ = ASTExecuteImmediateStatement::kConcreteNodeKind;
    }
  | "EXPORT" "DATA"
    {
      $$ = ASTExportDataStatement::kConcreteNodeKind;
    }
  | "EXPORT" "MODEL"
    {
      $$ = ASTExportModelStatement::kConcreteNodeKind;
    }
  | "EXPORT" "TABLE" "FUNCTION"? "METADATA"
    {
      $$ = ASTExportMetadataStatement::kConcreteNodeKind;
    }
  | "INSERT"
    {
      $$ = ASTInsertStatement::kConcreteNodeKind;
    }
  | "UPDATE"
    {
      $$ = ASTUpdateStatement::kConcreteNodeKind;
    }
  | "DELETE"
    {
      $$ = ASTDeleteStatement::kConcreteNodeKind;
    }
  | "MERGE"
    {
      $$ = ASTMergeStatement::kConcreteNodeKind;
    }
  | "CLONE" "DATA"
    {
      $$ = ASTCloneDataStatement::kConcreteNodeKind;
    }
  | "LOAD" "DATA"
    {
      $$ = ASTAuxLoadDataStatement::kConcreteNodeKind;
    }
  | describe_keyword
    {
      $$ = ASTDescribeStatement::kConcreteNodeKind;
    }
  | "SHOW"
    {
      $$ = ASTShowStatement::kConcreteNodeKind;
    }
  | "DROP" "PRIVILEGE"
    {
      $$ = ASTDropPrivilegeRestrictionStatement::kConcreteNodeKind;
    }
  | "DROP" "ALL" "ROW" access? "POLICIES"
    {
      $$ = ASTDropAllRowAccessPoliciesStatement::kConcreteNodeKind;
    }
  | "DROP" "ROW" "ACCESS" "POLICY"
    {
      $$ = ASTDropRowAccessPolicyStatement::kConcreteNodeKind;
    }
  | "DROP" "TABLE" .TableFunction KW_FUNCTION_IN_TABLE_FUNCTION[fn]?
    set(FollowsTableFunction)
    {
      if (@fn.has_value()) {
        $$ = ASTDropTableFunctionStatement::kConcreteNodeKind;
      } else {
        $$ = ASTDropStatement::kConcreteNodeKind;
      }
    }
  | "DROP" "SNAPSHOT" "TABLE"
    {
      $$ = ASTDropSnapshotTableStatement::kConcreteNodeKind;
    }
  | "DROP" "DATA_POLICY"
    {
      $$ = ASTDropEntityStatement::kConcreteNodeKind;
    }
  | "DROP" generic_entity_type
    {
      $$ = ASTDropEntityStatement::kConcreteNodeKind;
    }
  | "DROP" schema_object_kind[kind] if_exists? path_expression ("ON"[on])?
    set(";" | eoi | "IF" | first path_expression | "(" | "CASCADE" | "RESTRICT")
    {
      switch ($kind) {
        case SchemaObjectKind::kFunction:
          $$ = ASTDropFunctionStatement::kConcreteNodeKind;
          break;
        case SchemaObjectKind::kMaterializedView:
          $$ = ASTDropMaterializedViewStatement::kConcreteNodeKind;
          break;
        case SchemaObjectKind::kSearchIndex:
          $$ = ASTDropSearchIndexStatement::kConcreteNodeKind;
          break;
        case SchemaObjectKind::kVectorIndex:
          $$ = ASTDropVectorIndexStatement::kConcreteNodeKind;
          break;
        case SchemaObjectKind::kAiIndex:
          $$ = ASTDropAiIndexStatement::kConcreteNodeKind;
          break;
        case SchemaObjectKind::kIndex:
          if (@on.has_value()) {
            $$ = ASTDropIndexStatement::kConcreteNodeKind;
          } else {
            // TODO: The DROP INDEX statement without a trailing
            // ON clause produces the old AST node (ASTDropStatement) for
            // backwards compatibility until it is migrated.
            $$ = ASTDropStatement::kConcreteNodeKind;
          }
          break;
        default:
          $$ = ASTDropStatement::kConcreteNodeKind;
          break;
      }
    }
  | "GRANT"
    {
      $$ = ASTGrantStatement::kConcreteNodeKind;
    }
  | "GRAPH"
    {
      $$ = ASTQueryStatement::kConcreteNodeKind;
      ast_statement_properties->is_top_level_graph_statement_syntax = true;
    }
  | "REVOKE"
    {
      $$ = ASTRevokeStatement::kConcreteNodeKind;
    }
  | "RENAME"
    {
      $$ = ASTRenameStatement::kConcreteNodeKind;
    }
  | "START" "TRANSACTION"
    {
      $$ = ASTBeginStatement::kConcreteNodeKind;
    }
  | "BEGIN"
    {
      $$ = ASTBeginStatement::kConcreteNodeKind;
    }
  | "SET" "TRANSACTION" identifier
    {
      $$ = ASTSetTransactionStatement::kConcreteNodeKind;
    }
  | "SET" identifier "="
    {
      $$ = ASTSingleAssignment::kConcreteNodeKind;
    }
  | "SET" named_parameter_expression "="
    {
      $$ = ASTParameterAssignment::kConcreteNodeKind;
    }
  | "SET" system_variable_expression "="
    {
      $$ = ASTSystemVariableAssignment::kConcreteNodeKind;
    }
  | "SET" "("
    {
      $$ = ASTAssignmentFromStruct::kConcreteNodeKind;
    }
  | "COMMIT"
    {
      $$ = ASTCommitStatement::kConcreteNodeKind;
    }
  | "ROLLBACK"
    {
      $$ = ASTRollbackStatement::kConcreteNodeKind;
    }
  | "START" "BATCH"
    {
      $$ = ASTStartBatchStatement::kConcreteNodeKind;
    }
  | "RUN" path_expression %prec RUN_PRECEDENCE
    {
      absl::string_view first_identifier =
          $path_expression->child(0)->GetAs<ASTIdentifier>()->GetAsStringView();
      if ($path_expression->num_children() == 1
          && googlesql_base::CaseEqual(first_identifier, "BATCH")) {
        $$ = ASTRunBatchStatement::kConcreteNodeKind;
      } else {
        $$ = ASTRunStatement::kConcreteNodeKind;
      }
    }
  | "RUN" path_expression "("
    {
      $$ = ASTRunStatement::kConcreteNodeKind;
    }
  | "RUN" string_literal_component
    {
      $$ = ASTRunStatement::kConcreteNodeKind;
    }
  | "ABORT" "BATCH"
    {
      $$ = ASTAbortBatchStatement::kConcreteNodeKind;
    }
  | "ALTER" "APPROX" "VIEW"
    {
      $$ = ASTAlterApproxViewStatement::kConcreteNodeKind;
    }
  | "ALTER" "CONNECTION"
    {
      $$ = ASTAlterConnectionStatement::kConcreteNodeKind;
    }
  | "ALTER" "DATABASE"
    {
      $$ = ASTAlterDatabaseStatement::kConcreteNodeKind;
    }
  | "ALTER" index_type "INDEX"
    {
      $$ = ASTAlterIndexStatement::kConcreteNodeKind;
    }
  | "ALTER" "SCHEMA"
    {
      $$ = ASTAlterSchemaStatement::kConcreteNodeKind;
    }
  | "ALTER" "SEQUENCE"
    {
      $$ = ASTAlterSequenceStatement::kConcreteNodeKind;
    }
  | "ALTER" "EXTERNAL" "SCHEMA"
    {
      $$ = ASTAlterExternalSchemaStatement::kConcreteNodeKind;
    }
  | "ALTER" "TABLE"
    {
      $$ = ASTAlterTableStatement::kConcreteNodeKind;
    }
  | "ALTER" "PRIVILEGE"
    {
      $$ = ASTAlterPrivilegeRestrictionStatement::kConcreteNodeKind;
    }
  | "ALTER" "ROW"
    {
      $$ = ASTAlterRowAccessPolicyStatement::kConcreteNodeKind;
    }
  | "ALTER" "ALL" "ROW" "ACCESS" "POLICIES"
    {
      $$ =
          ASTAlterAllRowAccessPoliciesStatement::kConcreteNodeKind;
    }
  | "ALTER" "VIEW"
    {
      $$ = ASTAlterViewStatement::kConcreteNodeKind;
    }
  | "ALTER" "MATERIALIZED" "VIEW"
    {
      $$ = ASTAlterMaterializedViewStatement::kConcreteNodeKind;
    }
  | "ALTER" "DATA_POLICY"
    {
      if (language_options.LanguageFeatureEnabled(FEATURE_TAG_DATA_POLICY_DDL)) {
        $$ = ASTAlterDataPolicyStatement::kConcreteNodeKind;
      } else {
        $$ = ASTAlterEntityStatement::kConcreteNodeKind;
      }
    }
  | "ALTER" generic_entity_type
    {
      $$ = ASTAlterEntityStatement::kConcreteNodeKind;
    }
  | "ALTER" "MODEL"
    {
      $$ = ASTAlterModelStatement::kConcreteNodeKind;
    }
  | "CREATE" "DATABASE"
    {
      $$ = ASTCreateDatabaseStatement::kConcreteNodeKind;
    }
  | "CREATE" next_statement_kind_create_modifiers "CONNECTION"
    {
      $$ = ASTCreateConnectionStatement::kConcreteNodeKind;
    }
  | "CREATE" next_statement_kind_create_modifiers "AGGREGATE"? "CONSTANT"
    {
      $$ = ASTCreateConstantStatement::kConcreteNodeKind;
    }
  | "CREATE" next_statement_kind_create_modifiers "AGGREGATE"? "FUNCTION"
    {
      $$ = ASTCreateFunctionStatement::kConcreteNodeKind;
    }
  | "CREATE" next_statement_kind_create_modifiers "PROCEDURE"
    {
      $$ = ASTCreateProcedureStatement::kConcreteNodeKind;
    }
  | "CREATE" "LOCALITY" "GROUP"
    {
      $$ = ASTCreateLocalityGroupStatement::kConcreteNodeKind;
    }
  | "CREATE" opt_or_replace "UNIQUE"? "NULL_FILTERED"? index_type?
      "INDEX"
    {
      $$ = ASTCreateIndexStatement::kConcreteNodeKind;
    }
  | "CREATE" opt_or_replace "SCHEMA"
    {
      $$ = ASTCreateSchemaStatement::kConcreteNodeKind;
    }
  | "CREATE" next_statement_kind_create_modifiers "DATA_POLICY"
    {
      if (language_options.LanguageFeatureEnabled(FEATURE_TAG_DATA_POLICY_DDL)) {
        $$ = ASTCreateDataPolicyStatement::kConcreteNodeKind;
      } else {
        $$ = ASTCreateEntityStatement::kConcreteNodeKind;
      }
    }
  | "CREATE" next_statement_kind_create_modifiers generic_entity_type
    {
      $$ = ASTCreateEntityStatement::kConcreteNodeKind;
    }
  | "CREATE"
      next_statement_kind_create_modifiers
      next_statement_kind_table opt_if_not_exists
      maybe_dashed_path_expression opt_table_element_list
      opt_like_path_expression opt_clone_table opt_copy_table
      opt_default_collate_clause
      opt_partition_by_clause_no_hint
      opt_cluster_by_clause_no_hint opt_with_connection_clause opt_options_list
      // The "AS" query is the very last thing in the create table statement, so
      // the statement must end either with the query, a semicolon, or the end
      // of the input.
      ("AS"[as_for_query] | semicolon_or_eoi)
    {
      ast_statement_properties->is_create_table_as_select =
          @as_for_query.has_value();
      $$ = ASTCreateTableStatement::kConcreteNodeKind;
    }
  | "CREATE" next_statement_kind_create_modifiers "SEQUENCE"
    {
      $$ = ASTCreateSequenceStatement::kConcreteNodeKind;
    }
  | "CREATE" next_statement_kind_create_modifiers "LIVE" "TABLE" opt_if_not_exists
      maybe_dashed_path_expression opt_table_element_list
      opt_default_collate_clause opt_partition_by_clause_no_hint
      opt_cluster_by_clause_no_hint opt_with_connection_clause opt_options_list
      ("AS"[as_for_query] | semicolon_or_eoi)
    {
      ast_statement_properties->is_create_table_as_select =
          @as_for_query.has_value();
      $$ = ASTCreateLiveTableStatement::kConcreteNodeKind;
    }
  | "CREATE" next_statement_kind_create_modifiers "MODEL"
    {
      $$ = ASTCreateModelStatement::kConcreteNodeKind;
    }
  | "CREATE" next_statement_kind_create_modifiers
      "TABLE" .TableFunction KW_FUNCTION_IN_TABLE_FUNCTION[fn]
    {
      $$ = ASTCreateTableFunctionStatement::kConcreteNodeKind;
    }
  | "CREATE" next_statement_kind_create_modifiers "EXTERNAL" "TABLE"
    {
      $$ = ASTCreateExternalTableStatement::kConcreteNodeKind;
    }
  | "CREATE" next_statement_kind_create_modifiers "EXTERNAL" "SCHEMA"
    {
      $$ = ASTCreateExternalSchemaStatement::kConcreteNodeKind;
    }
  | "CREATE" opt_or_replace "PRIVILEGE"
    {
      $$ = ASTCreatePrivilegeRestrictionStatement::kConcreteNodeKind;
    }
  | "CREATE" opt_or_replace "ROW" access? "POLICY"
    {
      $$ = ASTCreateRowAccessPolicyStatement::kConcreteNodeKind;
    }
  | "CREATE" next_statement_kind_create_modifiers "RECURSIVE"? "VIEW"
    {
      $$ = ASTCreateViewStatement::kConcreteNodeKind;
    }
  | "CREATE" opt_or_replace "APPROX" "RECURSIVE"? "VIEW"
    {
      $$ = ASTCreateApproxViewStatement::kConcreteNodeKind;
    }
  | "CREATE" opt_or_replace "MATERIALIZED" "RECURSIVE"? "VIEW"
    {
      $$ = ASTCreateMaterializedViewStatement::kConcreteNodeKind;
    }
  | "CREATE" opt_or_replace "SNAPSHOT" "SCHEMA"
    {
      $$ = ASTCreateSnapshotStatement::kConcreteNodeKind;
    }
  | "CREATE" opt_or_replace "SNAPSHOT" "TABLE"
    {
      $$ = ASTCreateSnapshotTableStatement::kConcreteNodeKind;
    }
  | "CREATE" next_statement_kind_create_modifiers "PROPERTY" "GRAPH"
    .PropertyGraphType KW_TYPE_IN_PROPERTY_GRAPH_TYPE[type_kw]?
    set(FollowsGraph)
    {
      if (@type_kw.has_value()) {
        $$ = ASTCreatePropertyGraphTypeStatement::kConcreteNodeKind;
      } else {
        $$ = ASTCreatePropertyGraphStatement::kConcreteNodeKind;
      }
    }
  | "CALL"
    {
      $$ = ASTCallStatement::kConcreteNodeKind;
    }
  | "RETURN"
    {
      $$ = ASTReturnStatement::kConcreteNodeKind;
    }
  | "IMPORT"
    {
      $$ = ASTImportStatement::kConcreteNodeKind;
    }
  | "MODULE"
    {
      $$ = ASTModuleStatement::kConcreteNodeKind;
    }
  | "ANALYZE"
    {
      $$ = ASTAnalyzeStatement::kConcreteNodeKind;
    }
  | "ASSERT"
    {
      $$ = ASTAssertStatement::kConcreteNodeKind;
    }
  | "TRUNCATE"
    {
      $$ = ASTTruncateStatement::kConcreteNodeKind;
    }
  | "IF"
    {
      $$ = ASTIfStatement::kConcreteNodeKind;
    }
  | "CASE"
    {
      $$ = ASTCaseStatement::kConcreteNodeKind;
    }
  | "WHILE"
    {
      $$ = ASTWhileStatement::kConcreteNodeKind;
    }
  | "LOOP"
    {
      $$ = ASTWhileStatement::kConcreteNodeKind;
    }
  | "DECLARE"
    {
      $$ = ASTVariableDeclaration::kConcreteNodeKind;
    }
  | "BREAK"
    {
      $$ = ASTBreakStatement::kConcreteNodeKind;
    }
  | "LEAVE"
    {
      $$ = ASTBreakStatement::kConcreteNodeKind;
    }
  | "CONTINUE"
    {
      $$ = ASTContinueStatement::kConcreteNodeKind;
    }
  | "ITERATE"
    {
      $$ = ASTContinueStatement::kConcreteNodeKind;
    }
  | "RAISE"
    {
      $$ = ASTRaiseStatement::kConcreteNodeKind;
    }
  | "FOR"
    {
      $$ = ASTForInStatement::kConcreteNodeKind;
    }
  | "REPEAT"
    {
      $$ = ASTRepeatStatement::kConcreteNodeKind;
    }
  | label ":" "BEGIN"
    {
      $$ = ASTBeginStatement::kConcreteNodeKind;
    }
  | label ":" "LOOP"
    {
      $$ = ASTWhileStatement::kConcreteNodeKind;
    }
  | label ":" "WHILE"
    {
      $$ = ASTWhileStatement::kConcreteNodeKind;
    }
  | label ":" "FOR"
    {
      $$ = ASTForInStatement::kConcreteNodeKind;
    }
  | label ":" "REPEAT"
    {
      $$ = ASTRepeatStatement::kConcreteNodeKind;
    }
  | "UNDROP" schema_object_kind set(FollowsTableFunction)
    {
      $$ = ASTUndropStatement::kConcreteNodeKind;
    }
  | "|>"
    {
      $$ = ASTSubpipelineStatement::kConcreteNodeKind;
    }
;

// Spanner-specific non-terminal definitions
spanner_primary_key {ASTNode*}:
    "PRIMARY" "KEY" primary_key_element_list
    {
      if (!language_options.LanguageFeatureEnabled(
          FEATURE_SPANNER_LEGACY_DDL)) {
        return MakeSyntaxError(@1, "PRIMARY KEY must be defined in the "
                   "table element list as column attribute or constraint.");
      }
      $$ = MakeNode<ASTPrimaryKey>(@$, $3);
    }
;

spanner_index_interleave_clause {ASTSpannerInterleaveClause*}:
    "," "INTERLEAVE" "IN" maybe_dashed_path_expression[target]
    {
      if (!language_options.LanguageFeatureEnabled(
          FEATURE_SPANNER_LEGACY_DDL)) {
        return MakeSyntaxError(@1, "Syntax error: Expected end of input but "
            "got \",\"");
      }
      $$ = MakeNode<ASTSpannerInterleaveClause>(@$, $target);
      $$->set_type(ASTSpannerInterleaveClause::IN);
    }
;

opt_spanner_interleave_in_parent_clause {ASTNode*}:
    "," "INTERLEAVE" "IN" "PARENT" maybe_dashed_path_expression
    opt_foreign_key_on_delete
    {
      if (!language_options.LanguageFeatureEnabled(
          FEATURE_SPANNER_LEGACY_DDL)) {
        return MakeSyntaxError(@1, "Syntax error: Expected end of input but "
            "got \",\"");
      }

      auto* clause = MakeNode<ASTSpannerInterleaveClause>(@$, $5);
      clause->set_action($6);
      clause->set_type(ASTSpannerInterleaveClause::IN_PARENT);
      $$ = clause;
    }
  | %empty
    {
      $$ = nullptr;
    }
;

opt_spanner_table_options {ASTNode*}:
    spanner_primary_key opt_spanner_interleave_in_parent_clause
    {
      if (!language_options.LanguageFeatureEnabled(
          FEATURE_SPANNER_LEGACY_DDL)) {
        return MakeSyntaxError(@1, "PRIMARY KEY must be defined in the "
            "table element list as column attribute or constraint.");
      }

      $$ = MakeNode<ASTSpannerTableOptions>(@$, $1, $2);
    }
  | %empty
    {
      $$ = nullptr;
    }
;

// Feature-checking in this rule would make parser reduce and error out
// too early, so we rely on the check in spanner_alter_column_action.
spanner_generated_or_default {ASTNode*}:
    "AS" "(" expression ")" "STORED"
    {
      auto* node = MakeNode<ASTGeneratedColumnInfo>(@$, $3);
      node->set_stored_mode(ASTGeneratedColumnInfo::STORED);
      $$ = node;
    }
  | default_column_info
;

spanner_not_null_attribute {ASTNode*}:
    not_null_column_attribute
    {
      // Feature-checking here would make parser reduce and error out
      // too early, so we rely on the check in spanner_alter_column_action.
      $$ = MakeNode<ASTColumnAttributeList>(@$, $not_null_column_attribute);
    }
;

spanner_alter_column_action {ASTNode*}:
    "ALTER" "COLUMN" opt_if_exists identifier column_schema_inner
    spanner_not_null_attribute? spanner_generated_or_default?
    opt_options_list
    {
      if (!language_options.LanguageFeatureEnabled(
          FEATURE_SPANNER_LEGACY_DDL)) {
        return MakeSyntaxError(@column_schema_inner,
            "Expected keyword DROP or keyword SET but got identifier");
      }
      if ($opt_if_exists) {
        return MakeSyntaxError(@opt_if_exists,
            "Syntax error: IF EXISTS is not supported");
      }
      auto* schema = ExtendNodeRight($column_schema_inner,
            @$.end(),
            $spanner_generated_or_default,
            $spanner_not_null_attribute,
            $opt_options_list
          );
      auto* column = MakeNode<ASTColumnDefinition>(@$,
          $identifier, schema);
      $$ = MakeNode<ASTSpannerAlterColumnAction>(@$,
          WithStartLocation(column, @identifier));
    }
;

spanner_set_on_delete_action {ASTNode*}:
    "SET" "ON" "DELETE" foreign_key_action
    {
      if (!language_options.LanguageFeatureEnabled(
            FEATURE_SPANNER_LEGACY_DDL)) {
        return MakeSyntaxError(@2, "Syntax error: Unexpected keyword ON");
      }
      auto* node = MakeNode<ASTSpannerSetOnDeleteAction>(@$);
      node->set_action($foreign_key_action);
      $$ = node;
    }
;

create_sequence_statement {ASTCreateSequenceStatement*}:
    "CREATE" opt_or_replace "SEQUENCE" opt_if_not_exists path_expression
    options?
    {
      $$ = MakeNode<ASTCreateSequenceStatement>(@$, $path_expression, $options);
      $$->set_is_or_replace($opt_or_replace);
      $$->set_is_if_not_exists($opt_if_not_exists);
    }
;

%%

{{define "lexerHeaderIncludes"}}
#include <cstdint>
#include <ostream>

#include "{{.Options.AbslIncludePrefix}}/strings/string_view.h"
#include "{{.Options.DirIncludePrefix}}{{.Options.FilenamePrefix}}token.h"
#include "googlesql/parser/parser_mode.h"
#include "googlesql/public/parse_location.h"
#include "googlesql/common/errors.h"
#include "absl/strings/escaping.h"
{{end}}

{{define "stateVars"}}
  void SetOverrideError(const ParseLocationRange& location,
                        absl::string_view error_message) {
    override_error_ = MakeSqlErrorAtPoint(location.start()) << error_message;
  }

  void SetUnclosedError(absl::string_view kind) {
    SetOverrideError(LastTokenLocationWithStartOffset(),
                     /*error_message=*/absl::StrCat("Syntax error: Unclosed ",
                                                    kind));
  }

  void SetTripleUnclosedError(absl::string_view kind) {
    SetUnclosedError(/*kind=*/absl::StrCat("triple-quoted ", kind));
  }

  // Similar to LastTokenLocation(), but the start and end offsets are adjusted
  // to account for the fact that the lexer is initialized with a substring of
  // the input.
  ParseLocationRange LastTokenLocationWithStartOffset() const {
    ParseLocationRange location = LastTokenLocation();
    location.mutable_start().SetByteOffset(location.start().GetByteOffset() +
                                           start_offset_);
    location.mutable_end().SetByteOffset(location.end().GetByteOffset() +
                                         start_offset_);
    return location;
  }

  // Stores the error countered during lexing.
  absl::Status override_error_;

  // The input filename.
  absl::string_view filename_;

  // This is the additional offset to be added to the start and end offsets
  // of the tokens returned by LastTokenLocationWithStartOffset() to account for
  // the fact that the lexer is initialized with a substring of the input.
  //
  // `start_offset_` can be non-zero when parsing multiple statements. For
  // example,
  //
  // ```
  // SELECT 1;
  // SELECT 2;
  // ```
  //
  // A new lexer is created for each statement, so for "SELECT 2;", the
  // `start_offset_` is 10 and `input_source_` is "SELECT 2;", i.e. just the
  // substring. As a result, when `start_offset_` is not zero, line numbers are
  // not with respect to the entire input, but with respect to the substring
  // "SELECT 2;", and should not be used directly in the error messages.
  //
  // Using the function Rewind() can make sure the line numbers and the offsets
  // stay consistent w.r.t. the entire input, but its time complexity is O(n)
  // where n is the input length, making the parser time complexity become
  // O(n^2).
  int start_offset_ = 0;

  friend class GoogleSqlTokenizer;
{{end}}

{{- define "parserPublicDecls" -}}
  // Returns the expected tokens following a syntax error. Before a syntax error
  // occurs, this returns an empty vector. The vector contains one or more
  // sets of tokens, each one from a different state the parser has passed
  // through since the last token was consumed. Each token in each of the sets
  // is a potential expected token. We preserve the structure (keeping the
  // followsets of each set separate) to enable error message generation to
  // consider that when generating the error message.
  const std::vector<std::set<Token>>& expected_tokens() const {
    return expected_tokens_;
  }
{{- end -}}

{{- define "parserPrivateDecls" -}}

  absl::Status ValidateLabelSupport(
      ASTNode* node, const ParseLocationRange& location) {
    if (node == nullptr || language_options.LanguageFeatureEnabled(
                              FEATURE_SCRIPT_LABEL)) {
      return absl::OkStatus();
    }
    return MakeSyntaxError(location, "Script labels are not supported");
  }

  // Generate a parse error if macro expansions are disabled (`kNone`). This is
  // used to prevent the parser from silently ignoring macro constructs when the
  // the system isn't configured to use them.
  absl::Status ValidateMacroSupport(const ParseLocationRange& location) {
    if(macro_expansion_mode == MacroExpansionMode::kNone) {
      return MakeSyntaxError(
          location,
          "Syntax error: DEFINE MACRO statements are not supported because "
          "macro expansions are disabled");
    }
    return absl::OkStatus();
  }

  template <typename T, typename... TChildren>
  T* MakeNode(const ParseLocationRange& location, TChildren... children) {
    T* result = node_factory.CreateASTNode<T>(location);
    ExtendNodeRight(result, location.end(), children...);
    return result;
  }

  absl::StatusOr<ASTIdentifier*> MakeIdentifierFromKeyword(
      const ParseLocationRange& location, absl::string_view name) {
    GOOGLESQL_RETURN_IF_ERROR(
        AddWarningIfReserved(name, location, language_options, warning_sink));
    return node_factory.MakeIdentifier(location, name, /*is_quoted=*/false);
  }

  template <typename T, typename TElements>
  std::optional<T*> MakeListNode(const ParseLocationRange& location,
                                const std::vector<TElements>& children) {
    if (children.empty()) {
      return std::nullopt;
    }
    return MakeNode<T>(location, children);
  }

  // Get the followset for the given state.
  std::set<Token> GetFollowset(int32_t state);

  // Used to assign positions to ? parameters.
  int32_t previous_positional_parameter_position_ = 0;

  // Stores the tokens in the followset after a syntax error.
  std::vector<std::set<Token>> expected_tokens_;

{{ end -}}

{{ define "onAfterParser" -}}
// Get the followset for the given state.
std::set<Token> Parser::GetFollowset(int32_t state) {
  std::set<Token> followset;
  if (tmAction[state] > tmActionBase) {
    for (int32_t i = 0; i < static_cast<int32_t>(Token::NumTokens); i++) {
      // Skip ERROR and INVALID_TOKEN.
      if (i == 1 || i == 2) {
        continue;
      }
      int32_t action = tmAction[state];
      int32_t pos = action + i;
      if (pos >= 0 && pos < tmTableLen && tmCheck[pos] == i) {
        action = tmTable[pos];
      } else {
        action = tmDefAct[state];
      }
      if (action < -1) { // this is a shift into state = -2-action
        followset.insert(static_cast<Token>(i));
      }
    }
  }
  return followset;
}
{{ end -}}

{{ define "returnParserErr" -}}
    expected_tokens_.clear();
    expected_tokens_.reserve(1);
    expected_tokens_.push_back(GetFollowset(stack.back().state));
    return absl::InvalidArgumentError(
        absl::StrCat("syntax error, unexpected ",
                     tokenName[next_symbol_.symbol]));
{{ end -}}

{{ define "parserHeaderIncludes" -}}
#include <array>
#include <cstdint>
#include <functional>
#include <ostream>
#include <string>
#include <utility>
#include <vector>

#include "googlesql/parser/parse_tree.h"
#include "googlesql/parser/parser_mode.h"
#include "googlesql/parser/tm_parser_util.h"
#include "googlesql/parser/statement_properties.h"
#include "googlesql/parser/token_stream.h"
#include "absl/base/attributes.h"
#include "absl/log/log.h"
#include "absl/memory/memory.h"
#include "absl/status/status.h"

namespace googlesql::parser {
class TextMapperLexerAdapter;
}  // namespace googlesql::parser

using namespace googlesql::parser::internal;
{{end -}}

{{define "parserIncludes" -}}
#include "{{.Options.DirIncludePrefix}}{{.Options.FilenamePrefix}}parser.h"

#include <cstdint>
#include <string>

#include "googlesql/common/status_payload_utils.h"
#include "googlesql/parser/ast_node_factory.h"
#include "googlesql/parser/join_processor.h"
#include "googlesql/parser/statement_properties.h"
#include "googlesql/parser/textmapper_lexer_adapter.h"
#include "googlesql/public/strings.h"
#include "absl/functional/function_ref.h"
#include "absl/status/status.h"
#include "absl/strings/match.h"
#include "absl/strings/str_format.h"
#include "absl/strings/str_join.h"
#include "{{.Options.DirIncludePrefix}}{{.Options.FilenamePrefix}}token.h"

// There are n symbols in the RHS. The locations can be accessed by
// get_location(i) where i is in [0, n-1].
inline googlesql::ParseLocationRange CreateLocationFromRHS(
    int32_t n,
    absl::FunctionRef<googlesql::ParseLocationRange(int32_t)> get_location) {
  googlesql::ParseLocationRange range;
  int i = 0;
  // i<n-1 so that we don't run off the end of stack. If all empty we grab
  // the last symbol's location
  while (i < n-1 && get_location(i).IsEmpty()) {
    i++;
  }
  range.set_start(get_location(i).start());
  // If any of the RHS is empty, the end location is inherited from
  // the top of the stack: they cling to the previous symbol.
  range.set_end(get_location(n-1).end());
  return range;
}
{{end -}}

{{ define "LexerClass" }}TextMapperLexerAdapter{{ end -}}
{{ define "LocationClass" }}ParseLocationRange{{ end -}}
{{ define "Location" }}using Location = ::googlesql::ParseLocationRange;{{ end -}}
{{ define "locStart" }}.start(){{ end -}}
{{ define "locEnd" -}}.end(){{ end -}}
{{ define "LocationFromOffsets" -}}
ParseLocationRange(ParseLocationPoint::FromByteOffset(filename_, token_offset_),
                   ParseLocationPoint::FromByteOffset(filename_, offset_))
{{ end -}}
{{ define "CreateLocationFromRHS" -}}CreateLocationFromRHS{{ end -}}
{{ define "onAfterShift" -}}
stack.back().value = absl::string_view(stack.back().sym.text);
{{ end -}}


{{ define "symbol" -}}
struct symbol {
  int32_t symbol = 0;
  absl::string_view text;
  ParseLocationRange location = ParseLocationRange();
};
{{ end -}}

{{ define "lookaheadFetch" -}}
  Token tok = lexer.Next();
{{ end -}}

{{ define "fetchImpl" -}}
  Token tok = lexer.Next();
{{ end -}}

{{ define "onAfterFetchNext" -}}
next_symbol_.text = lexer.Text();
{{ end -}}
