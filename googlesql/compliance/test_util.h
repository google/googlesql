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

// Test utilities compliance tests.

#ifndef GOOGLESQL_COMPLIANCE_TEST_UTIL_H_
#define GOOGLESQL_COMPLIANCE_TEST_UTIL_H_

#include <stddef.h>

#include <cstdint>
#include <set>
#include <string>
#include <utility>

#include "absl/container/btree_map.h"
#include "absl/container/flat_hash_map.h"
#include "absl/status/status.h"
#include "absl/strings/string_view.h"
#include "google/protobuf/descriptor.h"
#include "googlesql/base/status.h"

namespace googlesql {

// Returns the global descriptors for builtin enums. Builtin enums should only
// use global descriptors.
const absl::flat_hash_map<absl::string_view, const google::protobuf::EnumDescriptor*>&
GetBuiltinEnumDescriptors();

// Returns the global descriptors for builtin protos. Builtin protos should only
// use global descriptors.
const absl::flat_hash_map<absl::string_view, const google::protobuf::Descriptor*>&
GetBuiltinProtoDescriptors();

// Recursively computes transitive closure of a set of protos P and a set of
// enums E.
absl::Status ComputeTransitiveClosure(const google::protobuf::DescriptorPool* pool,
                                      const std::set<std::string>& protos,
                                      const std::set<std::string>& enums,
                                      std::set<std::string>* proto_closure,
                                      std::set<std::string>* enum_closure);

// Functor which implements an absl string delimiter to split
// strings into chunks close to the maximum loggable size.
// It attempts to split chunks at newline boundaries when possible.
//
// Usage:
//  for (absl::string_view sp : absl::StrSplit(message, LogChunkDelimiter())) {
//    ABSL_LOG(INFO) << sp;
//  }
//
class LogChunkDelimiter {
 public:
  absl::string_view Find(absl::string_view text, size_t pos) const;
};

// This class generates a test repro command for non-deterministic tests.
// If the test was sharded, environment variables and flags will be added to
// only re-run the current shard.
class ReproCommand {
 public:
  ReproCommand();
  ReproCommand(const ReproCommand&) = delete;
  ReproCommand& operator=(const ReproCommand&) = delete;

  // Overrides the test target run by build.
  void OverrideTestTarget(absl::string_view test_target) {
    test_target_ = test_target;
  }

  // Overrides the value of the given build flag.
  void OverrideBuildFlag(absl::string_view flag_name, std::string flag_value) {
    build_flags_[flag_name] = std::move(flag_value);
  }

  // Remove the test arg flag if it's set. Ignored if not set. Returns true if
  // the flag was removed.
  bool RemoveBuildFlag(absl::string_view flag_name) {
    return RemoveFromMapIfPresent(flag_name, &build_flags_);
  }

  // Overrides the value of the given test argument flag.
  // This should be used to override the value of the seed flag used by the
  // random number generator.
  void OverrideTestArgFlag(absl::string_view flag_name,
                           std::string flag_value) {
    test_arg_flags_[flag_name] = std::move(flag_value);
  }

  // Remove the test arg flag if it's set. Ignored if not set. Returns true if
  // the flag was removed.
  bool RemoveTestArgFlag(absl::string_view flag_name) {
    return RemoveFromMapIfPresent(flag_name, &test_arg_flags_);
  }

  // Overrides the value of the given environment variable in the test.
  void OverrideTestEnv(absl::string_view env_name, std::string env_value) {
    test_envs_[env_name] = std::move(env_value);
  }

  // Overrides the value of the given environment variable in the test. Returns
  // true if the flag was removed.
  bool RemoveTestEnvFlag(absl::string_view env_name) {
    return RemoveFromMapIfPresent(env_name, &test_envs_);
  }

  // Gets the repro command to rerun the test target deterministically.
  std::string get() const;

  // Gets the test target.
  const std::string& test_target() const { return test_target_; }

 private:
  bool RemoveFromMapIfPresent(absl::string_view key,
                              absl::btree_map<std::string, std::string>* map);

  std::string test_target_;
  absl::btree_map<std::string, std::string> build_flags_;
  absl::btree_map<std::string, std::string> test_arg_flags_;
  absl::btree_map<std::string, std::string> test_envs_;
  const int64_t kDefaultTimeout = 300;  // Default to a 300 second timeout.
};

std::string ShellEscape(absl::string_view src);

}  // namespace googlesql

#endif  // GOOGLESQL_COMPLIANCE_TEST_UTIL_H_
