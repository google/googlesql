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

#include "googlesql/public/functions/json.h"

#include <cstddef>
#include <cstdint>
#include <functional>
#include <iterator>
#include <memory>
#include <optional>
#include <stack>
#include <string>
#include <utility>
#include <vector>

#include "googlesql/common/errors.h"
#include "googlesql/common/json_util.h"
#include "googlesql/public/functions/convert.h"
#include "googlesql/public/functions/convert_string.h"
#include "googlesql/public/functions/json_format.h"
#include "googlesql/public/functions/json_internal.h"
#include "googlesql/public/functions/to_json.h"
#include "googlesql/public/json_value.h"
#include "googlesql/public/language_options.h"
#include "googlesql/public/numeric_value.h"
#include "googlesql/public/value.h"
#include "absl/base/optimization.h"
#include "absl/container/btree_set.h"
#include "absl/container/inlined_vector.h"
#include "absl/functional/function_ref.h"
#include "absl/memory/memory.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/match.h"
#include "absl/strings/numbers.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/string_view.h"
#include "absl/types/span.h"
#include "googlesql/base/lossless_convert.h"
#include "re2/re2.h"
#include "googlesql/base/ret_check.h"
#include "googlesql/base/status_macros.h"

namespace googlesql {
namespace functions {
namespace {
using json_internal::JSONPathExtractor;
using json_internal::JsonPathOptions;
using json_internal::StrictJSONPathIterator;
using json_internal::StrictJSONPathToken;
using json_internal::ValidJSONPathIterator;
}  // namespace

JsonPathEvaluator::~JsonPathEvaluator() = default;

JsonPathEvaluator::JsonPathEvaluator(
    std::unique_ptr<ValidJSONPathIterator> itr,
    bool enable_special_character_escaping_in_values,
    bool enable_special_character_escaping_in_keys)
    : path_iterator_(std::move(itr)),
      enable_special_character_escaping_in_values_(
          enable_special_character_escaping_in_values),
      enable_special_character_escaping_in_keys_(
          enable_special_character_escaping_in_keys) {}

// static
absl::StatusOr<std::unique_ptr<JsonPathEvaluator>> JsonPathEvaluator::Create(
    absl::string_view json_path, bool sql_standard_mode,
    bool enable_special_character_escaping_in_values,
    bool enable_special_character_escaping_in_keys) {
  GOOGLESQL_ASSIGN_OR_RETURN(std::unique_ptr<ValidJSONPathIterator> itr,
                   ValidJSONPathIterator::Create(json_path, sql_standard_mode));
  // Scan tokens as json_path may not persist beyond this call.
  itr->Scan();
  return absl::WrapUnique(new JsonPathEvaluator(
      std::move(itr), enable_special_character_escaping_in_values,
      enable_special_character_escaping_in_keys));
}

absl::Status JsonPathEvaluator::Extract(
    absl::string_view json, std::string* value, bool* is_null,
    std::optional<std::function<void(absl::Status)>> issue_warning) const {
  JSONPathExtractor parser(json, path_iterator_.get());
  parser.set_special_character_escaping(
      enable_special_character_escaping_in_values_);
  parser.set_special_character_key_escaping(
      enable_special_character_escaping_in_keys_);
  parser.set_escaping_needed_callback(&escaping_needed_callback_);
  value->clear();
  parser.Extract(value, is_null, issue_warning);
  if (parser.StoppedDueToStackSpace()) {
    return MakeEvalError() << "JSON parsing failed due to deeply nested "
                              "array/struct. Maximum nesting depth is "
                           << JSONPathExtractor::kMaxParsingDepth;
  }
  return absl::OkStatus();
}

std::optional<JSONValueConstRef> JsonPathEvaluator::Extract(
    JSONValueConstRef input) const {
  bool first_token = true;
  for (path_iterator_->Rewind(); !path_iterator_->End(); ++(*path_iterator_)) {
    const ValidJSONPathIterator::Token& token = *(*path_iterator_);

    if (first_token) {
      // The JSONPath "$.a[1].b" will result in the following list of tokens:
      // "", "a", "1", "b". The first token is always the empty token
      // corresponding to the whole JSON document, and we don't need to do
      // anything in that iteration. There can be other empty tokens after
      // the first one (empty keys are valid).
      first_token = false;
      continue;
    }

    if (input.IsObject()) {
      std::optional<JSONValueConstRef> optional_member =
          input.GetMemberIfExists(token);
      if (!optional_member.has_value()) {
        return std::nullopt;
      }
      input = optional_member.value();
    } else if (input.IsArray()) {
      int64_t index;
      if (!absl::SimpleAtoi(token, &index) || index < 0 ||
          index >= input.GetArraySize()) {
        return std::nullopt;
      }
      input = input.GetArrayElement(index);
    } else {
      // The path is not present in the JSON object.
      return std::nullopt;
    }
  }

  return input;
}

absl::Status JsonPathEvaluator::ExtractScalar(absl::string_view json,
                                              std::string* value,
                                              bool* is_null) const {
  json_internal::JSONPathExtractScalar scalar_parser(json,
                                                     path_iterator_.get());
  value->clear();
  scalar_parser.Extract(value, is_null);
  if (scalar_parser.StoppedDueToStackSpace()) {
    return MakeEvalError() << "JSON parsing failed due to deeply nested "
                              "array/struct. Maximum nesting depth is "
                           << JSONPathExtractor::kMaxParsingDepth;
  }
  return absl::OkStatus();
}

std::optional<std::string> JsonPathEvaluator::ExtractScalar(
    JSONValueConstRef input) const {
  std::optional<JSONValueConstRef> optional_json = Extract(input);
  if (!optional_json.has_value() || optional_json->IsNull() ||
      optional_json->IsObject() || optional_json->IsArray()) {
    return std::nullopt;
  }

  if (optional_json->IsString()) {
    // ToString() adds extra quotes and escapes special characters,
    // which we don't want.
    return optional_json->GetString();
  }

  return optional_json->ToString();
}

absl::Status JsonPathEvaluator::ExtractArray(
    absl::string_view json, std::vector<std::string>* value, bool* is_null,
    std::optional<std::function<void(absl::Status)>> issue_warning) const {
  json_internal::JSONPathArrayExtractor array_parser(json,
                                                     path_iterator_.get());
  array_parser.set_special_character_escaping(
      enable_special_character_escaping_in_values_);
  array_parser.set_special_character_key_escaping(
      enable_special_character_escaping_in_keys_);
  value->clear();
  array_parser.ExtractArray(value, is_null, issue_warning);
  if (array_parser.StoppedDueToStackSpace()) {
    return MakeEvalError() << "JSON parsing failed due to deeply nested "
                              "array/struct. Maximum nesting depth is "
                           << JSONPathExtractor::kMaxParsingDepth;
  }
  return absl::OkStatus();
}

std::optional<std::vector<JSONValueConstRef>> JsonPathEvaluator::ExtractArray(
    JSONValueConstRef input) const {
  std::optional<JSONValueConstRef> json = Extract(input);
  if (!json.has_value() || json->IsNull() || !json->IsArray()) {
    return std::nullopt;
  }

  return json->GetArrayElements();
}

absl::Status JsonPathEvaluator::ExtractStringArray(
    absl::string_view json, std::vector<std::optional<std::string>>* value,
    bool* is_null) const {
  json_internal::JSONPathStringArrayExtractor array_parser(
      json, path_iterator_.get());
  value->clear();
  array_parser.ExtractStringArray(value, is_null);
  if (array_parser.StoppedDueToStackSpace()) {
    return MakeEvalError() << "JSON parsing failed due to deeply nested "
                              "array/struct. Maximum nesting depth is "
                           << JSONPathExtractor::kMaxParsingDepth;
  }
  return absl::OkStatus();
}

std::optional<std::vector<std::optional<std::string>>>
JsonPathEvaluator::ExtractStringArray(JSONValueConstRef input) const {
  std::optional<std::vector<JSONValueConstRef>> json_array =
      ExtractArray(input);
  if (!json_array.has_value()) {
    return std::nullopt;
  }

  std::vector<std::optional<std::string>> results;
  results.reserve(json_array->size());
  for (JSONValueConstRef element : *json_array) {
    if (element.IsArray() || element.IsObject()) {
      return std::nullopt;
    }

    if (element.IsNull()) {
      results.push_back(std::nullopt);
    } else if (element.IsString()) {
      // ToString() adds extra quotes and escapes special characters,
      // which we don't want.
      results.push_back(element.GetString());
    } else {
      results.push_back(element.ToString());
    }
  }
  return results;
}

std::string ConvertJSONPathTokenToSqlStandardMode(
    absl::string_view json_path_token) {
  // See json_internal.cc for list of characters that don't need escaping.
  static const LazyRE2 kSpecialCharsPattern = {R"([^\p{L}\p{N}\d_\-\:\s])"};
  static const LazyRE2 kDoubleQuotesPattern = {R"(")"};

  if (!RE2::PartialMatch(json_path_token, *kSpecialCharsPattern)) {
    // No special characters. Can be field access or array element access.
    // Note that '$[0]' is equivalent to '$.0'.
    return std::string(json_path_token);
  } else if (absl::StrContains(json_path_token, "\"")) {
    // We need to escape double quotes in the json_path_token because the SQL
    // standard mode use them to wrap around json_path_token with special
    // characters.
    std::string escaped(json_path_token);
    // Two backslashes are needed in the replacement string because \<digit>
    // is used for group matching.
    RE2::GlobalReplace(&escaped, *kDoubleQuotesPattern, R"(\\")");
    return absl::StrCat("\"", escaped, "\"");
  } else {
    // Special characters but no double quotes.
    return absl::StrCat("\"", json_path_token, "\"");
  }
}

absl::StatusOr<std::string> ConvertJSONPathToSqlStandardMode(
    absl::string_view json_path) {
  // See json_internal.cc for list of characters that don't need escaping.
  static const LazyRE2 kSpecialCharsPattern = {R"([^\p{L}\p{N}\d_\-\:\s])"};
  static const LazyRE2 kDoubleQuotesPattern = {R"(")"};

  GOOGLESQL_ASSIGN_OR_RETURN(auto iterator, json_internal::ValidJSONPathIterator::Create(
                                      json_path, /*sql_standard_mode=*/false));

  std::string new_json_path = "$";

  // First token is empty.
  ++(*iterator);

  for (; !iterator->End(); ++(*iterator)) {
    // Token is unescaped.
    absl::string_view token = **iterator;
    if (token.empty()) {
      // Special case: empty token needs to be escaped.
      absl::StrAppend(&new_json_path, ".\"\"");
    } else if (!RE2::PartialMatch(token, *kSpecialCharsPattern)) {
      // No special characters. Can be field access or array element access.
      // Note that '$[0]' is equivalent to '$.0'.
      absl::StrAppend(&new_json_path, ".", token);
    } else if (absl::StrContains(token, "\"")) {
      // We need to escape double quotes in the token because the SQL standard
      // mode use them to wrap around token with special characters.
      std::string escaped(token);
      // Two backslashes are needed in the replacement string because \<digit>
      // is used for group matching.
      RE2::GlobalReplace(&escaped, *kDoubleQuotesPattern, R"(\\")");
      absl::StrAppend(&new_json_path, ".\"", escaped, "\"");
    } else {
      // Special characters but no double quotes.
      absl::StrAppend(&new_json_path, ".\"", token, "\"");
    }
  }

  GOOGLESQL_RET_CHECK_OK(json_internal::IsValidJSONPath(new_json_path,
                                              /*sql_standard_mode=*/true));

  return new_json_path;
}

absl::StatusOr<std::string> MergeJSONPathsIntoSqlStandardMode(
    absl::Span<const std::string> json_paths) {
  if (json_paths.empty()) {
    return absl::OutOfRangeError("Empty JSONPaths.");
  }

  std::string merged_json_path = "$";

  for (absl::string_view json_path : json_paths) {
    if (json_internal::IsValidJSONPath(json_path, /*sql_standard_mode=*/true)
            .ok()) {
      // Remove the "$" prefix.
      absl::StrAppend(&merged_json_path, json_path.substr(1));
    } else {
      // Convert to SQL standard mode first.
      GOOGLESQL_ASSIGN_OR_RETURN(std::string sql_standard_json_path,
                       ConvertJSONPathToSqlStandardMode(json_path));

      absl::StrAppend(&merged_json_path, sql_standard_json_path.substr(1));
    }
  }

  GOOGLESQL_RET_CHECK_OK(json_internal::IsValidJSONPath(merged_json_path,
                                              /*sql_standard_mode=*/true));

  return merged_json_path;
}

absl::StatusOr<int64_t> ConvertJsonToInt64(JSONValueConstRef input) {
  int64_t output;
  if (input.IsInt64()) {
    return input.GetInt64();
  }
  if (input.IsDouble()) {
    if (googlesql_base::LosslessConvert(input.GetDouble(), &output)) {
      return output;
    }
    return MakeEvalError() << "The provided JSON number: " << input.GetDouble()
                           << " cannot be converted to an int64";
  }
  return MakeEvalError() << "The provided JSON input is not an integer";
}

absl::StatusOr<int32_t> ConvertJsonToInt32(JSONValueConstRef input) {
  int32_t output;
  if (input.IsInt64()) {
    if (googlesql_base::LosslessConvert(input.GetInt64(), &output)) {
      return output;
    }
    return MakeEvalError() << "The provided JSON number: " << input.GetInt64()
                           << " cannot be converted to an int32";
  }
  if (input.IsDouble()) {
    if (googlesql_base::LosslessConvert(input.GetDouble(), &output)) {
      return output;
    }
    return MakeEvalError() << "The provided JSON number: " << input.GetDouble()
                           << " cannot be converted to an int32";
  }
  return MakeEvalError() << "The provided JSON input is not an integer";
}

absl::StatusOr<uint32_t> ConvertJsonToUint32(JSONValueConstRef input) {
  uint32_t output;
  if (input.IsUInt64()) {
    if (googlesql_base::LosslessConvert(input.GetUInt64(), &output)) {
      return output;
    }
    return MakeEvalError() << "The provided JSON number: " << input.GetUInt64()
                           << " cannot be converted to an uint32";
  }
  if (input.IsInt64()) {
    if (googlesql_base::LosslessConvert(input.GetInt64(), &output)) {
      return output;
    }
    return MakeEvalError() << "The provided JSON number: " << input.GetInt64()
                           << " cannot be converted to an uint32";
  }
  if (input.IsDouble()) {
    if (googlesql_base::LosslessConvert(input.GetDouble(), &output)) {
      return output;
    }
    return MakeEvalError() << "The provided JSON number: " << input.GetDouble()
                           << " cannot be converted to an uint32";
  }
  return MakeEvalError() << "The provided JSON input is not an integer";
}

absl::StatusOr<uint64_t> ConvertJsonToUint64(JSONValueConstRef input) {
  uint64_t output;
  if (input.IsUInt64()) {
    return input.GetUInt64();
  }
  if (input.IsInt64()) {
    if (googlesql_base::LosslessConvert(input.GetInt64(), &output)) {
      return output;
    }
    return MakeEvalError() << "The provided JSON number: " << input.GetInt64()
                           << " cannot be converted to an uint64";
  }
  if (input.IsDouble()) {
    if (googlesql_base::LosslessConvert(input.GetDouble(), &output)) {
      return output;
    }
    return MakeEvalError() << "The provided JSON number: " << input.GetDouble()
                           << " cannot be converted to an uint64";
  }
  return MakeEvalError() << "The provided JSON input is not an integer";
}

absl::StatusOr<bool> ConvertJsonToBool(JSONValueConstRef input) {
  if (!input.IsBoolean()) {
    return MakeEvalError() << "The provided JSON input is not a boolean";
  }
  return input.GetBoolean();
}

absl::StatusOr<std::string> ConvertJsonToString(JSONValueConstRef input) {
  if (!input.IsString()) {
    return MakeEvalError() << "The provided JSON input is not a string";
  }
  return input.GetString();
}

absl::StatusOr<double> ConvertJsonToDouble(JSONValueConstRef input,
                                           WideNumberMode wide_number_mode,
                                           ProductMode product_mode) {
  std::string type_name =
      product_mode == PRODUCT_EXTERNAL ? "FLOAT64" : "DOUBLE";

  if (input.IsDouble()) {
    return input.GetDouble();
  }

  if (input.IsInt64()) {
    int64_t value = input.GetInt64();
    if (wide_number_mode == functions::WideNumberMode::kExact &&
        (value < kMinLosslessInt64ValueForJson ||
         value > kMaxLosslessInt64ValueForJson)) {
      return MakeEvalError()
             << "JSON number: " << value << " cannot be converted to "
             << type_name << " without loss of precision";
    }
    return double{static_cast<double>(value)};
  }
  if (input.IsUInt64()) {
    uint64_t value = input.GetUInt64();
    if (wide_number_mode == functions::WideNumberMode::kExact &&
        value > static_cast<uint64_t>(kMaxLosslessInt64ValueForJson)) {
      return MakeEvalError()
             << "JSON number: " << value << " cannot be converted to "
             << type_name << " without loss of precision";
    }
    return double{static_cast<double>(value)};
  }

  return MakeEvalError() << "The provided JSON input is not a number";
}

absl::StatusOr<float> ConvertJsonToFloat(JSONValueConstRef input,
                                         WideNumberMode wide_number_mode,
                                         ProductMode product_mode) {
  std::string type_name =
      product_mode == PRODUCT_EXTERNAL ? "FLOAT32" : "FLOAT";

  float output;
  if (input.IsDouble()) {
    double value = input.GetDouble();
    if (googlesql_base::LosslessConvert(value, &output)) {
      return output;
    }
    if (wide_number_mode == functions::WideNumberMode::kExact) {
      return MakeEvalError()
             << "JSON number: " << value << " cannot be converted to "
             << type_name << " without loss of precision";
    }
    // A static_cast can result in +/-INF, so check if input is in range.
    absl::Status status;
    if (!Convert(value, &output, &status)) {
      return MakeEvalError() << "JSON number: " << value
                             << " cannot be converted to " << type_name;
    }
    return output;
  }
  if (input.IsInt64()) {
    int64_t value = input.GetInt64();
    if (googlesql_base::LosslessConvert(value, &output)) {
      return output;
    }
    if (wide_number_mode == functions::WideNumberMode::kExact) {
      return MakeEvalError()
             << "JSON number: " << value << " cannot be converted to "
             << type_name << " without loss of precision";
    }
    return static_cast<float>(value);
  }
  if (input.IsUInt64()) {
    uint64_t value = input.GetUInt64();
    if (googlesql_base::LosslessConvert(value, &output)) {
      return output;
    }
    if (wide_number_mode == functions::WideNumberMode::kExact) {
      return MakeEvalError()
             << "JSON number: " << value << " cannot be converted to "
             << type_name << " without loss of precision";
    }
    return static_cast<float>(value);
  }

  return MakeEvalError() << "The provided JSON input is not a number";
}

template <typename T>
absl::StatusOr<std::vector<T>> ConvertJsonToArray(
    JSONValueConstRef input,
    absl::FunctionRef<absl::StatusOr<T>(JSONValueConstRef)> converter) {
  if (!input.IsArray()) {
    return MakeEvalError() << "The provided JSON input is not an array";
  }

  std::vector<T> result;
  result.reserve(input.GetArraySize());
  for (int i = 0; i < input.GetArraySize(); ++i) {
    GOOGLESQL_ASSIGN_OR_RETURN(*std::back_inserter(result),
                     converter(input.GetArrayElement(i)));
  }
  return result;
}

absl::StatusOr<std::vector<int64_t>> ConvertJsonToInt64Array(
    JSONValueConstRef input) {
  return ConvertJsonToArray<int64_t>(input, ConvertJsonToInt64);
}

absl::StatusOr<std::vector<int32_t>> ConvertJsonToInt32Array(
    JSONValueConstRef input) {
  return ConvertJsonToArray<int32_t>(input, ConvertJsonToInt32);
}

absl::StatusOr<std::vector<uint64_t>> ConvertJsonToUint64Array(
    JSONValueConstRef input) {
  return ConvertJsonToArray<uint64_t>(input, ConvertJsonToUint64);
}

absl::StatusOr<std::vector<uint32_t>> ConvertJsonToUint32Array(
    JSONValueConstRef input) {
  return ConvertJsonToArray<uint32_t>(input, ConvertJsonToUint32);
}

absl::StatusOr<std::vector<bool>> ConvertJsonToBoolArray(
    JSONValueConstRef input) {
  return ConvertJsonToArray<bool>(input, ConvertJsonToBool);
}

absl::StatusOr<std::vector<std::string>> ConvertJsonToStringArray(
    JSONValueConstRef input) {
  return ConvertJsonToArray<std::string>(input, ConvertJsonToString);
}

absl::StatusOr<std::vector<double>> ConvertJsonToDoubleArray(
    JSONValueConstRef input, WideNumberMode mode, ProductMode product_mode) {
  return ConvertJsonToArray<double>(
      input, [mode, product_mode](JSONValueConstRef input) {
        return ConvertJsonToDouble(input, mode, product_mode);
      });
}

absl::StatusOr<std::vector<float>> ConvertJsonToFloatArray(
    JSONValueConstRef input, WideNumberMode mode, ProductMode product_mode) {
  return ConvertJsonToArray<float>(
      input, [mode, product_mode](JSONValueConstRef input) {
        return ConvertJsonToFloat(input, mode, product_mode);
      });
}

absl::StatusOr<std::string> GetJsonType(JSONValueConstRef input) {
  if (input.IsNumber()) {
    return "number";
  }
  if (input.IsString()) {
    return "string";
  }
  if (input.IsBoolean()) {
    return "boolean";
  }
  if (input.IsObject()) {
    return "object";
  }
  if (input.IsArray()) {
    return "array";
  }
  if (input.IsNull()) {
    return "null";
  }
  GOOGLESQL_RET_CHECK_FAIL()
      << "Invalid JSON value that doesn't belong to any known JSON type";
}

template <typename FromType, typename ToType>
static std::optional<ToType> ConvertNumericToNumeric(FromType val) {
  absl::Status status;
  ToType out;
  if (!Convert(val, &out, &status)) {
    return std::nullopt;
  }
  return out;
}

template <typename Type>
static std::optional<std::string> ConvertNumericToString(Type val) {
  absl::Status status;
  std::string out;
  if (!NumericToString(val, &out, &status, /*canonicalize_zero=*/true)) {
    return std::nullopt;
  }
  return out;
}

template <typename Type>
static std::optional<Type> ConvertStringToNumeric(absl::string_view val) {
  absl::Status status;
  Type out;
  if (!StringToNumeric(val, &out, &status)) {
    return std::nullopt;
  }
  return out;
}

absl::StatusOr<std::optional<bool>> LaxConvertJsonToBool(
    JSONValueConstRef input) {
  if (input.IsBoolean()) {
    return input.GetBoolean();
  } else if (input.IsInt64()) {
    return input.GetInt64() != 0;
  } else if (input.IsUInt64()) {
    return input.GetUInt64() != 0;
  } else if (input.IsDouble()) {
    return input.GetDouble() != 0;
  } else if (input.IsString()) {
    return ConvertStringToNumeric<bool>(input.GetString());
  }
  return std::nullopt;
}

template <typename T>
absl::StatusOr<std::optional<T>> LaxConvertJsonToInt(JSONValueConstRef input) {
  if (input.IsBoolean()) {
    return input.GetBoolean() ? 1 : 0;
  } else if (input.IsInt64()) {
    return ConvertNumericToNumeric<int64_t, T>(input.GetInt64());
  } else if (input.IsUInt64()) {
    return ConvertNumericToNumeric<uint64_t, T>(input.GetUInt64());
  } else if (input.IsDouble()) {
    return ConvertNumericToNumeric<double, T>(input.GetDouble());
  } else if (input.IsString()) {
    BigNumericValue big_numeric_value;
    absl::Status status;
    T out;
    if (!StringToNumeric(input.GetString(), &big_numeric_value, &status) ||
        !Convert(big_numeric_value, &out, &status)) {
      return std::nullopt;
    }
    return out;
  }
  return std::nullopt;
}

absl::StatusOr<std::optional<int64_t>> LaxConvertJsonToInt64(
    JSONValueConstRef input) {
  return LaxConvertJsonToInt<int64_t>(input);
}

absl::StatusOr<std::optional<int32_t>> LaxConvertJsonToInt32(
    JSONValueConstRef input) {
  return LaxConvertJsonToInt<int32_t>(input);
}

absl::StatusOr<std::optional<uint64_t>> LaxConvertJsonToUint64(
    JSONValueConstRef input) {
  return LaxConvertJsonToInt<uint64_t>(input);
}

absl::StatusOr<std::optional<uint32_t>> LaxConvertJsonToUint32(
    JSONValueConstRef input) {
  return LaxConvertJsonToInt<uint32_t>(input);
}

template <typename T>
absl::StatusOr<std::optional<T>> LaxConvertJsonToFloat(
    JSONValueConstRef input) {
  // Note that unlike integers, we don't convert booleans into floats.
  if (input.IsInt64()) {
    return ConvertNumericToNumeric<int64_t, T>(input.GetInt64());
  } else if (input.IsUInt64()) {
    return ConvertNumericToNumeric<uint64_t, T>(input.GetUInt64());
  } else if (input.IsDouble()) {
    return ConvertNumericToNumeric<double, T>(input.GetDouble());
  } else if (input.IsString()) {
    return ConvertStringToNumeric<T>(input.GetString());
  }
  return std::nullopt;
}

absl::StatusOr<std::optional<double>> LaxConvertJsonToFloat64(
    JSONValueConstRef input) {
  return LaxConvertJsonToFloat<double>(input);
}

absl::StatusOr<std::optional<float>> LaxConvertJsonToFloat32(
    JSONValueConstRef input) {
  return LaxConvertJsonToFloat<float>(input);
}

absl::StatusOr<std::optional<std::string>> LaxConvertJsonToString(
    JSONValueConstRef input) {
  if (input.IsBoolean()) {
    return ConvertNumericToString<bool>(input.GetBoolean());
  } else if (input.IsInt64()) {
    return ConvertNumericToString<int64_t>(input.GetInt64());
  } else if (input.IsUInt64()) {
    return ConvertNumericToString<uint64_t>(input.GetUInt64());
  } else if (input.IsDouble()) {
    return ConvertNumericToString<double>(input.GetDouble());
  } else if (input.IsString()) {
    return input.GetString();
  }
  return std::nullopt;
}

template <typename T>
absl::StatusOr<std::optional<std::vector<std::optional<T>>>>
LaxConvertJsonToArray(
    JSONValueConstRef input,
    absl::FunctionRef<absl::StatusOr<std::optional<T>>(JSONValueConstRef)>
        converter) {
  if (!input.IsArray()) {
    return std::nullopt;
  }

  std::vector<std::optional<T>> result;
  result.reserve(input.GetArraySize());
  for (int i = 0; i < input.GetArraySize(); ++i) {
    GOOGLESQL_ASSIGN_OR_RETURN(*std::back_inserter(result),
                     converter(input.GetArrayElement(i)));
  }
  return result;
}

absl::StatusOr<std::optional<std::vector<std::optional<bool>>>>
LaxConvertJsonToBoolArray(JSONValueConstRef input) {
  return LaxConvertJsonToArray<bool>(input, LaxConvertJsonToBool);
}

absl::StatusOr<std::optional<std::vector<std::optional<int64_t>>>>
LaxConvertJsonToInt64Array(JSONValueConstRef input) {
  return LaxConvertJsonToArray<int64_t>(input, LaxConvertJsonToInt64);
}

absl::StatusOr<std::optional<std::vector<std::optional<int32_t>>>>
LaxConvertJsonToInt32Array(JSONValueConstRef input) {
  return LaxConvertJsonToArray<int32_t>(input, LaxConvertJsonToInt32);
}

absl::StatusOr<std::optional<std::vector<std::optional<uint64_t>>>>
LaxConvertJsonToUint64Array(JSONValueConstRef input) {
  return LaxConvertJsonToArray<uint64_t>(input, LaxConvertJsonToUint64);
}

absl::StatusOr<std::optional<std::vector<std::optional<uint32_t>>>>
LaxConvertJsonToUint32Array(JSONValueConstRef input) {
  return LaxConvertJsonToArray<uint32_t>(input, LaxConvertJsonToUint32);
}

absl::StatusOr<std::optional<std::vector<std::optional<double>>>>
LaxConvertJsonToFloat64Array(JSONValueConstRef input) {
  return LaxConvertJsonToArray<double>(input, LaxConvertJsonToFloat64);
}

absl::StatusOr<std::optional<std::vector<std::optional<float>>>>
LaxConvertJsonToFloat32Array(JSONValueConstRef input) {
  return LaxConvertJsonToArray<float>(input, LaxConvertJsonToFloat32);
}

absl::StatusOr<std::optional<std::vector<std::optional<std::string>>>>
LaxConvertJsonToStringArray(JSONValueConstRef input) {
  return LaxConvertJsonToArray<std::string>(input, LaxConvertJsonToString);
}

absl::StatusOr<JSONValue> JsonArray(absl::Span<const Value> args,
                                    const LanguageOptions& language_options,
                                    bool canonicalize_zero) {
  JSONValue json;
  JSONValueRef json_ref = json.GetRef();
  json_ref.SetToEmptyArray();
  if (args.empty()) {
    return json;
  }

  json_ref.GetArrayElement(args.size() - 1);
  for (size_t i = 0; i < args.size(); ++i) {
    const Value& arg = args[i];
    JSONValueRef ref = json_ref.GetArrayElement(i);
    GOOGLESQL_ASSIGN_OR_RETURN(JSONValue value,
                     ToJson(arg, /*stringify_wide_numbers=*/false,
                            language_options, canonicalize_zero));
    ref.Set(std::move(value));
  }
  return json;
}

absl::StatusOr<bool> JsonObjectBuilder::Add(absl::string_view key,
                                            const Value& value) {
  if (!keys_set_.insert(key).second) {
    // Duplicate key, simply return.
    return false;
  }
  JSONValueRef ref = result_.GetRef().GetMember(key);
  GOOGLESQL_ASSIGN_OR_RETURN(JSONValue json_value,
                   ToJson(value, /*stringify_wide_numbers=*/false, options_,
                          canonicalize_zero_));
  ref.Set(std::move(json_value));
  return true;
}

JSONValue JsonObjectBuilder::Build() {
  JSONValue result = std::move(result_);
  Reset();
  return result;
}

void JsonObjectBuilder::Reset() {
  result_ = JSONValue();
  result_.GetRef().SetToEmptyObject();
  keys_set_.clear();
}

absl::StatusOr<JSONValue> JsonObject(absl::Span<const absl::string_view> keys,
                                     absl::Span<const Value*> values,
                                     JsonObjectBuilder& builder) {
  if (keys.size() != values.size()) {
    return MakeEvalError() << "The number of keys and values must match";
  }

  for (size_t i = 0; i < keys.size(); ++i) {
    auto status = builder.Add(keys[i], *values[i]).status();
    if (!status.ok()) {
      builder.Reset();
      return status;
    }
  }
  return builder.Build();
}

absl::StatusOr<bool> JsonRemove(JSONValueRef input,
                                StrictJSONPathIterator& path_iterator) {
  path_iterator.Rewind();

  // First token is always empty.
  ++path_iterator;

  if (path_iterator.End()) {
    // `path` is '$'
    return MakeEvalError() << "The JSONPath cannot be '$'";
  }

  for (; !path_iterator.End(); ++path_iterator) {
    const StrictJSONPathToken& token = *path_iterator;

    if (const std::string* key = token.MaybeGetObjectKey();
        input.IsObject() && key != nullptr) {
      if (path_iterator.NoSuffixToken()) {
        auto success = input.RemoveMember(*key);
        GOOGLESQL_RET_CHECK_OK(success.status());
        return *success;
      }
      if (std::optional<JSONValueRef> member = input.GetMemberIfExists(*key);
          member.has_value()) {
        input = *member;
        continue;
      }
    } else if (const int64_t* index = token.MaybeGetArrayIndex();
               input.IsArray() && index != nullptr) {
      if (path_iterator.NoSuffixToken()) {
        auto success = input.RemoveArrayElement(*index);
        GOOGLESQL_RET_CHECK_OK(success.status());
        return *success;
      }
      if (*index >= 0 && *index < input.GetArraySize()) {
        input = input.GetArrayElement(static_cast<size_t>(*index));
        continue;
      }
    }
    // Nonexistent member, invalid array index or type mismatch. Do nothing
    // and exit.
    return false;
  }

  // This should never be reached.
  GOOGLESQL_RET_CHECK_FAIL();
}

namespace {

// How to add elements to the array.
enum class AddType {
  // Insert the element(s) at the index in the array.
  kInsert = 0,
  // Append the element(s) at the end of the array.
  kAppend,
};

absl::Status JsonAddArrayElement(JSONValueRef input,
                                 StrictJSONPathIterator& path_iterator,
                                 const Value& value,
                                 const LanguageOptions& language_options,
                                 bool canonicalize_zero, bool add_each_element,
                                 AddType add_type) {
  // We convert `value` into JSONValue first and return an error if conversion
  // fails, before even checking whether the path exists or not.
  GOOGLESQL_RET_CHECK(value.is_valid());
  std::vector<JSONValue> elements_to_insert;
  if (add_each_element && value.type()->IsArray()) {
    if (value.is_null()) {
      // If the value is a NULL ARRAY ignore the operation.
      return absl::OkStatus();
    }
    // If the value to be inserted is an array and add_each_element is true,
    // the function adds each element separately instead of a single JSON
    // array value.
    elements_to_insert.reserve(value.num_elements());
    for (const Value& element : value.elements()) {
      GOOGLESQL_ASSIGN_OR_RETURN(
          JSONValue e,
          functions::ToJson(element, /*stringify_wide_numbers=*/false,
                            language_options, canonicalize_zero));
      elements_to_insert.push_back(std::move(e));
    }
  } else {
    GOOGLESQL_ASSIGN_OR_RETURN(JSONValue e,
                     functions::ToJson(value, /*stringify_wide_numbers=*/false,
                                       language_options, canonicalize_zero));
    elements_to_insert.push_back(std::move(e));
  }

  path_iterator.Rewind();
  // First token is always empty.
  ++path_iterator;

  // Only contains a value for kInsert.
  std::optional<int64_t> index_to_insert;

  for (; !path_iterator.End(); ++path_iterator) {
    const StrictJSONPathToken& token = *path_iterator;
    if (add_type == AddType::kInsert && path_iterator.NoSuffixToken()) {
      // This is the last token. It has to be an array index for inserts.
      if (const int64_t* index = token.MaybeGetArrayIndex(); index != nullptr) {
        index_to_insert = *index;
      }
      // For inserts, the last token indicates the position in the array to
      // insert the value, so do not go down the JSON tree. The next iteration
      // will exit the loop.
      continue;
    }

    if (const std::string* key = token.MaybeGetObjectKey();
        input.IsObject() && key != nullptr) {
      if (std::optional<JSONValueRef> member = input.GetMemberIfExists(*key);
          member.has_value()) {
        input = *member;
        continue;
      }
    } else if (const int64_t* index = token.MaybeGetArrayIndex();
               input.IsArray() && index != nullptr) {
      if (*index >= 0 && *index < input.GetArraySize()) {
        input = input.GetArrayElement(*index);
        continue;
      }
    }
    // Inexistent member, invalid array index or type mismatch. Do nothing and
    // exit.
    return absl::OkStatus();
  }

  GOOGLESQL_RET_CHECK(path_iterator.End());

  if (!input.IsArray() && !input.IsNull()) {
    // Do nothing.
    return absl::OkStatus();
  }

  if (add_type == AddType::kInsert && !index_to_insert.has_value()) {
    // Do nothing in that case.
    return absl::OkStatus();
  }

  bool was_null = false;
  if (input.IsNull()) {
    was_null = true;
    input.SetToEmptyArray();
  }

  absl::Status status;
  if (add_each_element && value.type()->IsArray()) {
    if (add_type == AddType::kInsert) {
      status = input.InsertArrayElements(std::move(elements_to_insert),
                                         *index_to_insert);
    } else {
      status = input.AppendArrayElements(std::move(elements_to_insert));
    }
  } else {
    GOOGLESQL_RET_CHECK_EQ(elements_to_insert.size(), 1);
    if (add_type == AddType::kInsert) {
      status = input.InsertArrayElement(std::move(elements_to_insert[0]),
                                        *index_to_insert);
    } else {
      status = input.AppendArrayElement(std::move(elements_to_insert[0]));
    }
  }

  if (!status.ok()) {
    // If there was an error, make sure the original value is not modified.
    if (was_null) {
      input.SetNull();
    }
    GOOGLESQL_RET_CHECK(absl::IsOutOfRange(status));
  }

  return status;
}

}  // namespace

absl::Status JsonInsertArrayElement(JSONValueRef input,
                                    StrictJSONPathIterator& path_iterator,
                                    const Value& value,
                                    const LanguageOptions& language_options,
                                    bool canonicalize_zero,
                                    bool insert_each_element) {
  return JsonAddArrayElement(input, path_iterator, value, language_options,
                             canonicalize_zero, insert_each_element,
                             AddType::kInsert);
}

absl::Status JsonAppendArrayElement(JSONValueRef input,
                                    StrictJSONPathIterator& path_iterator,
                                    const Value& value,
                                    const LanguageOptions& language_options,
                                    bool canonicalize_zero,
                                    bool append_each_element) {
  return JsonAddArrayElement(input, path_iterator, value, language_options,
                             canonicalize_zero, append_each_element,
                             AddType::kAppend);
}

absl::Status JsonSet(JSONValueRef input, StrictJSONPathIterator& path_iterator,
                     const Value& value, bool create_if_missing,
                     const LanguageOptions& language_options,
                     bool canonicalize_zero) {
  // Ensure we always start from the beginning of the path.
  path_iterator.Rewind();
  // First token is always empty (no-op).
  ++path_iterator;

  GOOGLESQL_ASSIGN_OR_RETURN(JSONValue converted_value,
                   functions::ToJson(value, /*stringify_wide_numbers=*/false,
                                     language_options, canonicalize_zero));

  // The input path is '$'. This implies that we replace the entire value.
  if (path_iterator.End()) {
    input.Set(std::move(converted_value));
    return absl::OkStatus();
  }

  // Walk down the JSON tree.
  //
  // Cases for each token in path:
  // 1) If token in path exists in current JSON element, continue processing
  //    the JSON subtree with the next path token.
  // 2) If the member doesn't exist or the array index is out of bounds or
  //    the current JSON element is null, then exit the loop. Auto-creation
  //    will happen next.
  // 3) If there is a type mismatch, this is not a valid Set operation so
  //    ignore operation and return early.
  for (; !path_iterator.End(); ++path_iterator) {
    const StrictJSONPathToken& token = *path_iterator;
    if (auto* key = token.MaybeGetObjectKey();
        input.IsObject() && key != nullptr) {
      if (auto member = input.GetMemberIfExists(*key); member.has_value()) {
        input = *member;
        continue;
      } else {
        // Member doesn't exist.
        break;
      }
    } else if (auto* index = token.MaybeGetArrayIndex();
               input.IsArray() && index != nullptr) {
      // Negative indexes should have thrown an error during path validation.
      if (ABSL_PREDICT_FALSE(*index < 0)) {
        return MakeEvalError()
               << "Negative indexes are not supported in JSON paths.";
      }
      if (*index < input.GetArraySize()) {
        input = input.GetArrayElement(*index);
        continue;
      } else {
        // Array index doesn't exist.
        break;
      }
    } else if (input.IsNull()) {
      // Auto-creation is allowed on JSON 'null'.
      break;
    }
    // Type mismatch, ignore operation and return early.
    return absl::OkStatus();
  }

  // The path doesn't exist and we only replace existing values. Ignore
  // operation.
  if (!path_iterator.End() && !create_if_missing) {
    return absl::OkStatus();
  }

  if (!path_iterator.End()) {
    // Auto-creation will happen. Make sure it won't create an oversized
    // array.
    size_t path_position = path_iterator.Depth() - 1;
    for (; !path_iterator.End(); ++path_iterator) {
      const StrictJSONPathToken& token = *path_iterator;
      auto* index = token.MaybeGetArrayIndex();
      if (index == nullptr) {
        // Nothing to worry about for object creation.
        continue;
      }
      // Negative indexes should have thrown an error during path validation.
      if (ABSL_PREDICT_FALSE(*index < 0)) {
        return MakeEvalError()
               << "Negative indexes are not supported in JSON paths.";
      }
      if (ABSL_PREDICT_FALSE(*index >= kJSONMaxArraySize)) {
        return MakeEvalError()
               << "Exceeded maximum array size of " << kJSONMaxArraySize;
      }
    }
    path_iterator.Rewind();
    for (int i = 0; i < path_position; ++i) {
      ++path_iterator;
    }
  }

  // Auto-creation if !path_iterator.End()
  for (; !path_iterator.End(); ++path_iterator) {
    const StrictJSONPathToken& token = *path_iterator;
    if (auto* key = token.MaybeGetObjectKey(); key != nullptr) {
      GOOGLESQL_RET_CHECK(input.IsObject() || input.IsNull());
      input = input.GetMember(*key);
    } else {
      auto* index = token.MaybeGetArrayIndex();
      GOOGLESQL_RET_CHECK(index != nullptr);
      GOOGLESQL_RET_CHECK(input.IsArray() || input.IsNull());
      // If `index` is larger than the length of the JSON array, it
      // is automatically resized with null elements.
      input = input.GetArrayElement(*index);
    }
  }

  input.Set(std::move(converted_value));
  return absl::OkStatus();
}

namespace {

struct JsonValueNode {
  JSONValueRef node;
  // Set to true after all children have been processed.
  bool processed = false;
};

inline bool IsComplexType(JSONValueRef ref) {
  return ref.IsObject() || ref.IsArray();
}

absl::Status StripNullsImpl(JSONValueRef input, bool include_arrays,
                            JSONValueRef::RemoveEmptyOptions options) {
  GOOGLESQL_RET_CHECK(options == JSONValueRef::RemoveEmptyOptions::kNone ||
            options == JSONValueRef::RemoveEmptyOptions::kObject ||
            options == JSONValueRef::RemoveEmptyOptions::kObjectAndArray);
  if (!IsComplexType(input)) {
    // Nothing to process.
    return absl::OkStatus();
  }
  JSONValueRef root = input;
  std::stack<JsonValueNode> stack;
  stack.push({
      .node = input,
  });
  while (!stack.empty()) {
    auto& stack_element = stack.top();
    JSONValueRef json_node = stack_element.node;
    // Have not processed `json_node` yet. Process all children.
    if (!stack_element.processed) {
      if (json_node.IsObject()) {
        for (auto& [key, value] : json_node.GetMembers()) {
          if (IsComplexType(value)) {
            stack.push({.node = value});
          }
        }
      } else if (json_node.IsArray()) {
        for (auto& array_element : json_node.GetArrayElements()) {
          if (IsComplexType(array_element)) {
            stack.push({.node = array_element});
          }
        }
      }
      stack_element.processed = true;
    } else {
      // All children of `json_node` have been processed and we can now
      // safely cleanup `json_node`.
      stack.pop();
      if (json_node.IsObject()) {
        GOOGLESQL_RETURN_IF_ERROR(json_node.CleanupJsonObject(options));
      } else if (include_arrays) {
        GOOGLESQL_RET_CHECK(json_node.IsArray());
        GOOGLESQL_RETURN_IF_ERROR(json_node.CleanupJsonArray(options));
      }
    }
  }

  // If the JSON value is "{}" or "[]" and `remove_empty`, set the JSON value
  // to JSON 'null'.
  if ((options == JSONValueRef::RemoveEmptyOptions::kObject ||
       options == JSONValueRef::RemoveEmptyOptions::kObjectAndArray) &&
      root.IsObject() && root.GetObjectSize() == 0) {
    root.SetNull();
  } else if (options == JSONValueRef::RemoveEmptyOptions::kObjectAndArray &&
             root.IsArray() && root.GetArraySize() == 0) {
    root.SetNull();
  }
  return absl::OkStatus();
}

}  // namespace

absl::Status JsonStripNulls(JSONValueRef input,
                            StrictJSONPathIterator& path_iterator,
                            bool include_arrays, bool remove_empty) {
  path_iterator.Rewind();
  // First token is always empty.
  ++path_iterator;

  // Match the subtree pointed to by `path_iterator`. If the path doesn't
  // exist in `input` do nothing and return early.
  for (; !path_iterator.End(); ++path_iterator) {
    const StrictJSONPathToken& token = *path_iterator;
    if (auto* key = token.MaybeGetObjectKey();
        input.IsObject() && key != nullptr) {
      if (std::optional<JSONValueRef> member = input.GetMemberIfExists(*key);
          member.has_value()) {
        input = *member;
        continue;
      }
    } else if (auto* index = token.MaybeGetArrayIndex();
               input.IsArray() && index != nullptr) {
      // Negative indexes should have thrown an error during path validation.
      if (ABSL_PREDICT_FALSE(*index < 0)) {
        return MakeEvalError()
               << "Negative indexes are not supported in JSON paths.";
      }
      if (*index < input.GetArraySize()) {
        input = input.GetArrayElement(*index);
        continue;
      }
    }
    // Nonexistent member, invalid array index, or type mismatch. Ignore
    // operation and return early.
    return absl::OkStatus();
  }

  JSONValueRef::RemoveEmptyOptions options;
  if (remove_empty) {
    options = include_arrays ? JSONValueRef::RemoveEmptyOptions::kObjectAndArray
                             : JSONValueRef::RemoveEmptyOptions::kObject;
  } else {
    options = JSONValueRef::RemoveEmptyOptions::kNone;
  }

  return StripNullsImpl(input, include_arrays, options);
}

namespace {

class JsonTreeWalker {
 public:
  JsonTreeWalker(JSONValueConstRef root, StrictJSONPathIterator* path_iterator);

