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

#include "googlesql/parser/tokenizer.h"

#include "googlesql/parser/tm_lexer.h"
#include "googlesql/parser/tm_token.h"
#include "googlesql/public/parse_location.h"
#include "absl/flags/flag.h"
#include "googlesql/base/check.h"
#include "absl/strings/string_view.h"
#include "googlesql/base/status_macros.h"

// TODO: Remove flag when references are gone.
ABSL_FLAG(bool, googlesql_use_customized_flex_istream, true, "Unused");

namespace googlesql {
namespace parser {

absl::StatusOr<Token> GoogleSqlTokenizer::GetNextToken(
    ParseLocationRange* location) {
  Token token = Next();
  *location = LastTokenLocationWithStartOffset();
  GOOGLESQL_RETURN_IF_ERROR(override_error_);
  return token;
}

GoogleSqlTokenizer::GoogleSqlTokenizer(absl::string_view filename,
                                       absl::string_view input,
                                       int start_offset)
    // We do not use Lexer::Rewind() because its time complexity is
    // O(start_offset). See the comment for `Lexer::start_offset_` in
    // googlesql.tm for more information.
    : Lexer(absl::ClippedSubstr(input, start_offset)) {
  filename_ = filename;
  start_offset_ = start_offset;
}

}  // namespace parser
}  // namespace googlesql
