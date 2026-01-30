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

#include "googlesql/tools/execute_query/simple_proto_evaluator_table_iterator.h"

#include <string>

#include "googlesql/public/type.h"
#include "googlesql/public/value.h"
#include "googlesql/base/check.h"
#include "absl/status/status.h"
#include "absl/types/span.h"

namespace googlesql {

SimpleProtoEvaluatorTableIterator::SimpleProtoEvaluatorTableIterator(
    const ProtoType* proto_type, absl::Span<const int> columns)
    : proto_type_(proto_type), num_columns_(columns.size()) {
  // The analyzer might prune the one column if it's not used, and then
  // ask to read zero columns.
  ABSL_CHECK_LE(columns.size(), 1);
  if (!columns.empty()) {
    ABSL_CHECK_EQ(columns[0], 0);
  }
}

int SimpleProtoEvaluatorTableIterator::NumColumns() const {
  return num_columns_;
}

std::string SimpleProtoEvaluatorTableIterator::GetColumnName(int i) const {
  ABSL_CHECK_EQ(i, 0);
  return kValueColumnName;
}

const Type* SimpleProtoEvaluatorTableIterator::GetColumnType(int i) const {
  ABSL_CHECK_EQ(i, 0);
  return proto_type_;
}

const Value& SimpleProtoEvaluatorTableIterator::GetValue(int i) const {
  ABSL_CHECK_EQ(i, 0);
  return current_value_;
}

absl::Status SimpleProtoEvaluatorTableIterator::Status() const {
  return status_;
}

// No cancellation or deadline support.
absl::Status SimpleProtoEvaluatorTableIterator::Cancel() {
  return absl::OkStatus();
}

}  // namespace googlesql
