//
// Copyright 2018 Google LLC
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

#include "googlesql/base/testing/status_matchers.h"

#include "absl/status/status.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/string_view.h"
#include "googlesql/base/source_location.h"
#include "googlesql/base/status_builder.h"

namespace googlesql_base {
namespace testing {
namespace internal_status {

void AddFatalFailure(absl::string_view expression,
                     const ::googlesql_base::StatusBuilder& builder) {
  GTEST_MESSAGE_AT_(
      builder.source_location().file_name(), builder.source_location().line(),
      ::absl::StrCat(expression,
                     " returned error: ", absl::Status(builder).ToString())
          .c_str(),
      ::testing::TestPartResult::kFatalFailure);
}

}  // namespace internal_status
}  // namespace testing
}  // namespace googlesql_base
