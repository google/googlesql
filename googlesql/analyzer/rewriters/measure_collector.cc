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

#include "googlesql/analyzer/rewriters/measure_collector.h"

#include <string>

#include "googlesql/public/types/type.h"
#include "googlesql/resolved_ast/resolved_column.h"
#include "googlesql/base/check.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/str_cat.h"
#include "googlesql/base/ret_check.h"

namespace googlesql {

absl::Status MeasureCollector::AddMeasureInfo(Key key, MeasureInfo info) {
  GOOGLESQL_RET_CHECK(key != nullptr);
  auto [it, inserted] = measure_infos_.try_emplace(key, info);
  if (!inserted) {
    return absl::AlreadyExistsError(absl::StrCat(
        "Measure definition for key ", key->DebugString(), " already exists."));
  }
  return absl::OkStatus();
}

absl::StatusOr<MeasureInfo> MeasureCollector::GetMeasureInfo(Key key) const {
  GOOGLESQL_RET_CHECK(key != nullptr);
  auto it = measure_infos_.find(key);
  if (it == measure_infos_.end()) {
    return absl::NotFoundError(absl::StrCat("Measure definition for key ",
                                            key->DebugString(), " not found."));
  }
  return it->second;
}

absl::StatusOr<ResolvedColumn> MeasureCollector::GetClosureColumn(
    const ResolvedColumn& m) {
  GOOGLESQL_RET_CHECK(m.type()->IsMeasureType());
  const MeasureType* key = m.type()->AsMeasure();
  auto it = measure_infos_.find(key);
  if (it == measure_infos_.end()) {
    return absl::NotFoundError(absl::StrCat("Measure definition for column ",
                                            m.DebugString(), " not found."));
  }

  const MeasureInfo& measure_info = it->second;
  if (m == measure_info.measure_source_column) {
    return measure_info.closure_struct;
  }

  auto propagated_it = propagated_closure_columns_.find(m);
  if (propagated_it != propagated_closure_columns_.end()) {
    return propagated_it->second;
  }

  // This is a propagated measure column that we haven't seen before. Allocate a
  // new closure column for it.
  const std::string closure_column_name = absl::StrCat(
      "struct_for_measures_from_table_", m.table_name_id().ToStringView());
  const Type* closure_type = measure_info.closure_struct.type();
  ResolvedColumn new_closure_column = column_factory_.MakeCol(
      m.table_name_id().ToStringView(), closure_column_name, closure_type);
  propagated_closure_columns_[m] = new_closure_column;
  return new_closure_column;
}

void MeasureCollector::MarkAgged(Key key) {
  ABSL_DCHECK(key != nullptr);
  agged_measure_keys_.insert(key);
}

bool MeasureCollector::IsAgged(Key key) const {
  ABSL_DCHECK(key != nullptr);
  return agged_measure_keys_.contains(key);
}

absl::Status MeasureCollector::Validate() const {
  for (Key key : agged_measure_keys_) {
    if (!measure_infos_.contains(key)) {
      return absl::InternalError(absl::StrCat("Measure key ",
                                              key->DebugString(),
                                              " is marked as aggregated but "
                                              "does not have a definition."));
    }
  }
  return absl::OkStatus();
}

}  // namespace googlesql
