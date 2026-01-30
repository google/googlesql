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

#include "googlesql/common/unicode_utils.h"

#include <string>

#include "googlesql/base/logging.h"
#include "absl/algorithm/container.h"
#include "absl/base/nullability.h"
#include "absl/flags/flag.h"
#include "absl/log/log.h"
#include "absl/strings/ascii.h"
#include "absl/strings/escaping.h"
#include "absl/strings/string_view.h"
#include "unicode/bytestream.h"
#include "unicode/errorcode.h"
#include "unicode/normalizer2.h"

ABSL_FLAG(bool, googlesql_idstring_allow_unicode_characters, false,
          "Enables case insensitive comparison for idstrings with unicode "
          "characters.");

namespace {

const icu::Normalizer2* /*absl_nullable*/ GetCaseFoldingNormalizer() {
  static const icu::Normalizer2* normalizer = []() -> const icu::Normalizer2* {
    icu::ErrorCode error_code;
    const icu::Normalizer2* normalizer =
        icu::Normalizer2::getNFKCCasefoldInstance(error_code);

    if (error_code.isFailure()) {
      ABSL_LOG(ERROR) << "Failed to get a casefolding normalizer instance: "
                 << error_code.errorName();
      return nullptr;
    }
    return normalizer;
  }();

  return normalizer;
}

}  // namespace

namespace googlesql {

std::string GetNormalizedAndCasefoldedString(absl::string_view str) {
  if (!absl::GetFlag(FLAGS_googlesql_idstring_allow_unicode_characters) ||
      absl::c_all_of(str, absl::ascii_isascii)) {
    return absl::AsciiStrToLower(str);
  }

  const icu::Normalizer2* normalizer = GetCaseFoldingNormalizer();
  if (normalizer == nullptr) {
    LOG_EVERY_N_SEC(ERROR, 5) << "Failed to get a normalizer instance. "
                              << "Falling back to ASCII lowercase.";
    return absl::AsciiStrToLower(str);
  }

  icu::ErrorCode error_code;
  std::string result;
  result.reserve(str.size());
  icu::StringByteSink<std::string> sink(&result);
  normalizer->normalizeUTF8(/*options=*/0, str, sink, /*edits=*/nullptr,
                            error_code);
  if (error_code.isFailure()) {
    LOG_EVERY_N_SEC(ERROR, 5)
        << "Failed to normalize input: " << error_code.errorName()
        << ". Input: " << absl::Utf8SafeCHexEscape(str)
        << ". Falling back to ASCII lowercase.";
    result = absl::AsciiStrToLower(str);
  }
  return result;
}

}  // namespace googlesql
