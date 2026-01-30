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

#ifndef GOOGLESQL_TOOLS_FORMATTER_INTERNAL_CHUNK_GROUPING_STRATEGY_H_
#define GOOGLESQL_TOOLS_FORMATTER_INTERNAL_CHUNK_GROUPING_STRATEGY_H_

#include <vector>

#include "googlesql/tools/formatter/internal/chunk.h"
#include "absl/status/status.h"

namespace googlesql {
namespace formatter {
namespace internal {

// Computes the nesting levels for <chunks> using provided <block_factory>.
//
// If chunks in <chunks> already have chunk blocks, they will be overwritten.
absl::Status ComputeChunkBlocksForChunks(ChunkBlockFactory* block_factory,
                                         std::vector<Chunk>* chunks);

}  // namespace internal
}  // namespace formatter
}  // namespace googlesql

#endif  // GOOGLESQL_TOOLS_FORMATTER_INTERNAL_CHUNK_GROUPING_STRATEGY_H_
