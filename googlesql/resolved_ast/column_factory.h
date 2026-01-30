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

#ifndef GOOGLESQL_RESOLVED_AST_COLUMN_FACTORY_H_
#define GOOGLESQL_RESOLVED_AST_COLUMN_FACTORY_H_

#include <memory>

#include "googlesql/base/atomic_sequence_num.h"
#include "googlesql/public/builtin_function.pb.h"
#include "googlesql/public/id_string.h"
#include "googlesql/public/types/annotation.h"
#include "googlesql/public/types/type.h"
#include "googlesql/resolved_ast/resolved_column.h"
#include "absl/base/attributes.h"
#include "absl/strings/string_view.h"

namespace googlesql {

// A mutable ResolvedColumn factory that creates a new ResolvedColumn with a new
// column id on each call. This prevents column id collisions.
//
// Not thread safe.
class ColumnFactory {
 public:
  // Manages allocation of column ids and column names and helps construct
  // ResolvedColumns.
  //
  // `max_seen_col_id`: All generated column ids will be strictly greater than
  //     `max_seen_col_id`.
  //
  // `id_string_pool`: Used to allocate column names. The lifetime of
  //     `id_string_pool` must exceed that of the constructed `ColumnFactory`.
  //
  // `sequence`: Used to allocate column ids. The lifetime of `sequence`
  //      must exceed that of the constructed `ColumnFactory`.
  ColumnFactory(int max_seen_col_id, IdStringPool& id_string_pool,
                googlesql_base::SequenceNumber& sequence);

  // Manages allocation of column ids and column names and helps construct
  // ResolvedColumns.
  //
  // `max_seen_col_id`: All generated column ids will be strictly greater than
  //     `max_seen_col_id`.
  //
  // `id_string_pool`: Used to allocate column names. The lifetime of
  //     `id_string_pool` must exceed that of the constructed `ColumnFactory`.
  //
  // `sequence`: Used to allocate column ids.
  ColumnFactory(int max_seen_col_id, IdStringPool& id_string_pool,
                std::unique_ptr<googlesql_base::SequenceNumber> sequence);

  // Creates columns using column ids starting above the max seen column id.
  //
  // IdString's for column names are allocated from the IdStringPool provided,
  // which must outlive this ColumnFactory object.
  //
  // If 'sequence' is provided, it's used to do the allocations. IDs from the
  // sequence that are not above 'max_col_id' are discarded.
  ABSL_DEPRECATED(
      "This constructor does not prevent passing in nullptr. Use overload that "
      "consumes a reference to IdStringPool instead")
  ColumnFactory(int max_col_id, IdStringPool* id_string_pool,
                googlesql_base::SequenceNumber* sequence = nullptr);

  // Similar to the above constructor, except allocates column ids on the global
  // string pool.
  //
  // WARNING: Column factories produced by this constructor will leak memory
  // each time a column is created. To avoid this, use the above constructor
  // overload instead and supply an IdStringPool.
  ABSL_DEPRECATED(
      "This constructor will result in a ColumnFactory that leaks "
      "memory. Use overload that consumes an IdStringPool instead")
  explicit ColumnFactory(int max_col_id,
                         googlesql_base::SequenceNumber* sequence = nullptr);

  ColumnFactory(const ColumnFactory&) = delete;
  ColumnFactory& operator=(const ColumnFactory&) = delete;

  // Returns the maximum column id that has been allocated.
  int max_column_id() const { return max_seen_col_id_; }

  // Creates a new column, incrementing the counter for next use.
  ResolvedColumn MakeCol(absl::string_view table_name,
                         absl::string_view col_name, const Type* type);

  // Creates a new column with an AnnotatedType, incrementing the counter for
  // next use.
  ResolvedColumn MakeCol(absl::string_view table_name,
                         absl::string_view col_name, AnnotatedType type);

  // Allocates a new column id from the sequence higher than `max_seen_col_id_`.
  int AllocateColumnId();

 private:
  int max_seen_col_id_;
  IdStringPool* id_string_pool_;
  googlesql_base::SequenceNumber* sequence_;
  std::unique_ptr<googlesql_base::SequenceNumber> owned_column_id_sequence_;
};

}  // namespace googlesql

#endif  // GOOGLESQL_RESOLVED_AST_COLUMN_FACTORY_H_
