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

#ifndef GOOGLESQL_PUBLIC_FUNCTIONS_JSON_INTERNAL_H_
#define GOOGLESQL_PUBLIC_FUNCTIONS_JSON_INTERNAL_H_

#include <stdio.h>

#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <stack>
#include <string>
#include <utility>
#include <variant>
#include <vector>

#include "googlesql/common/json_parser.h"
#include "googlesql/common/json_util.h"
#include "googlesql/common/thread_stack.h"
#include "googlesql/base/string_numbers.h"  
#include "absl/base/attributes.h"
#include "absl/base/optimization.h"
#include "googlesql/base/check.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/string_view.h"

namespace googlesql {
namespace functions {
namespace json_internal {

void RemoveBackSlashFollowedByChar(std::string* token, char esc_chr);

// This class acts both like an STL container and iterator over JSON path
// tokens. The input JSONPath is parsed and underlying memory for the tokens are
// owned by this class. This is a read-only interface and JSONPath tokens cannot
// be modified after initialization as part of the contract.
//
// Functions in this class are inlined due to performance reasons.
template <typename Token>
class JSONPathIterator {
 public:
  // Rewind the iterator.
  inline void Rewind() {
    depth_ = 1;
    is_valid_ = true;
  }

  inline size_t Depth() { return depth_; }

  inline bool End() { return !is_valid_; }

  inline bool operator--() {
    if (ABSL_PREDICT_TRUE(depth_ > 0)) {
      --depth_;
    }
    is_valid_ = (depth_ > 0 && depth_ <= tokens_.size());
    return is_valid_;
  }

  // Precondition: End() is false.
  inline const Token& operator*() const {
    ABSL_DCHECK(depth_ > 0 && depth_ <= tokens_.size());
    return tokens_[depth_ - 1];
  }

  // Undefined behavior if `i` is out of bounds.
  inline const Token& GetToken(int i) const {
    ABSL_DCHECK(i >= 0 && i < tokens_.size());
    return tokens_[i];
  }

  inline bool NoSuffixToken() { return depth_ == tokens_.size(); }

  inline size_t Size() { return tokens_.size(); }

  ABSL_DEPRECATED("JSON paths are scanned during initialization.")
  // This function simply moves the current token position to the end of the
  // path. Because tokens are pre-processed this function has very limited
  // functionality. Eventually, we would like to remove this function once all
  // references are removed.
  inline void Scan() {
    depth_ = tokens_.size() + 1;
    is_valid_ = false;
  }

  inline bool operator++() {
    if (depth_ <= tokens_.size()) {
      ++depth_;
      is_valid_ = depth_ <= tokens_.size();
    }
    return is_valid_;
  }

 protected:
  explicit JSONPathIterator(std::vector<Token> tokens)
      : tokens_(std::move(tokens)) {}

  const std::vector<Token> tokens_;
  size_t depth_ = 1;
  bool is_valid_ = true;
};

// Checks if the given JSON path is supported and valid.
absl::Status IsValidJSONPath(absl::string_view text, bool sql_standard_mode);

// Bi-directed iterator over tokens in a valid JSON path, used by the extraction
// algorithm.
class ValidJSONPathIterator final : public JSONPathIterator<std::string> {
 public:
  typedef std::string Token;

  static absl::StatusOr<std::unique_ptr<ValidJSONPathIterator>> Create(
      absl::string_view js_path, bool sql_standard_mode);

 private:
  explicit ValidJSONPathIterator(std::vector<Token> tokens)
      : JSONPathIterator(std::move(tokens)) {}
};

// Checks if the given JSON path is supported and valid in standard sql with
// strict notation.
//
// Strict notation only allows '[]' to refer to an array location and .property
// to refer to an object member.
//
// If `enable_lax_mode` is set to true, enables lax path notation.
absl::Status IsValidJSONPathStrict(absl::string_view text,
                                   bool enable_lax_mode = false);

// Returns whether the given JSON path is valid and contains lax modifier.
// Returns:
// 1) True - The path is valid and in lax mode.
// 2) False - The path is valid but not in lax mode.
// 3) Error - Invalid path.
absl::StatusOr<bool> IsValidAndLaxJSONPath(absl::string_view text);

// Represents a strict parsed token.
class StrictJSONPathToken {
  using StrictTokenValue = std::variant<std::monostate, std::string, int64_t>;

