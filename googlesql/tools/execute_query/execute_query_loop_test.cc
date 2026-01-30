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

#include "googlesql/tools/execute_query/execute_query_loop.h"

#include <iosfwd>
#include <optional>
#include <sstream>
#include <string>
#include <utility>

#include "googlesql/common/testing/proto_matchers.h"
#include "googlesql/base/testing/status_matchers.h"
#include "googlesql/common/testing/status_payload_matchers.h"
#include "googlesql/tools/execute_query/execute_query.pb.h"
#include "googlesql/tools/execute_query/execute_query_prompt.h"
#include "googlesql/tools/execute_query/execute_query_tool.h"
#include "googlesql/tools/execute_query/execute_query_writer.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/string_view.h"

namespace googlesql {

using ::testing::HasSubstr;
using ::testing::IsEmpty;
using ::absl_testing::StatusIs;

namespace {
class StaticResultPrompt : public ExecuteQueryPrompt {
 public:
  void set_read_result(absl::StatusOr<std::optional<std::string>> r) {
    read_result_ = std::move(r);
  }

  absl::StatusOr<std::optional<std::string>> Read() override {
    return read_result_;
  }

 private:
  absl::StatusOr<std::optional<std::string>> read_result_;
};
}  // namespace

TEST(ExecuteQueryLoopTest, ReadError) {
  StaticResultPrompt prompt;

  prompt.set_read_result(absl::UnavailableError("test"));

  ExecuteQueryConfig config;
  std::ostringstream output;
  ExecuteQueryStreamWriter writer{output};

  EXPECT_THAT(ExecuteQueryLoop(prompt, config, writer),
              StatusIs(absl::StatusCode::kUnavailable, "test"));
}

TEST(ExecuteQueryLoopTest, NoInput) {
  StaticResultPrompt prompt;

  prompt.set_read_result(std::nullopt);

  ExecuteQueryConfig config;
  std::ostringstream output;
  ExecuteQueryStreamWriter writer{output};

  GOOGLESQL_EXPECT_OK(ExecuteQueryLoop(prompt, config, writer));
  EXPECT_THAT(output.str(), IsEmpty());
}

TEST(ExecuteQueryLoopTest, Callback) {
  StaticResultPrompt prompt;

  prompt.set_read_result("\ntest error   ");

  ExecuteQueryConfig config;
  std::ostringstream output;
  ExecuteQueryStreamWriter writer{output};

  const auto handler = [&prompt](absl::Status status) {
    if (status.code() == absl::StatusCode::kUnavailable &&
        status.message() == "input error") {
      return status;
    }

    // The query used invalid syntax but still continues after sending that
    // error to the output writer.
    GOOGLESQL_EXPECT_OK(status);

    // Provoke another error.  This one propagates out as an actual status.
    prompt.set_read_result(absl::UnavailableError("input error"));

    return absl::OkStatus();
  };

  EXPECT_THAT(ExecuteQueryLoop(prompt, config, writer, handler),
              StatusIs(absl::StatusCode::kUnavailable, "input error"));
  EXPECT_THAT(
      output.str(),
      HasSubstr("ERROR: INVALID_ARGUMENT: Syntax error: Unexpected identifier "
                "\"test\" [at 2:1]"));
}

}  // namespace googlesql
