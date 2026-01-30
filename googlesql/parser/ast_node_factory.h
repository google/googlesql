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

#ifndef GOOGLESQL_PARSER_AST_NODE_FACTORY_H_
#define GOOGLESQL_PARSER_AST_NODE_FACTORY_H_

#include <memory>
#include <vector>

#include "googlesql/base/arena.h"
#include "googlesql/base/arena_allocator.h"
#include "googlesql/parser/parse_tree.h"
#include "googlesql/public/id_string.h"
#include "googlesql/public/parse_location.h"
#include "googlesql/public/proto/logging.pb.h"
#include "absl/strings/string_view.h"
#include "absl/types/span.h"

namespace googlesql {
namespace parser {

// A factory class for allocating `ASTNode`s.
class ASTNodeFactory {
 public:
  ASTNodeFactory(googlesql_base::UnsafeArena* arena, IdStringPool* id_string_pool);

  IdStringPool* id_string_pool() const { return id_string_pool_; }

  // Creates an `ASTNode` of type `T`. Sets its location to `location`. Stores
  // the returned pointer in `allocated_ast_nodes_`.
  template <typename T>
  T* CreateASTNode(const ParseLocationRange& location) {
    return CreateASTNode<T>(location, /*children=*/{});
  }

  // Creates an `ASTNode` of type `T`. Sets its location to`location`. Then adds
  // `children` to the node. `InitFields()` is NOT called so that additional
  // children may be added after construction. Stores the returned pointer in
  // `allocated_ast_nodes_`.
  template <typename T>
  T* CreateASTNode(const ParseLocationRange& location,
                   absl::Span<ASTNode* const> children) {
    T* result = new (googlesql_base::AllocateInArena, arena_) T;
    allocated_ast_nodes_.push_back(std::unique_ptr<ASTNode>(result));
    for (ASTNode* child : children) {
      if (child != nullptr) {
        result->AddChild(child);
      }
    }
    result->set_location(location);
    return result;
  }

  // Creates an `ASTIdentifier` with text `name` and location `location`, and
  // whether it `is_quoted`.
  ASTIdentifier* MakeIdentifier(const ParseLocationRange& location,
                                absl::string_view name, bool is_quoted);

  // Creates an `ASTIdentifier` with text `name` and location `location`.
  ASTIdentifier* MakeIdentifier(const ParseLocationRange& location,
                                absl::string_view name);

  // Releases ownership of all allocated `ASTNodes` and closes the factory.
  std::vector<std::unique_ptr<ASTNode>> ReleaseAllocatedASTNodes() &&;

 private:
  // Identifiers and literal values are allocated from this arena.
  IdStringPool* id_string_pool_;

  // Arena used when allocating ASTNodes.
  googlesql_base::UnsafeArena* arena_;

  // Maintains ownership of all allocated ASTNodes.
  std::vector<std::unique_ptr<ASTNode>> allocated_ast_nodes_;
};

}  // namespace parser
}  // namespace googlesql

#endif  // GOOGLESQL_PARSER_AST_NODE_FACTORY_H_