  // Processes matches for the provided JSONPath given the JSON tree.
  absl::Status Process();

  // Fetch the computed result. In order for the result to be valid must have
  // called Process().
  //
  // Result only valid on first call.
  JSONValue ConsumeResult() { return std::move(output_); }

 private:
  // Handles a matched JSONValue.
  absl::Status HandleMatch(JSONValueConstRef json_value);

  struct StackElement {
   public:
    // The current index of the JSONPathToken to process in the
    // StrictJSONPathIterator. If `path_token_index` == path_iterator.size(),
    // this indicates we have hit the end of the JSONPath and there are no
    // tokens left to process.
    int path_token_index;
    // Matched subtree for a processed JSONPath prefix.
    JSONValueConstRef subtree;
  };

  // If path_token referenced by `stack_element` contains an object
  // key(string type), performs lax match operation in the `subtree`, updates
  // `path_element_stack_`, and returns true. Returns false if path_token is
  // not an object key which indicates no operation is performed.
  bool MaybeProcessObjectKey(const StackElement& stack_element);
  // If path_token referenced by `stack_element` contains an array
  // index(int type), performs lax match operation in the `subtree`, updates
  // `path_element_stack_`, and returns true. Returns false if path_token is
  // not an array index which indicates no operation is performed.
  bool MaybeProcessArrayIndex(const StackElement& stack_element);

