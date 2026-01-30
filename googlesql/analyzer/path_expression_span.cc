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

#include "googlesql/analyzer/path_expression_span.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <string>
#include <vector>

#include "googlesql/parser/parse_tree.h"
#include "googlesql/public/id_string.h"
#include "googlesql/public/parse_location.h"
#include "googlesql/public/strings.h"
#include "absl/base/casts.h"
#include "googlesql/base/check.h"
#include "absl/status/statusor.h"
#include "absl/strings/str_cat.h"
#include "googlesql/base/ret_check.h"

namespace googlesql {

absl::StatusOr<PathExpressionSpan> PathExpressionSpan::subspan(int32_t start,
                                                               int32_t end) {
  GOOGLESQL_RET_CHECK_GE(start, 0);
  GOOGLESQL_RET_CHECK_GT(end, start);
  GOOGLESQL_RET_CHECK_LE(end, node_.num_names());
  return PathExpressionSpan(node_, start, end);
}

int32_t PathExpressionSpan::num_names() const {
  return end_index_ - start_index_;
}

const ASTIdentifier* PathExpressionSpan::first_name() const {
  return this->name(0);
}

const ASTIdentifier* PathExpressionSpan::name(int32_t relative_index) const {
  return node_.name(start_index_ + relative_index);
}

IdString PathExpressionSpan::GetFirstIdString() const {
  return this->name(0)->GetAsIdString();
}

std::vector<IdString> PathExpressionSpan::ToIdStringVector() const {
  std::vector<IdString> id_strings;
  id_strings.reserve(end_index_ - start_index_);
  for (int32_t i = start_index_; i < end_index_; ++i) {
    id_strings.push_back(node_.name(i)->GetAsIdString());
  }
  return id_strings;
}

std::vector<std::string> PathExpressionSpan::ToIdentifierVector() const {
  std::vector<std::string> identifier_strings;
  identifier_strings.reserve(end_index_ - start_index_);
  for (int32_t i = start_index_; i < end_index_; ++i) {
    identifier_strings.push_back(node_.name(i)->GetAsString());
  }
  return identifier_strings;
}

std::string PathExpressionSpan::ToIdentifierPathString(
    size_t max_prefix_size) const {
  const size_t size =
      max_prefix_size == 0
          ? num_names()
          : std::min(absl::implicit_cast<size_t>(num_names()), max_prefix_size);
  std::string ret;
  ABSL_DCHECK(absl::implicit_cast<size_t>(start_index_) + size <
         absl::implicit_cast<size_t>(std::numeric_limits<int32_t>::max()));
  for (int32_t i = start_index_; i < start_index_ + static_cast<int32_t>(size);
       ++i) {
    if (i != start_index_) {
      absl::StrAppend(&ret, ".");
    }
    absl::StrAppend(&ret,
                    ToIdentifierLiteral(node_.name(i)->GetAsStringView()));
  }
  return ret;
}

ParseLocationRange PathExpressionSpan::GetParseLocationRange() {
  ParseLocationRange parse_location_range;
  parse_location_range.set_start(node_.name(start_index_)->location().start());
  parse_location_range.set_end(node_.name(end_index_ - 1)->location().end());
  return parse_location_range;
}

}  // namespace googlesql
