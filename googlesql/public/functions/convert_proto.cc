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

#include "googlesql/public/functions/convert_proto.h"

#include <string>
#include <utility>

#include "absl/strings/cord.h"
#include "absl/strings/match.h"
#include "google/protobuf/io/tokenizer.h"
#include "google/protobuf/io/zero_copy_stream_impl.h"
#include "google/protobuf/text_format.h"
#include "googlesql/base/source_location.h"
#include "googlesql/base/status_builder.h"

namespace googlesql {
namespace functions {

static bool ProtoToStringInternal(const google::protobuf::Message* value, absl::Cord* out,
                                  bool multiline, absl::Status* error) {
  google::protobuf::TextFormat::Printer printer;
  printer.SetUseUtf8StringEscaping(true);
  printer.SetSingleLineMode(!multiline);
  google::protobuf::io::CordOutputStream stream(std::move(*out));
  if (!printer.Print(*value, &stream)) {
    // This can happen (e.g.) when the algorithm thinks it is out of buffer
    // space. Generally it is unexpected here.
    *error = ::googlesql_base::InternalErrorBuilder()
             << "Failed to generate proto2 text format for printing a proto2 "
             << "message to a string.";
    return false;
  }
  // The "SingleLineMode" sometimes puts an extra space at the end of the
  // printed proto.
  *out = stream.Consume();
  if (out->EndsWith(" ")) {
    out->RemoveSuffix(1);
  }
  return true;
}

bool ProtoToString(const google::protobuf::Message* value, absl::Cord* out,
                   absl::Status* error) {
  return ProtoToStringInternal(value, out, /*multiline=*/false, error);
}

bool ProtoToMultilineString(const google::protobuf::Message* value, absl::Cord* out,
                            absl::Status* error) {
  return ProtoToStringInternal(value, out, /*multiline=*/true, error);
}

bool StringToProto(const absl::string_view value, google::protobuf::Message* out,
                   absl::Status* error) {
  class Proto2ErrorCollector final : public google::protobuf::io::ErrorCollector {
   public:
    explicit Proto2ErrorCollector(absl::Status* error) : error_(error) {}
    Proto2ErrorCollector(const Proto2ErrorCollector&) = delete;
    Proto2ErrorCollector& operator=(const Proto2ErrorCollector&) = delete;
    ~Proto2ErrorCollector() final = default;

    void RecordError(int line, int column, absl::string_view message) final {
      *error_ = ::googlesql_base::OutOfRangeErrorBuilder()
                << "Error parsing proto: " << message << " [" << line + 1 << ":"
                << column + 1 << "]";
    }

   private:
    absl::Status* error_;  // Not owned
  };

  google::protobuf::TextFormat::Parser parser;
  Proto2ErrorCollector error_collector(error);
  parser.RecordErrorsTo(&error_collector);
  return parser.ParseFromString(value, out);
}

}  // namespace functions
}  // namespace googlesql
