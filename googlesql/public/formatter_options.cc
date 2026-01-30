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

#include "googlesql/public/formatter_options.h"

#include <string>

#include "googlesql/public/formatter_options.pb.h"

namespace googlesql {

FormatterOptions::FormatterOptions(const FormatterOptionsProto& proto)
    : FormatterOptions() {
  if (proto.has_new_line_type()) {
    new_line_type_ = proto.new_line_type();
  }
  if (proto.has_line_length_limit()) {
    line_length_limit_ = proto.line_length_limit();
  }
  if (proto.has_indentation_spaces()) {
    indentation_spaces_ = proto.indentation_spaces();
  }
  if (proto.has_allow_invalid_tokens()) {
    allow_invalid_tokens_ = proto.allow_invalid_tokens();
  }
  if (proto.has_capitalize_keywords()) {
    capitalize_keywords_ = proto.capitalize_keywords();
  }
  if (proto.has_preserve_line_breaks()) {
    preserve_line_breaks_ = proto.preserve_line_breaks();
  }
  if (proto.has_expand_format_ranges()) {
    expand_format_ranges_ = proto.expand_format_ranges();
  }
  if (proto.has_enforce_single_quotes()) {
    enforce_single_quotes_ = proto.enforce_single_quotes();
  }
  if (proto.has_format_structured_strings()) {
    format_structured_strings_ = proto.format_structured_strings();
  }
  if (proto.has_format_comments()) {
    format_comments_ = proto.format_comments();
  }
}

}  // namespace googlesql
