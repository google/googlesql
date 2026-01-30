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

#ifndef GOOGLESQL_PUBLIC_ANALYZER_OUTPUT_PROPERTIES_H_
#define GOOGLESQL_PUBLIC_ANALYZER_OUTPUT_PROPERTIES_H_

#include "googlesql/public/options.pb.h"
#include "googlesql/resolved_ast/target_syntax.h"
#include "absl/base/attributes.h"
#include "absl/container/btree_set.h"
#include "absl/container/flat_hash_set.h"
#include "absl/strings/string_view.h"

namespace googlesql {

class AnalyzerOutputProperties {
 public:
  // TODO: Remove when external references drop to zero.
  ABSL_DEPRECATED(
      "Client code should consider this struct internal. "
      "It doesn't mean what you think it means.")
  bool has_anonymization = false;  // NOLINT

  // Adds a feature label to the analyzer output properties. Feature labels
  // provide visibility of feature use on (broken link) and in data
  // that query engines log for analysis. This API should only be used to log
  // labels for features that are not obvious in the ResolvedAST produced by the
  // Resolver. Features that are evident in the ResolvedAST should have their
  // labels added by the `googlesql::ComplianceLabelExtractor` which can run
  // out of band to not affect compilation latency.
  //
  // `feature_label` must be a string literal or constexpr string_view so that
  // its backing string data is statically allocated. `AnalyzerOutputProperties`
  // will not copy the data.
  void AddFeatureLabel(absl::string_view feature_label)
  {
    feature_labels_.insert(feature_label);
  }
  const absl::flat_hash_set<absl::string_view>& feature_labels() const {
    return feature_labels_;
  }

 private:
  // Defined in googlesql/common/internal_analyzer_output_properties.h.
  friend class InternalAnalyzerOutputProperties;

  absl::flat_hash_set<absl::string_view> feature_labels_;
  TargetSyntaxMap target_syntax_;
};

}  // namespace googlesql

#endif  // GOOGLESQL_PUBLIC_ANALYZER_OUTPUT_PROPERTIES_H_