 public:
  explicit StrictJSONPathToken(StrictTokenValue token)
      : token_(std::move(token)) {}

  // If the token type is neither an object nor an array, it is a no-op (token
  // value of '$').
  const std::string* MaybeGetObjectKey() const {
    return std::get_if<std::string>(&token_);
  }

  const int64_t* MaybeGetArrayIndex() const {
    return std::get_if<int64_t>(&token_);
  }

 private:
  StrictTokenValue token_;
};

// Specified path options for a JSONPath.
enum class JsonPathOptions { kStrict, kLax, kLaxRecursive };

// Used for strict iteration over a JSON path. Strict notation only allows '[]'
// to refer to an array location and .property to refer to an object member. It
// is a bi-directed iterator over tokens in a valid JSON path in standard sql
// notation.
class StrictJSONPathIterator final
    : public JSONPathIterator<StrictJSONPathToken> {
 public:
  // Specified path options for input JSONPath. The option fields can only be
  // set to true if `enable_lax_mode`= true.
  struct JsonPathOptions {
    bool lax = false;
    // Invariant: `recursive`= true only if `lax`= true. This is verified
    // during path parsing.
    bool recursive = false;
  };

  // JSON path must be in standard SQL. If `enable_lax_mode` is set to true,
  // enables lax path notation.
  static absl::StatusOr<std::unique_ptr<StrictJSONPathIterator>> Create(
      absl::string_view json_path, bool enable_lax_mode = false);

  // Returns the path options specified by `json_path`.
  JsonPathOptions GetJsonPathOptions() const { return json_path_options_; }

 private:
  StrictJSONPathIterator(std::vector<StrictJSONPathToken> tokens,
                         JsonPathOptions json_path_options)
      : JSONPathIterator(std::move(tokens)),
        json_path_options_(std::move(json_path_options)) {}

  JsonPathOptions json_path_options_;
};

//
// An efficient algorithm to extract the specified JSON path from the JSON text.
// Let $n$ be the number of nodes in the tree corresponding to the JSON text
// then the runtime of the algorithm is $\Theta(n)$ string comparisons in the
// worst case. The space complexity is the depth of the JSON path.
// The fundamental idea behind this algorithm is fully general to support all
// JSON path operators (@, *, .. etc.).
//
// Invariant maintaining functions are inlined for performance reasons.
//
class JSONPathExtractor : public JSONParser {
 public:
  // Maximum recursion depth when parsing JSON. We check both for stack space
  // and for depth relative to this limit when parsing; in practice, the
  // expectation is that we should reach this limit prior to exhausting stack
  // space in the interest of avoiding crashes.
  static constexpr int kMaxParsingDepth = 1000;

  // `iter` and the object underlying `json` must outlive this object.
  JSONPathExtractor(absl::string_view json, ValidJSONPathIterator* iter)
      : JSONParser(json),
        stack_(),
        curr_depth_(),
        matching_token_(),
        result_json_(),
        path_iterator_(*iter),
        escaping_needed_callback_(nullptr) {
    set_special_character_escaping(false);
    Init();
  }

  void set_special_character_escaping(bool escape_special_characters) {
    escape_special_characters_ = escape_special_characters;
  }

  // Sets JSON key escaping. Will only escape keys if
  // `set_special_character_escaping(true)` is called.
  void set_special_character_key_escaping(bool enable_key_escaping) {
    enable_key_escaping_ = enable_key_escaping;
  }

  // Sets the callback to be invoked when a string with special characters was
  // parsed, but special character escaping was turned off.  The caller is
  // responsible for ensuring that the lifetime of the callback object lasts as
  // long as any parsing calls that may invoke it.  No callback will be made if
  // this is set to nullptr or points to an empty target.
  void set_escaping_needed_callback(
      const std::function<void(absl::string_view, bool)>* callback) {
    escaping_needed_callback_ = callback;
  }

