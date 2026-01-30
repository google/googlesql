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

#include "googlesql/public/functions/regexp.h"

#include <ctype.h>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "googlesql/base/logging.h"
#include "googlesql/common/utf_util.h"
#include "googlesql/public/functions/string.h"
#include "googlesql/public/functions/util.h"
#include "googlesql/public/options.pb.h"
#include "googlesql/public/type.pb.h"
#include "googlesql/public/types/struct_type.h"
#include "googlesql/public/types/type.h"
#include "googlesql/public/types/type_factory.h"
#include "googlesql/public/value.h"
#include "googlesql/base/case.h"
#include "absl/container/flat_hash_set.h"
#include "absl/memory/memory.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/string_view.h"
#include "absl/strings/substitute.h"
#include "absl/types/span.h"
#include "unicode/utf8.h"
#include "unicode/utypes.h"
#include "re2/re2.h"
#include "googlesql/base/ret_check.h"
#include "googlesql/base/status_macros.h"

namespace googlesql {
namespace functions {

bool RegExp::InitializePatternUtf8(absl::string_view pattern,
                                   absl::Status* error) {
  RE2::Options options;
  options.set_log_errors(false);
  options.set_encoding(RE2::Options::EncodingUTF8);
  return InitializeWithOptions(pattern, options, error);
}

bool RegExp::InitializePatternBytes(absl::string_view pattern,
                                    absl::Status* error) {
  RE2::Options options;
  options.set_log_errors(false);
  options.set_encoding(RE2::Options::EncodingLatin1);
  return InitializeWithOptions(pattern, options, error);
}

bool RegExp::InitializeWithOptions(absl::string_view pattern,
                                   const RE2::Options& options,
                                   absl::Status* error) {
  re_ = std::make_unique<RE2>(pattern, options);
  if (!re_->ok()) {
    return internal::UpdateError(
        error, absl::StrCat("Cannot parse regular expression: ", re_->error()));
  }
  return true;
}

bool RegExp::Contains(absl::string_view str, bool* out,
                      absl::Status* error) const {
  ABSL_DCHECK(re_);
  *out = re_->PartialMatch(str, *re_);
  return true;
}

bool RegExp::Match(absl::string_view str, bool* out,
                   absl::Status* error) const {
  ABSL_DCHECK(re_);
  *out = re_->FullMatch(str, *re_);
  return true;
}

bool RegExp::Extract(absl::string_view str, PositionUnit position_unit,
                     int64_t position, int64_t occurrence_index,
                     bool use_legacy_position_behavior, absl::string_view* out,
                     bool* is_null, absl::Status* error) const {
  ABSL_DCHECK(re_);
  *is_null = true;
  *error = internal::ValidatePositionAndOccurrence(position, occurrence_index);
  if (!error->ok()) {
    return false;
  }
  int32_t str_length32 = 0;
  if (!CheckAndCastStrLength(str, &str_length32)) {
    return internal::UpdateError(
        error,
        absl::Substitute("Input string size too large $0", str.length()));
  }
  if (position > str_length32 && !(str.empty() && position == 1)) {
    return true;
  }
  int64_t offset = 0;
  if (position_unit == kUtf8Chars) {
    auto string_offset = ForwardN(str, str_length32, position - 1);
    if (string_offset == std::nullopt) {
      return true;
    }
    offset = string_offset.value();
  } else {
    offset = position - 1;
  }

  // If use_legacy_position_behavior is true, the offset is used to
  // left-truncate the input string before matching.
  if (use_legacy_position_behavior) {
    str.remove_prefix(offset);
    offset = 0;
  }

  if (str.data() == nullptr) {
    // Ensure that the output string never has a null data pointer if a match is
    // found.
    str = absl::string_view("", 0);
  }
  ExtractAllIterator iter = CreateExtractAllIterator(str, offset);

  for (int64_t current_index = 0; current_index < occurrence_index;
       ++current_index) {
    if (!iter.Next(out, error)) {
      return error->ok();
    }
    if (!error->ok()) return false;
  }
  if (out->data() != nullptr) *is_null = false;
  return true;
}

RegExp::ExtractAllIterator::ExtractAllIterator(const RE2* re,
                                               absl::string_view str,
                                               int64_t offset)
    : re_(re), extract_all_input_(str) {
  extract_all_position_ = (offset > 0) ? offset : 0;
}

bool RegExp::ExtractAllIterator::Next(absl::string_view* out,
                                      absl::Status* error) {
  if (re_->NumberOfCapturingGroups() > 1) {
    return internal::UpdateError(error,
                                 "Regular expressions passed into extraction "
                                 "functions must not have more "
                                 "than 1 capturing group");
  }
  if (last_match_) {
    *out = absl::string_view(nullptr, 0);
    return false;
  }
  absl::string_view groups[2];
  if (!re_->Match(extract_all_input_,
                  extract_all_position_,      // startpos
                  extract_all_input_.size(),  // endpos
                  RE2::UNANCHORED, groups,
                  2)) {  // number of matches at out.
    *out = absl::string_view(nullptr, 0);
    // No matches found in the remaining of the input string.
    return false;
  }

  extract_all_position_ = groups[0].end() - extract_all_input_.begin();
  if (re_->NumberOfCapturingGroups() == 0) {
    // If there's no capturing subgroups return the entire matching substring.
    *out = groups[0];
  } else {
    // If there's a single capturing group return substring matching that
    // group.
    *out = groups[1];
    capture_group_position_ =
        static_cast<int>(groups[1].end() - extract_all_input_.begin());
  }
  // RE2 library produces empty groups[0] when regular expression matches empty
  // string, so in this case we need to advance input by one character.
  if (groups[0].empty() &&
      extract_all_position_ < static_cast<int64_t>(extract_all_input_.size())) {
    if (re_->options().encoding() == RE2::Options::EncodingUTF8) {
      constexpr int64_t kMaxUtf8Length = 4;
      int64_t length_after_position =
          extract_all_input_.size() - extract_all_position_;
      int32_t length_after_position32 =
          static_cast<int32_t>(std::min(kMaxUtf8Length, length_after_position));

      int32_t character_length = 0;
      UChar32 character;
      U8_NEXT(&extract_all_input_[extract_all_position_], character_length,
              length_after_position32, character);
      extract_all_position_ += character_length;

      if (character < 0) {
        error->Update(
            absl::Status(absl::StatusCode::kOutOfRange,
                         "Input argument to REGEXP_EXTRACT_ALL function "
                         "is not valid UTF8 string"));
        return false;
      }
    } else {
      ++extract_all_position_;
    }
  }
  // No more input - next call to ExtractAllNext will return false.
  if (extract_all_position_ >=
      static_cast<int64_t>(extract_all_input_.size())) {
    last_match_ = true;
  }
  return true;
}

RegExp::ExtractAllIterator RegExp::CreateExtractAllIterator(
    absl::string_view str, int64_t offset) const {
  ABSL_DCHECK(re_.get());
  return ExtractAllIterator{re_.get(), str, offset};
}

// Parses the capturing groups in the regular expression and returns the
// information about the groups. For unnamed groups, only the field_type is
// populated.
absl::StatusOr<RegExp::ParsedCapturingGroups> RegExp::ParseCapturingGroups()
    const {
  int num_groups = re_->NumberOfCapturingGroups();
  if (num_groups == 0) {
    return absl::InvalidArgumentError(
        "Regular expression does not contain any capturing groups");
  }

  // Allocate the vector beforehand, so that the string_views in
  // CapturingGroupInfo that point to the group names remain stable.
  RegExp::ParsedCapturingGroups group_infos;
  group_infos.resize(num_groups);

  absl::flat_hash_set<absl::string_view, googlesql_base::StringViewCaseHash,
                      googlesql_base::StringViewCaseEqual>
      seen_group_names;
  const auto& group_name_map = re_->CapturingGroupNames();
  for (int i = 0; i < num_groups; ++i) {
    RegExp::CapturingGroupInfo& group_info = group_infos[i];

    // Group indexes are 1-based. Record the group name and field name and
    // type_suffix for named groups. If the group is unnamed, an entry is not
    // found in group_name_map and group info fields are left empty.
    const auto it = group_name_map.find(i + 1);
    if (it != group_name_map.end()) {
      group_info.group_name = it->second;
      absl::string_view group_name(group_info.group_name);
      size_t pos = group_name.rfind("__");
      if (pos != absl::string_view::npos) {
        group_info.field_name = group_name.substr(0, pos);
        group_info.type_suffix = group_name.substr(pos + 2);
      } else {
        group_info.field_name = group_name;
      }
    }
    if (!group_info.field_name.empty() &&
        !seen_group_names.insert(group_info.field_name).second) {
      return absl::InvalidArgumentError(absl::StrCat(
          "Regular expression contains duplicate capturing group name: ",
          group_info.field_name));
    }
  }
  return group_infos;
}

absl::StatusOr<const Type*> RegExp::ExtractGroupsResultStruct(
    TypeFactory* type_factory) const {
  GOOGLESQL_ASSIGN_OR_RETURN(ParsedCapturingGroups group_infos, ParseCapturingGroups());

  const Type* field_type =
      (re_->options().encoding() == RE2::Options::EncodingUTF8)
          ? types::StringType()
          : types::BytesType();

  std::vector<StructField> struct_fields;
  struct_fields.reserve(group_infos.size());
  for (CapturingGroupInfo& group_info : group_infos) {
    struct_fields.push_back({std::string(group_info.field_name), field_type});
  }

  const Type* result_type = nullptr;
  GOOGLESQL_RETURN_IF_ERROR(type_factory->MakeStructTypeFromVector(
      std::move(struct_fields), &result_type));
  return result_type;
}

absl::StatusOr<Value> RegExp::ExtractGroups(absl::string_view str,
                                            const Type* result_type) const {
  if (!result_type->IsStruct()) {
    return absl::InternalError(
        "Output type for ExtractGroups must be a struct.");
  }
  const StructType* struct_type = result_type->AsStruct();
  const int num_groups = re_->NumberOfCapturingGroups();

  GOOGLESQL_RET_CHECK(struct_type->num_fields() != 0)
      << "Result type for ExtractGroups must have at least one field.";
  if (struct_type->num_fields() != num_groups) {
    return absl::InternalError(
        absl::StrCat("Result type for ExtractGroups must have ", num_groups,
                     " fields, but has ", struct_type->num_fields()));
  }

  const bool is_bytes = re_->options().encoding() != RE2::Options::EncodingUTF8;
  const Type* field_type = is_bytes ? types::BytesType() : types::StringType();
  for (int i = 0; i < num_groups; ++i) {
    GOOGLESQL_RET_CHECK(struct_type->field(i).type == field_type);
  }

  // The groups vector contains the entire match at index 0, followed by the
  // groups in order. We do an unanchored match; if the user wants an anchored
  // match, they should include ^ and/or $ in the regexp.
  std::vector<absl::string_view> groups(num_groups + 1);
  std::vector<Value> struct_fields(struct_type->num_fields());
  if (!re_->Match(str, 0, str.length(), RE2::UNANCHORED, groups.data(),
                  num_groups + 1)) {
    // The pattern did not match, so we return a NULL struct. This is distinct
    // from the case where the pattern matched but the groups did not capture
    // anything.
    return Value::Null(result_type);
  }

  for (int i = 0; i < num_groups; ++i) {
    const Type* field_type = struct_type->field(i).type;
    // We only want the groups, so skip the first element which is the entire
    // match.
    absl::string_view match = groups[i + 1];

    // If the match is null, the group is not matched. This is distinct from
    // match being empty, in which case the group is matched to an empty string.
    if (match.data() == nullptr) {
      struct_fields[i] = Value::Null(field_type);
      continue;
    }

    Value value = is_bytes ? Value::Bytes(match) : Value::String(match);
    struct_fields[i] = std::move(value);
  }

  return Value::MakeStruct(struct_type, struct_fields);
}

bool RegExp::Instr(const InstrParams& options,
                   bool use_legacy_position_behavior,
                   absl::Status* error) const {
  ABSL_DCHECK(re_ != nullptr);
  ABSL_DCHECK(error != nullptr);
  ABSL_DCHECK(options.out != nullptr);
  absl::string_view str = options.input_str;
  *options.out = 0;
  *error = internal::ValidatePositionAndOccurrence(options.position,
                                                   options.occurrence_index);
  if (!error->ok()) {
    return false;  // position or occurrence_index <= 0
  }
  int32_t str_length32 = 0;
  if (!CheckAndCastStrLength(str, &str_length32)) {
    return internal::UpdateError(
        error,
        absl::Substitute("Input string size too large $0", str.length()));
  }
  if (options.position > str_length32 || re_->pattern().empty()) {
    return true;
  }
  int64_t offset = 0;
  if (options.position_unit == kUtf8Chars) {
    auto string_offset = ForwardN(str, str_length32, options.position - 1);
    if (string_offset == std::nullopt) {
      return true;  // input str is an invalid utf-8 string
    }
    offset = string_offset.value();
  } else {
    offset = options.position - 1;
  }

  // If `use_legacy_position_behavior` is true, the offset is used to
  // left-truncate the input string before matching.
  if (use_legacy_position_behavior) {
    str.remove_prefix(offset);
    offset = 0;
  }

  ExtractAllIterator iter = CreateExtractAllIterator(str, offset);

  absl::string_view next_match;
  for (int64_t current_index = 0; current_index < options.occurrence_index;
       ++current_index) {
    if (!iter.Next(&next_match, error)) {
      return error->ok();
    }
    if (!error->ok()) return false;
  }
  if (next_match.data() == nullptr) {
    return true;
  }

  // If we truncated the string before matching, we need to add back the
  // number of units truncated to get the correct position. Otherwise, add 1 to
  // to account for the fact that the result uses one-based indexing.
  int64_t padding = use_legacy_position_behavior ? options.position : 1;

  int32_t visited_bytes = 0;
  if (re_->NumberOfCapturingGroups() == 0) {
    // extract_all_position_ and capture_group_position_ are the indices based
    // on bytes
    visited_bytes = iter.extract_all_position_;
  } else {
    visited_bytes = iter.capture_group_position_;
  }
  if (options.return_position == kStartOfMatch) {
    visited_bytes -= next_match.length();
  }
  if (options.position_unit == kUtf8Chars) {
    // visited_bytes is the length of bytes before the position will be returned
    // We need to convert byte length to character length if the input is a
    // utf-8 string, for example, щ is one character but takes 2 bytes
    absl::string_view prev_str;
    if (!LeftBytes(str, visited_bytes, &prev_str, error)) {
      return false;
    }
    int64_t utf8_size = 0;
    if (!LengthUtf8(prev_str, &utf8_size, error)) {
      return false;
    }
    *options.out = utf8_size + padding;
  } else {
    *options.out = visited_bytes + padding;
  }
  return true;
}

bool RegExp::Replace(absl::string_view str, absl::string_view newsub,
                     std::string* out, absl::Status* error) const {
  return Replace(str, newsub, std::numeric_limits<int32_t>::max(), out, error);
}

bool RegExp::Replace(absl::string_view str, absl::string_view newsub,
                     int32_t max_out_size, std::string* out,
                     absl::Status* error) const {
  // The following implementation is similar to RE2::GlobalReplace, with a few
  // important differences: (1) it works correctly with UTF-8 strings,
  // (2) it returns proper error message instead of logging it, and
  // (3) limits the size of output string.

  ABSL_DCHECK(re_);

  std::string error_string;
  if (!re_->CheckRewriteString(newsub, &error_string)) {
    error->Update(absl::Status(
        absl::StatusCode::kOutOfRange,
        absl::StrCat("Invalid REGEXP_REPLACE pattern: ", error_string)));
    return false;
  }

  // "newsub" can reference at most 9 capturing groups indexed 1 to 9. Index 0
  // is reserved for the entire matching substring.
  std::vector<absl::string_view> match(10);

  out->clear();
  // Position of the end of the previous match. This is necessary if the regular
  // expression can match both empty string and some non-empty string, so that
  // we don't replace an empty match immediately following non-empty match.
  // Initialized to -1, so that if both str and match are empty, the condition
  //     match[0].begin() - str.begin() != lastpos
  // below will be true once. We use a position instead of a pointer to avoid
  // C++ undefined behavior caused by adding offsets to nullptr std.begin()
  // (which can happen when when str is empty).
  ptrdiff_t lastpos = -1;
  for (absl::string_view::iterator p = str.begin(); p <= str.end();) {
    // Find the first matching substring starting at p and store the
    // match and captured groups in vector 'match'.
    if (!re_->Match(str, p - str.begin(), str.size(),
                    RE2::UNANCHORED, /* match any substring */
                    match.data(), match.size())) {
      out->append(p, str.end());
      break;
    }
    // Emit text until the start of the match verbatim, and then emit
    // the rewritten match.
    out->append(p, match[0].begin());
    p = match[0].begin();
    if (!match[0].empty()) {
      if (!Rewrite(newsub, match, max_out_size, out, error)) return false;
      p = match[0].end();
    } else {
      // The regexp matches empty substring. Ignore the match if it starts at
      // the end of the previous one.
      if (match[0].begin() - str.begin() != lastpos &&
          !Rewrite(newsub, match, max_out_size, out, error)) {
        return false;
      }
      if (p < str.end()) {
        // Move p one character forward.
        int32_t len;
        if (re_->options().encoding() == RE2::Options::EncodingUTF8) {
          int32_t char_len = 0;
          constexpr std::ptrdiff_t kMaxUtf8Length = 4;
          U8_FWD_1(p, char_len, std::min(kMaxUtf8Length, str.end() - p));
          len = char_len;
        } else {
          len = 1;
        }
        out->append(p, p + len);
        p += len;
      } else {
        break;
      }
    }
    lastpos = match[0].end() - str.begin();
  }

  return true;
}

bool RegExp::Rewrite(absl::string_view rewrite,
                     absl::Span<const absl::string_view> groups,
                     int32_t max_out_size, std::string* out,
                     absl::Status* error) const {
  const char* const end = rewrite.data() + rewrite.size();
  for (const char* s = rewrite.data(); s < end; ++s) {
    const char* start = s;
    while (s < end && *s != '\\') s++;
    out->append(start, s);

    if (s < end) {
      s++;
      int c = (s < end) ? *s : -1;
      if (isdigit(c)) {
        int n = (c - '0');
        out->append(groups[n].data(), groups[n].size());
      } else if (c == '\\') {
        out->push_back('\\');
      } else {
        error->Update(absl::Status(absl::StatusCode::kInternal,
                                   "Invalid REGEXP_REPLACE pattern"));
        return false;
      }
    }

    if (out->length() > max_out_size) {
      error->Update(absl::Status(absl::StatusCode::kOutOfRange,
                                 "REGEXP_REPLACE: exceeded maximum output "
                                 "length"));
      return false;
    }
  }
  return true;
}

absl::StatusOr<std::unique_ptr<const RegExp>> MakeRegExpUtf8(
    absl::string_view pattern) {
  RE2::Options options;
  options.set_log_errors(false);
  options.set_encoding(RE2::Options::EncodingUTF8);
  return MakeRegExpWithOptions(pattern, options);
}

absl::StatusOr<std::unique_ptr<const RegExp>> MakeRegExpBytes(
    absl::string_view pattern) {
  RE2::Options options;
  options.set_log_errors(false);
  options.set_encoding(RE2::Options::EncodingLatin1);
  return MakeRegExpWithOptions(pattern, options);
}

absl::StatusOr<std::unique_ptr<const RegExp>> MakeRegExpWithOptions(
    absl::string_view pattern, const RE2::Options& options) {
  auto re = std::make_unique<const RE2>(pattern, options);
  if (!re->ok()) {
    return internal::CreateFunctionError(
        absl::StrCat("Cannot parse regular expression: ", re->error()));
  }
  return absl::WrapUnique(new RegExp(std::move(re)));
}

}  // namespace functions
}  // namespace googlesql
