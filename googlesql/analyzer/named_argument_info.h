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

#ifndef GOOGLESQL_ANALYZER_NAMED_ARGUMENT_INFO_H_
#define GOOGLESQL_ANALYZER_NAMED_ARGUMENT_INFO_H_

#include <optional>
#include <utility>

#include "googlesql/common/errors.h"
#include "googlesql/parser/ast_node.h"
#include "googlesql/parser/parse_tree_errors.h"
#include "googlesql/public/id_string.h"
#include "googlesql/public/parse_location.h"
#include "googlesql/resolved_ast/resolved_node.h"
#include "googlesql/base/status_builder.h"

namespace googlesql {

// Contains necessary named argument information for resolving function call.
class NamedArgumentInfo {
 public:
  NamedArgumentInfo() = delete;
  NamedArgumentInfo(const NamedArgumentInfo &) = default;
  NamedArgumentInfo(NamedArgumentInfo &&) = default;
  NamedArgumentInfo &operator=(const NamedArgumentInfo &) = default;
  NamedArgumentInfo &operator=(NamedArgumentInfo &&) = default;
  ~NamedArgumentInfo() = default;

  // Constructs NamedArgumentInfo using ASTNode:
  //   name - argument name
  //   index - concrete signature index
  //   ast_node - AST node from which we extract parse location to generate an
  //     error.
  NamedArgumentInfo(IdString name, int index, const ASTNode *ast_node)
      : name_(std::move(name)),
        index_(index),
        argument_error_location_(GetErrorLocationPoint(
            ast_node, /*include_leftmost_child=*/false)) {}

  // Constructs NamedArgumentInfo using ASTNode:
  //   name - argument name
  //   index - concrete signature index
  //   resolved_node - ResolvedAST node from which we extract parse location to
  //     generate an error if present - otherwise there will not be a a parse
  //     location.
  NamedArgumentInfo(IdString name, int index, const ResolvedNode *resolved_node)
      : name_(std::move(name)), index_(index) {
    if (resolved_node->GetParseLocationRangeOrNULL() != nullptr) {
      argument_error_location_ =
          resolved_node->GetParseLocationRangeOrNULL()->start();
    }
  }

  const IdString &name() const { return name_; }
  int index() const { return index_; }

  // Creates an error at the argument's parse location. Used if an error with
  // the provided named argument is encountered.
  googlesql_base::StatusBuilder MakeSQLError() const {
    if (argument_error_location_.has_value()) {
      return MakeSqlError().AttachPayload(
          argument_error_location_->ToInternalErrorLocation());
    }
    return MakeSqlError();
  }

 private:
  // Argument name.
  IdString name_;
  // Index of the named argument in the concrete signature - starts at 0.
  int index_ = 0;
  // Argument parse location used to create an error message.
  std::optional<ParseLocationPoint> argument_error_location_;
};
}  // namespace googlesql

#endif  // GOOGLESQL_ANALYZER_NAMED_ARGUMENT_INFO_H_
