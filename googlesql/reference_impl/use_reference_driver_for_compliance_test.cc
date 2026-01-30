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

#include "googlesql/compliance/test_driver.h"
#include "googlesql/public/options.pb.h"
#include "googlesql/reference_impl/reference_driver.h"
#include "googlesql/reference_impl/rewrite_flags.h"
#include "googlesql/resolved_ast/resolved_node_kind.pb.h"
#include "absl/flags/flag.h"

namespace googlesql {

// Unlike most cases, we don't put this in the same file as the TestDriver
// class because the reference driver gets linked in to all compliance test
// instances, and we don't usually want it to be the main one being tested.
TestDriver* GetComplianceTestDriver() {
  // This returns a default ReferenceDriver with rewrites given by the
  // --rewrites flag and default language options set. For compliance tests,
  // the desired options should always be filled in, so the reference driver
  // matches the options of the engine being tested, or is put into the
  // configurations specified inside each test when testing the reference
  // implementation itself and generating golden outputs.
  auto options = ReferenceDriver::DefaultLanguageOptions();
  options.EnableLanguageFeature(FEATURE_COLLATION_SUPPORT);
  options.AddSupportedStatementKind(RESOLVED_GENERALIZED_QUERY_STMT);
  options.AddSupportedStatementKind(RESOLVED_GENERALIZED_QUERY_SUBPIPELINE);
  return new ReferenceDriver(options, absl::GetFlag(FLAGS_rewrites));
}

}  // namespace googlesql