  bool Extract(std::string* result, bool* is_null,
               std::optional<std::function<void(absl::Status)>> issue_warning =
                   std::nullopt) {
    absl::Status parse_status = JSONParser::Parse();
    // TODO: b/196226376 - Fix the swallowing of the `parse_status` error.
    bool parse_success = parse_status.ok() || stop_on_first_match_;

    // Parse-failed OR no-match-found OR null-Value
    *is_null = !parse_success || !stop_on_first_match_ || parsed_null_result_;
    if (parse_success) {
      if (result_contains_unescaped_key_ && issue_warning.has_value()) {
        (*issue_warning)(
            absl::OutOfRangeError("The JSON value contains an unescaped key"));
      }
      result->assign(result_json_);
    }
    return parse_success;
  }

  bool StoppedOnFirstMatch() { return stop_on_first_match_; }

  // Returns whether parsing failed due to running out of stack space.
  bool StoppedDueToStackSpace() const { return stopped_due_to_stack_space_; }

 protected:
  bool BeginObject() override {
    if (!MaintainInvariantMovingDown()) {
      return false;
    }
    if (accept_) {
      absl::StrAppend(&result_json_, "{");
    }
    return true;
  }

  bool EndObject() override {
    if (accept_) {
      absl::StrAppend(&result_json_, "}");
    }
    MaintainInvariantMovingUp();
    return !stop_on_first_match_;
  }

  bool BeginArray() override {
    if (!MaintainInvariantMovingDown()) {
      return false;
    }

    // Stack Usage Invariant: !accept_ && match_
    if (!accept_ && extend_match_) {
      const std::string& token = *path_iterator_;
      // TODO: we can save more runtime by doing a sscanf in each
      // record. The tokens in the path iterator can store the index value
      // instead of doing sscanf in every record scan.
      has_index_token_ = (sscanf(token.c_str(), "%u", &index_token_) == 1);
      stack_.push(0);
    }

    // This is an array object so initialize the array index.
    if (accept_) {
      absl::StrAppend(&result_json_, "[");
      if (curr_depth_ == path_iterator_.Depth()) {
        array_accepted_ = true;
      }
    }
    return true;
  }

  bool EndArray() override {
    if (accept_) {
      absl::StrAppend(&result_json_, "]");
    }

    // Stack Usage Invariant: !accept_ && match_
    if (!accept_ && extend_match_) {
      stack_.pop();
      has_index_token_ = false;
    }
    MaintainInvariantMovingUp();
    return !stop_on_first_match_;
  }

  bool BeginMember(const std::string& key) override {
    if (accept_) {
      JsonEscapeAndAppendString</*is_key=*/true>(key);
    } else if (extend_match_) {
      MatchAndMaintainInvariant(key, false);
    }
    return true;
  }

  bool BeginArrayEntry() override {
    // Stack Usage Invariant: !accept_ && match_
    if (!accept_ && extend_match_) {
      MatchAndMaintainInvariant("", true);
    }
    return true;
  }

  bool EndMember(bool last) override {
    if (accept_ && !last) {
      absl::StrAppend(&result_json_, ",");
    }
    return true;
  }

  bool EndArrayEntry(bool last) override {
    if (!accept_ && extend_match_) {
      stack_.top()++;
    }

    if (accept_ && !last) {
      absl::StrAppend(&result_json_, ",");
    }
    return true;
  }

  bool ParsedNumber(absl::string_view str) override {
    if (AcceptableLeaf()) {
      absl::StrAppend(&result_json_, str);
    }
    return !stop_on_first_match_;
  }

  bool ParsedString(const std::string& str) override {
    if (AcceptableLeaf()) {
      JsonEscapeAndAppendString</*is_key=*/false>(str);
    }
    return !stop_on_first_match_;
  }

  bool ParsedBool(bool val) override {
    if (AcceptableLeaf()) {
      absl::StrAppend(&result_json_, googlesql_base::SimpleBtoa(val));
    }
    return !stop_on_first_match_;
  }

  bool ParsedNull() override {
    if (AcceptableLeaf()) {
      parsed_null_result_ = stop_on_first_match_;
      absl::StrAppend(&result_json_, "null");
    }
    return !stop_on_first_match_;
  }

  bool ReportFailure(absl::string_view error_message) override {
    return JSONParser::ReportFailure(error_message);
  }

  void Init() {
    path_iterator_.Rewind();
    curr_depth_ = 1;
    matching_token_ = true;
    accept_ = false;
    accept_array_elements_ = false;
    stop_on_first_match_ = false;
    parsed_null_result_ = false;
  }

