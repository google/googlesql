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

#include "googlesql/parser/ast_node_factory.h"

#include <memory>
#include <utility>
#include <vector>

#include "googlesql/base/arena.h"
#include "googlesql/parser/ast_node.h"
#include "googlesql/parser/parse_tree.h"
#include "googlesql/public/id_string.h"
#include "googlesql/public/parse_location.h"
#include "absl/strings/string_view.h"

namespace googlesql::parser {

ASTNodeFactory::ASTNodeFactory(googlesql_base::UnsafeArena* arena, IdStringPool* id_string_pool)
    : id_string_pool_(id_string_pool), arena_(arena) {}

ASTIdentifier* ASTNodeFactory::MakeIdentifier(
    const ParseLocationRange& location, absl::string_view name,
    bool is_quoted) {
  auto* identifier = CreateASTNode<ASTIdentifier>(location);
  identifier->SetIdentifier(id_string_pool()->Make(name));
  identifier->set_is_quoted(is_quoted);
  return identifier;
}

ASTIdentifier* ASTNodeFactory::MakeIdentifier(
    const ParseLocationRange& location, absl::string_view name) {
  return MakeIdentifier(location, name, /*is_quoted=*/false);
}

std::vector<std::unique_ptr<ASTNode>>
ASTNodeFactory::ReleaseAllocatedASTNodes() && {
  return std::move(allocated_ast_nodes_);
}

}  // namespace googlesql::parser