  // Represents the tuples of <path_token_index, JSONValue> to process.
  std::vector<StackElement> path_element_stack_;
  // The results.
  JSONValue output_;
  StrictJSONPathIterator* path_iterator_;
};

JsonTreeWalker::JsonTreeWalker(JSONValueConstRef root,
                               StrictJSONPathIterator* path_iterator)
    : path_iterator_(path_iterator) {
  // Push the first JSONPathToken and root JSON to process. The 0th index of
  // `path_iterator` is a no-op '$' token and we simply skip this.
  path_element_stack_.push_back({.path_token_index = 1, .subtree = root});
  output_.GetRef().SetToEmptyArray();
}

absl::Status JsonTreeWalker::HandleMatch(JSONValueConstRef json_value) {
  // Add the matched JSON to output JSON Array.
  GOOGLESQL_RETURN_IF_ERROR(
      output_.GetRef().AppendArrayElement(JSONValue::CopyFrom(json_value)));
  return absl::OkStatus();
}

absl::Status JsonTreeWalker::Process() {
  while (!path_element_stack_.empty()) {
    StackElement stack_element = path_element_stack_.back();
    path_element_stack_.pop_back();

    if (stack_element.path_token_index == path_iterator_->Size()) {
      // We have finished processing the entire JSONPath. Add matched element
      // to output.
      GOOGLESQL_RETURN_IF_ERROR(HandleMatch(stack_element.subtree));
      continue;
    }

    // The current path token expects an object.
    //
    // If failed to process, something has gone really wrong here. The
    // JSONPathToken is guaranteed to be an object key or an array index.
    // Reasoning: The beginning no-op token('$') is already processed.
    GOOGLESQL_RET_CHECK(MaybeProcessObjectKey(stack_element) ||
              MaybeProcessArrayIndex(stack_element))
        << "Unexpected JSONPathToken type encountered during "
           "JSON_QUERY_LAX matching.";
  }
  return absl::OkStatus();
}

bool JsonTreeWalker::MaybeProcessObjectKey(const StackElement& stack_element) {
  const std::string* key =
      path_iterator_->GetToken(stack_element.path_token_index)
          .MaybeGetObjectKey();
  if (key == nullptr) {
    // The JSONPathToken is not an object key.
    return false;
  }
  JSONValueConstRef subtree = stack_element.subtree;
  if (subtree.IsObject()) {
    // Path token and JSON subtree match OBJECT types.
    auto maybe_member = subtree.GetMemberIfExists(*key);
    if (!maybe_member.has_value()) {
      // The JSON OBJECT does not contain the path token. This branch is not
      // a match.
      return true;
    }
    // Match the next path token.
    path_element_stack_.push_back(
        {.path_token_index = stack_element.path_token_index + 1,
         .subtree = *maybe_member});
  } else if (subtree.IsArray()) {
    // The JSON subtree is an array and path token expects an object. We add
    // elements in reverse order to stack as we need to add matched JSON
    // path results in DFS in-order traversal.
    const auto& elements = subtree.GetArrayElements();
    for (auto it = elements.rbegin(); it != elements.rend(); ++it) {
      JSONValueConstRef element = *it;
      if (element.IsObject()) {
        auto maybe_member = element.GetMemberIfExists(*key);
        if (!maybe_member.has_value()) {
          // The object doesn't contain the JSONPathToken. No match for this
          // branch. Continue processing rest of array elements.
          continue;
        }
        path_element_stack_.push_back(
            {.path_token_index = stack_element.path_token_index + 1,
             .subtree = *maybe_member});
      } else if (path_iterator_->GetJsonPathOptions().recursive &&
                 element.IsArray()) {
        // This is a nested array element. Only process if `recursive` is set
        // to true. Else, this branch is not a match.
        path_element_stack_.push_back(
            {.path_token_index = stack_element.path_token_index,
             .subtree = element});
      }
      // Scalar value. No match for this branch.
    }
  }
  // If `subtree` is not an object or an array this indicates it's a scalar
  // value. A scalar value indicates no match for this branch so no further
  // processing is required.
  return true;
}

bool JsonTreeWalker::MaybeProcessArrayIndex(const StackElement& stack_element) {
  const int64_t* array_index =
      path_iterator_->GetToken(stack_element.path_token_index)
          .MaybeGetArrayIndex();
  if (array_index == nullptr) {
    // The JSONPathToken is not an array index.
    return false;
  }
  JSONValueConstRef subtree = stack_element.subtree;
  if (subtree.IsArray()) {
    // The subtree matches expected JSONPathToken array type.
    if (*array_index < subtree.GetArraySize()) {
      JSONValueConstRef matched_element = subtree.GetArrayElement(*array_index);
      path_element_stack_.push_back(
          {.path_token_index = stack_element.path_token_index + 1,
           .subtree = matched_element});
    }
    // The JSONPathToken array_index is larger than the size of the array.
    // This branch is not a match.
  } else if (*array_index == 0) {
    // The subtree is not an array. If the `array_index` = 0 implicitly
    // autowrap and access the 0th element. Essentially an `array_index` = 0
    // is a no-op JSONPathToken.
    path_element_stack_.push_back(
        {.path_token_index = stack_element.path_token_index + 1,
         .subtree = subtree});
  }
  return true;
}

}  // namespace

absl::StatusOr<JSONValue> JsonQueryLax(JSONValueConstRef input,
                                       StrictJSONPathIterator& path_iterator) {
  GOOGLESQL_RET_CHECK(path_iterator.GetJsonPathOptions().lax);
  JsonTreeWalker walker(input, &path_iterator);
  GOOGLESQL_RETURN_IF_ERROR(walker.Process());
  return walker.ConsumeResult();
}

bool JsonContainsImpl(JSONValueConstRef input, JSONValueConstRef target,
                      bool is_top_level) {
  if (target.IsObject()) {
    if (!input.IsObject()) {
      return false;
    }
    for (const auto& [key, value] : target.GetMembers()) {
      if (!input.HasMember(key) ||
          !JsonContainsImpl(input.GetMember(key), value,
                            /*is_top_level=*/false)) {
        return false;
      }
    }
    return true;
  }

  if (target.IsArray()) {
    if (!input.IsArray()) {
      return false;
    }
    for (const auto& element : target.GetArrayElements()) {
      // Find element from input JSON array.
      bool exists = false;
      for (const auto& source : input.GetArrayElements()) {
        if (JsonContainsImpl(source, element, /*is_top_level=*/false)) {
          exists = true;
          break;
        }
      }
      if (!exists) {
        return false;
      }
    }
    return true;
  }

  // The `target` is a scalar value.
  if (input.IsObject()) {
    return false;
  }
  if (input.IsArray()) {
    if (!is_top_level) {
      // Only top-level array `input` will be checked with scalar `target`.
      return false;
    }
    for (const auto& source : input.GetArrayElements()) {
      if (JsonContainsImpl(source, target, /*is_top_level=*/false)) {
        return true;
      }
    }
    return false;
  }

  // The `input` and `target` are both a scalar value.
  return input.NormalizedEquals(target);
}

bool JsonContains(JSONValueConstRef input, JSONValueConstRef target) {
  return JsonContainsImpl(input, target, /*is_top_level=*/true);
}

namespace {

class JsonKeysTreeWalker {
 public:
  JsonKeysTreeWalker(JSONValueConstRef root, const JSONKeysOptions& options)
      : options_(options) {
    path_element_stack_.push_back(
        {.subtree = root, .remaining_depth = options.max_depth, .key = ""});
  }

