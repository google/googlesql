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

#include "googlesql/public/collator.h"

#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "googlesql/base/logging.h"
#include "googlesql/common/errors.h"
#include "absl/memory/memory.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/cord.h"
#include "absl/strings/str_split.h"
#include "absl/strings/string_view.h"
#include "unicode/coll.h"
#include "unicode/errorcode.h"
#include "unicode/tblcoll.h"
#include "unicode/utypes.h"
#include "googlesql/base/ret_check.h"

namespace googlesql {
namespace {

static absl::Status MakeCollationError(absl::string_view collation_name,
                                       absl::string_view error = {}) {
  if (error.empty()) {
    return MakeEvalError() << "COLLATE has invalid collation name '"
                           << collation_name << "'";
  } else {
    return MakeEvalError() << "COLLATE has invalid collation name '"
                           << collation_name << "':" << error;
  }
}

enum class CollatorMode {
  kInvalidCollatorMode,
  kBinary,
  kIcu
};

static absl::Status ValidateLanguageTag(
    absl::string_view language_tag,
    absl::string_view collation_name_for_error) {
  return absl::OkStatus();
}
constexpr absl::string_view kUndLanguageTag = "und";

static absl::Status ValidateCollationName(
    absl::string_view collation_name,
    CollatorLegacyUnicodeMode legacy_unicode_mode,
    CollatorMode& collator_mode_out,
    std::optional<std::string>& icu_language_tag_out,
    std::optional<bool>& case_insensitive_out) {
  if (collation_name.empty()) {
    return MakeCollationError(collation_name,
                              "cannot contain empty language_tag");
  }
  collator_mode_out = CollatorMode::kInvalidCollatorMode;
  icu_language_tag_out = std::nullopt;
  case_insensitive_out = std::nullopt;  // default is case-sensitive

  const std::vector<absl::string_view> parts =
      absl::StrSplit(collation_name, ':');
  GOOGLESQL_RET_CHECK_GT(parts.size(), 0);
  absl::string_view raw_language_tag = parts[0];

  if (raw_language_tag.empty()) {
    return MakeCollationError(collation_name,
                              "cannot contain empty language_tag");
  }

  if (raw_language_tag == "binary") {
    // Handle 'binary'
    if (parts.size() > 1) {
      return MakeCollationError(collation_name,
                                "binary cannot be combined with a suffix");
    }
    collator_mode_out = CollatorMode::kBinary;
    // Binary has no tag, and no concept of case sensitivity.
    icu_language_tag_out = std::nullopt;
    case_insensitive_out = std::nullopt;
    return absl::OkStatus();
  }

  bool is_legacy_unicode_tag = false;

  if (raw_language_tag == "unicode") {
    if (legacy_unicode_mode == CollatorLegacyUnicodeMode::kError) {
      // 'unicode' is not allowed at all.
      return MakeCollationError(collation_name);
    }
    is_legacy_unicode_tag = true;
  } else {
    GOOGLESQL_RETURN_IF_ERROR(ValidateLanguageTag(raw_language_tag, collation_name));
  }

  if (parts.size() > 2) {
    // We only support case-sensitivity as a collation attribute now. So,
    // specifying multiple attributes is not allowed.
    return MakeCollationError(collation_name,
                              "only case sensitivity attribute is supported");
  }
  bool case_insensitive = false;
  if (parts.size() == 2) {
    absl::string_view suffix = parts[1];
    if (suffix == "ci") {
      case_insensitive = true;
    } else if (suffix == "cs") {
      case_insensitive = false;
    } else {
      return MakeCollationError(collation_name,
                                "case sensitivity must be 'ci' or 'cs'");
    }
  }
  if (is_legacy_unicode_tag && case_insensitive) {
    // This is the weird case.
    if (legacy_unicode_mode == CollatorLegacyUnicodeMode::kLegacyIcuOnly) {
      collator_mode_out = CollatorMode::kIcu;
      icu_language_tag_out = kUndLanguageTag;
      case_insensitive_out = case_insensitive;
      return absl::OkStatus();
    }

    return absl::Status(
        absl::StatusCode::kInternal,
        absl::StrCat("invalid legacy_unicode_mode: ", legacy_unicode_mode));
  }
  if (is_legacy_unicode_tag && !case_insensitive) {
    // Regular binary comparison
    collator_mode_out = CollatorMode::kBinary;
    icu_language_tag_out = std::nullopt;
    case_insensitive_out = std::nullopt;
    return absl::OkStatus();
  } else /* !is_legacy_unicode_tag */ {
    // Normal icu case.
    collator_mode_out = CollatorMode::kIcu;
    icu_language_tag_out = raw_language_tag;
    case_insensitive_out = case_insensitive;
    return absl::OkStatus();
  }
}

class GoogleSqlCollatorIcu : public GoogleSqlCollator {
 public:
  GoogleSqlCollatorIcu(
      absl::string_view collation_name, CollatorMode mode,
      std::unique_ptr<const icu::RuleBasedCollator> icu_collator);
  ~GoogleSqlCollatorIcu() override = default;

