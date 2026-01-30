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

#ifndef GOOGLESQL_COMMON_INTERNAL_ANALYZER_OUTPUT_PROPERTIES_H_
#define GOOGLESQL_COMMON_INTERNAL_ANALYZER_OUTPUT_PROPERTIES_H_

#include "googlesql/public/analyzer_output_properties.h"
#include "googlesql/resolved_ast/resolved_node.h"
#include "googlesql/resolved_ast/target_syntax.h"

namespace googlesql {

// InternalAnalyzerOutputProperties only contains static methods to access
// private fields of AnalyzerOutputProperties that are not part of the public
// APIs.
class InternalAnalyzerOutputProperties {
 public:
  InternalAnalyzerOutputProperties() = delete;
  InternalAnalyzerOutputProperties(const InternalAnalyzerOutputProperties&) =
      delete;
  InternalAnalyzerOutputProperties& operator=(
      const InternalAnalyzerOutputProperties&) = delete;

  // Inserts the `key` and `target_syntax` pair into the target syntax map
  // of `options`.
  static void MarkTargetSyntax(AnalyzerOutputProperties& options,
                               ResolvedNode* key,
                               SQLBuildTargetSyntax target_syntax) {
    options.target_syntax_.insert({key, target_syntax});
  }

  // Returns the target syntax map of `options`.
  static const TargetSyntaxMap& GetTargetSyntaxMap(
      const AnalyzerOutputProperties& options) {
    return options.target_syntax_;
  }
};

}  // namespace googlesql

#endif  // GOOGLESQL_COMMON_INTERNAL_ANALYZER_OUTPUT_PROPERTIES_H_