  // Processes keys for the provided JSONPath given the JSON tree.
  void Process();

  // Fetch the resulting keys. In order for the result to be valid must have
  // called Process().
  //
  // Result only valid on first call.
  std::vector<std::string> ConsumeResult() {
    std::vector<std::string> keys;
    keys.reserve(keys_.size());
    keys.insert(keys.end(), std::make_move_iterator(keys_.begin()),
                std::make_move_iterator(keys_.end()));
    keys_.clear();
    return keys;
  }

 private:
  struct StackElement {
   public:
    // The current subtree left to process.
    JSONValueConstRef subtree;
    int64_t remaining_depth;
    // The current JSONPath key prefix.
    std::string key;
  };

  std::string EscapeKey(absl::string_view key) {
    std::string escaped_key;
    // In addition to the normal key escaping, we need to escape the key if
    // contains a '.'.
    //
    // For example:
    // JSON '{"a.b": {"c": 1}}'
    // Escaped Key: "\"a.b\".c"
    if (googlesql::JsonStringNeedsEscaping(key) ||
        absl::StrContains(key, ".")) {
      googlesql::JsonEscapeString(key, &escaped_key);
    } else {
      escaped_key = key;
    }
    return escaped_key;
  }

  absl::btree_set<std::string> keys_;
  std::vector<StackElement> path_element_stack_;
  JSONKeysOptions options_;
};

void JsonKeysTreeWalker::Process() {
  while (!path_element_stack_.empty()) {
    StackElement stack_element = std::move(path_element_stack_.back());
    path_element_stack_.pop_back();
    JSONValueConstRef subtree = stack_element.subtree;
    if (subtree.IsObject()) {
      int64_t remaining_depth = stack_element.remaining_depth - 1;
      for (const auto& [key, value] : subtree.GetMembers()) {
        std::string new_key;
        if (stack_element.key.empty()) {
          new_key = EscapeKey(key);
        } else {
          new_key = absl::StrCat(stack_element.key, ".", EscapeKey(key));
        }
        keys_.insert(new_key);
        // Continue processing subtree.
        if (remaining_depth > 0) {
          path_element_stack_.push_back({.subtree = value,
                                         .remaining_depth = remaining_depth,
                                         .key = std::move(new_key)});
        }
      }
    }
    if (subtree.IsArray() &&
        options_.path_options != JsonPathOptions::kStrict) {
      for (const JSONValueConstRef& element : subtree.GetArrayElements()) {
        if (element.IsObject()) {
          path_element_stack_.push_back(
              {.subtree = element,
               .remaining_depth = stack_element.remaining_depth,
               .key = stack_element.key});
        } else if (element.IsArray() &&
                   options_.path_options == JsonPathOptions::kLaxRecursive) {
          // If recursive, then process nested arrays.
          path_element_stack_.push_back(
              {.subtree = element,
               .remaining_depth = stack_element.remaining_depth,
               .key = stack_element.key});
        }
        // Nothing left to do.
      }
    }
  }
}

}  // namespace

absl::StatusOr<std::vector<std::string>> JsonKeys(
    JSONValueConstRef input, const JSONKeysOptions& options) {
  // Check for valid arguments.
  if (options.max_depth <= 0) {
    return MakeEvalError() << "max_depth must be positive.";
  }

  JsonKeysTreeWalker walker(input, options);
  walker.Process();
  return walker.ConsumeResult();
}

std::vector<JSONValueConstRef> JsonFlatten(JSONValueConstRef input) {
  // Avoid the overhead of setting up traversal for these trivial cases.
  if (!input.IsArray()) {
    return {input};
  }
  if (input.GetArraySize() == 0) {
    return {};
  }

  // Stores every non-array value visited during traversal.
  std::vector<JSONValueConstRef> result;
  result.reserve(input.GetArraySize());

  // The stack stores `ArrayContext`s (an array and position into that array).
  // Every time we encounter a new array, we mark the current position and
  // push a new context onto the stack. Once an array is fully traversed, we
  // pop it off the stack and pick back up from the previous position. This
  // allows us to enforce ordering expectations while maintaining efficient
  // traversal of shallow/scalar arrays.
  struct ArrayContext {
    JSONValueConstRef array;
    size_t index;
  };
  std::stack<ArrayContext, absl::InlinedVector<ArrayContext, 10>> stack;
  stack.push({input, 0});

  while (!stack.empty()) {
    ArrayContext& top = stack.top();
    while (top.index < top.array.GetArraySize()) {
      JSONValueConstRef element = top.array.GetArrayElement(top.index++);
      if (top.index >= top.array.GetArraySize()) {
        // Pop the current array context off the stack once we have visited all
        // of its elements.
        stack.pop();
      }

      if (!element.IsArray()) {
        result.push_back(element);
      } else if (element.GetArraySize() > 0) {
        // Push a new context onto the stack, and break out of this loop so we
        // start traversing the nested array.
        stack.push({element, 0});
        break;
      }
    }
  }

  return result;
}

}  // namespace functions
}  // namespace googlesql
