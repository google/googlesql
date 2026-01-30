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

#ifndef GOOGLESQL_PUBLIC_FUNCTIONS_MATCH_RECOGNIZE_COMPILED_PATTERN_H_
#define GOOGLESQL_PUBLIC_FUNCTIONS_MATCH_RECOGNIZE_COMPILED_PATTERN_H_

#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <variant>

#include "googlesql/public/functions/match_recognize/compiled_pattern.pb.h"
#include "googlesql/public/functions/match_recognize/match_partition.h"
#include "googlesql/public/value.h"
#include "googlesql/resolved_ast/resolved_ast.h"
#include "absl/status/statusor.h"
#include "absl/strings/string_view.h"

namespace googlesql::functions::match_recognize {

// Supported pattern matching algorithms.
enum PatternMatchingAlgorithm {
  // Algorithm which compiles the pattern into a state machine and uses the set
  // of possible states the machine can be in for each row of input to determine
  // the matches.
  //
  // This algorithm is best for production, and has a guaranteed time and space
  // complexity of O(<length of input> + <sum of lengths of all matches>).
  // Details: (broken link)
  kStateMachineAlgorithm,
};

struct PatternOptions {
  // Which pattern matching algorithm to use.
  PatternMatchingAlgorithm algorithm = kStateMachineAlgorithm;

  // Returns the value of the specified query parameter, referenced inside of
  // the pattern. Needed to compile bounded patterns with bounded quantifiers,
  // which can reference query parameters.
  //
  // A value of nullptr indicates that query parameters are not available at
  // compile-time.
  std::function<absl::StatusOr<Value>(std::variant<int, absl::string_view>)>
      parameter_evaluator;
};

// Represents a pattern that has been pre-compiled for efficient matching.
// A compiled pattern can be reused to match multiple inputs.
class CompiledPattern {
 public:
  virtual ~CompiledPattern() = default;

  static absl::StatusOr<std::unique_ptr<const CompiledPattern>> Create(
      const ResolvedMatchRecognizeScan& scan, const PatternOptions& options);

  // Creates a matching session to match rows of input, given options.
  //
  // Note: If a parameter evaluator was not specified in the PatternOptions, it
  // must be specified in the MatchOptions if the underlying pattern uses any
  // query parameter as a quantifier bound.
  virtual absl::StatusOr<std::unique_ptr<MatchPartition>> CreateMatchPartition(
      const MatchOptions& options) const = 0;

  // Serializes this object into a byte sequence, which can be deserialized back
  // into a CompiledPattern object.
  virtual CompiledPatternProto Serialize() const = 0;

  // Deserializes bytes previously returned by Serialize() back into a
  // CompiledPattern object.
  //
  // Serialization formats may change from time to time. In the event that
  // the code was serialized using a different version of this library,
  // Deserialize() will accept the input if it is within one version of the
  // current serialization format.
  static absl::StatusOr<std::unique_ptr<const CompiledPattern>> Deserialize(
      const CompiledPatternProto& serialized);

  // Returns an implementation-dependent string describing the internal content
  // of the compiled pattern.
  virtual std::string DebugString() const = 0;
};
}  // namespace googlesql::functions::match_recognize
#endif  // GOOGLESQL_PUBLIC_FUNCTIONS_MATCH_RECOGNIZE_COMPILED_PATTERN_H_
