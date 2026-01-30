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

#include "googlesql/reference_impl/variable_generator.h"

#include <map>
#include <string>
#include <utility>
#include <vector>

#include "googlesql/reference_impl/parameters.h"
#include "googlesql/reference_impl/variable_id.h"
#include "googlesql/resolved_ast/resolved_column.h"
#include "absl/status/statusor.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/str_join.h"
#include "absl/strings/str_replace.h"
#include "googlesql/base/status_builder.h"

namespace googlesql {

VariableId VariableGenerator::GetNewVariableName(std::string suggested_name) {
  // Remove $ characters for readability. We use $ for printing variable names.
  absl::StrReplaceAll(
      {
          // Remove $ characters for readability.
          // We use $ for printing variable names.
          {"$", ""},
          // Remove @ characters for readability. @ is used for parameter names.
          {"@", ""},
          // Also replace dots because we use them for subscripting.
          {".", "_"},
      },
      &suggested_name);

  // To make variable names more readable, always make sure the prefix is
  // non-empty, but don't use $ or dot.
  if (suggested_name.empty()) {
    suggested_name = "_";
  }

  int& count = used_variable_names_[VariableId(suggested_name)];
  ++count;

  // If we encounter the same suggested name more than once, subscript it with
  // dot. We don't use _ for subscripting because it is frequently in the
  // suggested name argument to this function.
  if (count > 1) {
    absl::StrAppend(&suggested_name, ".", count);
  }

  return VariableId(suggested_name);
}

VariableId VariableGenerator::GetVariableNameFromSystemVariable(
    const std::vector<std::string>& name_path,
    SystemVariablesAlgebrizerMap* system_variables_map) {
  auto it = system_variables_map->find(name_path);
  if (it != system_variables_map->end()) {
    return (*it).second;
  }
  VariableId varname = GetNewVariableName(absl::StrJoin(name_path, "."));
  (*system_variables_map)[name_path] = varname;
  return varname;
}

VariableId VariableGenerator::GetVariableNameFromParameter(
    const std::string& parameter_name, ParameterMap* map) {
  ParameterMap::const_iterator it = map->find(parameter_name);
  if (it != map->end()) {
    return (*it).second;
  }
  VariableId varname = GetNewVariableName(parameter_name);
  (*map)[parameter_name] = varname;
  return varname;
}

VariableId VariableGenerator::GetVariableNameFromParameter(
    int parameter_position, ParameterList* parameter_list) {
  // Note that parameter positions are 1-based.
  if (parameter_position > parameter_list->size()) {
    parameter_list->resize(parameter_position);
  }
  VariableId& variable_name = (*parameter_list)[parameter_position - 1];
  if (!variable_name.is_valid()) {
    variable_name = GetNewVariableName(
        absl::StrCat("$positional_param_", parameter_position));
  }
  return variable_name;
}

VariableId ColumnToVariableMapping::AssignNewVariableToColumn(
    const ResolvedColumn& column) {
  VariableId varname = variable_gen_->GetNewVariableName(column.name());
  column_to_variable_[column] = varname;
  return varname;
}

VariableId ColumnToVariableMapping::GetVariableNameFromColumn(
    const ResolvedColumn& column) {
  const absl::StatusOr<VariableId> status_or_id =
      LookupVariableNameForColumn(column);
  if (status_or_id.ok()) return std::move(status_or_id).value();
  return AssignNewVariableToColumn(column);
}

absl::StatusOr<VariableId> ColumnToVariableMapping::LookupVariableNameForColumn(
    const ResolvedColumn& column) const {
  Map::const_iterator it = column_to_variable_.find(column);
  if (it != column_to_variable_.end()) {
    return (*it).second;
  }
  return googlesql_base::NotFoundErrorBuilder()
         << "Failed to find column: " << column.DebugString();
}

std::string ColumnToVariableMapping::DebugString() const {
  std::string debug_string;
  return absl::StrJoin(
      column_to_variable_, ", ",
      [](std::string* out, const std::pair<ResolvedColumn, VariableId>& pair) {
        absl::StrAppend(out, pair.first.DebugString(), " => ",
                        pair.second.ToString());
      });
  return debug_string;
}

}  // namespace googlesql
