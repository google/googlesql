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

#ifndef GOOGLESQL_PUBLIC_PARSE_RESUME_LOCATION_H_
#define GOOGLESQL_PUBLIC_PARSE_RESUME_LOCATION_H_

#include <string>
#include <utility>

#include "googlesql/public/parse_resume_location.pb.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/string_view.h"
#include "googlesql/base/ret_check.h"
#include "googlesql/base/status.h"

namespace googlesql {

// This stores the parser input and a location, and is used as a restart token
// in repeated calls to operations that parse multiple items from one input
// string/string_view. Each successive call updates this location object so
// the next call knows where to start.
// The <filename> is informational, and only used for error messaging.
class ParseResumeLocation {
 public:
  ParseResumeLocation(const ParseResumeLocation& rhs) {
    *this = rhs;
  }
  ParseResumeLocation(ParseResumeLocation&& rhs) noexcept {
    // This invokes the move assignment operator.
    *this = std::move(rhs);
  }
  ParseResumeLocation& operator=(const ParseResumeLocation& rhs) {
    byte_position_ = rhs.byte_position_;
    allow_resume_ = rhs.allow_resume_;
    input_storage_ = rhs.input_storage_;
    filename_storage_ = rhs.filename_storage_;
    input_ = rhs.input_;
    filename_ = rhs.filename_;
    // The input_ and filename_ may be pointing at the rhs's storage; point
    // it at our storage instead.
    if (input_.data() == rhs.input_storage_.data()) {
      input_ = absl::string_view(input_storage_);
    }
    if (filename_.data() == rhs.filename_storage_.data()) {
      filename_ = absl::string_view(filename_storage_);
    }
    return *this;
  }
  ParseResumeLocation& operator=(ParseResumeLocation&& rhs) noexcept {
    byte_position_ = rhs.byte_position_;
    allow_resume_ = rhs.allow_resume_;
    const bool point_input_at_storage =
        (rhs.input_.data() == rhs.input_storage_.data());
    const bool point_filename_at_storage =
        (rhs.filename_.data() == rhs.filename_storage_.data());
    input_storage_ = std::move(rhs.input_storage_);
    filename_storage_ = std::move(rhs.filename_storage_);
    // The input_ may be pointing at the rhs's input_storage_. Point it at our
    // storage instead.
    if (point_input_at_storage) {
      input_ = absl::string_view(input_storage_);
    } else {
      input_ = rhs.input_;
    }
    if (point_filename_at_storage) {
      filename_ = absl::string_view(filename_storage_);
    } else {
      filename_ = rhs.filename_;
    }
    return *this;
  }

  // Creates a ParseResumeLocation that reads from the start of 'input'.
  static ParseResumeLocation FromString(std::string filename,
                                        std::string input) {
    return ParseResumeLocation(std::move(filename), std::move(input));
  }
  // Similar to the previous, but without a filename.
  static ParseResumeLocation FromString(std::string input) {
    return ParseResumeLocation(/* filename = */ "", std::move(input));
  }

  // Creates a ParseResumeLocation that reads from the start of 'input', with
  // the given 'filename' (if applicable). The data pointed at by 'input' and
  // 'filename' is not copied, and must remain valid until the parsing
  // operations are complete.
  static ParseResumeLocation FromStringView(absl::string_view filename,
                                            absl::string_view input) {
    return ParseResumeLocation(filename, input);
  }
  // Similar to the previous, but without a filename.
  static ParseResumeLocation FromStringView(absl::string_view input) {
    return ParseResumeLocation(absl::string_view(), input);
  }

  // Creates a ParseResumeLocation with the input and the byte position
  // from the proto.
  static ParseResumeLocation FromProto(const ParseResumeLocationProto& proto) {
    ParseResumeLocation ret =
        ParseResumeLocation(std::string(proto.filename()),
                            std::string(proto.input()));
    ret.allow_resume_ = proto.allow_resume();
    ret.set_byte_position(proto.byte_position());
    return ret;
  }

  absl::string_view filename() const { return filename_; }
  absl::string_view input() const { return input_; }

  int byte_position() const { return byte_position_; }
  void set_byte_position(int byte_position) {
    byte_position_ = byte_position;
  }

  bool allow_resume() const { return allow_resume_; }
  // Disallows resuming from this ParseResumeLocation. For GoogleSQL internal
  // use only.
  void DisallowResume() { allow_resume_ = false; }

  absl::Status Validate() const {
    GOOGLESQL_RET_CHECK_GE(byte_position(), 0);
    GOOGLESQL_RET_CHECK_LE(byte_position(), input().size());
    return absl::OkStatus();
  }

  void Serialize(ParseResumeLocationProto* proto) const {
    proto->set_filename(filename_);
    proto->set_input(input_);
    proto->set_byte_position(byte_position_);
    proto->set_allow_resume(allow_resume_);
  }

  // If <verbose>, includes the input SQL string that the ParseResumeLocation
  // is related to.
  std::string DebugString(bool verbose = false) const {
    std::string debug_string = absl::StrCat(filename_, ":", byte_position_);
    if (allow_resume_) {
      absl::StrAppend(&debug_string, " (allow_resume)");
    }
    if (verbose) {
      absl::StrAppend(&debug_string, ", input:\n", input_);
    }
    return debug_string;
  }

 private:
  friend class ParseResumeLocationTest;

  // The parse will start from the beginning of the 'input' string.
  ParseResumeLocation(std::string filename, std::string input)
      : filename_storage_(std::move(filename)),
        filename_(filename_storage_),
        input_storage_(std::move(input)),
        input_(input_storage_) {}

  // The parse will start from the beginning of the 'input' absl::string_view.
  // The <filename> and <input> must remain alive until after all the parse
  // operations are completed.
  ParseResumeLocation(absl::string_view filename, absl::string_view input) :
      filename_(filename), input_(input) {}

  // This is used as the backing store for 'filename' if the string constructor
  // is used.  If the FromStringView() constructor is used, then it is not
  // used.
  std::string filename_storage_;

  // The actual filename for the parser.
  absl::string_view filename_;

  // This is used as the backing store for 'input' if the string constructor is
  // used. If the FromStringView() constructor is used, then it is not used.
  std::string input_storage_;

  // The actual input to the parser.
  absl::string_view input_;

  // The position of the next byte that is to be read.
  int byte_position_ = 0;

  // True if resuming is allowed. This is disabled in some well defined cases,
  // such as when GetParseTokens() is called with the max_tokens option.
  bool allow_resume_ = true;
};

}  // namespace googlesql

#endif  // GOOGLESQL_PUBLIC_PARSE_RESUME_LOCATION_H_