  // Returns false and sets stopped_due_to_stack_space_ if there is no more
  // stack space to continue parsing.
  ABSL_MUST_USE_RESULT inline bool MaintainInvariantMovingDown() {
    if (curr_depth_ >= kMaxParsingDepth + 1  //
    ) {                                      //
      stopped_due_to_stack_space_ = true;
      return false;
    }
    ++curr_depth_;
    extend_match_ = matching_token_;
    if (extend_match_) {
      matching_token_ = false;
      ++path_iterator_;
      accept_ = path_iterator_.End();
    }
    accept_array_elements_ = accept_ && (curr_depth_ == path_iterator_.Depth());
    return true;
  }

  inline void MaintainInvariantMovingUp() {
    if (extend_match_) {
      --path_iterator_;
      stop_on_first_match_ = accept_ && !path_iterator_.End();
      accept_ = path_iterator_.End();
    }
    --curr_depth_;
    accept_array_elements_ = accept_ && (curr_depth_ == path_iterator_.Depth());
    extend_match_ = (curr_depth_ == path_iterator_.Depth());
  }

  // This will be only called when extend_match_ is true.
  inline void MatchAndMaintainInvariant(absl::string_view key,
                                        bool is_array_index) {
    matching_token_ = false;
    if (is_array_index) {
      // match array index token.
      matching_token_ = has_index_token_ && (index_token_ == stack_.top());
    } else {
      matching_token_ = (*path_iterator_ == key);
    }
  }

  // Escapes and appends `val` to `result_json_` based on different conditions.
  // `is_key` indicates if we're appending a key or value to result.
  // 1) If no escaping is needed, simply appends `val` to `result_json_`.
  // 2) If `is_key`, all escaping is enabled, and any characters need escaping,
  //    escapes `val` in JSON style and appends it to `result_json_`.
  // 3) If `!is_key`, value escaping is enabled, and any characters need
  //    escaping, escapes `val` in JSON style and appends it to `result_json_`.
  // 4) If escaping is disabled and any characters need escaping, use custom
  //    escaping callback if provided. Else appends unescaped `val`.
  template <bool is_key>
  inline void JsonEscapeAndAppendString(absl::string_view val) {
    if (JsonStringNeedsEscaping(val)) {
      bool enable_value_escaping = escape_special_characters_;
      bool enable_key_escaping =
          escape_special_characters_ && enable_key_escaping_;
      if ((is_key && enable_key_escaping) ||
          (!is_key && enable_value_escaping)) {
        std::string escaped;
        JsonEscapeString(val, &escaped);
        // EscapeString adds quotes.
        absl::StrAppend(&result_json_, escaped, is_key ? ":" : "");
      } else {
        if (is_key && !enable_key_escaping_ && escape_special_characters_) {
          result_contains_unescaped_key_ = true;
        }
        if (escaping_needed_callback_ != nullptr &&
            *escaping_needed_callback_ /* contains a callable target */) {
          (*escaping_needed_callback_)(val, is_key);
        }
        absl::StrAppend(&result_json_, "\"", val, "\"", is_key ? ":" : "");
      }
    } else {
      absl::StrAppend(&result_json_, "\"", val, "\"", is_key ? ":" : "");
    }
  }

  // To accept the leaf its either in an acceptable sub-tree or a having
  // a parent with a matching token.
  bool AcceptableLeaf() {
    return accept_ || (stop_on_first_match_ =
                           (matching_token_ && path_iterator_.NoSuffixToken()));
  }

