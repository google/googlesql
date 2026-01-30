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

#include "googlesql/analyzer/filter_fields_path_validator.h"

#include <memory>
#include <string>
#include <vector>

#include "googlesql/base/logging.h"
#include "absl/container/flat_hash_map.h"
#include "absl/status/status.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/str_join.h"
#include "absl/strings/substitute.h"
#include "google/protobuf/descriptor.h"
#include "googlesql/base/map_util.h"
#include "googlesql/base/ret_check.h"
#include "googlesql/base/status_macros.h"

namespace googlesql {
namespace {

std::string PrintFieldPath(
    bool include, std::vector<const google::protobuf::FieldDescriptor*> field_path) {
  auto field_path_formater =
      [](std::string* out, const google::protobuf::FieldDescriptor* field_descriptor) {
        if (field_descriptor->is_extension()) {
          out->append(absl::StrCat("(", field_descriptor->full_name(), ")"));
        } else {
          out->append(field_descriptor->name());
        }
      };
  return absl::StrCat(include ? "+" : "-",
                      absl::StrJoin(field_path, ".", field_path_formater));
}

}  // namespace

absl::Status FilterFieldsPathValidator::ValidateFieldPath(
    bool include,
    const std::vector<const google::protobuf::FieldDescriptor*>& field_path) {
  GOOGLESQL_VLOG(3) << "Adding field path: " << PrintFieldPath(include, field_path);
  if (root_node_ == nullptr) {
    // Root node has the reverse inclusive/exclusive status with first inserted
    // field path.
    root_node_ =
        std::make_unique<FieldPathTrieNode>(root_node_descriptor_, !include);
  }

  FieldPathTrieNode* node = root_node_.get();
  for (int i = 0; i < field_path.size(); ++i) {
    const google::protobuf::FieldDescriptor* field_descriptor = field_path[i];
    GOOGLESQL_RET_CHECK_EQ(field_descriptor->containing_type(), node->descriptor)
        << field_descriptor->full_name() << "(whose parent is "
        << field_descriptor->containing_type()->full_name()
        << ") is not a child of " << node->descriptor->full_name();
    std::unique_ptr<FieldPathTrieNode>& child_node =
        node->children[field_descriptor->number()];
    if (child_node == nullptr) {
      if (node->include == include) {
        if (node == root_node_.get()) {
          return absl::InvalidArgumentError(
              absl::Substitute("Path $0 is invalid since the top-level message "
                               "is $1 due to the first field path being $2",
                               PrintFieldPath(include, field_path),
                               include ? "included" : "excluded",
                               include ? "excluded" : "included"));
        }
        return absl::InvalidArgumentError(
            absl::Substitute("Path $0 is invalid since its parent path is $1",
                             PrintFieldPath(include, field_path),
                             include ? "included" : "excluded"));
      }
      child_node = std::make_unique<FieldPathTrieNode>(
          field_descriptor->message_type(), node->include);
      GOOGLESQL_VLOG(4) << "Created child node: "
              << absl::Substitute(
                     "$0 [$1]",
                     field_descriptor ? field_descriptor->camelcase_name() : "",
                     child_node->include ? "inclusive" : "exclusive");
    } else if (i == field_path.size() - 1) {
      if (child_node->children.empty()) {
        // We're inserting a path that has been added before.
        return absl::InvalidArgumentError(
            absl::Substitute("Path $0 has already been added",
                             PrintFieldPath(include, field_path)));
      }
      // A child field path was inserted already.
      return absl::InvalidArgumentError(
          absl::Substitute("A child path appears before $0; child paths must "
                           "be added after their parent",
                           PrintFieldPath(include, field_path)));
    }
    node = child_node.get();
  }
  // Override inclusion/exclusion status inherited from parent node.
  node->include = include;
  return absl::OkStatus();
}

absl::Status FilterFieldsPathValidator::FinalValidation(
    bool reset_cleared_required_fields) const {
  if (root_node_ == nullptr) {
    return absl::InvalidArgumentError(
        "FILTER_FIELDS() should have at least one field path");
  }
  return reset_cleared_required_fields
             ? absl::OkStatus()
             : RecursivelyValidateNode(root_node_.get());
}

absl::Status FilterFieldsPathValidator::RecursivelyValidateNode(
    const FieldPathTrieNode* node) const {
  // No need to check required fields of a leaf node, since this node is
  // included or excluded by its parent, having required fields doesn't matter.
  if (node->children.empty()) {
    return absl::OkStatus();
  }

  for (int i = 0; i < node->descriptor->field_count(); ++i) {
    const google::protobuf::FieldDescriptor* descriptor = node->descriptor->field(i);

    bool included = node->include;
    const auto* child_node =
        googlesql_base::FindOrNull(node->children, descriptor->number());
    if (child_node) {
      // Even if the child is being excluded, if it's a message with children
      // under it that are being retained, the message itself is retained.
      included = (*child_node)->include || !(*child_node)->children.empty();
      GOOGLESQL_RETURN_IF_ERROR(RecursivelyValidateNode(child_node->get()));
    }

    if (!included && descriptor->is_required()) {
      return absl::InvalidArgumentError(absl::Substitute(
          "Field $0 is required but will be cleared given the list of paths",
          descriptor->full_name()));
    }
  }
  return absl::OkStatus();
}

}  // namespace googlesql
