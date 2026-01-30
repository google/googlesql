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

#ifndef GOOGLESQL_ANALYZER_FILTER_FIELDS_PATH_VALIDATOR_H_
#define GOOGLESQL_ANALYZER_FILTER_FIELDS_PATH_VALIDATOR_H_

#include <memory>
#include <vector>

#include "absl/container/flat_hash_map.h"
#include "absl/status/status.h"
#include "google/protobuf/descriptor.h"

namespace googlesql {

// This class accepts field paths with inclusion/exclusion status defined in
// (broken link).
class FilterFieldsPathValidator {
 public:
  explicit FilterFieldsPathValidator(
      const google::protobuf::Descriptor* root_node_descriptor)
      : root_node_descriptor_(root_node_descriptor) {}

  // Validates that a field path that is included or excluded (include=false) is
  // valid given the paths that have already been added.  Please note the order
  // to add field paths matters, It returns error according to rules described
  // in (broken link).
  absl::Status ValidateFieldPath(
      bool include,
      const std::vector<const google::protobuf::FieldDescriptor*>& field_path);

  // The final validation of a filter fields function. Should be called after
  // finishing calling on ValidateFieldPath.
  absl::Status FinalValidation(
      bool reset_cleared_required_fields = false) const;

 private:
  // Represent a field/subfield inside a proto. A path from the root node to
  // this node represent a valid field path.
  struct FieldPathTrieNode {
    FieldPathTrieNode(const google::protobuf::Descriptor* descriptor, bool include)
        : descriptor(descriptor), include(include) {}

    // nullptr for the leaf nodes.
    const google::protobuf::Descriptor* const descriptor;
    // Indicates whether this node is include or exclude.
    bool include;
    // Child nodes which are keyed by proto field tag numbers.
    absl::flat_hash_map<int, std::unique_ptr<FieldPathTrieNode>> children;
  };

  absl::Status RecursivelyValidateNode(const FieldPathTrieNode* node) const;

  const google::protobuf::Descriptor* root_node_descriptor_;
  std::unique_ptr<FieldPathTrieNode> root_node_;
};

}  // namespace googlesql

#endif  // GOOGLESQL_ANALYZER_FILTER_FIELDS_PATH_VALIDATOR_H_
