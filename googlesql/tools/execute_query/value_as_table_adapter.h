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

#ifndef GOOGLESQL_TOOLS_EXECUTE_QUERY_VALUE_AS_TABLE_ADAPTER_H_
#define GOOGLESQL_TOOLS_EXECUTE_QUERY_VALUE_AS_TABLE_ADAPTER_H_

#include <memory>

#include "googlesql/public/catalog.h"
#include "googlesql/public/evaluator_table_iterator.h"
#include "googlesql/public/simple_catalog.h"
#include "googlesql/public/types/type.h"
#include "googlesql/public/value.h"
#include "absl/status/statusor.h"

namespace googlesql {

class SimpleTable;

// This class adapts a Value representing the results of a query into an
// interface that can be interpreted as a table. The value is assumed to be an
// array, with one element per row. Each element of the array is a struct with
// one field per column.
class ValueAsTableAdapter {
 public:
  // The input value should outlive the returned adapter.
  static absl::StatusOr<std::unique_ptr<ValueAsTableAdapter>> Create(
      const Value& value);

  const Table* GetTable() const;

  // Returns a table iterator that can be used to iterate over the table.
  // The returned iterator should not outlive the ValueAsTableAdapter object.
  absl::StatusOr<std::unique_ptr<EvaluatorTableIterator>>
  CreateEvaluatorTableIterator() const;

 private:
  explicit ValueAsTableAdapter(const Value& value);

  const Value& value_;
  std::unique_ptr<SimpleTable> table_;
};

}  // namespace googlesql

#endif  // GOOGLESQL_TOOLS_EXECUTE_QUERY_VALUE_AS_TABLE_ADAPTER_H_
