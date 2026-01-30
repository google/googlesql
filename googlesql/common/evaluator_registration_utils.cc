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

#include "googlesql/common/evaluator_registration_utils.h"

#include "googlesql/public/collator.h"
#include "googlesql/reference_impl/functions/register_all.h"
#include "absl/base/call_once.h"
#include "absl/strings/string_view.h"

namespace googlesql::internal {

void EnableFullEvaluatorFeatures() {
  // Because registration is global, it doesn't need to run more than once
  // for a process. This saves a bit of time and avoids lock contention when
  // this function is called repeatedly.
  static absl::once_flag once;
  absl::call_once(once, []() {
    RegisterAllOptionalBuiltinFunctions();
    RegisterIcuCollatorImpl(
        [](absl::string_view collation_name, CollatorLegacyUnicodeMode mode) {
          return googlesql::MakeSqlCollator(collation_name, mode);
        });
  });
}

}  // namespace googlesql::internal