  int64_t CompareUtf8(absl::string_view s1, absl::string_view s2,
                      absl::Status* error) const override;

  absl::Status GetSortKeyUtf8(absl::string_view input,
                              absl::Cord* output) const override;

  bool IsBinaryComparison() const override {
    return mode_ == CollatorMode::kBinary;
  }

  const icu::RuleBasedCollator* GetIcuCollator() const override {
    return icu_collator_.get();
  }

 private:
  const CollatorMode mode_;
  const std::unique_ptr<const icu::RuleBasedCollator> icu_collator_;
};

GoogleSqlCollatorIcu::GoogleSqlCollatorIcu(
    absl::string_view collation_name, CollatorMode mode,
    std::unique_ptr<const icu::RuleBasedCollator> icu_collator)
    : GoogleSqlCollator(collation_name),
      mode_(mode),
      icu_collator_(std::move(icu_collator)) {}

int64_t GoogleSqlCollatorIcu::CompareUtf8(const absl::string_view s1,
                                          const absl::string_view s2,
                                          absl::Status* error) const {
  if (mode_ == CollatorMode::kBinary) {
    const int result = s1.compare(s2);
    return result < 0 ? -1 : (result > 0 ? 1 : 0);
  } else if (mode_ != CollatorMode::kIcu || icu_collator_ == nullptr) {
    *error = absl::Status(absl::StatusCode::kInternal,
                          absl::StrCat("Unknown collation mode: ", mode_));
    return 0;
  }

  icu::ErrorCode icu_error;

  UCollationResult result = icu_collator_->compareUTF8(
      icu::StringPiece(s1.data(), static_cast<int32_t>(s1.size())),
      icu::StringPiece(s2.data(), static_cast<int32_t>(s2.size())), icu_error);
  if (icu_error.isFailure()) {
    *error = absl::Status(absl::StatusCode::kInvalidArgument,
                          "Strings cannot be compared with the collator");
    icu_error.reset();
  }
  // UCollationResult is a three valued enum UCOL_EQUAL, UCOL_LESS AND
  // UCOL_GREATER.
  static_assert(UCOL_LESS == -1, "compareUTF8 result conversion");
  static_assert(UCOL_EQUAL == 0, "compareUTF8 result conversion");
  static_assert(UCOL_GREATER == 1, "compareUTF8 result conversion");
  return result;
}

absl::Status GoogleSqlCollatorIcu::GetSortKeyUtf8(absl::string_view input,
                                                  absl::Cord* output) const {
  if (mode_ == CollatorMode::kBinary) {
    // Binary collation, so just copy the input.
    output->Clear();
    output->Append(input);
    return absl::OkStatus();
  }

  GOOGLESQL_RET_CHECK(icu_collator_ != nullptr);

  // Convert the input UTF-8 string to UTF-16 (which is what collation API
  // unfortunately expects).
  icu::UnicodeString input_unicode = icu::UnicodeString::fromUTF8(input);

  // We don't know how large the output buffer needs to be. Per recommendation
  // on http://userguide.icu-project.org/collation/api#TOC-GetSortKey we create
  // a small buffer on stack. If it is insufficient, we reallocate it on heap.
  // Otherwise we allocate the exact needed size and call getSortKey again.
  char initial_output_buffer[512];
  int32_t output_size = icu_collator_->getSortKey(
      input_unicode, reinterpret_cast<uint8_t*>(initial_output_buffer),
      sizeof(initial_output_buffer));
  GOOGLESQL_RET_CHECK_NE(output_size, 0) << "Failed to generate the sort key";
  if (output_size <= sizeof(initial_output_buffer)) {
    // Chop off the NULL terminator (which we don't need for Cord).
    *output =
        absl::Cord(absl::string_view(initial_output_buffer, output_size - 1));
    return absl::OkStatus();
  }

  // The output buffer was not large enough, but now we know how large the
  // buffer really needs to be.
  const int32_t output_buffer_size = output_size;
  std::unique_ptr<char[]> output_buffer(new char[output_buffer_size]);
  output_size = icu_collator_->getSortKey(
      input_unicode, reinterpret_cast<uint8_t*>(output_buffer.get()),
      output_buffer_size);
  GOOGLESQL_RET_CHECK_GT(output_size, 0) << "Failed to generate the sort key";
  GOOGLESQL_RET_CHECK_LE(output_size, output_buffer_size)
      << "Insufficient buffer for the sort key";

  // Return the output buffer as Cord chopping off the NULL terminator (which we
  // don't need for Cord).
  *output = absl::MakeCordFromExternal(
      {output_buffer.release(), static_cast<size_t>(output_size - 1)},
      [](absl::string_view s) { delete[] s.data(); });
  return absl::OkStatus();
}

}  // namespace

absl::StatusOr<std::unique_ptr<const GoogleSqlCollator>> MakeSqlCollator(
    absl::string_view collation_name,
    CollatorLegacyUnicodeMode legacy_unicode_mode) {
  CollatorMode collator_mode = CollatorMode::kInvalidCollatorMode;
  std::optional<std::string> icu_language_tag;
  std::optional<bool> case_insensitive;

  GOOGLESQL_RETURN_IF_ERROR(ValidateCollationName(collation_name, legacy_unicode_mode,
                                        collator_mode, icu_language_tag,
                                        case_insensitive));

  GOOGLESQL_RET_CHECK(collator_mode != CollatorMode::kInvalidCollatorMode);

  if (collator_mode == CollatorMode::kBinary
      ) {
    // Don't need icu for this case.
    return std::make_unique<const GoogleSqlCollatorIcu>(
        collation_name, collator_mode,
        /*icu_collator=*/nullptr);
  }

  std::unique_ptr<icu::RuleBasedCollator> icu_collator;
  GOOGLESQL_RET_CHECK(icu_language_tag.has_value() && !icu_language_tag->empty());
  GOOGLESQL_RET_CHECK(case_insensitive.has_value());

  icu::Locale locale = icu::Locale::createCanonical(icu_language_tag->c_str());
  if (locale.isBogus()) {
    return MakeCollationError(collation_name);
  }
  icu::ErrorCode icu_error;
  icu_collator.reset(
      (icu::RuleBasedCollator*)icu::RuleBasedCollator::createInstance(
          locale, icu_error));
  if (icu_error.isFailure() || icu_collator == nullptr) {
    icu_error.reset();
    return MakeCollationError(collation_name, absl::StrCat(" is invalid - ",
        icu_error.errorName()));
  }

  if (*case_insensitive) {
    // Setting the icu::Collator strength to SECONDARY will ignore case
    // level comparisons.
    icu_collator->setStrength(icu::Collator::SECONDARY);
  } else {
    // We do nothing here as comparisons are case-sensitive by default in
    // icu::RuleBasedCollator.
  }
  return std::make_unique<const GoogleSqlCollatorIcu>(
      collation_name, collator_mode, std::move(icu_collator));
}

}  // namespace googlesql
