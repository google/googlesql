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

#include "googlesql/parser/keywords.h"

#include <cctype>
#include <cstdint>
#include <limits>
#include <memory>
#include <new>
#include <optional>
#include <utility>
#include <variant>
#include <vector>

#include "googlesql/base/logging.h"
#include "googlesql/parser/tm_token.h"
#include "absl/base/macros.h"
#include "absl/container/flat_hash_map.h"
#include "absl/strings/ascii.h"
#include "absl/strings/string_view.h"
#include "absl/types/variant.h"
#include "googlesql/base/map_util.h"

namespace googlesql {
namespace parser {

namespace {

enum KeywordClass {
  // Keyword is always reserved
  kReserved,

  // Keyword is never reserved
  kNotReserved,

  // LanguageOptions indicates whether the keyword is reserved or unreserved.
  // The "reserved" and "nonreserved" forms of the keyword produce different
  // tokens.
  kConditionallyReserved
};

struct ConditionallyReservedToken {
  Token reserved_token;
  Token nonreserved_token;
};

struct KeywordInfoPOD {
  absl::string_view keyword;
  std::variant<Token, ConditionallyReservedToken> token;
  KeywordClass keyword_class = kNotReserved;
};

constexpr KeywordInfoPOD kAllKeywords[] = {
    // Spanner-specific keywords
    {"interleave", Token::KW_INTERLEAVE},
    {"null_filtered", Token::KW_NULL_FILTERED},
    {"parent", Token::KW_PARENT},
    // End of Spanner-specific keywords

    // (broken link) start
    {"abort", Token::KW_ABORT},
    {"access", Token::KW_ACCESS},
    {"action", Token::KW_ACTION},
    {"acyclic", Token::KW_ACYCLIC},
    {"add", Token::KW_ADD},
    {"after", Token::KW_AFTER},
    {"aggregate", Token::KW_AGGREGATE},
    {"all", Token::KW_ALL, kReserved},
    {"alter", Token::KW_ALTER},
    {"always", Token::KW_ALWAYS},
    {"analyze", Token::KW_ANALYZE},
    {"and", Token::KW_AND, kReserved},
    {"any", Token::KW_ANY, kReserved},
    {"approx", Token::KW_APPROX},
    {"are", Token::KW_ARE},
    {"array", Token::KW_ARRAY, kReserved},
    {"as", Token::KW_AS, kReserved},
    {"asc", Token::KW_ASC, kReserved},
    {"ascending", Token::KW_ASCENDING},
    {"assert", Token::KW_ASSERT},
    {"assert_rows_modified", Token::KW_ASSERT_ROWS_MODIFIED, kReserved},
    {"at", Token::KW_AT, kReserved},
    {"batch", Token::KW_BATCH},
    {"begin", Token::KW_BEGIN},
    {"between", Token::KW_BETWEEN, kReserved},
    {"bigdecimal", Token::KW_BIGDECIMAL},
    {"bignumeric", Token::KW_BIGNUMERIC},
    {"break", Token::KW_BREAK},
    {"by", Token::KW_BY, kReserved},
    {"call", Token::KW_CALL},
    {"cascade", Token::KW_CASCADE},
    {"case", Token::KW_CASE, kReserved},
    {"cast", Token::KW_CAST, kReserved},
    {"cheapest", Token::KW_CHEAPEST},
    {"check", Token::KW_CHECK},
    {"clamped", Token::KW_CLAMPED},
    {"clone", Token::KW_CLONE},
    {"cluster", Token::KW_CLUSTER},
    {"collate", Token::KW_COLLATE, kReserved},
    {"column", Token::KW_COLUMN},
    {"columns", Token::KW_COLUMNS},
    {"commit", Token::KW_COMMIT},
    {"conflict", Token::KW_CONFLICT},
    {"connection", Token::KW_CONNECTION},
    {"constant", Token::KW_CONSTANT},
    {"constraint", Token::KW_CONSTRAINT},
    {"contains", Token::KW_CONTAINS, kReserved},
    {"continue", Token::KW_CONTINUE},
    {"copy", Token::KW_COPY},
    {"corresponding", Token::KW_CORRESPONDING},
    {"cost", Token::KW_COST},
    {"create", Token::KW_CREATE, kReserved},
    {"cross", Token::KW_CROSS, kReserved},
    {"cube", Token::KW_CUBE, kReserved},
    {"current", Token::KW_CURRENT, kReserved},
    {"cycle", Token::KW_CYCLE},
    {"data", Token::KW_DATA},
    {"database", Token::KW_DATABASE},
    {"date", Token::KW_DATE},
    {"datetime", Token::KW_DATETIME},
    {"decimal", Token::KW_DECIMAL},
    {"declare", Token::KW_DECLARE},
    {"default", Token::KW_DEFAULT, kReserved},
    {"define", Token::KW_DEFINE, kReserved},
    {"definer", Token::KW_DEFINER},
    {"delete", Token::KW_DELETE},
    {"deletion", Token::KW_DELETION},
    {"depth", Token::KW_DEPTH},
    {"desc", Token::KW_DESC, kReserved},
    {"descending", Token::KW_DESCENDING},
    {"describe", Token::KW_DESCRIBE},
    {"descriptor", Token::KW_DESCRIPTOR},
    {"destination", Token::KW_DESTINATION},
    {"deterministic", Token::KW_DETERMINISTIC},
    {"distinct", Token::KW_DISTINCT, kReserved},
    {"do", Token::KW_DO},
    {"drop", Token::KW_DROP},
    {"dynamic", Token::KW_DYNAMIC},
    {"edge", Token::KW_EDGE},
    {"else", Token::KW_ELSE, kReserved},
    {"elseif", Token::KW_ELSEIF},
    {"end", Token::KW_END, kReserved},
    {"enforced", Token::KW_ENFORCED},
    {"enum", Token::KW_ENUM, kReserved},
    {"error", Token::KW_ERROR},
    {"escape", Token::KW_ESCAPE, kReserved},
    {"except", Token::KW_EXCEPT, kReserved},
    {"exception", Token::KW_EXCEPTION},
    {"exclude", Token::KW_EXCLUDE, kReserved},
    {"execute", Token::KW_EXECUTE},
    {"exists", Token::KW_EXISTS, kReserved},
    {"explain", Token::KW_EXPLAIN},
    {"export", Token::KW_EXPORT},
    {"extend", Token::KW_EXTEND},
    {"external", Token::KW_EXTERNAL},
    {"extract", Token::KW_EXTRACT, kReserved},
    {"false", Token::KW_FALSE, kReserved},
    {"fetch", Token::KW_FETCH, kReserved},
    {"files", Token::KW_FILES},
    {"fill", Token::KW_FILL},
    {"filter", Token::KW_FILTER},
    {"first", Token::KW_FIRST},
    {"following", Token::KW_FOLLOWING, kReserved},
    {"for", Token::KW_FOR, kReserved},
    {"foreign", Token::KW_FOREIGN},
    {"fork", Token::KW_FORK},
    {"format", Token::KW_FORMAT},
    {"from", Token::KW_FROM, kReserved},
    {"full", Token::KW_FULL, kReserved},
    {"function", Token::KW_FUNCTION},
    {"generated", Token::KW_GENERATED},
    {"grant", Token::KW_GRANT},
    {"graph", Token::KW_GRAPH},
    {"graph_table",
     ConditionallyReservedToken{Token::KW_GRAPH_TABLE_RESERVED,
                                Token::KW_GRAPH_TABLE_NONRESERVED},
     kConditionallyReserved},
    {"group", Token::KW_GROUP, kReserved},
    {"group_rows", Token::KW_GROUP_ROWS},
    {"grouping", Token::KW_GROUPING, kReserved},
    {"groups", Token::KW_GROUPS, kReserved},
    {"hash", Token::KW_HASH, kReserved},
    {"having", Token::KW_HAVING, kReserved},
    {"hidden", Token::KW_HIDDEN},
    {"identity", Token::KW_IDENTITY},
    {"if", Token::KW_IF, kReserved},
    {"ignore", Token::KW_IGNORE, kReserved},
    {"immediate", Token::KW_IMMEDIATE},
    {"immutable", Token::KW_IMMUTABLE},
    {"import", Token::KW_IMPORT},
    {"in", Token::KW_IN, kReserved},
    {"include", Token::KW_INCLUDE},
    {"increment", Token::KW_INCREMENT},
    {"index", Token::KW_INDEX},
    {"inner", Token::KW_INNER, kReserved},
    {"inout", Token::KW_INOUT},
    {"input", Token::KW_INPUT},
    {"insert", Token::KW_INSERT},
    {"intersect", Token::KW_INTERSECT, kReserved},
    {"interval", Token::KW_INTERVAL, kReserved},
    {"into", Token::KW_INTO, kReserved},
    {"invoker", Token::KW_INVOKER},
    {"is", Token::KW_IS, kReserved},
    {"isolation", Token::KW_ISOLATION},
    {"iterate", Token::KW_ITERATE},
    {"join", Token::KW_JOIN, kReserved},
    {"json", Token::KW_JSON},
    {"key", Token::KW_KEY},
    {"label", Token::KW_LABEL},
    {"labeled", Token::KW_LABELED},
    {"language", Token::KW_LANGUAGE},
    {"last", Token::KW_LAST},
    {"lateral", Token::KW_LATERAL, kReserved},
    {"leave", Token::KW_LEAVE},
    {"left", Token::KW_LEFT, kReserved},
    {"let", Token::KW_LET},
    {"level", Token::KW_LEVEL},
    {"like", Token::KW_LIKE, kReserved},
    {"limit", Token::KW_LIMIT, kReserved},
    {"load", Token::KW_LOAD},
    {"locality", Token::KW_LOCALITY},
    {"log", Token::KW_LOG},
    {"lookup", Token::KW_LOOKUP, kReserved},
    {"loop", Token::KW_LOOP},
    {"macro", Token::KW_MACRO, kNotReserved},
    {"map", Token::KW_MAP, kNotReserved},
    {"match", Token::KW_MATCH},
    {"match_recognize",
     ConditionallyReservedToken{Token::KW_MATCH_RECOGNIZE_RESERVED,
                                Token::KW_MATCH_RECOGNIZE_NONRESERVED},
     kConditionallyReserved},
    {"matched", Token::KW_MATCHED},
    {"materialized", Token::KW_MATERIALIZED},
    {"max", Token::KW_MAX},
    {"maxvalue", Token::KW_MAXVALUE},
    {"measures", Token::KW_MEASURES},
    {"merge", Token::KW_MERGE, kReserved},
    {"message", Token::KW_MESSAGE},
    {"metadata", Token::KW_METADATA},
    {"min", Token::KW_MIN},
    {"minvalue", Token::KW_MINVALUE},
    {"model", Token::KW_MODEL},
    {"module", Token::KW_MODULE},
    {"name", Token::KW_NAME},
    {"natural", Token::KW_NATURAL, kReserved},
    {"new", Token::KW_NEW, kReserved},
    {"next", Token::KW_NEXT},
    {"no", Token::KW_NO, kReserved},
    {"node", Token::KW_NODE},
    {"not", Token::KW_NOT, kReserved},
    {"nothing", Token::KW_NOTHING},
    {"null", Token::KW_NULL, kReserved},
    {"nulls", Token::KW_NULLS, kReserved},
    {"numeric", Token::KW_NUMERIC},
    {"of", Token::KW_OF, kReserved},
    {"offset", Token::KW_OFFSET},
    {"on", Token::KW_ON, kReserved},
    {"only", Token::KW_ONLY},
    {"optional", Token::KW_OPTIONAL},
    {"options", Token::KW_OPTIONS},
    {"or", Token::KW_OR, kReserved},
    {"order", Token::KW_ORDER, kReserved},
    {"out", Token::KW_OUT},
    {"outer", Token::KW_OUTER, kReserved},
    {"output", Token::KW_OUTPUT},
    {"over", Token::KW_OVER, kReserved},
    {"overwrite", Token::KW_OVERWRITE},
    {"partition", Token::KW_PARTITION, kReserved},
    {"partitions", Token::KW_PARTITIONS},
    {"past", Token::KW_PAST},
    {"path", Token::KW_PATH},
    {"paths", Token::KW_PATHS},
    {"pattern", Token::KW_PATTERN},
    {"per", Token::KW_PER},
    {"percent", Token::KW_PERCENT},
    {"pivot", Token::KW_PIVOT},
    {"policies", Token::KW_POLICIES},
    {"policy", Token::KW_POLICY},
    {"preceding", Token::KW_PRECEDING, kReserved},
    {"primary", Token::KW_PRIMARY},
    {"private", Token::KW_PRIVATE},
    {"privilege", Token::KW_PRIVILEGE},
    {"privileges", Token::KW_PRIVILEGES},
    {"procedure", Token::KW_PROCEDURE},
    {"project", Token::KW_PROJECT},
    {"properties", Token::KW_PROPERTIES},
    {"property", Token::KW_PROPERTY},
    {"proto", Token::KW_PROTO, kReserved},
    {"public", Token::KW_PUBLIC},
    {"qualify",
     ConditionallyReservedToken{Token::KW_QUALIFY_RESERVED,
                                Token::KW_QUALIFY_NONRESERVED},
     kConditionallyReserved},
    {"raise", Token::KW_RAISE},
    {"range", Token::KW_RANGE, kReserved},
    {"read", Token::KW_READ},
    {"rebuild", Token::KW_REBUILD},
    {"recursive", Token::KW_RECURSIVE, kReserved},
    {"references", Token::KW_REFERENCES},
    {"remote", Token::KW_REMOTE},
    {"remove", Token::KW_REMOVE},
    {"rename", Token::KW_RENAME},
    {"repeat", Token::KW_REPEAT},
    {"repeatable", Token::KW_REPEATABLE},
    {"replace", Token::KW_REPLACE},
    {"replace_fields", Token::KW_REPLACE_FIELDS},
    {"replica", Token::KW_REPLICA},
    {"report", Token::KW_REPORT},
    {"respect", Token::KW_RESPECT, kReserved},
    {"restrict", Token::KW_RESTRICT},
    {"restriction", Token::KW_RESTRICTION},
    {"return", Token::KW_RETURN},
    {"returns", Token::KW_RETURNS},
    {"revoke", Token::KW_REVOKE},
    {"right", Token::KW_RIGHT, kReserved},
    {"rollback", Token::KW_ROLLBACK},
    {"rollup", Token::KW_ROLLUP, kReserved},
    {"row", Token::KW_ROW},
    {"rows", Token::KW_ROWS, kReserved},
    {"run", Token::KW_RUN},
    {"safe_cast", Token::KW_SAFE_CAST},
    {"schema", Token::KW_SCHEMA},
    {"search", Token::KW_SEARCH},
    {"security", Token::KW_SECURITY},
    {"select", Token::KW_SELECT, kReserved},
    {"sequence", Token::KW_SEQUENCE},
    {"set", Token::KW_SET, kReserved},
    {"sets", Token::KW_SETS},
    {"shortest", Token::KW_SHORTEST},
    {"show", Token::KW_SHOW},
    {"simple", Token::KW_SIMPLE},
    {"skip", Token::KW_SKIP},
    {"snapshot", Token::KW_SNAPSHOT},
    {"some", Token::KW_SOME, kReserved},
    {"source", Token::KW_SOURCE},
    {"sql", Token::KW_SQL},
    {"stable", Token::KW_STABLE},
    {"start", Token::KW_START},
    {"static_describe", Token::KW_STATIC_DESCRIBE},
    {"stored", Token::KW_STORED},
    {"storing", Token::KW_STORING},
    {"strict", Token::KW_STRICT},
    {"struct", Token::KW_STRUCT, kReserved},
    {"system", Token::KW_SYSTEM},
    {"system_time", Token::KW_SYSTEM_TIME},
    {"table", Token::KW_TABLE},
    {"tables", Token::KW_TABLES},
    {"tablesample", Token::KW_TABLESAMPLE, kReserved},
    {"target", Token::KW_TARGET},
    {"tee", Token::KW_TEE},
    {"temp", Token::KW_TEMP},
    {"temporary", Token::KW_TEMPORARY},
    {"then", Token::KW_THEN, kReserved},
    {"time", Token::KW_TIME},
    {"timestamp", Token::KW_TIMESTAMP},
    {"to", Token::KW_TO, kReserved},
    {"trail", Token::KW_TRAIL},
    {"transaction", Token::KW_TRANSACTION},
    {"transform", Token::KW_TRANSFORM},
    {"treat", Token::KW_TREAT, kReserved},
    {"true", Token::KW_TRUE, kReserved},
    {"truncate", Token::KW_TRUNCATE},
    {"type", Token::KW_TYPE},
    {"unbounded", Token::KW_UNBOUNDED, kReserved},
    {"undrop", Token::KW_UNDROP},
    {"union", Token::KW_UNION, kReserved},
    {"unique", Token::KW_UNIQUE},
    {"unknown", Token::KW_UNKNOWN},
    {"unnest", Token::KW_UNNEST, kReserved},
    {"unpivot", Token::KW_UNPIVOT},
    {"until", Token::KW_UNTIL},
    {"update", Token::KW_UPDATE},
    {"using", Token::KW_USING, kReserved},
    {"value", Token::KW_VALUE},
    {"values", Token::KW_VALUES},
    {"vector", Token::KW_VECTOR},
    {"view", Token::KW_VIEW},
    {"views", Token::KW_VIEWS},
    {"volatile", Token::KW_VOLATILE},
    {"walk", Token::KW_WALK},
    {"weight", Token::KW_WEIGHT},
    {"when", Token::KW_WHEN, kReserved},
    {"where", Token::KW_WHERE, kReserved},
    {"while", Token::KW_WHILE},
    {"window", Token::KW_WINDOW, kReserved},
    {"with", Token::KW_WITH, kReserved},
    {"within", Token::KW_WITHIN, kReserved},
    {"write", Token::KW_WRITE},
    {"yield", Token::KW_YIELD},
    {"zone", Token::KW_ZONE},
    // (broken link) end
};

// A case insensitive trie implementation. The ValueType is the type of value
// stored inside the trie. The stored values are non-owned pointers to
// ValueType. The case insensitivity is ASCII only. The maximum number of
// prefixes that this can store is 64K.
//
// Benchmark results showed that the functions below are several times faster on
// a hit than using dense_hash_map with a case insensitive hash function and
// equality (140-180 ns versus 14 ns). The performance for a miss is about 7 ns
// in the benchmark.
template <typename ValueType>
class CaseInsensitiveAsciiAlphaTrie {
 public:
  CaseInsensitiveAsciiAlphaTrie() : nodes_(1, TrieNode()) {}

