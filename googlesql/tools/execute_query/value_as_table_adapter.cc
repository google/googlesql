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

#include "googlesql/tools/execute_query/value_as_table_adapter.h"

#include <memory>
#include <string>
#include <vector>

#include "googlesql/public/catalog.h"
#include "googlesql/public/evaluator_table_iterator.h"
#include "googlesql/public/simple_catalog.h"
#include "googlesql/public/type.h"
#include "googlesql/public/types/struct_type.h"
#include "googlesql/public/value.h"
#include "absl/memory/memory.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/str_cat.h"
#include "absl/types/span.h"

namespace googlesql {

ValueAsTableAdapter::ValueAsTableAdapter(const Value& value) : value_(value) {
  const StructType* struct_type =
      value_.type()->AsArray()->element_type()->AsStruct();
  std::vector<SimpleTable::NameAndType> column_info;
  for (int i = 0; i < struct_type->num_fields(); ++i) {
    const StructField& field = struct_type->field(i);
    column_info.push_back(
        {field.name.empty() ? absl::StrCat("$col_", i + 1) : field.name,
         field.type});
  }

  table_ = std::make_unique<SimpleTable>("value_table", column_info);
  std::vector<std::vector<Value>> rows;
  rows.reserve(value_.num_elements());
  for (int i = 0; i < value_.num_elements(); ++i) {
    rows.push_back(value_.element(i).fields());
  }
  table_->SetContents(rows);
}

const Table* ValueAsTableAdapter::GetTable() const { return table_.get(); }

absl::StatusOr<std::unique_ptr<EvaluatorTableIterator>>
ValueAsTableAdapter::CreateEvaluatorTableIterator() const {
  std::vector<int> columns_idxs;
  columns_idxs.reserve(table_->NumColumns());
  for (int i = 0; i < table_->NumColumns(); ++i) {
    columns_idxs.push_back(i);
  }
  return table_->CreateEvaluatorTableIterator(columns_idxs);
}

absl::StatusOr<std::unique_ptr<ValueAsTableAdapter>>
ValueAsTableAdapter::Create(const Value& value) {
  if (!value.has_content() || !value.type()->IsArray() ||
      !value.type()->AsArray()->element_type()->IsStruct()) {
    return absl::InvalidArgumentError("The value must be an array of structs.");
  }
  return absl::WrapUnique(new ValueAsTableAdapter(value));
}

}  // namespace googlesql
