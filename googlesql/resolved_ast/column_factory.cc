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

#include "googlesql/resolved_ast/column_factory.h"

#include <cstdint>
#include <limits>
#include <memory>
#include <utility>

#include "googlesql/base/atomic_sequence_num.h"
#include "googlesql/public/id_string.h"
#include "googlesql/public/types/annotation.h"
#include "googlesql/public/types/type.h"
#include "googlesql/resolved_ast/resolved_column.h"
#include "googlesql/base/check.h"
#include "absl/strings/string_view.h"

namespace googlesql {

ColumnFactory::ColumnFactory(int max_seen_col_id, IdStringPool& id_string_pool,
                             googlesql_base::SequenceNumber& sequence)
    : max_seen_col_id_(max_seen_col_id),
      id_string_pool_(&id_string_pool),
      sequence_(&sequence) {}

ColumnFactory::ColumnFactory(int max_seen_col_id, IdStringPool& id_string_pool,
                             std::unique_ptr<googlesql_base::SequenceNumber> sequence)
    : max_seen_col_id_(max_seen_col_id), id_string_pool_(&id_string_pool) {
  owned_column_id_sequence_ = std::move(sequence);
  sequence_ = owned_column_id_sequence_.get();
}

ColumnFactory::ColumnFactory(int max_col_id, IdStringPool* id_string_pool,
                             googlesql_base::SequenceNumber* sequence)
    : max_seen_col_id_(max_col_id),
      id_string_pool_(id_string_pool),
      sequence_(sequence) {
  if (sequence == nullptr) {
    owned_column_id_sequence_ = std::make_unique<googlesql_base::SequenceNumber>();
    sequence_ = owned_column_id_sequence_.get();
  }
  // The implementation assumes that a nullptr <id_string_pool_> indicates
  // that the ColumnFactory was created with the legacy constructor that uses
  // the global string pool.
  //
  // This check ensures that it is safe to remove this assumption, once the
  // legacy constructor is removed and all callers have been migrated.
  ABSL_CHECK(id_string_pool != nullptr);
}

ColumnFactory::ColumnFactory(int max_col_id, googlesql_base::SequenceNumber* sequence)
    : max_seen_col_id_(max_col_id),
      id_string_pool_(nullptr),
      sequence_(sequence) {
  if (sequence == nullptr) {
    owned_column_id_sequence_ = std::make_unique<googlesql_base::SequenceNumber>();
    sequence_ = owned_column_id_sequence_.get();
  }
}

ResolvedColumn ColumnFactory::MakeCol(absl::string_view table_name,
                                      absl::string_view col_name,
                                      const Type* type) {
  int column_id = AllocateColumnId();
  if (id_string_pool_ != nullptr) {
    return ResolvedColumn(column_id, id_string_pool_->Make(table_name),
                          id_string_pool_->Make(col_name), type);
  } else {
    return ResolvedColumn(column_id,
                          googlesql::IdString::MakeGlobal(table_name),
                          googlesql::IdString::MakeGlobal(col_name), type);
  }
}

ResolvedColumn ColumnFactory::MakeCol(absl::string_view table_name,
                                      absl::string_view col_name,
                                      AnnotatedType annotated_type) {
  int column_id = AllocateColumnId();
  if (id_string_pool_ != nullptr) {
    return ResolvedColumn(column_id, id_string_pool_->Make(table_name),
                          id_string_pool_->Make(col_name), annotated_type);
  } else {
    return ResolvedColumn(
        column_id, googlesql::IdString::MakeGlobal(table_name),
        googlesql::IdString::MakeGlobal(col_name), annotated_type);
  }
}

int ColumnFactory::AllocateColumnId() {
  while (true) {
    // Allocate from the sequence, but make sure it's higher than the max we
    // should start from.
    int next_col_id = static_cast<int>(sequence_->GetNext());
    if (next_col_id > max_seen_col_id_) {
      ABSL_DCHECK_NE(next_col_id, 0);
      max_seen_col_id_ = next_col_id;
      break;
    }
  }
  // Should be impossible for this to happen unless sharing across huge
  // numbers of queries.  If it does, column_ids will wrap around as int32s.
  ABSL_DCHECK_LE(max_seen_col_id_, std::numeric_limits<int32_t>::max());
  return max_seen_col_id_;
}

}  // namespace googlesql
