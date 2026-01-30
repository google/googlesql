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

#include <memory>
#include <string>
#include <vector>

#include "googlesql/base/logging.h"
#include "googlesql/public/evaluator_table_iterator.h"
#include "googlesql/public/simple_catalog.h"
#include "googlesql/public/type.h"
#include "googlesql/public/types/proto_type.h"
#include "googlesql/public/types/type.h"
#include "googlesql/public/types/type_factory.h"
#include "googlesql/public/value.h"
#include "googlesql/tools/execute_query/simple_proto_evaluator_table_iterator.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/cord.h"
#include "absl/strings/string_view.h"
#include "absl/types/span.h"
#include "googlesql/base/file_util.h"

namespace googlesql {
namespace {

// Represents a binary proto file as a value table with 1 row.
class BinaryProtoEvaluatorTableIterator
    : public SimpleProtoEvaluatorTableIterator {
 public:
  BinaryProtoEvaluatorTableIterator(absl::string_view path,
                                    const ProtoType* proto_type,
                                    absl::Span<const int> columns)
      : SimpleProtoEvaluatorTableIterator(proto_type, columns), path_(path) {}

  bool NextRow() override {
    if (done_) {
      return false;
    }
    done_ = true;
    // The analyzer might prune the single column if it's not used. We return an
    // empty struct value in such cases.
    if (num_columns_ == 0) {
      current_value_ = Value::Struct(types::EmptyStructType(), {});
      return true;
    }
    std::string data;
    status_ = internal::GetContents(path_, &data);
    if (!status_.ok()) return false;
    current_value_ = Value::Proto(proto_type_, absl::Cord(data));
    return true;
  }

 private:
  const std::string path_;
  bool done_ = false;
};

}  // namespace

absl::StatusOr<std::unique_ptr<SimpleTable>> MakeTableFromBinaryProtoFile(
    absl::string_view table_name, absl::string_view path,
    const ProtoType* column_proto_type) {
  std::unique_ptr<SimpleTable> table;
  std::vector<SimpleTable::NameAndType> columns = {
      {SimpleProtoEvaluatorTableIterator::kValueColumnName, column_proto_type}};

  table = std::make_unique<SimpleTable>(table_name, columns);
  table->set_is_value_table(true);
  // Make a copy, because we cannot trust the lifetime of `path`.
  std::string string_path = std::string(path);
  table->SetEvaluatorTableIteratorFactory(
      [string_path, column_proto_type](absl::Span<const int> columns)
          -> absl::StatusOr<std::unique_ptr<EvaluatorTableIterator>> {
        return std::make_unique<BinaryProtoEvaluatorTableIterator>(
            string_path, column_proto_type, columns);
      });
  return table;
}

}  // namespace googlesql
