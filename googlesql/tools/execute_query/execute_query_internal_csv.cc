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

#include <fcntl.h>

#include <memory>
#include <string>
#include <vector>

#include "googlesql/public/simple_catalog.h"
#include "googlesql/public/type.h"
#include "googlesql/public/value.h"
#include "absl/status/statusor.h"
#include "absl/strings/string_view.h"
#include "riegeli/bytes/fd_reader.h"
#include "riegeli/csv/csv_reader.h"
#include "googlesql/base/status_macros.h"

namespace googlesql {

absl::StatusOr<std::unique_ptr<SimpleTable>> MakeTableFromCsvFile(
    absl::string_view table_name, absl::string_view path) {
  riegeli::CsvReader csv_reader{riegeli::FdReader(path)};

  std::vector<std::string> record;
  if (!csv_reader.ReadRecord(record)) {
    if (!csv_reader.ok()) return csv_reader.status();
    return googlesql_base::UnknownErrorBuilder()
           << "CSV file " << path << " does not contain a header row";
  }
  std::vector<SimpleTable::NameAndType> columns;
  columns.reserve(record.size());
  for (const std::string& column_name : record) {
    columns.emplace_back(column_name, types::StringType());
  }

  std::vector<std::vector<Value>> contents;
  while (csv_reader.ReadRecord(record)) {
    if (record.size() != columns.size()) {
      return googlesql_base::UnknownErrorBuilder()
             << "CSV file " << path << " has a header row with "
             << columns.size() << " columns, but row "
             << csv_reader.last_record_index() << " has " << record.size()
             << " fields";
    }
    std::vector<Value>& row = contents.emplace_back();
    row.reserve(record.size());
    for (const std::string& field : record) {
      row.push_back(Value::String(field));
    }
  }
  if (!csv_reader.Close()) return csv_reader.status();

  auto table = std::make_unique<SimpleTable>(table_name, columns);
  table->SetContents(contents);
  return table;
}

}  // namespace googlesql