  // Inserts 'key' into the trie, with value 'value'. Crashes if a value for
  // 'key' is already present.
  void Insert(absl::string_view key, const ValueType* value) {
    int node_index = 0;
    for (int i = 0; i < key.size(); ++i) {
      ABSL_CHECK(isalpha(key[i]) || key[i] == '_') << key;
      unsigned char c = absl::ascii_toupper(key[i]) - '0';
      int next_node_index = nodes_[node_index].children[c];
      if (next_node_index == 0) {
        ABSL_CHECK_LT(nodes_.size(), std::numeric_limits<uint16_t>::max());
        next_node_index = nodes_.size();
        nodes_[node_index].children[c] = next_node_index;
        nodes_.emplace_back();
      }
      node_index = next_node_index;
    }
    ABSL_CHECK(nodes_[node_index].value == nullptr) << "Duplicate key " << key;
    nodes_[node_index].value = value;
  }

  // Looks up 'key' in the trie. Returns nullptr for a non-match, or otherwise
  // the matched key's value. 'key' must only contain alphanumeric ASCII
  // characters or '_'.
  const ValueType* Get(absl::string_view key) const {
    int node_index = 0;
    for (int i = 0; i < key.size(); ++i) {
      unsigned char c = absl::ascii_toupper(key[i]) - '0';
      if (c >= ABSL_ARRAYSIZE(nodes_[node_index].children)) return nullptr;
      int next_node_index = nodes_[node_index].children[c];
      if (next_node_index == 0) return nullptr;
      node_index = next_node_index;
    }
    return nodes_[node_index].value;
  }