  // Stack will only be used to keep track of index of nested arrays.
  //
  std::stack<size_t> stack_;
  // Invariant: path_iterator_.Depth() <= curr_depth_
  size_t curr_depth_;
  // Invariant: if true the last compared token at this level has matched the
  // corresponding token in the path.
  bool matching_token_;
  std::string result_json_;
  ValidJSONPathIterator path_iterator_;
  // Invariant: if true the tokens till curr_depth_-1 are matched.
  bool extend_match_ = false;
  // Accept the entire sub-tree.
  bool accept_ = false;
  // Accept the element under the accepted array/object.
  bool accept_array_elements_ = false;
  // To report all matches remove this variable.
  bool stop_on_first_match_ = false;
  bool parsed_null_result_ = false;
  // `has_index_token_` is set when:
  //    - The start of an array is seen, AND
  //    - Tokens match till curr_depth_-1 (i.e. `extend_match_`= true ), AND
  //    - The sub-tree is not accepted (i.e. `accept_` = false), AND
  //    - The current token given by `path_iterator` is a valid index.
  // `has_index_token_` must be reset when:
  //    - The end of an array is seen, AND
  //    - Tokens match till curr_depth_-1 (i.e. `extend_match_`= true ), AND
  //    - The sub-tree is not accepted (i.e. `accept_` = false).
  bool has_index_token_ = false;
  unsigned int index_token_;
  // Whether to escape special JSON characters (e.g. newlines).
  bool escape_special_characters_;
  // Whether to escape special JSON characters in JSON keys.
  bool enable_key_escaping_ = false;
  // Whether the result contains an unescaped key when
  // `escape_special_characters_`. This can be used as a potential warning
  // for inconsistent behavior.
  bool result_contains_unescaped_key_ = false;
  // Callback to pass any strings that needed escaping when escaping special
  // characters is turned off.  No callback needed if set to nullptr.
  // If the parameter `is_key` is true, this callback is for JSON keys.
  // Otherwise, this callback is for JSON values.
  const std::function<void(absl::string_view, bool is_key)>*
      escaping_needed_callback_;
  // Whether parsing failed due to running out of stack space.
  bool stopped_due_to_stack_space_ = false;
  // Whether the JSONPath points to an array.
  bool array_accepted_ = false;
};

//
// Scalar version of JSONPathExtractor, stops parsing as soon we find an
// acceptable sub-tree which is a leaf.
//
class JSONPathExtractScalar final : public JSONPathExtractor {
 public:
  // `iter` and the object underlying `json` must outlive this object.
  JSONPathExtractScalar(absl::string_view json, ValidJSONPathIterator* iter)
      : JSONPathExtractor(json, iter) {}

  bool Extract(std::string* result, bool* is_null) {
    absl::Status parse_status = JSONParser::Parse();
    // TODO: b/196226376 - Fix the swallowing of the `parse_status` error.
    bool parse_success = parse_status.ok() || accept_ || stop_on_first_match_;

    // Parse-failed  OR Subtree-Node OR null-Value OR no-match-found
    *is_null = !parse_success || accept_ || parsed_null_result_ ||
               !stop_on_first_match_;

    if (!(*is_null)) {
      result->assign(result_json_);
    }
    return parse_success;
  }

 protected:
  bool BeginObject() override {
    if (!JSONPathExtractor::BeginObject()) {
      return false;
    }
    return !accept_;
  }

  bool BeginArray() override {
    if (!JSONPathExtractor::BeginArray()) {
      return false;
    }
    return !accept_;
  }

  bool ParsedString(const std::string& str) override {
    if (AcceptableLeaf()) {
      absl::StrAppend(&result_json_, str);
    }
    return !stop_on_first_match_;
  }
};

// A JSONPath extractor that extracts array referred to by JSONPath. Similar to
// the scalar version of JSONPath extractor, it finds the first sub-tree
// matching the JSONPath. If it is not an array, returns null. Otherwise it
// will iterate over all the elements of the array and append them to
// 'result_array_' as strings and finally return the array.
class JSONPathArrayExtractor final : public JSONPathExtractor {
 public:
  // `iter` and the object underlying `json` must outlive this object.
  JSONPathArrayExtractor(absl::string_view json, ValidJSONPathIterator* iter)
      : JSONPathExtractor(json, iter) {}

  bool ExtractArray(std::vector<std::string>* result, bool* is_null,
                    std::optional<std::function<void(absl::Status)>>
                        issue_warning = std::nullopt) {
    absl::Status parse_status = JSONParser::Parse();
    // TODO: b/196226376 - Fix the swallowing of the `parse_status` error.
    bool parse_success = parse_status.ok() || stop_on_first_match_;

    // Parse-failed OR no-match-found OR null-Value OR not-an-array
    *is_null = !parse_success || !stop_on_first_match_ || parsed_null_result_ ||
               !array_accepted_;
    if (parse_success) {
      if (result_contains_unescaped_key_ && issue_warning.has_value()) {
        (*issue_warning)(
            absl::OutOfRangeError("The JSON value contains an unescaped key"));
      }
      result->assign(result_array_.begin(), result_array_.end());
    }
    return parse_success;
  }

