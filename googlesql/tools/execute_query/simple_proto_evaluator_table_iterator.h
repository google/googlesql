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

#ifndef GOOGLESQL_TOOLS_EXECUTE_QUERY_SIMPLE_PROTO_EVALUATOR_TABLE_ITERATOR_H_
#define GOOGLESQL_TOOLS_EXECUTE_QUERY_SIMPLE_PROTO_EVALUATOR_TABLE_ITERATOR_H_

#include <string>

#include "googlesql/public/evaluator_table_iterator.h"
#include "googlesql/public/type.h"
#include "googlesql/public/types/proto_type.h"
#include "googlesql/public/value.h"
#include "absl/status/status.h"

namespace googlesql {

// Represents a sequence of protos as a value table.
// This way, only NextRow to read the proto needs to be implemented.
class SimpleProtoEvaluatorTableIterator : public EvaluatorTableIterator {
 public:
  static constexpr char kValueColumnName[] = "value";

  explicit SimpleProtoEvaluatorTableIterator(const ProtoType* proto_type,
                                             absl::Span<const int> columns);
  int NumColumns() const override;
  std::string GetColumnName(int i) const override;
  const Type* GetColumnType(int i) const override;
  const Value& GetValue(int i) const override;
  absl::Status Status() const override;
  absl::Status Cancel() override;

 protected:
  const ProtoType* proto_type_ = nullptr;
  Value current_value_;
  absl::Status status_;
  // The analyzer might prune the one column if it's not used in which case
  // num_columns_ will be zero. Otherwise its 1.
  int num_columns_;
};

}  // namespace googlesql

#endif  // GOOGLESQL_TOOLS_EXECUTE_QUERY_SIMPLE_PROTO_EVALUATOR_TABLE_ITERATOR_H_