 private:
  struct TrieNode {
    // Child nodes for each supported byte value. Each value is an index into
    // CaseInsensitiveAsciiAlphaTrie::nodes_. Value 0 means that the child
    // is not present. The number of entries is enough to store all characters
    // between '0' and '_' in ASCII. The entry at index i is for character
    // '0' + i.
    uint16_t children['_' - '0' + 1]{};

    // The stored value for this node, or NULL if this node is not in the trie.
    ValueType* value = nullptr;
  };

  // All the nodes in the trie. Node with index 0 is the root.
  std::vector<TrieNode> nodes_;
};
}  // namespace

std::unique_ptr<const CaseInsensitiveAsciiAlphaTrie<const KeywordInfo>>
CreateKeywordTrie() {
  const auto& all_keywords = GetAllKeywords();
  auto trie =
      std::make_unique<CaseInsensitiveAsciiAlphaTrie<const KeywordInfo>>();
  for (const auto& keyword_info : all_keywords) {
    trie->Insert(keyword_info.keyword(), &keyword_info);
  }
  return std::move(trie);
}

const KeywordInfo* GetKeywordInfo(absl::string_view keyword) {
  static const auto& trie = *CreateKeywordTrie().release();
  return trie.Get(keyword);
}

static std::unique_ptr<const absl::flat_hash_map<Token, const KeywordInfo*>>
CreateTokenToKeywordInfoMap() {
  const auto& all_keywords = GetAllKeywords();
  auto keyword_info_map =
      std::make_unique<absl::flat_hash_map<Token, const KeywordInfo*>>();
  for (const KeywordInfo& keyword_info : all_keywords) {
    if (keyword_info.CanBeReserved()) {
      googlesql_base::InsertOrDie(keyword_info_map.get(), keyword_info.reserved_token(),
                       &keyword_info);
    }
    if (!keyword_info.IsAlwaysReserved()) {
      googlesql_base::InsertOrDie(keyword_info_map.get(), keyword_info.nonreserved_token(),
                       &keyword_info);
    }
  }
  return std::move(keyword_info_map);
}

const KeywordInfo* GetKeywordInfoForToken(Token token) {
  static const auto& keyword_info_map =
      *CreateTokenToKeywordInfoMap().release();
  const KeywordInfo* const* info = googlesql_base::FindOrNull(keyword_info_map, token);
  if (info == nullptr) {
    return nullptr;
  }
  return *info;
}

// TODO: Use a central map that is shared with the GoogleSQL JavaCC
// parser, and generate the tokenizer rules and token definitions from that. For
// now we have a test that validates that the reserved words are the same, but
// there are no tests that test how nonreserved words work. Also, the tokenizer
// rules are still hand maintained. We could add a test that verifies that the
// tokenizer recognizes each of the keywords, but that doesn't test if there are
// more keywords that it recognizes but that aren't in the list. :-/
const std::vector<KeywordInfo>& GetAllKeywords() {
  static const std::vector<KeywordInfo>* all_keywords = []() {
    std::vector<KeywordInfo>* keywords = new std::vector<KeywordInfo>();
    for (const KeywordInfoPOD& keyword : kAllKeywords) {
      switch (keyword.keyword_class) {
        case kReserved:
          keywords->push_back(
              {keyword.keyword, std::get<Token>(keyword.token), std::nullopt});
          break;
        case kNotReserved:
          keywords->push_back(
              {keyword.keyword, std::nullopt, std::get<Token>(keyword.token)});
          break;
        case kConditionallyReserved: {
          auto token = std::get<ConditionallyReservedToken>(keyword.token);
          keywords->push_back(
              {keyword.keyword, token.reserved_token, token.nonreserved_token});
        }
      }
    }
    return keywords;
  }();
  return *all_keywords;
}

static std::unique_ptr<const CaseInsensitiveAsciiAlphaTrie<const KeywordInfo>>
CreateKeywordInTokenizerTrie() {
  auto trie =
      std::make_unique<CaseInsensitiveAsciiAlphaTrie<const KeywordInfo>>();
  // These words are keywords in JavaCC, so we want to treat them as keywords in
  // the tokenizer API even though they are not always treated as keywords in
  // the parser.
  for (const char* keyword : {
           "current_date",
           "current_time",
           "current_datetime",
           "current_timestamp",
           "current_timestamp_seconds",
           "current_timestamp_millis",
           "current_timestamp_micros",
       }) {
    // We don't care about the KeywordInfo, but we have to create one because
    // the trie needs a non-NULL value. We use an arbitrary token.
    KeywordInfo* keyword_info =
        new KeywordInfo(keyword, Token::KW_SELECT, std::nullopt);
    trie->Insert(keyword_info->keyword(), keyword_info);
  }
  return std::move(trie);
}

bool IsKeywordInTokenizer(absl::string_view identifier) {
  static const auto& trie = *CreateKeywordInTokenizerTrie().release();
  return trie.Get(identifier) || GetKeywordInfo(identifier);
}

static std::unique_ptr<const CaseInsensitiveAsciiAlphaTrie<const KeywordInfo>>
CreateNonReservedIdentifiersThatMustBeBackquotedTrie() {
  auto trie =
      std::make_unique<CaseInsensitiveAsciiAlphaTrie<const KeywordInfo>>();
  // These non-reserved keywords are used in the grammar in a location where
  // identifiers also occur, and their meaning is different when they are
  // used without backquoting.
  for (const char* keyword : {
           "access",  // DROP `row` `access` `policy` versus DROP ROW ACCESS
                      // POLICY
           "current_date", "current_datetime", "current_time",
           "current_timestamp", "current_timestamp_micros",
           "current_timestamp_millis", "current_timestamp_seconds", "function",
           "inout",      // See AMBIGUOUS CASE 7 in googlesql.tm
           "out",        // See AMBIGUOUS CASE 7 in googlesql.tm
           "policy",     // DROP `row` `access` `policy` versus DROP ROW ACCESS
                         // POLICY
           "replace",    // INSERT REPLACE versus INSERT `replace`
           "row",        // DROP `row` `access` `policy` versus DROP ROW ACCESS
                         // POLICY
           "safe_cast",  // SAFE_CAST(...) versus `safe_cast`(3)
           "update",     // INSERT UPDATE versus INSERT `update`
           "clamped",    // See AMBIGUOUS CASE 14 in googlesql.tm
           // "value" is not included because it causes too much escaping for
           // this very commonly used name. The impact of this is small. The
           // only place where this can be interpreted as a keyword is in AS
           // VALUE. The alternative interpretation in that case is of a named
           // (protocol buffer) type with name "value". That is unlikely to be
           // an issue in practice. The only risk is that someone can trick a
           // generated query to run as SELECT AS VALUE instead of SELECT AS
           // `VALUE`, which would be very likely to fail and cause type
           // mismatches when it is run.
       }) {
    // We don't care about the KeywordInfo, but we have to create one because
    // the trie needs a non-NULL value. We use an arbitrary token.
    KeywordInfo* keyword_info =
        new KeywordInfo(keyword, Token::KW_SELECT, std::nullopt);
    trie->Insert(keyword_info->keyword(), keyword_info);
  }
  return std::move(trie);
}

bool NonReservedIdentifierMustBeBackquoted(absl::string_view identifier) {
  static const auto& trie =
      *CreateNonReservedIdentifiersThatMustBeBackquotedTrie().release();
  return trie.Get(identifier);
}

const absl::flat_hash_map<absl::string_view, absl::string_view>&
GetUserFacingImagesForSpecialKeywordsMap() {
  static absl::flat_hash_map<absl::string_view, absl::string_view>* kMap =
      []() {
        return new absl::flat_hash_map<absl::string_view, absl::string_view>{
            // TODO: Fold this mapping into kAllTokens instead of
            //     having this second place that needs updating.
            // (broken link) start
            {"AND for BETWEEN", "AND"},
            {"BYTES_LITERAL", "bytes literal"},
            {"EXCEPT in set operation", "EXCEPT"},
            {"FLOATING_POINT_LITERAL", "floating point literal"},
            {"IDENTIFIER", "identifier"},
            {"INTEGER_LITERAL", "integer literal"},
            {"KW_DEFINE_FOR_MACROS", "DEFINE for macros"},
            {"KW_EXCEPT_IN_SET_OP", "EXCEPT"},
            {"KW_FULL_IN_SET_OP", "FULL"},
            {"KW_INNER_IN_SET_OP", "INNER"},
            {"KW_LEFT_IN_SET_OP", "LEFT"},
            {"KW_OPEN_HINT", "@ for hint"},
            {"KW_OPEN_INTEGER_HINT", "@ for hint"},
            {"KW_QUALIFY_RESERVED", "QUALIFY"},
            {"KW_TABLE_FOR_TABLE_CLAUSE", "TABLE"},
            {"KW_WITH_STARTING_WITH_EXPRESSION", "WITH"},
            {"KW_WITH_STARTING_WITH_GROUP_ROWS", "WITH"},
            {"NOT_SPECIAL", "NOT"},
            {"OPEN_INTEGER_PREFIX_HINT", "@ for hint"},
            {"STRING_LITERAL", "string literal"},
            {"WITH starting with expression", "WITH"},
            // (broken link) end
        };
      }();
  return *kMap;
}

}  // namespace parser
}  // namespace googlesql