 protected:
  bool BeginArrayEntry() override {
    // Stack Usage Invariant: !accept_ && match_
    if (!accept_ && extend_match_) {
      MatchAndMaintainInvariant("", true);
    } else if (accept_array_elements_) {
      result_json_.clear();
    }
    return true;
  }
  bool EndArrayEntry(bool last) override {
    if (!accept_ && extend_match_) {
      stack_.top()++;
    }
    if (accept_array_elements_) {
      result_array_.push_back(result_json_);
    } else if (accept_ && !last) {
      absl::StrAppend(&result_json_, ",");
    }
    return true;
  }
  std::vector<std::string> result_array_;
};

// A JSONPath extractor that extracts array referred to by JSONPath. Similar to
// the scalar version of JSONPath extractor, it finds the first sub-tree
// matching the JSONPath. If it is not an array, returns null. Otherwise it
// will iterate over all the elements of the array and append them to
// 'result_array_' as strings and finally return the array.
// An std::nullopt value in 'result_array_' represents the SQL NULL value.
class JSONPathStringArrayExtractor final : public JSONPathExtractor {
 public:
  // `iter` and the object underlying `json` must outlive this object.
  JSONPathStringArrayExtractor(absl::string_view json,
                               ValidJSONPathIterator* iter)
      : JSONPathExtractor(json, iter) {}

  bool ExtractStringArray(std::vector<std::optional<std::string>>* result,
                          bool* is_null) {
    absl::Status parse_status = JSONParser::Parse();
    // TODO: b/196226376 - Fix the swallowing of the `parse_status` error.
    bool parse_success = parse_status.ok() || stop_on_first_match_;

    // Parse-failed OR no-match-found OR null-Value OR not-an-array
    *is_null = !parse_success || !stop_on_first_match_ || parsed_null_result_ ||
               !scalar_array_accepted_;
    if (!(*is_null)) {
      result->assign(result_array_.begin(), result_array_.end());
    }
    return parse_success;
  }

 protected:
  bool BeginObject() override {
    if (!JSONPathExtractor::BeginObject()) {
      return false;
    }
    scalar_array_accepted_ = false;
    return true;
  }

  bool BeginArray() override {
    if (!JSONPathExtractor::BeginArray()) {
      return false;
    }
    scalar_array_accepted_ = accept_array_elements_;
    return true;
  }

  bool BeginArrayEntry() override {
    // Stack Usage Invariant: !accept_ && match_
    if (!accept_ && extend_match_) {
      MatchAndMaintainInvariant("", true);
    } else if (accept_array_elements_) {
      result_json_.clear();
    }
    return true;
  }

  bool EndArrayEntry(bool last) override {
    if (!accept_ && extend_match_) {
      stack_.top()++;
    }
    if (accept_array_elements_) {
      if (parsed_null_element_) {
        // This means the array element is the JSON null.
        result_array_.push_back(std::nullopt);
        parsed_null_element_ = false;
      } else {
        result_array_.push_back(result_json_);
      }
    } else if (accept_ && !last) {
      absl::StrAppend(&result_json_, ",");
    }
    return true;
  }

  bool ParsedString(const std::string& str) override {
    if (AcceptableLeaf()) {
      absl::StrAppend(&result_json_, str);
    }
    return !stop_on_first_match_;
  }

  bool ParsedNull() override {
    if (AcceptableLeaf()) {
      parsed_null_result_ = stop_on_first_match_;
      parsed_null_element_ = true;
    }
    return !stop_on_first_match_;
  }

  // Whether the JSONPath points to an array with scalar elements.
  bool scalar_array_accepted_ = false;
  // Whether the element that will be part of the result array is null.
  bool parsed_null_element_ = false;
  std::vector<std::optional<std::string>> result_array_;
};

}  // namespace json_internal
}  // namespace functions
}  // namespace googlesql

#endif  // GOOGLESQL_PUBLIC_FUNCTIONS_JSON_INTERNAL_H_
